#include "capturetoolbar.h"
#include "ui.h"
#include <QApplication>
#include <QDir>
#include <QFontDatabase>
#include <QImage>
#include <QPainter>
#include <QTextStream>

// A visual review utility, deliberately separate from geometry assertions.
// It draws the icons used by the application at their real toolbar sizes.
int main(int argc, char **argv) {
    QApplication application(argc, argv);
#ifdef Q_OS_WIN
    if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
        const QString fonts = qEnvironmentVariable("WINDIR") + "/Fonts/";
        if (QFontDatabase::addApplicationFont(fonts + "segoeui.ttf") < 0 ||
            QFontDatabase::addApplicationFont(fonts + "msyh.ttc") < 0) {
            QTextStream(stderr) << "Could not load the Windows UI fonts.\n";
            return 5;
        }
    }
#endif
    const QDir output(argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("icon-gallery"));
    if (!QDir().mkpath(output.absolutePath())) return 1;
    const QStringList names{
        "scroll", "image-save", "save", "settings", "open", "ocr", "capture", "image-copy",
        "copy", "edit", "pin", "help", "eye", "eye-off", "zoom-in", "explode",
        "note-add", "json", "json-copy", "fit", "point", "rect", "smart", "select",
        "undo", "redo", "fullscreen", "fullscreen-exit", "close", "notes", "trash", "more",
        "check", "corner", "shadow", "crop", "picker", "plus", "minus"};
    constexpr int columns = 8, cellWidth = 148, cellHeight = 106, top = 48;
    const int rows = (names.size() + columns - 1) / columns;
    for (const auto &theme : {h2d::ThemeMode::Light, h2d::ThemeMode::Dark}) {
        h2d::applyTheme(theme);
        const bool dark = theme == h2d::ThemeMode::Dark;
        const QColor face(dark ? "#202126" : "#fbfbfd");
        const QColor ink(dark ? "#ededf2" : "#232326");
        const QColor muted(dark ? "#a0a2ae" : "#898990");
        QImage sheet(columns * cellWidth, top + rows * cellHeight, QImage::Format_ARGB32_Premultiplied);
        sheet.fill(face);
        QPainter painter(&sheet);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(ink);
        painter.drawText(QRect(16, 8, sheet.width() - 32, 32), Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("EditHere icons  |  20 px / 24 px / 2x detail  |  %1")
                             .arg(dark ? "Dark" : "Light"));
        for (int index = 0; index < names.size(); ++index) {
            const QPoint origin((index % columns) * cellWidth, top + (index / columns) * cellHeight);
            auto icon = h2d::glyph(names[index]);
            const auto image = icon.pixmap(24, 24).toImage();
            bool visible = false;
            for (int y = 0; y < image.height() && !visible; ++y)
                for (int x = 0; x < image.width(); ++x)
                    if (image.pixelColor(x, y).alpha() > 0) {
                        visible = true;
                        break;
                    }
            if (!visible) {
                QTextStream(stderr) << "Empty glyph: " << names[index] << '\n';
                return 4;
            }
            for (const auto [x, size] : {std::pair{15, 20}, {48, 24}, {82, 48}}) {
                icon.paint(&painter, QRect(origin + QPoint(x, 14), QSize(size, size)));
            }
            painter.setPen(ink);
            painter.drawText(QRect(origin + QPoint(0, 70), QSize(cellWidth, 24)), Qt::AlignCenter, names[index]);
            painter.setPen(muted);
            painter.drawLine(origin + QPoint(8, 100), origin + QPoint(cellWidth - 8, 100));
        }
        painter.end();
        const auto suffix = dark ? QStringLiteral("dark") : QStringLiteral("light");
        if (!sheet.save(output.filePath(QStringLiteral("icons-%1.png").arg(suffix)))) return 2;
        const QStringList primaryNames{"scroll", "image-save", "settings", "open", "ocr"};
        const QStringList primaryLabels{QStringLiteral("长截图"), QStringLiteral("保存图片"),
                                        QStringLiteral("设置"), QStringLiteral("打开文件"),
                                        QStringLiteral("文字识别")};
        QImage primary(5 * cellWidth, cellHeight, QImage::Format_ARGB32_Premultiplied);
        primary.fill(face);
        QPainter primaryPainter(&primary);
        for (int index = 0; index < primaryNames.size(); ++index) {
            const int x = index * cellWidth;
            const auto icon = h2d::glyph(primaryNames[index]);
            icon.paint(&primaryPainter, QRect(x + 28, 18, 20, 20));
            icon.paint(&primaryPainter, QRect(x + 59, 16, 24, 24));
            primaryPainter.setPen(ink);
            primaryPainter.drawText(QRect(x, 53, cellWidth, 24), Qt::AlignCenter, primaryLabels[index]);
            primaryPainter.setPen(muted);
            primaryPainter.drawText(QRect(x, 78, cellWidth, 20), Qt::AlignCenter, QStringLiteral("20 px / 24 px"));
        }
        primaryPainter.end();
        if (!primary.save(output.filePath(QStringLiteral("icons-main-%1.png").arg(suffix)))) return 2;

        h2d::CaptureToolbar toolbar;
        toolbar.setSelectionSize(QSize(720, 480));
        toolbar.show();
        application.processEvents();
        if (!toolbar.grab().save(output.filePath(QStringLiteral("capture-toolbar-%1.png").arg(suffix)))) return 3;

        QImage picture = argc > 2 ? QImage(QString::fromLocal8Bit(argv[2])) : QImage();
        if (picture.isNull()) {
            picture = QImage(480, 1180, QImage::Format_RGB32);
            picture.fill(QColor("#ffffff"));
            QPainter content(&picture);
            content.fillRect(QRect(0, 0, 480, 60), QColor("#e5efff"));
            content.setPen(QColor("#232326"));
            content.drawText(QRect(20, 10, 440, 40), Qt::AlignVCenter, QStringLiteral("EditHere · 长截图预览"));
            for (int row = 0; row < 14; ++row) {
                const int y = 82 + row * 74;
                content.fillRect(QRect(20, y, 440, 52), QColor(row % 2 ? "#f5f5f7" : "#e5efff"));
                content.drawText(QRect(36, y, 404, 52), Qt::AlignVCenter,
                                 QStringLiteral("已拼接的内容 · %1").arg(row + 1));
            }
        }
        h2d::ScrollCaptureProgress progress;
        progress.setAutomatic(false);
        progress.setProgress(picture, 3);
        progress.show();
        application.processEvents();
        if (!progress.grab().save(output.filePath(QStringLiteral("long-capture-manual-%1.png").arg(suffix)))) return 3;
        progress.setAutomatic(true);
        progress.setProgress(picture, 3);
        application.processEvents();
        if (!progress.grab().save(output.filePath(QStringLiteral("long-capture-auto-%1.png").arg(suffix)))) return 3;
        progress.setStopped(QStringLiteral("已停止，可以完成当前长截图。"));
        application.processEvents();
        if (!progress.grab().save(output.filePath(QStringLiteral("long-capture-paused-%1.png").arg(suffix)))) return 3;
    }
    QTextStream(stdout) << output.absolutePath() << '\n';
    return 0;
}
