#include "olp/ui/TargetPropertyPanel.h"

#include "olp/ui/TwistScanWidget.h"
#include "ShellLabels.h"

#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QVBoxLayout>

#include <cmath>
#include <sstream>

namespace olp::ui {
namespace {

const core::Target* findTargetById(const app::ProjectDocument& doc, const std::string& id) {
    for (const auto& cu : doc.controllers) {
        for (const auto& program : cu.programs) {
            for (const auto& op : program.operations) {
                for (const auto& target : op.targets) {
                    if (target.id == id) {
                        return &target;
                    }
                }
            }
        }
    }
    return nullptr;
}


} // namespace

TargetPropertyPanel::TargetPropertyPanel(MainWindow& controller,
                                         std::function<std::vector<std::string>()> selectedTargetIds,
                                         std::function<void()> refresh,
                                         QWidget* parent)
    : QWidget(parent),
      controller_(controller),
      selectedTargetIds_(std::move(selectedTargetIds)),
      refresh_(std::move(refresh)) {
    propertyText_ = new QPlainTextEdit();
    propertyText_->setReadOnly(true);

    auto* propLayout = new QVBoxLayout(this);
    propLayout->setContentsMargins(4, 4, 4, 4);
    propLayout->setSpacing(6);
    propLayout->addWidget(propertyText_, 1);

    spinTuningWidget_ = new QGroupBox(QString::fromUtf8("焊枪自转角微调 (TCP Z 自转)"));
    auto* spinLayout = new QVBoxLayout(spinTuningWidget_);
    spinLayout->setContentsMargins(6, 6, 6, 6);
    spinLayout->setSpacing(4);

    twistScanWidget_ = new TwistScanWidget(this);
    spinLayout->addWidget(twistScanWidget_);

    connect(twistScanWidget_, &TwistScanWidget::angleSelected, this, [this](double clickedAngleDeg) {
        const auto selectedIds = selectedTargetIds_();
        std::vector<std::string> ids = selectedIds;
        if (ids.empty() && !controller_.selectedTargetId().empty()) {
            ids.push_back(controller_.selectedTargetId());
        }
        if (ids.empty()) return;
        double currentZAngleDeg = 0.0;
        const auto* target = controller_.document().findTarget(ids.front());
        if (target != nullptr) {
            currentZAngleDeg = core::rad2deg(std::atan2(target->userDeltaOffset.rot.m[1][0], target->userDeltaOffset.rot.m[0][0]));
        }
        const double deltaDeg = clickedAngleDeg - currentZAngleDeg;
        if (std::abs(deltaDeg) > 1e-4) {
            if (controller_.rotateTargetsTcpZ(ids, core::deg2rad(deltaDeg))) {
                showStatus(QString::fromUtf8("已将目标点绕 TCP Z 轴自转至 %1°").arg(clickedAngleDeg, 0, 'f', 1), 3000);
                refresh_();
            }
        }
    });

    auto* optBtn = new QPushButton(QString::fromUtf8("★ 一键微调至最优自转角"));
    optBtn->setToolTip(QString::fromUtf8("自动在 72 组采样中定位远离碰撞与奇异的最大连续可行域中心角"));
    connect(optBtn, &QPushButton::clicked, this, [this] {
        if (!twistScanWidget_) return;
        const auto optAngle = twistScanWidget_->findOptimalFeasibleAngleDeg();
        if (!optAngle.has_value()) {
            showStatus(QString::fromUtf8("未找到完全无碰撞且远离奇异的自转角。"), 3000);
            return;
        }
        const auto selectedIds = selectedTargetIds_();
        std::vector<std::string> ids = selectedIds;
        if (ids.empty() && !controller_.selectedTargetId().empty()) {
            ids.push_back(controller_.selectedTargetId());
        }
        if (ids.empty()) return;

        double currentZAngleDeg = 0.0;
        const auto* target = controller_.document().findTarget(ids.front());
        if (target != nullptr) {
            currentZAngleDeg = core::rad2deg(std::atan2(target->userDeltaOffset.rot.m[1][0], target->userDeltaOffset.rot.m[0][0]));
        }
        const double deltaDeg = *optAngle - currentZAngleDeg;
        if (std::abs(deltaDeg) > 1e-4) {
            if (controller_.rotateTargetsTcpZ(ids, core::deg2rad(deltaDeg))) {
                showStatus(QString::fromUtf8("已自动微调至最优自转角 %1° (增量 %2°)").arg(*optAngle, 0, 'f', 1).arg(deltaDeg, 0, 'f', 1), 3000);
                refresh_();
            }
        } else {
            showStatus(QString::fromUtf8("当前已处于最优自转角 %1°。").arg(*optAngle, 0, 'f', 1), 3000);
        }
    });
    spinLayout->addWidget(optBtn);

    // Row 1: Fast step buttons
    auto* stepBtnLayout = new QHBoxLayout();
    stepBtnLayout->setSpacing(4);
    const std::vector<std::pair<QString, double>> stepAngles = {
        {"-15°", -15.0},
        {"-5°", -5.0},
        {"-1°", -1.0},
        {"+1°", +1.0},
        {"+5°", +5.0},
        {"+15°", +15.0}
    };
    for (const auto& [lbl, deg] : stepAngles) {
        auto* btn = new QPushButton(lbl);
        btn->setToolTip(QString::fromUtf8("绕焊枪 TCP 本地 Z 轴自转 %1").arg(lbl));
        connect(btn, &QPushButton::clicked, this, [this, deg] {
            const auto selectedIds = selectedTargetIds_();
            std::vector<std::string> ids = selectedIds;
            if (ids.empty() && !controller_.selectedTargetId().empty()) {
                ids.push_back(controller_.selectedTargetId());
            }
            if (ids.empty()) return;
            if (controller_.rotateTargetsTcpZ(ids, core::deg2rad(deg))) {
                showStatus(QString::fromUtf8("已将目标点绕 TCP Z 轴自转 %1°").arg(deg, 0, 'f', 1), 3000);
                refresh_();
            }
        });
        stepBtnLayout->addWidget(btn);
    }
    spinLayout->addLayout(stepBtnLayout);

    // Row 2: Custom spinbox and step rotation
    auto* customLayout = new QHBoxLayout();
    auto* angleSpin = new QDoubleSpinBox();
    angleSpin->setRange(0.1, 180.0);
    angleSpin->setValue(10.0);
    angleSpin->setSuffix("°");
    angleSpin->setSingleStep(1.0);

    auto* ccwBtn = new QPushButton(QString::fromUtf8("逆时针 ⟲"));
    connect(ccwBtn, &QPushButton::clicked, this, [this, angleSpin] {
        const auto selectedIds = selectedTargetIds_();
        std::vector<std::string> ids = selectedIds;
        if (ids.empty() && !controller_.selectedTargetId().empty()) {
            ids.push_back(controller_.selectedTargetId());
        }
        if (ids.empty()) return;
        const double deg = -angleSpin->value();
        if (controller_.rotateTargetsTcpZ(ids, core::deg2rad(deg))) {
            showStatus(QString::fromUtf8("已将目标点绕 TCP Z 轴自转 %1°").arg(deg, 0, 'f', 1), 3000);
            refresh_();
        }
    });

    auto* cwBtn = new QPushButton(QString::fromUtf8("顺时针 ⟳"));
    connect(cwBtn, &QPushButton::clicked, this, [this, angleSpin] {
        const auto selectedIds = selectedTargetIds_();
        std::vector<std::string> ids = selectedIds;
        if (ids.empty() && !controller_.selectedTargetId().empty()) {
            ids.push_back(controller_.selectedTargetId());
        }
        if (ids.empty()) return;
        const double deg = angleSpin->value();
        if (controller_.rotateTargetsTcpZ(ids, core::deg2rad(deg))) {
            showStatus(QString::fromUtf8("已将目标点绕 TCP Z 轴自转 +%1°").arg(deg, 0, 'f', 1), 3000);
            refresh_();
        }
    });

    customLayout->addWidget(angleSpin);
    customLayout->addWidget(ccwBtn);
    customLayout->addWidget(cwBtn);
    spinLayout->addLayout(customLayout);

    propLayout->addWidget(spinTuningWidget_);
    spinTuningWidget_->setVisible(false);

}

void TargetPropertyPanel::showSceneNode(const QString& nodeId) {
    if (spinTuningWidget_) {
        spinTuningWidget_->setVisible(false);
    }
    if (nodeId.isEmpty()) {
        return;
    }
    const auto* node = controller_.document().findSceneNode(nodeId.toStdString());
    if (node == nullptr) {
        return;
    }
    const auto wpr = node->localTransform.toWPR();
    std::ostringstream text;
    text.setf(std::ios::fixed);
    text.precision(1);
    text << node->name << "\n"
         << "类型  " << typeText(node->type).toStdString() << "\n"
         << "碰撞角色  " << roleText(node->collisionRole).toStdString() << "\n"
         << "可见  " << (node->visible ? "是" : "否") << "\n"
         << "位置 mm  " << wpr.x << "  " << wpr.y << "  " << wpr.z << "\n"
         << "姿态 WPR °  " << wpr.wDeg << "  " << wpr.pDeg << "  " << wpr.rDeg << "\n";
    propertyText_->setPlainText(QString::fromStdString(text.str()));
}

void TargetPropertyPanel::showTarget(const QString& targetId) {
    if (spinTuningWidget_) {
        spinTuningWidget_->setVisible(true);
    }
    const auto* target = findTargetById(controller_.document(), targetId.toStdString());
    if (target == nullptr) {
        return;
    }
    const auto wpr = target->poseInUFrame.toWPR();
    std::ostringstream text;
    text.setf(std::ios::fixed);
    text.precision(1);
    text << target->name << "\n"
         << "运动  " << motionText(target->motion).toStdString() << "\n"
         << "速度  " << (target->useWeldSpeed ? "WELD_SPEED (" + std::to_string(static_cast<int>(target->speedValue)) + " mm/s)" : std::to_string(static_cast<int>(target->speedValue))) << "\n"
         << "平滑度  " << (target->termination == core::TerminationType::FINE ? "FINE" : "CNT " + std::to_string(target->cntValue)) << "\n";
    if (!target->events.empty()) {
        for (const auto& ev : target->events) {
            if (ev.type == core::ProcessEventType::ArcStart) {
                text << "工艺事件  Arc Start[" << ev.scheduleNumber << "]\n";
            } else if (ev.type == core::ProcessEventType::ArcEnd) {
                text << "工艺事件  Arc End[" << ev.scheduleNumber << "]\n";
            }
        }
    }
    text << "状态  " << statusText(target->cache.status).toStdString() << "\n"
         << "间隙 mm  " << target->cache.minClearanceMm << "\n";
    if (target->cache.valid) {
        text << "构型  " << target->cache.solvedConfig.toString() << "\n";
        text << "可操作度  " << target->cache.manipulabilityIndex << "\n";
        text << "关节角 °  J1:" << core::rad2deg(target->cache.jointsRad[0])
             << " J2:" << core::rad2deg(target->cache.jointsRad[1])
             << " J3:" << core::rad2deg(target->cache.jointsRad[2])
             << " J4:" << core::rad2deg(target->cache.jointsRad[3])
             << " J5:" << core::rad2deg(target->cache.jointsRad[4])
             << " J6:" << core::rad2deg(target->cache.jointsRad[5]) << "\n";
    }
    text << "UFRAME 位置 mm  " << wpr.x << "  " << wpr.y << "  " << wpr.z << "\n"
         << "UFRAME 姿态 WPR °  " << wpr.wDeg << "  " << wpr.pDeg << "  " << wpr.rDeg << "\n";
    if (!target->cache.issueMessage.empty()) {
        text << "说明  " << target->cache.issueMessage << "\n";
    }
    propertyText_->setPlainText(QString::fromStdString(text.str()));

    if (twistScanWidget_ != nullptr) {
        const double curZDeg = core::rad2deg(std::atan2(target->userDeltaOffset.rot.m[1][0], target->userDeltaOffset.rot.m[0][0]));
        const auto samples = controller_.scanSelectedTargetTwistAngles(5.0);
        twistScanWidget_->setSamples(samples, curZDeg);
    }
}

void TargetPropertyPanel::showOperationSummary(const std::string& operationName, int targetCount) {
    propertyText_->setPlainText(QString::fromUtf8("操作  ")
        + QString::fromStdString(operationName)
        + QString::fromUtf8("\n路径点  ")
        + QString::number(targetCount));
}

void TargetPropertyPanel::showStatus(const QString& message, int timeoutMs) {
    if (auto* mainWindow = qobject_cast<QMainWindow*>(window())) {
        mainWindow->statusBar()->showMessage(message, timeoutMs);
    }
}

} // namespace olp::ui
