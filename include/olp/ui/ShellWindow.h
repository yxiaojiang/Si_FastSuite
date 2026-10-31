#pragma once

#include "olp/algo/CollisionAndSim.h"
#include "olp/ui/MainWindow.h"
#include "olp/ui/TwistScanWidget.h"

#include <QMainWindow>
#include <QStringList>
#include <QWidget>
#include <functional>
#include <optional>
#include <vector>

class QAction;
class QCloseEvent;
class QDockWidget;
class QLabel;
class QSlider;
class QTreeWidget;

namespace olp::ui {

class GizmoDragPreviewHandler;
class IssueListPanel;
class OcctViewport;
class TargetPropertyPanel;
class TargetWaypointTable;
class TimelinePanel;

// M09: Speed cell parser and formatter helpers
struct ParsedSpeedCell {
    double speedValue{0.0};
    bool useWeldSpeed{false};
    bool valid{false};
};

ParsedSpeedCell parseSpeedCellText(const QString& rawText, double fallbackSpeed = 0.0);
QString formatSpeedCellText(const core::Target& target);

// M01 shell: two workbenches and the dock zones. The center view is OcctViewport.
class ShellWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit ShellWindow(MainWindow& controller, QWidget* parent = nullptr);
    ~ShellWindow() override;

    void setSampleLoader(std::function<void()> loader);

    QStringList dockTitles() const;
    bool zoneChecked(const QString& title) const;

    // M14-012: Non-blocking test hook for dirty document confirmation.
    // Return true to discard unsaved changes and proceed; return false to cancel.
    using ConfirmDiscardCallback = std::function<bool()>;
    void setConfirmDiscardCallback(ConfirmDiscardCallback callback);

public slots:
    void showLayoutWorkbench();
    void showOlpWorkbench();
    void loadSample();
    void refreshFromDocument();
    void openProjectFile();
    void openProjectFile(const QString& explicitPath);
    void saveProjectFile();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildChrome();
    void applyWorkbench(ActiveWorkbench workbench);
    void rebuildSceneTree();
    void rebuildProgramTree();
    void rebuildTargetTable();
    void showPropertiesForScene(const QString& nodeId);
    void showPropertiesForTarget(const QString& targetId);
    void updateUndoActions();
    void updateActionStates();
    void fillIssues();
    void applySelection();
    void syncJogFromRobot();
    std::vector<std::string> selectedTargetIdsFromTable() const;

    MainWindow& controller_;
    std::function<void()> loader_{};
    OcctViewport* viewport_{nullptr};
    size_t lastSceneNodeCount_{0};
    size_t lastSeamCount_{0};
    int documentListenerId_{-1};
    int selectionListenerId_{-1};

    QTreeWidget* sceneTree_{nullptr};
    QTreeWidget* programTree_{nullptr};
    TargetWaypointTable* targetTable_{nullptr};
    IssueListPanel* issueList_{nullptr};
    TargetPropertyPanel* propertyPanel_{nullptr};
    QLabel* jogReadout_{nullptr};
    QSlider* jogSliders_[6]{};
    TimelinePanel* timeline_{nullptr};
    std::unique_ptr<GizmoDragPreviewHandler> gizmoPreviewHandler_{nullptr};

    QDockWidget* sceneDock_{nullptr};
    QDockWidget* programDock_{nullptr};
    QDockWidget* targetDock_{nullptr};
    QDockWidget* propertyDock_{nullptr};
    QDockWidget* jogDock_{nullptr};
    QDockWidget* issueDock_{nullptr};

    QAction* layoutAction_{nullptr};
    QAction* olpAction_{nullptr};
    QAction* undoAction_{nullptr};
    QAction* redoAction_{nullptr};
    QAction* exportMenuAction_{nullptr};
    QAction* exportPdfAction_{nullptr};
    QAction* exportHtmlAction_{nullptr};
    QAction* exportAction_{nullptr};
    QAction* pathAction_{nullptr};
    QAction* slerpAction_{nullptr};
    QAction* prevTargetAction_{nullptr};
    QAction* nextTargetAction_{nullptr};
    struct RefreshGuard {
        int& depth;
        explicit RefreshGuard(int& d) : depth(d) { ++depth; }
        ~RefreshGuard() { --depth; }
    };

    int refreshDepth_{0};
    bool isRefreshing() const noexcept { return refreshDepth_ > 0; }
    bool inItemChanged_{false};

    bool confirmDiscardIfDirty();
    ConfirmDiscardCallback confirmDiscardCallback_{};
};

} // namespace olp::ui
