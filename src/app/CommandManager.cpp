#include "olp/app/CommandManager.h"
#include <algorithm>

namespace olp::app {

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

core::SceneNode* ProjectDocument::findSceneNode(const std::string& nodeId) {
    for (auto& n : sceneNodes) {
        if (n.id == nodeId) {
            return &n;
        }
    }
    return nullptr;
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
// ModifyTargetPoseCommand
// ------------------------------------------------------------------------------
ModifyTargetPoseCommand::ModifyTargetPoseCommand(
    ProjectDocument& doc,
    std::string targetId,
    const core::Transform4d& newPoseInUFrame,
    bool allowMerge)
    : doc_(doc),
      targetId_(std::move(targetId)),
      newPose_(newPoseInUFrame),
      allowMerge_(allowMerge) {}

void ModifyTargetPoseCommand::execute() {
    core::Target* t = doc_.findTarget(targetId_);
    if (!t) return;

    if (!initialized_) {
        oldPose_ = t->poseInUFrame;
        oldDelta_ = t->userDeltaOffset;
        oldUserModified_ = t->userModified;
        initialized_ = true;
    }

    const core::Transform4d stepDelta = oldPose_.inverse() * newPose_;
    t->poseInUFrame = newPose_;
    t->userModified = true;
    t->userDeltaOffset = oldDelta_ * stepDelta;
    t->cache.valid = false;
    doc_.isDirty = true;
}

void ModifyTargetPoseCommand::undo() {
    core::Target* t = doc_.findTarget(targetId_);
    if (!t) return;

    t->poseInUFrame = oldPose_;
    t->userDeltaOffset = oldDelta_;
    t->userModified = oldUserModified_;
    t->cache.valid = false;
    doc_.isDirty = true;
}

bool ModifyTargetPoseCommand::mergeWith(const ICommand* other) {
    if (!allowMerge_ || other->commandTypeId() != commandTypeId()) {
        return false;
    }
    const auto* rhs = static_cast<const ModifyTargetPoseCommand*>(other);
    if (!rhs->allowMerge_ || rhs->targetId_ != targetId_) {
        return false;
    }
    newPose_ = rhs->newPose_;
    return true;
}

// ------------------------------------------------------------------------------
// ReverseOperationPathCommand
// ------------------------------------------------------------------------------
ReverseOperationPathCommand::ReverseOperationPathCommand(
    ProjectDocument& doc,
    std::string operationId)
    : doc_(doc), operationId_(std::move(operationId)) {}

void ReverseOperationPathCommand::execute() { toggleReverse(); }
void ReverseOperationPathCommand::undo() { toggleReverse(); }

void ReverseOperationPathCommand::toggleReverse() {
    core::Operation* op = doc_.findOperation(operationId_);
    if (!op || op->targets.size() < 2) return;

    std::reverse(op->targets.begin(), op->targets.end());

    // Swap APPROACH <-> RETRACT and WELD_START (ArcStart) <-> WELD_END (ArcEnd)
    for (auto& t : op->targets) {
        if (t.role == core::TargetRole::APPROACH) {
            t.role = core::TargetRole::RETRACT;
            t.name = "RETRACT";
        } else if (t.role == core::TargetRole::RETRACT) {
            t.role = core::TargetRole::APPROACH;
            t.name = "APPROACH";
        } else if (t.role == core::TargetRole::WELD_START) {
            t.role = core::TargetRole::WELD_END;
            t.name = "WELD_END";
            for (auto& ev : t.events) {
                if (ev.type == core::ProcessEventType::ArcStart) {
                    ev.type = core::ProcessEventType::ArcEnd;
                }
            }
        } else if (t.role == core::TargetRole::WELD_END) {
            t.role = core::TargetRole::WELD_START;
            t.name = "WELD_START";
            for (auto& ev : t.events) {
                if (ev.type == core::ProcessEventType::ArcEnd) {
                    ev.type = core::ProcessEventType::ArcStart;
                }
            }
        }
        t.arcLengthRatio = 1.0 - t.arcLengthRatio;
    }
    doc_.isDirty = true;
}

// ------------------------------------------------------------------------------
// CommandManager
// ------------------------------------------------------------------------------
CommandManager::CommandManager(size_t maxHistoryDepth)
    : maxDepth_(maxHistoryDepth) {}

void CommandManager::executeCommand(std::unique_ptr<ICommand> cmd) {
    if (!cmd) return;
    cmd->execute();

    if (!undoStack_.empty() && undoStack_.back()->mergeWith(cmd.get())) {
        redoStack_.clear();
        if (onChanged_) onChanged_();
        return;
    }

    if (undoStack_.size() >= maxDepth_) {
        undoStack_.erase(undoStack_.begin());
    }
    undoStack_.push_back(std::move(cmd));
    redoStack_.clear();

    if (onChanged_) onChanged_();
}

bool CommandManager::undo() {
    if (undoStack_.empty()) return false;
    auto cmd = std::move(undoStack_.back());
    undoStack_.pop_back();
    cmd->undo();
    redoStack_.push_back(std::move(cmd));
    if (onChanged_) onChanged_();
    return true;
}

bool CommandManager::redo() {
    if (redoStack_.empty()) return false;
    auto cmd = std::move(redoStack_.back());
    redoStack_.pop_back();
    cmd->execute();
    undoStack_.push_back(std::move(cmd));
    if (onChanged_) onChanged_();
    return true;
}

void CommandManager::clear() {
    undoStack_.clear();
    redoStack_.clear();
}

} // namespace olp::app
