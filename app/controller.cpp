#include "controller.h"
#include "agentprotocol.h"
#include "capturetoolbar.h"
#include "i18n.h"
#include "ocrdialog.h"
#include "pinwindow.h"
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QStandardPaths>
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
#include <algorithm>
namespace h2d {
namespace {
// How many earlier captures the menu beside a finished region offers. More than a
// handful would be a list nobody reads, and the pictures live in a cache that is
// trimmed to twenty anyway.
constexpr int kHistoryMenuEntries = 5;
} // namespace
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
    captureAction_ = menu->addAction(QString(), this, &Controller::capture);
    captureAction_->setObjectName("trayCapture");
    openAction_ = menu->addAction(QString(), &editor_, [this] {
        editor_.openFile();
        if (guidePending_ && editor_.hasDocument()) showGuide();
    });
    pasteAction_ = menu->addAction(QString(), &editor_, [this] {
        editor_.pasteImage();
        if (guidePending_ && editor_.hasDocument()) showGuide();
    });
    restoreAction_ = menu->addAction(QString(), this, &Controller::activate);
#ifdef Q_OS_MAC
    accessibilityAction_ = menu->addAction(QString(), this, [this] {
        if (requestAccessibility())
            tray_.showMessage("EditHere", tr("已启用系统元素识别"));
        else
            tray_.showMessage("EditHere", tr("请在系统设置中授予辅助功能权限，图片识别仍可直接使用。"));
    });
#endif
    menu->addSeparator();
    settingsAction_ = menu->addAction(QString(), this, [this] { openSettings(); });
    settingsAction_->setObjectName("traySettings");
    updatesAction_ = menu->addAction(QString(), this, [this] { openSettings(true); });
    updatesAction_->setObjectName("trayUpdates");
    menu->addSeparator();
    quitAction_ = menu->addAction(QString(), this, &Controller::quit);
    tray_.setContextMenu(menu);
    retranslate();
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
            tray_.showMessage("EditHere", tr("无法记录引导状态，下次启动时可能再次显示。\n") + error);
    });
    connect(&editor_, &Editor::toolbarSettingsRequested,this,[this] { openSettings(false,true); });
    connect(&editor_, &Editor::settingsRequested, this, [this] { openSettings(); });
    if (!shortcut_.start(settings_.shortcuts.value("capture")))
        tray_.showMessage("EditHere", tr("截图快捷键未能注册，请右键托盘打开设置修改。"));
}
void Controller::retranslate() {
    if (openAction_) openAction_->setText(tr("打开图片或项目"));
    if (pasteAction_) pasteAction_->setText(tr("粘贴图片"));
    if (restoreAction_) restoreAction_->setText(tr("恢复批注窗口"));
    if (accessibilityAction_) accessibilityAction_->setText(tr("启用系统元素识别"));
    if (settingsAction_) settingsAction_->setText(tr("设置…"));
    if (updatesAction_) updatesAction_->setText(tr("检查更新…"));
    if (quitAction_) quitAction_->setText(tr("退出"));
    updateTrayShortcut();
}
void Controller::updateTrayShortcut() {
    const auto label = settings_.shortcuts.value("capture").toString(QKeySequence::NativeText);
    captureAction_->setText(label.isEmpty() ? tr("截图") : tr("截图") + "    " + label);
    const auto brand = tr("EditHere · 改这里");
    tray_.setToolTip(label.isEmpty() ? brand : brand + " · " + label);
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
    // A language switch inside the dialog is applied live; the tray menu is not a
    // top-level widget, so it has to rebuild its own labels here.
    connect(&dialog, &SettingsDialog::languageApplied, this, &Controller::retranslate);
    bool guideRequested = false;
    connect(&dialog, &SettingsDialog::guideRequested, &dialog, [&] { guideRequested = true; });
    if (updates)
        dialog.showUpdates(true);
    else if (toolbar)
        dialog.showToolbar();
    dialog.setApplyHandler([this](const AppSettings &next) -> QString {
        if (auto error = validateSettings(next); !error.isEmpty())
            return error;
        const auto previousLanguage = settings_.language;
        if (!installLanguage(next.language))
            return tr("界面语言加载失败，请重新安装 EditHere。");
        QString startupError;
        const bool wasRegistered = launchAtLoginEnabled(&startupError);
        if (!startupError.isEmpty())
            return startupError;
        const auto oldShortcut = shortcut_.sequence();
        if (!shortcut_.start(next.shortcuts.value("capture")))
            return shortcut_.lastError().isEmpty() ? tr("截图快捷键无法注册，请更换组合键。")
                                                   : shortcut_.lastError();
        QString error;
        const bool startupChanged = wasRegistered != next.launchAtLogin;
        if (!setLaunchAtLoginEnabled(next.launchAtLogin, &error)) {
            if (!shortcut_.start(oldShortcut))
                error += tr("\n原快捷键未能恢复，请重新设置截图快捷键。");
            return error;
        }
        if (!saveSettings(next, &error, settingsFile_)) {
            // Nothing was written, so the live language preview has to go back too.
            if (next.language != previousLanguage)
                installLanguage(previousLanguage);
            QString rollbackError;
            if (startupChanged && !setLaunchAtLoginEnabled(wasRegistered, &rollbackError))
                error += tr("\n开机自启未能恢复：") + rollbackError;
            if (!shortcut_.start(oldShortcut))
                error += tr("\n原快捷键未能恢复，请重新设置截图快捷键。");
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
        tray_.showMessage("EditHere", tr("截图快捷键未能恢复，请在设置中更换组合键。"));
    if (accepted) {
        const auto notice = launchAtLoginNotice();
        if (!notice.isEmpty()) tray_.showMessage(tr("EditHere 开机自启"), notice);
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
        editor_.setDocument(fromImage(exampleImage(), "demo", tr("示例产品页面")));
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
                            tray_.showMessage(tr("EditHere 更新"), message + tr("\n右键托盘选择检查更新。"),
                                              QSystemTrayIcon::Information);
                        checker->deleteLater();
                    });
            QTimer::singleShot(3000, checker, &UpdateChecker::check);
        }
    }
}
void Controller::raiseEditor() {
    if (!editor_.hasDocument())
        return;
    if (editor_.isMinimized())
        editor_.setWindowState(editor_.windowState() & ~Qt::WindowMinimized);
    editor_.show();
    editor_.raise();
    editor_.activateWindow();
}
void Controller::activate() {
    if (guidePending_) {
        showGuide();
        return;
    }
    if (editor_.hasDocument())
        raiseEditor();
    else
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
    // A new capture always starts on the screen, not on whatever was being browsed
    // before it.
    historyIndex_ = -1;
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
                QMessageBox::warning(&owner->editor_, tr("截图未完成"), error);
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
                connect(overlay, &Overlay::copyRequested, owner,
                        [owner, overlay](QRect) { owner->copyRegion(overlay); });
                connect(overlay, &Overlay::pinRequested, owner,
                        [owner, overlay](QRect) { owner->pinRegion(overlay); });
                connect(overlay, &Overlay::saveRequested, owner,
                        [owner, overlay](QRect) { owner->saveRegion(overlay); });
                connect(overlay, &Overlay::ocrRequested, owner,
                        [owner, overlay](QRect, OcrLanguageMode language) {
                            owner->recognizeRegion(overlay, language);
                        });
                connect(overlay, &Overlay::historyRequested, owner,
                        [owner](int index) { owner->openHistory(index); });
                connect(overlay, &Overlay::historyStepRequested, owner,
                        [owner](int delta) { owner->stepHistory(delta); });
                connect(overlay, &Overlay::styleRememberRequested, owner,
                        [owner](const CaptureStyle &style) { owner->rememberCaptureStyle(style); });
                owner->applyCaptureHistory(overlay);
                // The style and the recogniser language the user settled on last time
                // are what this capture starts from; that is what remembering them is
                // for.
                overlay->setStyle(owner->settings_.captureStyle);
                overlay->setOcrLanguage(owner->settings_.ocrLanguage);
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
void Controller::remember(const QImage &image, const QRect &selection) {
    // The recent captures and the recent selections are what the history is for, and
    // neither is worth keeping if it could not be written.
    history_.add(image);
    history_.rememberSelection(selection);
}
void Controller::applyCaptureHistory(Overlay *overlay) {
    // The labels are built when a capture starts rather than kept around, so a picture
    // that has since left the cache is shown as missing instead of being offered as if
    // it were still there.
    QStringList labels;
    const int entries = std::min(history_.count(), kHistoryMenuEntries);
    for (int index = 0; index < entries; ++index) {
        const QImage picture = history_.at(index);
        labels.append(picture.isNull()
                          ? tr("第 %1 张 · 已不在缓存里").arg(index + 1)
                          : tr("第 %1 张 · %2 × %3").arg(index + 1).arg(picture.width()).arg(
                                picture.height()));
    }
    overlay->setHistory(labels, historyIndex_);
    overlay->setSelections(history_.selections());
}

void Controller::stepHistory(int delta) {
    const int entries = std::min(history_.count(), kHistoryMenuEntries);
    if (entries == 0 || overlays_.isEmpty() || delta == 0)
        return;
    // Index -1 is the screen; the captures run backwards from it, newest first.
    int next = delta < 0 ? historyIndex_ + 1 : historyIndex_ - 1;
    if (next >= entries || (next < 0 && delta > 0 && historyIndex_ < 0))
        return;
    QImage picture;
    if (next >= 0) {
        picture = history_.at(next);
        if (picture.isNull()) {
            // A capture that has since left the cache is skipped rather than shown as
            // an empty window.
            tray_.showMessage(QStringLiteral("EditHere"), tr("这张截图已经不在缓存里了。"));
            return;
        }
    }
    historyIndex_ = next;
    for (auto overlay : overlays_) {
        overlay->showHistoryPicture(picture);
        overlay->setHistoryIndex(next);
    }
}

void Controller::rememberCaptureStyle(const CaptureStyle &style) {
    settings_.captureStyle = style;
    QString error;
    if (!saveSettings(settings_, &error, settingsFile_)) {
        tray_.showMessage(QStringLiteral("EditHere"), error);
        return;
    }
    tray_.showMessage(QStringLiteral("EditHere"), tr("已记住这个样式，以后每次截图都从这里开始。"));
}
void Controller::openHistory(int index) {
    const QImage picture = history_.at(index);
    if (picture.isNull()) {
        tray_.showMessage(QStringLiteral("EditHere"), tr("这张截图已经不在缓存里了。"));
        return;
    }
    clearOverlays();
    try {
        editor_.setDocument(fromImage(picture, "history", tr("历史截图")));
    } catch (const std::exception &error) {
        if (wasVisible_)
            activate();
        QMessageBox::warning(&editor_, tr("无法打开历史截图"), QString::fromUtf8(error.what()));
    }
}
void Controller::uncoverForDialog() {
    // A file dialog is a window of its own, and the capture window covers every screen
    // it was taken over, so it steps aside for as long as the dialog is up.
    for (auto overlay : overlays_)
        overlay->hide();
}
void Controller::recoverAfterDialog() {
    for (auto overlay : overlays_) {
        overlay->show();
        configureNativeWindow(overlay, true);
    }
}
void Controller::copyRegion(Overlay *source) {
    const QImage picture = source->selectionImage();
    if (!picture.isNull()) {
        QApplication::clipboard()->setImage(picture);
        remember(picture, source->selection());
    }
    clearOverlays();
    if (wasVisible_)
        activate();
}
void Controller::pinRegion(Overlay *source) {
    const QImage bare = source->selectionPixels();
    const CaptureStyle style = source->style();
    const QImage picture = composeCapture(bare, style);
    if (!picture.isNull()) {
        remember(picture, source->selection());
        // A region taken straight off the screen goes back on top of the very thing it
        // shows; that is what makes a pin read as "the same thing, held still" rather
        // than as another window that happens to be in the way.
        auto *pinned = pinImage(picture, source->placementFor(picture.size()));
        if (pinned != nullptr) {
            // The pin shows the halo; the editor gets the screenshot inside it. Without
            // this the canvas would grow by the shadow on every side the moment
            // annotating became the default thing to do with a capture.
            pinned->setDecoration(style.shadowRadius());
            // Kept as well, so the shadow can be switched off again from the pin's own
            // menu instead of being baked into the pixels for good.
            pinned->setUndecorated(bare, style);
        }
    }
    clearOverlays();
    if (wasVisible_)
        activate();
}
void Controller::saveRegion(Overlay *source) {
    const QImage picture = source->selectionImage();
    if (picture.isNull())
        return;
    uncoverForDialog();
    if (saveImage(picture)) {
        // The region was kept, so it is worth keeping it in the history too.
        remember(picture, source->selection());
        clearOverlays();
        if (wasVisible_)
            activate();
        return;
    }
    // Nothing was written, which means the user changed their mind: the region comes
    // back rather than being thrown away.
    recoverAfterDialog();
}
void Controller::recognizeRegion(Overlay *source, OcrLanguageMode language) {
    // The bare region, not the decorated picture: a shadow is drawn for the eye, and a
    // halo around the words would both be misread and shift every line box away from
    // the text it points at.
    const QImage picture = source->selectionPixels();
    if (picture.isNull())
        return;
    remember(picture, source->selection());
    clearOverlays();
    if (wasVisible_)
        activate();
    recognize(picture, language);
}
PinWindow *Controller::pinImage(const QImage &image, QRect placement) {
    if (image.isNull())
        return nullptr;
    auto *window = new PinWindow(image, placement);
    pins_.append(window);
    // A pin owns itself: the window has no parent, so the only thing that ever ends it
    // is the list dropping it.
    connect(window, &PinWindow::closed, this, [this](PinWindow *which) {
        pins_.removeAll(which);
        which->deleteLater();
    });
    connect(window, &PinWindow::copyRequested, this, [this](const QImage &picture) {
        QApplication::clipboard()->setImage(picture);
    });
    connect(window, &PinWindow::saveRequested, this, [this](const QImage &picture) { saveImage(picture); });
    connect(window, &PinWindow::ocrRequested, this,
            [this](const QImage &picture) { recognize(picture, settings_.ocrLanguage); });
    connect(window, &PinWindow::annotateRequested, this, [this](const QImage &picture) {
        // Pinning a picture is a way to park it; annotating it is why it was taken. The
        // pin stays where it is, so the picture can be looked at while it is marked up.
        try {
            editor_.setDocument(fromImage(picture, "pin", tr("置顶图片")));
            raiseEditor();
        } catch (const std::exception &error) {
            QMessageBox::warning(&editor_, tr("无法打开这张图片"),
                                 QString::fromUtf8(error.what()));
        }
    });
    window->show();
    return window;
}
bool Controller::saveImage(const QImage &image) {
    if (image.isNull())
        return false;
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString suggested =
        QDir(folder).filePath(QStringLiteral("EditHere-%1.png")
                                  .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
    const QString path = QFileDialog::getSaveFileName(
        &editor_, tr("保存图片"), suggested,
        tr("PNG 图片 (*.png);;JPEG 图片 (*.jpg);;所有文件 (*)"));
    if (path.isEmpty())
        return false;
    if (image.save(path)) {
        tray_.showMessage("EditHere", tr("已保存 %1").arg(QFileInfo(path).fileName()));
        return true;
    }
    QMessageBox::warning(&editor_, tr("保存失败"), tr("无法写入 %1，请检查目录是否存在以及是否可写。").arg(path));
    return false;
}
void Controller::recognize(const QImage &image, OcrLanguageMode language) {
    if (image.isNull())
        return;
    ocrPicture_ = image;
    // The choice is remembered as it is made, so the next capture starts from the
    // language that was actually wanted rather than from the default.
    if (settings_.ocrLanguage != language) {
        settings_.ocrLanguage = language;
        QString error;
        saveSettings(settings_, &error, settingsFile_);
    }
    if (ocrDialog_.isNull()) {
        auto *dialog = new OcrDialog(image, language, &editor_);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        ocrDialog_ = dialog;
        connect(dialog, &QObject::destroyed, this, [this] {
            // Closing the result window gives up on a run that is still going.
            if (ocr_ != nullptr)
                ocr_->cancel();
        });
        connect(dialog, &OcrDialog::retryRequested, this, [this](OcrLanguageMode next) {
            // The choice is remembered, so the next recognition starts from it.
            settings_.ocrLanguage = next;
            QString error;
            saveSettings(settings_, &error, settingsFile_);
            runRecognition(next);
        });
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
    } else {
        ocrDialog_->setBusy(true);
    }
    runRecognition(language);
}
void Controller::runRecognition(OcrLanguageMode language) {
    if (ocr_ == nullptr)
        ocr_ = new OcrEngine(this);
    QPointer<Controller> self(this);
    // The engine answers on this thread, but not before the function returns, so the
    // window can be closed in the meantime and the guard is what notices.
    ocr_->recognize(ocrPicture_, language, [self](OcrResult result) {
        if (!self || self->ocrDialog_.isNull())
            return;
        self->ocrDialog_->setResult(result);
    });
}

void Controller::completeCapture(Overlay *source, QRect area, QVector<Candidate> candidates) {
    try {
        const ScreenFrame frame = source->frame();
        auto doc = fromImage(frame.image.copy(area), "screen", tr("屏幕截图"));
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
        QMessageBox::warning(&editor_, tr("截图未完成"), QString::fromUtf8(e.what()));
    }
}
void Controller::quit() {
    const bool pendingSession = !agentSessionId_.isEmpty();
    if (pendingSession)
        cancelAgentSession(agentSessionId_, "cancelled", "EditHere is exiting. No feedback was returned.");
    if (capturing_)
        cancelCapture();
    // The update flow and installers ask a tray-resident instance to exit, so the
    // save/discard/cancel prompt has to come to the front instead of hiding there.
    if ((pendingSession || editor_.hasUnsavedChanges()) && !editor_.isVisible())
        raiseEditor();
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
