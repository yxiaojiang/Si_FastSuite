#include "olp/ui/MainWindow.h"

#include <algorithm>

namespace olp::ui {

MainWindow::MainWindow() = default;

MainWindow::~MainWindow() = default;

void MainWindow::selectSceneNode(const std::string& nodeId) {
    if (selectedSceneNodeId_ == nodeId && selectedTargetId_.empty()) {
        return;
    }
    selectedSceneNodeId_ = nodeId;
    selectedTargetId_.clear();
    notifySelectionChanged();
}

void MainWindow::selectOperation(const std::string& operationId) {
    if (selectedOperationId_ == operationId && selectedTargetId_.empty() && selectedSceneNodeId_.empty()) {
        return;
    }
    selectedOperationId_ = operationId;
    selectedTargetId_.clear();
    selectedSceneNodeId_.clear();
    notifySelectionChanged();
}

void MainWindow::selectTarget(const std::string& targetId, const std::string& operationId) {
    if (selectedTargetId_ == targetId && selectedOperationId_ == operationId && selectedSceneNodeId_.empty()) {
        return;
    }
    selectedTargetId_ = targetId;
    selectedOperationId_ = operationId;
    selectedSceneNodeId_.clear();
    notifySelectionChanged();
}

int MainWindow::addSelectionListener(std::function<void()> listener) {
    const int id = nextSelectionListenerId_++;
    selectionListeners_.push_back(SelectionListener{id, std::move(listener)});
    return id;
}

void MainWindow::removeSelectionListener(int listenerId) {
    selectionListeners_.erase(
        std::remove_if(selectionListeners_.begin(), selectionListeners_.end(),
            [&](const SelectionListener& listener) { return listener.id == listenerId; }),
        selectionListeners_.end());
}

void MainWindow::noteSampleRun(int exitCode, int frameCount, double cycleTimeSec, double weldTimeSec) noexcept {
    lastSampleExitCode_ = exitCode;
    lastFrameCount_ = frameCount;
    lastCycleTimeSec_ = cycleTimeSec;
    lastWeldTimeSec_ = weldTimeSec;
}

void MainWindow::notifySelectionChanged() {
    const auto listeners = selectionListeners_;
    for (const auto& listener : listeners) {
        if (listener.fn) {
            listener.fn();
        }
    }
}

} // namespace olp::ui
