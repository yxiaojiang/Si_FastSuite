#include "olp/ui/TwistScanWidget.h"

#include <QFont>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QToolTip>

#include <algorithm>
#include <cmath>

namespace olp::ui {

TwistScanWidget::TwistScanWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(220, 110);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(true);
}

void TwistScanWidget::setSamples(const std::vector<algo::TwistScanSample>& samples, double currentAngleDeg) {
    samples_ = samples;
    currentAngleDeg_ = currentAngleDeg;
    hoveredIndex_ = -1;
    update();
}

std::optional<double> TwistScanWidget::findOptimalFeasibleAngleDeg() const {
    if (samples_.empty()) return std::nullopt;

    const size_t n = samples_.size();
    auto isFeasible = [](const algo::TwistScanSample& s) {
        return s.reachable && s.collisionFree && !s.nearSingularityOrLimit && s.minClearanceMm >= 5.0;
    };

    size_t bestStart = 0;
    size_t bestLen = 0;
    size_t curStart = 0;
    size_t curLen = 0;

    for (size_t i = 0; i < 2 * n; ++i) {
        if (isFeasible(samples_[i % n])) {
            if (curLen == 0) curStart = i;
            ++curLen;
            if (curLen > bestLen) {
                bestLen = curLen;
                bestStart = curStart;
            }
            if (curLen >= n) break;
        } else {
            curLen = 0;
        }
    }

    if (bestLen == 0) {
        for (size_t i = 0; i < 2 * n; ++i) {
            if (samples_[i % n].reachable && samples_[i % n].collisionFree) {
                if (curLen == 0) curStart = i;
                ++curLen;
                if (curLen > bestLen) {
                    bestLen = curLen;
                    bestStart = curStart;
                }
                if (curLen >= n) break;
            } else {
                curLen = 0;
            }
        }
    }

    if (bestLen == 0) return std::nullopt;

    const size_t midIdx = (bestStart + bestLen / 2) % n;
    return samples_[midIdx].twistAngleDeg;
}

void TwistScanWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const int w = width();
    const int h = height();
    const int cx = w / 2;
    const int cy = h / 2;
    const int rOut = std::min(cx, cy) - 6;
    const int rIn = std::max(10, rOut - 16);

    if (samples_.empty()) {
        painter.setPen(QPen(QColor(100, 100, 100, 140), 1.0, Qt::DashLine));
        painter.setBrush(QColor(40, 44, 52, 120));
        painter.drawEllipse(QPointF(cx, cy), rOut, rOut);
        painter.setBrush(QColor(30, 33, 39));
        painter.drawEllipse(QPointF(cx, cy), rIn, rIn);
        painter.setPen(QColor(180, 180, 180));
        painter.setFont(QFont("Segoe UI", 9));
        painter.drawText(rect(), Qt::AlignCenter, QString::fromUtf8("选择路径点以扫描自转角"));
        return;
    }

    const size_t n = samples_.size();
    const double stepDeg = 360.0 / static_cast<double>(n);

    for (size_t i = 0; i < n; ++i) {
        const auto& s = samples_[i];
        QColor color;
        if (!s.reachable || !s.collisionFree) {
            color = QColor(225, 55, 55); // Red
        } else if (s.nearSingularityOrLimit || s.minClearanceMm < 5.0) {
            color = QColor(235, 185, 25); // Yellow
        } else {
            color = QColor(45, 195, 80); // Green
        }

        if (static_cast<int>(i) == hoveredIndex_) {
            color = color.lighter(130);
        }

        const double a1 = core::deg2rad(s.twistAngleDeg);
        const double a2 = core::deg2rad(s.twistAngleDeg + stepDeg);

        QPolygonF sectorPoly;
        sectorPoly << QPointF(cx + rIn * std::cos(a1), cy - rIn * std::sin(a1))
                   << QPointF(cx + rOut * std::cos(a1), cy - rOut * std::sin(a1))
                   << QPointF(cx + rOut * std::cos(a2), cy - rOut * std::sin(a2))
                   << QPointF(cx + rIn * std::cos(a2), cy - rIn * std::sin(a2));

        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawPolygon(sectorPoly);
    }

    // Inner background circle
    painter.setPen(QPen(QColor(30, 33, 39), 1.5));
    painter.setBrush(QColor(36, 40, 48));
    painter.drawEllipse(QPointF(cx, cy), rIn, rIn);

    // Current angle needle / pointer
    const double curRad = core::deg2rad(currentAngleDeg_);
    const QPointF needleTip(cx + (rOut + 2) * std::cos(curRad), cy - (rOut + 2) * std::sin(curRad));
    const QPointF needleBase(cx + (rIn - 4) * std::cos(curRad), cy - (rIn - 4) * std::sin(curRad));
    painter.setPen(QPen(QColor(255, 255, 255), 2.5, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(needleBase, needleTip);

    // Center text
    painter.setPen(QColor(240, 240, 240));
    painter.setFont(QFont("Segoe UI", 9, QFont::Bold));
    painter.drawText(QRect(cx - rIn, cy - rIn, 2 * rIn, 2 * rIn), Qt::AlignCenter,
                     QString("%1°").arg(currentAngleDeg_, 0, 'f', 0));
}

void TwistScanWidget::mousePressEvent(QMouseEvent* event) {
    if (samples_.empty()) return;
    const double dx = event->pos().x() - (width() / 2);
    const double dy = (height() / 2) - event->pos().y();
    if (std::hypot(dx, dy) < 8.0) return;

    double deg = core::rad2deg(std::atan2(dy, dx));
    deg = std::round(deg / 5.0) * 5.0;
    if (deg > 180.0) deg -= 360.0;
    if (deg <= -180.0) deg += 360.0;
    emit angleSelected(deg);
}

void TwistScanWidget::mouseMoveEvent(QMouseEvent* event) {
    if (samples_.empty()) return;
    const double dx = event->pos().x() - (width() / 2);
    const double dy = (height() / 2) - event->pos().y();
    const double r = std::hypot(dx, dy);
    const int rOut = std::min(width(), height()) / 2 - 6;
    const int rIn = std::max(10, rOut - 16);

    if (r >= rIn - 4 && r <= rOut + 4) {
        double deg = core::rad2deg(std::atan2(dy, dx));
        if (deg > 180.0) deg -= 360.0;
        if (deg < -180.0) deg += 360.0;

        int closest = 0;
        double minDiff = 1e9;
        for (size_t i = 0; i < samples_.size(); ++i) {
            const double diff = std::abs(core::normalizeAngleRad(core::deg2rad(samples_[i].twistAngleDeg - deg)));
            if (diff < minDiff) {
                minDiff = diff;
                closest = static_cast<int>(i);
            }
        }
        hoveredIndex_ = closest;
        const auto& s = samples_[static_cast<size_t>(closest)];
        QString statusDesc = QString::fromUtf8("可行安全");
        if (!s.reachable) statusDesc = QString::fromUtf8("不可达 (超出工作空间)");
        else if (!s.collisionFree) statusDesc = QString::fromUtf8("碰撞干涉");
        else if (s.nearSingularityOrLimit) statusDesc = QString::fromUtf8("警示 (接近奇异或关节限位)");
        else if (s.minClearanceMm < 5.0) statusDesc = QString::fromUtf8("间隙偏小 (< 5mm)");

        setToolTip(QString::fromUtf8("自转角: %1°\n评估: %2\n最小间隙: %3 mm")
            .arg(s.twistAngleDeg, 0, 'f', 1)
            .arg(statusDesc)
            .arg(s.minClearanceMm, 0, 'f', 1));
        update();
    } else {
        if (hoveredIndex_ != -1) {
            hoveredIndex_ = -1;
            update();
        }
    }
}

} // namespace olp::ui
