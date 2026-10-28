#pragma once

#include "olp/algo/TrajectorySimulator.h"
#include "olp/ui/MainWindow.h"

#include <QWidget>

#include <functional>
#include <memory>
#include <optional>

class QComboBox;
class QLabel;
class QSlider;
class QTimer;

namespace olp::ui {

// M11 playback bar. The shell keeps the toolbar slot; this widget owns the clock.
class TimelinePanel : public QWidget {
public:
    explicit TimelinePanel(MainWindow& controller, QWidget* parent = nullptr);

    void setViewportHooks(
        std::function<void(const algo::BakedFrame&)> showFrame,
        std::function<void(const std::vector<core::Vec3>&)> showTrail,
        std::function<void()> clearPreview);

    void invalidate();
    void stopPlayback();

private:
    void rebuildPlan();
    void play();
    void pausePlayback();
    void stepPoints(int direction);
    void stepClock();
    void playFromSelected();
    void seekFromSlider(int sliderValue);
    void tick();
    void publish(bool includeTrail);
    void updateReadout();
    const core::Program* activeProgram() const;

    MainWindow& controller_;
    std::unique_ptr<algo::OpwKinematics> kinematics_;
    std::optional<algo::TrajectoryPlan> plan_;
    algo::PlaybackCursor cursor_;
    bool sliderGuard_{false};

    std::function<void(const algo::BakedFrame&)> showFrame_;
    std::function<void(const std::vector<core::Vec3>&)> showTrail_;
    std::function<void()> clearPreview_;

    QLabel* readout_{nullptr};
    QSlider* slider_{nullptr};
    QComboBox* speedBox_{nullptr};
    QTimer* timer_{nullptr};
};

} // namespace olp::ui
