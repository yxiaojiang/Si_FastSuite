#include "ShellChromeHelpers.h"

#include <QAction>
#include <QDockWidget>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QToolButton>
#include <QVBoxLayout>

namespace olp::ui {

QIcon commandIcon(IconKind kind, const QColor& ink) {
    QPixmap pix(32, 32);
    pix.fill(Qt::transparent);
    QPainter painter(&pix);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(ink, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    switch (kind) {
    case IconKind::New:
        painter.drawRoundedRect(8, 6, 14, 18, 2, 2);
        painter.drawLine(12, 14, 18, 14);
        painter.drawLine(15, 11, 15, 17);
        break;
    case IconKind::Open:
        painter.drawRect(7, 12, 18, 12);
        painter.drawLine(7, 12, 12, 8);
        painter.drawLine(12, 8, 20, 8);
        painter.drawLine(20, 8, 20, 12);
        break;
    case IconKind::Save:
        painter.drawRoundedRect(8, 6, 16, 18, 2, 2);
        painter.drawRect(12, 6, 8, 6);
        painter.drawLine(11, 18, 21, 18);
        break;
    case IconKind::Import:
        painter.drawLine(16, 6, 16, 18);
        painter.drawLine(11, 14, 16, 19);
        painter.drawLine(21, 14, 16, 19);
        painter.drawLine(8, 24, 24, 24);
        break;
    case IconKind::Sample:
        painter.drawRect(8, 10, 16, 12);
        painter.drawLine(8, 10, 16, 6);
        painter.drawLine(24, 10, 16, 6);
        break;
    case IconKind::Undo:
        painter.drawArc(10, 8, 12, 12, 30 * 16, 180 * 16);
        painter.drawLine(10, 14, 6, 11);
        painter.drawLine(10, 14, 8, 18);
        break;
    case IconKind::Redo:
        painter.drawArc(10, 8, 12, 12, 0, 180 * 16);
        painter.drawLine(22, 14, 26, 11);
        painter.drawLine(22, 14, 24, 18);
        break;
    case IconKind::Copy:
        painter.drawRect(12, 8, 12, 14);
        painter.drawRect(8, 12, 12, 14);
        break;
    case IconKind::Paste:
        painter.drawRoundedRect(9, 8, 14, 16, 2, 2);
        painter.drawLine(12, 8, 12, 6);
        painter.drawLine(12, 6, 20, 6);
        painter.drawLine(20, 6, 20, 8);
        break;
    case IconKind::Delete:
        painter.drawLine(10, 11, 22, 11);
        painter.drawRect(11, 11, 10, 12);
        painter.drawLine(14, 8, 18, 8);
        break;
    case IconKind::Layout:
        painter.drawRect(7, 7, 8, 8);
        painter.drawRect(17, 7, 8, 8);
        painter.drawRect(7, 17, 8, 8);
        painter.drawRect(17, 17, 8, 8);
        break;
    case IconKind::Program:
        painter.drawEllipse(QPointF(9, 10), 2.5, 2.5);
        painter.drawEllipse(QPointF(18, 16), 2.5, 2.5);
        painter.drawEllipse(QPointF(24, 10), 2.5, 2.5);
        painter.drawLine(11, 11, 16, 15);
        painter.drawLine(20, 15, 22, 11);
        break;
    case IconKind::Fit:
        painter.drawLine(7, 12, 7, 7);
        painter.drawLine(7, 7, 12, 7);
        painter.drawLine(20, 7, 25, 7);
        painter.drawLine(25, 7, 25, 12);
        painter.drawLine(25, 20, 25, 25);
        painter.drawLine(25, 25, 20, 25);
        painter.drawLine(12, 25, 7, 25);
        painter.drawLine(7, 25, 7, 20);
        break;
    case IconKind::Pick:
        painter.drawLine(10, 22, 16, 10);
        painter.drawLine(16, 10, 22, 16);
        painter.drawLine(14, 16, 18, 20);
        break;
    case IconKind::Path:
        painter.drawLine(7, 22, 14, 12);
        painter.drawLine(14, 12, 20, 18);
        painter.drawLine(20, 18, 26, 8);
        break;
    case IconKind::Sim:
        painter.drawPolygon(QPolygonF({QPointF(12, 8), QPointF(24, 16), QPointF(12, 24)}));
        break;
    case IconKind::Export:
        painter.drawLine(16, 20, 16, 8);
        painter.drawLine(11, 12, 16, 7);
        painter.drawLine(21, 12, 16, 7);
        painter.drawLine(8, 24, 24, 24);
        break;
    }
    return QIcon(pix);
}

QAction* disabledAction(QWidget* owner, const QString& text, const QString& tip, IconKind kind, const QColor& ink) {
    auto* action = new QAction(commandIcon(kind, ink), text, owner);
    action->setToolTip(tip);
    action->setStatusTip(tip);
    action->setEnabled(false);
    return action;
}

QWidget* commandGroup(QWidget* parent, const QString& title, const QList<QAction*>& actions) {
    auto* group = new QWidget(parent);
    auto* column = new QVBoxLayout(group);
    column->setContentsMargins(8, 4, 8, 2);
    column->setSpacing(1);
    auto* row = new QHBoxLayout();
    row->setSpacing(2);
    for (auto* action : actions) {
        auto* button = new QToolButton(group);
        button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setIconSize(QSize(24, 24));
        button->setAutoRaise(true);
        button->setMinimumWidth(52);
        row->addWidget(button);
    }
    auto* caption = new QLabel(title, group);
    caption->setAlignment(Qt::AlignCenter);
    caption->setStyleSheet(QString::fromUtf8("color: #5d6c78; font-size: 11px;"));
    column->addLayout(row);
    column->addWidget(caption);
    return group;
}

QWidget* groupDivider(QWidget* parent) {
    auto* line = new QFrame(parent);
    line->setFrameShape(QFrame::VLine);
    line->setStyleSheet(QString::fromUtf8("color: #c5d0d8;"));
    line->setFixedWidth(8);
    return line;
}

QDockWidget* makeDock(QMainWindow* owner, const QString& title, QWidget* body) {
    auto* dock = new QDockWidget(title, owner);
    dock->setObjectName(title);
    QPalette pal = dock->palette();
    pal.setColor(QPalette::WindowText, QColor(255, 255, 255));
    pal.setColor(QPalette::Text, QColor(255, 255, 255));
    dock->setPalette(pal);
    dock->setWidget(body);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
    return dock;
}

} // namespace olp::ui
