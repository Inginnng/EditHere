#include "controller.h"
#include "diagnostics.h"
#include "agentprotocol.h"
#include "capturetoolbar.h"
#include "i18n.h"
#include "ocrdialog.h"
#include "pinwindow.h"
#include "scrollcapture.h"
#include "scrollstitch.h"
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QSettings>
#include <QStandardPaths>
#include "autostart.h"
#include "settingsdialog.h"
#include "ui.h"
#include "updatechecker.h"
#include <QApplication>
#include <QClipboard>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <algorithm>
namespace h2d {
namespace {
// How many earlier captures the menu beside a finished region offers. More than a
// handful would be a list nobody reads, and the pictures live in a cache that is
// trimmed to twenty anyway.
constexpr int kHistoryMenuEntries = 5;
constexpr double kScrollStableDifference = 0.45;
// Manual scrolling is sampled often enough that consecutive samples overlap even
// while the user keeps the wheel turning; waiting for a still page alone loses it.
constexpr int kScrollManualSampleMs = 60;
} // namespace
Controller::Controller(QObject *parent, const AppSettings &settings, const QString &settingsFile)
    : QObject(parent), settings_(settings), settingsFile_(settingsFile), editor_(),
      tray_(QApplication::windowIcon().isNull() ? glyph("capture", accent()) : QApplication::windowIcon(), this),
      shortcut_(this), annotateShortcut_(this) {
    shortcut_.setObjectName("captureShortcut");
    annotateShortcut_.setObjectName("annotateShortcut");
    connect(&editor_, &Editor::agentFinishRequested, this, &Controller::finishAgentSession);
    connect(&editor_, &Editor::agentCancelRequested, this, [this] {
        cancelAgentSession(agentSessionId_, "cancelled", "User cancelled. Edits remain in EditHere.");
    });
    editor_.setPreferences(settings_);
    editor_.setShortcuts(settings_.shortcuts);
    tray_.setObjectName("edithereTray");
    auto menu = new QMenu(&editor_);
    captureAction_ = menu->addAction(QString(), this, [this] { beginCapture(true); });
    captureAction_->setObjectName("trayCapture");
    annotateAction_ = menu->addAction(QString(), this, &Controller::openAnnotation);
    annotateAction_->setObjectName("trayAnnotate");
    openAction_ = menu->addAction(QString(), &editor_, [this] {
        scheduleStartupUpdate();
        editor_.openFile();
        if (guidePending_ && editor_.hasDocument()) showGuide();
    });
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
#ifdef Q_OS_LINUX
    connect(&editor_, &Editor::hiddenToTray, this, [] {
        if (QGuiApplication::platformName() != "offscreen" && !QSystemTrayIcon::isSystemTrayAvailable())
            QCoreApplication::quit();
    });
#endif
    connect(&tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger)
            beginCapture(true);
    });
    connect(&shortcut_, &GlobalShortcut::triggered, this, &Controller::capture);
    connect(&annotateShortcut_, &GlobalShortcut::triggered, this, &Controller::openAnnotation);
    connect(&editor_, &Editor::captureRequested, this, &Controller::capture);
    connect(&editor_, &Editor::guideDismissed, this, [this] {
        guidePending_ = false;
        QString error;
        if (!hasSeenGuide(settingsFile_) && !markGuideSeen(&error, settingsFile_)) {
            diagnostics::write(diagnostics::Level::Error, "settings.guide", error);
            tray_.showMessage("EditHere", tr("无法记录引导状态，下次启动时可能再次显示。\n") + error);
        }
    });
    connect(&editor_, &Editor::toolbarSettingsRequested,this,[this] { openSettings(false,true); });
    connect(&editor_, &Editor::settingsRequested, this, [this] { openSettings(); });
    connect(&editor_, &Editor::preferencesChanged, this, [this] {
        // The editor turned a preference off by itself — the "don't ask again" box on
        // the close question does — and an answer the user gave has to outlive the
        // window it was given in.
        settings_.confirmBeforeDiscard = false;
        QString error;
        if (!saveSettings(settings_, &error, settingsFile_)) {
            diagnostics::write(diagnostics::Level::Error, "settings.persist", error);
            tray_.showMessage("EditHere", tr("设置未能保存：") + error);
        }
    });
    if (!shortcut_.start(settings_.shortcuts.value("capture"))) {
        diagnostics::write(diagnostics::Level::Warning, "shortcuts.register", shortcut_.lastError());
        tray_.showMessage("EditHere", tr("截图快捷键未能注册，请右键托盘打开设置修改。"));
    }
    if (!annotateShortcut_.start(settings_.shortcuts.value("annotate"))) {
        diagnostics::write(diagnostics::Level::Warning, "shortcuts.annotate.register", annotateShortcut_.lastError());
        tray_.showMessage("EditHere", tr("新建批注快捷键未能注册，请右键托盘打开设置修改。"));
    }
}
void Controller::retranslate() {
    shortcut_.setAction("capture", tr("截图"));
    annotateShortcut_.setAction("annotate", tr("新建批注（空窗口）"));
    if (openAction_) openAction_->setText(tr("打开图片或项目"));
    if (accessibilityAction_) accessibilityAction_->setText(tr("启用系统元素识别"));
    if (settingsAction_) settingsAction_->setText(tr("设置…"));
    if (updatesAction_) updatesAction_->setText(tr("检查更新…"));
    if (quitAction_) quitAction_->setText(tr("退出"));
    updateTrayShortcut();
}
void Controller::updateTrayShortcut() {
    const auto label = settings_.shortcuts.value("capture").toString(QKeySequence::NativeText);
    captureAction_->setText(label.isEmpty() ? tr("截图") : tr("截图") + "    " + label);
    const auto annotateLabel = settings_.shortcuts.value("annotate").toString(QKeySequence::NativeText);
    annotateAction_->setText(annotateLabel.isEmpty() ? tr("新建批注（空窗口）")
                                                  : tr("新建批注（空窗口）") + "    " + annotateLabel);
    const auto brand = tr("EditHere · 改这里");
    tray_.setToolTip(label.isEmpty() ? brand : brand + " · " + label);
}
void Controller::openAnnotation() {
    if (capturing_ || QApplication::activeModalWidget())
        return;
    scheduleStartupUpdate();
    if (auto menu = tray_.contextMenu())
        menu->close();
    editor_.openEmpty();
}
void Controller::openSettings(bool updates, bool toolbar) {
    openSettingsDialog(updates, toolbar);
}
void Controller::openSettingsDialog(bool updates, bool toolbar, const UpdateChecker::Result *release) {
    if (capturing_ || QApplication::activeModalWidget())
        return;
    scheduleStartupUpdate();
    if (auto menu = tray_.contextMenu())
        menu->close();
    const auto activeShortcut = shortcut_.sequence();
    const auto activeAnnotateShortcut = annotateShortcut_.sequence();
    shortcut_.stop();
    annotateShortcut_.stop();
    auto draft = settings_;
    QString startupReadError;
    const bool registered = launchAtLoginEnabled(&startupReadError);
    if (startupReadError.isEmpty())
        draft.launchAtLogin = registered;
    SettingsDialog dialog(draft, &editor_);
    dialog.setUpdatePreparationHandler([this] {
        // Never discard an active agent session to make room for an update.
        // allowReplace also commits inline edits and offers Save/Discard/Cancel.
        return editor_.allowReplace();
    });
    bool updateStarted = false;
    connect(&dialog, &SettingsDialog::updateInstallStarted, &dialog, [&] { updateStarted = true; });
    dialog.setLaunchAtLoginNotice(startupReadError.isEmpty() ? launchAtLoginNotice() : startupReadError);
    // A language switch inside the dialog is applied live; the tray menu is not a
    // top-level widget, so it has to rebuild its own labels here.
    connect(&dialog, &SettingsDialog::languageApplied, this, &Controller::retranslate);
    bool guideRequested = false;
    connect(&dialog, &SettingsDialog::guideRequested, &dialog, [&] { guideRequested = true; });
    if (release)
        dialog.offerUpdate(*release, true);
    else if (updates)
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
        const auto oldAnnotateShortcut = annotateShortcut_.sequence();
        auto restoreShortcuts = [&] {
            // Release both first so restoring swapped assignments cannot conflict.
            shortcut_.stop();
            annotateShortcut_.stop();
            QString error;
            if (!shortcut_.start(oldShortcut))
                error += tr("\n原快捷键未能恢复，请重新设置截图快捷键。");
            if (!annotateShortcut_.start(oldAnnotateShortcut))
                error += tr("\n原快捷键未能恢复，请重新设置新建批注快捷键。");
            return error;
        };
        if (!shortcut_.start(next.shortcuts.value("capture"))) {
            const auto error = shortcut_.lastError().isEmpty() ? tr("截图快捷键无法注册，请更换组合键。")
                                                              : shortcut_.lastError();
            return error + restoreShortcuts();
        }
        if (!annotateShortcut_.start(next.shortcuts.value("annotate"))) {
            const auto error = annotateShortcut_.lastError().isEmpty()
                                   ? tr("新建批注快捷键无法注册，请更换组合键。")
                                   : tr("新建批注（空窗口）：") + annotateShortcut_.lastError();
            return error + restoreShortcuts();
        }
        QString error;
        const bool startupChanged = wasRegistered != next.launchAtLogin;
        if (!setLaunchAtLoginEnabled(next.launchAtLogin, &error)) {
            return error + restoreShortcuts();
        }
        if (!saveSettings(next, &error, settingsFile_)) {
            // Nothing was written, so the live language preview has to go back too.
            if (next.language != previousLanguage)
                installLanguage(previousLanguage);
            QString rollbackError;
            if (startupChanged && !setLaunchAtLoginEnabled(wasRegistered, &rollbackError))
                error += tr("\n开机自启未能恢复：") + rollbackError;
            return error + restoreShortcuts();
        }
        settings_ = next;
        editor_.setPreferences(settings_);
        editor_.setShortcuts(settings_.shortcuts);
        applyTheme(settings_.theme);
        updateTrayShortcut();
        return {};
    });
    const bool accepted = dialog.exec() == QDialog::Accepted;
    if (updateStarted) {
        tray_.hide();
        // The user has confirmed, and the maintenance worker has validated and
        // prepared the update. Its ACK allows it to wait for this process to exit.
        // Do not run the close-to-tray path or ask the same question a second time.
        QCoreApplication::exit(0);
        return;
    }
    if (!accepted) {
        if (!shortcut_.start(activeShortcut))
            tray_.showMessage("EditHere", tr("截图快捷键未能恢复，请在设置中更换组合键。"));
        if (!annotateShortcut_.start(activeAnnotateShortcut))
            tray_.showMessage("EditHere", tr("新建批注快捷键未能恢复，请在设置中更换组合键。"));
    }
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
    if (!background)
        scheduleStartupUpdate();
#ifdef Q_OS_LINUX
    // GNOME may have no tray host. Keep a visible entry point in that case.
    if (QGuiApplication::platformName() != "offscreen" && !QSystemTrayIcon::isSystemTrayAvailable() && !background)
        editor_.show();
#endif
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
}
void Controller::scheduleStartupUpdate() {
    if (!agentSessionId_.isEmpty())
        return;
    if (!installationReceiptChecked_) {
        installationReceiptChecked_ = true;
        QTimer::singleShot(3000, this, &Controller::readInstallationReceipt);
    }
    if (startupUpdateChecked_ || !settings_.checkUpdatesOnStartup)
        return;
    startupUpdateChecked_ = true;
    auto checker = new UpdateChecker(this);
    checker->setObjectName("startupUpdateChecker");
    connect(checker, &UpdateChecker::finished, this, [this, checker] {
        startupUpdateFinished(checker->lastResult());
        checker->deleteLater();
    });
    QTimer::singleShot(3000, checker, &UpdateChecker::check);
}
void Controller::startupUpdateFinished(const UpdateChecker::Result &result) {
    if (result.status != UpdateChecker::Available || !settings_.checkUpdatesOnStartup)
        return;
    pendingStartupUpdate_ = result;
    presentStartupUpdate();
}
void Controller::presentStartupUpdate() {
    if (!pendingStartupUpdate_)
        return;
    if (!settings_.checkUpdatesOnStartup) {
        pendingStartupUpdate_.reset();
        return;
    }
    if (capturing_ || !agentSessionId_.isEmpty() || QApplication::activeModalWidget() || editor_.guideActive()) {
        QTimer::singleShot(500, this, &Controller::presentStartupUpdate);
        return;
    }
    const auto release = std::move(*pendingStartupUpdate_);
    pendingStartupUpdate_.reset();
    QMessageBox prompt(QMessageBox::Information, tr("EditHere 更新"),
                       tr("发现新版本 %1。").arg(release.tagName), QMessageBox::NoButton, &editor_);
    prompt.setObjectName("startupUpdatePrompt");
    auto update = prompt.addButton(tr("立即更新"), QMessageBox::AcceptRole);
    auto skip = prompt.addButton(tr("跳过"), QMessageBox::RejectRole);
    prompt.setDefaultButton(skip);
    prompt.setEscapeButton(skip);
    prompt.exec();
    if (prompt.clickedButton() == update)
        openSettingsDialog(true, false, &release);
}
void Controller::readInstallationReceipt() {
    // An explicit settings file keeps test/development instances isolated from
    // the normal application's persistent installer result.
    const auto path = settingsFile_.isEmpty()
                          ? UpdateChecker::installationResultPath()
                          : QDir(QFileInfo(settingsFile_).absolutePath()).filePath("installer-results/last.json");
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly) || file.size() > 65536)
        return;
    const auto document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
        return;
    const auto receipt = document.object();
    if (UpdateChecker::installationResultMessage(receipt, QCoreApplication::applicationDirPath(),
                                                  QCoreApplication::applicationVersion()).isEmpty())
        return;
    const auto settingsPath = settingsFile_.isEmpty()
                                  ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
                                        .filePath("settings.ini") : settingsFile_;
    QSettings persisted(settingsPath, QSettings::IniFormat);
    if (persisted.value("updates/seenInstallerResult").toString() == receipt.value("id").toString())
        return;
    pendingInstallationReceipt_ = receipt;
    presentInstallationReceipt();
}
void Controller::presentInstallationReceipt() {
    if (pendingInstallationReceipt_.isEmpty())
        return;
    if (capturing_ || !agentSessionId_.isEmpty() || QApplication::activeModalWidget() || editor_.guideActive()) {
        QTimer::singleShot(500, this, &Controller::presentInstallationReceipt);
        return;
    }
    const auto receipt = std::move(pendingInstallationReceipt_);
    pendingInstallationReceipt_ = {};
    const auto message = UpdateChecker::installationResultMessage(receipt, QCoreApplication::applicationDirPath(),
                                                                  QCoreApplication::applicationVersion());
    QMessageBox prompt(QMessageBox::Warning, tr("EditHere 安装结果"), message, QMessageBox::Ok, &editor_);
    prompt.setObjectName("installationResultPrompt");
    QStringList details;
    if (const auto value = receipt.value("log").toString(); !value.isEmpty())
        details.append(tr("安装日志：%1").arg(value));
    if (const auto value = receipt.value("journal").toString(); !value.isEmpty())
        details.append(tr("事务记录：%1").arg(value));
    if (const auto value = receipt.value("recovery").toString(); !value.isEmpty())
        details.append(tr("恢复目录：%1").arg(value));
    if (const auto value = receipt.value("stage").toString(); !value.isEmpty())
        details.append(tr("暂存目录：%1").arg(value));
    if (!details.isEmpty()) prompt.setDetailedText(details.join('\n'));
    prompt.exec();
    const auto settingsPath = settingsFile_.isEmpty()
                                  ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
                                        .filePath("settings.ini") : settingsFile_;
    QSettings persisted(settingsPath, QSettings::IniFormat);
    persisted.setValue("updates/seenInstallerResult", receipt.value("id").toString());
    persisted.sync();
    if (persisted.status() != QSettings::NoError)
        diagnostics::write(diagnostics::Level::Warning, "updates.receipt", "Could not persist installer result acknowledgement.");
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
    scheduleStartupUpdate();
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
    scheduleStartupUpdate();
    beginCapture(false);
}
void Controller::beginCapture(bool fromTray) {
    if (fromTray)
        scheduleStartupUpdate();
    if (!agentSessionId_.isEmpty()) { activate(); return; }
    if (capturing_ || QApplication::activeModalWidget())
        return;
    if (guidePending_) {
        showGuide();
        return;
    }
    const auto foreground = captureForegroundWindow();
    editor_.dismissGuide();
    if (auto menu = tray_.contextMenu())
        menu->close();
    if (!editor_.allowReplace())
        return;
    capturing_ = true;
    captureForeground_ = fromTray ? 0 : foreground;
    // A new capture always starts on the screen, not on whatever was being browsed
    // before it.
    historyIndex_ = -1;
    wasVisible_ = editor_.isVisible();
    editor_.hide();
    const quint64 generation = ++captureGeneration_;
    QPointer<Controller> self(this);
    captureIo_.prepare(this, [self, generation] {
        if (!self || !self->capturing_ || generation != self->captureGeneration_)
            return;
        self->captureIo_.grab([self, generation](QVector<ScreenFrame> frames, QString error) {
            if (!self || !self->capturing_ || generation != self->captureGeneration_)
                return;
            auto owner = self.data();
            if (frames.isEmpty()) {
                diagnostics::write(diagnostics::Level::Error, "capture.screens", error);
                owner->capturing_ = false;
                owner->restoreAfterCapture();
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
                connect(overlay, &Overlay::scrollRequested, owner,
                        [owner, overlay](QRect) { owner->startScrollCapture(overlay); });
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
    }, fromTray);
}
void Controller::restoreAfterCapture() {
    if (wasVisible_) {
        if (captureForeground_) {
            editor_.setAttribute(Qt::WA_ShowWithoutActivating, true);
            editor_.show();
            editor_.setAttribute(Qt::WA_ShowWithoutActivating, false);
        } else {
            raiseEditor();
        }
    }
    restoreCaptureForegroundWindow(captureForeground_);
    captureForeground_ = 0;
}
void Controller::clearOverlays() {
    ++captureGeneration_;
    capturing_ = false;
    discardScrollRun();
    for (auto overlay : overlays_) {
        overlay->hide();
        overlay->deleteLater();
    }
    overlays_.clear();
}

void Controller::startScrollCapture(Overlay *source) {
    if (!source || scrollSource_ || source->selection().isEmpty())
        return;
    QString reason;
    if (!scrollIo_.supported(&reason)) {
        diagnostics::write(diagnostics::Level::Warning, "long_capture.support", reason);
        source->setCaptureNotice(reason);
        return;
    }
    const ScreenFrame &frame = source->frame();
    const QRect area = source->selection();
    // Screen captures carry native pixels and a native origin. Scaling a logical
    // desktop coordinate by one global DPI loses the origin on mixed-DPI screens.
    if (!frame.nativePixels || frame.nativeGeometry.size() != frame.image.size() ||
        !frame.image.rect().contains(area)) {
        source->setCaptureNotice(tr("只能对当前屏幕选区进行长截图，请重新截取屏幕。"));
        return;
    }
    if (area.height() < 120 || area.width() < 64) {
        source->setCaptureNotice(tr("长截图选区过小，请选择至少 64 × 120 px 的内容区域。"));
        return;
    }
    const QRect nativeRegion = area.translated(frame.nativeGeometry.topLeft());
    if (!scrollIo_.begin(frame, nativeRegion, &reason)) {
        diagnostics::write(diagnostics::Level::Error, "long_capture.begin", reason);
        source->setCaptureNotice(reason);
        return;
    }
    scrollRun_ = {};
    scrollRun_.axis = settings_.scrollAxis;
    scrollRun_.nativeRegion = nativeRegion;
    diagnostics::write(diagnostics::Level::Info, "long_capture.begin", "Long capture started",
        {{"width", nativeRegion.width()}, {"height", nativeRegion.height()},
         {"axis", settings_.scrollAxis == Qt::Vertical ? "vertical" : "horizontal"},
         {"ultraLong", settings_.scrollUltraLong}});
    const double sx = double(frame.logicalGeometry.width()) / frame.image.width();
    const double sy = double(frame.logicalGeometry.height()) / frame.image.height();
    scrollRun_.logicalRegion = QRect(frame.logicalGeometry.topLeft() +
                                        QPoint(qRound(area.x() * sx), qRound(area.y() * sy)),
                                    QSize(qRound(area.width() * sx), qRound(area.height() * sy)));
    scrollSource_ = source;
    if (!scroller_)
        scroller_ = new ScrollCapture(this);
    scroller_->setUltraLong(settings_.scrollUltraLong);
    scroller_->begin({}, 1, scrollRun_.axis);
    scroller_->setAutoCrop(settings_.scrollAutoCrop);
    const quint64 generation = ++scrollGeneration_;
    // A frozen full-screen image cannot remain on top of a scrolling application.
    // Replace it with a hollow live frame and a separate preview/control panel.
    for (auto *overlay : overlays_) {
        overlay->setBusy(true);
        overlay->hide();
    }
    auto *shade = new ScrollCaptureShade(&editor_);
    scrollShade_ = shade;
    shade->setSelection(scrollRun_.logicalRegion, source->frame().logicalGeometry);
    shade->show();
    configureNativeWindow(shade, true);
    auto *region = new ScrollCaptureRegion(&editor_);
    scrollRegion_ = region;
    region->setSelection(scrollRun_.logicalRegion, source->frame().logicalGeometry);
    region->setState(true, scrollRun_.axis);
    region->moveHandle()->setEnabled(false);
    region->show();
    configureNativeWindow(region, true);
    connect(region, &ScrollCaptureRegion::regionChanged, this, &Controller::changeScrollRegion);
    connect(region, &ScrollCaptureRegion::dragStarted, this, [this] {
        if (!scrollSource_ || !scroller_) return;
        ++scrollGeneration_;
        scrollRun_.dragging = true;
        scrollRun_.held = {};
        // A pending capture may have hidden these surfaces. Keep the handle
        // alive throughout its mouse grab and sample only after it is released.
        if (scrollShade_) scrollShade_->show();
        if (scrollRegion_) {
            scrollRegion_->show();
            scrollRegion_->moveHandle()->show();
        }
        if (scrollProgress_) scrollProgress_->show();
    });
    connect(region, &ScrollCaptureRegion::dragFinished, this, [this] {
        if (!scrollSource_ || !scroller_ || !scrollRun_.dragging) return;
        scrollRun_.dragging = false;
        if (scrollRun_.paused) return;
        beginScrollSettle();
        readScrollFrame();
    });
    auto *progress = new ScrollCaptureProgress(&editor_);
    scrollProgress_ = progress;
    connect(progress, &ScrollCaptureProgress::positionChanged, region,
            &ScrollCaptureRegion::setHandlePosition);
    progress->setAxis(scrollRun_.axis);
    QString automaticReason;
    progress->setAutomaticSupport(scrollIo_.automaticSupported(&automaticReason), automaticReason);
    progress->setAutoCrop(settings_.scrollAutoCrop);
    progress->placeBeside(scrollRun_.logicalRegion, source->frame().logicalGeometry);
    progress->setSelectionSize(scrollRun_.nativeRegion.size());
    connect(progress, &ScrollCaptureProgress::stopRequested, this, &Controller::stopScrollCapture);
    connect(progress, &ScrollCaptureProgress::finishRequested, this, &Controller::finishScrollCapture);
    connect(progress, &ScrollCaptureProgress::cancelRequested, this, [this] { abortScrollCapture(); });
    connect(progress, &ScrollCaptureProgress::automaticChanged, this, &Controller::setAutoScrollCapture);
    connect(progress, &ScrollCaptureProgress::autoCropChanged, this, [this](bool enabled) {
        if (scroller_) scroller_->setAutoCrop(enabled);
    });
    connect(progress, &ScrollCaptureProgress::resumeRequested, this, &Controller::resumeScrollCapture);
    connect(progress, &ScrollCaptureProgress::directionRequested, this, &Controller::changeScrollDirection);
    connect(progress, &ScrollCaptureProgress::cropRequested, this, [this](bool end) {
        if (!scroller_ || !scroller_->hasProgress()) return;
        const bool trimmed = end ? scroller_->cropAfterViewport() : scroller_->cropBeforeViewport();
        if (trimmed && scrollProgress_) {
            scrollProgress_->clearCrop();
            showScrollProgress();
        }
    });
    connect(progress, &ScrollCaptureProgress::copyRequested, this, [this] { exportScrollCapture(0); });
    connect(progress, &ScrollCaptureProgress::saveRequested, this, [this] { exportScrollCapture(1); });
    connect(progress, &ScrollCaptureProgress::pinRequested, this, [this] { exportScrollCapture(2); });
    // The user scrolls the original application in the default mode. Preview
    // updates must not take keyboard focus away from that application.
    progress->setAttribute(Qt::WA_ShowWithoutActivating);
    progress->show();
    configureNativeWindow(progress, true);
    region->setHandlePosition(progress->moveHandlePosition());
    beginScrollSettle();
    QPointer<Controller> self(this);
    scrollIo_.prepare(this, [self, generation] {
        if (!self || generation != self->scrollGeneration_ || !self->scrollSource_)
            return;
        // A full-screen selection may leave no room for the controls. Hide them
        // while resolving the original target as well as while reading pixels.
        const bool overlaps = self->scrollProgress_ && !excludedFromCapture(self->scrollProgress_) &&
                              self->scrollProgress_->geometry().intersects(self->scrollRun_.logicalRegion);
        if (overlaps)
            self->scrollProgress_->hide();
        const auto resolveTarget = [self, generation, overlaps] {
            if (!self || generation != self->scrollGeneration_ || !self->scrollSource_)
                return;
            self->scrollRun_.target = self->scrollIo_.targetAt(self->scrollRun_.nativeRegion.center());
            if (!self->scrollRun_.target.window && !self->scrollRun_.target.captureSession) {
                self->abortScrollCapture(tr("找不到选区下可滚动的窗口，请重试。"));
                return;
            }
            if (self->scrollRun_.target.window && !self->scrollRun_.target.processId && self->scrollProgress_) {
                self->scrollRun_.automatic = false;
                self->scrollProgress_->setAutomatic(false);
                self->scrollProgress_->setAutomaticSupport(false,
                    tr("无法确认滚动窗口所属进程，请使用手动滚动。"));
            }
            if (self->scrollRegion_) self->scrollRegion_->moveHandle()->setEnabled(true);
            if (overlaps && self->scrollProgress_)
                self->scrollProgress_->show();
            if (self->scrollRun_.target.window)
                self->scrollIo_.focus(self->scrollRun_.target);
            self->readScrollFrame();
        };
        if (overlaps)
            QTimer::singleShot(35, self.data(), resolveTarget);
        else
            resolveTarget();
    });
}

void Controller::beginScrollSettle() {
    scrollRun_.held = {};
    scrollRun_.settleDeadline.start();
    const quint64 generation = scrollGeneration_;
    const quint64 round = ++scrollRun_.settleRound;
    armScrollTimeout(generation, round);
}

void Controller::armScrollTimeout(quint64 generation, quint64 round) {
    // The platform callback can fail to arrive. A separate watchdog keeps Stop
    // and Return usable and also prevents an old round from stopping a new one.
    const int remaining = qMax(1, scrollIo_.settleTimeoutMs - int(scrollRun_.settleDeadline.elapsed()));
    QTimer::singleShot(remaining, this, [this, generation, round] {
        if (generation == scrollGeneration_ && round == scrollRun_.settleRound &&
            scrollSource_ && !scrollRun_.paused) {
            if (scrollRun_.settleDeadline.elapsed() >= scrollIo_.settleTimeoutMs)
                pauseScrollCapture(scrollRun_.automatic
                                       ? tr("页面一直在变化，等待稳定画面超时。请暂停动画后重试。")
                                       : tr("画面采集超时，请返回选区重试。"));
            else
                armScrollTimeout(generation, round);
        }
    });
}

void Controller::stepScrollCapture() {
    if (!scrollSource_ || !scroller_ || !scroller_->running() || scrollRun_.paused || scrollRun_.dragging)
        return;
    beginScrollSettle();
    const quint64 generation = scrollGeneration_;
    const quint64 round = scrollRun_.settleRound;
    if (!scrollRun_.automatic) {
        // Stable, unchanged content is an idle page, not the end of a manual
        // capture. Each sampling round has its own watchdog; waiting for the
        // user to scroll has no deadline.
        QTimer::singleShot(kScrollManualSampleMs, this, [this, generation, round] {
            if (generation == scrollGeneration_ && round == scrollRun_.settleRound)
                readScrollFrame();
        });
        return;
    }
    // Chromium re-routes wheel messages through WindowFromPoint even when they
    // were addressed to its child HWND. Controls covering the region must leave
    // before input as well as before capture, or a successful send never scrolls.
    // Capture exclusion does not help here: the wheel follows window stacking.
    const bool hideProgress = scrollProgress_ && scrollProgress_->isVisible() &&
                              scrollProgress_->geometry().intersects(scrollRun_.logicalRegion);
    if (hideProgress)
        scrollProgress_->hide();
    const auto scroll = [this, generation, round, hideProgress] {
        if (generation != scrollGeneration_ || round != scrollRun_.settleRound ||
            !scrollSource_ || scrollRun_.paused || !scrollRun_.automatic)
            return;
        QString error;
        const bool sent = scrollIo_.step(scrollRun_.target, scrollRun_.nativeRegion.center(), 1, &error);
        if (hideProgress && scrollProgress_)
            scrollProgress_->show();
        if (!sent) {
            pauseScrollCapture(error.isEmpty() ? tr("目标窗口无法继续滚动。") : error);
            return;
        }
        QTimer::singleShot(160, this, [this, generation, round] {
            if (generation == scrollGeneration_ && round == scrollRun_.settleRound)
                readScrollFrame();
        });
    };
    if (hideProgress)
        QTimer::singleShot(35, this, scroll);
    else
        scroll();
}

void Controller::setAutoScrollCapture(bool automatic) {
    if (scrollRun_.axis == Qt::Horizontal) automatic = false;
    if (!scrollSource_ || scrollRun_.paused || scrollRun_.automatic == automatic)
        return;
    if (automatic) {
        QString reason;
        const bool knownOwner = !scrollRun_.target.window || scrollRun_.target.processId;
        if (!knownOwner) reason = tr("无法确认滚动窗口所属进程，请使用手动滚动。");
        if (!knownOwner || !scrollIo_.automaticSupported(&reason)) {
            if (scrollProgress_) {
                scrollProgress_->setAutomaticSupport(false, reason);
                scrollProgress_->setNotice(reason);
            }
            return;
        }
    }
    scrollRun_.automatic = automatic;
    scrollRun_.unchanged = 0;
    if (scrollProgress_) {
        scrollProgress_->setAutomatic(automatic);
        if (scroller_ && scroller_->running())
            showScrollProgress();
    }
    // During preparation the first frame still needs to be accepted. Later
    // toggles invalidate any pending frame and wheel timer before starting the
    // next round, while retaining the already stitched pixels.
    if (!scrollRun_.initial) {
        ++scrollRun_.settleRound;
        if (scrollProgress_)
            scrollProgress_->show();
        if (scrollShade_) scrollShade_->show();
        if (scrollRegion_) {
            scrollRegion_->show();
            scrollRegion_->moveHandle()->show();
        }
        if (!automatic)
            scrollIo_.focus(scrollRun_.target);
        stepScrollCapture();
    }
}

void Controller::readScrollFrame() {
    if (!scrollSource_ || scrollRun_.paused || scrollRun_.dragging)
        return;
    const quint64 generation = scrollGeneration_;
    const quint64 round = scrollRun_.settleRound;
    // Display affinity is best effort on older Windows and some graphics drivers.
    // If the controls overlap the capture and are not excluded from it, hide them
    // for every read, including the first; wait one compositor frame first.
    const bool hideProgress = scrollProgress_ && scrollProgress_->isVisible() &&
                              !excludedFromCapture(scrollProgress_) &&
                              scrollProgress_->geometry().intersects(scrollRun_.logicalRegion);
    QPointer<QWidget> preview = scrollProgress_ ? scrollProgress_->findChild<QWidget *>("scrollPreviewArea") : nullptr;
    const bool hidePreview = preview && preview->isVisible() && !excludedFromCapture(preview) &&
                             preview->geometry().intersects(scrollRun_.logicalRegion);
    QPointer<QWidget> cropMenu = scrollProgress_ ? scrollProgress_->findChild<QWidget *>("scrollCropMenu") : nullptr;
    const bool hideCropMenu = cropMenu && cropMenu->isVisible() &&
                             (hideProgress || (!excludedFromCapture(cropMenu) &&
                              cropMenu->geometry().intersects(scrollRun_.logicalRegion)));
    QPointer<QWidget> handle = scrollRegion_ ? scrollRegion_->moveHandle() : nullptr;
    const bool hideHandle = handle && handle->isVisible() && !excludedFromCapture(handle) &&
                            handle->geometry().intersects(scrollRun_.logicalRegion);
    QPointer<ScrollCaptureRegion> region = scrollRegion_;
    QPointer<ScrollCaptureShade> shade = scrollShade_;
    const bool hideRegion = region && region->isVisible() && !excludedFromCapture(region);
    const bool hideShade = shade && shade->isVisible() && !excludedFromCapture(shade);
    const bool restoreHandle = handle && handle->isVisible() && (hideHandle || hideRegion);
    if (hideProgress)
        scrollProgress_->hide();
    if (hidePreview) preview->hide();
    if (hideCropMenu) cropMenu->hide();
    if (hideHandle) handle->hide();
    if (hideRegion) region->hide();
    if (hideShade) shade->hide();
    scrollIo_.prepareFrame();
    QPointer<Controller> self(this);
    const auto read = [self, generation, round, hideProgress, hidePreview, hideCropMenu, hideRegion, hideShade, restoreHandle, preview, cropMenu, handle, region, shade] {
        if (!self || generation != self->scrollGeneration_ || round != self->scrollRun_.settleRound ||
            !self->scrollSource_)
            return;
        const auto target = self->scrollIo_.targetAt(self->scrollRun_.nativeRegion.center());
        if (target.window != self->scrollRun_.target.window ||
            target.processId != self->scrollRun_.target.processId ||
            target.captureSession != self->scrollRun_.target.captureSession) {
            self->pauseScrollCapture(tr("原滚动窗口已关闭、移动或被遮挡，请返回选区重试。"));
            return;
        }
        self->scrollIo_.grab(self->scrollRun_.nativeRegion,
                            [self, generation, round, hideProgress, hidePreview, hideCropMenu, hideRegion, hideShade, restoreHandle, preview, cropMenu, handle, region, shade](QImage grabbed, QString error) {
            if (!self || generation != self->scrollGeneration_ || round != self->scrollRun_.settleRound ||
                !self->scrollSource_)
                return;
            if (hideProgress && self->scrollProgress_)
                self->scrollProgress_->show();
            if (hidePreview && preview) preview->show();
            if (hideCropMenu && cropMenu) cropMenu->show();
            if (hideShade && shade) shade->show();
            if (hideRegion && region) region->show();
            if (restoreHandle && handle) handle->show();
            if (!error.isEmpty() || grabbed.isNull() || grabbed.size() != self->scrollRun_.nativeRegion.size()) {
                self->pauseScrollCapture(error.isEmpty() ? tr("屏幕选区采集失败，请重新选择区域。") : error);
                return;
            }
            if (self->scrollRun_.settleDeadline.elapsed() >= self->scrollIo_.settleTimeoutMs) {
                self->pauseScrollCapture(self->scrollRun_.automatic
                                            ? tr("页面一直在变化，等待稳定画面超时。请暂停动画后重试。")
                                            : tr("画面采集超时，请返回选区重试。"));
                return;
            }
            // Continuous manual scrolling can keep producing valid frames for
            // longer than the settling timeout. Wait for the user's pause while
            // retaining a deadline only for a missing capture callback.
            if (!self->scrollRun_.automatic)
                self->scrollRun_.settleDeadline.restart();
            if (!self->scrollRun_.held.isNull() &&
                frameDifference(self->scrollRun_.held, grabbed) <= kScrollStableDifference) {
                self->placeScrollFrame(grabbed);
                return;
            }
            // While the user keeps scrolling, an exact continuation is appended
            // at once so the preview follows; a still frame later confirms it.
            if (!self->scrollRun_.automatic && !self->scrollRun_.initial && !self->scrollRun_.held.isNull() &&
                self->scroller_ && self->scroller_->takeMoving(grabbed)) {
                self->scrollRun_.mismatched = false;
                self->showScrollProgress();
            }
            self->scrollRun_.held = std::move(grabbed);
            QTimer::singleShot(self->scrollRun_.automatic ? 100 : kScrollManualSampleMs, self.data(),
                               [self, generation, round] {
                if (self && generation == self->scrollGeneration_ && round == self->scrollRun_.settleRound)
                    self->readScrollFrame();
            });
        });
    };
    if (hideProgress || hidePreview || hideCropMenu || hideHandle || hideRegion || hideShade)
        QTimer::singleShot(35, this, read);
    else
        read();
}

void Controller::placeScrollFrame(const QImage &frame) {
    if (!scrollSource_ || !scroller_ || scrollRun_.paused)
        return;
    ++scrollRun_.settleRound;
    if (scrollRun_.initial) {
        scrollRun_.initial = false;
        if (!scroller_->begin(frame, 1, scrollRun_.axis)) {
            abortScrollCapture(tr("长截图选区超过图像上限，请缩小区域后重试。"));
            return;
        }
        showScrollProgress();
        stepScrollCapture();
        return;
    }
    const auto outcome = scroller_->take(frame);
    if (outcome == ScrollCapture::Outcome::Added) {
        scrollRun_.unchanged = 0;
        scrollRun_.mismatched = false;
        showScrollProgress();
        if (scroller_->atLimit()) {
            pauseScrollCapture(tr("已达长截图上限，可以完成已拼接的部分。"));
            return;
        }
    } else if (outcome == ScrollCapture::Outcome::Repeat) {
        scrollRun_.mismatched = false;
        showScrollProgress();
        if (scrollRun_.automatic && ++scrollRun_.unchanged >= 2) {
            if (scroller_->frames() > 0)
                finishScrollCapture();
            else
                abortScrollCapture(tr("内容没有产生可拼接的滚动，请选择可滚动的内容区域。"));
            return;
        }
    } else if (scroller_->atLimit() || scrollRun_.automatic) {
        pauseScrollCapture(scroller_->atLimit() ? tr("已达长截图上限，可以完成已拼接的部分。")
                                              : tr("无法匹配相邻画面，采集已停止。可完成已有部分或返回选区重试。"));
        return;
    } else {
        // A manual scroll can outrun sampling and leave no shared rows. The user
        // can scroll back a little; the stitch resumes from the last good frame.
        scrollRun_.mismatched = true;
        diagnostics::write(diagnostics::Level::Warning, "long_capture.match", "No overlap with previous frame",
            {{"frames", scroller_->frames()}, {"width", scroller_->size().width()}, {"height", scroller_->size().height()}});
        scrollRun_.notice = tr("滚动过快，新画面与已拼接部分没有重叠。\n请稍微往回滚动，对上后会继续拼接。");
        if (scrollProgress_)
            scrollProgress_->setNotice(scrollRun_.notice);
        showScrollProgress();
    }
    stepScrollCapture();
}

void Controller::showScrollProgress() {
    if (!scrollProgress_ || !scroller_)
        return;
    const QRect source = scrollProgress_->previewSourceRect(scroller_->size(), scroller_->viewportRect(),
                                                           scrollRun_.axis, scroller_->originOffset());
    scrollProgress_->setProgress(scroller_->previewRegion(source, scrollProgress_->previewWidth()), scroller_->frames(),
                                 scroller_->size(), scroller_->viewportRect(), scroller_->matched(), scrollRun_.axis, source);
    if (scrollRun_.mismatched)
        scrollProgress_->setNotice(scrollRun_.notice);
}

void Controller::pauseScrollCapture(const QString &message, bool allowEmpty) {
    if (!scrollSource_)
        return;
    diagnostics::write(diagnostics::Level::Info, "long_capture.pause", message,
        {{"frames", scroller_ ? scroller_->frames() : 0}, {"automatic", scrollRun_.automatic},
         {"width", scroller_ ? scroller_->size().width() : 0}, {"height", scroller_ ? scroller_->size().height() : 0}});
    if (!scroller_ || (scroller_->frames() == 0 && (!allowEmpty || scrollRun_.initial))) {
        abortScrollCapture(message);
        return;
    }
    ++scrollGeneration_;
    scrollRun_.paused = true;
    scroller_->pause();
    if (scrollShade_) scrollShade_->show();
    if (scrollRegion_) {
        scrollRegion_->setState(false, scrollRun_.axis);
        scrollRegion_->show();
        scrollRegion_->moveHandle()->show();
    }
    if (scrollProgress_) {
        scrollProgress_->setStopped(message);
        scrollProgress_->show();
        scrollProgress_->raise();
        scrollProgress_->activateWindow();
        scrollProgress_->setFocus();
    }
}

void Controller::stopScrollCapture() {
    if (!scrollSource_ || !scroller_) return;
    if (!scrollRun_.target.window && !scrollRun_.target.captureSession) {
        abortScrollCapture();
        return;
    }
    ++scrollGeneration_;
    scrollRun_.paused = true;
    scrollRun_.automatic = false;
    scrollRun_.initial = true;
    scrollRun_.mismatched = false;
    scrollRun_.held = {};
    scroller_->begin({}, 1, scrollRun_.axis);
    if (scrollShade_) scrollShade_->show();
    if (scrollRegion_) {
        scrollRegion_->setState(false, scrollRun_.axis);
        scrollRegion_->show();
        scrollRegion_->moveHandle()->show();
    }
    if (scrollProgress_) {
        scrollProgress_->setProgress({}, 0, {}, {}, true, scrollRun_.axis);
        scrollProgress_->setSelectionSize(scrollRun_.nativeRegion.size());
        scrollProgress_->setStopped(tr("已停止截图，可调整选区后重新开始。"));
        scrollProgress_->show();
    }
}

void Controller::resumeScrollCapture() {
    if (!scrollSource_ || !scroller_ || !scrollRun_.paused) return;
    ++scrollGeneration_;
    scrollRun_.paused = false;
    scrollRun_.automatic = false;
    scrollRun_.mismatched = false;
    scrollRun_.unchanged = 0;
    if (!scrollRun_.initial) scroller_->resume();
    if (scrollShade_) scrollShade_->show();
    if (scrollRegion_) {
        scrollRegion_->setState(true, scrollRun_.axis);
        scrollRegion_->show();
        scrollRegion_->moveHandle()->show();
    }
    if (scrollProgress_) scrollProgress_->setRunning(scrollRun_.axis);
    if (scrollProgress_) scrollProgress_->show();
    if (!scrollRun_.initial) showScrollProgress();
    if (scrollRun_.target.window) scrollIo_.focus(scrollRun_.target);
    beginScrollSettle();
    readScrollFrame();
}

void Controller::changeScrollDirection() {
    if (!scrollSource_ || !scroller_) return;
    const bool prepared = scrollRun_.target.window || scrollRun_.target.captureSession;
    // The direction control is available while native capture preparation is
    // pending. Keep that callback alive; it resolves the original window before
    // sampling the first viewport using the newly selected axis.
    if (prepared) ++scrollGeneration_;
    scrollRun_.axis = scrollRun_.axis == Qt::Vertical ? Qt::Horizontal : Qt::Vertical;
    scrollRun_.automatic = false;
    scrollRun_.paused = false;
    scrollRun_.initial = true;
    scrollRun_.mismatched = false;
    scrollRun_.unchanged = 0;
    scroller_->begin({}, 1, scrollRun_.axis);
    if (scrollShade_) scrollShade_->show();
    if (scrollRegion_) {
        scrollRegion_->setState(true, scrollRun_.axis);
        scrollRegion_->show();
        scrollRegion_->moveHandle()->show();
    }
    scrollProgress_->setRunning(scrollRun_.axis);
    scrollProgress_->show();
    scrollProgress_->setProgress({}, 0, {}, {}, true, scrollRun_.axis);
    scrollProgress_->setSelectionSize(scrollRun_.nativeRegion.size());
    scrollProgress_->setNotice(tr("已切换截图方向，从当前画面重新开始。"));
    if (!prepared) return;
    beginScrollSettle();
    readScrollFrame();
}

void Controller::changeScrollRegion(const QRect &logicalRegion) {
    if (!scrollSource_ || !scroller_ || logicalRegion.isEmpty()) return;
    const auto &frame = scrollSource_->frame();
    const double sx = double(frame.image.width()) / frame.logicalGeometry.width();
    const double sy = double(frame.image.height()) / frame.logicalGeometry.height();
    const QPoint local = logicalRegion.topLeft() - frame.logicalGeometry.topLeft();
    const bool resized = logicalRegion.size() != scrollRun_.logicalRegion.size();
    // A logical rectangle is only a rounded display of the native selection.
    // At 150%/200%, converting its size back can add or lose one pixel. A move
    // must retain the exact original dimensions, including its unchanged axis.
    QPoint origin = frame.nativeGeometry.topLeft() + QPoint(qRound(local.x() * sx), qRound(local.y() * sy));
    QSize size = resized ? QSize(qRound(logicalRegion.width() * sx), qRound(logicalRegion.height() * sy))
                         : scrollRun_.nativeRegion.size();
    if (logicalRegion.left() == scrollRun_.logicalRegion.left()) origin.setX(scrollRun_.nativeRegion.left());
    if (logicalRegion.top() == scrollRun_.logicalRegion.top()) origin.setY(scrollRun_.nativeRegion.top());
    origin.setX(std::clamp(origin.x(), frame.nativeGeometry.left(), frame.nativeGeometry.right() + 1 - size.width()));
    origin.setY(std::clamp(origin.y(), frame.nativeGeometry.top(), frame.nativeGeometry.bottom() + 1 - size.height()));
    const QRect native(origin, size);
    ++scrollGeneration_;
    scrollRun_.logicalRegion = logicalRegion;
    scrollRun_.nativeRegion = native;
    if (scrollShade_) {
        scrollShade_->setSelection(logicalRegion, frame.logicalGeometry);
        scrollShade_->show();
    }
    if (scrollRegion_) scrollRegion_->show();
    if (scrollProgress_) {
        scrollProgress_->placeBeside(logicalRegion, frame.logicalGeometry);
        const QRect crop = scrollProgress_->croppedRect();
        const QSize resultSize = resized || scroller_->size().isEmpty() ? native.size()
                                 : crop.isEmpty() ? scroller_->size() : crop.size();
        scrollProgress_->setSelectionSize(resultSize);
        if (scrollRegion_) scrollRegion_->setHandlePosition(scrollProgress_->moveHandlePosition());
    }
    scrollRun_.mismatched = false;
    scrollRun_.unchanged = 0;
    if (resized) {
        scroller_->begin({}, 1, scrollRun_.axis);
        scrollRun_.initial = true;
        scrollProgress_->setProgress({}, 0, {}, {}, true, scrollRun_.axis);
        scrollProgress_->setNotice(tr("选区尺寸已调整，点击开始从新选区采集。"));
    }
    if (!scrollRun_.paused && !scrollRun_.dragging) {
        if (scrollRegion_) scrollRegion_->moveHandle()->show();
        scrollProgress_->show();
        beginScrollSettle();
        readScrollFrame();
    }
}

QImage Controller::scrollResult() const {
    if (!scroller_ || !scroller_->hasProgress()) return {};
    const QImage image = scroller_->picture();
    const QRect crop = scrollProgress_ ? scrollProgress_->croppedRect().intersected(image.rect()) : image.rect();
    return crop.isEmpty() || crop == image.rect() ? image : image.copy(crop);
}

void Controller::exportScrollCapture(int action) {
    if (action < 0 || action > 2 || !scroller_ || !scroller_->hasProgress() || !scrollSource_) return;
    if (!scroller_->fitsImageLimits()) {
        if (action != 1) return;
        if (!scrollRun_.paused) pauseScrollCapture(tr("采集已暂停，正在保存长截图。"));
        const QString folder = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
        const QString filename = QStringLiteral("EditHere-%1.png")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")));
        QString path = QDir(folder).filePath(filename);
        if (scrollShade_) scrollShade_->hide();
        if (scrollProgress_) scrollProgress_->hide();
        if (scrollRegion_) scrollRegion_->hide();
        path = QFileDialog::getSaveFileName(&editor_, tr("保存长截图"), path, tr("PNG 图像 (*.png)"));
        if (scrollShade_) scrollShade_->show();
        if (scrollRegion_) scrollRegion_->show();
        if (scrollProgress_) scrollProgress_->show();
        if (path.isEmpty()) return;
        if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".png");
        QString error;
        if (!QDir().mkpath(QFileInfo(path).absolutePath()) || !scroller_->savePng(path, &error)) {
            diagnostics::write(diagnostics::Level::Error, "long_capture.save", error.isEmpty() ? "Unable to create save directory" : error);
            if (scrollProgress_) scrollProgress_->setNotice(error.isEmpty() ? tr("无法创建保存目录。") : error);
            return;
        }
        tray_.showMessage(QStringLiteral("EditHere"), tr("已保存 %1").arg(path));
        clearOverlays();
        restoreAfterCapture();
        return;
    }
    const QImage picture = scrollResult();
    if (picture.isNull() || !scrollSource_) return;
    const QRect selection = scrollRun_.nativeRegion.translated(-scrollSource_->frame().nativeGeometry.topLeft());
    if (action == 1) {
        if (!scrollRun_.paused) pauseScrollCapture(tr("采集已暂停，正在保存长截图。"));
        if (scrollProgress_) scrollProgress_->hide();
        if (scrollRegion_) scrollRegion_->hide();
        if (scrollShade_) scrollShade_->hide();
        const bool saved = saveImage(picture);
        if (!saved) {
            if (scrollShade_) scrollShade_->show();
            if (scrollRegion_) scrollRegion_->show();
            if (scrollProgress_) scrollProgress_->show();
            return;
        }
    } else if (action == 0) {
        QApplication::clipboard()->setImage(picture);
    } else if (action == 2) {
        pinImage(picture, scrollRun_.logicalRegion);
    }
    remember(picture, selection);
    clearOverlays();
    restoreAfterCapture();
}

void Controller::finishScrollCapture() {
    if (!scrollSource_ || !scroller_ || scroller_->frames() == 0)
        return;
    const QImage picture = scrollResult();
    const QRect selection = scrollRun_.nativeRegion.translated(-scrollSource_->frame().nativeGeometry.topLeft());
    if (picture.isNull())
        return;
    try {
        auto doc = fromImage(picture, "scroll", tr("长截图"));
        // Long screenshots carry the exact stitched pixels. Decorations can push
        // an image at the capture limit beyond the editor's supported dimensions.
        remember(picture, selection);
        clearOverlays();
        captureForeground_ = 0;
        editor_.setDocument(std::move(doc));
        raiseEditor();
    } catch (const std::exception &error) {
        abortScrollCapture(QString::fromUtf8(error.what()));
    }
}

void Controller::discardScrollRun(bool retainScreenSession) {
    ++scrollGeneration_;
    scrollIo_.end(retainScreenSession);
    // Release accepted slices after completion or cancellation. The editor owns
    // its final image; a paused run alone keeps the extra viewport data alive.
    delete scroller_;
    scroller_ = nullptr;
    if (scrollRegion_) {
        scrollRegion_->hide();
        scrollRegion_->deleteLater();
        scrollRegion_ = nullptr;
    }
    if (scrollShade_) {
        scrollShade_->hide();
        scrollShade_->deleteLater();
        scrollShade_ = nullptr;
    }
    if (scrollProgress_) {
        scrollProgress_->hide();
        scrollProgress_->deleteLater();
        scrollProgress_ = nullptr;
    }
    scrollSource_ = nullptr;
    scrollRun_ = {};
}

void Controller::abortScrollCapture(const QString &message) {
    diagnostics::write(message.isEmpty() ? diagnostics::Level::Info : diagnostics::Level::Warning,
                       "long_capture.abort", message.isEmpty() ? "Long capture cancelled" : message);
    QPointer<Overlay> source = scrollSource_;
    discardScrollRun(true);
    if (!source)
        return;
    for (auto *overlay : overlays_) {
        overlay->setBusy(false);
        overlay->show();
        configureNativeWindow(overlay, true);
    }
    source->setCaptureNotice(message);
    source->raise();
    source->activateWindow();
    source->setFocus();
}
void Controller::cancelCapture() {
    clearOverlays();
    restoreAfterCapture();
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
        diagnostics::write(diagnostics::Level::Error, "settings.capture_style", error);
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
        restoreAfterCapture();
        diagnostics::write(diagnostics::Level::Error, "capture.history", QString::fromUtf8(error.what()));
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
    restoreAfterCapture();
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
    restoreAfterCapture();
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
        restoreAfterCapture();
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
    restoreAfterCapture();
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
        if (capturing_ || QApplication::activeModalWidget())
            return;
        raiseEditor();
        if (!editor_.allowReplace())
            return;
        try {
            editor_.setDocument(fromImage(picture, "pin", tr("置顶图片")));
            raiseEditor();
        } catch (const std::exception &error) {
            diagnostics::write(diagnostics::Level::Error, "capture.pin_edit", QString::fromUtf8(error.what()));
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
    diagnostics::write(diagnostics::Level::Error, "capture.save", "Image could not be written",
                       {{"format", QFileInfo(path).suffix()}, {"width", image.width()}, {"height", image.height()}});
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
        if (!saveSettings(settings_, &error, settingsFile_))
            diagnostics::write(diagnostics::Level::Error, "settings.ocr", error);
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
            if (!saveSettings(settings_, &error, settingsFile_))
                diagnostics::write(diagnostics::Level::Error, "settings.ocr", error);
            runRecognition(next);
        });
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
    } else {
        ocrDialog_->setImage(image, language);
        ocrDialog_->show();
        ocrDialog_->raise();
        ocrDialog_->activateWindow();
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
        restoreAfterCapture();
        diagnostics::write(diagnostics::Level::Error, "capture.complete", QString::fromUtf8(e.what()));
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
                {"hasDocument", editor_.hasDocument()}, {"dirty", editor_.hasUnsavedChanges()},
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
        // An Agent capture is not the user's first interactive desktop launch.
        beginCapture(false);
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
        guidePending_ = false;
        editor_.loadMedia(input);
    } catch (const std::exception &error) {
        diagnostics::write(diagnostics::Level::Error, "agent.load_media", QString::fromUtf8(error.what()));
        return agentError("io_error", QString::fromUtf8(error.what()));
    }
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
    diagnostics::write(code == "io_error" ? diagnostics::Level::Error : diagnostics::Level::Info,
                       "agent.session_end", message, {{"code", code}});
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
                            {"annotations", editor_.totalAnnotationCount()}, {"imageIncluded", agentEmbed_}};
    agentSessionId_.clear();
    agentOutput_.clear();
    editor_.setAgentSession(false);
    emit agentSessionFinished(id, result);
}
} // namespace h2d
