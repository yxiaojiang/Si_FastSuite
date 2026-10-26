#include "olp/ui/MainWindow.h"

#include <algorithm>

namespace olp::ui {

MainWindow::MainWindow() {
    cmdMgr_.addDocumentChangedListener([this] {
        syncRobotJointsFromSelectedTarget();
    });
}

MainWindow::~MainWindow() = default;

void MainWindow::syncRobotJointsFromSelectedTarget() {
    if (selectedTargetId_.empty()) return;
    const auto* t = doc_.findTarget(selectedTargetId_);
    if (t != nullptr && t->cache.valid) {
        for (auto& node : doc_.sceneNodes) {
            if (node.type == core::SceneNodeType::Robot) {
                node.currentJointsRad = t->cache.jointsRad;
                break;
            }
        }
    }
}

void MainWindow::selectSceneNode(const std::string& nodeId) {
    if (selectedSceneNodeId_ == nodeId && selectedTargetId_.empty()) {
        return;
    }
    selectedSceneNodeId_ = nodeId;
    selectedTargetId_.clear();
    notifySelectionChanged();
}

void MainWindow::selectOperation(const std::string& operationId) {
    if (selectedOperationId_ == operationId && selectedTargetId_.empty() && selectedSceneNodeId_.empty()) {
        return;
    }
    selectedOperationId_ = operationId;
    selectedTargetId_.clear();
    selectedSceneNodeId_.clear();
    notifySelectionChanged();
}

void MainWindow::selectTarget(const std::string& targetId, const std::string& operationId) {
    if (selectedTargetId_ == targetId && (operationId.empty() || selectedOperationId_ == operationId) && selectedSceneNodeId_.empty()) {
        return;
    }
    selectedTargetId_ = targetId;
    if (!operationId.empty()) {
        selectedOperationId_ = operationId;
    } else if (!targetId.empty()) {
        std::string foundOpId;
        for (const auto& cu : doc_.controllers) {
            for (const auto& prog : cu.programs) {
                for (const auto& op : prog.operations) {
                    for (const auto& t : op.targets) {
                        if (t.id == targetId) {
                            foundOpId = op.id;
                            break;
                        }
                    }
                    if (!foundOpId.empty()) break;
                }
                if (!foundOpId.empty()) break;
            }
            if (!foundOpId.empty()) break;
        }
        if (!foundOpId.empty()) {
            selectedOperationId_ = foundOpId;
        }
    }
    selectedSceneNodeId_.clear();

    const auto* target = doc_.findTarget(targetId);
    if (target != nullptr && target->cache.valid) {
        for (auto& node : doc_.sceneNodes) {
            if (node.type == core::SceneNodeType::Robot) {
                node.currentJointsRad = target->cache.jointsRad;
                break;
            }
        }
    }

    notifySelectionChanged();
}

bool MainWindow::stepTarget(bool forward, bool wrap) {
    const core::Operation* activeOp = nullptr;
    if (!selectedOperationId_.empty()) {
        activeOp = doc_.findOperation(selectedOperationId_);
    }
    if (activeOp == nullptr && !selectedTargetId_.empty()) {
        for (const auto& cu : doc_.controllers) {
            for (const auto& prog : cu.programs) {
                for (const auto& op : prog.operations) {
                    for (const auto& t : op.targets) {
                        if (t.id == selectedTargetId_) {
                            activeOp = &op;
                            break;
                        }
                    }
                    if (activeOp) break;
                }
                if (activeOp) break;
            }
            if (activeOp) break;
        }
    }
    if (activeOp == nullptr) {
        for (const auto& cu : doc_.controllers) {
            for (const auto& prog : cu.programs) {
                if (!prog.operations.empty()) {
                    activeOp = &prog.operations.front();
                    break;
                }
            }
            if (activeOp) break;
        }
    }
    if (activeOp == nullptr || activeOp->targets.empty()) {
        return false;
    }

    const auto& targets = activeOp->targets;
    int currIdx = -1;
    for (size_t i = 0; i < targets.size(); ++i) {
        if (targets[i].id == selectedTargetId_) {
            currIdx = static_cast<int>(i);
            break;
        }
    }

    int nextIdx = 0;
    if (currIdx < 0) {
        nextIdx = forward ? 0 : static_cast<int>(targets.size()) - 1;
    } else {
        if (forward) {
            if (currIdx + 1 < static_cast<int>(targets.size())) {
                nextIdx = currIdx + 1;
            } else {
                nextIdx = wrap ? 0 : currIdx;
            }
        } else {
            if (currIdx > 0) {
                nextIdx = currIdx - 1;
            } else {
                nextIdx = wrap ? (static_cast<int>(targets.size()) - 1) : 0;
            }
        }
    }

    if (nextIdx == currIdx && currIdx >= 0) {
        return false;
    }

    selectTarget(targets[static_cast<size_t>(nextIdx)].id, activeOp->id);
    return true;
}

bool MainWindow::generateOperationFromSelectedSeam(const core::ToolpathGenParams* params) {
    std::string seamId{};
    if (!selectedOperationId_.empty()) {
        const auto* op = doc_.findOperation(selectedOperationId_);
        if (op != nullptr && !op->seamId.empty()) {
            seamId = op->seamId;
        }
    }
    if (seamId.empty() && !doc_.seams.empty()) {
        seamId = doc_.seams.front().id;
    }
    if (seamId.empty()) {
        return false;
    }

    auto cmd = std::make_unique<app::GenerateOperationFromSeamCommand>(doc_, seamId, params);
    const std::string opId = cmd->operationId();
    cmdMgr_.executeCommand(std::move(cmd));
    selectOperation(opId);
    validateOperation(opId);
    return true;
}

bool MainWindow::smoothTargetOrientations(
    const std::string& startTargetId,
    const std::string& endTargetId,
    const std::string& operationId) {
    std::string opId = operationId.empty() ? selectedOperationId_ : operationId;
    if (opId.empty() && !startTargetId.empty()) {
        for (const auto& cu : doc_.controllers) {
            for (const auto& prog : cu.programs) {
                for (const auto& op : prog.operations) {
                    for (const auto& t : op.targets) {
                        if (t.id == startTargetId) {
                            opId = op.id;
                            break;
                        }
                    }
                    if (!opId.empty()) break;
                }
                if (!opId.empty()) break;
            }
            if (!opId.empty()) break;
        }
    }
    if (opId.empty()) {
        for (const auto& cu : doc_.controllers) {
            for (const auto& prog : cu.programs) {
                if (!prog.operations.empty()) {
                    opId = prog.operations.front().id;
                    break;
                }
            }
            if (!opId.empty()) break;
        }
    }
    if (opId.empty()) {
        return false;
    }

    auto cmd = std::make_unique<app::SmoothTargetOrientationsCommand>(
        doc_, opId, startTargetId, endTargetId);
    const bool mutated = cmdMgr_.executeCommand(std::move(cmd));
    if (mutated) {
        validateOperation(opId);
        syncRobotJointsFromSelectedTarget();
        cmdMgr_.notifyDocumentChanged();
    }
    return mutated;
}

bool MainWindow::rotateTargetTcpZ(const std::string& targetId, double angleRad) {
    return rotateTargetsTcpZ({targetId}, angleRad);
}

bool MainWindow::rotateTargetsTcpZ(const std::vector<std::string>& targetIds, double angleRad) {
    if (targetIds.empty() || std::abs(angleRad) < 1e-6) {
        return false;
    }
    auto cmd = std::make_unique<app::RotateTargetAroundTcpZCommand>(doc_, targetIds, angleRad);
    const bool mutated = cmdMgr_.executeCommand(std::move(cmd));
    if (mutated) {
        if (!selectedOperationId_.empty()) {
            validateOperation(selectedOperationId_);
        } else {
            validateCurrentOperation();
        }
        syncRobotJointsFromSelectedTarget();
        cmdMgr_.notifyDocumentChanged();
    }
    return mutated;
}

bool MainWindow::modifyTargetProcessParams(
    const std::string& targetId,
    const app::TargetProcessParams& params) {
    return modifyTargetsProcessParams({targetId}, params);
}

bool MainWindow::modifyTargetsProcessParams(
    const std::vector<std::string>& targetIds,
    const app::TargetProcessParams& params) {
    if (targetIds.empty()) {
        return false;
    }
    auto cmd = std::make_unique<app::ModifyTargetProcessParamsCommand>(doc_, targetIds, params);
    const bool mutated = cmdMgr_.executeCommand(std::move(cmd));
    if (mutated) {
        if (!selectedOperationId_.empty()) {
            validateOperation(selectedOperationId_);
        } else {
            validateCurrentOperation();
        }
        syncRobotJointsFromSelectedTarget();
        cmdMgr_.notifyDocumentChanged();
    }
    return mutated;
}

algo::SimulationResult MainWindow::validateOperation(const std::string& operationId) {
    algo::SimulationResult result;
    core::Operation* op = doc_.findOperation(operationId);
    if (!op) return result;

    core::Program* prog = nullptr;
    for (auto& cu : doc_.controllers) {
        for (auto& p : cu.programs) {
            for (auto& o : p.operations) {
                if (o.id == operationId) {
                    prog = &p;
                    break;
                }
            }
            if (prog) break;
        }
        if (prog) break;
    }
    if (!prog) return result;

    core::Transform4d robotBaseInWorld = core::Transform4d::fromTranslation(0.0, 0.0, 500.0);
    for (const auto& node : doc_.sceneNodes) {
        if (node.type == core::SceneNodeType::Robot) {
            robotBaseInWorld = node.localTransform;
            break;
        }
    }

    const core::Transform4d uframeInWorld = doc_.computeUFrameInWorld(prog->uframeNumber);
    core::Transform4d utoolOffset = core::Transform4d::fromTranslation(0.0, 0.0, 150.0);
    for (const auto& node : doc_.sceneNodes) {
        if (node.type == core::SceneNodeType::Tool) {
            utoolOffset = node.localTransform;
            break;
        }
    }

    algo::OpwKinematics kin(robotModel_);
    algo::CollisionAndSimEngine simEngine(kin, doc_.settings);

    result = simEngine.validateAndSimulateProgram(
        *prog,
        robotBaseInWorld,
        uframeInWorld,
        utoolOffset,
        doc_.sceneNodes,
        0.05);

    lastSimulationResult_ = result;
    return result;
}

algo::SimulationResult MainWindow::validateCurrentOperation() {
    if (selectedOperationId_.empty()) return {};
    return validateOperation(selectedOperationId_);
}

std::vector<algo::TwistScanSample> MainWindow::scanSelectedTargetTwistAngles(double stepDeg) const {
    if (selectedTargetId_.empty()) return {};
    const core::Target* target = doc_.findTarget(selectedTargetId_);
    if (!target) return {};

    const core::Operation* op = nullptr;
    const core::Program* prog = nullptr;
    for (const auto& cu : doc_.controllers) {
        for (const auto& p : cu.programs) {
            for (const auto& o : p.operations) {
                for (const auto& t : o.targets) {
                    if (t.id == selectedTargetId_) {
                        op = &o;
                        prog = &p;
                        break;
                    }
                }
                if (op) break;
            }
            if (op) break;
        }
        if (op) break;
    }
    if (!op || !prog) return {};

    core::Transform4d robotBaseInWorld = core::Transform4d::fromTranslation(0.0, 0.0, 500.0);
    for (const auto& node : doc_.sceneNodes) {
        if (node.type == core::SceneNodeType::Robot) {
            robotBaseInWorld = node.localTransform;
            break;
        }
    }

    const core::Transform4d uframeInWorld = doc_.computeUFrameInWorld(prog->uframeNumber);
    core::Transform4d utoolOffset = core::Transform4d::fromTranslation(0.0, 0.0, 150.0);
    for (const auto& node : doc_.sceneNodes) {
        if (node.type == core::SceneNodeType::Tool) {
            utoolOffset = node.localTransform;
            break;
        }
    }

    const core::Transform4d baseTargetWorldPose = uframeInWorld * target->poseInUFrame;
    std::optional<core::FanucConfig> requiredConfig = std::nullopt;
    std::array<double, 6> refJoints = prog->startState.homeJointsRad;

    const bool hasPriorSolution =
        target->cache.valid ||
        target->cache.status != core::TargetValidationState::Unchecked;

    if (hasPriorSolution) {
        requiredConfig = target->cache.solvedConfig;
        refJoints = target->cache.jointsRad;
    }

    const bool isWeldRole =
        target->role == core::TargetRole::WELD_START ||
        target->role == core::TargetRole::WELD_VIA ||
        target->role == core::TargetRole::WELD_END;

    algo::OpwKinematics kin(robotModel_);
    algo::CollisionAndSimEngine simEngine(kin, doc_.settings);

    return simEngine.scanTwistAngles(
        baseTargetWorldPose,
        robotBaseInWorld,
        utoolOffset,
        refJoints,
        requiredConfig,
        doc_.sceneNodes,
        op->targetWorkpieceId,
        stepDeg,
        op->exemptionPartIds,
        isWeldRole);
}

int MainWindow::addSelectionListener(std::function<void()> listener) {
    const int id = nextSelectionListenerId_++;
    selectionListeners_.push_back(SelectionListener{id, std::move(listener)});
    return id;
}

void MainWindow::removeSelectionListener(int listenerId) {
    selectionListeners_.erase(
        std::remove_if(selectionListeners_.begin(), selectionListeners_.end(),
            [&](const SelectionListener& listener) { return listener.id == listenerId; }),
        selectionListeners_.end());
}

void MainWindow::noteSampleRun(int exitCode, int frameCount, double cycleTimeSec, double weldTimeSec) noexcept {
    lastSampleExitCode_ = exitCode;
    lastFrameCount_ = frameCount;
    lastCycleTimeSec_ = cycleTimeSec;
    lastWeldTimeSec_ = weldTimeSec;
}

void MainWindow::notifySelectionChanged() {
    const auto listeners = selectionListeners_;
    for (const auto& listener : listeners) {
        if (listener.fn) {
            listener.fn();
        }
    }
}

} // namespace olp::ui
