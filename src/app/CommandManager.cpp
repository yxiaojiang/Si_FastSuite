#include "olp/app/CommandManager.h"
#include "olp/algo/SeamAndToolpathAlgo.h"
#include <algorithm>

namespace olp::app {

ProjectDocument::ProjectDocument() {
    core::WeldScheduleDefinition sched1;
    sched1.scheduleNumber = 1;
    sched1.currentAmps = 180.0;
    sched1.voltageVolts = 22.0;
    sched1.wireFeedSpeedMPerMin = 6.0;
    sched1.setWeldSpeedCmPerMin(50.0);
    sched1.gasFlowRateLPerMin = 15.0;
    sched1.description = "Schedule 1: 180A, 22V, 50 cm/min";
    weldSchedules.push_back(sched1);
}

core::Target* ProjectDocument::findTarget(const std::string& targetId) {
    for (auto& cu : controllers) {
        for (auto& prog : cu.programs) {
            for (auto& op : prog.operations) {
                for (auto& t : op.targets) {
                    if (t.id == targetId) {
                        return &t;
                    }
                }
            }
        }
    }
    return nullptr;
}

const core::Target* ProjectDocument::findTarget(const std::string& targetId) const {
    for (const auto& cu : controllers) {
        for (const auto& prog : cu.programs) {
            for (const auto& op : prog.operations) {
                for (const auto& t : op.targets) {
                    if (t.id == targetId) {
                        return &t;
                    }
                }
            }
        }
    }
    return nullptr;
}

core::Operation* ProjectDocument::findOperation(const std::string& operationId) {
    for (auto& cu : controllers) {
        for (auto& prog : cu.programs) {
            for (auto& op : prog.operations) {
                if (op.id == operationId) {
                    return &op;
                }
            }
        }
    }
    return nullptr;
}

const core::Operation* ProjectDocument::findOperation(const std::string& operationId) const {
    for (const auto& cu : controllers) {
        for (const auto& prog : cu.programs) {
            for (const auto& op : prog.operations) {
                if (op.id == operationId) {
                    return &op;
                }
            }
        }
    }
    return nullptr;
}

core::SceneNode* ProjectDocument::findSceneNode(const std::string& nodeId) {
    for (auto& n : sceneNodes) {
        if (n.id == nodeId) {
            return &n;
        }
    }
    return nullptr;
}

const core::SceneNode* ProjectDocument::findSceneNode(const std::string& nodeId) const {
    for (const auto& n : sceneNodes) {
        if (n.id == nodeId) {
            return &n;
        }
    }
    return nullptr;
}

core::SeamDefinition* ProjectDocument::findSeam(const std::string& seamId) {
    for (auto& s : seams) {
        if (s.id == seamId) {
            return &s;
        }
    }
    return nullptr;
}

const core::SeamDefinition* ProjectDocument::findSeam(const std::string& seamId) const {
    for (const auto& s : seams) {
        if (s.id == seamId) {
            return &s;
        }
    }
    return nullptr;
}

core::WeldScheduleDefinition* ProjectDocument::findWeldSchedule(int scheduleNumber) {
    for (auto& s : weldSchedules) {
        if (s.scheduleNumber == scheduleNumber) {
            return &s;
        }
    }
    return nullptr;
}

const core::WeldScheduleDefinition* ProjectDocument::findWeldSchedule(int scheduleNumber) const {
    for (const auto& s : weldSchedules) {
        if (s.scheduleNumber == scheduleNumber) {
            return &s;
        }
    }
    return nullptr;
}

core::Transform4d ProjectDocument::computeUFrameInWorld(int uframeNumber) const {
    for (const auto& node : sceneNodes) {
        if (node.type == core::SceneNodeType::Frame && node.uframeNumber == uframeNumber) {
            return node.localTransform;
        }
    }
    return core::Transform4d::identity();
}

// ------------------------------------------------------------------------------
// MacroCommand
// ------------------------------------------------------------------------------
MacroCommand::MacroCommand(std::string desc) : desc_(std::move(desc)) {}

void MacroCommand::addSubCommand(std::unique_ptr<ICommand> cmd) {
    if (cmd) {
        subCommands_.push_back(std::move(cmd));
    }
}

void MacroCommand::execute() {
    for (auto& cmd : subCommands_) {
        cmd->execute();
    }
}

void MacroCommand::undo() {
    for (auto it = subCommands_.rbegin(); it != subCommands_.rend(); ++it) {
        (*it)->undo();
    }
}

// ------------------------------------------------------------------------------
// GenerateOperationFromSeamCommand
// ------------------------------------------------------------------------------
GenerateOperationFromSeamCommand::GenerateOperationFromSeamCommand(
    ProjectDocument& doc,
    std::string seamId,
    const core::ToolpathGenParams* params,
    std::string programId)
    : doc_(doc),
      seamId_(std::move(seamId)),
      programId_(std::move(programId)) {
    if (params != nullptr) {
        hasCustomParams_ = true;
        customParams_ = *params;
    }
    // Determine operationId up front so operationId() accessor is valid immediately
    for (const auto& cu : doc_.controllers) {
        for (const auto& prog : cu.programs) {
            for (const auto& op : prog.operations) {
                if (op.seamId == seamId_) {
                    operationId_ = op.id;
                    isNewOperation_ = false;
                    return;
                }
            }
        }
    }
    operationId_ = "op_" + seamId_;
    isNewOperation_ = true;
}

void GenerateOperationFromSeamCommand::execute() {
    core::SeamDefinition* seam = doc_.findSeam(seamId_);
    if (!seam || seam->sampledFrames.empty()) {
        mutated_ = false;
        return;
    }

    if (!initialized_) {
        beforeDirty_ = doc_.isDirty;
        beforeSeamProgrammed_ = seam->programmed;

        const core::Operation* existingOp = nullptr;
        for (const auto& cu : doc_.controllers) {
            for (const auto& prog : cu.programs) {
                for (const auto& op : prog.operations) {
                    if (op.seamId == seamId_) {
                        existingOp = &op;
                        break;
                    }
                }
                if (existingOp) break;
            }
            if (existingOp) break;
        }

        if (existingOp != nullptr) {
            isNewOperation_ = false;
            operationId_ = existingOp->id;
            beforeOp_ = *existingOp;
        } else {
            isNewOperation_ = true;
            operationId_ = "op_" + seamId_;
        }
        initialized_ = true;
    }

    if (isNewOperation_) {
        if (doc_.controllers.empty()) {
            core::ControllerUnit cu;
            cu.id = "cu_1";
            cu.name = "FANUC_R30iB_Plus";
            doc_.controllers.push_back(std::move(cu));
            createdController_ = true;
        }
        if (doc_.controllers[0].programs.empty()) {
            core::Program p;
            p.id = "prog_1";
            p.name = "WELD_001";
            doc_.controllers[0].programs.push_back(std::move(p));
            createdProgram_ = true;
        }

        core::Program* targetProg = nullptr;
        if (!programId_.empty()) {
            for (auto& cu : doc_.controllers) {
                for (auto& p : cu.programs) {
                    if (p.id == programId_) {
                        targetProg = &p;
                        break;
                    }
                }
                if (targetProg) break;
            }
        }
        if (targetProg == nullptr) {
            targetProg = &doc_.controllers[0].programs[0];
        }

        core::Operation op;
        op.id = operationId_;
        op.name = "Op_" + seam->name;
        op.seamId = seam->id;
        op.exemptionPartIds = {seam->face1PartId, seam->face2PartId};
        if (hasCustomParams_) {
            op.genParams = customParams_;
        }

        const core::Transform4d uframeInWorld = doc_.computeUFrameInWorld(targetProg->uframeNumber);
        algo::SeamAndToolpathAlgo::generateOperationTargets(*seam, uframeInWorld, op);

        targetProg->operations.push_back(std::move(op));
    } else {
        core::Operation* op = doc_.findOperation(operationId_);
        if (!op) {
            mutated_ = false;
            return;
        }
        if (hasCustomParams_) {
            op->genParams = customParams_;
        }
        int uframeNum = 1;
        for (const auto& cu : doc_.controllers) {
            for (const auto& p : cu.programs) {
                for (const auto& checkOp : p.operations) {
                    if (checkOp.id == operationId_) {
                        uframeNum = p.uframeNumber;
                        break;
                    }
                }
            }
        }
        const core::Transform4d uframeInWorld = doc_.computeUFrameInWorld(uframeNum);
        algo::SeamAndToolpathAlgo::generateOperationTargets(*seam, uframeInWorld, *op);
    }

    seam->programmed = true;
    doc_.isDirty = true;
    mutated_ = true;
}

void GenerateOperationFromSeamCommand::undo() {
    if (!mutated_) return;

    core::SeamDefinition* seam = doc_.findSeam(seamId_);
    if (seam) {
        seam->programmed = beforeSeamProgrammed_;
    }

    if (isNewOperation_) {
        for (auto& cu : doc_.controllers) {
            for (auto& prog : cu.programs) {
                auto it = std::remove_if(prog.operations.begin(), prog.operations.end(),
                    [&](const core::Operation& o) { return o.id == operationId_; });
                if (it != prog.operations.end()) {
                    prog.operations.erase(it, prog.operations.end());
                }
            }
        }
        if (createdProgram_ && !doc_.controllers.empty() && !doc_.controllers[0].programs.empty()) {
            doc_.controllers[0].programs.pop_back();
        }
        if (createdController_) {
            doc_.controllers.pop_back();
        }
    } else {
        core::Operation* op = doc_.findOperation(operationId_);
        if (op) {
            *op = beforeOp_;
        }
    }

    doc_.isDirty = beforeDirty_;
}

std::string GenerateOperationFromSeamCommand::description() const {
    return (isNewOperation_ ? "Generate Operation from Seam (" : "Regenerate Operation from Seam (") + seamId_ + ")";
}

// ------------------------------------------------------------------------------
// RegenerateOperationTargetsCommand
// ------------------------------------------------------------------------------
RegenerateOperationTargetsCommand::RegenerateOperationTargetsCommand(
    ProjectDocument& doc, std::string operationId)
    : doc_(doc), operationId_(std::move(operationId)) {}

void RegenerateOperationTargetsCommand::execute() {
    core::Operation* op = doc_.findOperation(operationId_);
    if (!op || op->seamId.empty()) {
        mutated_ = false;
        return;
    }
    core::SeamDefinition* seam = doc_.findSeam(op->seamId);
    if (!seam) {
        mutated_ = false;
        return;
    }

    if (!initialized_) {
        seamId_ = op->seamId;
        beforeTargets_ = op->targets;
        beforeSeamProgrammed_ = seam->programmed;
        beforeDirty_ = doc_.isDirty;
        initialized_ = true;
    }

    int uframeNum = 1;
    for (const auto& cu : doc_.controllers) {
        for (const auto& p : cu.programs) {
            for (const auto& checkOp : p.operations) {
                if (checkOp.id == operationId_) {
                    uframeNum = p.uframeNumber;
                    break;
                }
            }
        }
    }
    const core::Transform4d uframeInWorld = doc_.computeUFrameInWorld(uframeNum);
    algo::SeamAndToolpathAlgo::generateOperationTargets(*seam, uframeInWorld, *op);

    seam->programmed = true;
    doc_.isDirty = true;
    mutated_ = true;
}

void RegenerateOperationTargetsCommand::undo() {
    if (!mutated_) return;
    core::Operation* op = doc_.findOperation(operationId_);
    if (op) {
        op->targets = beforeTargets_;
    }
    core::SeamDefinition* seam = doc_.findSeam(seamId_);
    if (seam) {
        seam->programmed = beforeSeamProgrammed_;
    }
    doc_.isDirty = beforeDirty_;
}

// ------------------------------------------------------------------------------
// CommandManager
// ------------------------------------------------------------------------------
CommandManager::CommandManager(size_t maxHistoryDepth)
    : maxDepth_(maxHistoryDepth) {}

bool CommandManager::executeCommand(std::unique_ptr<ICommand> cmd) {
    if (!cmd) return false;
    cmd->execute();
    if (!cmd->didMutate()) {
        return false;
    }

    if (mergeSession_ && !undoStack_.empty() && undoStack_.back()->mergeWith(cmd.get())) {
        redoStack_.clear();
        notifyDocumentChanged();
        return true;
    }

    if (undoStack_.size() >= maxDepth_) {
        undoStack_.erase(undoStack_.begin());
    }
    undoStack_.push_back(std::move(cmd));
    redoStack_.clear();
    notifyDocumentChanged();
    return true;
}

bool CommandManager::undo() {
    if (undoStack_.empty()) return false;
    auto cmd = std::move(undoStack_.back());
    undoStack_.pop_back();
    cmd->undo();
    redoStack_.push_back(std::move(cmd));
    notifyDocumentChanged();
    return true;
}

bool CommandManager::redo() {
    if (redoStack_.empty()) return false;
    auto cmd = std::move(redoStack_.back());
    redoStack_.pop_back();
    cmd->execute();
    undoStack_.push_back(std::move(cmd));
    notifyDocumentChanged();
    return true;
}

void CommandManager::clear() {
    undoStack_.clear();
    redoStack_.clear();
}

int CommandManager::addDocumentChangedListener(std::function<void()> listener) {
    const int id = nextListenerId_++;
    listeners_.push_back(DocumentListener{id, std::move(listener)});
    return id;
}

void CommandManager::removeDocumentChangedListener(int listenerId) {
    listeners_.erase(
        std::remove_if(listeners_.begin(), listeners_.end(), [&](const DocumentListener& listener) {
            return listener.id == listenerId;
        }),
        listeners_.end());
}

void CommandManager::notifyDocumentChanged() {
    if (onChanged_) {
        onChanged_();
    }
    for (const auto& listener : listeners_) {
        if (listener.fn) {
            listener.fn();
        }
    }
}

} // namespace olp::app
