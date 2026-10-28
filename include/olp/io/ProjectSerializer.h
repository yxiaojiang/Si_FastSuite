#pragma once

#include "olp/app/CommandManager.h"
#include <string>

namespace olp::io {

struct ProjectSerializeOptions {
    bool pretty{true};
    int indentSpaces{2};
    bool saveMeshes{true}; // When true, serializes render and collision meshes
};

class ProjectSerializer {
public:
    // Serializes a ProjectDocument to a JSON string.
    static std::string toJsonString(
        const app::ProjectDocument& doc,
        const ProjectSerializeOptions& options = {});

    // Deserializes a ProjectDocument from a JSON string.
    static bool fromJsonString(
        const std::string& jsonStr,
        app::ProjectDocument& outDoc,
        std::string* errorOut = nullptr);

    // Saves a ProjectDocument to a JSON file (.json or project.json).
    static bool saveToFile(
        const app::ProjectDocument& doc,
        const std::string& filePath,
        const ProjectSerializeOptions& options = {},
        std::string* errorOut = nullptr);

    // Loads a ProjectDocument from a JSON file.
    static bool loadFromFile(
        const std::string& filePath,
        app::ProjectDocument& outDoc,
        std::string* errorOut = nullptr);
};

} // namespace olp::io
