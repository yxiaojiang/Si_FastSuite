#include "olp/algo/CollisionAndSim.h"
#include "olp/algo/OpwKinematics.h"
#include "olp/algo/SeamAndToolpathAlgo.h"
#include "olp/app/CommandManager.h"
#include "olp/app/WorkcellLayoutService.h"
#include "olp/cad/OcctCadKernel.h"
#include "olp/device/RobotModel.h"
#include "olp/io/FanucLsPostProcessor.h"
#include "olp/ui/MainWindow.h"

#ifdef OLP_HAS_QT6
#include "olp/ui/ShellWindow.h"
#include <QApplication>
#include <QStringList>
#endif

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_passed = 0;
int g_failed = 0;

void check(bool cond, const std::string& testName, const std::string& detail = "") {
    if (cond) {
        ++g_passed;
        std::cout << "  [PASS] " << testName << "\n";
    } else {
        ++g_failed;
        std::cerr << "  [FAIL] " << testName << (detail.empty() ? "" : (" -> " + detail)) << "\n";
    }
}

void testWprAndGimbalLock() {
    std::cout << "\n--- Test 1: FANUC WPR Conversion & Gimbal Lock (Section 6.4) ---\n";
    const olp::core::WprPose inPose{450.25, -120.50, 310.00, 180.0, -45.0, 30.0};
    const auto T = olp::core::Transform4d::fromWPR(inPose);
    const auto outPose = T.toWPR();
    const auto T2 = olp::core::Transform4d::fromWPR(outPose);

    const double posErr = (T.pos - T2.pos).norm();
    const double rotErrDeg = olp::core::rad2deg(T.rot.angularDistanceRad(T2.rot));
    check(posErr < 1e-9 && rotErrDeg < 1e-7,
          "Standard WPR round-trip precision (< 1e-7 deg)",
          "rotErrDeg=" + std::to_string(rotErrDeg));

    // Gimbal lock at P = +90 deg and P = -90 deg
    const olp::core::WprPose gimbalPose{100.0, 200.0, 300.0, 25.0, 90.0, -40.0};
    const auto Tg1 = olp::core::Transform4d::fromWPR(gimbalPose);
    const auto Tg2 = olp::core::Transform4d::fromWPR(Tg1.toWPR());
    const double gimbalRotErrDeg = olp::core::rad2deg(Tg1.rot.angularDistanceRad(Tg2.rot));
    check(gimbalRotErrDeg < 1e-6,
          "Gimbal lock (P = +90 deg) rotation matrix round-trip",
          "gimbalRotErrDeg=" + std::to_string(gimbalRotErrDeg));
}

void testUrdfAndStlLoading() {
    std::cout << "\n--- Test 2: SampleScenes Scene1 URDF & STL Mesh Loading (M02/M05) ---\n";
    const std::string ws = OLP_SOURCE_DIR;
    const std::string urdfPath =
        ws + "/SampleScenes/Scene1_TJoint_And_FANUC_M10iA/Robot_FANUC_M10iA/urdf/fanuc_m10ia_standalone.urdf";

    auto robotOpt = olp::device::RobotModel::loadFromUrdf(urdfPath);
    check(robotOpt.has_value(), "Load fanuc_m10ia_standalone.urdf");
    if (robotOpt) {
        std::ifstream jsonFile(ws + "/resources/robots/fanuc_m10ia.json");
        std::stringstream jsonText;
        jsonText << jsonFile.rdbuf();
        const std::string jsonBody = jsonText.str();
        const auto model = olp::device::RobotModel::createFanucM10iA();
        check(jsonBody.find("-90.0") != std::string::npos &&
              std::abs(model.opw.offsetsRad[2] + olp::core::kPi * 0.5) < 1e-9 &&
              std::abs(model.opw.offsetsRad[5] - olp::core::kPi) < 1e-9,
              "M-10iA joint offsets are loaded from fanuc_m10ia.json");

        olp::cad::OcctCadKernel cad;
        olp::core::TriMesh rawMesh;
        olp::core::TriMesh baseMesh;
        const bool rawOk = cad.importStl(robotOpt->visualMeshFiles[0], rawMesh);
        const bool stlOk = cad.importRobotStlMillimeters(robotOpt->visualMeshFiles[0], baseMesh);
        check(rawOk && rawMesh.bboxMax.z < 2.0,
              "STL loader keeps file units (base_link stays in meters)");
        check(stlOk && baseMesh.bboxMax.z > 270.0 && baseMesh.bboxMax.z < 290.0,
              "Robot STL loader converts meters to millimeters (base height " +
              std::to_string(baseMesh.bboxMax.z) + " mm)");

        std::ifstream urdf(urdfPath);
        std::stringstream urdfText;
        urdfText << urdf.rdbuf();
        const std::string urdfBody = urdfText.str();
        const auto upperPos = urdfBody.find("upper=\"");
        double fileUpper = 0.0;
        if (upperPos != std::string::npos) {
            fileUpper = std::stod(urdfBody.substr(upperPos + 7));
        }
        check(std::abs(robotOpt->jointLimits[0].upperRad - fileUpper) < 1e-4,
              "URDF joint limit is read from the file, not a twin constant");
    }
}

void testOpwKinematics100Samples() {
    std::cout << "\n--- Test 3: OPW FK/IK 100-Sample Closed-Loop & J2/J3 Coupling (Section 11.1) ---\n";
    const auto model = olp::device::RobotModel::createFanucM10iA();
    olp::algo::OpwKinematics kin(model);
    const auto utool = olp::core::Transform4d::fromWPR({0.0, 0.0, 160.0, 0.0, 35.0, 0.0});

    double maxPosErrMm = 0.0;
    double maxRotErrDeg = 0.0;
    double maxJointRecoveryErrDeg = 0.0;
    int successCount = 0;

    for (int i = 0; i < 100; ++i) {
        const double t = static_cast<double>(i) / 100.0;
        const std::array<double, 6> testJointsRad = {
            -1.2 + 2.4 * t,
            -0.2 + 0.5 * std::sin(i * 0.37),
            -0.2 + 0.4 * std::cos(i * 0.29),
            2.5 * std::sin(i * 0.53),
            -1.2 - 0.3 * std::cos(i * 0.41), // Keep J5 away from 0 to test non-singular posture recovery
            -2.0 + 4.0 * t
        };

        const auto fk = kin.forwardKinematics(testJointsRad, utool);
        const auto ikSols = kin.inverseKinematics(fk.tcpPoseInBase, utool, false);
        if (ikSols.empty()) continue;

        // Verify all IK solutions map back to the exact target TCP pose
        for (const auto& sol : ikSols) {
            const auto fkCheck = kin.forwardKinematics(sol.jointsRad, utool);
            const double pErr = (fkCheck.tcpPoseInBase.pos - fk.tcpPoseInBase.pos).norm();
            const double rErr = olp::core::rad2deg(
                fkCheck.tcpPoseInBase.rot.angularDistanceRad(fk.tcpPoseInBase.rot));
            maxPosErrMm = std::max(maxPosErrMm, pErr);
            maxRotErrDeg = std::max(maxRotErrDeg, rErr);
        }

        // Verify selectBestSolution recovers the exact original joint angles
        const auto recovered = kin.selectBestSolution(ikSols, testJointsRad, fk.config, true);
        if (recovered.has_value()) {
            ++successCount;
            for (size_t k = 0; k < 6; ++k) {
                const double jErrDeg = olp::core::rad2deg(
                    std::abs(olp::core::normalizeAngleRad(recovered->jointsRad[k] - testJointsRad[k])));
                maxJointRecoveryErrDeg = std::max(maxJointRecoveryErrDeg, jErrDeg);
            }
        }
    }

    check(successCount == 100 && maxPosErrMm < 0.01 && maxRotErrDeg < 0.001 && maxJointRecoveryErrDeg < 0.001,
          "100-Sample FK(IK(x)) = x (maxPosErr=" + std::to_string(maxPosErrMm) +
          "mm, maxRotErr=" + std::to_string(maxRotErrDeg) + "deg)",
          "successCount=" + std::to_string(successCount) +
          ", maxJointRecoveryErrDeg=" + std::to_string(maxJointRecoveryErrDeg));

    auto rotAbout = [](olp::core::Vec3 axis, double angle) {
        const double n = axis.norm();
        axis = axis * (1.0 / n);
        const double c = std::cos(angle);
        const double s = std::sin(angle);
        const double t = 1.0 - c;
        olp::core::Mat3 R;
        R.m[0][0] = t * axis.x * axis.x + c;
        R.m[0][1] = t * axis.x * axis.y - s * axis.z;
        R.m[0][2] = t * axis.x * axis.z + s * axis.y;
        R.m[1][0] = t * axis.x * axis.y + s * axis.z;
        R.m[1][1] = t * axis.y * axis.y + c;
        R.m[1][2] = t * axis.y * axis.z - s * axis.x;
        R.m[2][0] = t * axis.x * axis.z - s * axis.y;
        R.m[2][1] = t * axis.y * axis.z + s * axis.x;
        R.m[2][2] = t * axis.z * axis.z + c;
        return R;
    };
    const olp::core::Mat3 tool0Rot =
        olp::core::Mat3::rotZ(0.0) * olp::core::Mat3::rotY(-1.570796) * olp::core::Mat3::rotX(3.14159);
    auto urdfFlange = [&](const std::array<double, 6>& urdfQ) {
        const olp::core::Vec3 origins[6] = {
            {0, 0, 450}, {150, 0, 0}, {0, 0, 600}, {0, 0, 150}, {640, 0, 0}, {100, 0, 0}
        };
        const olp::core::Vec3 axes[6] = {
            {0, 0, 1}, {0, 1, 0}, {0, -1, 0}, {-1, 0, 0}, {0, -1, 0}, {-1, 0, 0}
        };
        olp::core::Transform4d T = olp::core::Transform4d::identity();
        for (int j = 0; j < 6; ++j) {
            olp::core::Transform4d joint;
            joint.pos = origins[j];
            joint.rot = rotAbout(axes[j], urdfQ[static_cast<size_t>(j)]);
            T = T * joint;
        }
        T.rot = T.rot * tool0Rot;
        return T;
    };

    double maxGoldMm = 0.0;
    double maxGoldAxis = 0.0;
    unsigned seed = 2026u;
    for (int i = 0; i < 200; ++i) {
        std::array<double, 6> display{};
        if (i == 0) {
            display = {0, 0, 0, 0, 0, 0};
        } else if (i == 1) {
            display = {0, olp::core::deg2rad(10.0), 0, 0, 0, 0};
        } else {
            for (double& q : display) {
                seed = seed * 1664525u + 1013904223u;
                const double unit = static_cast<double>(seed % 10000) / 10000.0;
                q = olp::core::deg2rad(-80.0 + 160.0 * unit);
            }
            display[4] = olp::core::deg2rad(-60.0);
        }
        std::array<double, 6> urdf = display;
        urdf[2] = display[2] + display[1];
        const auto gold = urdfFlange(urdf);
        const auto fk = kin.forwardKinematics(display);
        maxGoldMm = std::max(maxGoldMm, (fk.flangePoseInBase.pos - gold.pos).norm());
        maxGoldAxis = std::max(maxGoldAxis, (fk.flangePoseInBase.rot.colZ() - gold.rot.colZ()).norm());
    }
    check(maxGoldMm < 0.01 && maxGoldAxis < 1e-4,
          "URDF-chain gold: flange position and tool0 Z stay within 0.01 mm",
          "maxPos=" + std::to_string(maxGoldMm) + " maxAxis=" + std::to_string(maxGoldAxis));
}

void testSchemeAFullWorkcellImport() {
    std::cout << "\n--- Test 4: Section 3.4 Scheme A Full Workcell STEP Import & Robot Replacement (Section 11.7) ---\n";
    olp::app::ProjectDocument doc;
    olp::cad::OcctCadKernel cad;
    const auto robotModel = olp::device::RobotModel::createFanucM10iA();
    const std::string stepPath =
        std::string(OLP_SOURCE_DIR) + "/SampleScenes/Scene2_Full_Workcell_STEP/Full_Welding_Cell_With_Robot.step";

    const auto res = olp::app::WorkcellLayoutService::importFullSceneAndReplaceRobot(
        doc, cad, stepPath, robotModel);

    check(res.success && res.totalSolidsImported == 12 && res.staticRobotSolidsHidden == 5,
          "Import 12 solids from Full_Welding_Cell_With_Robot.step and hide 5 static robot solids");

    // Pedestal top is at (0, 0, 500) mm; verify alignment deviation < 1.0 mm (Section 11.7)
    const olp::core::Vec3 expectedBase{0.0, 0.0, 500.0};
    const double baseDevMm = (res.detectedRobotBaseTransform.pos - expectedBase).norm();
    check(baseDevMm < 1.0,
          "Replaced movable FANUC M-10iA base alignment error < 1 mm (actual=" +
          std::to_string(baseDevMm) + " mm)");

    int frontHandle = -1;
    std::vector<int> workpieceHandles;
    for (const auto& node : doc.sceneNodes) {
        if (node.type == olp::core::SceneNodeType::Part &&
            node.collisionRole == olp::core::CollisionRole::Workpiece) {
            workpieceHandles.push_back(node.cadHandleId);
            if (node.name.find("Workpiece_BoxWall_Front") != std::string::npos) {
                frontHandle = node.cadHandleId;
            }
        }
    }
    const auto edges = cad.getEdges(frontHandle);
    auto seamOpt = olp::algo::SeamAndToolpathAlgo::buildSeamFromPickedEdge(
        cad, frontHandle, 3, workpieceHandles, 50.0);
    const bool endpointCovered = seamOpt.has_value() && !seamOpt->sampledFrames.empty() &&
        edges.size() > 3 &&
        std::abs(seamOpt->sampledFrames.back().arcLengthMm -
                 (edges[3].lengthMm - seamOpt->endTrimMm)) < 0.05;
    check(endpointCovered, "Seam sampler always includes the trimmed endpoint");

    const char* curvedFiles[] = {
        "/SampleScenes/Scene1_TJoint_And_FANUC_M10iA/Workpiece_PipeFlange_DN15.step",
        "/SampleScenes/Scene1_TJoint_And_FANUC_M10iA/Workpiece_U_Bracket_L70.step",
        "/SampleScenes/Scene2_Full_Workcell_STEP/Workpiece_SquareTube_80x80x5.step",
        "/SampleScenes/Scene2_Full_Workcell_STEP/Workpiece_SteelHanger_SAET200.step"
    };
#ifdef OLP_HAS_OCCT
    bool curvedOk = true;
    int curvedNonLinearEdges = 0;
    int curvedNonPlaneFaces = 0;
    for (const char* rel : curvedFiles) {
        std::vector<olp::core::SceneNode> curved;
        if (!cad.importStep(std::string(OLP_SOURCE_DIR) + rel, curved) || curved.empty()) {
            curvedOk = false;
            continue;
        }
        for (const auto& node : curved) {
            for (const auto& edge : cad.getEdges(node.cadHandleId)) {
                if (!edge.isLinear && edge.lengthMm > 0.1) ++curvedNonLinearEdges;
            }
            for (const auto& face : cad.getFaces(node.cadHandleId)) {
                if (face.surfaceType != olp::cad::CadSurfaceType::Plane) ++curvedNonPlaneFaces;
            }
        }
    }
    check(curvedOk && curvedNonLinearEdges > 0 && curvedNonPlaneFaces > 0,
          "OCCT imports the four curved FreeCAD files with non-linear edges and non-plane faces",
          "edges=" + std::to_string(curvedNonLinearEdges) +
          " faces=" + std::to_string(curvedNonPlaneFaces));
#else
    bool curvedRejected = true;
    for (const char* rel : curvedFiles) {
        std::vector<olp::core::SceneNode> curved;
        if (cad.importStep(std::string(OLP_SOURCE_DIR) + rel, curved)) {
            curvedRejected = false;
        }
    }
    check(curvedRejected, "Headless STEP import rejects the four curved FreeCAD files");
#endif

    auto seamTrim = olp::algo::SeamAndToolpathAlgo::buildSeamFromPickedEdge(
        cad, frontHandle, 3, workpieceHandles, 50.0);
    olp::core::Operation trimOp;
    if (seamTrim.has_value()) {
        trimOp.targetWorkpieceId = seamTrim->face1PartId;
        trimOp.exemptionPartIds = {seamTrim->face1PartId, seamTrim->face2PartId};
    }
    trimOp.genParams.travelAngleDeg = 10.0;
    trimOp.genParams.approachDistMm = 45.0;
    trimOp.genParams.retractDistMm = 45.0;
    if (seamTrim.has_value()) {
        olp::algo::SeamAndToolpathAlgo::generateOperationTargets(
            *seamTrim, olp::core::Transform4d::identity(), trimOp);
    }
    const auto trimRobot = olp::device::RobotModel::createFanucM10iA();
    olp::algo::OpwKinematics trimKin(trimRobot);
    olp::core::UnitsSettings trimSettings;
    olp::algo::CollisionAndSimEngine trimEngine(trimKin, trimSettings);
    const auto beforeTrim = trimEngine.measureOperationTorchClearance(
        trimOp, olp::core::Transform4d::identity(), doc.sceneNodes);
    check(seamTrim.has_value() && beforeTrim.minClearanceMm < 0.0 &&
              beforeTrim.partName == "Table_Clamp_Block_1",
          "Default 5 mm trim puts the torch into Table_Clamp_Block_1",
          "clearance=" + std::to_string(beforeTrim.minClearanceMm) + " part=" + beforeTrim.partName);

    const bool cleared = seamTrim.has_value() &&
        olp::algo::SeamAndToolpathAlgo::extendStartTrimUntilTorchClears(
            cad, frontHandle, 3, workpieceHandles, 50.0, trimEngine,
            olp::core::Transform4d::identity(), doc.sceneNodes, *seamTrim, trimOp);
    const auto afterTrim = trimEngine.measureOperationTorchClearance(
        trimOp, olp::core::Transform4d::identity(), doc.sceneNodes);
    check(cleared && afterTrim.meetsSafeDistance && seamTrim.has_value() && seamTrim->startTrimMm > 5.0,
          "Seam start trim moves off the clamp while keeping the fillet torch angle",
          "trim=" + (seamTrim ? std::to_string(seamTrim->startTrimMm) : "none") +
          " clearance=" + std::to_string(afterTrim.minClearanceMm) + " part=" + afterTrim.partName);
}

void testOcctPerSolidNames() {
    std::cout << "\n--- Test 4b: OCCT per-solid STEP names ---\n";
#ifdef OLP_HAS_OCCT
    olp::cad::OcctCadKernel cad;
    const std::string fullPath = std::string(OLP_SOURCE_DIR) +
        "/SampleScenes/Scene2_Full_Workcell_STEP/Full_Welding_Cell_With_Robot.step";
    std::vector<olp::core::SceneNode> fullNodes;
    const bool fullOk = cad.importStepViaOcct(fullPath, fullNodes);
    const char* fullExpected[] = {
        "Robot_Pedestal_Base",
        "FANUC_M10iA_Static_Base",
        "FANUC_M10iA_Static_Link1_Turret",
        "FANUC_M10iA_Static_Link2_LowerArm",
        "FANUC_M10iA_Static_Link3_UpperArm",
        "FANUC_M10iA_Static_Wrist_And_Torch",
        "Welding_Fixture_Table",
        "Workpiece_Base_Plate",
        "Workpiece_BoxWall_Front",
        "Workpiece_BoxWall_Side",
        "Table_Clamp_Block_1",
        "Table_Clamp_Block_2"
    };
    std::set<std::string> fullNames;
    bool staticBaseIsEnvironment = false;
    for (const auto& node : fullNodes) {
        fullNames.insert(node.name);
        if (node.name == "FANUC_M10iA_Static_Base" &&
            node.collisionRole == olp::core::CollisionRole::Environment) {
            staticBaseIsEnvironment = true;
        }
    }
    bool fullMatch = fullOk && fullNodes.size() == 12 && staticBaseIsEnvironment &&
                     fullNames.size() == 12;
    std::string missing;
    for (const char* name : fullExpected) {
        if (fullNames.find(name) == fullNames.end()) {
            fullMatch = false;
            missing += name;
            missing += " ";
        }
    }
    std::string got;
    for (const auto& name : fullNames) {
        got += name;
        got += " ";
    }
    check(fullMatch,
          "OCCT reads each MANIFOLD_SOLID_BREP name in the full workcell",
          "missing=[" + missing + "] got=[" + got + "]");

    const std::string tjointPath = std::string(OLP_SOURCE_DIR) +
        "/SampleScenes/Scene1_TJoint_And_FANUC_M10iA/Workpiece_TJoint_With_Clamps.step";
    std::vector<olp::core::SceneNode> tNodes;
    const bool tOk = cad.importStepViaOcct(tjointPath, tNodes);
    std::set<std::string> tNames;
    for (const auto& node : tNodes) tNames.insert(node.name);
    const bool tMatch = tOk && tNodes.size() == 4 &&
        tNames.count("Base_Plate_400x300x12") == 1 &&
        tNames.count("Vertical_Web_Plate_400x12x160") == 1 &&
        tNames.count("Fixture_Clamp_Left") == 1 &&
        tNames.count("Fixture_Clamp_Right") == 1;
    std::string tGot;
    for (const auto& name : tNames) {
        tGot += name;
        tGot += " ";
    }
    check(tMatch, "OCCT reads each solid name in the T-joint file", "got=[" + tGot + "]");

    std::vector<olp::core::SceneNode> tube;
    const bool tubeOk = cad.importStepViaOcct(
        std::string(OLP_SOURCE_DIR) +
            "/SampleScenes/Scene2_Full_Workcell_STEP/Workpiece_SquareTube_80x80x5.step",
        tube);
    const bool tubeNamed = tubeOk && tube.size() == 1 &&
        tube[0].name.find("Square_hollow_section_80x80x5") != std::string::npos;
    check(tubeNamed,
          "OCCT keeps the product name when the solid name is empty",
          tube.empty() ? "no solids" : tube[0].name);
#else
    check(true, "OCCT per-solid names are skipped without OpenCASCADE");
#endif
}

int findNamedPart(const std::vector<olp::core::SceneNode>& nodes, const std::string& token) {
    for (const auto& node : nodes) {
        if (node.name.find(token) != std::string::npos) return node.cadHandleId;
    }
    return -1;
}

bool segmentsMatch(const olp::cad::CadEdgeInfo& edge, const olp::core::Vec3& a, const olp::core::Vec3& b, double tol) {
    const double forward = (edge.startPt - a).norm() + (edge.endPt - b).norm();
    const double backward = (edge.startPt - b).norm() + (edge.endPt - a).norm();
    return std::min(forward, backward) <= tol;
}

int findSegment(const std::vector<olp::cad::CadEdgeInfo>& edges, const olp::core::Vec3& a, const olp::core::Vec3& b) {
    for (int i = 0; i < static_cast<int>(edges.size()); ++i) {
        if (segmentsMatch(edges[static_cast<std::size_t>(i)], a, b, 0.05)) return i;
    }
    return -1;
}

std::string edgeText(const olp::cad::CadEdgeInfo& edge) {
    std::ostringstream oss;
    oss << "(" << edge.startPt.x << "," << edge.startPt.y << "," << edge.startPt.z
        << ")->(" << edge.endPt.x << "," << edge.endPt.y << "," << edge.endPt.z
        << ") L=" << edge.lengthMm << " adj=" << edge.adjacentFaceIndices.size();
    return oss.str();
}

std::vector<int> workpieceHandlesOf(const std::vector<olp::core::SceneNode>& nodes) {
    std::vector<int> handles;
    for (const auto& node : nodes) {
        if (node.type == olp::core::SceneNodeType::Part &&
            node.collisionRole == olp::core::CollisionRole::Workpiece) {
            handles.push_back(node.cadHandleId);
        }
    }
    return handles;
}

void testOcctWeldEdgeMatchesBuiltin() {
    std::cout << "\n--- Test 4c: OCCT weld edge against the built-in parser ---\n";
#ifndef OLP_HAS_OCCT
    check(true, "OCCT weld-edge comparison is skipped without OpenCASCADE");
    return;
#else
    struct WeldCase {
        const char* file;
        const char* part;
    };
    const WeldCase cases[] = {
        {"/SampleScenes/Scene1_TJoint_And_FANUC_M10iA/Workpiece_TJoint_With_Clamps.step",
         "Vertical_Web_Plate"},
        {"/SampleScenes/Scene2_Full_Workcell_STEP/Full_Welding_Cell_With_Robot.step",
         "Workpiece_BoxWall_Front"}
    };

    for (const WeldCase& item : cases) {
        olp::cad::OcctCadKernel builtinCad;
        olp::cad::OcctCadKernel occtCad;
        std::vector<olp::core::SceneNode> builtinNodes;
        std::vector<olp::core::SceneNode> occtNodes;
        const std::string path = std::string(OLP_SOURCE_DIR) + item.file;
        const bool builtinOk = builtinCad.importStepViaBuiltin(path, builtinNodes);
        const bool occtOk = occtCad.importStepViaOcct(path, occtNodes);
        const int builtinPart = findNamedPart(builtinNodes, item.part);
        const int occtPart = findNamedPart(occtNodes, item.part);
        const auto builtinEdges = builtinCad.getEdges(builtinPart);
        const auto occtEdges = occtCad.getEdges(occtPart);

        int missingInOcct = 0;
        int missingInBuiltin = 0;
        if (builtinOk && occtOk) {
            for (const auto& edge : builtinEdges) {
                if (findSegment(occtEdges, edge.startPt, edge.endPt) < 0) ++missingInOcct;
            }
            for (const auto& edge : occtEdges) {
                if (findSegment(builtinEdges, edge.startPt, edge.endPt) < 0) ++missingInBuiltin;
            }
        }
        const bool sameEdges = builtinOk && occtOk && builtinPart >= 0 && occtPart >= 0 &&
                               missingInOcct == 0 && missingInBuiltin == 0 &&
                               builtinEdges.size() == occtEdges.size();
        check(sameEdges,
              std::string("OCCT and the built-in parser list the same edges of ") + item.part,
              "builtin=" + std::to_string(builtinEdges.size()) +
              " occt=" + std::to_string(occtEdges.size()) +
              " missingInOcct=" + std::to_string(missingInOcct) +
              " missingInBuiltin=" + std::to_string(missingInBuiltin));

        std::string weldDetail = "no built-in edge 3";
        bool sameIndex = false;
        bool sameDirection = false;
        bool sameSeam = false;
        if (builtinEdges.size() > 3 && !occtEdges.empty()) {
            const auto& reference = builtinEdges[3];
            const int occtIndex = findSegment(occtEdges, reference.startPt, reference.endPt);
            sameIndex = occtIndex == 3;
            if (occtIndex >= 0) {
                const auto& occtEdge = occtEdges[static_cast<std::size_t>(occtIndex)];
                sameDirection = (occtEdge.startPt - reference.startPt).norm() <= 0.05 &&
                                (occtEdge.endPt - reference.endPt).norm() <= 0.05;
            }
            const auto builtinWp = workpieceHandlesOf(builtinNodes);
            const auto occtWp = workpieceHandlesOf(occtNodes);
            const auto builtinSeam = olp::algo::SeamAndToolpathAlgo::buildSeamFromPickedEdge(
                builtinCad, builtinPart, 3, builtinWp, 50.0);
            const auto occtSeam = occtIndex >= 0
                ? olp::algo::SeamAndToolpathAlgo::buildSeamFromPickedEdge(
                      occtCad, occtPart, occtIndex, occtWp, 50.0)
                : std::nullopt;
            if (builtinSeam && occtSeam && !builtinSeam->sampledFrames.empty() &&
                !occtSeam->sampledFrames.empty()) {
                const auto& b0 = builtinSeam->sampledFrames.front();
                const auto& o0 = occtSeam->sampledFrames.front();
                const double startGap = (b0.position - o0.position).norm();
                const double bisectorDeg = olp::core::rad2deg(std::acos(std::clamp(
                    std::abs(b0.bisector.dot(o0.bisector)), 0.0, 1.0)));
                const double lengthGap = std::abs(reference.lengthMm - occtEdges[static_cast<std::size_t>(occtIndex)].lengthMm);
                sameSeam = startGap < 0.5 && bisectorDeg < 5.0 && lengthGap < 0.1 &&
                           builtinSeam->face1PartId != builtinSeam->face2PartId &&
                           occtSeam->face1PartId != occtSeam->face2PartId;
                weldDetail = "builtinIndex=3 occtIndex=" + std::to_string(occtIndex) +
                             " startGap=" + std::to_string(startGap) +
                             " bisectorDeg=" + std::to_string(bisectorDeg) +
                             " builtin=" + edgeText(reference) +
                             " occt=" + edgeText(occtEdges[static_cast<std::size_t>(occtIndex)]);
            } else {
                weldDetail = "builtinIndex=3 occtIndex=" + std::to_string(occtIndex) +
                             " builtinSeam=" + (builtinSeam ? "yes" : "no") +
                             " occtSeam=" + (occtSeam ? "yes" : "no") +
                             " builtin=" + edgeText(reference);
                if (occtIndex >= 0) {
                    weldDetail += " occt=" + edgeText(occtEdges[static_cast<std::size_t>(occtIndex)]);
                }
            }
        }
        check(sameIndex && sameDirection,
              std::string("OCCT keeps edge 3 and its direction on ") + item.part,
              weldDetail);
        check(sameSeam,
              std::string("OCCT rebuilds the same fillet seam on ") + item.part,
              weldDetail);
    }
#endif
}

void testTJointCrossPartSeamAndCollisionExemption() {
    std::cout << "\n--- Test 5: T-Joint Cross-Part Seam Recognition & Wire-Tip Collision Exemption (Section 11.3 & 11.4) ---\n";
    olp::cad::OcctCadKernel cad;
    std::vector<olp::core::SceneNode> nodes;
    const std::string tjointPath =
        std::string(OLP_SOURCE_DIR) + "/SampleScenes/Scene1_TJoint_And_FANUC_M10iA/Workpiece_TJoint_With_Clamps.step";

    const bool ok = cad.importStep(tjointPath, nodes);
    check(ok && nodes.size() == 4, "Import 4 solids from Workpiece_TJoint_With_Clamps.step");

    int webHandle = -1;
    std::string webNodeId;
    std::vector<int> wpHandles;
    for (const auto& n : nodes) {
        if (n.collisionRole == olp::core::CollisionRole::Workpiece) {
            wpHandles.push_back(n.cadHandleId);
            if (n.name.find("Vertical_Web_Plate") != std::string::npos) {
                webHandle = n.cadHandleId;
                webNodeId = n.id;
            }
        }
    }

    // Edge index 3 is the 400mm long front T-joint seam (-200,-6,12) -> (200,-6,12)
    auto seamOpt = olp::algo::SeamAndToolpathAlgo::buildSeamFromPickedEdge(
        cad, webHandle, 3, wpHandles, 50.0);
    check(seamOpt.has_value() && seamOpt->face1PartId != seamOpt->face2PartId,
          "Cross-part T-joint flank recognition (Vertical_Web_Plate edge + Base_Plate mating face)");

    if (seamOpt) {
        // Verify angle bisector between +Z (0,0,1) and -Y (0,-1,0) is 45 deg
        const auto& sf = seamOpt->sampledFrames.front();
        const olp::core::Vec3 expectedBisector = olp::core::Vec3{0.0, -1.0, 1.0}.normalized();
        const double angleDiffDeg = olp::core::rad2deg(
            std::acos(std::clamp(std::abs(sf.bisector.dot(expectedBisector)), 0.0, 1.0)));
        check(angleDiffDeg < 5.0,
              "Auto-computed T-joint torch bisector angle deviation < 5 deg (actual=" +
              std::to_string(angleDiffDeg) + " deg)");

        // Test wire-tip exemption vs clamp collision (M10)
        const auto robotModel = olp::device::RobotModel::createFanucM10iA();
        olp::algo::OpwKinematics kin(robotModel);
        olp::core::UnitsSettings settings;
        olp::algo::CollisionAndSimEngine colEngine(kin, settings);

        double minClearance = 0.0;
        std::string hitPart;
        const auto torchPoseAtSeam = olp::algo::SeamAndToolpathAlgo::applyWeldingAngles(
            sf.frame, 0.0, 10.0, 0.0);
        const bool falseAlarm = colEngine.checkTorchPoseCollision(
            torchPoseAtSeam, nodes, webNodeId, true, minClearance, hitPart,
            {seamOpt->face1PartId, seamOpt->face2PartId});
        check(!falseAlarm, "Wire-tip collision exemption prevents false alarm at weld root",
              "hit=" + hitPart + " clearance=" + std::to_string(minClearance));

        // Place torch body directly inside Fixture_Clamp_Left (-180..-120, -130..-40, 12..52)
        olp::core::Transform4d collidingPose = torchPoseAtSeam;
        collidingPose.pos = {-150.0, -80.0, 30.0};
        collidingPose.rot = olp::core::Mat3::identity();
        const bool clampDetected = colEngine.checkTorchPoseCollision(
            collidingPose, nodes, webNodeId, true, minClearance, hitPart);
        check(clampDetected && hitPart.find("Fixture_Clamp_Left") != std::string::npos,
              "Detect torch collision against Fixture_Clamp_Left (" + hitPart + ")");
    }
}

void testGizmoAndUndoRedo100Steps() {
    std::cout << "\n--- Test 6: TcpGizmo Preview/Commit/Esc & 100-Step Undo/Redo Stack (Section 11.5 & 11.8) ---\n";
    olp::ui::MainWindow win;
    auto& doc = win.document();
    auto& cmdMgr = win.commandManager();
    auto& gizmo = win.viewport().tcpGizmo();

    olp::core::ControllerUnit cu;
    olp::core::Program prog;
    olp::core::Operation op;
    op.id = "op_test";
    olp::core::Target t1;
    t1.id = "target_1";
    t1.name = "WELD_START";
    t1.poseInUFrame = olp::core::Transform4d::fromTranslation(100.0, 200.0, 300.0);
    op.targets.push_back(t1);
    prog.operations.push_back(op);
    cu.programs.push_back(prog);
    doc.controllers.push_back(cu);

    // 1. Begin drag and move 20 frames -> undoStack must remain 0 during preview
    int previewCount = 0;
    gizmo.setPreviewCallback([&](const olp::ui::GizmoPreviewState&) { ++previewCount; });
    gizmo.setSnapSteps(true, 1.0, 1.0);
    gizmo.beginDrag({"target_1"});
    for (int i = 1; i <= 20; ++i) {
        gizmo.updateDragDelta({i * 1.4, 0.0, 0.0}, {0.0, 0.0, 0.0});
    }
    check(previewCount == 20 && cmdMgr.undoCount() == 0 &&
          std::abs(doc.findTarget("target_1")->poseInUFrame.pos.x - 100.0) < 1e-6,
          "TcpGizmo 20 drag frames emit previews without polluting Undo stack");

    // 2. Release mouse -> commits exactly 1 command with 1mm snapped translation (20 * 1.4 = 28.0 mm)
    gizmo.commitDrag();
    check(cmdMgr.undoCount() == 1 &&
          std::abs(doc.findTarget("target_1")->poseInUFrame.pos.x - 128.0) < 1e-6 &&
          doc.findTarget("target_1")->userModified,
          "TcpGizmo mouse release commits 1 command (snapped X = 128.0 mm, userModified = true)");

    // 3. Drag and press Esc -> cancels without adding undo command
    gizmo.beginDrag({"target_1"});
    gizmo.updateDragDelta({50.0, 50.0, 50.0}, {10.0, 0.0, 0.0});
    gizmo.cancelDrag();
    check(cmdMgr.undoCount() == 1 &&
          std::abs(doc.findTarget("target_1")->poseInUFrame.pos.x - 128.0) < 1e-6,
          "TcpGizmo Esc key cancels active drag cleanly");

    // 4. Execute 100 commands, Undo 100 steps, Redo 100 steps
    cmdMgr.clear();
    const double baseZ = doc.findTarget("target_1")->poseInUFrame.pos.z;
    for (int step = 1; step <= 100; ++step) {
        auto pose = doc.findTarget("target_1")->poseInUFrame;
        pose.pos.z = baseZ + step;
        cmdMgr.executeCommand(std::make_unique<olp::app::ModifyTargetPoseCommand>(
            doc, "target_1", pose, false));
    }
    check(cmdMgr.undoCount() == 100 &&
          std::abs(doc.findTarget("target_1")->poseInUFrame.pos.z - (baseZ + 100.0)) < 1e-6,
          "Push 100 commands to Undo stack");

    for (int step = 0; step < 100; ++step) {
        cmdMgr.undo();
    }
    check(cmdMgr.undoCount() == 0 && cmdMgr.redoCount() == 100 &&
          std::abs(doc.findTarget("target_1")->poseInUFrame.pos.z - baseZ) < 1e-6,
          "Undo 100 consecutive steps restores exact initial state");

    for (int step = 0; step < 100; ++step) {
        cmdMgr.redo();
    }
    check(cmdMgr.undoCount() == 100 && cmdMgr.redoCount() == 0 &&
          std::abs(doc.findTarget("target_1")->poseInUFrame.pos.z - (baseZ + 100.0)) < 1e-6,
          "Redo 100 consecutive steps restores exact final state");
}

void testFanucLsGeneration() {
    std::cout << "\n--- Test 7: FANUC .ls Post-Processor Golden Structure (M12 & Section 11.2) ---\n";
    olp::core::Program prog;
    prog.name = "WELD_001";
    prog.uframeNumber = 1;
    prog.utoolNumber = 1;

    olp::core::Operation op;
    op.id = "op_1";
    op.name = "TJoint_Fillet";

    olp::core::Target tStart;
    tStart.name = "WELD_START";
    tStart.role = olp::core::TargetRole::WELD_START;
    tStart.motion = olp::core::MotionType::L;
    tStart.speedValue = 200.0;
    tStart.termination = olp::core::TerminationType::FINE;
    tStart.poseInUFrame = olp::core::Transform4d::fromWPR({450.25, -120.50, 310.00, 180.0, -45.0, 0.0});
    tStart.events.push_back({olp::core::ProcessEventType::ArcStart, 1, 0.0, ""});
    tStart.cache.valid = true;
    tStart.cache.solvedConfig.flip = true;
    tStart.cache.solvedConfig.turns = {1, 0, -1};

    olp::core::Target tEnd;
    tEnd.name = "WELD_END";
    tEnd.role = olp::core::TargetRole::WELD_END;
    tEnd.motion = olp::core::MotionType::L;
    tEnd.useWeldSpeed = true;
    tEnd.termination = olp::core::TerminationType::FINE;
    tEnd.poseInUFrame = olp::core::Transform4d::fromWPR({450.25, 120.50, 310.00, 180.0, -45.0, 0.0});
    tEnd.events.push_back({olp::core::ProcessEventType::ArcEnd, 1, 0.0, ""});
    tEnd.cache.valid = true;
    tEnd.cache.solvedConfig = tStart.cache.solvedConfig;

    op.targets = {tStart, tEnd};
    prog.operations.push_back(op);

    const std::string lsText = olp::io::FanucLsPostProcessor::generateLsString(prog);
    const bool hasSections =
        lsText.find("/PROG  WELD_001") != std::string::npos &&
        lsText.find("/ATTR") != std::string::npos &&
        lsText.find("/APPL") != std::string::npos &&
        lsText.find("ARC Welding Equipment : 1,*,*,*,*;") != std::string::npos &&
        lsText.find("/MN") != std::string::npos &&
        lsText.find("Arc Start[1] ;") != std::string::npos &&
        lsText.find("WELD_SPEED FINE") != std::string::npos &&
        lsText.find("Arc End[1] ;") != std::string::npos &&
        lsText.find("/POS") != std::string::npos &&
        lsText.find("CONFIG : 'F U T, 1, 0, -1'") != std::string::npos &&
        lsText.find("HOME joints deg") != std::string::npos &&
        lsText.find("/END") != std::string::npos;

    check(hasSections, "Verify generated .ls contains /PROG, /ATTR, /APPL, /MN, Arc Start/End, /POS, /END");

    olp::core::Program blocked = prog;
    blocked.operations[0].targets[0].cache.valid = false;
    std::string exportError;
    const std::string refused = olp::io::FanucLsPostProcessor::generateLsString(blocked, {}, &exportError);
    check(refused.empty() && !exportError.empty(),
          "Post-processor refuses a target whose IK cache is not valid");

    olp::core::Program circular = prog;
    circular.operations[0].targets[0].motion = olp::core::MotionType::C;
    circular.operations[0].targets[0].cache.valid = true;
    std::string circularError;
    const std::string circularText = olp::io::FanucLsPostProcessor::generateLsString(circular, {}, &circularError);
    check(circularText.empty() && circularError.find("Circular") != std::string::npos,
          "Post-processor refuses to write a circular move as a linear move");
}

olp::core::SeamSampleFrame straightSample(double arcMm) {
    olp::core::SeamSampleFrame frame;
    frame.arcLengthMm = arcMm;
    frame.position = {arcMm, 0.0, 0.0};
    frame.tangentX = {1.0, 0.0, 0.0};
    frame.frame = olp::core::Transform4d::fromTranslation(arcMm, 0.0, 0.0);
    return frame;
}

void testPathEditsAndCommandGates() {
    std::cout << "\n--- Test 8: Seam events, reverse speeds, and command no-ops ---\n";
    olp::core::SeamDefinition one;
    one.sampledFrames.push_back(straightSample(0.0));
    olp::core::Operation single;
    single.id = "single";
    olp::algo::SeamAndToolpathAlgo::generateOperationTargets(one, olp::core::Transform4d::identity(), single);
    bool hasArcStart = false;
    bool hasArcEnd = false;
    for (const auto& target : single.targets) {
        for (const auto& ev : target.events) {
            hasArcStart = hasArcStart || ev.type == olp::core::ProcessEventType::ArcStart;
            hasArcEnd = hasArcEnd || ev.type == olp::core::ProcessEventType::ArcEnd;
        }
    }
    check(hasArcStart && hasArcEnd, "A one-point seam emits both Arc Start and Arc End");

    olp::core::SeamDefinition seam;
    seam.sampledFrames.push_back(straightSample(0.0));
    seam.sampledFrames.push_back(straightSample(100.0));
    olp::core::Operation clean;
    clean.id = "edit";
    olp::algo::SeamAndToolpathAlgo::generateOperationTargets(seam, olp::core::Transform4d::identity(), clean);
    olp::core::Operation edited = clean;
    for (auto& target : edited.targets) {
        if (target.role == olp::core::TargetRole::APPROACH) {
            target.userModified = true;
            target.userDeltaOffset = olp::core::Transform4d::fromTranslation(80.0, 0.0, 0.0);
        }
    }
    olp::algo::SeamAndToolpathAlgo::generateOperationTargets(seam, olp::core::Transform4d::identity(), edited);
    const olp::core::Target* cleanStart = nullptr;
    const olp::core::Target* editedStart = nullptr;
    const olp::core::Target* cleanApproach = nullptr;
    const olp::core::Target* editedApproach = nullptr;
    for (const auto& target : clean.targets) {
        if (target.role == olp::core::TargetRole::WELD_START) cleanStart = &target;
        if (target.role == olp::core::TargetRole::APPROACH) cleanApproach = &target;
    }
    for (const auto& target : edited.targets) {
        if (target.role == olp::core::TargetRole::WELD_START) editedStart = &target;
        if (target.role == olp::core::TargetRole::APPROACH) editedApproach = &target;
    }
    const bool approachKept = cleanStart && editedStart && cleanApproach && editedApproach &&
        (editedStart->poseInUFrame.pos - cleanStart->poseInUFrame.pos).norm() < 1e-6 &&
        (editedApproach->poseInUFrame.pos - cleanApproach->poseInUFrame.pos).norm() > 1.0;
    check(approachKept, "An approach edit is not copied onto WELD_START");

    olp::app::ProjectDocument doc;
    olp::core::ControllerUnit cu;
    olp::core::Program prog;
    olp::core::Operation op;
    op.id = "rev";
    auto makeTarget = [](olp::core::TargetRole role, const char* name, olp::core::MotionType motion,
                         double speed, bool weldSpeed) {
        olp::core::Target target;
        target.role = role;
        target.name = name;
        target.motion = motion;
        target.speedValue = speed;
        target.useWeldSpeed = weldSpeed;
        return target;
    };
    op.targets.push_back(makeTarget(olp::core::TargetRole::APPROACH, "APPROACH", olp::core::MotionType::J, 50.0, false));
    op.targets.push_back(makeTarget(olp::core::TargetRole::WELD_START, "WELD_START", olp::core::MotionType::L, 200.0, false));
    op.targets.push_back(makeTarget(olp::core::TargetRole::WELD_END, "WELD_END", olp::core::MotionType::L, 10.0, true));
    op.targets.push_back(makeTarget(olp::core::TargetRole::RETRACT, "RETRACT", olp::core::MotionType::L, 200.0, false));
    prog.operations.push_back(op);
    cu.programs.push_back(prog);
    doc.controllers.push_back(cu);

    olp::app::CommandManager commands;
    commands.executeCommand(std::make_unique<olp::app::ReverseOperationPathCommand>(doc, "rev"));
    const olp::core::Target* reversedEnd = nullptr;
    const olp::core::Target* reversedApproach = nullptr;
    for (const auto& target : doc.controllers[0].programs[0].operations[0].targets) {
        if (target.role == olp::core::TargetRole::WELD_END) reversedEnd = &target;
        if (target.role == olp::core::TargetRole::APPROACH) reversedApproach = &target;
    }
    check(reversedEnd && reversedEnd->useWeldSpeed && reversedApproach &&
          reversedApproach->motion == olp::core::MotionType::J &&
          std::abs(reversedApproach->speedValue - 50.0) < 1e-6,
          "Reversing a path keeps weld speed on the weld and joint motion on the approach");

    commands.executeCommand(std::make_unique<olp::app::ModifyTargetPoseCommand>(
        doc, "missing-target", olp::core::Transform4d::identity()));
    check(commands.undoCount() == 1, "A command that does not change a target is not stacked");

    olp::ui::MainWindow win;
    auto& gizmo = win.viewport().tcpGizmo();
    olp::core::Target dragTarget;
    dragTarget.id = "drag";
    dragTarget.poseInUFrame = olp::core::Transform4d::fromTranslation(10.0, 0.0, 0.0);
    win.document().controllers.push_back({});
    win.document().controllers[0].programs.push_back({});
    win.document().controllers[0].programs[0].operations.push_back({});
    win.document().controllers[0].programs[0].operations[0].targets.push_back(dragTarget);
    gizmo.setSnapSteps(false, 1.0, 1.0);
    gizmo.beginDrag({"drag"});
    gizmo.updateDragDelta({0.0, 0.0, 0.0}, {0.0, 0.0, 0.0});
    check(!gizmo.commitDrag() && win.commandManager().undoCount() == 0,
          "A zero-delta gizmo drag does not commit");

    gizmo.setUFrameInWorld(olp::core::Transform4d::fromWPR({0, 0, 0, 0, 0, 90}));
    olp::core::Vec3 worldPreview{};
    olp::core::Vec3 uframePreview{};
    gizmo.setReferenceFrame(olp::ui::GizmoRefFrame::World);
    gizmo.setPreviewCallback([&](const olp::ui::GizmoPreviewState& state) {
        worldPreview = state.previewPoseInUFrame.pos;
    });
    gizmo.beginDrag({"drag"});
    gizmo.updateDragDelta({20.0, 0.0, 0.0}, {0.0, 0.0, 0.0});
    gizmo.setPreviewCallback(nullptr);
    gizmo.cancelDrag();
    gizmo.setReferenceFrame(olp::ui::GizmoRefFrame::UFrame);
    gizmo.setPreviewCallback([&](const olp::ui::GizmoPreviewState& state) {
        uframePreview = state.previewPoseInUFrame.pos;
    });
    gizmo.beginDrag({"drag"});
    gizmo.updateDragDelta({20.0, 0.0, 0.0}, {0.0, 0.0, 0.0});
    gizmo.setPreviewCallback(nullptr);
    gizmo.cancelDrag();
    check((worldPreview - uframePreview).norm() > 1.0,
          "World and UFrame drags differ when the UFRAME is rotated",
          "world=(" + std::to_string(worldPreview.x) + "," + std::to_string(worldPreview.y) +
          ") uframe=(" + std::to_string(uframePreview.x) + "," + std::to_string(uframePreview.y) + ")");
}

void testSweepCollisionBetweenClearTargets() {
    std::cout << "\n--- Test 9: Torch sweep collision between clear targets ---\n";
    const auto robot = olp::device::RobotModel::createFanucM10iA();
    olp::algo::OpwKinematics kin(robot);
    olp::core::UnitsSettings settings;
    olp::algo::CollisionAndSimEngine engine(kin, settings);
    const auto utool = olp::core::Transform4d::identity();
    const auto home = robot.homeJointsRad;
    auto endJoints = home;
    endJoints[0] += 0.4;

    const double jDist = olp::algo::OpwKinematics::weightedJointDistance(home, endJoints);
    const double sampleStep = 0.05;
    const double segDuration = std::max(0.2, jDist / 1.5);
    const int numSteps = std::max(2, static_cast<int>(std::ceil(segDuration / sampleStep)));
    const int midStep = std::max(1, numSteps / 2);
    const double alpha = static_cast<double>(midStep) / static_cast<double>(numSteps);
    std::array<double, 6> midJoints{};
    for (size_t k = 0; k < 6; ++k) {
        midJoints[k] = home[k] + alpha * (endJoints[k] - home[k]);
    }
    const auto midTcp = kin.forwardKinematics(midJoints, utool).tcpPoseInBase;
    const olp::core::Vec3 gateCenter = midTcp.pos + (-midTcp.rot.colZ()) * 95.0;
    constexpr double half = 6.0;

    olp::core::SceneNode gate;
    gate.id = "sweep_gate";
    gate.name = "Sweep_Gate";
    gate.type = olp::core::SceneNodeType::Part;
    gate.visible = true;
    gate.collisionRole = olp::core::CollisionRole::Fixture;
    gate.collisionMesh.bboxMin = gateCenter - olp::core::Vec3{half, half, half};
    gate.collisionMesh.bboxMax = gateCenter + olp::core::Vec3{half, half, half};
    const std::vector<olp::core::SceneNode> scene{gate};

    const auto startTcp = kin.forwardKinematics(home, utool).tcpPoseInBase;
    const auto endTcp = kin.forwardKinematics(endJoints, utool).tcpPoseInBase;
    double startClearance = 0.0;
    double endClearance = 0.0;
    std::string startHit;
    std::string endHit;
    const bool startHits = engine.checkTorchPoseCollision(
        startTcp, scene, "", false, startClearance, startHit);
    const bool endHits = engine.checkTorchPoseCollision(
        endTcp, scene, "", false, endClearance, endHit);
    check(!startHits && !endHits,
          "Sweep gate misses both ends of the joint move",
          "start=" + std::to_string(startClearance) + " end=" + std::to_string(endClearance));

    olp::core::Program program;
    program.id = "prog_sweep";
    program.startState.homeJointsRad = home;
    olp::core::Operation op;
    op.id = "op_sweep";
    olp::core::Target target;
    target.id = "via_end";
    target.name = "Joint_End";
    target.role = olp::core::TargetRole::VIA;
    target.motion = olp::core::MotionType::J;
    target.poseInUFrame = endTcp;
    op.targets.push_back(target);
    program.operations.push_back(op);

    const auto sim = engine.validateAndSimulateProgram(
        program,
        olp::core::Transform4d::identity(),
        olp::core::Transform4d::identity(),
        utool,
        scene,
        sampleStep);
    bool sweepHit = false;
    std::string issueText;
    for (const auto& issue : sim.issues) {
        issueText += issue.description + "; ";
        if (issue.state == olp::core::TargetValidationState::Collision &&
            issue.description.find("Sweep collides with Sweep_Gate") != std::string::npos) {
            sweepHit = true;
        }
    }
    const bool endpointClear =
        program.operations[0].targets[0].cache.status != olp::core::TargetValidationState::Collision;
    check(sweepHit && endpointClear,
          "Joint interpolation reports a torch sweep through a fixture the targets miss",
          issueText);
}

void testApproachTransitClearsWall() {
    std::cout << "\n--- Test 10: Home-to-approach transit clears a wall ---\n";
    const auto robot = olp::device::RobotModel::createFanucM10iA();
    olp::algo::OpwKinematics kin(robot);
    olp::core::UnitsSettings settings;
    olp::algo::CollisionAndSimEngine engine(kin, settings);
    const auto utool = olp::core::Transform4d::identity();
    const auto home = robot.homeJointsRad;
    const auto homeTcp = kin.forwardKinematics(home, utool).tcpPoseInBase;

    olp::core::Transform4d approach = homeTcp;
    approach.pos.x += 250.0;
    approach.pos.z -= 180.0;

    olp::core::SceneNode wall;
    wall.id = "transit_wall";
    wall.name = "Transit_Wall";
    wall.type = olp::core::SceneNodeType::Part;
    wall.visible = true;
    wall.collisionRole = olp::core::CollisionRole::Fixture;
    // Sits on the straight descent, and stops short of the approach so the torch
    // body (150 mm back along tool -Z, radius 25 mm) is outside the box.
    wall.collisionMesh.bboxMin = {homeTcp.pos.x + 30.0, homeTcp.pos.y - 600.0, approach.pos.z - 40.0};
    wall.collisionMesh.bboxMax = {homeTcp.pos.x + 70.0, homeTcp.pos.y + 600.0, homeTcp.pos.z - 30.0};
    const std::vector<olp::core::SceneNode> scene{wall};

    double startClearance = 0.0;
    double endClearance = 0.0;
    std::string startHit;
    std::string endHit;
    const bool startHits = engine.checkTorchPoseCollision(
        homeTcp, scene, "", false, startClearance, startHit);
    const bool endHits = engine.checkTorchPoseCollision(
        approach, scene, "", false, endClearance, endHit);
    check(!startHits && !endHits,
          "Transit wall misses the home pose and the approach pose",
          "start=" + std::to_string(startClearance) + " end=" + std::to_string(endClearance));

    olp::core::Program program;
    program.id = "prog_transit";
    program.startState.homeJointsRad = home;
    olp::core::Operation op;
    op.id = "op_transit";
    olp::core::Target target;
    target.id = "approach";
    target.name = "APPROACH";
    target.role = olp::core::TargetRole::APPROACH;
    target.motion = olp::core::MotionType::J;
    target.speedValue = 50.0;
    target.poseInUFrame = approach;
    const olp::core::Vec3 approachPos = target.poseInUFrame.pos;
    op.targets.push_back(target);
    program.operations.push_back(op);

    const auto identity = olp::core::Transform4d::identity();
    auto before = engine.validateAndSimulateProgram(
        program, identity, identity, utool, scene, 0.05);
    bool sweepHit = false;
    for (const auto& issue : before.issues) {
        if (issue.state == olp::core::TargetValidationState::Collision &&
            issue.description.find("Sweep collides with Transit_Wall") != std::string::npos) {
            sweepHit = true;
        }
    }
    check(sweepHit, "Joint move from home to the approach sweeps the wall");

    const bool inserted = engine.insertSafeTransitBeforeApproach(
        program, identity, identity, utool, scene, 0.05);
    check(inserted, "A torch transit over the wall is found");

    bool clearancePoint = false;
    for (const auto& kept : program.operations[0].targets) {
        if (kept.role == olp::core::TargetRole::APPROACH) {
            check((kept.poseInUFrame.pos - approachPos).norm() < 1e-6,
                  "The approach pose itself stays where it was");
        }
        if (kept.name.rfind("CLEARANCE_", 0) == 0) {
            clearancePoint = true;
        }
    }
    check(clearancePoint, "The transit adds a clearance point above the wall");

    auto after = engine.validateAndSimulateProgram(
        program, identity, identity, utool, scene, 0.05);
    bool blocking = false;
    std::string issueText;
    for (const auto& issue : after.issues) {
        issueText += issue.targetName + ": " + issue.description + "; ";
        if (issue.state == olp::core::TargetValidationState::Collision ||
            issue.state == olp::core::TargetValidationState::Unreachable ||
            issue.state == olp::core::TargetValidationState::JointLimitExceeded ||
            issue.state == olp::core::TargetValidationState::ConfigJump) {
            blocking = true;
        }
    }
    check(!blocking, "The clearance transit has no blocking collision or reach issue", issueText);
}

void testMainWindowZones() {
    std::cout << "\n--- Test 11: Main window zones ---\n";
#ifndef OLP_HAS_QT6
    check(false, "Qt6 is required to build the main window");
#else
    qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef OLP_QT_PLUGINS
    qputenv("QT_PLUGIN_PATH", OLP_QT_PLUGINS);
#endif
    int argc = 1;
    char exe[] = "olp_tests";
    char* argv[] = {exe, nullptr};
    QApplication app(argc, argv);
    olp::ui::MainWindow window;
    olp::ui::ShellWindow shell(window);
    shell.show();
    const QStringList titles = shell.dockTitles();
    const QStringList required = {
        QString::fromUtf8("场景"),
        QString::fromUtf8("程序"),
        QString::fromUtf8("路径点"),
        QString::fromUtf8("属性"),
        QString::fromUtf8("关节点动"),
        QString::fromUtf8("问题")
    };
    QString missing;
    for (const auto& title : required) {
        if (!titles.contains(title)) {
            missing += title + " ";
        }
    }
    check(missing.isEmpty(), "Main window has the six functional docks", missing.toStdString());
    check(shell.zoneChecked(QString::fromUtf8("场景")) && !shell.zoneChecked(QString::fromUtf8("程序")),
          "Layout workbench shows the scene dock");
    shell.showOlpWorkbench();
    check(shell.zoneChecked(QString::fromUtf8("程序")) &&
              shell.zoneChecked(QString::fromUtf8("路径点")) &&
              shell.zoneChecked(QString::fromUtf8("关节点动")) &&
              shell.zoneChecked(QString::fromUtf8("问题")) &&
              !shell.zoneChecked(QString::fromUtf8("场景")),
          "Programming workbench shows the program, points, jog, and issues");
    shell.showLayoutWorkbench();
    check(shell.zoneChecked(QString::fromUtf8("场景")) && shell.zoneChecked(QString::fromUtf8("属性")),
          "Switching back to the layout workbench restores the scene dock");
#endif
}

} // namespace

int main() {
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

    std::cout << "\n====================================================================\n";
    std::cout << "  Summary: " << g_passed << " passed, " << g_failed << " failed.\n";
    std::cout << "====================================================================\n";
    return (g_failed == 0) ? 0 : 1;
}
