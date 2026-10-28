#pragma once

#include <QColor>
#include <QIcon>
#include <QList>
#include <QString>

class QAction;
class QDockWidget;
class QMainWindow;
class QWidget;

namespace olp::ui {

enum class IconKind {
    New, Open, Save, Import, Sample,
    Undo, Redo, Copy, Paste, Delete,
    Layout, Program, Fit,
    Pick, Path, Sim, Export
};

QIcon commandIcon(IconKind kind, const QColor& ink);
QAction* disabledAction(QWidget* owner, const QString& text, const QString& tip, IconKind kind, const QColor& ink);
QWidget* commandGroup(QWidget* parent, const QString& title, const QList<QAction*>& actions);
QWidget* groupDivider(QWidget* parent);
QDockWidget* makeDock(QMainWindow* owner, const QString& title, QWidget* body);

} // namespace olp::ui
