#include "olp/ui/MainWindow.h"

#include <algorithm>

namespace olp::ui {

MainWindow::MainWindow() = default;

MainWindow::~MainWindow() = default;

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
    cmdMgr_.executeCommand(std::move(cmd));
    return true;
}

bool MainWindow::rotateTargetTcpZ(const std::string& targetId, double angleRad) {
    if (targetId.empty() || std::abs(angleRad) < 1e-6) {
        return false;
    }
    auto cmd = std::make_unique<app::RotateTargetAroundTcpZCommand>(doc_, targetId, angleRad);
    cmdMgr_.executeCommand(std::move(cmd));
    return true;
}

bool MainWindow::rotateTargetsTcpZ(const std::vector<std::string>& targetIds, double angleRad) {
    if (targetIds.empty() || std::abs(angleRad) < 1e-6) {
        return false;
    }
    auto cmd = std::make_unique<app::RotateTargetAroundTcpZCommand>(doc_, targetIds, angleRad);
    cmdMgr_.executeCommand(std::move(cmd));
    return true;
}

bool MainWindow::modifyTargetProcessParams(
    const std::string& targetId,
    const app::TargetProcessParams& params) {
    if (targetId.empty()) {
        return false;
    }
    auto cmd = std::make_unique<app::ModifyTargetProcessParamsCommand>(doc_, targetId, params);
    cmdMgr_.executeCommand(std::move(cmd));
    return true;
}

bool MainWindow::modifyTargetsProcessParams(
    const std::vector<std::string>& targetIds,
    const app::TargetProcessParams& params) {
    if (targetIds.empty()) {
        return false;
    }
    auto cmd = std::make_unique<app::ModifyTargetProcessParamsCommand>(doc_, targetIds, params);
    cmdMgr_.executeCommand(std::move(cmd));
    return true;
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
