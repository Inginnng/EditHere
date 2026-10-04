#include "diagnosticspage.h"
#include "diagnostics.h"
#include "ui.h"
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

namespace h2d {
DiagnosticsPage::DiagnosticsPage(QWidget *parent) : QWidget(parent) {
    setObjectName("settingsDiagnosticsPage");
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 22, 20, 20);
    layout->setSpacing(18);
    title_ = new QLabel(this);
    title_->setObjectName("settingsSection");
    description_ = mutedLabel({}, this);
    description_->setObjectName("diagnosticsDescription");
    description_->setWordWrap(true);
    path_ = new QLabel(this);
    path_->setObjectName("diagnosticsPath");
    path_->setWordWrap(true);
    path_->setTextFormat(Qt::PlainText);
    path_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    status_ = new QLabel(this);
    status_->setObjectName("diagnosticsStatus");
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    for (auto label : {title_, description_, path_, status_}) layout->addWidget(label);
    auto actions = new QHBoxLayout;
    export_ = textButton({}, true, this);
    export_->setObjectName("exportDiagnostics");
    open_ = textButton({}, false, this);
    open_->setObjectName("openDiagnosticsFolder");
    actions->addWidget(export_);
    actions->addWidget(open_);
    actions->addStretch();
    layout->addLayout(actions);
    layout->addStretch();
    connect(export_, &QPushButton::clicked, this, &DiagnosticsPage::exportLogs);
    connect(open_, &QPushButton::clicked, this, [this] {
        refresh();
        const auto folder = diagnostics::directory();
        if (!folder.isEmpty() && !QDesktopServices::openUrl(QUrl::fromLocalFile(folder))) {
            diagnostics::write(diagnostics::Level::Warning, "diagnostics.open_folder", "Unable to open log folder");
            status_->setText(tr("无法打开日志目录，可以复制上方路径手动打开。"));
        }
    });
    retranslate();
}
void DiagnosticsPage::changeEvent(QEvent *event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) retranslate();
}
void DiagnosticsPage::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    refresh();
}
void DiagnosticsPage::retranslate() {
    title_->setText(tr("诊断日志"));
    description_->setText(tr("遇到问题后，导出日志并随问题描述一起反馈。日志记录程序版本、运行环境、操作状态和错误，不包含截图、视频或批注内容。日志仅保存在本机，不会自动上传。"));
    export_->setText(tr("导出诊断日志…"));
    export_->setToolTip(tr("将近期日志和运行环境汇总为一个文本文件，方便反馈问题。"));
    open_->setText(tr("打开日志目录"));
    open_->setToolTip(tr("查看程序自动保存的日志文件。"));
    refresh();
}
void DiagnosticsPage::refresh() {
    const auto folder = diagnostics::directory();
    path_->setText(folder.isEmpty() ? tr("日志尚未初始化。") : QDir::toNativeSeparators(folder));
    open_->setEnabled(!folder.isEmpty() && QDir(folder).exists());
    export_->setEnabled(!diagnostics::logFiles().isEmpty());
    const auto error = diagnostics::lastError();
    status_->setText(error.isEmpty() ? tr("自动记录错误，并限制日志大小；旧日志会自动轮换。")
                                   : tr("日志写入失败：%1").arg(error));
}
void DiagnosticsPage::exportLogs() {
    const auto filename = QStringLiteral("EditHere-diagnostics-%1.txt")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
    auto path = QFileDialog::getSaveFileName(this, tr("导出诊断日志"),
        QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(filename),
        tr("文本文件 (*.txt)"));
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += ".txt";
    const auto error = diagnostics::exportReport(path);
    if (!error.isEmpty()) {
        diagnostics::write(diagnostics::Level::Error, "diagnostics.export", error);
        status_->setText(tr("日志导出失败：%1").arg(error));
    } else {
        status_->setText(tr("日志已导出：%1").arg(QDir::toNativeSeparators(path)));
    }
}
} // namespace h2d
