#pragma once

#include "olp/ui/MainWindow.h"

#include <QWidget>

#include <functional>
#include <string>
#include <vector>

class QGroupBox;
class QPlainTextEdit;

namespace olp::ui {

class TwistScanWidget;

class TargetPropertyPanel : public QWidget {
public:
    TargetPropertyPanel(MainWindow& controller,
                        std::function<std::vector<std::string>()> selectedTargetIds,
                        std::function<void()> refresh,
                        QWidget* parent = nullptr);

    void showSceneNode(const QString& nodeId);
    void showTarget(const QString& targetId);
    void showOperationSummary(const std::string& operationName, int targetCount);

private:
    void showStatus(const QString& message, int timeoutMs);

    MainWindow& controller_;
    std::function<std::vector<std::string>()> selectedTargetIds_;
    std::function<void()> refresh_;
    QPlainTextEdit* propertyText_{nullptr};
    QGroupBox* spinTuningWidget_{nullptr};
    TwistScanWidget* twistScanWidget_{nullptr};
};

} // namespace olp::ui
