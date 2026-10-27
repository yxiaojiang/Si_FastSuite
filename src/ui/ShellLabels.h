#pragma once

#include "olp/core/ProjectModel.h"

#include <QString>

namespace olp::ui {

inline QString roleText(core::CollisionRole role) {
    switch (role) {
    case core::CollisionRole::Workpiece: return QString::fromUtf8("工件");
    case core::CollisionRole::Fixture: return QString::fromUtf8("夹具");
    case core::CollisionRole::Environment: return QString::fromUtf8("环境");
    case core::CollisionRole::RobotLink: return QString::fromUtf8("机器人连杆");
    case core::CollisionRole::WeldingTorch: return QString::fromUtf8("焊枪");
    case core::CollisionRole::Ignored: return QString::fromUtf8("忽略");
    }
    return QString::fromUtf8("其他");
}


inline QString typeText(core::SceneNodeType type) {
    switch (type) {
    case core::SceneNodeType::Group: return QString::fromUtf8("组");
    case core::SceneNodeType::Part: return QString::fromUtf8("零件");
    case core::SceneNodeType::Robot: return QString::fromUtf8("机器人");
    case core::SceneNodeType::Tool: return QString::fromUtf8("工具");
    case core::SceneNodeType::Frame: return QString::fromUtf8("坐标系");
    }
    return QString::fromUtf8("对象");
}


inline QString motionText(core::MotionType motion) {
    switch (motion) {
    case core::MotionType::J: return "J";
    case core::MotionType::L: return "L";
    case core::MotionType::C: return "C";
    }
    return "?";
}


inline QString statusText(core::TargetValidationState state) {
    switch (state) {
    case core::TargetValidationState::Unchecked: return QString::fromUtf8("未检查");
    case core::TargetValidationState::Valid: return QString::fromUtf8("通过");
    case core::TargetValidationState::Unreachable: return QString::fromUtf8("不可达");
    case core::TargetValidationState::JointLimitExceeded: return QString::fromUtf8("超限位");
    case core::TargetValidationState::NearJointLimit: return QString::fromUtf8("靠近限位");
    case core::TargetValidationState::Singularity: return QString::fromUtf8("奇异");
    case core::TargetValidationState::ConfigJump: return QString::fromUtf8("构型跳变");
    case core::TargetValidationState::Collision: return QString::fromUtf8("碰撞");
    case core::TargetValidationState::NearCollision: return QString::fromUtf8("间隙不足");
    }
    return QString::fromUtf8("未知");
}


} // namespace olp::ui
