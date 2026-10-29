#pragma once

#include "olp/core/ProjectModel.h"
#include <string>
#include <vector>

namespace olp::io {

struct LsPostConfig {
    std::string owner{"MNEDITOR"};
    std::string defaultGroup{"1,*,*,*,*"};
    bool includeArcWeldingAppl{true};
    bool appendReturnHome{true};
    bool emitHomeAsJointTarget{false};
    bool blockOnCollision{true};
    bool blockOnConfigJump{true};
    bool blockOnUnreachable{true};
    int maxCommentLength{16};
    int maxProgramCommentLength{32};
    int defaultUFrame{1};
    int defaultUTool{1};
};

struct LsValidationIssue {
    enum class Severity { Error, Warning };
    Severity severity{Severity::Error};
    std::string operationId{};
    std::string targetId{};
    std::string message{};
};

struct LsValidationResult {
    bool ok{true};
    std::vector<LsValidationIssue> issues{};

    bool hasErrors() const {
        for (const auto& is : issues) {
            if (is.severity == LsValidationIssue::Severity::Error) return true;
        }
        return false;
    }
};

// M12: FANUC ASCII .ls Post-Processor
class FanucLsPostProcessor {
public:
    // Validate FANUC TP program name (uppercase letter start, alphanumeric/underscore, <= 16 chars)
    static bool validateProgramName(const std::string& progName, std::string& outError);

    // Validate entire program readiness for FANUC .ls export
    static LsValidationResult validateProgram(
        const core::Program& program,
        const LsPostConfig& config = {});

    // Generate complete FANUC .ls text content from Program
    static std::string generateLsString(
        const core::Program& program,
        const LsPostConfig& config = {},
        std::string* errorOut = nullptr);

    // Export single .ls file to disk
    static bool exportToFile(
        const core::Program& program,
        const std::string& outputFilePath,
        const LsPostConfig& config = {},
        std::string* errorOut = nullptr);

    // Export all programs in a controller to directory
    static bool exportControllerPrograms(
        const core::ControllerUnit& controller,
        const std::string& outputDir,
        const LsPostConfig& config = {},
        std::vector<std::string>* exportedFiles = nullptr,
        std::vector<std::string>* errors = nullptr);
};

} // namespace olp::io
