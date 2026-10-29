#ifndef _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING 1
#endif

#include "olp/io/Utf8Path.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace olp::io {

std::filesystem::path utf8ToPath(const std::string& utf8Str) {
#ifdef _WIN32
    if (utf8Str.empty()) return {};
    const int wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), -1, nullptr, 0);
    if (wideLen <= 0) return std::filesystem::u8path(utf8Str);
    std::wstring wideStr(static_cast<size_t>(wideLen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8Str.c_str(), -1, wideStr.data(), wideLen);
    if (!wideStr.empty() && wideStr.back() == L'\0') wideStr.pop_back();
    return std::filesystem::path(wideStr);
#else
    return std::filesystem::u8path(utf8Str);
#endif
}

} // namespace olp::io
