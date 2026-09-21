#include "controller.h"
#include "agentprotocol.h"
#include <QFileInfo>
#include "autostart.h"
#include "settingsdialog.h"
#include "ui.h"
#include "updatechecker.h"
#include <QApplication>
#include <QClipboard>
#include <QMenu>
#include <QMessageBox>
#include <QScreen>
#include <QTimer>
namespace h2d {
Controller::Controller(QObject *parent, const AppSettings &settings, const QString &settingsFile)
    : QObject(parent), settings_(settings), settingsFile_(settingsFile), editor_(),
      tray_(QApplication::windowIcon().isNull() ? glyph("capture", accent()) : QApplication::windowIcon(), this),
      shortcut_(this) {
    connect(&editor_, &Editor::agentFinishRequested, this, &Controller::finishAgentSession);
    connect(&editor_, &Editor::agentCancelRequested, this, [this] {
        cancelAgentSession(agentSessionId_, "cancelled", "User cancelled. Edits remain in EditHere.");
    });
    editor_.setPreferences(settings_);
    editor_.setShortcuts(settings_.shortcuts);
    tray_.setObjectName("edithereTray");
    auto menu = new QMenu(&editor_);
    captureAction_ = menu->addAction("截图", this, &Controller::capture);
    captureAction_->setObjectName("trayCapture");
    menu->addAction("打开图片或项目", &editor_, [this] {
        editor_.openFile();
        if (guidePending_ && editor_.hasDocument()) showGuide();
    });
    menu->addAction("粘贴图片", &editor_, [this] {
        editor_.pasteImage();
        if (guidePending_ && editor_.hasDocument()) showGuide();
    });
    menu->addAction("恢复批注窗口", this, &Controller::activate);
#ifdef Q_OS_MAC
    menu->addAction("启用系统元素识别", this, [this] {
        if (requestAccessibility())
            tray_.showMessage("EditHere", "已启用系统元素识别");
        else
            tray_.showMessage("EditHere", "请在系统设置中授予辅助功能权限，图片识别仍可直接使用。");
    });
#endif
    menu->addSeparator();
    auto settingsAction = menu->addAction("设置…", this, [this] { openSettings(); });
    settingsAction->setObjectName("traySettings");
    auto updatesAction = menu->addAction("检查更新…", this, [this] { openSettings(true); });
    updatesAction->setObjectName("trayUpdates");
    menu->addSeparator();
    menu->addAction("退出", this, &Controller::quit);
    tray_.setContextMenu(menu);
    updateTrayShortcut();
    tray_.show();
    connect(&tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger)
            capture();
    });
    connect(&shortcut_, &GlobalShortcut::triggered, this, &Controller::capture);
    connect(&editor_, &Editor::captureRequested, this, &Controller::capture);
    connect(&editor_, &Editor::guideDismissed, this, [this] {
        guidePending_ = false;
        QString error;
        if (!hasSeenGuide(settingsFile_) && !markGuideSeen(&error, settingsFile_))
            tray_.showMessage("EditHere", "无法记录引导状态，下次启动时可能再次显示。\n" + error);
    });
    connect(&editor_, &Editor::toolbarSettingsRequested,this,[this] { openSettings(false,true); });
    connect(&editor_, &Editor::settingsRequested, this, [this] { openSettings(); });
    if (!shortcut_.start(settings_.shortcuts.value("capture")))
        tray_.showMessage("EditHere", "截图快捷键未能注册，请右键托盘打开设置修改。");
}
void Controller::updateTrayShortcut() {
    const auto label = settings_.shortcuts.value("capture").toString(QKeySequence::NativeText);
    captureAction_->setText(label.isEmpty() ? "截图" : "截图    " + label);
    tray_.setToolTip(label.isEmpty() ? "EditHere · 改这里" : "EditHere · 改这里 · " + label);
}
void Controller::openSettings(bool updates, bool toolbar) {
    if (capturing_ || QApplication::activeModalWidget())
        return;
    if (auto menu = tray_.contextMenu())
        menu->close();
    const auto activeShortcut = shortcut_.sequence();
    shortcut_.stop();
    auto draft = settings_;
    QString startupReadError;
    const bool registered = launchAtLoginEnabled(&startupReadError);
    if (startupReadError.isEmpty())
        draft.launchAtLogin = registered;
    SettingsDialog dialog(draft, &editor_);
    dialog.setLaunchAtLoginNotice(startupReadError.isEmpty() ? launchAtLoginNotice() : startupReadError);
    bool guideRequested = false;
    connect(&dialog, &SettingsDialog::guideRequested, &dialog, [&] { guideRequested = true; });
    if (updates)
        dialog.showUpdates(true);
    else if (toolbar)
        dialog.showToolbar();
    dialog.setApplyHandler([this](const AppSettings &next) -> QString {
        if (auto error = validateSettings(next); !error.isEmpty())
            return error;
        QString startupError;
        const bool wasRegistered = launchAtLoginEnabled(&startupError);
        if (!startupError.isEmpty())
            return startupError;
        const auto oldShortcut = shortcut_.sequence();
        if (!shortcut_.start(next.shortcuts.value("capture")))
            return shortcut_.lastError().isEmpty() ? "截图快捷键无法注册，请更换组合键。"
                                                   : shortcut_.lastError();
        QString error;
        const bool startupChanged = wasRegistered != next.launchAtLogin;
        if (!setLaunchAtLoginEnabled(next.launchAtLogin, &error)) {
            if (!shortcut_.start(oldShortcut))
                error += "\n原快捷键未能恢复，请重新设置截图快捷键。";
            return error;
        }
        if (!saveSettings(next, &error, settingsFile_)) {
            QString rollbackError;
            if (startupChanged && !setLaunchAtLoginEnabled(wasRegistered, &rollbackError))
                error += "\n开机自启未能恢复：" + rollbackError;
            if (!shortcut_.start(oldShortcut))
                error += "\n原快捷键未能恢复，请重新设置截图快捷键。";
            return error;
        }
        settings_ = next;
        editor_.setPreferences(settings_);
        editor_.setShortcuts(settings_.shortcuts);
        applyTheme(settings_.theme);
        updateTrayShortcut();
        return {};
    });
    const bool accepted = dialog.exec() == QDialog::Accepted;
    if (!accepted && !shortcut_.start(activeShortcut))
        tray_.showMessage("EditHere", "截图快捷键未能恢复，请在设置中更换组合键。");
    if (accepted) {
        const auto notice = launchAtLoginNotice();
        if (!notice.isEmpty()) tray_.showMessage("EditHere 开机自启", notice);
    }
    if (guideRequested)
        showGuide();
}
void Controller::showGuide() {
    if (capturing_ || QApplication::activeModalWidget())
        return;
    if (auto menu = tray_.contextMenu())
        menu->close();
    guidePending_ = false;
    editor_.showGuide();
}
void Controller::start(bool demo, const QString &path, bool background, bool firstUse) {
    guidePending_ = guidePending_ || firstUse;
    if (!agentSessionId_.isEmpty()) {
        activate();
        return;
    }
    if (!path.isEmpty())
        editor_.openFile(path);
    else if (demo)
        editor_.setDocument(fromImage(exampleImage(), "demo", "示例产品页面"));
    // A login launch stays in the tray, including before the first manual use.
    if (!background) {
        if (guidePending_)
            showGuide();
        else if (path.isEmpty() && !demo && settings_.captureOnStartup)
            capture();
    }
    if (!startupUpdateChecked_) {
        startupUpdateChecked_ = true;
        if (settings_.checkUpdatesOnStartup) {
            auto checker = new UpdateChecker(this);
            connect(checker, &UpdateChecker::finished, this,
                    [this, checker](UpdateChecker::Status status, const QString &message, const QUrl &) {
                        if (status == UpdateChecker::Available)
                            tray_.showMessage("EditHere 更新", message + "\n右键托盘选择检查更新。",
                                              QSystemTrayIcon::Information);
                        checker->deleteLater();
                    });
            QTimer::singleShot(3000, checker, &UpdateChecker::check);
        }
    }
}
void Controller::activate() {
    if (guidePending_) {
        showGuide();
        return;
    }
    if (editor_.hasDocument()) {
        if (editor_.isMinimized())
            editor_.setWindowState(editor_.windowState() & ~Qt::WindowMinimized);
        editor_.show();
        editor_.raise();
        editor_.activateWindow();
    } else
        capture();
}
void Controller::capture() {
    if (!agentSessionId_.isEmpty()) { activate(); return; }
    if (capturing_ || QApplication::activeModalWidget())
        return;
    if (guidePending_) {
        showGuide();
        return;
    }
    editor_.dismissGuide();
    if (auto menu = tray_.contextMenu())
        menu->close();
    if (!editor_.allowReplace())
        return;
    capturing_ = true;
    wasVisible_ = editor_.isVisible();
    editor_.hide();
    prepareScreenCapture(this, [this] {
        if (!capturing_)
            return;
        QPointer<Controller> self(this);
        captureScreens([self](QVector<ScreenFrame> frames, QString error) {
            if (!self)
                return;
            auto owner = self.data();
            if (frames.isEmpty()) {
                owner->capturing_ = false;
                if (owner->wasVisible_)
                    owner->activate();
                QMessageBox::warning(&owner->editor_, "截图未完成", error);
                return;
            }
            for (auto &frame : frames) {
                auto overlay = new Overlay(std::move(frame));
                owner->overlays_.append(overlay);
                connect(overlay, &Overlay::cancelled, owner, &Controller::cancelCapture);
                connect(overlay, &Overlay::selectionBegan, owner, [owner, overlay] {
                    for (auto other : owner->overlays_)
                        if (other != overlay)
                            other->resetSelection();
                });
                connect(overlay, &Overlay::accepted, owner,
                        [owner, overlay](QRect area, QVector<Candidate> candidates) {
                            owner->completeCapture(overlay, area, std::move(candidates));
                        });
                connect(overlay, &Overlay::copyRequested, owner, [owner, overlay](QRect area) {
                    QApplication::clipboard()->setImage(overlay->frame().image.copy(area));
                    owner->clearOverlays();
                    if (owner->wasVisible_)
                        owner->activate();
                });
                overlay->show();
                configureNativeWindow(overlay, true);
            }
            for (auto overlay : owner->overlays_)
                if (overlay->frame().logicalGeometry.contains(QCursor::pos())) {
                    overlay->raise();
                    overlay->activateWindow();
                    overlay->setFocus();
                    break;
                }
        });
    });
}
void Controller::clearOverlays() {
    for (auto overlay : overlays_) {
        overlay->hide();
        overlay->deleteLater();
    }
    overlays_.clear();
    capturing_ = false;
}
void Controller::cancelCapture() {
    clearOverlays();
    if (wasVisible_)
        activate();
}
void Controller::completeCapture(Overlay *source, QRect area, QVector<Candidate> candidates) {
    try {
        const ScreenFrame frame = source->frame();
        auto doc = fromImage(frame.image.copy(area), "screen", "屏幕截图");
        if (frame.nativePixels && !frame.nativeGeometry.isEmpty())
            doc.screenBounds = area.translated(frame.nativeGeometry.topLeft());
        for (auto c : candidates) {
            QRect intersection = c.bounds.intersected(area);
            if (intersection.isEmpty() || intersection == area)
                continue;
            c.target["clipped"] = c.target["clipped"].toBool() || intersection != c.bounds;
            c.bounds = intersection.translated(-area.topLeft());
            doc.candidates.append(c);
        }
        clearOverlays();
        editor_.setDocument(std::move(doc));
    } catch (const std::exception &e) {
        clearOverlays();
        if (wasVisible_)
            activate();
        QMessageBox::warning(&editor_, "截图未完成", QString::fromUtf8(e.what()));
    }
}
void Controller::quit() {
    if (!agentSessionId_.isEmpty())
        cancelAgentSession(agentSessionId_, "cancelled", "EditHere is exiting. No feedback was returned.");
    if (capturing_)
        cancelCapture();
    if (!editor_.allowReplace())
        return;
    tray_.hide();
    qApp->quit();
}
QJsonObject Controller::handleAgentRequest(const QJsonObject &request) {
    const auto command = request["command"].toString();
    if (command == "status") {
        QString startupError;
        const bool startupRegistered = launchAtLoginEnabled(&startupError);
        return {{"ok", true}, {"running", true}, {"version", EDITHERE_VERSION},
                {"executable", QCoreApplication::applicationFilePath()},
                {"startupRegistered", startupRegistered}, {"startupNotice", startupError.isEmpty() ? launchAtLoginNotice() : startupError},
                {"hasDocument", editor_.hasDocument()}, {"dirty", editor_.document().dirty},
                {"capturing", capturing_}, {"agentSession", !agentSessionId_.isEmpty()}};
    }
    if (command != "open" && command != "capture" && command != "annotate")
        return agentError("invalid_arguments", "Unsupported command.");
    if (!agentSessionId_.isEmpty() || capturing_ || QApplication::activeModalWidget())
        return agentError("busy", "EditHere is busy with another request, capture or dialog.");
    if (editor_.hasUnsavedChanges())
        return agentError("busy", "The current document has unsaved changes. Save or close it in EditHere, then retry.");
    if (command == "capture") {
        guidePending_ = false;
        capture();
        return {{"ok", true}, {"command", command}, {"accepted", true}};
    }
    const auto input = request["input"].toString();
    if (!QFileInfo(input).isAbsolute()) return agentError("invalid_arguments", "Input path must be absolute.");
    QString output;
    int timeout = 1800;
    if (command == "annotate") {
        output = request["output"].toString();
        const auto error = validateNewFeedbackPath(output);
        if (!error.isEmpty()) return agentError("io_error", error);
        timeout = request["timeout"].toInt(1800);
        if (timeout < 1 || timeout > 86400) return agentError("invalid_arguments", "Timeout must be between 1 and 86400 seconds.");
    }
    try {
        auto doc = loadDocument(input);
        guidePending_ = false;
        editor_.setDocument(std::move(doc), input.endsWith(".edithere", Qt::CaseInsensitive) ? input : QString());
    } catch (const std::exception &error) { return agentError("io_error", QString::fromUtf8(error.what())); }
    if (command == "open") return {{"ok", true}, {"command", command}, {"accepted", true}, {"input", input}};
    agentSessionId_ = uniqueId();
    agentOutput_ = output;
    agentEmbed_ = request["embed"].toBool(true);
    editor_.setAgentSession(true);
    const auto id = agentSessionId_;
    QTimer::singleShot(timeout * 1000, this, [this, id] {
        cancelAgentSession(id, "timeout", "Annotation timed out. Your edits remain in EditHere.");
    });
    return {{"ok", true}, {"pending", true}, {"session", id}};
}
void Controller::cancelAgentSession(const QString &id, const QString &code, const QString &message) {
    if (id.isEmpty() || id != agentSessionId_) return;
    const auto finishedId = agentSessionId_;
    agentSessionId_.clear();
    agentOutput_.clear();
    editor_.setAgentSession(false);
    emit agentSessionFinished(finishedId, agentError(code, message));
}
void Controller::finishAgentSession() {
    if (agentSessionId_.isEmpty()) return;
    const auto id = agentSessionId_;
    const auto output = agentOutput_;
    try {
        writeNewFeedback(output, editor_.agentFeedback(agentEmbed_));
    } catch (const std::exception &error) {
        cancelAgentSession(id, "io_error", QString::fromUtf8(error.what()));
        return;
    }
    const QJsonObject result{{"ok", true}, {"command", "annotate"}, {"output", output},
                            {"annotations", editor_.document().notes.size()}, {"imageIncluded", agentEmbed_}};
    agentSessionId_.clear();
    agentOutput_.clear();
    editor_.setAgentSession(false);
    emit agentSessionFinished(id, result);
}
} // namespace h2d
