#include "olp/ui/TimelinePanel.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTimer>

#include <cmath>

namespace olp::ui {
namespace {

QPushButton* makeButton(const QString& text, QWidget* parent) {
    auto* button = new QPushButton(text, parent);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

} // namespace

TimelinePanel::TimelinePanel(MainWindow& controller, QWidget* parent)
    : QWidget(parent), controller_(controller) {
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(8, 2, 8, 2);
    row->setSpacing(4);

    auto* playButton = makeButton(QString::fromUtf8("播放"), this);
    auto* pauseButton = makeButton(QString::fromUtf8("暂停"), this);
    auto* stopButton = makeButton(QString::fromUtf8("停止"), this);
    auto* prevButton = makeButton(QString::fromUtf8("上一点"), this);
    auto* nextButton = makeButton(QString::fromUtf8("下一点"), this);
    auto* stepButton = makeButton(QString::fromUtf8("步进 0.1s"), this);
    auto* fromPointButton = makeButton(QString::fromUtf8("从选中点"), this);
    speedBox_ = new QComboBox(this);
    speedBox_->addItem(QString::fromUtf8("0.25x"), 0.25);
    speedBox_->addItem(QString::fromUtf8("0.5x"), 0.5);
    speedBox_->addItem(QString::fromUtf8("1x"), 1.0);
    speedBox_->addItem(QString::fromUtf8("2x"), 2.0);
    speedBox_->addItem(QString::fromUtf8("4x"), 4.0);
    speedBox_->setCurrentIndex(2);

    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setRange(0, 10000);
    readout_ = new QLabel(QString::fromUtf8("播放时按当前程序估算节拍。几何按插补，节拍为估算。"), this);
    readout_->setMinimumWidth(280);

    row->addWidget(playButton);
    row->addWidget(pauseButton);
    row->addWidget(stopButton);
    row->addWidget(prevButton);
    row->addWidget(nextButton);
    row->addWidget(stepButton);
    row->addWidget(fromPointButton);
    row->addWidget(speedBox_);
    row->addWidget(slider_, 1);
    row->addWidget(readout_);

    timer_ = new QTimer(this);
    timer_->setInterval(33);

    connect(playButton, &QPushButton::clicked, this, [this] { play(); });
    connect(pauseButton, &QPushButton::clicked, this, [this] { pausePlayback(); });
    connect(stopButton, &QPushButton::clicked, this, [this] { stopPlayback(); });
    connect(prevButton, &QPushButton::clicked, this, [this] { stepPoints(-1); });
    connect(nextButton, &QPushButton::clicked, this, [this] { stepPoints(1); });
    connect(stepButton, &QPushButton::clicked, this, [this] { stepClock(); });
    connect(fromPointButton, &QPushButton::clicked, this, [this] { playFromSelected(); });
    connect(speedBox_, &QComboBox::currentIndexChanged, this, [this](int) {
        cursor_.setSpeedMultiplier(speedBox_->currentData().toDouble());
    });
    connect(slider_, &QSlider::sliderPressed, this, [this] { pausePlayback(); });
    connect(slider_, &QSlider::valueChanged, this, [this](int value) {
        if (!sliderGuard_) {
            seekFromSlider(value);
        }
    });
    connect(timer_, &QTimer::timeout, this, [this] { tick(); });
}

void TimelinePanel::setViewportHooks(
    std::function<void(const algo::BakedFrame&)> showFrame,
    std::function<void(const std::vector<core::Vec3>&)> showTrail,
    std::function<void()> clearPreview) {
    showFrame_ = std::move(showFrame);
    showTrail_ = std::move(showTrail);
    clearPreview_ = std::move(clearPreview);
}

void TimelinePanel::invalidate() {
    timer_->stop();
    cursor_.unbind();
    plan_.reset();
    kinematics_.reset();
    if (clearPreview_) {
        clearPreview_();
    }
    sliderGuard_ = true;
    slider_->setValue(0);
    sliderGuard_ = false;
    readout_->setText(QString::fromUtf8("播放时按当前程序估算节拍。几何按插补，节拍为估算。"));
}

const core::Program* TimelinePanel::activeProgram() const {
    const auto& doc = controller_.document();
    if (!controller_.selectedOperationId().empty()) {
        for (const auto& cu : doc.controllers) {
            for (const auto& program : cu.programs) {
                for (const auto& operation : program.operations) {
                    if (operation.id == controller_.selectedOperationId()) {
                        return &program;
                    }
                }
            }
        }
    }
    for (const auto& cu : doc.controllers) {
        if (!cu.programs.empty()) {
            return &cu.programs.front();
        }
    }
    return nullptr;
}

void TimelinePanel::rebuildPlan() {
    timer_->stop();
    cursor_.unbind();
    plan_.reset();
    const core::Program* program = activeProgram();
    if (program == nullptr) {
        readout_->setText(QString::fromUtf8("没有可播放的程序。"));
        return;
    }

    core::Transform4d robotBase = core::Transform4d::fromTranslation(0.0, 0.0, 500.0);
    core::Transform4d utool = core::Transform4d::fromTranslation(0.0, 0.0, 150.0);
    for (const auto& node : controller_.document().sceneNodes) {
        if (node.type == core::SceneNodeType::Robot) {
            robotBase = node.localTransform;
            break;
        }
    }
    for (const auto& node : controller_.document().sceneNodes) {
        if (node.type == core::SceneNodeType::Tool) {
            utool = node.localTransform;
            break;
        }
    }
    const core::Transform4d uframe = controller_.document().computeUFrameInWorld(program->uframeNumber);

    kinematics_ = std::make_unique<algo::OpwKinematics>(controller_.activeRobotModel());
    algo::TrajectorySimulator simulator(*kinematics_, controller_.activeRobotModel());
    plan_ = simulator.plan(*program, robotBase, uframe, utool, 30.0);
    cursor_.bind(*plan_, *kinematics_);
    cursor_.setSpeedMultiplier(speedBox_->currentData().toDouble());
}

void TimelinePanel::publish(bool includeTrail) {
    if (!plan_) {
        return;
    }
    if (includeTrail && showTrail_) {
        std::vector<core::Vec3> trail;
        trail.reserve(plan_->frames.size());
        for (const auto& frame : plan_->frames) {
            trail.push_back(frame.tcpWorldPos);
        }
        showTrail_(trail);
    }
    if (showFrame_) {
        showFrame_(cursor_.sample());
    }
    updateReadout();
}

void TimelinePanel::updateReadout() {
    if (!plan_) {
        return;
    }
    const double duration = std::max(plan_->totalCycleTimeSec, 1e-6);
    sliderGuard_ = true;
    slider_->setValue(static_cast<int>(std::clamp(cursor_.timeSec() / duration, 0.0, 1.0) * 10000.0));
    sliderGuard_ = false;
    readout_->setText(QString::fromUtf8("估算 %1 / %2 s   焊接 %3 s   空走 %4 s   节拍为估算")
        .arg(cursor_.timeSec(), 0, 'f', 2)
        .arg(plan_->totalCycleTimeSec, 0, 'f', 2)
        .arg(plan_->arcWeldTimeSec, 0, 'f', 2)
        .arg(plan_->airCutTimeSec, 0, 'f', 2));
}

void TimelinePanel::play() {
    if (!plan_) {
        rebuildPlan();
    }
    if (!plan_) {
        return;
    }
    cursor_.setSpeedMultiplier(speedBox_->currentData().toDouble());
    cursor_.playFromStartIfFinished();
    publish(true);
    timer_->start();
}

void TimelinePanel::pausePlayback() {
    cursor_.pause();
    timer_->stop();
}

void TimelinePanel::stopPlayback() {
    timer_->stop();
    cursor_.stop();
    if (clearPreview_) {
        clearPreview_();
    }
    sliderGuard_ = true;
    slider_->setValue(0);
    sliderGuard_ = false;
    if (plan_) {
        updateReadout();
    }
}

void TimelinePanel::stepPoints(int direction) {
    if (!plan_) {
        rebuildPlan();
    }
    if (!plan_) {
        return;
    }
    timer_->stop();
    cursor_.stepTarget(direction);
    publish(true);
}

void TimelinePanel::stepClock() {
    if (!plan_) {
        rebuildPlan();
    }
    if (!plan_) {
        return;
    }
    timer_->stop();
    cursor_.stepTime(0.1);
    publish(true);
}

void TimelinePanel::playFromSelected() {
    if (!plan_) {
        rebuildPlan();
    }
    if (!plan_) {
        return;
    }
    const std::string targetId = controller_.selectedTargetId();
    if (!targetId.empty()) {
        cursor_.seekTarget(targetId);
    }
    cursor_.setSpeedMultiplier(speedBox_->currentData().toDouble());
    cursor_.play();
    publish(true);
    timer_->start();
}

void TimelinePanel::seekFromSlider(int sliderValue) {
    if (!plan_) {
        rebuildPlan();
    }
    if (!plan_) {
        return;
    }
    const double time = (static_cast<double>(sliderValue) / 10000.0) * plan_->totalCycleTimeSec;
    cursor_.seek(time);
    publish(true);
}

void TimelinePanel::tick() {
    cursor_.advanceWall(0.033);
    publish(false);
    if (cursor_.state() != algo::PlaybackState::Playing) {
        timer_->stop();
    }
}

} // namespace olp::ui
