#include "olp/ui/LsExportDialog.h"
#include "olp/ui/MainWindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QFileDialog>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace olp::ui {

LsExportDialog::LsExportDialog(MainWindow& controller, QWidget* parent)
    : QDialog(parent), controller_(controller) {
    setWindowTitle(QString::fromUtf8("FANUC ASCII .ls 导出与代码预览"));
    resize(780, 620);
    setupUi();
    refreshPreview();
}

void LsExportDialog::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    // Header info / status
    statusLabel_ = new QLabel(this);
    statusLabel_->setTextFormat(Qt::RichText);
    mainLayout->addWidget(statusLabel_);

    // Options group box
    auto* optGroup = new QGroupBox(QString::fromUtf8("导出选项 (LsPostConfig)"), this);
    auto* optLayout = new QHBoxLayout(optGroup);
    optLayout->setContentsMargins(10, 8, 10, 8);
    optLayout->setSpacing(16);

    chkArcAppl_ = new QCheckBox(QString::fromUtf8("包含 /APPL 焊接应用"), optGroup);
    chkArcAppl_->setChecked(true);
    chkReturnHome_ = new QCheckBox(QString::fromUtf8("尾部追加 Return Home"), optGroup);
    chkReturnHome_->setChecked(true);
    chkHomeJoint_ = new QCheckBox(QString::fromUtf8("Home 输出为 GP1 关节点"), optGroup);
    chkHomeJoint_->setChecked(false);
    chkBlockCollision_ = new QCheckBox(QString::fromUtf8("阻断碰撞点导出"), optGroup);
    chkBlockCollision_->setChecked(false);

    optLayout->addWidget(chkArcAppl_);
    optLayout->addWidget(chkReturnHome_);
    optLayout->addWidget(chkHomeJoint_);
    optLayout->addWidget(chkBlockCollision_);
    optLayout->addStretch(1);

    connect(chkArcAppl_, &QCheckBox::toggled, this, &LsExportDialog::refreshPreview);
    connect(chkReturnHome_, &QCheckBox::toggled, this, &LsExportDialog::refreshPreview);
    connect(chkHomeJoint_, &QCheckBox::toggled, this, &LsExportDialog::refreshPreview);
    connect(chkBlockCollision_, &QCheckBox::toggled, this, &LsExportDialog::refreshPreview);

    mainLayout->addWidget(optGroup);

    // Monospace Code Editor
    codeEdit_ = new QPlainTextEdit(this);
    codeEdit_->setReadOnly(true);
    QFont monoFont(QString::fromUtf8("Consolas"), 10);
    monoFont.setStyleHint(QFont::Monospace);
    codeEdit_->setFont(monoFont);
    codeEdit_->setStyleSheet(
        QString::fromUtf8("QPlainTextEdit { background-color: #1e1e1e; color: #d4d4d4; "
                          "border: 1px solid #3c3c3c; border-radius: 4px; padding: 6px; }"));
    mainLayout->addWidget(codeEdit_, 1);

    // Bottom Action Buttons
    auto* btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(10);

    auto* copyBtn = new QPushButton(QString::fromUtf8("复制到剪贴板"), this);
    copyBtn->setToolTip(QString::fromUtf8("将生成的 .ls 文本完整复制到系统剪贴板"));
    connect(copyBtn, &QPushButton::clicked, this, &LsExportDialog::copyToClipboard);
    btnLayout->addWidget(copyBtn);

    btnLayout->addStretch(1);

    auto* exportBtn = new QPushButton(QString::fromUtf8("导出为 .ls 文件..."), this);
    exportBtn->setDefault(true);
    exportBtn->setStyleSheet(
        QString::fromUtf8("QPushButton { background-color: #007acc; color: white; font-weight: bold; padding: 6px 14px; border-radius: 3px; }"
                          "QPushButton:hover { background-color: #0098ff; }"));
    connect(exportBtn, &QPushButton::clicked, this, &LsExportDialog::exportToFile);
    btnLayout->addWidget(exportBtn);

    auto* closeBtn = new QPushButton(QString::fromUtf8("关闭"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(closeBtn);

    mainLayout->addLayout(btnLayout);
}

void LsExportDialog::refreshPreview() {
    config_.includeArcWeldingAppl = chkArcAppl_->isChecked();
    config_.appendReturnHome = chkReturnHome_->isChecked();
    config_.emitHomeAsJointTarget = chkHomeJoint_->isChecked();
    config_.blockOnCollision = chkBlockCollision_->isChecked();

    const auto& doc = controller_.document();
    const core::Program* activeProg = nullptr;
    for (const auto& cu : doc.controllers) {
        if (!cu.programs.empty()) {
            activeProg = &cu.programs.front();
            break;
        }
    }

    if (!activeProg) {
        statusLabel_->setText(QString::fromUtf8("<b style='color:#e06c75;'>未找到可用程序。请先创建或加载工位程序。</b>"));
        codeEdit_->setPlainText(QString());
        return;
    }

    const auto valResult = io::FanucLsPostProcessor::validateProgram(*activeProg, config_);
    std::string errStr;
    const std::string lsText = io::FanucLsPostProcessor::generateLsString(*activeProg, config_, &errStr);

    if (valResult.ok && !lsText.empty()) {
        statusLabel_->setText(QString::fromUtf8(
            "<span style='color:#98c379;'><b>✓ 语法与运动学校验通过</b></span> "
            "| 程序: <b>%1</b> | 操作数: %2 | 注释: \"%3\"")
            .arg(QString::fromStdString(activeProg->name))
            .arg(activeProg->operations.size())
            .arg(QString::fromStdString(activeProg->comment)));
        codeEdit_->setPlainText(QString::fromStdString(lsText));
    } else {
        QString issuesHtml;
        for (const auto& is : valResult.issues) {
            if (is.severity == io::LsValidationIssue::Severity::Error) {
                issuesHtml += QString::fromUtf8("<br/>• <span style='color:#e06c75;'>%1</span>").arg(QString::fromStdString(is.message));
            } else {
                issuesHtml += QString::fromUtf8("<br/>• <span style='color:#e5c07b;'>%1</span>").arg(QString::fromStdString(is.message));
            }
        }
        statusLabel_->setText(QString::fromUtf8("<b style='color:#e06c75;'>导出校验未通过:</b>%1").arg(issuesHtml));
        codeEdit_->setPlainText(QString::fromStdString(lsText));
    }
}

void LsExportDialog::copyToClipboard() {
    const QString text = codeEdit_->toPlainText();
    if (!text.isEmpty()) {
        QApplication::clipboard()->setText(text);
        QMessageBox::information(this, QString::fromUtf8("已复制"), QString::fromUtf8("FANUC .ls 代码已成功复制到系统剪贴板。"));
    }
}

void LsExportDialog::exportToFile() {
    const QString text = codeEdit_->toPlainText();
    if (text.isEmpty()) {
        QMessageBox::warning(this, QString::fromUtf8("无法导出"), QString::fromUtf8("当前无可导出的 .ls 内容。"));
        return;
    }

    const auto& doc = controller_.document();
    std::string defaultProgName = "WELD_001";
    for (const auto& cu : doc.controllers) {
        if (!cu.programs.empty()) {
            defaultProgName = cu.programs.front().name;
            break;
        }
    }

    const QString defaultPath = QString::fromStdString(defaultProgName + ".ls");
    const QString savePath = QFileDialog::getSaveFileName(
        this,
        QString::fromUtf8("保存 FANUC .ls 程序"),
        defaultPath,
        QString::fromUtf8("FANUC ASCII LS Files (*.ls);;All Files (*.*)"));

    if (savePath.isEmpty()) return;

    std::string exportErr;
    if (controller_.exportFanucLs(savePath.toStdString(), "", config_, &exportErr)) {
        QMessageBox::information(this, QString::fromUtf8("导出成功"),
            QString::fromUtf8("已成功导出 FANUC .ls 文件至:\n%1").arg(savePath));
    } else {
        QMessageBox::critical(this, QString::fromUtf8("导出失败"),
            QString::fromUtf8("写入 .ls 文件失败:\n%1").arg(QString::fromStdString(exportErr)));
    }
}

} // namespace olp::ui
