#include "olp/app/WorkcellLayoutService.h"
#include <algorithm>

namespace olp::app {

SceneRobotReplaceResult WorkcellLayoutService::importFullSceneAndReplaceRobot(
    ProjectDocument& doc,
    cad::ICadKernel& cadKernel,
    const std::string& stepFilePath,
    const device::RobotModel& libraryRobotModel) {
    SceneRobotReplaceResult res;
    std::vector<core::SceneNode> importedNodes;
    if (!cadKernel.importStep(stepFilePath, importedNodes)) {
        return res;
    }

    res.totalSolidsImported = static_cast<int>(importedNodes.size());

    bool foundStaticBase = false;
    core::Vec3 baseMountPos{0.0, 0.0, 0.0};

    for (auto& node : importedNodes) {
        // Identify static robot solids by naming convention ("Static_" or "FANUC_")
        if (node.name.find("Static_") != std::string::npos ||
            node.name.find("FANUC_") != std::string::npos) {
            if (node.name.find("Static_Base") != std::string::npos) {
                foundStaticBase = true;
                baseMountPos.x = 0.5 * (node.renderMesh.bboxMin.x + node.renderMesh.bboxMax.x);
                baseMountPos.y = 0.5 * (node.renderMesh.bboxMin.y + node.renderMesh.bboxMax.y);
                baseMountPos.z = node.renderMesh.bboxMin.z; // Bottom mounting plane of robot base
            }
            // Scheme A: Hide static robot solid and exclude from collision checking
            node.visible = false;
            node.collisionRole = core::CollisionRole::Ignored;
            ++res.staticRobotSolidsHidden;
        } else if (!foundStaticBase && node.name.find("Pedestal_Base") != std::string::npos) {
            baseMountPos.x = 0.5 * (node.renderMesh.bboxMin.x + node.renderMesh.bboxMax.x);
            baseMountPos.y = 0.5 * (node.renderMesh.bboxMin.y + node.renderMesh.bboxMax.y);
            baseMountPos.z = node.renderMesh.bboxMax.z; // Top mounting surface of pedestal
        }

        doc.sceneNodes.push_back(std::move(node));
    }

    // Place movable library robot at the detected base mounting transform
    core::SceneNode robotNode;
    robotNode.id = "robot_active_" + libraryRobotModel.modelId;
    robotNode.name = libraryRobotModel.displayName;
    robotNode.type = core::SceneNodeType::Robot;
    robotNode.collisionRole = core::CollisionRole::RobotLink;
    robotNode.robotModelId = libraryRobotModel.modelId;
    robotNode.currentJointsRad = libraryRobotModel.homeJointsRad;
    robotNode.localTransform = core::Transform4d::fromTranslation(
        baseMountPos.x, baseMountPos.y, baseMountPos.z);

    res.activeRobotNodeId = robotNode.id;
    res.detectedRobotBaseTransform = robotNode.localTransform;
    doc.sceneNodes.push_back(std::move(robotNode));

    res.success = true;
    doc.isDirty = true;
    return res;
}

std::string WorkcellLayoutService::defineUFrameByThreePoints(
    ProjectDocument& doc,
    int uframeNumber,
    const std::string& frameName,
    const core::Vec3& originWorld,
    const core::Vec3& ptXWorld,
    const core::Vec3& ptXYWorld) {
    const core::Vec3 xDir = ptXWorld - originWorld;
    const core::Vec3 yDir = ptXYWorld - originWorld;
    if (xDir.norm() < 1e-6 || xDir.cross(yDir).norm() < 1e-6) {
        return {};
    }

    const std::string nodeId = "uframe_" + std::to_string(uframeNumber);
    for (auto& n : doc.sceneNodes) {
        if (n.id == nodeId) {
            n.name = frameName;
            n.localTransform = core::Transform4d::fromThreePoints(originWorld, ptXWorld, ptXYWorld);
            doc.isDirty = true;
            return n.id;
        }
    }

    core::SceneNode frameNode;
    frameNode.id = nodeId;
    frameNode.name = frameName;
    frameNode.type = core::SceneNodeType::Frame;
    frameNode.collisionRole = core::CollisionRole::Ignored;
    frameNode.uframeNumber = uframeNumber;
    frameNode.localTransform = core::Transform4d::fromThreePoints(originWorld, ptXWorld, ptXYWorld);
    doc.sceneNodes.push_back(std::move(frameNode));
    doc.isDirty = true;
    return nodeId;
}

ImportSceneAndReplaceRobotCommand::ImportSceneAndReplaceRobotCommand(
    ProjectDocument& doc,
    cad::ICadKernel& cadKernel,
    std::string stepFilePath,
    device::RobotModel libraryRobotModel,
    SceneRobotReplaceResult* resultOut)
    : doc_(doc),
      cadKernel_(cadKernel),
      stepFilePath_(std::move(stepFilePath)),
      libraryRobotModel_(std::move(libraryRobotModel)),
      resultOut_(resultOut) {}

void ImportSceneAndReplaceRobotCommand::execute() {
    if (!applied_) {
        beforeNodes_ = doc_.sceneNodes;
        beforeDirty_ = doc_.isDirty;
        result_ = WorkcellLayoutService::importFullSceneAndReplaceRobot(
            doc_, cadKernel_, stepFilePath_, libraryRobotModel_);
        afterNodes_ = doc_.sceneNodes;
        applied_ = true;
        mutated_ = result_.success;
        if (!mutated_) {
            doc_.sceneNodes = beforeNodes_;
            doc_.isDirty = beforeDirty_;
        }
        if (resultOut_ != nullptr) *resultOut_ = result_;
        return;
    }
    if (!mutated_) return;
    doc_.sceneNodes = afterNodes_;
    doc_.isDirty = true;
    if (resultOut_ != nullptr) *resultOut_ = result_;
}

void ImportSceneAndReplaceRobotCommand::undo() {
    if (!mutated_) return;
    doc_.sceneNodes = beforeNodes_;
    doc_.isDirty = beforeDirty_;
}

DefineUFrameCommand::DefineUFrameCommand(
    ProjectDocument& doc,
    int uframeNumber,
    std::string frameName,
    core::Vec3 originWorld,
    core::Vec3 ptXWorld,
    core::Vec3 ptXYWorld,
    std::string* nodeIdOut)
    : doc_(doc),
      uframeNumber_(uframeNumber),
      frameName_(std::move(frameName)),
      originWorld_(originWorld),
      ptXWorld_(ptXWorld),
      ptXYWorld_(ptXYWorld),
      nodeIdOut_(nodeIdOut) {}

void DefineUFrameCommand::execute() {
    if (!applied_) {
        beforeNodes_ = doc_.sceneNodes;
        beforeDirty_ = doc_.isDirty;
        nodeId_ = WorkcellLayoutService::defineUFrameByThreePoints(
            doc_, uframeNumber_, frameName_, originWorld_, ptXWorld_, ptXYWorld_);
        afterNodes_ = doc_.sceneNodes;
        applied_ = true;
        mutated_ = !nodeId_.empty();
        if (!mutated_) {
            doc_.sceneNodes = beforeNodes_;
            doc_.isDirty = beforeDirty_;
        }
        if (nodeIdOut_ != nullptr) *nodeIdOut_ = nodeId_;
        return;
    }
    if (!mutated_) return;
    doc_.sceneNodes = afterNodes_;
    doc_.isDirty = true;
    if (nodeIdOut_ != nullptr) *nodeIdOut_ = nodeId_;
}

void DefineUFrameCommand::undo() {
    if (!mutated_) return;
    doc_.sceneNodes = beforeNodes_;
    doc_.isDirty = beforeDirty_;
}

} // namespace olp::app
