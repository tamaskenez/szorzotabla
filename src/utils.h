#pragma once

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>

std::string_view stripSpace(std::string_view x);

template<typename T>
bool contains(std::vector<T>& xs, const T& x)
{
    return std::find(xs.begin(), xs.end(), x) != xs.end();
}

template<typename... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};

template<typename... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

template<typename... Ts, typename Variant>
auto switch_variant(Variant&& variant, Ts&&... ts)
{
    return std::visit(overloaded{std::forward<Ts>(ts)...}, std::forward<Variant>(variant));
}

std::optional<std::string> ReadFileIntoString(const std::filesystem::path& path, bool binaryMode);
