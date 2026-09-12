#include "controller.h"
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
Controller::Controller(QObject *parent, const AppSettings &settings)
    : QObject(parent), settings_(settings), editor_(),
      tray_(QApplication::windowIcon().isNull() ? glyph("capture", accent()) : QApplication::windowIcon(), this),
      shortcut_(this) {
    editor_.setPreferences(settings_);
    editor_.setShortcuts(settings_.shortcuts);
    tray_.setObjectName("helpDesignTray");
    auto menu = new QMenu(&editor_);
    captureAction_ = menu->addAction("截图", this, &Controller::capture);
    captureAction_->setObjectName("trayCapture");
    menu->addAction("打开图片或项目", &editor_, [this] { editor_.openFile(); });
    menu->addAction("粘贴图片", &editor_, &Editor::pasteImage);
    menu->addAction("恢复批注窗口", this, &Controller::activate);
#ifdef Q_OS_MAC
    menu->addAction("启用系统元素识别", this, [this] {
        if (requestAccessibility())
            tray_.showMessage("HelpDesign", "已启用系统元素识别");
        else
            tray_.showMessage("HelpDesign", "请在系统设置中授予辅助功能权限，图片识别仍可直接使用。");
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
    connect(&editor_, &Editor::toolbarSettingsRequested,this,[this] { openSettings(false,true); });
    if (!shortcut_.start(settings_.shortcuts.value("capture")))
        tray_.showMessage("HelpDesign", "截图快捷键未能注册，请右键托盘打开设置修改。");
}
void Controller::updateTrayShortcut() {
    const auto label = settings_.shortcuts.value("capture").toString(QKeySequence::NativeText);
    captureAction_->setText(label.isEmpty() ? "截图" : "截图    " + label);
    tray_.setToolTip(label.isEmpty() ? "HelpDesign" : "HelpDesign · " + label);
}
void Controller::openSettings(bool updates, bool toolbar) {
    if (capturing_ || QApplication::activeModalWidget())
        return;
    if (auto menu = tray_.contextMenu())
        menu->close();
    const auto activeShortcut = shortcut_.sequence();
    shortcut_.stop();
    SettingsDialog dialog(settings_, &editor_);
    if (updates)
        dialog.showUpdates(true);
    else if (toolbar)
        dialog.showToolbar();
    dialog.setApplyHandler([this](const AppSettings &next) -> QString {
        if (auto error = validateSettings(next); !error.isEmpty())
            return error;
        const auto oldShortcut = shortcut_.sequence();
        if (!shortcut_.start(next.shortcuts.value("capture")))
            return shortcut_.lastError().isEmpty() ? "截图快捷键无法注册，请更换组合键。"
                                                   : shortcut_.lastError();
        QString error;
        if (!saveSettings(next, &error)) {
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
    if (dialog.exec() != QDialog::Accepted && !shortcut_.start(activeShortcut))
        tray_.showMessage("HelpDesign", "截图快捷键未能恢复，请在设置中更换组合键。");
}
void Controller::start(bool demo, const QString &path) {
    if (!path.isEmpty())
        editor_.openFile(path);
    else if (demo)
        editor_.setDocument(fromImage(exampleImage(), "demo", "示例产品页面"));
    else if (settings_.captureOnStartup)
        capture();
    if (!startupUpdateChecked_) {
        startupUpdateChecked_ = true;
        if (settings_.checkUpdatesOnStartup) {
            auto checker = new UpdateChecker(this);
            connect(checker, &UpdateChecker::finished, this,
                    [this, checker](UpdateChecker::Status status, const QString &message, const QUrl &) {
                        if (status == UpdateChecker::Available)
                            tray_.showMessage("HelpDesign 更新", message + "\n右键托盘选择检查更新。",
                                              QSystemTrayIcon::Information);
                        checker->deleteLater();
                    });
            QTimer::singleShot(3000, checker, &UpdateChecker::check);
        }
    }
}
void Controller::activate() {
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
    if (capturing_ || QApplication::activeModalWidget())
        return;
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
    if (capturing_)
        cancelCapture();
    if (!editor_.allowReplace())
        return;
    tray_.hide();
    qApp->quit();
}
} // namespace h2d
