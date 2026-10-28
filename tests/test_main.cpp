#include "test_support.h"

#ifdef OLP_HAS_QT6
#include <QApplication>
#endif

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
        std::cerr << "  [FAIL] " << testName << (detail.empty() ? "" : (" -> " + detail)) << "\n";
    }
}


int main(int argc, char* argv[]) {
#ifdef OLP_HAS_QT6
    qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef OLP_QT_PLUGINS
    qputenv("QT_PLUGIN_PATH", OLP_QT_PLUGINS);
#endif
    QApplication app(argc, argv);
#endif
    std::cout << "====================================================================\n";
    std::cout << "  Running SiFastSuite OLP Automated Verification Suite\n";
    std::cout << "====================================================================\n";

     testWprAndGimbalLock();
     testUrdfAndStlLoading();
     testOpwKinematics100Samples();
     testSchemeAFullWorkcellImport();
     testOcctPerSolidNames();
     testOcctWeldEdgeMatchesBuiltin();
     testTJointCrossPartSeamAndCollisionExemption();
     testGizmoAndUndoRedo100Steps();
     testFanucLsGeneration();
     testPathEditsAndCommandGates();
  testSweepCollisionBetweenClearTargets();
     testApproachTransitClearsWall();
     testMainWindowZones();
    testBRepMeshRenderingAndEdgePicking();
    testTcpGizmoMountAndInteraction();
    testTangentContinuousEdgeChaining();
    testSeamToOperationAndSlerpStepping();
    testWeldingProcessEventsAndTorchSpinTuning();
    testKinematicsReachabilitySingularityAndTwistScan();
    testTrajectoryAndPlayback();
    testFanucLsPostProcessorValidationAndExport();
    testWorkcellPdfReportExport();
    testFclSinglePoseTiming();
    testProjectJsonSerialization();
    testTJointSampleSceneEndToEndRegression();

    std::cout << "\n====================================================================\n";
    std::cout << "  Summary: " << g_passed << " passed, " << g_failed << " failed.\n";
    std::cout << "====================================================================\n";
    return (g_failed == 0) ? 0 : 1;
}

