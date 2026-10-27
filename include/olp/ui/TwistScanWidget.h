#pragma once

#include "olp/algo/CollisionAndSim.h"

#include <QMouseEvent>
#include <QPaintEvent>
#include <QWidget>

#include <optional>
#include <vector>

namespace olp::ui {

class TwistScanWidget : public QWidget {
    Q_OBJECT
public:
    explicit TwistScanWidget(QWidget* parent = nullptr);

    void setSamples(const std::vector<algo::TwistScanSample>& samples, double currentAngleDeg = 0.0);
    std::optional<double> findOptimalFeasibleAngleDeg() const;
    const std::vector<algo::TwistScanSample>& samples() const noexcept { return samples_; }
    double currentAngleDeg() const noexcept { return currentAngleDeg_; }

signals:
    void angleSelected(double angleDeg);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    std::vector<algo::TwistScanSample> samples_{};
    double currentAngleDeg_{0.0};
    int hoveredIndex_{-1};
};

} // namespace olp::ui
