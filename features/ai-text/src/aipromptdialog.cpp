#include "aipromptdialog.h"
#include "aifeedback.h"
#include "ui.h"
#include <QCoreApplication>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace h2d {
std::optional<QString> editAiSystemPrompt(const QString &prompt, QWidget *parent) {
    QDialog dialog(parent);
    dialog.setObjectName("aiPromptDialog");
    dialog.setWindowTitle(QCoreApplication::translate("h2d::AiSettingsPage", "EditHere 系统提示词"));
    dialog.resize(680, 560);
    auto *body = new QVBoxLayout(&dialog);
    body->setContentsMargins(24, 24, 24, 24);
    auto *hint = mutedLabel(QCoreApplication::translate("h2d::AiSettingsPage",
        "这段提示词会随每次解析一起发送给所选服务商。修改只影响本机，可随时恢复默认。"), &dialog);
    hint->setWordWrap(true);
    body->addWidget(hint);
    auto *text = new QPlainTextEdit(&dialog);
    text->setObjectName("aiBuiltinPrompt");
    text->setPlainText(prompt.isEmpty() ? aiSystemPrompt() : prompt);
    body->addWidget(text, 1);
    auto *actions = new QHBoxLayout;
    auto *reset = textButton(QCoreApplication::translate("h2d::AiSettingsPage", "恢复默认"), false, &dialog);
    auto *cancel = textButton(QCoreApplication::translate("h2d::AiSettingsPage", "取消"), false, &dialog);
    auto *save = textButton(QCoreApplication::translate("h2d::AiSettingsPage", "保存"), true, &dialog);
    save->setObjectName("aiPromptSave");
    actions->addWidget(reset); actions->addStretch(); actions->addWidget(cancel); actions->addWidget(save);
    body->addLayout(actions);
    QObject::connect(reset, &QPushButton::clicked, text, [text] { text->setPlainText(aiSystemPrompt()); });
    QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(save, &QPushButton::clicked, &dialog, &QDialog::accept);
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    const auto value = text->toPlainText();
    return value.trimmed().isEmpty() || value == aiSystemPrompt() ? QString() : value;
}
}
