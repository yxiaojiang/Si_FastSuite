#include "olp/app/CommandManager.h"
#include "olp/algo/SeamAndToolpathAlgo.h"
#include <algorithm>
#include <cmath>

namespace olp::app {

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

} // namespace olp::app
