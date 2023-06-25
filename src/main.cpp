#include "utils.h"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <optional>
#include <random>
#include <unordered_set>
#include <variant>
#include <vector>

namespace chr = std::chrono;
using clk = chr::steady_clock;
namespace fs = std::filesystem;

constexpr int kMaxCurrentSetSize = 7;

std::default_random_engine dre;

struct QA {
    std::string q, a;
};

// History item.
struct HItem {
    int qaIdx;
    std::optional<float> time;  // nullopt if failed
};

namespace QState
{
struct Unanswered {
};
struct Unstable {
};
struct WorstTime {
    float time;
};
using V = std::variant<Unanswered, Unstable, WorstTime>;
};  // namespace QState

struct State {
    std::vector<QA> qas;
    std::unordered_set<int> currentSet;
    std::optional<float> targetTime = 5.0f;
    std::vector<HItem> hs;  // History.
    bool echoLog = false;
    std::vector<std::string> log;
    // Return
    // - Unanswered if there were no answers yet
    // - Unstable if on the last 3 occassions there was a failure or there were less the 3 times
    // - Max time if the 3 latest answer were good
    QState::V GetQState(int qaIdx)
    {
        std::vector<float> ts;
        for (auto it = hs.rbegin(); it != hs.rend(); ++it) {
            if (it->qaIdx != qaIdx) {
                continue;
            }
            if (it->time) {
                ts.push_back(*it->time);
                if (ts.size() == 3) {
                    return QState::WorstTime{*std::max_element(ts.begin(), ts.end())};
                }
            } else {
                return QState::Unstable{};
            }
        }
        return ts.empty() ? QState::V(QState::Unanswered{}) : QState::V(QState::Unstable{});
    }
    void Log(std::string x)
    {
        if (echoLog) {
            fmt::print("// {}\n", x);
        }
        log.push_back(std::move(x));
    }
};

nlohmann::json StateToJson(const State& s)
{
    nlohmann::json j = nlohmann::json::object();
    auto jqas = nlohmann::json::array();
    for (auto& x : s.qas) {
        jqas.push_back(nlohmann::json({{"q", x.q}, {"a", x.a}}));
    }
    j["qas"] = std::move(jqas);
    auto jcurrentSet = nlohmann::json::array();
    for (auto& x : s.currentSet) {
        jcurrentSet.push_back(x);
    }
    j["currentSet"] = std::move(jcurrentSet);
    auto jtargetTime = nlohmann::json();
    if (s.targetTime) {
        jtargetTime = *s.targetTime;
    }
    j["targetTime"] = jtargetTime;
    auto jhs = nlohmann::json::array();
    for (auto& x : s.hs) {
        auto time = nlohmann::json();
        if (x.time) {
            time = *x.time;
        }
        jhs.push_back(nlohmann::json({{"qaIdx", x.qaIdx}, {"time", time}}));
    }
    j["hs"] = std::move(jhs);
    auto jlog = nlohmann::json::array();
    for (auto& x : s.log) {
        jlog.push_back(x);
    }
    j["log"] = std::move(jlog);
    return j;
}

HItem HItemFromJson(const nlohmann::json& j)
{
    std::optional<float> time;
    auto& jtime = j.at("time");
    if (!jtime.is_null()) {
        time = jtime.get<float>();
    }
    return HItem{.qaIdx = j.at("qaIdx"), .time = time};
}

QA QAFromJson(const nlohmann::json& j)
{
    return QA{.q = j.at("q").get<std::string>(), .a = j.at("a").get<std::string>()};
}

State StateFromJson(const nlohmann::json& j)
{
    State s;
    for (auto& x : j.at("qas")) {
        s.qas.push_back(QAFromJson(x));
    }
    for (auto& x : j.at("currentSet")) {
        s.currentSet.insert(x.get<int>());
    }
    if (!j.at("targetTime").is_null()) {
        s.targetTime = j.at("targetTime").get<float>();
    }
    for (auto& x : j.at("hs")) {
        s.hs.push_back(HItemFromJson(x));
    }
    for (auto& x : j.at("log")) {
        s.log.push_back(x.get<std::string>());
    }
    return s;
}

int GenerateQuestion(State& s)
{
    assert(!s.currentSet.empty());
    for (;;) {
        int csidx = std::uniform_int_distribution<int>(0, int(s.currentSet.size() - 1))(dre);
        std::vector<int> cs(s.currentSet.begin(), s.currentSet.end());
        auto qaIdx = cs[csidx];
        if (s.hs.empty() || s.hs.back().qaIdx != qaIdx) {
            return qaIdx;
        }
    }
}

void UpdateWithAnswer(State& s, int qaIdx, std::optional<float> time)
{
    s.hs.push_back(HItem{.qaIdx = qaIdx, .time = time});
    // Update targetTime
    if (!s.targetTime) {
        // Establish target time, set it to the median of the times of the first valid answer of
        // each qa in current set.
        std::vector<float> times;
        for (auto x : s.currentSet) {
            // Get first time of this qa
            for (auto& hi : s.hs) {
                if (hi.qaIdx == x && hi.time) {
                    times.push_back(*hi.time);
                    break;
                }
            }
        }
        if (times.size() == s.currentSet.size()) {
            auto m = times.begin() + times.size() / 2;
            std::nth_element(times.begin(), m, times.end());
            s.targetTime = *m;
            s.Log(fmt::format("Establish target time to median: {}", *m));
        } else {
            s.Log(fmt::format("Couldn't establish target time for current set results: {}",
                              fmt::join(times, " ")));
        }
    }
    if (!s.targetTime) {
        // Nothing to do until we don't have a target time.
        s.Log("No change in current set");
        return;
    }
    // Remove an item which was 3 times under target time.
    float maxTime = *s.targetTime;
    std::optional<int> bestIdx;
    for (auto idx : s.currentSet) {
        auto qs = s.GetQState(idx);
        if (auto* wt = std::get_if<QState::WorstTime>(&qs)) {
            if (wt->time <= maxTime) {
                maxTime = wt->time;
                bestIdx = idx;
            }
        }
    }
    if (!bestIdx) {
        // No such idx, keep on asking current set.
        s.Log(fmt::format(
            "Not removing item, all items in current set are unstable or above target time {}",
            *s.targetTime));
        return;
    }
    s.Log(fmt::format(
        "Removing {}, it's time {} <= target time {}", s.qas[*bestIdx].q, maxTime, *s.targetTime));
    fmt::print("CONGRATULATIONS! You seem to know that {} = {} very well!\n",
               s.qas[*bestIdx].q,
               s.qas[*bestIdx].a);
    s.currentSet.erase(*bestIdx);
    assert(s.currentSet.size() < kMaxCurrentSetSize);
    // Pick a new question. Prefer
    // 1. unstable
    // 2. unanswered
    // 3. with longest time
    std::vector<int> unstableOnes;
    std::vector<int> unansweredOnes;
    std::vector<std::pair<float, int>> withTimes;
    for (int i = 0; i < s.qas.size(); ++i) {
        if (s.currentSet.contains(i)) {
            continue;
        }
        switch_variant(
            s.GetQState(i),
            [&unstableOnes, i](QState::Unanswered) {
                unstableOnes.push_back(i);
            },
            [&unansweredOnes, i](QState::Unstable) {
                unansweredOnes.push_back(i);
            },
            [&withTimes, i](QState::WorstTime wt) {
                withTimes.emplace_back(wt.time, i);
            });
    }
    s.Log(fmt::format("Finding new q, unstable: {}, unanswered: {}, with times: {}",
                      unstableOnes.size(),
                      unansweredOnes.size(),
                      withTimes.size()));
    std::optional<int> newQaIdx;
    if (!unstableOnes.empty()) {
        newQaIdx =
            unstableOnes[std::uniform_int_distribution<int>(0, int(unstableOnes.size() - 1))(dre)];
        s.Log(fmt::format("Picking unstable {}", s.qas[*newQaIdx].q));
    } else if (!unansweredOnes.empty()) {
        newQaIdx = unansweredOnes[std::uniform_int_distribution<int>(
            0, int(unansweredOnes.size() - 1))(dre)];
        s.Log(fmt::format("Picking unanswered {}", s.qas[*newQaIdx].q));
    } else {
        assert(!withTimes.empty());
        std::sort(withTimes.begin(), withTimes.end());
        auto& b = withTimes.back();
        newQaIdx = b.second;
        s.Log(fmt::format("Picking with worst time {}", s.qas[*newQaIdx].q));
        if (b.first < s.targetTime) {
            s.Log(fmt::format("Reducing target time {} -> {}", *s.targetTime, b.first));
            s.targetTime = b.first;
        }
    }
    assert(newQaIdx);
    s.currentSet.insert(*newQaIdx);
}

int main(int argc, const char* argv[])
{
    // start koppany-plus10
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        args.push_back(argv[i]);
    }
    if (args.size() != 1) {
        fprintf(stderr, "Usage szorzotable <name-ops>\n");
        exit(1);
    }

    auto filename = fs::path(fmt::format("{}.json", args[0]));
    State state;
    if (auto fileContents = ReadFileIntoString(filename, false)) {
        state = StateFromJson(nlohmann::json::parse(*fileContents));
    } else {
        std::vector<QA> qas;
        for (int i = 1; i <= 10; ++i) {
            for (int j = 1; j <= 10; ++j) {
                qas.push_back(QA{.q = fmt::format("{} + {}", i, j), .a = fmt::format("{}", i + j)});
            }
        }

        assert(qas.size() >= kMaxCurrentSetSize);
        std::unordered_set<int> currentSet;
        while (currentSet.size() < kMaxCurrentSetSize) {
            int x = std::uniform_int_distribution<int>(0, int(qas.size() - 1))(dre);
            currentSet.insert(x);
        }
        state = State{.qas = qas, .currentSet = currentSet};

        std::vector<std::string> currentSetQs;
        for (auto qix : state.currentSet) {
            currentSetQs.push_back(state.qas[qix].q);
        }
        state.Log(fmt::format("Initialized with current set: {}", fmt::join(currentSetQs, ", ")));
    }

    bool first = true;
    for (;;) {
        auto qaIdx = GenerateQuestion(state);
        auto& qa = state.qas[qaIdx];
        bool failure = false;
        std::optional<float> time;
        for (;;) {
            fmt::print("{} = ", qa.q);
            auto t0 = clk::now();
            std::string a;
            std::cin >> a;
            auto t1 = clk::now();
            a = stripSpace(a);
            if (a != qa.a) {
                fmt::print("Think again, ");
                failure = true;
                continue;
            }
            if (!failure) {
                time = chr::duration<float>(t1 - t0).count();
            }
            break;
        }
        if (first) {
            state.Log("Skipping first answer");
            first = false;
            continue;
        }
        if (time) {
            state.Log(fmt::format("Got answer with time {} sec", *time));
        } else {
            state.Log("Got failed answer");
        }
        auto ttbefore = state.targetTime;
        UpdateWithAnswer(state, qaIdx, time);
        auto j = StateToJson(state);
        std::ofstream(filename) << std::setw(4) << j << std::endl;
        if (state.targetTime != ttbefore) {
            fmt::print("CONGRATULATIONS, you know all the numbers! Bye!\n");
            exit(0);
        }
    }
}
