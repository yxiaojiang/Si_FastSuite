#include "test_support.h"
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

int findSegment(const std::vector<olp::cad::CadEdgeInfo>& edges, const olp::core::Vec3& a, const olp::core::Vec3& b) {
    for (int i = 0; i < static_cast<int>(edges.size()); ++i) {
        if (segmentsMatch(edges[static_cast<std::size_t>(i)], a, b, 0.05)) return i;
    }
    return -1;
}


} // namespace

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


