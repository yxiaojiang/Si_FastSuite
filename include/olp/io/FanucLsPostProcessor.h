#pragma once

#include "olp/core/ProjectModel.h"
#include <string>

namespace olp::io {

struct LsPostConfig {
    std::string owner{"MNEDITOR"};
    std::string defaultGroup{"1,*,*,*,*"};
    bool includeArcWeldingAppl{true};
    bool appendReturnHome{true};
};

// M12: FANUC ASCII .ls Post-Processor
class FanucLsPostProcessor {
public:
    // Validate FANUC TP program name (uppercase letter start, alphanumeric/underscore, <= 16 chars)
    static bool validateProgramName(const std::string& progName, std::string& outError);

    // Generate complete FANUC .ls text content from Program
    static std::string generateLsString(
        const core::Program& program,
        const LsPostConfig& config = {},
        std::string* errorOut = nullptr);

    // Export .ls file to disk
    static bool exportToFile(
        const core::Program& program,
        const std::string& outputFilePath,
        const LsPostConfig& config = {});
};

} // namespace olp::io
