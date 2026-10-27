#pragma once

#include <string>

extern int g_passed;
extern int g_failed;

void check(bool cond, const std::string& testName, const std::string& detail = "");

void testWprAndGimbalLock();
void testUrdfAndStlLoading();
void testOpwKinematics100Samples();
void testSchemeAFullWorkcellImport();
void testOcctPerSolidNames();
void testOcctWeldEdgeMatchesBuiltin();
void testTJointCrossPartSeamAndCollisionExemption();
void testGizmoAndUndoRedo100Steps();
void testFanucLsGeneration();
void testPathEditsAndCommandGates();
void testSweepCollisionBetweenClearTargets();
void testApproachTransitClearsWall();
void testMainWindowZones();
void testBRepMeshRenderingAndEdgePicking();
void testTcpGizmoMountAndInteraction();
void testTangentContinuousEdgeChaining();
void testSeamToOperationAndSlerpStepping();
void testWeldingProcessEventsAndTorchSpinTuning();
void testKinematicsReachabilitySingularityAndTwistScan();
void testTrajectoryAndPlayback();
void testFanucLsPostProcessorValidationAndExport();
void testWorkcellPdfReportExport();
