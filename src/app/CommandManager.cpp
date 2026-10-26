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
    if (!t) {
        mutated_ = false;
        return;
    }

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
    mutated_ = true;
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

void ReverseOperationPathCommand::execute() {
    if (!snapshotTaken_) {
        core::Operation* op = doc_.findOperation(operationId_);
        if (!op || op->targets.size() < 2) {
            mutated_ = false;
            snapshotTaken_ = true;
            return;
        }
        before_ = op->targets;
        mutated_ = true;
        snapshotTaken_ = true;
        applyReversed(op->targets);
        after_ = op->targets;
        doc_.isDirty = true;
        return;
    }
    if (!mutated_) return;
    if (core::Operation* op = doc_.findOperation(operationId_)) {
        op->targets = after_;
        doc_.isDirty = true;
    }
}

void ReverseOperationPathCommand::undo() {
    if (!mutated_) return;
    if (core::Operation* op = doc_.findOperation(operationId_)) {
        op->targets = before_;
        doc_.isDirty = true;
    }
}

void ReverseOperationPathCommand::applyReversed(std::vector<core::Target>& targets) {
    std::reverse(targets.begin(), targets.end());

    auto swapMotion = [](core::Target& a, core::Target& b) {
        std::swap(a.motion, b.motion);
        std::swap(a.speedValue, b.speedValue);
        std::swap(a.useWeldSpeed, b.useWeldSpeed);
        std::swap(a.termination, b.termination);
        std::swap(a.cntValue, b.cntValue);
    };

    core::Target* approach = nullptr;
    core::Target* retract = nullptr;
    core::Target* weldStart = nullptr;
    core::Target* weldEnd = nullptr;
    for (auto& t : targets) {
        if (t.role == core::TargetRole::APPROACH) approach = &t;
        else if (t.role == core::TargetRole::RETRACT) retract = &t;
        else if (t.role == core::TargetRole::WELD_START) weldStart = &t;
        else if (t.role == core::TargetRole::WELD_END) weldEnd = &t;
    }
    // After the geometric reverse, the old retract point is now first but still
    // carries retract motion. Swap motion with the point that now has the other role
    // so each role keeps the speeds the process table assigns to it.
    if (approach && retract) swapMotion(*approach, *retract);
    if (weldStart && weldEnd) swapMotion(*weldStart, *weldEnd);

    for (auto& t : targets) {
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
        // Travel direction flipped: yaw the stored pose 180° about tool Z so a
        // push angle does not silently become a drag angle of the opposite sign.
        t.poseInUFrame.rot = t.poseInUFrame.rot * core::Mat3::rotZ(core::kPi);
    }
}

// ------------------------------------------------------------------------------
// CommandManager
// ------------------------------------------------------------------------------
CommandManager::CommandManager(size_t maxHistoryDepth)
    : maxDepth_(maxHistoryDepth) {}

void CommandManager::executeCommand(std::unique_ptr<ICommand> cmd) {
    if (!cmd) return;
    cmd->execute();
    if (!cmd->didMutate()) {
        return;
    }

    if (mergeSession_ && !undoStack_.empty() && undoStack_.back()->mergeWith(cmd.get())) {
        redoStack_.clear();
        notifyDocumentChanged();
        return;
    }

    if (undoStack_.size() >= maxDepth_) {
        undoStack_.erase(undoStack_.begin());
    }
    undoStack_.push_back(std::move(cmd));
    redoStack_.clear();
    notifyDocumentChanged();
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
