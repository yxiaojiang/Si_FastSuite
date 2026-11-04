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
#else
#include <unistd.h>
#endif

#include <system_error>

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

std::string pathToUtf8(const std::filesystem::path& p) {
    if (p.empty()) return {};
    const auto genericNorm = p.lexically_normal();
#ifdef _WIN32
    const std::wstring w = genericNorm.generic_wstring();
    if (w.empty()) return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        const auto u8 = genericNorm.generic_u8string();
        return std::string(u8.begin(), u8.end());
    }
    std::string s(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), len, nullptr, nullptr);
    if (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
#else
    const auto u8 = genericNorm.generic_u8string();
    return std::string(u8.begin(), u8.end());
#endif
}

namespace {

std::filesystem::path getExecutableDirInternal() {
#ifdef _WIN32
    std::wstring buf(MAX_PATH, L'\0');
    DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    while (len >= buf.size()) {
        buf.resize(buf.size() * 2);
        len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    }
    buf.resize(len);
    std::filesystem::path exePath(buf);
    return exePath.parent_path();
#else
    std::error_code ec;
    auto p = std::filesystem::canonical("/proc/self/exe", ec);
    if (!ec && !p.empty()) {
        return p.parent_path();
    }
    return std::filesystem::current_path(ec);
#endif
}

} // namespace

bool isSandboxEnvironment() {
    std::error_code ec;
    const auto exeDir = getExecutableDirInternal();
    if (exeDir.empty()) return false;
    const bool hasScenes = std::filesystem::is_directory(exeDir / "SampleScenes", ec);
    const bool hasResources = std::filesystem::is_directory(exeDir / "resources", ec);
    return hasScenes && hasResources;
}

std::string resolveWorkspaceOrAppDir(const std::string& overrideRoot) {
    if (!overrideRoot.empty()) {
        return overrideRoot;
    }

    std::error_code ec;
    const auto exeDir = getExecutableDirInternal();
    if (!exeDir.empty()) {
        const bool hasScenes = std::filesystem::is_directory(exeDir / "SampleScenes", ec);
        const bool hasResources = std::filesystem::is_directory(exeDir / "resources", ec);
        if (hasScenes && hasResources) {
            return pathToUtf8(exeDir);
        }
    }

#ifdef OLP_SOURCE_DIR
    const auto srcPath = utf8ToPath(OLP_SOURCE_DIR);
    if (!srcPath.empty() && std::filesystem::is_directory(srcPath / "SampleScenes", ec)) {
        return pathToUtf8(srcPath);
    }
#endif

    if (!exeDir.empty()) {
        return pathToUtf8(exeDir);
    }

    return pathToUtf8(std::filesystem::current_path(ec));
}

std::string resolveSessionOutputDir(const std::string& wsRoot, bool forceSandbox) {
    if (forceSandbox || isSandboxEnvironment()) {
        std::error_code ec;
        const auto tempDir = std::filesystem::temp_directory_path(ec) / "SiFastSuiteOLP";
        std::filesystem::create_directories(tempDir, ec);
        return pathToUtf8(tempDir);
    }

    const std::string root = wsRoot.empty() ? resolveWorkspaceOrAppDir() : wsRoot;
    const auto devDir = utf8ToPath(root) / "build";
    std::error_code ec;
    std::filesystem::create_directories(devDir, ec);
    return pathToUtf8(devDir);
}

} // namespace olp::io
