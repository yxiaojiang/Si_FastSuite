#pragma once

#include "olp/ui/MainWindow.h"
#include <string>
#include <vector>

namespace olp::ui {

// The console verification path and the main window share this sample cell.
struct SampleSessionResult {
    int exitCode{1};
    int frameCount{0};
    double cycleTimeSec{0.0};
    double weldTimeSec{0.0};
    std::string summary{};
    std::vector<std::string> issues{};
};

SampleSessionResult runSampleWorkcell(MainWindow& window);

} // namespace olp::ui
