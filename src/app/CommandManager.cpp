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
        std::swap(a.processModified, b.processModified);
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
// SmoothTargetOrientationsCommand
// ------------------------------------------------------------------------------
SmoothTargetOrientationsCommand::SmoothTargetOrientationsCommand(
    ProjectDocument& doc,
    std::string operationId,
    std::string startTargetId,
    std::string endTargetId)
    : doc_(doc),
      operationId_(std::move(operationId)),
      startTargetId_(std::move(startTargetId)),
      endTargetId_(std::move(endTargetId)) {}

void SmoothTargetOrientationsCommand::execute() {
    core::Operation* op = doc_.findOperation(operationId_);
    if (!op) {
        mutated_ = false;
        return;
    }

    if (!initialized_) {
        beforeTargets_ = op->targets;
        beforeDirty_ = doc_.isDirty;
        initialized_ = true;
    }

    if (!afterTargets_.empty()) {
        op->targets = afterTargets_;
        doc_.isDirty = true;
        mutated_ = true;
        return;
    }

    const core::SeamDefinition* seam = doc_.findSeam(op->seamId);
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

    const bool ok = algo::SeamAndToolpathAlgo::interpolateOrientationsSlerp(
        *op, startTargetId_, endTargetId_, seam, uframeInWorld);
    if (!ok) {
        mutated_ = false;
        return;
    }

    doc_.isDirty = true;
    mutated_ = true;
}

void SmoothTargetOrientationsCommand::undo() {
    if (!mutated_) return;
    core::Operation* op = doc_.findOperation(operationId_);
    if (op) {
        if (afterTargets_.empty()) {
            afterTargets_ = op->targets;
        }
        op->targets = beforeTargets_;
    }
    doc_.isDirty = beforeDirty_;
}

std::string SmoothTargetOrientationsCommand::description() const {
    return "Smooth Target Orientations (" + operationId_ + ")";
}

// ------------------------------------------------------------------------------
// ModifyTargetProcessParamsCommand
// ------------------------------------------------------------------------------
ModifyTargetProcessParamsCommand::ModifyTargetProcessParamsCommand(
    ProjectDocument& doc,
    std::string targetId,
    const TargetProcessParams& params)
    : doc_(doc), targetIds_({std::move(targetId)}), newParams_(params) {}

ModifyTargetProcessParamsCommand::ModifyTargetProcessParamsCommand(
    ProjectDocument& doc,
    std::vector<std::string> targetIds,
    const TargetProcessParams& params)
    : doc_(doc), targetIds_(std::move(targetIds)), newParams_(params) {}

ModifyTargetProcessParamsCommand::ModifyTargetProcessParamsCommand(
    ProjectDocument& doc,
    std::string targetId,
    core::MotionType motion,
    double speedValue,
    bool useWeldSpeed,
    core::TerminationType termination,
    int cntValue,
    std::vector<core::ProcessEvent> events)
    : doc_(doc), targetIds_({std::move(targetId)}) {
    newParams_.motion = motion;
    newParams_.speedValue = speedValue;
    newParams_.useWeldSpeed = useWeldSpeed;
    newParams_.termination = termination;
    newParams_.cntValue = cntValue;
    newParams_.events = std::move(events);
}

void ModifyTargetProcessParamsCommand::execute() {
    if (!initialized_) {
        beforeDirty_ = doc_.isDirty;
        beforeSnapshots_.clear();

        for (const auto& tid : targetIds_) {
            auto* t = doc_.findTarget(tid);
            if (!t) continue;

            bool changed = false;
            if (newParams_.motion.has_value() && *newParams_.motion != t->motion) changed = true;
            if (newParams_.speedValue.has_value() && std::abs(*newParams_.speedValue - t->speedValue) > 1e-4) changed = true;
            if (newParams_.useWeldSpeed.has_value() && *newParams_.useWeldSpeed != t->useWeldSpeed) changed = true;
            if (newParams_.termination.has_value() && *newParams_.termination != t->termination) changed = true;
            if (newParams_.cntValue.has_value() && *newParams_.cntValue != t->cntValue) changed = true;
            if (newParams_.events.has_value() && *newParams_.events != t->events) changed = true;

            if (changed) {
                TargetParamsSnapshot snap;
                snap.targetId = t->id;
                snap.motion = t->motion;
                snap.speedValue = t->speedValue;
                snap.useWeldSpeed = t->useWeldSpeed;
                snap.termination = t->termination;
                snap.cntValue = t->cntValue;
                snap.events = t->events;
                snap.processModified = t->processModified;
                snap.cache = t->cache;
                beforeSnapshots_.push_back(std::move(snap));
            }
        }

        if (beforeSnapshots_.empty()) {
            mutated_ = false;
            initialized_ = true;
            return;
        }
        initialized_ = true;
    }

    if (beforeSnapshots_.empty()) {
        mutated_ = false;
        return;
    }

    if (!afterSnapshots_.empty()) {
        for (const auto& snap : afterSnapshots_) {
            auto* t = doc_.findTarget(snap.targetId);
            if (!t) continue;
            t->motion = snap.motion;
            t->speedValue = snap.speedValue;
            t->useWeldSpeed = snap.useWeldSpeed;
            t->termination = snap.termination;
            t->cntValue = snap.cntValue;
            t->events = snap.events;
            t->processModified = snap.processModified;
            t->cache = snap.cache;
        }
        doc_.isDirty = true;
        mutated_ = true;
        return;
    }

    for (const auto& snap : beforeSnapshots_) {
        auto* t = doc_.findTarget(snap.targetId);
        if (!t) continue;
        if (newParams_.motion.has_value()) t->motion = *newParams_.motion;
        if (newParams_.speedValue.has_value()) t->speedValue = *newParams_.speedValue;
        if (newParams_.useWeldSpeed.has_value()) t->useWeldSpeed = *newParams_.useWeldSpeed;
        if (newParams_.termination.has_value()) t->termination = *newParams_.termination;
        if (newParams_.cntValue.has_value()) t->cntValue = *newParams_.cntValue;
        if (newParams_.events.has_value()) t->events = *newParams_.events;
        t->processModified = true;
        t->cache.valid = false;
    }

    doc_.isDirty = true;
    mutated_ = true;
}

void ModifyTargetProcessParamsCommand::undo() {
    if (!mutated_) return;
    if (afterSnapshots_.empty()) {
        for (const auto& snap : beforeSnapshots_) {
            auto* t = doc_.findTarget(snap.targetId);
            if (!t) continue;
            TargetParamsSnapshot afterSnap;
            afterSnap.targetId = t->id;
            afterSnap.motion = t->motion;
            afterSnap.speedValue = t->speedValue;
            afterSnap.useWeldSpeed = t->useWeldSpeed;
            afterSnap.termination = t->termination;
            afterSnap.cntValue = t->cntValue;
            afterSnap.events = t->events;
            afterSnap.processModified = t->processModified;
            afterSnap.cache = t->cache;
            afterSnapshots_.push_back(std::move(afterSnap));
        }
    }
    for (const auto& snap : beforeSnapshots_) {
        auto* t = doc_.findTarget(snap.targetId);
        if (!t) continue;
        t->motion = snap.motion;
        t->speedValue = snap.speedValue;
        t->useWeldSpeed = snap.useWeldSpeed;
        t->termination = snap.termination;
        t->cntValue = snap.cntValue;
        t->events = snap.events;
        t->processModified = snap.processModified;
        t->cache = snap.cache;
    }
    doc_.isDirty = beforeDirty_;
}

std::string ModifyTargetProcessParamsCommand::description() const {
    if (targetIds_.size() == 1) {
        return "Modify Process Parameters (" + targetIds_.front() + ")";
    }
    return "Modify Process Parameters (" + std::to_string(targetIds_.size()) + " targets)";
}

// ------------------------------------------------------------------------------
// RotateTargetAroundTcpZCommand
// ------------------------------------------------------------------------------
RotateTargetAroundTcpZCommand::RotateTargetAroundTcpZCommand(
    ProjectDocument& doc,
    std::string targetId,
    double angleRad,
    bool allowMerge)
    : doc_(doc),
      targetIds_({std::move(targetId)}),
      angleRad_(angleRad),
      allowMerge_(allowMerge) {}

RotateTargetAroundTcpZCommand::RotateTargetAroundTcpZCommand(
    ProjectDocument& doc,
    std::vector<std::string> targetIds,
    double angleRad,
    bool allowMerge)
    : doc_(doc),
      targetIds_(std::move(targetIds)),
      angleRad_(angleRad),
      allowMerge_(allowMerge) {}

void RotateTargetAroundTcpZCommand::execute() {
    if (std::abs(angleRad_) < 1e-6 || targetIds_.empty()) {
        mutated_ = false;
        return;
    }

    if (!initialized_) {
        beforeDirty_ = doc_.isDirty;
        beforeSnapshots_.clear();

        for (const auto& tid : targetIds_) {
            auto* t = doc_.findTarget(tid);
            if (!t) continue;
            TargetPoseSnapshot snap;
            snap.targetId = t->id;
            snap.poseInUFrame = t->poseInUFrame;
            snap.userDeltaOffset = t->userDeltaOffset;
            snap.userModified = t->userModified;
            snap.cache = t->cache;
            beforeSnapshots_.push_back(std::move(snap));
        }

        if (beforeSnapshots_.empty()) {
            mutated_ = false;
            initialized_ = true;
            return;
        }
        initialized_ = true;
    }

    if (!afterSnapshots_.empty()) {
        for (const auto& snap : afterSnapshots_) {
            auto* t = doc_.findTarget(snap.targetId);
            if (!t) continue;
            t->poseInUFrame = snap.poseInUFrame;
            t->userDeltaOffset = snap.userDeltaOffset;
            t->userModified = snap.userModified;
            t->cache = snap.cache;
        }
        doc_.isDirty = true;
        mutated_ = true;
        return;
    }

    bool anyRotated = false;
    for (const auto& snap : beforeSnapshots_) {
        auto* t = doc_.findTarget(snap.targetId);
        if (!t) continue;
        if (algo::SeamAndToolpathAlgo::rotateTargetAroundTcpZ(*t, angleRad_)) {
            anyRotated = true;
        }
    }

    if (!anyRotated) {
        mutated_ = false;
        return;
    }

    doc_.isDirty = true;
    mutated_ = true;
}

void RotateTargetAroundTcpZCommand::undo() {
    if (!mutated_) return;
    if (afterSnapshots_.empty()) {
        for (const auto& snap : beforeSnapshots_) {
            auto* t = doc_.findTarget(snap.targetId);
            if (!t) continue;
            TargetPoseSnapshot afterSnap;
            afterSnap.targetId = t->id;
            afterSnap.poseInUFrame = t->poseInUFrame;
            afterSnap.userDeltaOffset = t->userDeltaOffset;
            afterSnap.userModified = t->userModified;
            afterSnap.cache = t->cache;
            afterSnapshots_.push_back(std::move(afterSnap));
        }
    }
    for (const auto& snap : beforeSnapshots_) {
        auto* t = doc_.findTarget(snap.targetId);
        if (!t) continue;
        t->poseInUFrame = snap.poseInUFrame;
        t->userDeltaOffset = snap.userDeltaOffset;
        t->userModified = snap.userModified;
        t->cache = snap.cache;
    }
    doc_.isDirty = beforeDirty_;
}

bool RotateTargetAroundTcpZCommand::mergeWith(const ICommand* other) {
    if (!allowMerge_ || other->commandTypeId() != commandTypeId()) {
        return false;
    }
    const auto* rhs = static_cast<const RotateTargetAroundTcpZCommand*>(other);
    if (!rhs->allowMerge_ || rhs->targetIds_ != targetIds_) {
        return false;
    }
    for (const auto& snap : beforeSnapshots_) {
        auto* t = doc_.findTarget(snap.targetId);
        if (!t) continue;
        algo::SeamAndToolpathAlgo::rotateTargetAroundTcpZ(*t, rhs->angleRad_);
    }
    angleRad_ += rhs->angleRad_;
    afterSnapshots_.clear();
    return true;
}

std::string RotateTargetAroundTcpZCommand::description() const {
    if (targetIds_.size() == 1) {
        return "Rotate Target Around TCP Z (" + targetIds_.front() + ")";
    }
    return "Rotate Targets Around TCP Z (" + std::to_string(targetIds_.size()) + " targets)";
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
