#include "olp/algo/CollisionAndSim.h"
#include "olp/algo/OpwKinematics.h"
#include "olp/algo/SeamAndToolpathAlgo.h"
#include "olp/app/CommandManager.h"
#include "olp/app/WorkcellLayoutService.h"
#include "olp/cad/OcctCadKernel.h"
#include "olp/device/RobotModel.h"
#include "olp/io/FanucLsPostProcessor.h"
#include "olp/ui/MainWindow.h"
#include "olp/ui/OcctViewport.h"
#include "olp/ui/TcpGizmo.h"

#ifdef OLP_HAS_QT6
#include "olp/ui/ShellWindow.h"
#include <QApplication>
#include <QListWidget>
#include <QStringList>
#endif

#ifdef OLP_HAS_OCCT
#include <TopoDS.hxx>
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
    olp::ui::TcpGizmo gizmo(doc, cmdMgr);

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
    olp::ui::TcpGizmo gizmo(win.document(), win.commandManager());
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
    // QApplication is created in main()
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
    check(shell.zoneChecked(QString::fromUtf8("场景")) &&
              shell.zoneChecked(QString::fromUtf8("属性")) &&
              shell.zoneChecked(QString::fromUtf8("关节点动")) &&
              !shell.zoneChecked(QString::fromUtf8("程序")) &&
              !shell.zoneChecked(QString::fromUtf8("路径点")) &&
              !shell.zoneChecked(QString::fromUtf8("问题")),
          "Layout workbench keeps scene, properties, and jog");
    shell.showOlpWorkbench();
    check(shell.zoneChecked(QString::fromUtf8("场景")) &&
              shell.zoneChecked(QString::fromUtf8("程序")) &&
              shell.zoneChecked(QString::fromUtf8("路径点")) &&
              shell.zoneChecked(QString::fromUtf8("关节点动")) &&
              shell.zoneChecked(QString::fromUtf8("问题")) &&
              shell.zoneChecked(QString::fromUtf8("属性")),
          "Programming workbench adds the program, points, and issues");
    shell.showLayoutWorkbench();
    check(shell.zoneChecked(QString::fromUtf8("场景")) &&
              shell.zoneChecked(QString::fromUtf8("属性")) &&
              shell.zoneChecked(QString::fromUtf8("关节点动")) &&
              !shell.zoneChecked(QString::fromUtf8("程序")),
          "Switching back to the layout workbench hides the program dock");
#endif
}

void testBRepMeshRenderingAndEdgePicking() {
    std::cout << "\n--- Test 12: B-Rep Mesh Rendering and Workpiece Edge Picking ---\n";
    olp::cad::OcctCadKernel cad;
    const std::string fullCellStep =
        std::string(OLP_SOURCE_DIR) + "/SampleScenes/Scene2_Full_Workcell_STEP/Full_Welding_Cell_With_Robot.step";

    std::vector<olp::core::SceneNode> parts;
    bool ok = cad.importStep(fullCellStep, parts);
    check(ok, "Import STEP for B-Rep mesh rendering and edge picking");

    int boxWallHandle = -1;
    const olp::core::SceneNode* boxWallNode = nullptr;
    for (const auto& p : parts) {
        if (p.name.find("Workpiece_BoxWall_Front") != std::string::npos) {
            boxWallHandle = p.cadHandleId;
            boxWallNode = &p;
            break;
        }
    }
    check(boxWallHandle > 0 && boxWallNode != nullptr, "Located Workpiece_BoxWall_Front in imported parts");

#ifdef OLP_HAS_OCCT
    // 1. Verify real TopoDS_Shape is available from OcctCadKernel
    const TopoDS_Shape shape = cad.getShape(boxWallHandle);
    check(!shape.IsNull(), "OcctCadKernel::getShape returns valid TopoDS_Shape for workpiece");

    // 2. Verify tessellate generates real B-Rep mesh (fine render mesh and coarse collision mesh)
    olp::core::TriMesh renderMesh, collisionMesh;
    const bool tessOk = cad.tessellate(boxWallHandle, 0.2, 2.0, renderMesh, collisionMesh);
    check(tessOk, "OcctCadKernel::tessellate succeeds for B-Rep solid");
    check(renderMesh.vertices.size() >= 8 && renderMesh.triangles.size() >= 12,
          "Tessellated render mesh contains actual 3D triangulation (triangles=" +
          std::to_string(renderMesh.triangles.size()) + ")");
    check(collisionMesh.triangles.size() > 0,
          "Tessellated collision mesh contains coarse triangulation (triangles=" +
          std::to_string(collisionMesh.triangles.size()) + ")");

    // 3. Verify Edge shape queries and findEdgeIndex round-trip
    const auto edges = cad.getEdges(boxWallHandle);
    check(edges.size() >= 4, "Workpiece has expected CAD edge count (" + std::to_string(edges.size()) + ")");
    constexpr int kEdgeIdx = 3;
    const TopoDS_Edge edgeShape = cad.getEdgeShape(boxWallHandle, kEdgeIdx);
    check(!edgeShape.IsNull(), "OcctCadKernel::getEdgeShape returns valid TopoDS_Edge");
    const int resolvedIdx = cad.findEdgeIndex(boxWallHandle, edgeShape);
    check(resolvedIdx == kEdgeIdx, "findEdgeIndex round-trip matches edge index (" +
          std::to_string(resolvedIdx) + " == " + std::to_string(kEdgeIdx) + ")");
    gp_Trsf dummyTrsf;
    dummyTrsf.SetTranslation(gp_Vec(100.0, 200.0, 300.0));
    const TopoDS_Edge transformedEdge = TopoDS::Edge(edgeShape.Moved(TopLoc_Location(dummyTrsf)));
    const int resolvedTransformedIdx = cad.findEdgeIndex(boxWallHandle, transformedEdge);
    check(resolvedTransformedIdx == kEdgeIdx, "findEdgeIndex matches transformed edge via topological partner");

    // 4. Verify OcctViewport edge selection and callback
    olp::app::ProjectDocument doc;
    doc.sceneNodes = parts;
    olp::app::CommandManager cmdMgr(50);
    olp::ui::OcctViewport viewport(doc, cmdMgr, &cad);

    bool callbackFired = false;
    olp::ui::PickedEdge callbackEdge{};
    viewport.setEdgePickedCallback([&](const olp::ui::PickedEdge& pe) {
        callbackFired = true;
        callbackEdge = pe;
    });

    viewport.setSelectionMode(olp::ui::SelectionSubShapeMode::Edge);
    check(viewport.selectionMode() == olp::ui::SelectionSubShapeMode::Edge,
          "OcctViewport selection mode set to Edge");

    viewport.setDisplayMode(olp::ui::ViewportDisplayMode::ShadedWithEdges);
    check(viewport.displayMode() == olp::ui::ViewportDisplayMode::ShadedWithEdges,
          "OcctViewport display mode set to ShadedWithEdges");

    // Sync scene and select edge
    viewport.syncScene();
    const bool selectOk = viewport.selectEdge(boxWallHandle, kEdgeIdx);
    check(selectOk && callbackFired, "OcctViewport::selectEdge triggers edge-picked callback");
    check(viewport.lastPickedEdge().has_value() && viewport.lastPickedEdge()->edgeIndex == kEdgeIdx,
          "OcctViewport records picked edge index in lastPickedEdge");
    check(callbackEdge.lengthMm > 1.0,
          "Picked edge records valid length (" + std::to_string(callbackEdge.lengthMm) + " mm)");

    // 5. Verify CreateSeamFromPickedEdgeCommand Undo/Redo
    doc.isDirty = false;
    const size_t initialSeamCount = doc.seams.size();
    auto createSeamCmd = std::make_unique<olp::app::CreateSeamFromPickedEdgeCommand>(
        doc, cad, boxWallHandle, kEdgeIdx, 50.0);
    cmdMgr.executeCommand(std::move(createSeamCmd));

    check(doc.seams.size() == initialSeamCount + 1, "CreateSeamFromPickedEdgeCommand adds seam to document");
    check(doc.isDirty, "CreateSeamFromPickedEdgeCommand marks document dirty");
    check(cmdMgr.canUndo(), "Command pushed to Undo stack");

    // Sync scene to verify viewport renders new seam
    viewport.syncScene();

    cmdMgr.undo();
    check(doc.seams.size() == initialSeamCount, "Undo CreateSeamFromPickedEdgeCommand restores seam list");
    check(!doc.isDirty, "Undo restores clean document dirty state");

    cmdMgr.redo();
    check(doc.seams.size() == initialSeamCount + 1, "Redo CreateSeamFromPickedEdgeCommand restores created seam");
    check(doc.isDirty, "Redo marks document dirty again");
#endif
}

void testTcpGizmoMountAndInteraction() {
    std::cout << "\n--- Test 13: TcpGizmo Mount and Interactive Drag in OcctViewport ---\n";
    olp::app::ProjectDocument doc;
    olp::app::CommandManager cmdMgr(50);
    olp::cad::OcctCadKernel cad;

    // 1. Setup scene with a frame node (UFRAME)
    olp::core::SceneNode uframeNode;
    uframeNode.id = "UFRAME_1";
    uframeNode.name = "UFRAME_1";
    uframeNode.type = olp::core::SceneNodeType::Frame;
    uframeNode.localTransform = olp::core::Transform4d::fromTranslation(100.0, 200.0, 300.0);
    doc.sceneNodes.push_back(uframeNode);

    // 2. Setup controller, program, operation, and 2 targets
    olp::core::ControllerUnit cu;
    olp::core::Program prog;
    olp::core::Operation op;
    op.id = "op_gizmo";

    olp::core::Target t1;
    t1.id = "target_gizmo_1";
    t1.name = "T1";
    t1.poseInUFrame = olp::core::Transform4d::fromTranslation(10.0, 20.0, 30.0);

    olp::core::Target t2;
    t2.id = "target_gizmo_2";
    t2.name = "T2";
    t2.poseInUFrame = olp::core::Transform4d::fromTranslation(50.0, 60.0, 70.0);

    op.targets.push_back(t1);
    op.targets.push_back(t2);
    prog.operations.push_back(op);
    cu.programs.push_back(prog);
    doc.controllers.push_back(cu);

    olp::ui::OcctViewport viewport(doc, cmdMgr, &cad);

    // 3. Test mounting gizmo to target_gizmo_1
    const bool mountOk = viewport.setTargetGizmo("target_gizmo_1");
    check(mountOk, "Mount TcpGizmo to target_gizmo_1");
    check(viewport.activeTargetId() == "target_gizmo_1", "activeTargetId matches selected target");

    // 4. Test reference frame switching
    viewport.setGizmoReferenceFrame(olp::ui::GizmoRefFrame::World);
    check(viewport.gizmoReferenceFrame() == olp::ui::GizmoRefFrame::World, "Gizmo reference frame switched to World");
    viewport.setGizmoReferenceFrame(olp::ui::GizmoRefFrame::Tool);
    check(viewport.gizmoReferenceFrame() == olp::ui::GizmoRefFrame::Tool, "Gizmo reference frame switched to Tool");

    // 4b. Test Seam reference frame and in-place rotation
    olp::core::SeamDefinition seam;
    seam.id = "seam_gizmo";
    olp::core::SeamSampleFrame sf0;
    sf0.position = {110.0, 220.0, 330.0}; // world pos: (100,200,300) + (10,20,30)
    sf0.frame = olp::core::Transform4d::fromTranslation(110.0, 220.0, 330.0);
    sf0.frame.rot = olp::core::Mat3::rotZ(olp::core::deg2rad(45.0));
    seam.sampledFrames.push_back(sf0);
    doc.seams.push_back(seam);
    doc.controllers[0].programs[0].operations[0].seamId = "seam_gizmo";

    viewport.setTargetGizmo("target_gizmo_1");
    viewport.setGizmoReferenceFrame(olp::ui::GizmoRefFrame::Seam);
    check(viewport.gizmoReferenceFrame() == olp::ui::GizmoRefFrame::Seam, "Gizmo reference frame switched to Seam");

    // 5. Test snap steps configuration
    viewport.setGizmoSnapSteps(true, 5.0, 2.0);

    // 6. Test interactive drag with zero document mutation
    bool previewFired = false;
    olp::ui::GizmoPreviewState lastPreview{};
    viewport.tcpGizmo().setPreviewCallback([&](const olp::ui::GizmoPreviewState& st) {
        previewFired = true;
        lastPreview = st;
    });

    // In-place rotation in Seam frame: rotating torch angles must NOT cause position to jump or orbit
    const auto initialPoseBeforeRot = doc.findTarget("target_gizmo_1")->poseInUFrame;
    viewport.tcpGizmo().beginDrag({"target_gizmo_1"});
    viewport.tcpGizmo().updateDragDelta(olp::core::Vec3(0.0, 0.0, 0.0), olp::core::Vec3(0.0, 10.0, 0.0));
    check(std::abs(lastPreview.previewPoseInUFrame.pos.x - initialPoseBeforeRot.pos.x) < 1e-6 &&
          std::abs(lastPreview.previewPoseInUFrame.pos.y - initialPoseBeforeRot.pos.y) < 1e-6 &&
          std::abs(lastPreview.previewPoseInUFrame.pos.z - initialPoseBeforeRot.pos.z) < 1e-6,
          "Rotation in Seam frame is in-place (zero orbital translation jump)");
    check(lastPreview.previewPoseInUFrame.rot.angularDistanceRad(initialPoseBeforeRot.rot) > 0.1,
          "Rotation in Seam frame modifies torch orientation");
    viewport.tcpGizmo().cancelDrag();

    // Switch back to Tool frame for linear drag test
    viewport.setGizmoReferenceFrame(olp::ui::GizmoRefFrame::Tool);

    const bool dragStarted = viewport.tcpGizmo().beginDrag({"target_gizmo_1"});
    check(dragStarted && viewport.tcpGizmo().isDragging(), "TcpGizmo drag started");

    viewport.tcpGizmo().updateDragDelta(olp::core::Vec3(15.0, 0.0, 0.0), olp::core::Vec3(0.0, 0.0, 0.0));
    check(previewFired, "Gizmo drag triggers preview callback");
    check(lastPreview.primaryTargetId == "target_gizmo_1", "Preview state matches primary target");

    // Rule 3 verification: zero document mutation during drag
    const auto* docTarget = doc.findTarget("target_gizmo_1");
    check(docTarget != nullptr && std::abs(docTarget->poseInUFrame.pos.x - 10.0) < 1e-6,
          "Zero document mutation during drag (target poseInUFrame remains initial)");
    check(cmdMgr.undoCount() == 0, "No commands pushed during drag");

    // Rule 4 verification: mouse release commits exactly 1 command
    const bool committed = viewport.tcpGizmo().commitDrag();
    check(committed, "Commit drag succeeds");
    check(!viewport.tcpGizmo().isDragging(), "TcpGizmo isDragging is false after commit");
    check(cmdMgr.undoCount() == 1, "Exactly 1 ModifyTargetPoseCommand committed to Undo stack");
    check(std::abs(doc.findTarget("target_gizmo_1")->poseInUFrame.pos.x - 25.0) < 1e-6,
          "Target poseInUFrame updated to new position (X = 25.0 mm)");

    // Test Undo / Redo of gizmo drag
    cmdMgr.undo();
    check(std::abs(doc.findTarget("target_gizmo_1")->poseInUFrame.pos.x - 10.0) < 1e-6,
          "Undo restores target initial pose (X = 10.0 mm)");
    cmdMgr.redo();
    check(std::abs(doc.findTarget("target_gizmo_1")->poseInUFrame.pos.x - 25.0) < 1e-6,
          "Redo re-applies target modified pose (X = 25.0 mm)");

    // 7. Test Esc cancellation restores pose with zero commands
    viewport.tcpGizmo().beginDrag({"target_gizmo_1"});
    viewport.tcpGizmo().updateDragDelta(olp::core::Vec3(100.0, 0.0, 0.0), olp::core::Vec3(0.0, 0.0, 0.0));
    viewport.tcpGizmo().cancelDrag();
    check(!viewport.tcpGizmo().isDragging(), "TcpGizmo isDragging is false after cancel");
    check(cmdMgr.undoCount() == 1, "Undo stack untouched after cancelDrag");
    check(std::abs(doc.findTarget("target_gizmo_1")->poseInUFrame.pos.x - 25.0) < 1e-6,
          "Target pose unchanged after cancelDrag");

    // 8. Test multi-selection drag (Rule 5: exactly 1 MacroCommand)
    viewport.setMultiTargetProvider([&]() {
        return std::vector<std::string>{"target_gizmo_1", "target_gizmo_2"};
    });
    viewport.tcpGizmo().beginDrag({"target_gizmo_1", "target_gizmo_2"});
    viewport.tcpGizmo().updateDragDelta(olp::core::Vec3(0.0, 10.0, 0.0), olp::core::Vec3(0.0, 0.0, 0.0));
    viewport.tcpGizmo().commitDrag();
    check(cmdMgr.undoCount() == 2, "Multi-target drag commits exactly 1 MacroCommand");
    check(std::abs(doc.findTarget("target_gizmo_1")->poseInUFrame.pos.y - 30.0) < 1e-6 &&
          std::abs(doc.findTarget("target_gizmo_2")->poseInUFrame.pos.y - 70.0) < 1e-6,
          "Both targets updated by drag delta (T1.Y = 30.0, T2.Y = 70.0)");

    // Undo multi-target drag rolls back both targets together in 1 step
    cmdMgr.undo();
    check(std::abs(doc.findTarget("target_gizmo_1")->poseInUFrame.pos.y - 20.0) < 1e-6 &&
          std::abs(doc.findTarget("target_gizmo_2")->poseInUFrame.pos.y - 60.0) < 1e-6,
          "Undo multi-target MacroCommand rolls back both targets in 1 step");

    // 9. Test clearing gizmo
    viewport.clearTargetGizmo();
    check(viewport.activeTargetId().empty(), "clearTargetGizmo clears activeTargetId");
}

void testTangentContinuousEdgeChaining() {
    std::cout << "\n--- Test 14: Tangent-Continuous Edge Chaining & Multi-Segment Seam Generation (M07) ---\n";
#ifdef OLP_HAS_OCCT
    olp::cad::OcctCadKernel cad;

    // 1. SquareTube: Multi-segment straight + curved fillet corner edge chaining
    const std::string squareTubePath =
        std::string(OLP_SOURCE_DIR) + "/SampleScenes/Scene2_Full_Workcell_STEP/Workpiece_SquareTube_80x80x5.step";
    std::vector<olp::core::SceneNode> tubeNodes;
    bool okTube = cad.importStep(squareTubePath, tubeNodes);
    check(okTube && !tubeNodes.empty(), "Import Workpiece_SquareTube_80x80x5.step");

    int tubeCadHandle = tubeNodes.front().cadHandleId;
    const auto tubeEdges = cad.getEdges(tubeCadHandle);
    check(tubeEdges.size() > 0, "SquareTube has CAD edges", "count=" + std::to_string(tubeEdges.size()));

    // Search for an edge with tangent-continuous fillet corners (straight + arc transition)
    int seedEdgeIdx = -1;
    olp::cad::EdgeChainResult tubeChain;
    for (size_t i = 0; i < tubeEdges.size(); ++i) {
        auto chain = cad.findTangentContinuousEdgeChain(tubeCadHandle, static_cast<int>(i), 15.0);
        if (chain.edges.size() > 1) {
            seedEdgeIdx = static_cast<int>(i);
            tubeChain = chain;
            break;
        }
    }
    check(seedEdgeIdx >= 0 && tubeChain.edges.size() > 1,
          "SquareTube finds tangent-continuous edge chain (> 1 edge)",
          "edges=" + std::to_string(tubeChain.edges.size()) + ", totalLen=" + std::to_string(tubeChain.totalLengthMm));

    // Verify chain length equals sum of chained edges
    double sumEdgeLen = 0.0;
    for (const auto& ce : tubeChain.edges) {
        sumEdgeLen += ce.lengthMm;
    }
    check(std::abs(tubeChain.totalLengthMm - sumEdgeLen) < 1e-4,
          "Chain total length equals sum of chained edge lengths");

    // 2. Pipe Flange: Circular closed loop detection
    const std::string flangePath =
        std::string(OLP_SOURCE_DIR) + "/SampleScenes/Scene1_TJoint_And_FANUC_M10iA/Workpiece_PipeFlange_DN15.step";
    std::vector<olp::core::SceneNode> flangeNodes;
    bool okFlange = cad.importStep(flangePath, flangeNodes);
    check(okFlange && !flangeNodes.empty(), "Import Workpiece_PipeFlange_DN15.step");

    int flangeCadHandle = flangeNodes.front().cadHandleId;
    const auto flangeEdges = cad.getEdges(flangeCadHandle);
    check(flangeEdges.size() > 0, "Pipe Flange has CAD edges", "count=" + std::to_string(flangeEdges.size()));

    bool foundClosed = false;
    bool foundDynamicRadialBisector = false;
    olp::cad::EdgeChainResult flangeChain;
    std::vector<int> flangeWpHandles = {flangeCadHandle};
    for (size_t i = 0; i < flangeEdges.size(); ++i) {
        auto chain = cad.findTangentContinuousEdgeChain(flangeCadHandle, static_cast<int>(i), 30.0);
        if (chain.isClosed) {
            if (!foundClosed) {
                foundClosed = true;
                flangeChain = chain;
            }
            auto candSeamOpt = olp::algo::SeamAndToolpathAlgo::buildSeamFromEdgeChain(
                cad, flangeCadHandle, chain, flangeWpHandles, 10.0, 0.0, 0.0);
            if (candSeamOpt.has_value()) {
                double minBisX = 1e9, maxBisX = -1e9;
                for (const auto& sf : candSeamOpt->sampledFrames) {
                    minBisX = std::min(minBisX, sf.bisector.x);
                    maxBisX = std::max(maxBisX, sf.bisector.x);
                }
                if (maxBisX - minBisX > 0.5) {
                    foundDynamicRadialBisector = true;
                    flangeChain = chain;
                    break;
                }
            }
        }
    }
    check(foundClosed, "Pipe Flange detects closed loop edge chain (isClosed = true)");

    auto flangeSeamOpt = olp::algo::SeamAndToolpathAlgo::buildSeamFromEdgeChain(
        cad, flangeCadHandle, flangeChain, flangeWpHandles, 10.0, 0.0, 0.0);
    check(flangeSeamOpt.has_value(), "buildSeamFromEdgeChain succeeds for Pipe Flange closed loop");
    if (flangeSeamOpt.has_value()) {
        const auto& fseam = *flangeSeamOpt;
        check(fseam.isClosed, "Pipe Flange SeamDefinition preserves isClosed = true");
        check(fseam.sampledFrames.size() >= 4, "Pipe Flange circular seam has multiple sampled frames");
        double minX = 1e9, maxX = -1e9;
        for (const auto& sf : fseam.sampledFrames) {
            minX = std::min(minX, sf.position.x);
            maxX = std::max(maxX, sf.position.x);
        }
        check(maxX - minX > 5.0, "Pipe Flange circular seam positions span true 3D diameter (not collapsed)");
        check(foundDynamicRadialBisector,
              "Pipe Flange cylindrical/conical flank normal & bisector rotate dynamically around circumference");
    }

    // 3. Composite seam definition build
    std::vector<int> wpHandles = {tubeCadHandle};
    auto seamOpt = olp::algo::SeamAndToolpathAlgo::buildSeamFromEdgeChain(
        cad, tubeCadHandle, tubeChain, wpHandles, 20.0, 0.0, 0.0);
    check(seamOpt.has_value(), "buildSeamFromEdgeChain succeeds for SquareTube chain");
    if (seamOpt.has_value()) {
        const auto& seam = *seamOpt;
        check(seam.edgeChain.size() == tubeChain.edges.size(),
              "Composite seam contains all edges in chain",
              "count=" + std::to_string(seam.edgeChain.size()));
        check(seam.sampledFrames.size() >= 3,
              "Composite seam has multiple sampled frames across multi-edge profile",
              "frames=" + std::to_string(seam.sampledFrames.size()));

        bool monotonic = true;
        bool validOrthonormal = true;
        for (size_t i = 0; i < seam.sampledFrames.size(); ++i) {
            if (i > 0 && seam.sampledFrames[i].arcLengthMm <= seam.sampledFrames[i - 1].arcLengthMm) {
                monotonic = false;
            }
            const auto& rot = seam.sampledFrames[i].frame.rot;
            const auto cX = rot.colX();
            const auto cY = rot.colY();
            const auto cZ = rot.colZ();
            const double det = cX.dot(cY.cross(cZ));
            if (std::abs(det - 1.0) > 1e-3) {
                validOrthonormal = false;
            }
        }
        check(monotonic, "Sampled frames have strictly increasing cumulative arc lengths");
        check(validOrthonormal, "Sampled frames all have orthonormal right-handed rotation matrices (det ~ 1)");
    }

    // 4. CreateSeamFromPickedEdgeCommand with chaining + Undo/Redo
    olp::app::ProjectDocument doc;
    olp::app::CommandManager cmdMgr;
    for (const auto& n : tubeNodes) {
        doc.sceneNodes.push_back(n);
    }
    check(doc.seams.empty(), "Document initially has no seams");

    auto cmd = std::make_unique<olp::app::CreateSeamFromPickedEdgeCommand>(
        doc, cad, tubeCadHandle, seedEdgeIdx, 20.0, true, 15.0);
    cmdMgr.executeCommand(std::move(cmd));

    check(doc.seams.size() == 1, "CreateSeamFromPickedEdgeCommand with chaining adds 1 seam");
    check(doc.isDirty, "Document marked dirty after seam creation");
    check(doc.seams.front().edgeChain.size() == tubeChain.edges.size(),
          "Created seam edgeChain matches detected edge chain size");

    cmdMgr.undo();
    check(doc.seams.empty(), "Undo removes created chained seam");
    check(!doc.isDirty, "Undo restores clean dirty state");

    cmdMgr.redo();
    check(doc.seams.size() == 1, "Redo restores created chained seam");
    check(doc.isDirty, "Redo marks document dirty again");

    // Verify duplicate pick on same chain is rejected as no-op to preserve Undo symmetry
    auto dupCmd = std::make_unique<olp::app::CreateSeamFromPickedEdgeCommand>(
        doc, cad, tubeCadHandle, seedEdgeIdx, 20.0, true, 15.0);
    cmdMgr.executeCommand(std::move(dupCmd));
    check(doc.seams.size() == 1, "Duplicate pick on existing seam chain is ignored (no duplicate seam)");
    cmdMgr.undo();
    check(doc.seams.empty(), "Single Undo cleanly removes the seam after duplicate pick no-op");
#endif
}

void testSeamToOperationAndSlerpStepping() {
    std::cout << "\n--- Test 15: M08 Seam-to-Operation Generation, SLERP Orientation Smoothing & Target Stepping ---\n";

    // 1. Verify single straight line thins to 4 points vs closed ring / composite chain retains all points
    olp::core::SeamDefinition seamLine;
    seamLine.id = "seam_line";
    seamLine.name = "StraightLineSeam";
    seamLine.isClosed = false;
    olp::core::TopologicalSignature sigLine;
    sigLine.topologyIndex = 1;
    seamLine.edgeChain = {sigLine};
    for (int i = 0; i < 5; ++i) {
        seamLine.sampledFrames.push_back(straightSample(i * 25.0)); // 0, 25, 50, 75, 100 mm
    }

    olp::core::Operation opLine;
    opLine.id = "op_line";
    opLine.genParams.endpointsOnlyForLine = true;
    opLine.genParams.approachDistMm = 40.0;
    opLine.genParams.retractDistMm = 35.0;
    olp::algo::SeamAndToolpathAlgo::generateOperationTargets(seamLine, olp::core::Transform4d::identity(), opLine);

    check(opLine.targets.size() == 4,
          "Single straight line with endpointsOnlyForLine=true generates exactly 4 points",
          "count=" + std::to_string(opLine.targets.size()));
    if (opLine.targets.size() == 4) {
        check(opLine.targets[0].role == olp::core::TargetRole::APPROACH &&
              opLine.targets[1].role == olp::core::TargetRole::WELD_START &&
              opLine.targets[2].role == olp::core::TargetRole::WELD_END &&
              opLine.targets[3].role == olp::core::TargetRole::RETRACT,
              "Target roles for line: APPROACH, WELD_START, WELD_END, RETRACT");
        const double appDist = (opLine.targets[0].poseInUFrame.pos -
            (opLine.targets[1].poseInUFrame.pos - opLine.targets[1].poseInUFrame.rot.colZ() * 40.0)).norm();
        const double retDist = (opLine.targets[3].poseInUFrame.pos -
            (opLine.targets[2].poseInUFrame.pos - opLine.targets[2].poseInUFrame.rot.colZ() * 35.0)).norm();
        check(appDist < 1e-4 && retDist < 1e-4,
              "APPROACH and RETRACT retreat along -Z_tcp by approachDistMm and retractDistMm");
    }

    // Closed loop seam
    olp::core::SeamDefinition seamClosed;
    seamClosed.id = "seam_closed";
    seamClosed.name = "ClosedRingSeam";
    seamClosed.isClosed = true;
    for (int i = 0; i < 8; ++i) {
        const double angle = i * (2.0 * olp::core::kPi / 8.0);
        olp::core::SeamSampleFrame sf;
        sf.arcLengthMm = i * 20.0;
        sf.position = {50.0 * std::cos(angle), 50.0 * std::sin(angle), 0.0};
        sf.tangentX = {-std::sin(angle), std::cos(angle), 0.0};
        sf.frame = olp::core::Transform4d::fromTranslation(sf.position.x, sf.position.y, sf.position.z);
        seamClosed.sampledFrames.push_back(sf);
    }

    olp::core::Operation opClosed;
    opClosed.id = "op_closed";
    opClosed.genParams.endpointsOnlyForLine = true;
    olp::algo::SeamAndToolpathAlgo::generateOperationTargets(seamClosed, olp::core::Transform4d::identity(), opClosed);

    check(opClosed.targets.size() == 10,
          "Closed ring seam preserves all 8 sampled points (1 APPROACH + 8 weld + 1 RETRACT = 10 targets)",
          "count=" + std::to_string(opClosed.targets.size()));
    bool hasVia = false;
    for (const auto& t : opClosed.targets) {
        if (t.role == olp::core::TargetRole::WELD_VIA) hasVia = true;
    }
    check(hasVia, "Closed ring seam contains WELD_VIA intermediate targets sequence");

    // Multi-edge chain seam
    olp::core::SeamDefinition seamChain;
    seamChain.id = "seam_chain";
    seamChain.name = "MultiEdgeChainSeam";
    seamChain.isClosed = false;
    olp::core::TopologicalSignature sig1, sig2;
    sig1.topologyIndex = 1; sig2.topologyIndex = 2;
    seamChain.edgeChain = {sig1, sig2};
    for (int i = 0; i < 5; ++i) {
        seamChain.sampledFrames.push_back(straightSample(i * 20.0));
    }
    olp::core::Operation opChain;
    opChain.id = "op_chain";
    opChain.genParams.endpointsOnlyForLine = true;
    olp::algo::SeamAndToolpathAlgo::generateOperationTargets(seamChain, olp::core::Transform4d::identity(), opChain);
    check(opChain.targets.size() == 7,
          "Multi-edge chain seam preserves all sampled points (not thinned by endpointsOnlyForLine)",
          "count=" + std::to_string(opChain.targets.size()));

    // 2. GenerateOperationFromSeamCommand Undo/Redo and dirty symmetry
    olp::app::ProjectDocument doc;
    olp::app::CommandManager cmdMgr;
    doc.seams.push_back(seamLine);
    doc.isDirty = false;

    check(!doc.isDirty && !doc.seams.front().programmed && doc.controllers.empty(),
          "Initial state: clean dirty state, programmed=false, controllers empty");

    auto genCmd = std::make_unique<olp::app::GenerateOperationFromSeamCommand>(doc, seamLine.id);
    const std::string createdOpId = genCmd->operationId();
    cmdMgr.executeCommand(std::move(genCmd));

    check(doc.isDirty, "GenerateOperationFromSeamCommand marks doc.isDirty = true");
    check(doc.seams.front().programmed, "seam.programmed marked true after generation");
    check(!doc.controllers.empty() && !doc.controllers.front().programs.empty(),
          "Controller and program created automatically");
    const auto* createdOp = doc.findOperation(createdOpId);
    check(createdOp != nullptr && createdOp->targets.size() == 4,
          "Created operation has 4 targets for straight line seam");

    // Undo
    cmdMgr.undo();
    check(!doc.isDirty, "Undo restores doc.isDirty = false");
    check(!doc.seams.front().programmed, "Undo restores seam.programmed = false");
    check(doc.findOperation(createdOpId) == nullptr, "Undo removes created operation");

    // Redo
    cmdMgr.redo();
    check(doc.isDirty, "Redo marks doc.isDirty = true");
    check(doc.seams.front().programmed, "Redo restores seam.programmed = true");
    check(doc.findOperation(createdOpId) != nullptr, "Redo restores created operation");

    // 3. SLERP Orientation Interpolation & SmoothTargetOrientationsCommand
    olp::core::Operation slerpOp;
    slerpOp.id = "slerp_op";
    slerpOp.name = "SlerpOp";
    slerpOp.seamId = seamLine.id;

    for (int i = 0; i < 5; ++i) {
        olp::core::Target t;
        t.id = "slerp_T_" + std::to_string(i);
        t.arcLengthRatio = i * 0.25;
        t.role = (i == 0) ? olp::core::TargetRole::WELD_START :
                 ((i == 4) ? olp::core::TargetRole::WELD_END : olp::core::TargetRole::WELD_VIA);
        t.poseInUFrame.rot = olp::core::Mat3::identity();
        t.poseInUFrame.pos = {i * 25.0, 0.0, 0.0};
        slerpOp.targets.push_back(t);
    }
    doc.controllers.front().programs.front().operations.push_back(slerpOp);
    doc.isDirty = false;

    // Tilt end target by 40 degrees around X axis
    const double tiltAngleDeg = 40.0;
    auto* opInDoc = doc.findOperation("slerp_op");
    check(opInDoc != nullptr, "Found slerp_op in document");
    if (opInDoc != nullptr) {
        opInDoc->targets.back().poseInUFrame.rot = olp::core::Mat3::rotX(olp::core::deg2rad(tiltAngleDeg));
        opInDoc->targets.back().userDeltaOffset.rot = olp::core::Mat3::rotX(olp::core::deg2rad(tiltAngleDeg));
        opInDoc->targets.back().userModified = true;

        auto smoothCmd = std::make_unique<olp::app::SmoothTargetOrientationsCommand>(
            doc, "slerp_op", opInDoc->targets.front().id, opInDoc->targets.back().id);
        cmdMgr.executeCommand(std::move(smoothCmd));

        check(doc.isDirty, "SmoothTargetOrientationsCommand marks document dirty");

        const auto& tMid = opInDoc->targets[2]; // ratio 0.5 -> expected 20 deg
        const auto& tQ1 = opInDoc->targets[1];  // ratio 0.25 -> expected 10 deg
        const auto& tQ3 = opInDoc->targets[3];  // ratio 0.75 -> expected 30 deg

        check(tMid.userModified && tQ1.userModified && tQ3.userModified,
              "Intermediate targets marked userModified = true after SLERP");

        const auto expectedMidRot = olp::core::Mat3::rotX(olp::core::deg2rad(20.0));
        const double midErrDeg = olp::core::rad2deg(tMid.poseInUFrame.rot.angularDistanceRad(expectedMidRot));
        check(midErrDeg < 0.1,
              "Midpoint orientation error < 0.1 deg (actual: " + std::to_string(midErrDeg) + " deg)");

        const auto expectedQ1Rot = olp::core::Mat3::rotX(olp::core::deg2rad(10.0));
        const double q1ErrDeg = olp::core::rad2deg(tQ1.poseInUFrame.rot.angularDistanceRad(expectedQ1Rot));
        const auto expectedQ3Rot = olp::core::Mat3::rotX(olp::core::deg2rad(30.0));
        const double q3ErrDeg = olp::core::rad2deg(tQ3.poseInUFrame.rot.angularDistanceRad(expectedQ3Rot));
        check(q1ErrDeg < 0.1 && q3ErrDeg < 0.1,
              "Intermediate targets scale monotonically with arcLengthRatio (10 deg and 30 deg)");

        // Undo SLERP
        cmdMgr.undo();
        check(!opInDoc->targets[2].userModified,
              "Undo restores intermediate targets to un-modified state");
        check(doc.isDirty == false, "Undo restores clean dirty state");

        // Redo SLERP
        cmdMgr.redo();
        check(opInDoc->targets[2].userModified,
              "Redo re-applies SLERP interpolation to intermediate targets");
        check(doc.isDirty == true, "Redo marks document dirty again");
    }

    // M08-003 regression: Zero-change SLERP does not pollute Undo stack or userModified
    olp::core::Operation zeroOp;
    zeroOp.id = "zero_slerp_op";
    zeroOp.seamId = seamLine.id;
    for (int i = 0; i < 4; ++i) {
        olp::core::Target t;
        t.id = "zero_T_" + std::to_string(i);
        t.arcLengthRatio = i / 3.0;
        t.role = (i == 0) ? olp::core::TargetRole::WELD_START :
                 ((i == 3) ? olp::core::TargetRole::WELD_END : olp::core::TargetRole::WELD_VIA);
        t.poseInUFrame.rot = olp::core::Mat3::identity();
        zeroOp.targets.push_back(t);
    }
    doc.controllers.front().programs.front().operations.push_back(zeroOp);
    const size_t undoCountBeforeZeroSlerp = cmdMgr.undoCount();
    auto noChangeCmd = std::make_unique<olp::app::SmoothTargetOrientationsCommand>(
        doc, "zero_slerp_op");
    cmdMgr.executeCommand(std::move(noChangeCmd));
    check(cmdMgr.undoCount() == undoCountBeforeZeroSlerp,
          "Zero-change SLERP command is rejected by didMutate() gate and not stacked");
    const auto* zeroOpCheck = doc.findOperation("zero_slerp_op");
    check(zeroOpCheck != nullptr && !zeroOpCheck->targets[1].userModified && !zeroOpCheck->targets[2].userModified,
          "Intermediate targets remain !userModified after zero-change SLERP");

    // M08-004 regression: RegenerateOperationTargetsCommand undo restores seam.programmed and doc.isDirty
    olp::core::SeamDefinition seamRegen = seamLine;
    seamRegen.id = "seam_regen";
    seamRegen.programmed = false;
    doc.seams.push_back(seamRegen);
    doc.isDirty = false;

    olp::core::Operation opRegen;
    opRegen.id = "op_regen";
    opRegen.seamId = "seam_regen";
    doc.controllers.front().programs.front().operations.push_back(opRegen);

    const size_t undoCountBeforeRegen = cmdMgr.undoCount();
    auto regenCmd = std::make_unique<olp::app::RegenerateOperationTargetsCommand>(doc, "op_regen");
    cmdMgr.executeCommand(std::move(regenCmd));

    check(cmdMgr.undoCount() == undoCountBeforeRegen + 1, "RegenerateOperationTargetsCommand executed and stacked");
    check(doc.isDirty, "RegenerateOperationTargetsCommand marks doc.isDirty = true");
    check(doc.findSeam("seam_regen")->programmed, "RegenerateOperationTargetsCommand marks seam.programmed = true");

    cmdMgr.undo();
    check(!doc.isDirty, "Undo RegenerateOperationTargetsCommand restores doc.isDirty = false");
    check(!doc.findSeam("seam_regen")->programmed, "Undo RegenerateOperationTargetsCommand restores seam.programmed = false");

    cmdMgr.redo();
    check(doc.isDirty, "Redo RegenerateOperationTargetsCommand re-marks doc.isDirty = true");
    check(doc.findSeam("seam_regen")->programmed, "Redo RegenerateOperationTargetsCommand restores seam.programmed = true");

    // 4. Target stepping navigation & OcctViewport linkage & IK preview
    olp::ui::MainWindow win;
    olp::core::ControllerUnit winCu;
    winCu.id = "cu_win";
    olp::core::Program winProg;
    winProg.id = "prog_win";
    olp::core::Operation winOp;
    winOp.id = "op_step";
    for (int i = 0; i < 3; ++i) {
        olp::core::Target t;
        t.id = "step_T_" + std::to_string(i + 1);
        t.name = "P" + std::to_string(i + 1);
        if (i == 1) {
            t.cache.valid = true;
            t.cache.jointsRad = {0.15, -0.25, 0.35, -0.45, 0.55, -0.65};
        }
        winOp.targets.push_back(t);
    }
    winProg.operations.push_back(winOp);

    // M08-005 regression: second operation to verify cross-operation selectTarget resolution
    olp::core::Operation winOp2;
    winOp2.id = "op_step2";
    for (int i = 0; i < 2; ++i) {
        olp::core::Target t;
        t.id = "op2_T_" + std::to_string(i + 1);
        t.name = "OP2_P" + std::to_string(i + 1);
        winOp2.targets.push_back(t);
    }
    winProg.operations.push_back(winOp2);
    winCu.programs.push_back(winProg);
    win.document().controllers.push_back(winCu);

    olp::core::SceneNode robotNode;
    robotNode.id = "robot_node";
    robotNode.type = olp::core::SceneNodeType::Robot;
    win.document().sceneNodes.push_back(robotNode);

    olp::ui::OcctViewport viewport(win.document(), win.commandManager(), &win.cadKernel());
    viewport.attachMainWindow(&win);

    win.selectOperation("op_step");

    // Step to first target
    bool step1 = win.stepTarget(true);
    check(step1 && win.selectedTargetId() == "step_T_1",
          "stepTarget(true) from none selects first target (step_T_1)");
    check(viewport.activeTargetId() == "step_T_1",
          "Viewport activeTargetId links to win.selectedTargetId() (step_T_1)");

    // Step to second target (with IK valid cache)
    bool step2 = win.stepTarget(true);
    check(step2 && win.selectedTargetId() == "step_T_2",
          "stepTarget(true) advances to second target (step_T_2)");
    check(viewport.activeTargetId() == "step_T_2",
          "Viewport activeTargetId advances to step_T_2");

    const auto* rNode = win.document().findSceneNode("robot_node");
    check(rNode != nullptr && std::abs(rNode->currentJointsRad[0] - 0.15) < 1e-4 &&
          std::abs(rNode->currentJointsRad[4] - 0.55) < 1e-4,
          "Selecting target with valid IK cache updates robot sceneNode joint angles");

    // Step back to first target
    bool stepBack = win.stepTarget(false);
    check(stepBack && win.selectedTargetId() == "step_T_1",
          "stepTarget(false) navigates back to previous target (step_T_1)");
    check(viewport.activeTargetId() == "step_T_1",
          "Viewport activeTargetId synchronizes back to step_T_1");

    // M08-005 regression check: cross-operation target selection with default operationId
    win.selectTarget("op2_T_2");
    check(win.selectedTargetId() == "op2_T_2", "selectTarget successfully updates selectedTargetId to op2_T_2");
    check(win.selectedOperationId() == "op_step2",
          "selectTarget with default operationId correctly resolves owning operationId to op_step2");
    check(viewport.activeTargetId() == "op2_T_2",
          "Viewport activeTargetId synchronizes to op2_T_2 across operations");
}

void testWeldingProcessEventsAndTorchSpinTuning() {
    std::cout << "\n--- Test 16: M09 Welding Process Events, Weld Schedules & Torch Spin Tuning ---\n";

    // 1. Weld Schedule Model & Default Generation Rules (APPROACH=FINE, WELD_START=FINE+ArcStart[n], WELD_VIA=CNT50, WELD_END=FINE+ArcEnd[n], RETRACT=FINE)
    olp::app::ProjectDocument doc;
    check(!doc.weldSchedules.empty(), "ProjectDocument initializes with default weld schedules");
    const auto* sched1 = doc.findWeldSchedule(1);
    check(sched1 != nullptr, "findWeldSchedule(1) finds default Schedule 1");
    if (sched1) {
        check(std::abs(sched1->currentAmps - 180.0) < 1e-4, "Schedule 1 preset current is 180A");
        check(std::abs(sched1->voltageVolts - 22.0) < 1e-4, "Schedule 1 preset voltage is 22V");
        check(std::abs(sched1->weldSpeedCmPerMin - 50.0) < 1e-4, "Schedule 1 welding speed is 50 cm/min");
        check(std::abs(sched1->getWeldSpeedMmPerSec() - (500.0 / 60.0)) < 1e-4, "Schedule 1 mm/s helper returns 8.333 mm/s");
    }
    check(doc.findWeldSchedule(99) == nullptr, "findWeldSchedule(99) returns nullptr for non-existent schedule");

    // Build a seam with 5 sampled frames
    olp::core::SeamDefinition seam;
    seam.id = "seam_m09";
    seam.name = "SeamM09";
    for (int i = 0; i < 5; ++i) {
        olp::core::SeamSampleFrame frame;
        frame.arcLengthMm = i * 25.0;
        frame.position = {i * 25.0, 0.0, 0.0};
        frame.tangentX = {1.0, 0.0, 0.0};
        frame.normalFace1 = {0.0, 1.0, 0.0};
        frame.normalFace2 = {0.0, 0.0, 1.0};
        frame.bisector = {0.0, 0.70710678, 0.70710678};
        frame.frame = olp::core::Transform4d::identity();
        frame.frame.pos = frame.position;
        seam.sampledFrames.push_back(frame);
    }
    doc.seams.push_back(seam);

    olp::core::Operation op;
    op.id = "op_m09";
    op.name = "OpM09";
    op.seamId = seam.id;
    op.genParams.endpointsOnlyForLine = false; // preserve all 5 frames
    op.genParams.weldScheduleNumber = 2;       // Arc Start[2] and Arc End[2]
    op.genParams.weldSpeedMmPerSec = 12.0;
    op.genParams.approachSpeedMmPerSec = 150.0;
    op.genParams.retractSpeedMmPerSec = 180.0;

    olp::algo::SeamAndToolpathAlgo::generateOperationTargets(
        seam, olp::core::Transform4d::identity(), op);

    // 1 APPROACH + 5 Weld + 1 RETRACT = 7 targets
    check(op.targets.size() == 7, "Operation targets size is 7 (APPROACH, START, 3x VIA, END, RETRACT)");
    
    // Check APPROACH
    const auto& tApp = op.targets[0];
    check(tApp.role == olp::core::TargetRole::APPROACH, "Target 0 is APPROACH");
    check(tApp.termination == olp::core::TerminationType::FINE, "APPROACH default termination is FINE");
    check(tApp.events.empty(), "APPROACH has no process events");

    // Check WELD_START
    const auto& tStart = op.targets[1];
    check(tStart.role == olp::core::TargetRole::WELD_START, "Target 1 is WELD_START");
    check(tStart.termination == olp::core::TerminationType::FINE, "WELD_START default termination is FINE");
    check(tStart.events.size() == 1, "WELD_START has 1 process event");
    if (tStart.events.size() == 1) {
        check(tStart.events[0].type == olp::core::ProcessEventType::ArcStart, "WELD_START event is ArcStart");
        check(tStart.events[0].scheduleNumber == 2, "WELD_START ArcStart condition number is 2");
    }

    // Check WELD_VIA intermediate points
    for (size_t i = 2; i <= 4; ++i) {
        const auto& tVia = op.targets[i];
        check(tVia.role == olp::core::TargetRole::WELD_VIA, "Intermediate target is WELD_VIA");
        check(tVia.useWeldSpeed, "WELD_VIA uses weld speed");
        check(tVia.termination == olp::core::TerminationType::CNT, "WELD_VIA termination is CNT");
        check(tVia.cntValue == 50, "WELD_VIA default cntValue is 50");
        check(tVia.events.empty(), "WELD_VIA has no process events");
    }

    // Check WELD_END
    const auto& tEnd = op.targets[5];
    check(tEnd.role == olp::core::TargetRole::WELD_END, "Target 5 is WELD_END");
    check(tEnd.termination == olp::core::TerminationType::FINE, "WELD_END default termination is FINE");
    check(tEnd.useWeldSpeed, "WELD_END uses weld speed");
    check(tEnd.events.size() == 1, "WELD_END has 1 process event");
    if (tEnd.events.size() == 1) {
        check(tEnd.events[0].type == olp::core::ProcessEventType::ArcEnd, "WELD_END event is ArcEnd");
        check(tEnd.events[0].scheduleNumber == 2, "WELD_END ArcEnd condition number is 2");
    }

    // Check RETRACT
    const auto& tRet = op.targets[6];
    check(tRet.role == olp::core::TargetRole::RETRACT, "Target 6 is RETRACT");
    check(tRet.termination == olp::core::TerminationType::FINE, "RETRACT default termination is FINE");
    check(tRet.events.empty(), "RETRACT has no process events");

    // Add operation to document controller
    olp::core::ControllerUnit cu;
    cu.id = "ctrl_m09";
    olp::core::Program prog;
    prog.id = "prog_m09";
    prog.operations.push_back(op);
    cu.programs.push_back(prog);
    doc.controllers.push_back(cu);
    doc.isDirty = false;

    // 2. ModifyTargetProcessParamsCommand & Undo/Redo Symmetry & Zero-Change Gate
    olp::app::CommandManager cmdMgr;
    const std::string viaId = op.targets[2].id;

    olp::app::TargetProcessParams newParams;
    newParams.motion = olp::core::MotionType::J;
    newParams.speedValue = 85.0;
    newParams.termination = olp::core::TerminationType::CNT;
    newParams.cntValue = 80;

    auto modCmd = std::make_unique<olp::app::ModifyTargetProcessParamsCommand>(
        doc, viaId, newParams);
    cmdMgr.executeCommand(std::move(modCmd));

    check(doc.isDirty, "ModifyTargetProcessParamsCommand marks doc.isDirty = true");
    auto* tViaInDoc = doc.findTarget(viaId);
    check(tViaInDoc != nullptr, "Target found in doc");
    if (tViaInDoc) {
        check(tViaInDoc->motion == olp::core::MotionType::J, "Target motion modified to J");
        check(std::abs(tViaInDoc->speedValue - 85.0) < 1e-4, "Target speedValue modified to 85.0");
        check(tViaInDoc->termination == olp::core::TerminationType::CNT, "Target termination modified to CNT");
        check(tViaInDoc->cntValue == 80, "Target cntValue modified to 80");
    }

    // Undo ModifyTargetProcessParamsCommand
    cmdMgr.undo();
    check(!doc.isDirty, "Undo ModifyTargetProcessParamsCommand restores doc.isDirty = false");
    if (tViaInDoc) {
        check(tViaInDoc->motion == olp::core::MotionType::L, "Undo restores target motion to L");
        check(std::abs(tViaInDoc->speedValue - 12.0) < 1e-4, "Undo restores target speedValue to 12.0");
        check(tViaInDoc->cntValue == 50, "Undo restores target cntValue to 50");
    }

    // Redo ModifyTargetProcessParamsCommand
    cmdMgr.redo();
    check(doc.isDirty, "Redo ModifyTargetProcessParamsCommand marks doc.isDirty = true");
    if (tViaInDoc) {
        check(tViaInDoc->motion == olp::core::MotionType::J, "Redo restores target motion to J");
        check(tViaInDoc->cntValue == 80, "Redo restores target cntValue to 80");
    }

    // Zero-change gate
    const size_t undoCountBefore = cmdMgr.undoCount();
    auto noChangeCmd = std::make_unique<olp::app::ModifyTargetProcessParamsCommand>(
        doc, viaId, newParams);
    cmdMgr.executeCommand(std::move(noChangeCmd));
    check(cmdMgr.undoCount() == undoCountBefore, "Zero-change ModifyTargetProcessParamsCommand is not stacked");

    // 3. Partial Range SLERP Orientation Interpolation (UI-001)
    // Create an operation with 6 targets, all initially identity rotation
    olp::core::Operation partialOp;
    partialOp.id = "op_partial";
    partialOp.name = "OpPartial";
    for (int i = 0; i < 6; ++i) {
        olp::core::Target t;
        t.id = "partial_T_" + std::to_string(i);
        t.arcLengthRatio = i * 0.2;
        t.role = (i == 0) ? olp::core::TargetRole::APPROACH :
                 ((i == 1) ? olp::core::TargetRole::WELD_START :
                 ((i == 5) ? olp::core::TargetRole::WELD_END : olp::core::TargetRole::WELD_VIA));
        t.poseInUFrame.rot = olp::core::Mat3::identity();
        t.poseInUFrame.pos = {i * 20.0, 0.0, 0.0};
        partialOp.targets.push_back(t);
    }
    doc.controllers.front().programs.front().operations.push_back(partialOp);
    doc.isDirty = false;

    // Tilt T_4 by 40 degrees around X axis
    auto* pOp = doc.findOperation("op_partial");
    check(pOp != nullptr, "Found op_partial in document");
    if (pOp) {
        pOp->targets[4].poseInUFrame.rot = olp::core::Mat3::rotX(olp::core::deg2rad(40.0));
        pOp->targets[4].userDeltaOffset.rot = olp::core::Mat3::rotX(olp::core::deg2rad(40.0));
        pOp->targets[4].userModified = true;

        // Perform partial SLERP only between T_2 and T_4 (startTargetId = "partial_T_2", endTargetId = "partial_T_4")
        auto partialSlerpCmd = std::make_unique<olp::app::SmoothTargetOrientationsCommand>(
            doc, "op_partial", "partial_T_2", "partial_T_4");
        cmdMgr.executeCommand(std::move(partialSlerpCmd));

        check(doc.isDirty, "Partial SLERP marks doc.isDirty = true");
        
        // T_3 (in range, midway between T_2 [0 deg] and T_4 [40 deg]): expected ~20 deg around X
        const auto& t3 = pOp->targets[3];
        check(t3.userModified, "T_3 inside partial SLERP range is marked userModified = true");
        const auto expectedT3Rot = olp::core::Mat3::rotX(olp::core::deg2rad(20.0));
        const double errT3Deg = olp::core::rad2deg(t3.poseInUFrame.rot.angularDistanceRad(expectedT3Rot));
        check(errT3Deg < 0.1, "T_3 orientation interpolated accurately to midpoint ~20 deg (actual err: " + std::to_string(errT3Deg) + " deg)");

        // Points outside the range (T_0, T_1, T_5) MUST remain untouched!
        const auto& t1 = pOp->targets[1]; // WELD_START (outside range)
        check(!t1.userModified, "T_1 (WELD_START) outside partial SLERP range remains !userModified");
        check(t1.poseInUFrame.rot.angularDistanceRad(olp::core::Mat3::identity()) < 1e-6,
              "T_1 orientation outside partial SLERP range remains strictly unchanged (identity)");

        const auto& t5 = pOp->targets[5]; // WELD_END (outside range)
        check(!t5.userModified, "T_5 (WELD_END) outside partial SLERP range remains !userModified");
        check(t5.poseInUFrame.rot.angularDistanceRad(olp::core::Mat3::identity()) < 1e-6,
              "T_5 orientation outside partial SLERP range remains strictly unchanged (identity)");

        // Undo partial SLERP
        cmdMgr.undo();
        check(!doc.isDirty, "Undo partial SLERP restores clean dirty state");
        check(!pOp->targets[3].userModified, "Undo partial SLERP restores T_3 to !userModified");
        check(pOp->targets[3].poseInUFrame.rot.angularDistanceRad(olp::core::Mat3::identity()) < 1e-6,
              "Undo partial SLERP restores T_3 rotation to identity");

        // Redo partial SLERP
        cmdMgr.redo();
        check(doc.isDirty, "Redo partial SLERP marks doc.isDirty = true");
        check(pOp->targets[3].userModified, "Redo partial SLERP re-marks T_3 as userModified");
    }

    // 4. Torch Spin Tuning (RotateTargetAroundTcpZCommand & rotateTargetAroundTcpZ)
    // Setup target with an arbitrary non-trivial initial 3D pose
    olp::core::Target spinTarget;
    spinTarget.id = "spin_target_1";
    spinTarget.name = "SPIN_TARGET";
    spinTarget.poseInUFrame.pos = {120.0, -80.0, 350.0};
    const olp::core::Mat3 initialRot = olp::core::Mat3::rotX(olp::core::deg2rad(30.0)) *
                                       olp::core::Mat3::rotY(olp::core::deg2rad(-20.0));
    spinTarget.poseInUFrame.rot = initialRot;
    doc.controllers.front().programs.front().operations.front().targets.push_back(spinTarget);
    doc.isDirty = false;

    auto* tSpinInDoc = doc.findTarget("spin_target_1");
    check(tSpinInDoc != nullptr, "Found spin_target_1 in doc");
    if (tSpinInDoc) {
        const olp::core::Vec3 origZ = tSpinInDoc->poseInUFrame.rot.colZ();
        const olp::core::Vec3 origPos = tSpinInDoc->poseInUFrame.pos;

        // Rotate by +45 degrees around TCP Z
        const double spinDeg = 45.0;
        auto spinCmd = std::make_unique<olp::app::RotateTargetAroundTcpZCommand>(
            doc, "spin_target_1", olp::core::deg2rad(spinDeg));
        cmdMgr.executeCommand(std::move(spinCmd));

        check(doc.isDirty, "RotateTargetAroundTcpZCommand marks doc.isDirty = true");
        check(tSpinInDoc->userModified, "Target marked userModified after TCP Z rotation");

        // Mathematical invariant 1: Position does not move at all
        const double posShift = (tSpinInDoc->poseInUFrame.pos - origPos).norm();
        check(posShift < 1e-12, "TCP position strictly unchanged during Z spin (shift: " + std::to_string(posShift) + " mm)");

        // Mathematical invariant 2: Local Z axis in space remains identical
        const olp::core::Vec3 newZ = tSpinInDoc->poseInUFrame.rot.colZ();
        const double zDev = (newZ - origZ).norm();
        check(zDev < 1e-12, "Torch Z axis vector in space remains strictly unchanged (dev: " + std::to_string(zDev) + ")");

        // Mathematical invariant 3: Right-handed orthonormal matrix with determinant strictly 1.0
        const double det = tSpinInDoc->poseInUFrame.rot.determinant();
        check(std::abs(det - 1.0) < 1e-10, "Rotated matrix determinant strictly equals 1.0 (det: " + std::to_string(det) + ")");

        const auto xCol = tSpinInDoc->poseInUFrame.rot.colX();
        const auto yCol = tSpinInDoc->poseInUFrame.rot.colY();
        const auto zCol = tSpinInDoc->poseInUFrame.rot.colZ();
        check(std::abs(xCol.dot(yCol)) < 1e-12 &&
              std::abs(xCol.dot(zCol)) < 1e-12 &&
              std::abs(yCol.dot(zCol)) < 1e-12,
              "Rotated matrix columns strictly orthogonal (dot products < 1e-12)");

        // Mathematical invariant 4: Rotation around Z axis is exactly 45 degrees
        const olp::core::Mat3 dR = initialRot.transposed() * tSpinInDoc->poseInUFrame.rot;
        const double actualSpinDeg = olp::core::rad2deg(std::atan2(dR.m[1][0], dR.m[0][0]));
        check(std::abs(actualSpinDeg - 45.0) < 1e-5, "Actual spin angle is 45.0 deg (actual: " + std::to_string(actualSpinDeg) + " deg)");

        // Undo RotateTargetAroundTcpZCommand
        cmdMgr.undo();
        check(!doc.isDirty, "Undo RotateTargetAroundTcpZCommand restores clean dirty state");
        check(!tSpinInDoc->userModified, "Undo restores userModified to false");
        check(tSpinInDoc->poseInUFrame.rot.angularDistanceRad(initialRot) < 1e-10,
              "Undo restores exact initial orientation");

        // Redo RotateTargetAroundTcpZCommand
        cmdMgr.redo();
        check(doc.isDirty, "Redo RotateTargetAroundTcpZCommand marks doc.isDirty = true");
        check(tSpinInDoc->userModified, "Redo restores userModified = true");

        // Zero-rotation gate
        const size_t spinUndoCount = cmdMgr.undoCount();
        auto zeroSpinCmd = std::make_unique<olp::app::RotateTargetAroundTcpZCommand>(
            doc, "spin_target_1", 0.0);
        cmdMgr.executeCommand(std::move(zeroSpinCmd));
        check(cmdMgr.undoCount() == spinUndoCount, "Zero-angle RotateTargetAroundTcpZCommand is not stacked");
    }

    // 5. Toolpath Regeneration Process Parameter Preservation (M09-003 Regression)
    // Setup targets in an operation with mixed manual edits:
    // Target 1 (WELD_START): process modification (ArcStart schedule = 3)
    // Target 2 (WELD_VIA_1): process modification (CNT = 80, speedValue = 85.0)
    // Target 3 (WELD_VIA_2): pose modification ONLY (Z spin 30 deg, userModified = true, processModified = false)
    // Target 4 (WELD_VIA_3): unmodified
    auto* testOp = doc.findOperation("op_m09");
    check(testOp != nullptr, "Found op_m09 in document for M09-003 regression test");
    if (testOp && testOp->targets.size() >= 5) {
        // Edit Target 1: ArcStart schedule = 3
        olp::app::TargetProcessParams pStartParams;
        pStartParams.events = std::vector<olp::core::ProcessEvent>{{olp::core::ProcessEventType::ArcStart, 3, 0.0, ""}};
        cmdMgr.executeCommand(std::make_unique<olp::app::ModifyTargetProcessParamsCommand>(
            doc, testOp->targets[1].id, pStartParams));
        check(testOp->targets[1].processModified, "WELD_START marked processModified after process param edit");

        // Target 2 was already edited above with CNT = 80 and motion = J, speed = 85.0
        check(testOp->targets[2].processModified, "WELD_VIA_1 marked processModified after earlier edit");

        // Edit Target 3: Pose rotation ONLY (30 deg around TCP Z)
        cmdMgr.executeCommand(std::make_unique<olp::app::RotateTargetAroundTcpZCommand>(
            doc, testOp->targets[3].id, olp::core::deg2rad(30.0)));
        check(testOp->targets[3].userModified, "WELD_VIA_2 marked userModified after TCP Z rotation");
        check(!testOp->targets[3].processModified, "WELD_VIA_2 remains !processModified after pure pose edit");

        // Target 4 remains untouched
        check(!testOp->targets[4].userModified, "WELD_VIA_3 is !userModified");
        check(!testOp->targets[4].processModified, "WELD_VIA_3 is !processModified");

        // Regenerate operation targets using RegenerateOperationTargetsCommand
        cmdMgr.executeCommand(std::make_unique<olp::app::RegenerateOperationTargetsCommand>(
            doc, testOp->id));

        // Invariant 1: WELD_START retained manual process change (schedule = 3, processModified = true)
        check(testOp->targets[1].processModified, "Regenerated WELD_START preserves processModified = true");
        check(testOp->targets[1].events.size() == 1, "Regenerated WELD_START retains 1 event");
        if (testOp->targets[1].events.size() == 1) {
            check(testOp->targets[1].events[0].scheduleNumber == 3,
                  "Regenerated WELD_START preserved manual Schedule 3 (not overwritten by default)");
        }

        // Invariant 2: WELD_VIA_1 retained manual process change (CNT = 80, speed = 85.0, motion = J)
        check(testOp->targets[2].processModified, "Regenerated WELD_VIA_1 preserves processModified = true");
        check(testOp->targets[2].cntValue == 80, "Regenerated WELD_VIA_1 preserved manual CNT 80");
        check(testOp->targets[2].motion == olp::core::MotionType::J, "Regenerated WELD_VIA_1 preserved manual motion J");
        check(std::abs(testOp->targets[2].speedValue - 85.0) < 1e-4, "Regenerated WELD_VIA_1 preserved manual speed 85.0");

        // Invariant 3: WELD_VIA_2 (pose edit only) preserved its pose offset, but its process params WERE rewritten per genParams rules!
        check(testOp->targets[3].userModified, "Regenerated WELD_VIA_2 preserved userModified = true");
        check(!testOp->targets[3].processModified, "Regenerated WELD_VIA_2 has processModified = false");
        check(testOp->targets[3].cntValue == 50,
              "Regenerated WELD_VIA_2 (pose-only edit) has CNT reset to generation rule 50 (not locked)");
        check(testOp->targets[3].motion == olp::core::MotionType::L,
              "Regenerated WELD_VIA_2 (pose-only edit) has motion reset to generation rule L");

        // Invariant 4: WELD_VIA_3 unmodified remains clean
        check(!testOp->targets[4].userModified, "Regenerated WELD_VIA_3 remains !userModified");
        check(!testOp->targets[4].processModified, "Regenerated WELD_VIA_3 remains !processModified");
        check(testOp->targets[4].cntValue == 50, "Regenerated WELD_VIA_3 has default rule CNT 50");
    }

#ifdef OLP_HAS_QT6
    // 6. Speed Cell Text Parsing & Round-trip (M09-001 Regression)
    {
        // Test "WELD_SPEED (80 mm/s)"
        const auto p1 = olp::ui::parseSpeedCellText("WELD_SPEED (80 mm/s)");
        check(p1.valid, "parseSpeedCellText('WELD_SPEED (80 mm/s)') is valid");
        check(p1.useWeldSpeed, "parseSpeedCellText('WELD_SPEED (80 mm/s)') has useWeldSpeed = true");
        check(std::abs(p1.speedValue - 80.0) < 1e-4, "parseSpeedCellText('WELD_SPEED (80 mm/s)') speedValue is 80.0");

        // Test "80 mm/s" -> exits WELD_SPEED (useWeldSpeed = false)
        const auto p2 = olp::ui::parseSpeedCellText("80 mm/s");
        check(p2.valid, "parseSpeedCellText('80 mm/s') is valid");
        check(!p2.useWeldSpeed, "parseSpeedCellText('80 mm/s') has useWeldSpeed = false (exits WELD_SPEED)");
        check(std::abs(p2.speedValue - 80.0) < 1e-4, "parseSpeedCellText('80 mm/s') speedValue is 80.0");

        // Test "50%" -> joint motion speed
        const auto p3 = olp::ui::parseSpeedCellText("50%");
        check(p3.valid, "parseSpeedCellText('50%') is valid");
        check(!p3.useWeldSpeed, "parseSpeedCellText('50%') has useWeldSpeed = false");
        check(std::abs(p3.speedValue - 50.0) < 1e-4, "parseSpeedCellText('50%') speedValue is 50.0");

        // Test "50 cm/min" -> converted to mm/s (50 * 10 / 60 = 8.33333 mm/s)
        const auto p4 = olp::ui::parseSpeedCellText("50 cm/min");
        check(p4.valid, "parseSpeedCellText('50 cm/min') is valid");
        check(!p4.useWeldSpeed, "parseSpeedCellText('50 cm/min') has useWeldSpeed = false");
        check(std::abs(p4.speedValue - (50.0 * 10.0 / 60.0)) < 1e-4,
              "parseSpeedCellText('50 cm/min') converts to 8.333 mm/s");

        // Test bare number "250"
        const auto p5 = olp::ui::parseSpeedCellText("250");
        check(p5.valid, "parseSpeedCellText('250') is valid");
        check(!p5.useWeldSpeed, "parseSpeedCellText('250') has useWeldSpeed = false");
        check(std::abs(p5.speedValue - 250.0) < 1e-4, "parseSpeedCellText('250') speedValue is 250.0");

        // Round-trip test with Target 1: WELD_SPEED mode
        olp::core::Target tgtWeld;
        tgtWeld.motion = olp::core::MotionType::L;
        tgtWeld.speedValue = 80.0;
        tgtWeld.useWeldSpeed = true;
        const QString formattedWeld = olp::ui::formatSpeedCellText(tgtWeld);
        check(formattedWeld == "WELD_SPEED (80 mm/s)", "formatSpeedCellText formats WELD_SPEED correctly");
        const auto parsedWeld = olp::ui::parseSpeedCellText(formattedWeld);
        check(parsedWeld.valid && parsedWeld.useWeldSpeed && std::abs(parsedWeld.speedValue - 80.0) < 1e-4,
              "Round-trip format -> parse preserves WELD_SPEED and speedValue");

        // Round-trip test with Target 2: Explicit linear speed mode
        olp::core::Target tgtLinear;
        tgtLinear.motion = olp::core::MotionType::L;
        tgtLinear.speedValue = 150.0;
        tgtLinear.useWeldSpeed = false;
        const QString formattedLinear = olp::ui::formatSpeedCellText(tgtLinear);
        check(formattedLinear == "150 mm/s", "formatSpeedCellText formats linear speed correctly");
        const auto parsedLinear = olp::ui::parseSpeedCellText(formattedLinear);
        check(parsedLinear.valid && !parsedLinear.useWeldSpeed && std::abs(parsedLinear.speedValue - 150.0) < 1e-4,
              "Round-trip format -> parse preserves linear speed without WELD_SPEED");

        // Round-trip test with Target 3: Joint motion percentage mode
        olp::core::Target tgtJoint;
        tgtJoint.motion = olp::core::MotionType::J;
        tgtJoint.speedValue = 60.0;
        tgtJoint.useWeldSpeed = false;
        const QString formattedJoint = olp::ui::formatSpeedCellText(tgtJoint);
        check(formattedJoint == "60%", "formatSpeedCellText formats joint speed percentage correctly");
        const auto parsedJoint = olp::ui::parseSpeedCellText(formattedJoint);
        check(parsedJoint.valid && !parsedJoint.useWeldSpeed && std::abs(parsedJoint.speedValue - 60.0) < 1e-4,
              "Round-trip format -> parse preserves joint speed percentage");
    }
#endif
}

void testKinematicsReachabilitySingularityAndTwistScan() {
    std::cout << "\n--- Test 17: M10 Kinematics Reachability, Singularity Warning, Wire-Tip Collision Exemption & Twist Angle Scan ---\n";

    const auto robot = olp::device::RobotModel::createFanucM10iA();
    olp::algo::OpwKinematics kin(robot);

    // 1. Joint Limits & 5° Warning Margin Check (checkJointLimits)
    {
        bool within = false;
        bool nearLimit = false;

        // Home joints: all nominal within limits
        kin.checkJointLimits(robot.homeJointsRad, within, nearLimit);
        check(within, "Home posture is strictly within joint limits");
        check(!nearLimit, "Home posture is not near any joint limits");

        // J1 upper limit is 170 deg (approx 2.967 rad), warning margin is 5 deg (0.08726 rad)
        // Angle at 168 deg (within limit, but inside 5 deg warning margin)
        auto jNear = robot.homeJointsRad;
        jNear[0] = olp::core::deg2rad(168.0);
        kin.checkJointLimits(jNear, within, nearLimit);
        check(within, "J1 at 168° is within joint limit (< 170°)");
        check(nearLimit, "J1 at 168° triggers near-limit warning (< 5° margin)");

        // J1 at 175 deg (exceeds 170 deg upper limit)
        auto jOver = robot.homeJointsRad;
        jOver[0] = olp::core::deg2rad(175.0);
        kin.checkJointLimits(jOver, within, nearLimit);
        check(!within, "J1 at 175° is correctly flagged as exceeding joint limit");

        // J2 lower limit is -90 deg (-1.5708 rad), test -88 deg (near lower limit)
        auto j2Near = robot.homeJointsRad;
        j2Near[1] = olp::core::deg2rad(-88.0);
        kin.checkJointLimits(j2Near, within, nearLimit);
        check(within && nearLimit, "J2 at -88° triggers near-lower-limit warning");

        // J2 at -95 deg (exceeds lower limit)
        auto j2Over = robot.homeJointsRad;
        j2Over[1] = olp::core::deg2rad(-95.0);
        kin.checkJointLimits(j2Over, within, nearLimit);
        check(!within, "J2 at -95° is correctly flagged as exceeding joint limit");
    }

    // 2. Singularity Detection & Manipulability Index (checkSingularity)
    {
        double manip = 0.0;

        // Nominal posture: safe, non-singular, healthy manipulability
        bool isSing = kin.checkSingularity(robot.homeJointsRad, manip);
        check(!isSing, "Home posture is not near any kinematic singularities");
        check(manip > 0.05, "Home posture has healthy manipulability index (> 0.05, actual: " + std::to_string(manip) + ")");

        // Wrist singularity: J5 = 0.0 deg (axes 4 and 6 become collinear)
        auto jWristSing = robot.homeJointsRad;
        jWristSing[4] = 0.0;
        isSing = kin.checkSingularity(jWristSing, manip);
        check(isSing, "Singularity detected when wrist J5 = 0° (axes 4 and 6 collinear)");
        check(manip < 0.001, "Manipulability index drops to ~0 at wrist singularity (actual: " + std::to_string(manip) + ")");

        // Elbow singularity: arm in collinear extension (q2 + psi = 0)
        const double psi = std::atan2(robot.opw.a2, robot.opw.c3);
        auto qElbow = kin.displayToModelJoints(robot.homeJointsRad);
        qElbow[2] = -psi;
        const auto jElbowSing = kin.modelToDisplayJoints(qElbow);
        isSing = kin.checkSingularity(jElbowSing, manip);
        check(isSing, "Elbow singularity correctly detected when forearm is in collinear extension");

        // Shoulder singularity: wrist center on J1 rotation axis
        auto jShoulderSing = robot.homeJointsRad;
        jShoulderSing[1] = olp::core::deg2rad(45.0);
        jShoulderSing[2] = olp::core::deg2rad(-120.0);
        kin.checkSingularity(jShoulderSing, manip);
        check(manip >= 0.0 && manip <= 1.0, "Manipulability index stays strictly bounded in [0, 1]");
    }

    // 3. FANUC Configuration Consistency & Configuration Jump Detection
    {
        const auto utoolOffset = olp::core::Transform4d::fromTranslation(0.0, 0.0, 150.0);
        const auto fkRes = kin.forwardKinematics(robot.homeJointsRad, utoolOffset);
        const auto homeConfig = fkRes.config;

        const auto ikSols = kin.inverseKinematics(fkRes.tcpPoseInBase, utoolOffset, false);
        check(ikSols.size() >= 2, "Inverse kinematics returns multiple candidate solutions (count: " + std::to_string(ikSols.size()) + ")");

        const auto chosen = kin.selectBestSolution(ikSols, robot.homeJointsRad, homeConfig, true);
        check(chosen.has_value(), "selectBestSolution finds matching solution with locked home configuration");
        if (chosen.has_value()) {
            check(chosen->config.flip == homeConfig.flip, "Selected solution matches locked flip configuration");
            check(chosen->config.up == homeConfig.up, "Selected solution matches locked up configuration");
            check(chosen->config.top == homeConfig.top, "Selected solution matches locked top configuration");
        }

        olp::core::FanucConfig impossibleConfig = homeConfig;
        impossibleConfig.flip = !homeConfig.flip;
        impossibleConfig.up = !homeConfig.up;
        impossibleConfig.top = !homeConfig.top;
        const auto noMatch = kin.selectBestSolution(ikSols, robot.homeJointsRad, impossibleConfig, true);
        if (!noMatch.has_value()) {
            check(true, "ConfigJump prevented: selectBestSolution returns nullopt when locked configuration cannot be preserved");
        } else {
            check(noMatch->config.flip == impossibleConfig.flip, "Found candidate matching forced configuration");
        }
    }

    // 4. Wire-Tip Collision Exemption vs Non-Welding Collision
    {
        olp::core::UnitsSettings settings;
        settings.collisionSafeClearanceMm = 5.0;
        olp::algo::CollisionAndSimEngine simEngine(kin, settings);

        olp::core::SceneNode workpieceNode;
        workpieceNode.id = "mock_workpiece";
        workpieceNode.name = "WorkpiecePlate";
        workpieceNode.type = olp::core::SceneNodeType::Part;
        workpieceNode.collisionRole = olp::core::CollisionRole::Workpiece;
        workpieceNode.visible = true;
        workpieceNode.renderMesh.bboxMin = {0.0, 0.0, 0.0};
        workpieceNode.renderMesh.bboxMax = {200.0, 200.0, 20.0};
        workpieceNode.collisionMesh.bboxMin = {0.0, 0.0, 0.0};
        workpieceNode.collisionMesh.bboxMax = {200.0, 200.0, 20.0};

        olp::core::SceneNode clampNode;
        clampNode.id = "mock_clamp";
        clampNode.name = "Fixture_Clamp_Left";
        clampNode.type = olp::core::SceneNodeType::Part;
        clampNode.collisionRole = olp::core::CollisionRole::Fixture;
        clampNode.visible = true;
        clampNode.renderMesh.bboxMin = {100.0, -50.0, 40.0};
        clampNode.renderMesh.bboxMax = {160.0, 50.0, 80.0};
        clampNode.collisionMesh.bboxMin = {100.0, -50.0, 40.0};
        clampNode.collisionMesh.bboxMax = {160.0, 50.0, 80.0};

        std::vector<olp::core::SceneNode> scene = {workpieceNode, clampNode};

        olp::core::Transform4d weldTcp;
        weldTcp.pos = {100.0, 100.0, 20.0};
        weldTcp.rot = olp::core::Mat3::rotX(olp::core::kPi);

        double clearance = 0.0;
        std::string hitPart;

        bool collided = simEngine.checkTorchPoseCollision(
            weldTcp, scene, "mock_workpiece", true, clearance, hitPart, {"mock_workpiece"});
        check(!collided, "Wire-tip collision exemption: welding role touches workpiece without false collision alarm");

        collided = simEngine.checkTorchPoseCollision(
            weldTcp, scene, "mock_workpiece", false, clearance, hitPart, {});
        check(collided || clearance < 5.0, "Non-welding role at same position correctly detects contact/near-collision");

        olp::core::Transform4d clampHitTcp;
        clampHitTcp.pos = {130.0, 0.0, 20.0};
        clampHitTcp.rot = olp::core::Mat3::rotX(olp::core::kPi);
        collided = simEngine.checkTorchPoseCollision(
            clampHitTcp, scene, "mock_workpiece", true, clearance, hitPart, {"mock_workpiece"});
        check(collided, "Torch nozzle colliding with fixture clamp is strictly detected as collision");
        check(hitPart == "Fixture_Clamp_Left", "Colliding part accurately identified as Fixture_Clamp_Left (got: " + hitPart + ")");
    }

    // 5. 72-Sample Twist Angle Feasibility Scan (scanTwistAngles)
    {
        olp::core::UnitsSettings settings;
        olp::algo::CollisionAndSimEngine simEngine(kin, settings);

        const auto robotBase = olp::core::Transform4d::fromTranslation(0.0, 0.0, 500.0);
        const auto utoolOffset = olp::core::Transform4d::fromTranslation(0.0, 0.0, 150.0);

        const auto fkHome = kin.forwardKinematics(robot.homeJointsRad, utoolOffset);
        const olp::core::Transform4d targetPose = robotBase * fkHome.tcpPoseInBase;

        const auto samples = simEngine.scanTwistAngles(
            targetPose,
            robotBase,
            utoolOffset,
            robot.homeJointsRad,
            kin.computeConfig(robot.homeJointsRad),
            {},
            "",
            5.0);

        check(samples.size() == 72, "Twist angle scan produces exactly 72 samples spanning [-180°, 175°]");
        if (samples.size() == 72) {
            check(std::abs(samples.front().twistAngleDeg - (-180.0)) < 1e-4, "First sample angle is -180.0°");
            check(std::abs(samples.back().twistAngleDeg - (175.0)) < 1e-4, "Last sample angle is +175.0°");

            size_t reachableCount = 0;
            for (const auto& s : samples) {
                if (s.reachable) ++reachableCount;
            }
            check(reachableCount > 0, "Twist angle scan identifies reachable sector (reachable: " + std::to_string(reachableCount) + "/72)");
        }

#ifdef OLP_HAS_QT6
        olp::ui::TwistScanWidget widget;
        std::vector<olp::algo::TwistScanSample> testSamples;
        for (double d = -180.0; d < 180.0 - 1e-5; d += 5.0) {
            olp::algo::TwistScanSample ts;
            ts.twistAngleDeg = d;
            if (d >= -30.0 && d <= 50.0) {
                ts.reachable = true;
                ts.collisionFree = true;
                ts.nearSingularityOrLimit = false;
                ts.minClearanceMm = 15.0;
            } else {
                ts.reachable = false;
                ts.collisionFree = false;
            }
            testSamples.push_back(ts);
        }
        widget.setSamples(testSamples, 0.0);
        const auto opt = widget.findOptimalFeasibleAngleDeg();
        check(opt.has_value(), "TwistScanWidget finds optimal feasible angle");
        if (opt.has_value()) {
            check(std::abs(*opt - 10.0) < 1e-4,
                  "Optimal feasible angle is accurately at center of [-30°, 50°] range (+10°, got: " + std::to_string(*opt) + "°)");
        }
#endif
    }

    // 6. Interactive MainWindow::validateOperation & Issue Navigation
    {
        olp::ui::MainWindow mainWin;
        olp::core::SeamDefinition seam;
        seam.id = "seam_m10";
        seam.name = "SeamM10";
        for (int i = 0; i < 3; ++i) {
            olp::core::SeamSampleFrame f;
            f.arcLengthMm = i * 30.0;
            f.position = {600.0 + i * 30.0, 0.0, 500.0};
            f.tangentX = {1.0, 0.0, 0.0};
            f.normalFace1 = {0.0, 1.0, 0.0};
            f.normalFace2 = {0.0, 0.0, 1.0};
            f.bisector = {0.0, 0.70710678, 0.70710678};
            f.frame = olp::core::Transform4d::identity();
            f.frame.pos = f.position;
            seam.sampledFrames.push_back(f);
        }
        mainWin.document().seams.push_back(seam);

        olp::core::SceneNode robotNode;
        robotNode.id = "robot_m10";
        robotNode.type = olp::core::SceneNodeType::Robot;
        robotNode.localTransform = olp::core::Transform4d::fromTranslation(0.0, 0.0, 500.0);
        mainWin.document().sceneNodes.push_back(robotNode);

        olp::core::ToolpathGenParams genParams;
        genParams.endpointsOnlyForLine = false;
        const bool genOk = mainWin.generateOperationFromSelectedSeam(&genParams);
        check(genOk, "MainWindow::generateOperationFromSelectedSeam succeeds");
        check(!mainWin.selectedOperationId().empty(), "Operation is selected after generation");

        const auto simResult = mainWin.validateOperation(mainWin.selectedOperationId());
        check(mainWin.lastSimulationResult().frames.size() == simResult.frames.size(),
              "MainWindow caches lastSimulationResult");

        const auto* op = mainWin.document().findOperation(mainWin.selectedOperationId());
        check(op != nullptr, "Generated operation found in document");
        if (op && !op->targets.empty()) {
            check(op->targets.front().cache.valid, "First target cache is marked valid after validation");
            check(op->targets.front().cache.status != olp::core::TargetValidationState::Unchecked,
                  "First target cache status is evaluated (not Unchecked)");

            // Use WELD_START (target 1 on the seam, z=500) for rotation, SLERP, and exemption tests
            const std::string t0Id = (op->targets.size() > 1) ? op->targets[1].id : op->targets.front().id;
            mainWin.selectTarget(t0Id, op->id);

            // M10-001: Return value, robot joints sync, and Undo/Redo cache symmetry
            const bool zeroRot = mainWin.rotateTargetsTcpZ({t0Id}, 0.0);
            check(!zeroRot, "M10-001: Zero-delta rotation returns false and is not stacked");

            const auto origPose = mainWin.document().findTarget(t0Id)->poseInUFrame;
            const auto origJoints = mainWin.document().findTarget(t0Id)->cache.jointsRad;
            const bool origUserMod = mainWin.document().findTarget(t0Id)->userModified;

            const bool rotOk = mainWin.rotateTargetsTcpZ({t0Id}, olp::core::deg2rad(15.0));
            check(rotOk, "M10-001: rotateTargetsTcpZ returns true when actually mutated");
            check(mainWin.document().findTarget(t0Id)->userModified, "M10-001: Target marked userModified after rotation");
            check(mainWin.document().findTarget(t0Id)->cache.valid, "M10-001: Target cache re-validated and valid after rotation");

            const auto* rNode = mainWin.document().findSceneNode("robot_m10");
            check(rNode != nullptr, "Robot node found in scene");
            if (rNode) {
                check(rNode->currentJointsRad == mainWin.document().findTarget(t0Id)->cache.jointsRad,
                      "M10-001: Robot node currentJointsRad matches newly validated target cache joints");
            }

            // Undo: pose, userModified, cache and robot joints restore exactly
            mainWin.commandManager().undo();
            const auto* tAfterUndo = mainWin.document().findTarget(t0Id);
            check((tAfterUndo->poseInUFrame.pos - origPose.pos).norm() < 1e-4, "M10-001: Target position restored after undo");
            check(std::abs(tAfterUndo->poseInUFrame.toWPR().wDeg - origPose.toWPR().wDeg) < 1e-4,
                  "M10-001: Target orientation restored after undo");
            check(tAfterUndo->userModified == origUserMod, "M10-001: userModified restored after undo");
            check(tAfterUndo->cache.valid, "M10-001: Target cache valid restored after undo");
            check(tAfterUndo->cache.jointsRad == origJoints, "M10-001: Target cache joints restored after undo");
            if (rNode) {
                check(rNode->currentJointsRad == origJoints, "M10-001: Robot node joints restored to match undone cache joints");
            }

            // Redo: pose, userModified, cache and robot joints match redone state
            mainWin.commandManager().redo();
            const auto* tAfterRedo = mainWin.document().findTarget(t0Id);
            check(tAfterRedo->cache.valid, "M10-001: Target cache valid after redo");
            check(tAfterRedo->userModified, "M10-001: userModified true after redo");
            if (rNode) {
                check(rNode->currentJointsRad == tAfterRedo->cache.jointsRad,
                      "M10-001: Robot node joints match redone cache joints");
            }

            // SLERP smoothing across rotated WELD_START and unrotated WELD_END
            const bool smoothOk = mainWin.smoothTargetOrientations();
            check(smoothOk, "M10-001: smoothTargetOrientations returns true when targets smoothed");

            // M10-002: scanSelectedTargetTwistAngles locks solvedConfig and distinguishes welding vs approach role
            const auto targetSamples = mainWin.scanSelectedTargetTwistAngles(5.0);
            check(targetSamples.size() == 72, "M10-002: scanSelectedTargetTwistAngles returns 72 samples for selected target");

            auto itZero = std::find_if(targetSamples.begin(), targetSamples.end(),
                [](const olp::algo::TwistScanSample& s) { return std::abs(s.twistAngleDeg) < 1e-4; });
            check(itZero != targetSamples.end() && itZero->reachable,
                  "M10-002: Current angle 0° is reachable under locked solvedConfig");

            // Workpiece at weld root (z = 500)
            olp::core::SceneNode wpNode;
            wpNode.id = "wp_test_m10";
            wpNode.name = "WorkpiecePlate";
            wpNode.type = olp::core::SceneNodeType::Part;
            wpNode.collisionRole = olp::core::CollisionRole::Workpiece;
            wpNode.visible = true;
            wpNode.collisionMesh.bboxMin = {550.0, -100.0, 480.0};
            wpNode.collisionMesh.bboxMax = {750.0, 100.0, 500.0};
            mainWin.document().sceneNodes.push_back(wpNode);

            auto* opMut = mainWin.document().findOperation(mainWin.selectedOperationId());
            opMut->targetWorkpieceId = "wp_test_m10";

            auto scanWeld = mainWin.scanSelectedTargetTwistAngles(5.0);
            auto itZeroWeld = std::find_if(scanWeld.begin(), scanWeld.end(),
                [](const olp::algo::TwistScanSample& s) { return std::abs(s.twistAngleDeg) < 1e-4; });
            check(itZeroWeld != scanWeld.end() && itZeroWeld->collisionFree,
                  "M10-002: Welding role wire tip touches workpiece -> collisionFree = true (exemption)");

            // Change role to APPROACH at identical geometric position -> wire tip is NOT exempted
            auto* targetMut = mainWin.document().findTarget(t0Id);
            const auto origRole = targetMut->role;
            targetMut->role = olp::core::TargetRole::APPROACH;
            auto scanApproach = mainWin.scanSelectedTargetTwistAngles(5.0);
            auto itZeroAppr = std::find_if(scanApproach.begin(), scanApproach.end(),
                [](const olp::algo::TwistScanSample& s) { return std::abs(s.twistAngleDeg) < 1e-4; });
            check(itZeroAppr != scanApproach.end() && !itZeroAppr->collisionFree,
                  "M10-002: APPROACH role at same pose touches workpiece -> collisionFree = false (no exemption)");
            targetMut->role = origRole;

            // M10-003: Mid-segment sweep collision appears in SimulationResult and ShellWindow issue list
            // Place obstacle at the midpoint between target 1 and target 2 along torch body offset (95mm)
            const auto midPos = (op->targets[1].poseInUFrame.pos + op->targets[2].poseInUFrame.pos) * 0.5;
            const auto midRot = op->targets[1].poseInUFrame.rot;
            const olp::core::Vec3 clampCenter = midPos + (-midRot.colZ()) * 95.0;
            constexpr double h = 8.0;

            olp::core::SceneNode sweepClamp;
            sweepClamp.id = "sweep_clamp_m10";
            sweepClamp.name = "Fixture_Clamp_Middle";
            sweepClamp.type = olp::core::SceneNodeType::Part;
            sweepClamp.collisionRole = olp::core::CollisionRole::Fixture;
            sweepClamp.visible = true;
            sweepClamp.collisionMesh.bboxMin = clampCenter - olp::core::Vec3{h, h, h};
            sweepClamp.collisionMesh.bboxMax = clampCenter + olp::core::Vec3{h, h, h};
            mainWin.document().sceneNodes.push_back(sweepClamp);

            const auto simSweep = mainWin.validateOperation(mainWin.selectedOperationId());
            bool foundSweepIssue = false;
            for (const auto& issue : simSweep.issues) {
                if (issue.description.find("Fixture_Clamp_Middle") != std::string::npos) {
                    foundSweepIssue = true;
                    break;
                }
            }
            check(foundSweepIssue, "M10-003: SimulationResult reports sweep collision against mid-segment clamp");

#ifdef OLP_HAS_QT6
            olp::ui::ShellWindow shell(mainWin);
            const auto listWidgets = shell.findChildren<QListWidget*>();
            bool shellHasSweepIssue = false;
            for (auto* lw : listWidgets) {
                for (int i = 0; i < lw->count(); ++i) {
                    auto* item = lw->item(i);
                    if (item && item->text().contains("Fixture_Clamp_Middle")) {
                        shellHasSweepIssue = true;
                        check(!item->data(Qt::UserRole).toString().isEmpty(), "M10-003: Issue item carries targetId");
                        check(!item->data(Qt::UserRole + 1).toString().isEmpty(), "M10-003: Issue item carries operationId");
                        break;
                    }
                }
                if (shellHasSweepIssue) break;
            }
            check(shellHasSweepIssue, "M10-003: ShellWindow issueList_ displays sweep collision item for navigation");
#endif
        }
    }
}

} // namespace

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

    std::cout << "\n====================================================================\n";
    std::cout << "  Summary: " << g_passed << " passed, " << g_failed << " failed.\n";
    std::cout << "====================================================================\n";
    return (g_failed == 0) ? 0 : 1;
}
