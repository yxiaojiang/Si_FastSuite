#pragma once

#include <filesystem>
#include <string>

namespace olp::io {

// Convert a UTF-8 path string to std::filesystem::path.
// On Windows uses MultiByteToWideChar(CP_UTF8) so Chinese paths work under
// CP936/ACP narrow code pages; elsewhere uses std::filesystem::u8path.
std::filesystem::path utf8ToPath(const std::string& utf8Str);

} // namespace olp::io
