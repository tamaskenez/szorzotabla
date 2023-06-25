#include "utils.h"

#include <fmt/format.h>
#include <fmt/std.h>

#include <cctype>

namespace fs = std::filesystem;

std::string_view stripSpace(std::string_view x)
{
    while (!x.empty() && isspace(x[0])) {
        x.remove_prefix(1);
    }
    while (!x.empty() && isspace(x.back())) {
        x.remove_suffix(1);
    }
    return x;
}

std::optional<std::string> ReadFileIntoString(const fs::path& path, bool binaryMode)
{
    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        fmt::print(stderr, "Can't get file size for {}, reason: {}\n", path, ec.message());
        return std::nullopt;
    }
    std::ios::openmode openMode = std::ios::in;
    if (binaryMode) {
        openMode |= std::ios::binary;
    }
    std::ifstream file(path, openMode);
    if (!file) {
        fmt::print(stderr, "Can't open file {}\n", path);
        return std::nullopt;
    }
    std::string data;
    data.reserve(size);
    data.append(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return data;
}
