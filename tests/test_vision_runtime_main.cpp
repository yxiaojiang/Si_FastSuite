#include "test_support.h"

#include <iostream>
#include <string>

int g_passed = 0;
int g_failed = 0;

void check(bool cond, const std::string& testName, const std::string& detail) {
    if (cond) {
        ++g_passed;
        std::cout << "  [PASS] " << testName << "\n";
    } else {
        ++g_failed;
        std::cerr << "  [FAIL] " << testName
                  << (detail.empty() ? "" : (" -> " + detail)) << "\n";
    }
}

int main() {
    std::cout << "====================================================================\n";
    std::cout << "  SiFastSuite Vision_Edge Runtime Tests (VE-D1..D7 headless)\n";
    std::cout << "====================================================================\n";

    testVisionRuntimeDeviceMocks();
    testVisionHandEyeCalibration();
    testVisionBoardPerception();
    testVisionBoardOutlineFit();
    testVisionEdgeRollPath();
    testVisionEdgeRollValidateExportMock();
    testVisionEdgeDemoOrchestrateAB();

    std::cout << "\n====================================================================\n";
    std::cout << "  Summary: " << g_passed << " passed, " << g_failed << " failed.\n";
    std::cout << "====================================================================\n";
    return (g_failed == 0) ? 0 : 1;
}
