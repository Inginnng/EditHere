#include "controller.h"
#include "ui.h"
#include <QApplication>
#include <QClipboard>
#include <QMenu>
#include <QMessageBox>
#include <QScreen>
#include <QTimer>
namespace h2d {
Controller::Controller(QObject *parent)
    : QObject(parent), editor_(), tray_(glyph("capture", accent()), this), shortcut_(this) {
    auto menu = new QMenu(&editor_);
    menu->addAction("截图    " + globalShortcutLabel(), this, &Controller::capture);
    menu->addAction("打开图片或项目", &editor_, [this] { editor_.openFile(); });
    menu->addAction("粘贴图片", &editor_, &Editor::pasteImage);
    menu->addAction("恢复批注窗口", this, &Controller::activate);
#ifdef Q_OS_MAC
    menu->addAction("启用系统元素识别", this, [this] {
        if (requestAccessibility())
            tray_.showMessage("Help2Design", "已启用系统元素识别");
        else
            tray_.showMessage("Help2Design", "请在系统设置中授予辅助功能权限，图片识别仍可直接使用。");
    });
#endif
    menu->addSeparator();
    menu->addAction("退出", this, &Controller::quit);
    tray_.setContextMenu(menu);
    tray_.setToolTip("Help2Design · " + globalShortcutLabel());
    tray_.show();
    connect(&tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger)
            capture();
    });
    connect(&shortcut_, &GlobalShortcut::triggered, this, &Controller::capture);
    connect(&editor_, &Editor::captureRequested, this, &Controller::capture);
    if (!shortcut_.start())
        tray_.showMessage("Help2Design", "截图快捷键已被占用，可点击托盘图标截图。");
}
void Controller::start(bool demo, const QString &path) {
    if (!path.isEmpty())
        editor_.openFile(path);
    else if (demo)
        editor_.setDocument(fromImage(exampleImage(), "demo", "示例产品页面"));
    else
        capture();
}
void Controller::activate() {
    if (editor_.hasDocument()) {
        editor_.show();
        editor_.raise();
        editor_.activateWindow();
    } else
        capture();
}
void Controller::capture() {
    if (capturing_)
        return;
    if (!editor_.allowReplace())
        return;
    capturing_ = true;
    wasVisible_ = editor_.isVisible();
    editor_.hide();
    QTimer::singleShot(140, this, [this] {
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
