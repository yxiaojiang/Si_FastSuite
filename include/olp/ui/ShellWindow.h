#pragma once

#include "olp/ui/MainWindow.h"

#include <QMainWindow>
#include <QStringList>
#include <functional>

class QAction;
class QDockWidget;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QSlider;
class QTableWidget;
class QTreeWidget;

namespace olp::ui {

class OcctViewport;

// M01 shell: two workbenches and the dock zones. The center view is OcctViewport.
class ShellWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit ShellWindow(MainWindow& controller, QWidget* parent = nullptr);
    ~ShellWindow() override;

    void setSampleLoader(std::function<void()> loader);

    QStringList dockTitles() const;
    bool zoneChecked(const QString& title) const;

public slots:
    void showLayoutWorkbench();
    void showOlpWorkbench();
    void loadSample();
    void refreshFromDocument();

private:
    void buildChrome();
    void applyWorkbench(ActiveWorkbench workbench);
    void rebuildSceneTree();
    void rebuildProgramTree();
    void rebuildTargetTable();
    void showPropertiesForScene(const QString& nodeId);
    void showPropertiesForTarget(const QString& targetId);
    void updateUndoActions();
    void fillIssues();
    void applySelection();
    void syncJogFromRobot();

    MainWindow& controller_;
    std::function<void()> loader_{};
    OcctViewport* viewport_{nullptr};
    int documentListenerId_{-1};
    int selectionListenerId_{-1};

    QTreeWidget* sceneTree_{nullptr};
    QTreeWidget* programTree_{nullptr};
    QTableWidget* targetTable_{nullptr};
    QListWidget* issueList_{nullptr};
    QPlainTextEdit* propertyText_{nullptr};
    QLabel* jogReadout_{nullptr};
    QSlider* jogSliders_[6]{};
    QLabel* timelineLabel_{nullptr};
    QSlider* timelineSlider_{nullptr};

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
    bool refreshing_{false};
};

} // namespace olp::ui
