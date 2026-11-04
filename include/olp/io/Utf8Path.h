#pragma once

#include <filesystem>
#include <string>

namespace olp::io {

// Convert a UTF-8 path string to std::filesystem::path.
// On Windows uses MultiByteToWideChar(CP_UTF8) so Chinese paths work under
// CP936/ACP narrow code pages; elsewhere uses std::filesystem::u8path.
std::filesystem::path utf8ToPath(const std::string& utf8Str);

// Convert a std::filesystem::path to a UTF-8 encoded std::string with generic forward slashes.
// On Windows uses WideCharToMultiByte(CP_UTF8); elsewhere uses generic_u8string().
std::string pathToUtf8(const std::filesystem::path& p);

// Detects whether the application is running in an installed/standalone sandbox environment
// (i.e. 'SampleScenes' and 'resources' exist alongside the executable).
bool isSandboxEnvironment();

// Resolves the workspace or application root directory returning a UTF-8 string.
// 1. If overrideRoot is non-empty, returns overrideRoot directly.
// 2. Probes the directory containing the currently executing binary:
//    if <exeDir>/SampleScenes and <exeDir>/resources exist, returns <exeDir>.
// 3. If not found and in development environment (OLP_SOURCE_DIR is defined and valid),
//    falls back to OLP_SOURCE_DIR.
// 4. Otherwise falls back to <exeDir> or current working directory.
std::string resolveWorkspaceOrAppDir(const std::string& overrideRoot = "");

// Resolves the output directory for sample session runs (e.g. WELD_001.ls & Preview.html).
// In sandbox environment (or when forceSandbox is true), writes to user temp directory
// (std::filesystem::temp_directory_path() / "SiFastSuiteOLP") to avoid UAC permissions
// and installation leftovers. In development environment, writes to <wsRoot>/build.
std::string resolveSessionOutputDir(const std::string& wsRoot = "", bool forceSandbox = false);

} // namespace olp::io
