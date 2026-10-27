#pragma once

#include "olp/io/FanucLsPostProcessor.h"
#include <QDialog>

class QPlainTextEdit;
class QLabel;
class QCheckBox;

namespace olp::ui {

class MainWindow;

class LsExportDialog : public QDialog {
    Q_OBJECT
public:
    explicit LsExportDialog(MainWindow& controller, QWidget* parent = nullptr);
    ~LsExportDialog() override = default;

private slots:
    void refreshPreview();
    void copyToClipboard();
    void exportToFile();

private:
    void setupUi();

    MainWindow& controller_;
    io::LsPostConfig config_{};
    QLabel* statusLabel_{nullptr};
    QPlainTextEdit* codeEdit_{nullptr};
    QCheckBox* chkArcAppl_{nullptr};
    QCheckBox* chkReturnHome_{nullptr};
    QCheckBox* chkHomeJoint_{nullptr};
    QCheckBox* chkBlockCollision_{nullptr};
};

} // namespace olp::ui
