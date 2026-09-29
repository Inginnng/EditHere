#include "controller.h"
#include "capturetoolbar.h"
#include "editor.h"
#include "explosion.h"
#include "overlay.h"
#include "settingsdialog.h"
#include "ui.h"
#include "fonts.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QMimeData>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFocusEvent>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFutureWatcher>
#include <QGraphicsEffect>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalSpy>
#include <QSystemTrayIcon>
#include <QStandardPaths>
#include <QScrollBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextCursor>
#include <QTimer>
#include <QVariantAnimation>
#include <QWheelEvent>
using namespace h2d;

// Where the crosshair is, which is where the window thinks the pointer is: a white
// cross nine pixels each way on a screen that has nothing else as light. It is read off
// the rendering rather than asked of the window, because what has to hold is what the
// user sees.
static QPoint crosshair(const QImage &image, int left, int right) {
    int row = -1, start = 0, longest = 0;
    for (int y = 100; y < 300; ++y) {
        int first = -1, width = 0;
        for (int x = left; x <= right; ++x) {
            const QColor pixel = image.pixelColor(x, y);
            const bool light = pixel.red() > 244 && pixel.green() > 244 && pixel.blue() > 244;
            if (light) {
                if (first < 0)
                    first = x;
                ++width;
            } else if (width > longest) {
                longest = width;
                row = y;
                start = first;
                width = 0;
                first = -1;
            }
        }
        if (width > longest) {
            longest = width;
            row = y;
            start = first;
        }
    }
    return {start + longest / 2, row};
}
// Puts the pointer on a point and makes sure the window is told about it. A move to
// where the pointer already is is not delivered at all, and a test that runs after
// another one that left the pointer on the same spot would otherwise be testing a
// window that never saw the pointer arrive.
static void movePointerTo(QWidget &overlay, QPoint at) {
    QTest::mouseMove(&overlay, at + QPoint(3, 3));
    QTest::mouseMove(&overlay, at);
}
// The same, on a point that is not a whole pixel. A mouse is put where the system can
// put it, and on a scaled display that is a fraction of a pixel off the pixel that was
// aimed at, which is the ordinary state of affairs rather than an oddity.
static void putPointerOn(QWidget &overlay, QPointF at) {
    for (const QPointF &spot : {at + QPointF(3, 3), at}) {
        QMouseEvent move(QEvent::MouseMove, spot, spot, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&overlay, &move);
    }
}
// The rectangles the window's own tools occupy. They are children, so they are painted
// over the magnifier, and the panel has to be placed clear of them.
static QVector<QRectF> magnifierTools(QWidget &overlay) {
    QVector<QRectF> tools;
    for (const auto &name : {QStringLiteral("captureToolbar"), QStringLiteral("captureSidebar")}) {
        const auto *tool = overlay.findChild<QWidget *>(name);
        if (tool != nullptr && tool->isVisible())
            tools.append(tool->geometry());
    }
    return tools;
}
// Where the magnifier panel lands for a pointer at `at`, asked of the one function the
// window itself uses, so the test cannot end up looking somewhere else.
static QRect magnifierPanel(QWidget &overlay, QPoint at, QSize window) {
    return magnifierPlacement(at, window, magnifierTools(overlay)).toRect();
}
// A table drawn on a plain screen: three columns and four rows of one-pixel lines.
// It is the ordinary shape that offers several blocks at one spot — a cell inside a
// row inside the whole table — which is what gives the wheel something to choose
// between. The lines are what the detector finds, so the blocks it reports are a
// pixel or two off these numbers.
static QImage tableShot() {
    QImage image(800, 600, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter p(&image);
    const QRect table(70, 60, 600, 400);
    for (int x = 0; x <= 3; ++x)
        p.fillRect(table.x() + x * table.width() / 3, table.y(), 1, table.height() + 1,
                   QColor("#404040"));
    for (int y = 0; y <= 4; ++y)
        p.fillRect(table.x(), table.y() + y * table.height() / 4, table.width() + 1, 1,
                   QColor("#404040"));
    return image;
}
// Two rectangles that name the same block: what the detector finds sits a pixel or two
// off the lines it was given, so "the same" has to mean "within a couple of pixels".
static bool sameBlock(const QRect &a, const QRect &b) {
    return std::abs(a.x() - b.x()) <= 4 && std::abs(a.y() - b.y()) <= 4 &&
           std::abs(a.width() - b.width()) <= 6 && std::abs(a.height() - b.height()) <= 6;
}
// The colour a recognised block is marked with, and the only reason a pixel that
// colour can appear inside a screen that has none of it.
static bool isAccentColour(const QColor &colour) {
    return std::abs(colour.red() - 0) < 40 && std::abs(colour.green() - 122) < 40 &&
           std::abs(colour.blue() - 255) < 40;
}
// How much of a rendering is painted darker than a limit on every channel. The panel
// draws its own background over whatever is behind it, so no single exact colour can
// name it; "a lot more dark pixels" can, and it survives the panel moving.
static int countDark(const QImage &image, int limit) {
    int found = 0;
    for (int row = 0; row < image.height(); ++row)
        for (int column = 0; column < image.width(); ++column) {
            const QColor pixel = image.pixelColor(column, row);
            if (pixel.red() < limit && pixel.green() < limit && pixel.blue() < limit)
                ++found;
        }
    return found;
}

// Read JSON from clipboard — supports both file-URL (new copy behavior) and text (legacy).
static QJsonDocument clipboardJson(QJsonParseError *error = nullptr) {
    const auto *mime = QApplication::clipboard()->mimeData();
    if (!mime) return QJsonDocument();
    const auto urls = mime->urls();
    if (!urls.isEmpty()) {
        QFile file(urls.first().toLocalFile());
        if (file.open(QIODevice::ReadOnly)) return QJsonDocument::fromJson(file.readAll(), error);
    }
    return QJsonDocument::fromJson(mime->text().toUtf8(), error);
}

class UiTests : public QObject {
    Q_OBJECT
  private:
    void artifact(QWidget &widget, const QString &name) {
        const QString folder = qEnvironmentVariable("H2D_TEST_ARTIFACTS");
        if (!folder.isEmpty()) {
            QDir().mkpath(folder);
            QVERIFY(widget.grab().save(QDir(folder).filePath(name)));
        }
    }
    QPushButton *toolButton(Editor &editor, const QString &tooltip) {
        for (auto button : editor.findChildren<QPushButton *>())
            if (button->toolTip() == tooltip)
                return button;
        return nullptr;
    }
    Document gridDocument() {
        QImage image(800, 600, QImage::Format_ARGB32);
        image.fill(QColor("#f3f4f7"));
        QPainter painter(&image);
        const QVector<QColor> colors{QColor("#4676c9"), QColor("#4c967c"), QColor("#c67453"),
                                     QColor("#9b70be"), QColor("#557e89"), QColor("#b69848")};
        painter.setFont(QFont(qApp->font().family(), 26, QFont::DemiBold));
        QVector<Candidate> candidates;
        auto candidate = [&](QRect rect, const QString &label) {
            auto target = manualTarget();
            target["source"] = "vision";
            target["label"] = label;
            target["method"] = "test-grid";
            candidates.append({rect, target});
        };
        for (int i = 0; i < 6; ++i) {
            const QRect cell(60 + (i % 2) * 120, 60 + (i / 2) * 90, 120, 90);
            painter.fillRect(cell, colors[i]);
            painter.setPen(Qt::white);
            painter.drawText(cell, Qt::AlignCenter, QString::number(i + 1));
            candidate(cell, QString::number(i + 1));
        }
        painter.end();
        candidate({60, 150, 240, 90}, "34");
        candidate({60, 150, 240, 180}, "3456");
        candidate({60, 60, 240, 270}, "整个表格");
        auto document = fromImage(image, "demo", "组件布局回归示例");
        document.candidates = candidates;
        return document;
    }
    QPoint canvasPoint(LayoutCanvas *canvas, QPointF imagePoint) {
        return {qRound(imagePoint.x() * canvas->zoom()), qRound(imagePoint.y() * canvas->zoom())};
    }
    void wheel(LayoutCanvas *canvas, QPointF imagePoint, int delta = 120) {
        const QPoint position = canvasPoint(canvas, imagePoint);
        QWheelEvent event(QPointF(position), QPointF(canvas->mapToGlobal(position)), QPoint(),
                          QPoint(0, delta), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(canvas, &event);
    }
    void drag(LayoutCanvas *canvas, QPointF start, QPointF end) {
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, canvasPoint(canvas, start));
        QTest::mouseMove(canvas, canvasPoint(canvas, end));
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, canvasPoint(canvas, end));
    }
    void finishInlineNote(Editor &editor, const QString &comment, const QString &id = {}) {
        QVERIFY(QApplication::activeModalWidget() == nullptr);
        const QString noteId = id.isEmpty() ? editor.document().notes.last().id : id;
        auto input = editor.findChild<QPlainTextEdit *>("noteText_" + noteId);
        QVERIFY(input);
        QTRY_VERIFY_WITH_TIMEOUT(input->isVisible(), 1500);
        QCOMPARE(input->property("noteId").toString(), noteId);
        input->setFocus();
        input->setPlainText(comment);
        auto surface = editor.layoutCanvas() && editor.layoutCanvas()->isVisible()
                           ? static_cast<QWidget *>(editor.layoutCanvas()) : static_cast<QWidget *>(editor.canvas());
        surface->setFocus();
        QTRY_VERIFY(!input->hasFocus());
        QTest::qWait(20); // Finish the queued focus-out transaction before testing undo.
        QVERIFY(QApplication::activeModalWidget() == nullptr);
    }
  private slots:
    void initTestCase() {
        // Keep the save-failure test's file chooser inspectable through Qt on every platform.
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QStandardPaths::setTestModeEnabled(true);
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() == "offscreen") {
            const QString fonts = qEnvironmentVariable("WINDIR") + "/Fonts/";
            QVERIFY(QFontDatabase::addApplicationFont(fonts + "segoeui.ttf") >= 0);
            QVERIFY(QFontDatabase::addApplicationFont(fonts + "msyh.ttc") >= 0);
        }
#endif
        applyTheme();
        QVERIFY(QFontMetrics(qApp->font()).inFontUcs4('A'));
        QVERIFY(QFontMetrics(qApp->font()).inFontUcs4(0x4e2d));
    }

    void settingsAreAvailableFromTrayWithoutAScreenshot() {
        auto settings = defaultSettings();
        settings.shortcuts["capture"] = {};
        settings.captureOnStartup = false;
        Controller controller(nullptr, settings);
        controller.start(false);
        QTest::qWait(100);
        for (auto widget : QApplication::topLevelWidgets())
            QVERIFY(!qobject_cast<Overlay *>(widget));
        auto tray = controller.findChild<QSystemTrayIcon *>("edithereTray");
        QVERIFY(tray && tray->contextMenu());
        auto action = tray->contextMenu()->findChild<QAction *>("traySettings");
        QVERIFY(action);
        bool opened = false;
        QTimer::singleShot(80, &controller, [&] {
            auto dialog = qobject_cast<SettingsDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            opened = true;
            QVERIFY(!dialog->windowFlags().testFlag(Qt::WindowStaysOnTopHint));
            artifact(*dialog, "settings-shortcuts-light.png");
            auto theme = dialog->findChild<QComboBox *>("themeMode");
            QVERIFY(theme);
            dialog->reject();
        });
        QTimer::singleShot(2500, &controller, [] {
            if (auto dialog = QApplication::activeModalWidget())
                dialog->close();
        });
        action->trigger();
        QVERIFY(opened);
        QVERIFY(QApplication::activeModalWidget() == nullptr);
    }
    // Annotating a picture that already exists should not have to start with taking a
    // screenshot: the window opens empty and says what it is waiting for, and the
    // picture arrives by being dropped on it.
    void anEmptyAnnotationWindowSaysItIsWaitingForAPicture() {
        Editor editor;
        editor.setPreferences(defaultSettings());
        editor.openEmpty();
        QVERIFY(QTest::qWaitForWindowExposed(&editor));
        QVERIFY2(!editor.hasDocument(), "the window opens with nothing in it");
        auto *well = editor.findChild<QWidget *>("emptyWell");
        QVERIFY2(well != nullptr, "an empty window has to say so");
        auto *hint = editor.findChild<QLabel *>("emptyHint");
        QVERIFY2(hint != nullptr, "an empty window has to say what it is waiting for");
        QVERIFY2(!hint->text().isEmpty(), "the hint has to actually say something");
        QVERIFY2(well->isVisible(), "an empty window shows the hint");
        editor.resize(1240, 820);
        QTest::qWait(80);
        artifact(editor, "empty-window-check.png");
        // The drop is not the only way in: the button opens the same file chooser as
        // the tray entry, so the window is usable with nothing to drag into it.
        auto *import = editor.findChild<QPushButton *>("emptyImport");
        QVERIFY2(import != nullptr, "an empty window offers to open a picture");
        QVERIFY2(import->isVisible() && !import->text().isEmpty(),
                 "the button says what it opens");
        bool asked = false;
        QTimer::singleShot(60, &editor, [&] {
            if (auto dialog = QApplication::activeModalWidget()) {
                asked = true;
                dialog->close();
            }
        });
        import->click();
        QVERIFY2(asked, "the button opens the file chooser rather than doing nothing");
        // Once a picture is in it, the hint gets out of the way.
        editor.setDocument(gridDocument());
        QVERIFY(editor.hasDocument());
        QVERIFY2(!well->isVisible(), "the hint is only for a window with no picture");
        // Closing and opening empty again is the same state, not a leftover one.
        editor.openEmpty();
        QVERIFY(!editor.hasDocument());
        QVERIFY(well->isVisible());
        // A picture dropped on the window still arrives: the well covers the whole
        // viewport, so the drop has to keep travelling up to the window itself.
        editor.openEmpty();
        QMimeData data;
        data.setImageData(gridDocument().image);
        const QPoint at = editor.rect().center();
        QDragEnterEvent entering(at, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(editor.focusWidget() ? editor.focusWidget() : &editor, &entering);
        QDropEvent dropping(at, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(well, &dropping);
        QVERIFY2(editor.hasDocument(), "a picture dropped on the empty window lands in it");
        editor.hide();
    }
    // A family asked for by name can be missing on a slimmed-down Windows install,
    // and the digits then fall back into a symbol font (REG-108). Every family the
    // app ends up using has to be one the machine actually has, or none at all.
    void aFontThatIsNotInstalledIsNeverAskedForByName() {
        QVERIFY2(resolveFontFamily({"EditHere Missing Font XYZ"}).isEmpty(),
                 "an unknown family resolves to nothing, not to some other face");
        const QStringList installed = QFontDatabase::families();
        if (!installed.isEmpty()) {
            const QString picked =
                resolveFontFamily({"EditHere Missing Font XYZ", installed.first()});
            QVERIFY2(picked == installed.first(),
                     "the first installed candidate wins over the unknown one");
        }
        applyInterfaceFont();
        const QString family = qApp->font().family();
        QVERIFY2(family.isEmpty() || installed.contains(family, Qt::CaseInsensitive),
                 "the application font names an installed family or leaves it to the platform");
        QVERIFY2(!latinFontFamily().isEmpty() || !installed.isEmpty(),
                 "the Latin face either resolves or is left to the platform default");
    }
    void defaultPreferencesApplyToNextImageAndExport() {
        Editor editor;
        auto preferences = defaultSettings();
        preferences.defaultTool = Canvas::Rectangle;
        preferences.fitImageOnOpen = false;
        preferences.embedOriginal = false;
        editor.setPreferences(preferences);
        editor.setDocument(gridDocument());
        QTest::qWait(100);
        QCOMPARE(editor.canvas()->mode(), Canvas::Rectangle);
        QCOMPARE(editor.canvas()->zoom(), 1.0);
        bool inspected = false;
        QTimer::singleShot(60, &editor, [&] {
            auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            auto embed = dialog->findChild<QCheckBox *>("embedOriginal");
            QVERIFY(embed);
            QVERIFY(!embed->isChecked());
            inspected = true;
            dialog->reject();
        });
        QTimer::singleShot(2000, &editor, [] {
            if (auto dialog = QApplication::activeModalWidget())
                dialog->close();
        });
        editor.exportJson();
        QVERIFY(inspected);
        preferences.defaultTool = Canvas::Point;
        editor.setPreferences(preferences);
        QCOMPARE(editor.canvas()->mode(), Canvas::Rectangle);
        editor.setDocument(gridDocument());
        QCOMPARE(editor.canvas()->mode(), Canvas::Point);
        editor.hide();
        SettingsDialog dialog(preferences);
        auto tabs = dialog.findChild<QTabWidget *>("settingsTabs");
        QVERIFY(tabs);
        dialog.show();
        tabs->setCurrentIndex(2);
        QTest::qWait(50);
        artifact(dialog, "settings-defaults-light.png");
        dialog.showUpdates();
        QTest::qWait(50);
        artifact(dialog, "settings-updates-light.png");
        auto status = dialog.findChild<QLabel *>("updateStatus");
        QVERIFY(status && status->text().contains("尚未"));
        applyTheme(ThemeMode::Dark);
        artifact(dialog, "settings-updates-dark.png");
        applyTheme(ThemeMode::System);
    }
    void shortcutChangesAndThemesPreserveFeedback() {
        auto document = gridDocument();
        document.layout = createLayout(document.image.size(), document.candidates);
        Note note;
        note.point = {120, 105};
        note.comment = "主题切换只改变界面，不改变原图与批注。";
        document.notes.append(note);
        Editor editor;
        editor.setDocument(document);
        editor.resize(1240, 820);
        QTest::qWait(80);
        const auto feedback = exportFeedback(editor.document(), true);
        auto keys = defaultSettings().shortcuts;
        keys["point"] = QKeySequence("Q");
        keys["smart"] = {};
        editor.setShortcuts(keys);
        auto point = editor.findChild<QShortcut *>("shortcutAction_point");
        auto smart = editor.findChild<QShortcut *>("shortcutAction_smart");
        QVERIFY(point && smart);
        QVERIFY(!smart->isEnabled());
        QCOMPARE(point->key(), QKeySequence("Q"));
        editor.activateWindow();
        editor.canvas()->setFocus();
        QTRY_VERIFY(editor.isActiveWindow());
        QTest::keyClick(editor.canvas(), Qt::Key_Q);
        QCOMPARE(editor.canvas()->mode(), Canvas::Point);
        QTest::keyClick(editor.canvas(), Qt::Key_R);
        QCOMPARE(editor.canvas()->mode(), Canvas::Rectangle);
        QTest::keyClick(editor.canvas(), Qt::Key_P);
        QCOMPARE(editor.canvas()->mode(), Canvas::Rectangle);
        QTest::keyClick(editor.canvas(), Qt::Key_B);
        QCOMPARE(editor.canvas()->mode(), Canvas::Rectangle);

        applyTheme(ThemeMode::Dark);
        QVERIFY(isDarkTheme());
        QVERIFY(qApp->palette().color(QPalette::Window).lightness() < 80);
        QVERIFY(qApp->palette().color(QPalette::WindowText).lightness() > 170);
        QTest::qWait(40);
        QCOMPARE(exportFeedback(editor.document(), true), feedback);
        artifact(editor, "editor-dark.png");
        QTimer::singleShot(70, &editor, [&] {
            auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            artifact(*dialog, "export-dark.png");
            dialog->reject();
        });
        editor.exportJson();
        auto darkSettings = defaultSettings();
        darkSettings.theme = ThemeMode::Dark;
        SettingsDialog settings(darkSettings, &editor);
        settings.show();
        QTest::qWait(40);
        artifact(settings, "settings-dark.png");
        auto tabs = settings.findChild<QTabWidget *>("settingsTabs");
        QVERIFY(tabs);
        tabs->setCurrentIndex(1);
        QTest::qWait(30);
        artifact(settings, "settings-appearance-dark.png");
        settings.hide();
        applyTheme(ThemeMode::Light);
        QVERIFY(!isDarkTheme());
        QVERIFY(qApp->palette().color(QPalette::Window).lightness() > 200);
        QCOMPARE(exportFeedback(editor.document(), true), feedback);
        editor.hide();
    }

    void pointAndFrame() {
        applyTheme();
        Editor editor;
        editor.setDocument(gridDocument());
        editor.resize(1240, 820);
        QTest::qWait(80);
        auto imageScroll = editor.findChild<QScrollArea *>("imageWell");
        auto notesPanel = editor.findChild<QWidget *>("notesPanel");
        QVERIFY(imageScroll && notesPanel && notesPanel->isVisible());
        QVERIFY(editor.document().notes.isEmpty());
        const QRect originalGeometry = editor.geometry();
        const QSize originalViewport = imageScroll->viewport()->size();
        const QRect originalNotesGeometry = notesPanel->geometry();
        artifact(editor, "empty-notes-layout.png");
        Canvas *canvas = editor.canvas();
        editor.findChild<QPushButton *>("mode_point")->click();
        QPoint target(qRound(200 * canvas->zoom()), qRound(100 * canvas->zoom()));
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, target);
        QCOMPARE(editor.document().notes.size(), 1);
        finishInlineNote(editor, "Refine the button spacing. 标题字重调轻，卡片间距保持一致。");
        QCOMPARE(editor.geometry(), originalGeometry);
        QCOMPARE(imageScroll->viewport()->size(), originalViewport);
        QCOMPARE(notesPanel->geometry(), originalNotesGeometry);
        QVERIFY(editor.document().notes.first().comment.startsWith("Refine"));
        QVERIFY((editor.document().notes[0].point - QPoint(200, 100)).manhattanLength() <= 2);

        // Releasing a frame stores its reusable region and immediately focuses an inline draft.
        editor.findChild<QPushButton *>("mode_rect")->click();
        QSignalSpy regions(canvas, &Canvas::regionRequested);
        QPoint start(qRound(400 * canvas->zoom()), qRound(250 * canvas->zoom()));
        QPoint end(qRound(700 * canvas->zoom()), qRound(590 * canvas->zoom()));
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(canvas, end);
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, end);
        QCOMPARE(regions.size(), 1);
        QCOMPARE(editor.document().notes.size(), 2);
        QVERIFY(editor.document().layout.has_value());
        auto frameInput = editor.findChild<QPlainTextEdit *>("noteText_" + editor.document().notes.last().id);
        QVERIFY(frameInput);
        QTRY_VERIFY(frameInput->hasFocus());
        QVERIFY(QApplication::activeModalWidget() == nullptr);
        QVERIFY(exportLayoutChanges(*editor.document().layout).isEmpty());
        finishInlineNote(editor, "这一块整体更轻盈，留白更从容。");
        QVERIFY(exportLayoutChanges(*editor.document().layout).isEmpty());
        QVERIFY(!editor.document().notes.last().isPoint);
        QVERIFY(editor.document().notes.last().rect.width() > 290);
        editor.findChild<QPushButton *>("mode_select")->click();
        artifact(editor, "editor.png");
        const auto note = editor.document().notes.last();
        QPoint handle(qRound((note.rect.x() + note.rect.width()) * canvas->zoom()),
                      qRound((note.rect.y() + note.rect.height()) * canvas->zoom()));
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, handle);
        QTest::mouseMove(canvas, handle + QPoint(20, 10));
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, handle + QPoint(20, 10));
        QVERIFY(editor.document().notes.last().rect.width() > note.rect.width());
        QTimer::singleShot(80, [this] {
            auto dialog = QApplication::activeModalWidget();
            QVERIFY(dialog);
            artifact(*dialog, "export-dialog.png");
            dialog->close();
        });
        editor.exportJson();
        editor.hide();
    }
    void emptyNoteHintSurvivesSynchronousDocumentChanges() {
        const auto emptyDocument = gridDocument();
        auto annotatedDocument = emptyDocument;
        Note note;
        note.isGlobal = true;
        note.comment = "保留这条全局意见。";
        annotatedDocument.notes.append(note);
        Editor editor;
        editor.setDocument(emptyDocument);

        // Stay in this event-loop turn: deleteLater() must not be needed to keep the hint unique.
        for (int i = 0; i < 3; ++i) {
            editor.setDocument(emptyDocument);
            QCOMPARE(editor.findChildren<QLabel *>("emptyNotes").size(), 1);
            QVERIFY(editor.findChild<QLabel *>("emptyNotes")->isVisible());
            editor.setDocument(annotatedDocument);
            QCOMPARE(editor.findChildren<QLabel *>("emptyNotes").size(), 1);
            QVERIFY(editor.findChild<QLabel *>("emptyNotes")->isHidden());
        }
        editor.setDocument(emptyDocument);
        QCOMPARE(editor.findChildren<QLabel *>("emptyNotes").size(), 1);
        QPointer<QLabel> hint = editor.findChild<QLabel *>("emptyNotes");
        QVERIFY(hint->isVisible());
        QCOMPARE(hint->text(), QString("圈出位置，或添加一条全局意见。"));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QTest::qWait(20);
        QCOMPARE(editor.findChildren<QLabel *>("emptyNotes").size(), 1);
        QVERIFY(hint && hint->isVisible());
        editor.hide();
    }
    void emptyNoteHintReturnsAfterDraftCancellationAndLastDeletion() {
        Editor editor;
        editor.setDocument(gridDocument());
        QTest::qWait(80);
        auto add = editor.findChild<QPushButton *>("addGlobalNote");
        QVERIFY(add);
        QCOMPARE(editor.findChildren<QLabel *>("emptyNotes").size(), 1);
        QPointer<QLabel> hint = editor.findChild<QLabel *>("emptyNotes");
        for (int i = 0; i < 3; ++i) {
            add->click();
            QCOMPARE(editor.document().notes.size(), 1);
            QCOMPARE(editor.findChildren<QLabel *>("emptyNotes").size(), 1);
            QVERIFY(hint && hint->isHidden());
            auto draft = editor.findChild<QPlainTextEdit *>("noteText_" + editor.document().notes.last().id);
            QVERIFY(draft);
            // Send Escape synchronously so all refreshes run before deferred widget deletion.
            QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QApplication::sendEvent(draft, &escape);
            QVERIFY(editor.hasDocument() && editor.isVisible());
            QVERIFY(editor.document().notes.isEmpty());
            QCOMPARE(editor.findChildren<QLabel *>("emptyNotes").size(), 1);
            QVERIFY(hint && hint->isVisible());
        }

        auto document = gridDocument();
        Note note;
        note.isGlobal = true;
        note.comment = "删除最后一条意见后应恢复空提示。";
        document.notes.append(note);
        editor.setDocument(document);
        QVERIFY(hint && hint->isHidden());
        auto input = editor.findChild<QPlainTextEdit *>("noteText_" + note.id);
        QVERIFY(input);
        QPushButton *remove = nullptr;
        for (auto button : input->parentWidget()->findChildren<QPushButton *>())
            if (button->toolTip() == "删除批注") remove = button;
        QVERIFY(remove);
        editor.canvas()->select(note.id);
        remove->click();
        QVERIFY(editor.document().notes.isEmpty());
        QCOMPARE(editor.findChildren<QLabel *>("emptyNotes").size(), 1);
        QVERIFY(hint && hint->isVisible());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QTest::qWait(20);
        QCOMPARE(editor.findChildren<QLabel *>("emptyNotes").size(), 1);
        QVERIFY(hint && hint->isVisible());
        QVERIFY(editor.findChildren<QWidget *>("noteCard").isEmpty());
        editor.hide();
    }
    void minimizeRestoresDocumentAndLayout() {
        auto document = gridDocument();
        document.layout = createLayout(document.image.size(), document.candidates);
        Note note;
        note.isGlobal = true;
        note.comment = "最小化后继续调整当前截图。";
        document.notes.append(note);
        Editor editor;
        editor.setDocument(document);
        editor.resize(1240, 820);
        QTest::qWait(80);
        auto minimize = editor.findChild<QPushButton *>("minimizeWindow");
        auto imageScroll = editor.findChild<QScrollArea *>("imageWell");
        QVERIFY(minimize && minimize->isVisible() && minimize->isEnabled() && imageScroll);
        QCOMPARE(minimize->toolTip(), QString("最小化"));
        editor.explode();
        QPointer<LayoutCanvas> canvas = editor.layoutCanvas();
        QVERIFY(canvas && canvas->isVisible());
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvasPoint(canvas, {100, 110}));
        QCOMPARE(canvas->selectionBounds(), QRectF(60, 60, 120, 90));
        canvas->transformSelection(QRectF(440, 60, 120, 90));
        QVERIFY(editor.document().dirty);
        QTest::qWait(30);
        const auto before = editor.document();
        const auto feedback = exportFeedback(before);
        const QRect geometry = editor.geometry();
        const QSize viewport = imageScroll->viewport()->size();
        const QStringList selection = canvas->selected();
        QSignalSpy hiddenToTray(&editor, &Editor::hiddenToTray);

        minimize->click();
        QTRY_VERIFY(editor.isMinimized());
        QVERIFY(editor.hasDocument());
        QCOMPARE(hiddenToTray.count(), 0);
        editor.showNormal();
        editor.activateWindow();
        QTRY_VERIFY(editor.isVisible() && !editor.isMinimized());
        QVERIFY(canvas && editor.layoutCanvas() == canvas && canvas->isVisible());
        QVERIFY(editor.explosionActive());
        QCOMPARE(editor.document().id, before.id);
        QCOMPARE(editor.document().png, before.png);
        QVERIFY(editor.document().notes == before.notes);
        QVERIFY(editor.document().layout == before.layout);
        QCOMPARE(exportFeedback(editor.document()), feedback);
        QVERIFY(editor.document().dirty);
        QCOMPARE(canvas->selected(), selection);
        QCOMPARE(editor.geometry(), geometry);
        QCOMPARE(imageScroll->viewport()->size(), viewport);
        QCOMPARE(hiddenToTray.count(), 0);

        minimize->click();
        QTRY_VERIFY(editor.isMinimized());
        const auto replacement = gridDocument();
        editor.setDocument(replacement);
        QTRY_VERIFY(editor.isVisible() && !editor.isMinimized());
        QCOMPARE(editor.document().id, replacement.id);
        QVERIFY(editor.document().notes.isEmpty());
        QVERIFY(editor.layoutCanvas() == nullptr);
        QCOMPARE(editor.findChildren<QLabel *>("emptyNotes").size(), 1);
        QVERIFY(editor.findChild<QLabel *>("emptyNotes")->isVisible());
        QCOMPARE(hiddenToTray.count(), 0);
        editor.hide();
    }
    void mainHeaderSettingsOpensTheExistingSettingsDialog() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString settingsFile = temporary.filePath("settings.json");
        auto settings = defaultSettings();
        settings.shortcuts["capture"] = {};
        settings.captureOnStartup = false;
        settings.checkUpdatesOnStartup = false;
        Controller controller(nullptr, settings, settingsFile);
        controller.start(false);
        auto tray = controller.findChild<QSystemTrayIcon *>("edithereTray");
        QVERIFY(tray && tray->contextMenu());
        auto editor = qobject_cast<Editor *>(tray->contextMenu()->parentWidget());
        QVERIFY(editor);
        editor->setDocument(gridDocument());
        editor->resize(1240, 820);
        QTest::qWait(60);
        auto settingsButton = editor->findChild<QPushButton *>("openSettings");
        auto minimize = editor->findChild<QPushButton *>("minimizeWindow");
        auto fullscreen = editor->findChild<QPushButton *>("fullscreenWindow");
        auto close = editor->findChild<QPushButton *>("closeWindow");
        QVERIFY(settingsButton && minimize && fullscreen && close);
        QVERIFY(settingsButton->isVisible() && settingsButton->isEnabled());
        QCOMPARE(settingsButton->toolTip(), QString("设置"));
        QVERIFY(minimize->geometry().right() < fullscreen->geometry().left());
        QVERIFY(fullscreen->geometry().right() < close->geometry().left());
        const auto before = exportFeedback(editor->document(), true);
        QSignalSpy requested(editor, &Editor::settingsRequested);
        int opened = 0;
        auto inspectSettings = [&] {
            auto dialog = qobject_cast<SettingsDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            auto tabs = dialog->findChild<QTabWidget *>("settingsTabs");
            QVERIFY(tabs);
            QCOMPARE(tabs->currentIndex(), 0);
            QVERIFY(!dialog->windowFlags().testFlag(Qt::WindowStaysOnTopHint));
            ++opened;
            dialog->reject();
        };
        QTimer::singleShot(60, &controller, inspectSettings);
        QTimer::singleShot(2000, &controller, [] {
            if (auto modal = QApplication::activeModalWidget()) modal->close();
        });
        settingsButton->click();
        QCOMPARE(opened, 1);
        QCOMPARE(requested.count(), 1);
        QVERIFY(QApplication::activeModalWidget() == nullptr);
        QCOMPARE(exportFeedback(editor->document(), true), before);
        QVERIFY(!QFileInfo::exists(settingsFile));

        QTimer::singleShot(60, &controller, [&] {
            auto menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            QVERIFY(menu);
            auto action = menu->findChild<QAction *>("editorSettings");
            menu->close();
            QVERIFY(action && action->isEnabled());
            QTimer::singleShot(60, &controller, inspectSettings);
            action->trigger();
        });
        QTimer::singleShot(2000, &controller, [] {
            if (auto popup = QApplication::activePopupWidget()) popup->close();
            if (auto modal = QApplication::activeModalWidget()) modal->close();
        });
        QTest::mouseClick(editor->canvas(), Qt::RightButton, Qt::NoModifier, QPoint(40, 40));
        QCOMPARE(opened, 2);
        QCOMPARE(requested.count(), 2);
        QCOMPARE(exportFeedback(editor->document(), true), before);
        QVERIFY(!QFileInfo::exists(settingsFile));
        artifact(*editor, "main-header-controls.png");
        editor->hide();
    }
    void fullscreenPreservesTheEditingSession_data() {
        QTest::addColumn<bool>("explosion");
        QTest::addColumn<bool>("maximized");
        QTest::newRow("annotation-normal") << false << false;
        QTest::newRow("explosion-normal") << true << false;
        QTest::newRow("annotation-maximized") << false << true;
        QTest::newRow("explosion-maximized") << true << true;
    }
    void fullscreenPreservesTheEditingSession() {
        QFETCH(bool, explosion);
        QFETCH(bool, maximized);
        auto document = gridDocument();
        document.layout = createLayout(document.image.size(), document.candidates);
        Note note;
        note.isGlobal = true;
        note.comment = "全屏前后的布局和意见保持一致。";
        document.notes.append(note);
        Editor editor;
        editor.setDocument(document);
        editor.resize(1100, 720);
        if (maximized) editor.showMaximized();
        QTest::qWait(60);
        if (maximized) QTRY_VERIFY(editor.isMaximized());
        if (explosion) {
            editor.explode();
            auto canvas = editor.layoutCanvas();
            QVERIFY(canvas && canvas->isVisible());
            QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvasPoint(canvas, {100, 110}));
            canvas->transformSelection(QRectF(440, 60, 120, 90));
        }
        auto plus = toolButton(editor, "放大");
        auto fullscreen = editor.findChild<QPushButton *>("fullscreenWindow");
        auto imageScroll = editor.findChild<QScrollArea *>("imageWell");
        QVERIFY(plus && fullscreen && imageScroll);
        for (int i = 0; editor.canvas()->zoom() < 2.0 && i < 16; ++i) plus->click();
        QTest::qWait(30);
        auto horizontal = imageScroll->horizontalScrollBar();
        auto vertical = imageScroll->verticalScrollBar();
        QVERIFY(horizontal->maximum() > 240 && vertical->maximum() > 200);
        horizontal->setValue(240);
        vertical->setValue(200);
        const QPoint offsets(horizontal->value(), vertical->value());
        const QRect geometry = editor.geometry();
        const auto before = editor.document();
        const auto feedback = exportFeedback(before, true);
        const double zoom = editor.canvas()->zoom();
        QPointer<LayoutCanvas> layoutCanvas = editor.layoutCanvas();
        const QStringList selection = layoutCanvas ? layoutCanvas->selected() : QStringList();
        QSignalSpy hiddenToTray(&editor, &Editor::hiddenToTray);
        QCOMPARE(fullscreen->toolTip(), QString("全屏"));

        fullscreen->click();
        QTRY_VERIFY(editor.isFullScreen());
        QVERIFY(fullscreen->isChecked() && fullscreen->isVisible());
        QCOMPARE(fullscreen->toolTip(), QString("退出全屏"));
        QVERIFY(editor.findChild<QPushButton *>("openSettings")->isVisible());
        QVERIFY(editor.findChild<QPushButton *>("closeWindow")->isVisible());
        QCOMPARE(editor.canvas()->zoom(), zoom);
        QCOMPARE(editor.explosionActive(), explosion);
        QVERIFY(!editor.windowFlags().testFlag(Qt::WindowStaysOnTopHint));
        if (explosion) {
            QVERIFY(layoutCanvas && editor.layoutCanvas() == layoutCanvas);
            QCOMPARE(layoutCanvas->zoom(), zoom);
            QCOMPARE(layoutCanvas->selected(), selection);
        }
        horizontal->setValue(0);
        vertical->setValue(0);
        fullscreen->click();
        QTRY_VERIFY(!editor.isFullScreen());
        QTRY_COMPARE(editor.isMaximized(), maximized);
        if (!maximized) QTRY_COMPARE(editor.geometry(), geometry);
        QTRY_COMPARE(QPoint(horizontal->value(), vertical->value()), offsets);
        QVERIFY(!fullscreen->isChecked());
        QCOMPARE(fullscreen->toolTip(), QString("全屏"));
        QCOMPARE(editor.canvas()->zoom(), zoom);
        QVERIFY(editor.document().layout == before.layout);
        QVERIFY(editor.document().notes == before.notes);
        QCOMPARE(editor.document().dirty, before.dirty);
        QCOMPARE(exportFeedback(editor.document(), true), feedback);
        QCOMPARE(hiddenToTray.count(), 0);

        fullscreen->click();
        QTRY_VERIFY(editor.isFullScreen());
        editor.activateWindow();
        QTRY_VERIFY(editor.isActiveWindow());
        if (!explosion && !maximized) {
            // Escape belongs to the active guide or note editor before the window control.
            editor.showGuide();
            auto next = editor.findChild<QPushButton *>("guideNext");
            QVERIFY(next && editor.guideActive());
            QTest::keyClick(next, Qt::Key_Escape);
            QVERIFY(!editor.guideActive() && editor.isFullScreen());
            auto add = editor.findChild<QPushButton *>("addGlobalNote");
            QVERIFY(add);
            add->click();
            auto input = editor.findChild<QPlainTextEdit *>("noteText_" + editor.document().notes.last().id);
            QVERIFY(input);
            QTRY_VERIFY(input->hasFocus());
            QTest::keyClick(input, Qt::Key_Escape);
            QVERIFY(editor.isFullScreen());
            QTRY_COMPARE(editor.document().notes.size(), before.notes.size());
            artifact(editor, "fullscreen-annotation.png");
        }
        QWidget *surface = explosion ? static_cast<QWidget *>(editor.layoutCanvas())
                                     : static_cast<QWidget *>(editor.canvas());
        surface->setFocus();
        QTest::keyClick(surface, Qt::Key_Escape);
        QTRY_VERIFY(!editor.isFullScreen());
        QTRY_COMPARE(editor.isMaximized(), maximized);
        QTRY_COMPARE(QPoint(horizontal->value(), vertical->value()), offsets);
        QCOMPARE(editor.explosionActive(), explosion);
        QCOMPARE(editor.document().id, before.id);
        QCOMPARE(editor.document().png, before.png);
        QVERIFY(editor.document().layout == before.layout);
        QVERIFY(editor.document().notes == before.notes);
        QCOMPARE(exportFeedback(editor.document(), true), feedback);
        QCOMPARE(hiddenToTray.count(), 0);
        QVERIFY(editor.isVisible() && editor.hasDocument());
        editor.hide();
    }
    void middleDraggingPansTheImageWithoutEditing_data() {
        QTest::addColumn<bool>("explosion");
        QTest::addColumn<bool>("blankViewport");
        QTest::addColumn<bool>("fitted");
        for (bool explosion : {false, true})
            for (bool blank : {false, true})
                for (bool fitted : {false, true}) {
                    const QByteArray name = QByteArray(explosion ? "explosion" : "annotation") +
                        (blank ? "-blank-viewport" : "-canvas") + (fitted ? "-fitted" : "-zoomed");
                    QTest::newRow(name.constData()) << explosion << blank << fitted;
                }
    }
    void middleDraggingPansTheImageWithoutEditing() {
        QFETCH(bool, explosion);
        QFETCH(bool, blankViewport);
        QFETCH(bool, fitted);
        auto document = gridDocument();
        if (blankViewport) {
            QImage image(1600, 80, QImage::Format_ARGB32);
            image.fill(QColor("#f3f4f7"));
            document = fromImage(image, "demo", "宽图画布留白");
            document.candidates.append({QRect(60, 10, 120, 50), manualTarget()});
        }
        document.layout = createLayout(document.image.size(), document.candidates);
        Note note;
        note.isGlobal = true;
        note.comment = "拖动视野不会改变组件位置。";
        document.notes.append(note);
        Editor editor;
        auto preferences = defaultSettings();
        preferences.fitImageOnOpen = false;
        editor.setPreferences(preferences);
        editor.setDocument(document);
        editor.resize(1100, 720);
        QTest::qWait(60);
        if (explosion) {
            editor.explode();
            auto canvas = editor.layoutCanvas();
            QVERIFY(canvas);
            const QPointF target = blankViewport ? QPointF(100, 35) : QPointF(100, 110);
            QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvasPoint(canvas, target));
            QVERIFY(!canvas->selected().isEmpty());
        }
        auto plus = toolButton(editor, "放大");
        auto scroll = editor.findChild<QScrollArea *>("imageWell");
        QVERIFY(plus && scroll);
        if (fitted) editor.fit();
        else for (int i = 0; editor.canvas()->zoom() < 2.0 && i < 16; ++i) plus->click();
        QTest::qWait(30);
        auto horizontal = scroll->horizontalScrollBar();
        auto vertical = scroll->verticalScrollBar();
        QWidget *canvas = explosion ? static_cast<QWidget *>(editor.layoutCanvas())
                                    : static_cast<QWidget *>(editor.canvas());
        QCOMPARE(scroll->widget(), canvas);
        if (!fitted) {
            horizontal->setValue(240);
            if (!blankViewport) vertical->setValue(200);
        } else {
            QVERIFY(canvas->width() <= scroll->viewport()->width());
            QVERIFY(canvas->height() <= scroll->viewport()->height());
        }
        const QPoint beforeOffset(horizontal->value(), vertical->value());
        const QPoint beforePosition = canvas->pos();
        const QRect geometry = editor.geometry();
        const auto before = editor.document();
        const auto feedback = exportFeedback(before, true);
        const double zoom = editor.canvas()->zoom();
        const QStringList selection = explosion ? editor.layoutCanvas()->selected() : QStringList(editor.canvas()->selected());
        QWidget *surface = blankViewport ? scroll->viewport() : canvas;
        const QPoint viewportPosition = blankViewport ? QPoint(140, scroll->viewport()->height() - 20)
                                                      : QPoint(160, 160);
        const QPoint start = scroll->viewport()->mapToGlobal(viewportPosition);
        if (blankViewport) QVERIFY(!canvas->rect().contains(canvas->mapFromGlobal(start)));
        else QVERIFY(canvas->rect().contains(canvas->mapFromGlobal(start)));
        auto mouse = [&](QEvent::Type type, const QPoint &global, Qt::MouseButton button,
                         Qt::MouseButtons buttons) {
            QMouseEvent event(type, QPointF(surface->mapFromGlobal(global)), QPointF(global),
                              button, buttons, Qt::NoModifier);
            QApplication::sendEvent(surface, &event);
        };
        mouse(QEvent::MouseButtonPress, start, Qt::MiddleButton, Qt::MiddleButton);
        // Continue the same gesture until the entire image has passed every viewport edge.
        const int farX = canvas->width() + scroll->viewport()->width() + 300;
        const int farY = canvas->height() + scroll->viewport()->height() + 300;
        const QVector<QPoint> offsets{{farX, 0}, {-farX, 0}, {0, farY}, {0, -farY}, {130, -85}};
        for (const QPoint &offset : offsets) {
            mouse(QEvent::MouseMove, start + offset, Qt::NoButton, Qt::MiddleButton);
            QTRY_COMPARE(canvas->pos(), beforePosition + offset);
            QCOMPARE(QPoint(horizontal->value(), vertical->value()), beforeOffset - offset);
            if (offset.x() == farX) QVERIFY(canvas->geometry().left() > scroll->viewport()->width());
            if (offset.x() == -farX) QVERIFY(canvas->geometry().right() < 0);
            if (offset.y() == farY) QVERIFY(canvas->geometry().top() > scroll->viewport()->height());
            if (offset.y() == -farY) QVERIFY(canvas->geometry().bottom() < 0);
        }
        const QPoint finalOffset = offsets.last();
        mouse(QEvent::MouseButtonRelease, start + finalOffset, Qt::MiddleButton, Qt::NoButton);
        QCOMPARE(editor.geometry(), geometry);
        QCOMPARE(editor.canvas()->zoom(), zoom);
        QCOMPARE(editor.explosionActive(), explosion);
        QCOMPARE(explosion ? editor.layoutCanvas()->selected() : QStringList(editor.canvas()->selected()), selection);
        QCOMPARE(editor.document().dirty, before.dirty);
        QVERIFY(editor.document().layout == before.layout);
        QVERIFY(editor.document().notes == before.notes);
        QCOMPARE(exportFeedback(editor.document(), true), feedback);
        // Releasing the middle button must end the pan, including on empty viewport space.
        mouse(QEvent::MouseMove, start + finalOffset + QPoint(30, 20), Qt::NoButton, Qt::NoButton);
        QCOMPARE(canvas->pos(), beforePosition + finalOffset);
        QCOMPARE(editor.geometry(), geometry);

        const QPoint pannedPosition = canvas->pos();
        editor.explode();
        QCOMPARE(scroll->widget()->pos(), pannedPosition);
        QCOMPARE(editor.canvas()->zoom(), zoom);
        editor.explode();
        QCOMPARE(scroll->widget(), canvas);
        QCOMPARE(canvas->pos(), pannedPosition);
        QCOMPARE(editor.canvas()->zoom(), zoom);
        QCOMPARE(exportFeedback(editor.document(), true), feedback);
        QCOMPARE(editor.document().dirty, before.dirty);
        editor.fit();
        QTRY_VERIFY(qAbs(canvas->geometry().center().x() - scroll->viewport()->rect().center().x()) <= 1);
        QTRY_VERIFY(qAbs(canvas->geometry().center().y() - scroll->viewport()->rect().center().y()) <= 1);
        QVERIFY(canvas->width() <= scroll->viewport()->width());
        QVERIFY(canvas->height() <= scroll->viewport()->height());
        QCOMPARE(exportFeedback(editor.document(), true), feedback);
        editor.hide();
    }
    void wheelZoomKeepsThePixelUnderThePointer_data() {
        QTest::addColumn<bool>("explosion");
        QTest::addColumn<bool>("blankViewport");
        QTest::addColumn<int>("modifier");
        for (bool explosion : {false, true})
            for (bool blank : {false, true})
                for (int modifier : {int(Qt::NoModifier), int(Qt::ControlModifier), int(Qt::MetaModifier)}) {
                    const QByteArray name = QByteArray(explosion ? "explosion" : "annotation") +
                        (blank ? "-blank-viewport" : "-canvas") +
                        (modifier == Qt::ControlModifier ? "-ctrl" : modifier == Qt::MetaModifier ? "-meta" : "-plain");
                    QTest::newRow(name.constData()) << explosion << blank << modifier;
                }
    }
    void wheelZoomKeepsThePixelUnderThePointer() {
        QFETCH(bool, explosion);
        QFETCH(bool, blankViewport);
        QFETCH(int, modifier);
        auto document = gridDocument();
        document.layout = createLayout(document.image.size(), document.candidates);
        Editor editor;
        editor.setDocument(document);
        editor.resize(1100, 720);
        QTest::qWait(60);
        if (explosion) {
            editor.explode();
            auto layout = editor.layoutCanvas();
            QVERIFY(layout);
            QTest::mouseClick(layout, Qt::LeftButton, Qt::NoModifier, canvasPoint(layout, {100, 110}));
            QVERIFY(!layout->selected().isEmpty());
        }
        editor.fit();
        QTest::qWait(30);
        auto scroll = editor.findChild<QScrollArea *>("imageWell");
        QVERIFY(scroll);
        QWidget *canvas = explosion ? static_cast<QWidget *>(editor.layoutCanvas())
                                    : static_cast<QWidget *>(editor.canvas());
        QCOMPARE(scroll->widget(), canvas);
        // Start with an off-center camera: anchored zoom must preserve a deliberate pan.
        const QPoint start = canvas->mapToGlobal(canvas->rect().center());
        auto mouse = [&](QEvent::Type type, const QPoint &global, Qt::MouseButton button,
                         Qt::MouseButtons buttons) {
            QMouseEvent event(type, QPointF(canvas->mapFromGlobal(global)), QPointF(global),
                              button, buttons, Qt::NoModifier);
            QApplication::sendEvent(canvas, &event);
        };
        mouse(QEvent::MouseButtonPress, start, Qt::MiddleButton, Qt::MiddleButton);
        mouse(QEvent::MouseMove, start + QPoint(85, -45), Qt::NoButton, Qt::MiddleButton);
        mouse(QEvent::MouseButtonRelease, start + QPoint(85, -45), Qt::MiddleButton, Qt::NoButton);
        const double initialZoom = editor.canvas()->zoom();
        const QPoint initialPosition = canvas->pos();
        // Plain wheel uses empty image content; modifiers must zoom even over a selected component.
        const QPointF imagePoint = modifier == Qt::NoModifier ? QPointF(650.25, 450.75) : QPointF(100.25, 110.75);
        const QPointF cursor = blankViewport ? QPointF(8.25, 8.75)
            : QPointF(canvas->pos()) + imagePoint * initialZoom;
        QVERIFY(scroll->viewport()->rect().contains(cursor.toPoint()));
        if (blankViewport) QVERIFY(!QRectF(canvas->geometry()).contains(cursor));
        else QVERIFY(QRectF(canvas->geometry()).contains(cursor));
        const QPointF anchoredPixel = (cursor - QPointF(canvas->pos())) / initialZoom;
        const auto before = editor.document();
        const auto feedback = exportFeedback(before, true);
        const QStringList selection = explosion ? editor.layoutCanvas()->selected() : QStringList(editor.canvas()->selected());
        const QRect geometry = editor.geometry();
        bool largerThanViewport = false;
        for (int cycle = 0; cycle < 2; ++cycle) {
            for (int step = 0; step < 16; ++step) {
                QWidget *surface = blankViewport ? scroll->viewport() : canvas;
                const QPointF local = blankViewport ? cursor : cursor - QPointF(canvas->pos());
                const QPointF global = QPointF(scroll->viewport()->mapToGlobal(QPoint())) + cursor;
                const double oldZoom = editor.canvas()->zoom();
                QWheelEvent event(local, global, QPoint(), QPoint(0, step < 8 ? 120 : -120),
                                  Qt::NoButton, Qt::KeyboardModifiers(modifier), Qt::NoScrollPhase, false);
                QApplication::sendEvent(surface, &event);
                const double newZoom = editor.canvas()->zoom();
                if (step < 8) QVERIFY(newZoom > oldZoom);
                else QVERIFY(newZoom < oldZoom);
                const QPointF mapped = QPointF(canvas->pos()) + anchoredPixel * newZoom;
                QVERIFY2(qAbs(mapped.x() - cursor.x()) <= 1.2 && qAbs(mapped.y() - cursor.y()) <= 1.2,
                         qPrintable(QString("Cursor anchor drifted: expected (%1, %2), actual (%3, %4)")
                             .arg(cursor.x()).arg(cursor.y()).arg(mapped.x()).arg(mapped.y())));
                largerThanViewport |= canvas->width() > scroll->viewport()->width() &&
                                      canvas->height() > scroll->viewport()->height();
                if (explosion) QCOMPARE(editor.layoutCanvas()->zoom(), newZoom);
            }
            QVERIFY(qAbs(editor.canvas()->zoom() - initialZoom) < 1e-9);
            QVERIFY((canvas->pos() - initialPosition).manhattanLength() <= 2);
        }
        QVERIFY(largerThanViewport);
        QCOMPARE(editor.explosionActive(), explosion);
        QCOMPARE(explosion ? editor.layoutCanvas()->selected() : QStringList(editor.canvas()->selected()), selection);
        QCOMPARE(editor.document().dirty, before.dirty);
        QVERIFY(editor.document().layout == before.layout);
        QVERIFY(editor.document().notes == before.notes);
        QCOMPARE(exportFeedback(editor.document(), true), feedback);
        QCOMPARE(editor.geometry(), geometry);
        editor.hide();
    }
    void controllerActivationRestoresMinimizedDocument() {
        auto settings = defaultSettings();
        settings.shortcuts["capture"] = {};
        settings.captureOnStartup = false;
        settings.checkUpdatesOnStartup = false;
        Controller controller(nullptr, settings);
        controller.start(false);
        auto tray = controller.findChild<QSystemTrayIcon *>("edithereTray");
        QVERIFY(tray && tray->contextMenu());
        auto editor = qobject_cast<Editor *>(tray->contextMenu()->parentWidget());
        QVERIFY(editor);
        auto document = gridDocument();
        document.layout = createLayout(document.image.size(), document.candidates);
        document.layout->pieces[0].destination.moveLeft(440);
        document.dirty = true;
        Note note;
        note.isGlobal = true;
        note.comment = "恢复窗口后保留未保存的意见和布局。";
        document.notes.append(note);
        editor->setDocument(document);
        QTest::qWait(30);
        auto minimize = editor->findChild<QPushButton *>("minimizeWindow");
        QVERIFY(minimize && minimize->isVisible());
        QSignalSpy hiddenToTray(editor, &Editor::hiddenToTray);
        minimize->click();
        QTRY_VERIFY(editor->isMinimized());
        controller.activate();
        QTRY_VERIFY(editor->isVisible() && !editor->isMinimized());
        QCOMPARE(editor->document().id, document.id);
        QCOMPARE(editor->document().png, document.png);
        QVERIFY(editor->document().notes == document.notes);
        QVERIFY(editor->document().layout == document.layout);
        QVERIFY(editor->document().dirty);
        QCOMPARE(hiddenToTray.count(), 0);
        editor->hide();
    }
    void inlineNotesCommitOnBlurAndCancelEmptyDraft() {
        Editor editor;
        editor.setDocument(gridDocument());
        QTest::qWait(80);
        auto point = editor.findChild<QPushButton *>("mode_point");
        auto plus = toolButton(editor, "放大");
        auto undo = toolButton(editor, "撤销"), redo = toolButton(editor, "重做");
        QVERIFY(point && plus && undo && redo);
        point->click();
        auto canvas = editor.canvas();
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier,
                          QPoint(qRound(110 * canvas->zoom()), qRound(100 * canvas->zoom())));
        QCOMPARE(editor.document().notes.size(), 1);
        const QString id = editor.document().notes[0].id;
        auto input = editor.findChild<QPlainTextEdit *>("noteText_" + id);
        QVERIFY(input && input->hasFocus());
        QVERIFY(QApplication::activeModalWidget() == nullptr);
        input->setPlainText("  整体更轻盈，减少装饰。  ");
        QTest::mouseClick(plus, Qt::LeftButton); // An ordinary click outside the editor commits the draft.
        QTRY_VERIFY(!input->hasFocus());
        QTRY_COMPARE(editor.document().notes[0].comment, QString("整体更轻盈，减少装饰。"));
        QVERIFY(undo->isEnabled());
        undo->click();
        QVERIFY(editor.document().notes.isEmpty());
        redo->click();
        QCOMPARE(editor.document().notes.size(), 1);
        QCOMPARE(editor.document().notes[0].id, id);
        QCOMPARE(editor.document().notes[0].comment, QString("整体更轻盈，减少装饰。"));

        input = editor.findChild<QPlainTextEdit *>("noteText_" + id);
        QVERIFY(input);
        input->setFocus();
        input->setPlainText("文字修改应当作为一次撤销。");
        QTest::mouseClick(plus, Qt::LeftButton);
        QTRY_VERIFY(!input->hasFocus());
        QTest::qWait(20);
        undo->click();
        QCOMPARE(editor.document().notes[0].comment, QString("整体更轻盈，减少装饰。"));
        redo->click();
        QCOMPARE(editor.document().notes[0].comment, QString("文字修改应当作为一次撤销。"));

        const auto beforeEmpty = editor.document().notes;
        editor.findChild<QPushButton *>("addGlobalNote")->click();
        QCOMPARE(editor.document().notes.size(), 2);
        QVERIFY(editor.document().notes.last().isGlobal);
        QTest::mouseClick(plus, Qt::LeftButton);
        QTRY_COMPARE(editor.document().notes.size(), 1);
        QVERIFY(editor.document().notes == beforeEmpty);
        artifact(editor, "inline-note-editor.png");
        editor.hide();
    }
    void globalNotesRemainCompactAndExportWithoutCoordinates() {
        Editor editor;
        editor.setDocument(gridDocument());
        editor.resize(1240, 820);
        QTest::qWait(80);
        auto add = editor.findChild<QPushButton *>("addGlobalNote");
        QVERIFY(add);
        for (int i = 0; i < 8; ++i) {
            add->click();
            QCOMPARE(editor.document().notes.size(), i + 1);
            QVERIFY(editor.document().notes.last().isGlobal);
            finishInlineNote(editor, QString("整体意见 %1：留白更从容。").arg(i + 1));
        }
        auto notesPanel = editor.findChild<QWidget *>("notesPanel");
        auto scroll = notesPanel->findChild<QScrollArea *>();
        QVERIFY(scroll);
        scroll->verticalScrollBar()->setValue(0);
        QTest::qWait(20);
        const auto cards = notesPanel->findChildren<QWidget *>("noteCard");
        QCOMPARE(cards.size(), 8);
        int fullyVisible = 0;
        for (auto card : cards) {
            QVERIFY2(card->height() <= 105, "Short notes must not expand into tall fixed-height cards");
            const QRect area(card->mapTo(scroll->viewport(), QPoint()), card->size());
            if (scroll->viewport()->rect().contains(area)) ++fullyVisible;
        }
        QVERIFY2(fullyVisible >= 6, "The reserved sidebar should show more than five short notes at once");
        const auto feedback = exportFeedback(editor.document());
        const auto objects = feedback["objects"].toArray();
        QCOMPARE(objects.size(), 8);
        for (const auto &value : objects) {
            const auto obj = value.toObject();
            QVERIFY(obj["source"].isNull()); // 全局意见没有坐标源
            QVERIFY(obj["movements"].toArray().isEmpty());
            QCOMPARE(obj["annotations"].toArray().size(), 1);
        }
        QVERIFY(!editor.document().layout.has_value() ||
                exportLayoutChanges(*editor.document().layout).isEmpty());
        artifact(editor, "compact-global-notes.png");
        editor.hide();
    }
    void newGlobalNoteIsFocusedAndVisibleAfterMoreThanTenNotes() {
        for (bool explosion : {false, true}) {
            auto document = gridDocument();
            document.layout = createLayout(document.image.size(), document.candidates);
            for (int i = 0; i < 12; ++i) {
                Note note;
                note.isGlobal = true;
                note.comment = QString("整体意见 %1：保持视觉风格一致。").arg(i + 1);
                document.notes.append(note);
            }
            Editor editor;
            editor.setDocument(document);
            editor.resize(1240, 760);
            QTest::qWait(80);
            if (explosion) {
                editor.explode();
                auto layout = editor.layoutCanvas();
                QVERIFY(layout);
                QTest::mouseClick(layout, Qt::LeftButton, Qt::NoModifier, canvasPoint(layout, {100, 110}));
            }
            auto scroll = editor.findChild<QScrollArea *>("notesScroll");
            auto count = editor.findChild<QLabel *>("noteCount");
            auto add = editor.findChild<QPushButton *>("addGlobalNote");
            QVERIFY(scroll && count && add);
            QCOMPARE(count->text(), QString("批注 12 条"));
            QTRY_VERIFY(scroll->verticalScrollBar()->maximum() > 0);
            scroll->verticalScrollBar()->setValue(0);
            add->click();
            QCOMPARE(editor.document().notes.size(), 13);
            QCOMPARE(count->text(), QString("批注 13 条"));
            auto input = editor.findChild<QPlainTextEdit *>("noteText_" + editor.document().notes.last().id);
            QVERIFY(input);
            QTRY_VERIFY(input->hasFocus());
            auto cursorVisible = [&] {
                const QRect cursor(input->viewport()->mapTo(scroll->viewport(), input->cursorRect().topLeft()),
                                   input->cursorRect().size());
                return scroll->viewport()->rect().contains(cursor);
            };
            QTRY_VERIFY2(cursorVisible(), "A newly inserted note's actual text cursor must be inside the sidebar viewport");
            QVERIFY(scroll->verticalScrollBar()->value() > 0);
            finishInlineNote(editor, "新增意见：统一材质与色调。");
            QCOMPARE(editor.document().notes.last().comment, QString("新增意见：统一材质与色调。"));
            if (explosion) {
                auto inspector = editor.findChild<QScrollArea *>("componentInspectorScroll");
                QVERIFY(inspector && inspector->isVisible());
            }
            artifact(editor, explosion ? "new-note-after-twelve-explosion.png" : "new-note-after-twelve.png");
            editor.hide();
        }
    }
    void longNotesExpandFullyAndFoldWhenAnotherNoteIsAddedOrEdited() {
        Editor editor;
        editor.setDocument(gridDocument());
        editor.resize(1240, 760);
        QTest::qWait(80);
        auto add = editor.findChild<QPushButton *>("addGlobalNote");
        auto scroll = editor.findChild<QScrollArea *>("notesScroll");
        QVERIFY(add && scroll);
        auto foldFor = [&](const QString &id) -> QPushButton * {
            for (auto fold : editor.findChildren<QPushButton *>("foldNote"))
                if (fold->property("noteId").toString() == id) return fold;
            return nullptr;
        };
        auto cardFor = [&](const QString &id) -> QWidget * {
            for (auto card : editor.findChildren<QWidget *>("noteCard"))
                if (card->property("noteId").toString() == id) return card;
            return nullptr;
        };
        add->click();
        const QString shortId = editor.document().notes.last().id;
        finishInlineNote(editor, "第一行意见\n第二行意见", shortId);
        auto shortFold = foldFor(shortId);
        QVERIFY(shortFold && !shortFold->isVisible());
        add->click();
        const QString longId = editor.document().notes.last().id;
        auto input = editor.findChild<QPlainTextEdit *>("noteText_" + longId);
        auto fold = foldFor(longId);
        QVERIFY(input && fold);
        QTRY_VERIFY(input->hasFocus());
        QStringList lines;
        for (int i = 0; i < 42; ++i)
            lines.append(QString("第 %1 条：让留白更从容，保留自然质感与完整的设计说明。").arg(i + 1));
        const QString fullText = lines.join('\n');
        input->setPlainText(fullText);
        input->moveCursor(QTextCursor::End);
        QTRY_VERIFY(fold->isVisible());
        QCOMPARE(fold->text(), QString("收起"));
        QCOMPARE(input->verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
        QCOMPARE(input->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
        QTRY_COMPARE(input->verticalScrollBar()->maximum(), 0);
        QTRY_VERIFY(input->height() > scroll->viewport()->height());
        auto cursorVisible = [&] {
            const QRect cursor(input->viewport()->mapTo(scroll->viewport(), input->cursorRect().topLeft()),
                               input->cursorRect().size());
            return scroll->viewport()->rect().contains(cursor);
        };
        QTRY_VERIFY2(cursorVisible(), "Typing beyond the visible sidebar must scroll the outer list to the cursor");
        // Let all text-change reveal callbacks finish: cursor-only keyboard navigation must work too.
        QTest::qWait(40);
        QTest::keyClick(input, Qt::Key_Home, Qt::ControlModifier);
        QCOMPARE(input->textCursor().position(), 0);
        QTRY_VERIFY2(cursorVisible(), "Ctrl+Home must reveal the cursor through the outer notes list");
        QTest::qWait(40);
        QTest::keyClick(input, Qt::Key_End, Qt::ControlModifier);
        QCOMPARE(input->textCursor().position(), fullText.size());
        QTRY_VERIFY2(cursorVisible(), "Ctrl+End must reveal the cursor without changing any text");
        QCOMPARE(input->toPlainText(), fullText);
        QTRY_COMPARE(input->verticalScrollBar()->maximum(), 0);
        const int expandedHeight = input->height();
        QCOMPARE(input->toPlainText(), fullText);
        QCOMPARE(editor.document().notes.last().comment, fullText);
        QVERIFY(input->viewport()->graphicsEffect() && !input->viewport()->graphicsEffect()->isEnabled());
        artifact(editor, "long-note-expanded.png");

        fold->click();
        QTRY_COMPARE(fold->text(), QString("展开"));
        QVERIFY(input->height() < expandedHeight / 3);
        QVERIFY(input->height() <= input->fontMetrics().lineSpacing() * 3 + 16);
        QCOMPARE(input->verticalScrollBar()->value(), 0);
        QVERIFY(input->viewport()->graphicsEffect()->isEnabled());
        QCOMPARE(input->toPlainText(), fullText);
        const auto objs = exportFeedback(editor.document())["objects"].toArray();
        QCOMPARE(objs.last().toObject()["annotations"].toArray().last().toString(), fullText);
        artifact(editor, "long-note-collapsed.png");
        fold->click();
        QTRY_COMPARE(fold->text(), QString("收起"));
        QTRY_COMPARE(input->height(), expandedHeight);
        QTRY_COMPARE(input->verticalScrollBar()->maximum(), 0);
        QCOMPARE(input->toPlainText(), fullText);

        // Adding another note folds the previous one, without discarding text or moving data.
        add->click();
        const QString thirdId = editor.document().notes.last().id;
        auto third = editor.findChild<QPlainTextEdit *>("noteText_" + thirdId);
        QVERIFY(third);
        QTRY_VERIFY(third->hasFocus());
        QCOMPARE(fold->text(), QString("展开"));
        QVERIFY(input->viewport()->graphicsEffect()->isEnabled());
        finishInlineNote(editor, "新的一条整体意见。", thirdId);
        QCOMPARE(editor.document().notes[1].comment, fullText);
        auto longCard = cardFor(longId);
        auto shortCard = cardFor(shortId);
        QVERIFY(longCard && shortCard);
        QPushButton *editLongNote = nullptr, *editShortNote = nullptr;
        for (auto button : longCard->findChildren<QPushButton *>())
            if (button->toolTip() == "编辑批注") editLongNote = button;
        for (auto button : shortCard->findChildren<QPushButton *>())
            if (button->toolTip() == "编辑批注") editShortNote = button;
        QVERIFY(editLongNote && editShortNote);
        editLongNote->click();
        QTRY_VERIFY(input->hasFocus());
        QCOMPARE(fold->text(), QString("收起"));
        QTRY_COMPARE(input->height(), expandedHeight);
        editShortNote->click();
        auto shortInput = editor.findChild<QPlainTextEdit *>("noteText_" + shortId);
        QVERIFY(shortInput);
        QTRY_VERIFY(shortInput->hasFocus());
        QCOMPARE(fold->text(), QString("展开"));
        QVERIFY(!shortFold->isVisible());
        QCOMPARE(input->toPlainText(), fullText);
        QCOMPARE(editor.document().notes[1].comment, fullText);
        editor.hide();
    }
    void componentInspectorStaysBelowTheScrollableNotes() {
        auto document = gridDocument();
        document.layout = createLayout(document.image.size(), document.candidates);
        for (int i = 0; i < 12; ++i) {
            Note note;
            note.isGlobal = true;
            note.comment = QString("组件布局意见 %1：大小与间距保持统一。").arg(i + 1);
            document.notes.append(note);
        }
        Editor editor;
        editor.setDocument(document);
        editor.resize(1240, 820);
        QTest::qWait(80);
        auto notes = editor.findChild<QWidget *>("notesPanel");
        auto scroll = editor.findChild<QScrollArea *>("notesScroll");
        auto separator = editor.findChild<QWidget *>("inspectorSeparator");
        QVERIFY(notes && scroll && separator && !separator->isVisible());
        editor.explode();
        auto layout = editor.layoutCanvas();
        QVERIFY(layout);
        QTest::mouseClick(layout, Qt::LeftButton, Qt::NoModifier, canvasPoint(layout, {100, 110}));
        auto inspector = editor.findChild<QScrollArea *>("componentInspectorScroll");
        auto guides = editor.findChild<QCheckBox *>("layoutGuides");
        QVERIFY(inspector && guides);
        QCOMPARE(guides->text(), QString("显示分解框"));
        QTRY_VERIFY(inspector->isVisible() && separator->isVisible());
        QTest::qWait(30);
        const QRect scrollRect(scroll->mapTo(notes, QPoint()), scroll->size());
        const QRect separatorRect(separator->mapTo(notes, QPoint()), separator->size());
        const QRect inspectorRect(inspector->mapTo(notes, QPoint()), inspector->size());
        QVERIFY(scrollRect.bottom() < separatorRect.top());
        QVERIFY(separatorRect.bottom() < inspectorRect.top());
        QVERIFY(notes->height() - 1 - inspectorRect.bottom() <= 8);
        QVERIFY(separatorRect.height() <= 2 && separatorRect.width() >= notes->width() - 24);
        QTRY_VERIFY(scroll->verticalScrollBar()->maximum() > 0);
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
        QTest::qWait(30);
        QCOMPARE(QRect(inspector->mapTo(notes, QPoint()), inspector->size()), inspectorRect);
        QCOMPARE(QRect(separator->mapTo(notes, QPoint()), separator->size()), separatorRect);
        QVERIFY(scroll->viewport()->height() >= 180);
        for (auto label : notes->findChildren<QLabel *>("noteCoordinates")) {
            QVERIFY(!label->text().contains("【"));
            QVERIFY(!label->text().contains("】"));
        }
        artifact(editor, "bottom-component-inspector.png");
        editor.explode();
        QVERIFY(!inspector->isVisible() && !separator->isVisible());
        QCOMPARE(editor.document().notes.size(), 12);
        editor.hide();
    }
    void annotationVisibilityPreservesDataAndCopyIncludesMarks() {
        auto document = gridDocument();
        document.layout = createLayout(document.image.size(), document.candidates);
        Note note;
        note.point = {120, 80};
        note.comment = "保持自然、克制的视觉风格。";
        document.notes.append(note);
        Editor editor;
        editor.setDocument(document);
        editor.resize(1240, 820);
        QTest::qWait(80);
        editor.explode();
        auto layout = editor.layoutCanvas();
        QVERIFY(layout);
        QTest::mouseClick(layout, Qt::LeftButton, Qt::NoModifier, canvasPoint(layout, {100, 130}));
        layout->transformSelection(QRectF(440, 60, 120, 90));
        const auto feedback = exportFeedback(editor.document(), true);
        const auto state = *editor.document().layout;
        auto hide = editor.findChild<QPushButton *>("hideAnnotations");
        QVERIFY(hide);
        hide->click();
        QVERIFY(hide->isChecked());
        QVERIFY(!editor.canvas()->annotationsVisible());
        QVERIFY(!layout->annotationsVisible());
        QCOMPARE(exportFeedback(editor.document(), true), feedback);
        QVERIFY(editor.document().layout == state);
        editor.explode();
        QVERIFY(!editor.canvas()->annotationsVisible());
        QCOMPARE(exportFeedback(editor.document(), true), feedback);
        auto copy = editor.findChild<QPushButton *>("copyImage");
        QVERIFY(copy);
        copy->click();
        const auto expected = previewImage(editor.document()).convertToFormat(QImage::Format_ARGB32);
        QTRY_COMPARE(QApplication::clipboard()->image().convertToFormat(QImage::Format_ARGB32), expected);
        hide->click();
        QVERIFY(editor.canvas()->annotationsVisible());
        QCOMPARE(exportFeedback(editor.document(), true), feedback);
        editor.hide();
    }
    void blankViewportWheelZoomsBothSurfacesWithoutChangingLayout() {
        auto document = gridDocument();
        document.layout = createLayout(document.image.size(), document.candidates);
        Editor editor;
        editor.setDocument(document);
        editor.resize(1240, 820);
        QTest::qWait(80);
        auto scroll = editor.findChild<QScrollArea *>("imageWell");
        QVERIFY(scroll);
        const auto baseline = exportFeedback(editor.document());
        auto send = [&](QPoint delta) {
            const QPoint position(2, 2);
            QVERIFY(!scroll->widget()->rect().contains(scroll->widget()->mapFrom(scroll->viewport(), position)));
            QWheelEvent event(QPointF(position), QPointF(scroll->viewport()->mapToGlobal(position)), QPoint(),
                              delta, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(scroll->viewport(), &event);
        };
        double before = editor.canvas()->zoom();
        send({120, 0});
        QCOMPARE(editor.canvas()->zoom(), before);
        send({0, -120});
        QVERIFY(editor.canvas()->zoom() < before);
        editor.explode();
        before = editor.layoutCanvas()->zoom();
        send({0, -120});
        QVERIFY(editor.layoutCanvas()->zoom() < before);
        QCOMPARE(editor.layoutCanvas()->zoom(), editor.canvas()->zoom());
        QCOMPARE(exportFeedback(editor.document()), baseline);
        editor.hide();
    }
    void toolbarPreferencesKeepCoreToolsAndSettingsReachable() {
        Editor editor;
        editor.setDocument(gridDocument());
        editor.resize(1600, 900);
        QTest::qWait(80);
        const auto defaults = defaultSettings();
        for (const auto &definition : toolbarActionDefinitions()) {
            auto action = editor.findChild<QPushButton *>(definition.id);
            QVERIFY(action);
            QCOMPARE(action->isVisible(), defaults.toolbarActions.contains(definition.id));
            QCOMPARE(action->toolTip(), definition.label);
        }
        QVERIFY(!editor.findChild<QPushButton *>("componentTool"));
        QVERIFY(!editor.findChild<QPushButton *>("manualRegion"));
        auto separator = editor.findChild<QWidget *>("toolbarSeparator");
        auto collapse = editor.findChild<QPushButton *>("collapseNotes");
        auto save = editor.findChild<QPushButton *>("saveProject");
        QVERIFY(separator && collapse && save && separator->isVisible());
        const QRect separatorRect(separator->mapTo(&editor, QPoint()), separator->size());
        const QRect collapseRect(collapse->mapTo(&editor, QPoint()), collapse->size());
        const QRect saveRect(save->mapTo(&editor, QPoint()), save->size());
        QVERIFY(separatorRect.left() > collapseRect.right());
        QVERIFY(separatorRect.right() < saveRect.left());
        QVERIFY(separatorRect.width() <= 2 && separatorRect.height() >= 15);

        auto preferences = defaults;
        preferences.toolbarActions = {"copyJson", "capture", "fit"};
        editor.setPreferences(preferences);
        for (const auto &definition : toolbarActionDefinitions())
            QCOMPARE(editor.findChild<QPushButton *>(definition.id)->isVisible(),
                     preferences.toolbarActions.contains(definition.id));
        QSignalSpy captures(&editor, &Editor::captureRequested);
        editor.findChild<QPushButton *>("capture")->click();
        QCOMPARE(captures.size(), 1);
        auto imageWell = editor.findChild<QScrollArea *>("imageWell");
        QVERIFY(imageWell);
        editor.canvas()->zoomRequested(0.25);
        QCOMPARE(editor.canvas()->zoom(), 0.25);
        editor.findChild<QPushButton *>("fit")->click();
        QVERIFY(editor.canvas()->zoom() > 0.25);

        preferences.toolbarActions.clear();
        editor.setPreferences(preferences);
        for (const auto &id : {"mode_smart", "mode_point", "mode_rect", "mode_select", "collapseNotes", "moreActions"})
            QVERIFY(editor.findChild<QPushButton *>(id)->isVisible());
        for (const auto &name : {"撤销", "重做", "缩小", "放大"})
            QVERIFY(toolButton(editor, name)->isVisible());
        for (const auto &definition : toolbarActionDefinitions())
            QVERIFY(!editor.findChild<QPushButton *>(definition.id)->isVisible());

        // Hidden actions remain operational through the same menu; choosing them needs no hidden UI.
        auto triggerMore = [&](const QString &id) {
            bool selected = false;
            QTimer::singleShot(30, &editor, [&] {
                auto menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                QVERIFY(menu);
                for (const auto &definition : toolbarActionDefinitions()) {
                    auto action = menu->findChild<QAction *>("more_" + definition.id);
                    QVERIFY(action && action->isEnabled());
                    QCOMPARE(action->text(), definition.label);
                }
                auto action = menu->findChild<QAction *>("more_" + id);
                QVERIFY(action);
                selected = true;
                action->trigger();
                menu->close();
            });
            QTimer::singleShot(1500, &editor, [] { if (auto menu = QApplication::activePopupWidget()) menu->close(); });
            editor.findChild<QPushButton *>("moreActions")->click();
            QVERIFY(selected);
        };
        QApplication::clipboard()->clear();
        triggerMore("copyJson");
        QTRY_COMPARE(clipboardJson().object(),
                     exportFeedback(editor.document(), true));
        QVERIFY(QApplication::activeModalWidget() == nullptr);
        triggerMore("capture");
        QCOMPARE(captures.size(), 2);
        editor.canvas()->zoomRequested(0.25);
        triggerMore("fit");
        QVERIFY(editor.canvas()->zoom() > 0.25);
        QSignalSpy settingsRequested(&editor, &Editor::toolbarSettingsRequested);
        bool selected = false;
        QTimer::singleShot(30, &editor, [&] {
            auto menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            QVERIFY(menu);
            for (auto action : menu->actions()) if (action->text() == "自定义工具栏…") {
                selected = true;
                action->trigger();
                menu->close();
                return;
            }
            menu->close();
            QFAIL("Toolbar customization must remain available when all optional actions are hidden");
        });
        QTimer::singleShot(1500, &editor, [] { if (auto menu = QApplication::activePopupWidget()) menu->close(); });
        editor.findChild<QPushButton *>("moreActions")->click();
        QVERIFY(selected);
        QCOMPARE(settingsRequested.size(), 1);
        editor.setPreferences(defaults);
        for (const auto &definition : toolbarActionDefinitions())
            QCOMPARE(editor.findChild<QPushButton *>(definition.id)->isVisible(),
                     defaults.toolbarActions.contains(definition.id));
        editor.hide();
    }
    void primaryCopiesCompleteJsonAndSmallButtonOpensPreview() {
        Editor editor;
        editor.setDocument(gridDocument());
        editor.resize(1240, 820);
        QTest::qWait(80);
        auto copy = editor.findChild<QPushButton *>("copyJson");
        auto view = editor.findChild<QPushButton *>("exportJson");
        QVERIFY(copy && view && copy->isVisible() && view->isVisible());
        QCOMPARE(copy->text(), QString("复制 JSON"));
        QVERIFY(copy->property("primary").toBool());
        QVERIFY(view->text().isEmpty());
        QVERIFY(!view->property("primary").toBool());
        QCOMPARE(view->toolTip(), QString("查看 JSON"));
        QVERIFY(copy->width() > view->width() * 2);
        editor.findChild<QPushButton *>("addGlobalNote")->click();
        auto input = editor.findChild<QPlainTextEdit *>("noteText_" + editor.document().notes.last().id);
        QVERIFY(input);
        QTRY_VERIFY(input->hasFocus());
        input->setPlainText("  复制按钮应当提交正在编辑的意见，并包含完整原图。  ");
        QApplication::clipboard()->clear();
        copy->click();
        QVERIFY(QApplication::activeModalWidget() == nullptr);
        QCOMPARE(editor.document().notes.size(), 1);
        QCOMPARE(editor.document().notes[0].comment, QString("复制按钮应当提交正在编辑的意见，并包含完整原图。"));
        const auto expected = exportFeedback(editor.document(), true);
        QTRY_COMPARE(clipboardJson().object(), expected);
        const QString image = expected["image"].toString();
        QVERIFY(image.startsWith("data:image/png;base64,"));
        QCOMPARE(QByteArray::fromBase64(image.mid(QString("data:image/png;base64,").size()).toLatin1()),
                 editor.document().png);
        bool opened = false;
        QTimer::singleShot(60, &editor, [&] {
            auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            auto preview = dialog->findChild<QPlainTextEdit *>();
            auto embed = dialog->findChild<QCheckBox *>("embedOriginal");
            QVERIFY(preview && embed && embed->isChecked());
            auto json = QJsonDocument::fromJson(preview->toPlainText().toUtf8()).object();
            QVERIFY(json["image"].toString().startsWith("data:image/png;base64,"));
            json.remove("image"); // The readable preview abbreviates only the base64 value.
            auto withoutImage = expected;
            withoutImage.remove("image");
            QCOMPARE(json, withoutImage);
            opened = true;
            dialog->reject();
        });
        QTimer::singleShot(1800, &editor, [] { if (auto modal = QApplication::activeModalWidget()) modal->close(); });
        view->click();
        QVERIFY(opened);
        editor.hide();
    }
    void canvasIgnoresHorizontalAndZeroWheelEvents() {
        auto document = gridDocument();
        Canvas canvas;
        canvas.setDocument(&document);
        canvas.setMode(Canvas::Smart);
        canvas.show();
        QTest::qWait(20);
        const QPoint position(120, 195);
        auto sendWheel = [&](QPoint delta, Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                             Qt::ScrollPhase phase = Qt::NoScrollPhase) {
            QWheelEvent event(QPointF(position), QPointF(canvas.mapToGlobal(position)), QPoint(), delta,
                              Qt::NoButton, modifiers, phase, false);
            QApplication::sendEvent(&canvas, &event);
        };
        QSignalSpy hints(&canvas, &Canvas::hintChanged);
        QSignalSpy zooms(&canvas, &Canvas::zoomRequested);
        QSignalSpy edits(&canvas, &Canvas::editRequested);
        sendWheel({0, 120});
        QCOMPARE(hints.size(), 1);
        sendWheel({120, 0});
        sendWheel({}, Qt::NoModifier, Qt::ScrollEnd);
        QCOMPARE(hints.size(), 1);
        QVERIFY(zooms.isEmpty());
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, position);
        QCOMPARE(edits.size(), 1);
        QCOMPARE(qvariant_cast<Note>(edits.first().first()).rect, QRect(60, 150, 240, 90));

        sendWheel({120, 0}, Qt::ControlModifier);
        sendWheel({}, Qt::ControlModifier, Qt::ScrollEnd);
        canvas.setMode(Canvas::Point);
        sendWheel({120, 0});
        sendWheel({}, Qt::NoModifier, Qt::ScrollEnd);
        QVERIFY(zooms.isEmpty());
        sendWheel({0, -120});
        QCOMPARE(zooms.size(), 1);
        QVERIFY(zooms.first().first().toDouble() < canvas.zoom());
    }
    void explosionCancelsInterruptedGesturesAndPreservesSelectionOnFocusChange() {
        auto document = gridDocument();
        const auto baseline = createLayout(document.image.size(), document.candidates);
        LayoutCanvas canvas(document.image, baseline);
        LayoutInspector inspector(&canvas);
        canvas.show();
        QTest::qWait(20);
        QSignalSpy changes(&canvas, &LayoutCanvas::changed);
        const QPoint start(100, 110), end(140, 135);
        auto pointerMove = [&](QPoint position, Qt::MouseButtons buttons) {
            QMouseEvent event(QEvent::MouseMove, QPointF(position), QPointF(canvas.mapToGlobal(position)),
                              Qt::NoButton, buttons, Qt::NoModifier);
            QApplication::sendEvent(&canvas, &event);
        };
        auto loseFocus = [&](Qt::FocusReason reason) {
            QFocusEvent event(QEvent::FocusOut, reason);
            QApplication::sendEvent(&canvas, &event);
        };
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, start);
        pointerMove(end, Qt::LeftButton);
        QVERIFY(canvas.state() != baseline);
        QVERIFY(changes.isEmpty());
        loseFocus(Qt::ActiveWindowFocusReason);
        QVERIFY(canvas.state() == baseline);
        QVERIFY(canvas.selected().isEmpty());
        pointerMove(end + QPoint(50, 25), Qt::NoButton);
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, end);
        QVERIFY(canvas.state() == baseline);
        QVERIFY(changes.isEmpty());

        canvas.setDrawing(true);
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, start);
        pointerMove(end, Qt::LeftButton);
        loseFocus(Qt::ActiveWindowFocusReason);
        QVERIFY(!canvas.drawingMode());
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, end);
        QVERIFY(canvas.state() == baseline);
        QVERIFY(changes.isEmpty());

        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, start);
        const auto selected = canvas.selected();
        QVERIFY(!selected.isEmpty());
        loseFocus(Qt::MouseFocusReason);
        QCOMPARE(canvas.selected(), selected);
        QVERIFY(canvas.state() == baseline);
        auto x = inspector.findChild<QDoubleSpinBox *>("layoutX");
        QVERIFY(x && x->isEnabled());
        x->setValue(80);
        QVERIFY(QMetaObject::invokeMethod(x, "editingFinished", Qt::DirectConnection));
        QCOMPARE(canvas.selectionBounds().x(), 80.0);
        QCOMPARE(changes.size(), 1);
    }
    void explosionWaveRestartsAndFinishesWithoutTakingInput() {
        QWidget host;
        host.resize(480, 320);
        QPlainTextEdit input(&host);
        input.setGeometry(host.rect());
        ExplosionWave wave(&host);
        wave.setGeometry(host.rect());
        host.show();
        input.setFocus();
        QTRY_VERIFY(input.hasFocus());

        auto animation = wave.findChild<QVariantAnimation *>("explosionWaveAnimation");
        QVERIFY(animation);
        QVERIFY(animation->duration() > 0);
        QVERIFY(wave.isHidden());
        QCOMPARE(animation->state(), QAbstractAnimation::Stopped);
        QVERIFY(wave.testAttribute(Qt::WA_TransparentForMouseEvents));
        QCOMPARE(wave.focusPolicy(), Qt::NoFocus);
        QSignalSpy finished(animation, &QVariantAnimation::finished);

        wave.start();
        QVERIFY(wave.isVisible());
        QCOMPARE(animation->state(), QAbstractAnimation::Running);
        QVERIFY(input.hasFocus());
        const auto hit = host.childAt(host.rect().center());
        QVERIFY(hit && (hit == &input || input.isAncestorOf(hit)));
        animation->setCurrentTime(animation->duration() / 2);
        QVERIFY(animation->currentTime() > 0);

        wave.start();
        QCOMPARE(animation->currentTime(), 0);
        QCOMPARE(animation->state(), QAbstractAnimation::Running);
        QVERIFY(wave.isVisible());
        QVERIFY(input.hasFocus());
        QVERIFY(finished.isEmpty());

        // Advance near the end, then let the animation's own timer finish the lifecycle.
        animation->setCurrentTime(animation->duration() - 1);
        QTRY_VERIFY_WITH_TIMEOUT(wave.isHidden(), 1000);
        QCOMPARE(finished.size(), 1);
        QCOMPARE(animation->state(), QAbstractAnimation::Stopped);
        QSignalSpy frames(animation, &QVariantAnimation::valueChanged);
        QTest::qWait(60);
        QVERIFY(frames.isEmpty());
        QVERIFY(input.hasFocus());
    }
    void explosionWaveHidingCancelsTheAnimationAndCanRestart() {
        QWidget host;
        host.resize(480, 320);
        ExplosionWave wave(&host);
        wave.setGeometry(host.rect());
        host.show();
        auto animation = wave.findChild<QVariantAnimation *>("explosionWaveAnimation");
        QVERIFY(animation);
        QSignalSpy finished(animation, &QVariantAnimation::finished);

        wave.start();
        animation->setCurrentTime(animation->duration() / 3);
        wave.hide();
        QVERIFY(wave.isHidden());
        QCOMPARE(animation->state(), QAbstractAnimation::Stopped);
        const int stoppedTime = animation->currentTime();
        QSignalSpy frames(animation, &QVariantAnimation::valueChanged);
        QTest::qWait(60);
        QCOMPARE(animation->currentTime(), stoppedTime);
        QVERIFY(frames.isEmpty());
        QVERIFY(finished.isEmpty());

        wave.start();
        QVERIFY(wave.isVisible());
        QCOMPARE(animation->currentTime(), 0);
        QCOMPARE(animation->state(), QAbstractAnimation::Running);
        host.hide();
        QVERIFY(!wave.isVisible());
        QCOMPARE(animation->state(), QAbstractAnimation::Stopped);
        QVERIFY(finished.isEmpty());
    }
    void explosionSelectionTransformAndManualRegion() {
        auto document = gridDocument();
        document.layout = createLayout(document.image.size(), document.candidates);
        Editor editor;
        editor.setDocument(document);
        editor.resize(1240, 820);
        QTest::qWait(80);
        auto imageScroll = editor.findChild<QScrollArea *>("imageWell");
        auto explode = editor.findChild<QPushButton *>("explodeButton");
        QVERIFY(imageScroll && explode && explode->isEnabled());
        const QRect geometry = editor.geometry();
        const QSize viewport = imageScroll->viewport()->size();
        explode->click();
        auto canvas = editor.layoutCanvas();
        QVERIFY(canvas && editor.explosionActive() && explode->isChecked());
        QVERIFY(!editor.document().dirty);
        QVERIFY(QApplication::activeModalWidget() == nullptr);
        QCOMPARE(canvas->window(), &editor);
        QVERIFY(!editor.windowFlags().testFlag(Qt::WindowStaysOnTopHint));
        QTest::qWait(30);
        QCOMPARE(editor.geometry(), geometry);
        QCOMPARE(imageScroll->viewport()->size(), viewport);

        auto animation = editor.findChild<QVariantAnimation *>("explosionWaveAnimation");
        auto wave = editor.findChild<QWidget *>("explosionWave");
        QVERIFY(animation && wave && wave->isVisible());
        QVERIFY(wave->testAttribute(Qt::WA_TransparentForMouseEvents));
        QCOMPARE(animation->state(), QAbstractAnimation::Running);
        animation->pause();
        animation->setCurrentTime(400);
        artifact(editor, "inline-wave.png");
        animation->resume();

        const QPointF three(120, 195);
        QTest::mouseMove(canvas, canvasPoint(canvas, three));
        for (int i = 0; i < 3; ++i)
            wheel(canvas, three);
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvasPoint(canvas, three));
        QCOMPARE(canvas->selectionBounds(), QRectF(60, 60, 240, 270));

        auto clear = editor.findChild<QPushButton *>("clearLayoutSelection");
        QVERIFY(clear);
        clear->click();
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvasPoint(canvas, {100, 110}));
        QCOMPARE(canvas->selectionBounds(), QRectF(60, 60, 120, 90));
        const QStringList selection = canvas->selected();
        const QString firstId = selection.isEmpty() ? QString() : selection.first();

        auto before = canvas->selectionBounds();
        const QPointF east(before.right(), before.center().y());
        drag(canvas, east, east + QPointF(40, 0));
        auto afterEdge = canvas->selectionBounds();
        QVERIFY(afterEdge.width() > before.width() + 35);
        QVERIFY(qAbs(afterEdge.height() - before.height()) < 0.01);
        QCOMPARE(canvas->selected(), QStringList(firstId));

        const QPointF corner = afterEdge.bottomRight();
        drag(canvas, corner, corner + QPointF(48, 27));
        const auto afterCorner = canvas->selectionBounds();
        QVERIFY(afterCorner.width() > afterEdge.width());
        QVERIFY(qAbs(afterCorner.width() / afterCorner.height() - afterEdge.width() / afterEdge.height()) <
                0.001);
        wheel(canvas, afterCorner.center());
        const auto afterWheel = canvas->selectionBounds();
        QVERIFY(afterWheel.width() > afterCorner.width());
        QVERIFY(qAbs(afterWheel.width() / afterWheel.height() - afterCorner.width() / afterCorner.height()) <
                0.001);

        auto x = editor.findChild<QDoubleSpinBox *>("layoutX");
        QVERIFY(x && x->isEnabled());
        x->setValue(440.25);
        QVERIFY(QMetaObject::invokeMethod(x, "editingFinished", Qt::DirectConnection));
        QVERIFY(qAbs(canvas->selectionBounds().x() - 440.25) < 0.01);
        const auto moved = renderLayout(document.image, canvas->state());
        QCOMPARE(moved.pixelColor(120, 105).alpha(), 0);
        QVERIFY(moved.pixelColor(460, 105).alpha() > 0);

        const auto beforeManual = canvas->state();
        auto manual = editor.findChild<QPushButton *>("mode_rect");
        QVERIFY(manual);
        manual->click();
        QVERIFY(canvas->drawingMode());
        drag(canvas, {200, 165}, {270, 215});
        QCOMPARE(canvas->state().groups.size(), beforeManual.groups.size() + 1);
        QVERIFY(!canvas->selected().isEmpty());
        QVERIFY(!canvas->drawingMode());
        const auto afterManual = canvas->state();
        QCOMPARE(editor.document().notes.size(), 1);
        auto manualDraft = editor.findChild<QPlainTextEdit *>("noteText_" + editor.document().notes.last().id);
        QVERIFY(manualDraft);
        QTRY_VERIFY(manualDraft->hasFocus());
        canvas->setFocus();
        QTRY_VERIFY(editor.document().notes.isEmpty());
        auto undo = toolButton(editor, "撤销"), redo = toolButton(editor, "重做");
        QVERIFY(undo && redo && undo->isEnabled());
        undo->click();
        QVERIFY(canvas->state() == beforeManual);
        QVERIFY(redo->isEnabled());
        redo->click();
        QVERIFY(canvas->state() == afterManual);
        QVERIFY(editor.document().layout == afterManual);

        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier,
                          canvasPoint(canvas, layoutBounds(canvas->state(), firstId).bottomLeft() + QPointF(16, -16)));
        QCOMPARE(canvas->selected(), QStringList(firstId));
        animation->stop();
        wave->hide();
        artifact(editor, "inline-explosion.png");
        const QString folder = qEnvironmentVariable("H2D_TEST_ARTIFACTS");
        if (!folder.isEmpty())
            QVERIFY(renderLayout(document.image, canvas->state())
                        .save(QDir(folder).filePath("inline-result.png")));
        QTest::keyClick(canvas, Qt::Key_Escape);
        QVERIFY(canvas->selected().isEmpty());
        manual->click();
        QVERIFY(canvas->drawingMode());
        QTest::keyClick(canvas, Qt::Key_Escape);
        QVERIFY(!canvas->drawingMode());
        QVERIFY(!canvas->drawingMode());
        QVERIFY(editor.isVisible());
        QCOMPARE(editor.geometry(), geometry);
        QCOMPARE(imageScroll->viewport()->size(), viewport);
        editor.hide();
    }
    void editorRetainsInlineLayoutUntilScreenshotCloses() {
        auto document = gridDocument();
        document.layout = createLayout(document.image.size(), document.candidates);
        Note note;
        note.point = {120, 105};
        note.comment = "将第一格移到右侧，批注跟随组件。";
        document.notes.append(note);
        const auto originalNotes = document.notes;
        const auto initialLayout = *document.layout;
        Editor editor;
        editor.setDocument(document);
        editor.resize(1240, 820);
        QTest::qWait(80);
        auto imageScroll = editor.findChild<QScrollArea *>("imageWell");
        auto explode = editor.findChild<QPushButton *>("explodeButton");
        QVERIFY(imageScroll && explode && explode->isEnabled());
        const QRect geometry = editor.geometry();
        const QSize viewport = imageScroll->viewport()->size();
        explode->click();
        QPointer<LayoutCanvas> canvas = editor.layoutCanvas();
        QVERIFY(canvas && editor.explosionActive());
        QVERIFY(QApplication::activeModalWidget() == nullptr);
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvasPoint(canvas, {100, 110}));
        QCOMPARE(canvas->selectionBounds(), QRectF(60, 60, 120, 90));
        auto destination = canvas->selectionBounds();
        destination.moveLeft(440);
        canvas->transformSelection(destination);
        QVERIFY(editor.document().layout.has_value());
        QCOMPARE(editor.document().notes[0].point, QPoint(500, 105));
        QCOMPARE(editor.document().notes[0].comment, note.comment);
        const auto appliedNotes = editor.document().notes;
        const auto appliedLayout = *editor.document().layout;
        const auto feedback = exportFeedback(editor.document());
        QCOMPARE(feedback.keys(), QStringList({"annotationSpace", "objects"}));
        const auto objects = feedback["objects"].toArray();
        bool foundMovement = false;
        for (const auto &value : objects) {
            const auto obj = value.toObject();
            const auto movements = obj["movements"].toArray();
            if (!movements.isEmpty()) {
                foundMovement = true;
                QCOMPARE(movements.size(), 1);
                // 源坐标 = from，movements[0].to = to
                QCOMPARE(obj["source"].toObject(),
                         (QJsonObject{{"x1", 60.}, {"y1", 60.}, {"x2", 180.}, {"y2", 150.}}));
                QCOMPARE(movements[0].toObject()["to"].toObject(),
                         (QJsonObject{{"x1", 440.}, {"y1", 60.}, {"x2", 560.}, {"y2", 150.}}));
            }
        }
        QVERIFY(foundMovement);

        auto undo = toolButton(editor, "撤销"), redo = toolButton(editor, "重做");
        QVERIFY(undo && redo && undo->isEnabled());
        undo->click();
        QVERIFY(editor.document().layout == initialLayout);
        QVERIFY(canvas->state() == initialLayout);
        QVERIFY(editor.document().notes == originalNotes);
        QVERIFY(editor.explosionActive());
        QVERIFY(redo->isEnabled());
        redo->click();
        QVERIFY(editor.document().layout == appliedLayout);
        QVERIFY(canvas->state() == appliedLayout);
        QVERIFY(editor.document().notes == appliedNotes);

        // Replaying the decorative wave must preserve the edited result and the active selection.
        auto wave = editor.findChild<ExplosionWave *>("explosionWave");
        auto animation = editor.findChild<QVariantAnimation *>("explosionWaveAnimation");
        QVERIFY(wave && animation);
        canvas->setFocus();
        QTRY_VERIFY(canvas->hasFocus());
        const QStringList selected = canvas->selected();
        const bool dirty = editor.document().dirty;
        const auto feedbackBeforeWave = serializeFeedback(editor.document(), true);
        QSignalSpy layoutChanges(canvas, &LayoutCanvas::changed);
        wave->start();
        animation->setCurrentTime(animation->duration() / 2);
        wave->start();
        animation->setCurrentTime(animation->duration());
        QVERIFY(wave->isHidden());
        QVERIFY(canvas->hasFocus());
        QCOMPARE(canvas->selected(), selected);
        QVERIFY(layoutChanges.isEmpty());
        QVERIFY(editor.document().layout == appliedLayout);
        QVERIFY(canvas->state() == appliedLayout);
        QVERIFY(editor.document().notes == appliedNotes);
        QCOMPARE(editor.document().dirty, dirty);
        QCOMPARE(serializeFeedback(editor.document(), true), feedbackBeforeWave);

        QStringList pieceIds;
        for (const auto &piece : canvas->state().pieces)
            pieceIds.append(piece.id);
        explode->click();
        QVERIFY(!editor.explosionActive() && !explode->isChecked());
        QVERIFY(editor.canvas()->layoutPreview());
        QVERIFY(!canvas->isVisible());
        QVERIFY(editor.document().layout == appliedLayout);
        QVERIFY(editor.document().notes == appliedNotes);
        QTest::qWait(30);
        QCOMPARE(editor.geometry(), geometry);
        QCOMPARE(imageScroll->viewport()->size(), viewport);
        artifact(editor, "inline-result-preview.png");

        explode->click();
        QVERIFY(editor.explosionActive() && explode->isChecked());
        QCOMPARE(editor.layoutCanvas(), canvas.data());
        QVERIFY(canvas->state() == appliedLayout);
        QStringList resumedIds;
        for (const auto &piece : canvas->state().pieces)
            resumedIds.append(piece.id);
        QCOMPARE(resumedIds, pieceIds);
        QCOMPARE(editor.geometry(), geometry);
        QCOMPARE(imageScroll->viewport()->size(), viewport);

        bool discarded = false;
        QTimer dismiss;
        connect(&dismiss, &QTimer::timeout, &editor, [&] {
            if (auto prompt = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                if (auto discard = prompt->button(QMessageBox::Discard)) {
                    discarded = true;
                    discard->click();
                }
            }
        });
        QTimer::singleShot(3000, &editor, [] {
            if (auto active = QApplication::activeModalWidget())
                active->close();
        });
        dismiss.start(20);
        editor.close();
        dismiss.stop();
        QVERIFY(discarded);
        QVERIFY(!editor.isVisible());
        QVERIFY(!editor.hasDocument());
        QVERIFY(!editor.document().layout.has_value());
        QVERIFY(editor.layoutCanvas() == nullptr);
        QVERIFY(canvas.isNull());
        QVERIFY(!editor.explosionActive());
        editor.setDocument(gridDocument());
        QVERIFY(editor.hasDocument());
        QVERIFY(editor.layoutCanvas() == nullptr);
        QVERIFY(!editor.document().layout.has_value());
        editor.hide();
    }
    void explosionEditsAndAnnotationsShareResult() {
        auto document = gridDocument();
        document.layout = createLayout(document.image.size(), document.candidates);
        Editor editor;
        editor.setDocument(document);
        editor.resize(1240, 820);
        QTest::qWait(80);
        auto explode = editor.findChild<QPushButton *>("explodeButton");
        auto component = editor.findChild<QPushButton *>("mode_smart");
        auto imageScroll = editor.findChild<QScrollArea *>("imageWell");
        QVERIFY(explode && component && imageScroll);
        explode->click();
        auto layout = editor.layoutCanvas();
        QVERIFY(layout && editor.explosionActive() && explode->isChecked());
        layout->zoomRequested(1.0);
        QCOMPARE(layout->zoom(), 1.0);
        const QRect geometry = editor.geometry();
        const QSize viewport = imageScroll->viewport()->size();
        QTest::mouseClick(layout, Qt::LeftButton, Qt::NoModifier, canvasPoint(layout, {100, 110}));
        QCOMPARE(layout->selectionBounds(), QRectF(60, 60, 120, 90));
        const auto componentId = layout->selected();
        layout->transformSelection(QRectF(440, 60, 120, 90));
        const auto movedLayout = *editor.document().layout;
        QCOMPARE(renderLayout(document.image, movedLayout).pixelColor(120, 105).alpha(), 0);

        // Point and rectangle tools annotate the edited image without disabling explosion.
        QTest::keyClick(layout, Qt::Key_P);
        auto result = editor.canvas();
        QCOMPARE(result->mode(), Canvas::Point);
        QVERIFY(result->isVisible() && result->layoutPreview());
        QVERIFY(editor.explosionActive() && explode->isChecked());
        QVERIFY(component->isVisible() && !component->isChecked());
        QCOMPARE(result->zoom(), 1.0);
        QTest::mouseClick(result, Qt::LeftButton, Qt::NoModifier, QPoint(500, 80));
        QCOMPARE(editor.document().notes.size(), 1);
        finishInlineNote(editor, "标题希望更有编辑感。");
        QCOMPARE(editor.document().notes[0].point, QPoint(500, 80));
        QVERIFY(editor.document().layout == movedLayout);

        // The shared frame tool also starts its annotation immediately in explosion mode.
        editor.findChild<QPushButton *>("mode_rect")->click();
        QVERIFY(layout->isVisible() && layout->drawingMode());
        const auto beforeManualFeedback = exportLayoutChanges(*editor.document().layout);
        drag(layout, {450, 70}, {540, 125});
        QCOMPARE(editor.document().notes.size(), 2);
        auto frameInput = editor.findChild<QPlainTextEdit *>("noteText_" + editor.document().notes.last().id);
        QVERIFY(frameInput);
        QTRY_VERIFY(frameInput->hasFocus());
        QVERIFY(!layout->drawingMode());
        QCOMPARE(layout->selectionBounds(), QRectF(450, 70, 90, 55));
        QCOMPARE(exportLayoutChanges(*editor.document().layout), beforeManualFeedback);
        auto annotate = editor.findChild<QPushButton *>("annotateComponent");
        QVERIFY(annotate && annotate->isEnabled());
        finishInlineNote(editor, "这个区域的内容需要对齐。");
        QVERIFY(!editor.document().notes[1].isPoint);
        QCOMPARE(editor.document().notes[1].rect, QRect(450, 70, 90, 55));
        const auto firstNotes = editor.document().notes;
        const auto firstLayout = *editor.document().layout;
        const auto originalPieceCount = firstLayout.pieces.size();
        component->click();
        QCOMPARE(result->mode(), Canvas::Smart);
        QVERIFY(editor.explosionActive() && explode->isChecked());
        QCOMPARE(editor.geometry(), geometry);
        QCOMPARE(imageScroll->viewport()->size(), viewport);
        QTest::qWait(30);
        auto notesPanel = editor.findChild<QWidget *>("notesPanel");
        QVERIFY(notesPanel && notesPanel->isVisible());
        auto inspector = editor.findChild<QScrollArea *>("componentInspectorScroll");
        QVERIFY(inspector && inspector->isVisible() && notesPanel->isVisible());
        int visibleNoteCards = 0;
        for (auto card : notesPanel->findChildren<QWidget *>("noteCard"))
            if (card->isVisible())
                ++visibleNoteCards;
        // Orphan movements (movements without a linked note) also get sidebar cards.
        const auto markers = movementMarkers(*editor.document().layout, editor.document().notes);
        const int orphanCards = std::count_if(markers.begin(), markers.end(),
                                               [](const MovementMarker &m) { return m.noteIndex < 0; });
        QCOMPARE(visibleNoteCards, editor.document().notes.size() + orphanCards);
        artifact(editor, "result-annotations.png");

        // A selected annotation in the hidden result canvas must not be deleted in component mode.
        QCOMPARE(result->selected(), firstNotes.last().id);
        component->click();
        QCOMPARE(editor.layoutCanvas(), layout);
        QVERIFY(layout->isVisible() && component->isChecked());
        QTest::keyClick(layout, Qt::Key_Delete);
        QVERIFY(editor.document().notes == firstNotes);
        QVERIFY(editor.document().layout == firstLayout);

        // Continue component adjustment and carry both annotations with the same pixels.
        layout->clearSelection();
        QTest::mouseClick(layout, Qt::LeftButton, Qt::NoModifier, canvasPoint(layout, {450, 140}));
        QCOMPARE(layout->selected(), componentId);
        layout->transformSelection(QRectF(600, 240, 120, 90));
        const auto secondLayout = *editor.document().layout;
        const auto secondNotes = editor.document().notes;
        QCOMPARE(secondNotes[0].point, QPoint(660, 260));
        QCOMPARE(secondNotes[1].rect, QRect(610, 250, 90, 55));
        QCOMPARE(secondLayout.pieces.size(), originalPieceCount);
        auto undo = toolButton(editor, "撤销"), redo = toolButton(editor, "重做");
        QVERIFY(undo && redo && undo->isEnabled());
        undo->click();
        QVERIFY(editor.document().layout == firstLayout);
        QVERIFY(editor.document().notes == firstNotes);
        QVERIFY(editor.explosionActive() && explode->isChecked());
        redo->click();
        QVERIFY(editor.document().layout == secondLayout);
        QVERIFY(editor.document().notes == secondNotes);
        QVERIFY(editor.explosionActive() && explode->isChecked());

        // Annotating a selected component and editing a badge never alter the cuts.
        QTest::mouseClick(layout, Qt::LeftButton, Qt::NoModifier, canvasPoint(layout, {610, 320}));
        QCOMPARE(layout->selected(), componentId);
        QVERIFY(annotate && annotate->isEnabled());
        QSignalSpy geometryChanged(layout, &LayoutCanvas::changed);
        annotate->click();
        finishInlineNote(editor, "保持这个组件的新位置。");
        QCOMPARE(editor.document().notes.size(), 3);
        QVERIFY(!editor.document().notes[2].isPoint);
        QCOMPARE(editor.document().notes[2].rect, QRect(600, 240, 120, 90));
        QVERIFY(editor.explosionActive() && explode->isChecked() && layout->isVisible());
        QVERIFY(layout->state() == secondLayout);
        QCOMPARE(layout->selected(), componentId);
        const auto beforeTextEdit = editor.document().notes;
        QTest::mouseClick(layout, Qt::LeftButton, Qt::NoModifier, canvasPoint(layout, {660, 260}));
        finishInlineNote(editor, "标题字号改成 24 像素。", firstNotes[0].id);
        QCOMPARE(editor.document().notes.size(), 3);
        QCOMPARE(editor.document().notes[0].comment, QString("标题字号改成 24 像素。"));
        QCOMPARE(editor.document().notes[0].point, QPoint(660, 260));
        QCOMPARE(geometryChanged.count(), 0);
        QVERIFY(layout->state() == secondLayout);
        const auto afterTextEdit = editor.document().notes;
        undo->click();
        QVERIFY(editor.document().notes == beforeTextEdit);
        QVERIFY(editor.document().layout == secondLayout);
        redo->click();
        QVERIFY(editor.document().notes == afterTextEdit);
        QVERIFY(editor.document().layout == secondLayout);
        QVERIFY(editor.explosionActive() && explode->isChecked());
        QTest::mouseClick(layout, Qt::LeftButton, Qt::NoModifier, canvasPoint(layout, {610, 320}));
        QCOMPARE(layout->selected(), componentId);
        QTest::qWait(30);
        artifact(editor, "explosion-with-annotations.png");
        const auto feedback = exportFeedback(editor.document());
        QCOMPARE(feedback["annotationSpace"].toString(), QString("result"));
        const auto objects = feedback["objects"].toArray();
        bool foundMovement = false;
        for (const auto &value : objects) {
            const auto obj = value.toObject();
            if (!obj["movements"].toArray().isEmpty()) {
                foundMovement = true;
                QCOMPARE(obj["source"].toObject(),
                         (QJsonObject{{"x1", 60.}, {"y1", 60.}, {"x2", 180.}, {"y2", 150.}}));
                QCOMPARE(obj["movements"].toArray()[0].toObject()["to"].toObject(),
                         (QJsonObject{{"x1", 600.}, {"y1", 240.}, {"x2", 720.}, {"y2", 330.}}));
            }
        }
        QVERIFY(foundMovement);
        QCOMPARE(editor.geometry(), geometry);
        QCOMPARE(imageScroll->viewport()->size(), viewport);
        editor.hide();
    }
    void compactFeedbackExportsSavesAndReopens() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto document = gridDocument();
        document.layout = createLayout(document.image.size(), document.candidates);
        Note point;
        point.point = {120, 105};
        point.comment = "把第一格移到右边。";
        Note rectangle;
        rectangle.isPoint = false;
        rectangle.rect = {60, 150, 240, 90};
        rectangle.comment = "这一行的文字保持左对齐。";
        document.notes = {point, rectangle};
        Editor editor;
        editor.setDocument(document);
        editor.resize(1240, 820);
        QTest::qWait(60);
        editor.explode();
        QPointer<LayoutCanvas> canvas = editor.layoutCanvas();
        QVERIFY(canvas && editor.explosionActive());
        QVERIFY(!editor.document().dirty);

        // Editing only the temporary partition does not create user feedback.
        auto manual = editor.findChild<QPushButton *>("mode_rect");
        QVERIFY(manual);
        const int groupCount = canvas->state().groups.size();
        manual->click();
        drag(canvas, {500, 400}, {600, 450});
        QCOMPARE(canvas->state().groups.size(), groupCount + 1);
        QCOMPARE(editor.document().notes.size(), 3);
        auto manualDraft = editor.findChild<QPlainTextEdit *>("noteText_" + editor.document().notes.last().id);
        QVERIFY(manualDraft);
        QTRY_VERIFY(manualDraft->hasFocus());
        canvas->setFocus();
        QTRY_COMPARE(editor.document().notes.size(), 2);
        QVERIFY(!editor.document().dirty);
        QVERIFY(exportLayoutChanges(*editor.document().layout).isEmpty());
        canvas->clearSelection();
        QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, canvasPoint(canvas, {100, 110}));
        QCOMPARE(canvas->selectionBounds(), QRectF(60, 60, 120, 90));
        auto destination = canvas->selectionBounds();
        destination.moveLeft(440);
        canvas->transformSelection(destination);
        QVERIFY(editor.document().dirty);
        const auto expectedFeedback = exportFeedback(editor.document());
        const auto expectedEmbedded = exportFeedback(editor.document(), true);
        const auto expectedImage = renderLayout(editor.document().image, *editor.document().layout);
        const auto expectedObjects = expectedFeedback["objects"].toArray();
        QVERIFY(expectedObjects.size() >= 3); // 移动 + 点 + 矩形

        auto exportButton = editor.findChild<QPushButton *>("exportJson");
        QVERIFY(exportButton);
        QCOMPARE(exportButton->toolTip(), QString("查看 JSON"));
        bool exportChecked = false;
        QTimer::singleShot(60, &editor, [&] {
            auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            QTimer::singleShot(3000, dialog, &QDialog::reject);
            auto text = dialog->findChild<QPlainTextEdit *>();
            QVERIFY(text && text->isVisible());
            QJsonParseError error;
            const auto parsed = QJsonDocument::fromJson(text->toPlainText().toUtf8(), &error);
            QCOMPARE(error.error, QJsonParseError::NoError);
            QVERIFY(parsed.isObject());
            auto embed = dialog->findChild<QCheckBox *>("embedOriginal");
            QVERIFY(embed && embed->isChecked());
            auto preview = parsed.object();
            QCOMPARE(preview.keys(), QStringList({"annotationSpace", "image", "objects"}));
            QVERIFY(preview["image"].toString().startsWith("data:image/png;base64,"));
            QVERIFY(preview["image"] != expectedEmbedded["image"]);
            preview.remove("image");
            QCOMPARE(preview, expectedFeedback);
            QPushButton *copy = nullptr;
            for (auto button : dialog->findChildren<QPushButton *>())
                if (button->text() == "复制 JSON")
                    copy = button;
            QVERIFY(copy && copy->isEnabled());
            copy->click();
            QTRY_COMPARE(clipboardJson().object(),
                         expectedEmbedded);
            const auto copied = clipboardJson(&error);
            QCOMPARE(error.error, QJsonParseError::NoError);
            QCOMPARE(copied.object(), expectedEmbedded);
            QTRY_VERIFY(dialog->findChild<QLabel *>("exportStatus")->text().startsWith("JSON 文件已复制"));
            const auto encoded =
                copied.object()["image"].toString().mid(QString("data:image/png;base64,").size());
            QCOMPARE(QByteArray::fromBase64(encoded.toLatin1()), document.png);
            QApplication::clipboard()->clear();
            dialog->raise();
            dialog->activateWindow();
            text->setFocus();
            QTRY_VERIFY(dialog->isActiveWindow() && text->hasFocus());
            text->selectAll();
            QTest::keyClick(text, Qt::Key_C, Qt::ControlModifier);
            QTRY_COMPARE(clipboardJson().object(),
                         expectedEmbedded);
            const auto keyboardCopy =
                clipboardJson(&error);
            QCOMPARE(error.error, QJsonParseError::NoError);
            QCOMPARE(keyboardCopy.object(), expectedEmbedded);
            const auto keyboardImage =
                keyboardCopy.object()["image"].toString().mid(QString("data:image/png;base64,").size());
            QCOMPARE(QByteArray::fromBase64(keyboardImage.toLatin1()), document.png);
            text->moveCursor(QTextCursor::Start);
            artifact(*dialog, "embedded-export.png");
            embed->click();
            QVERIFY(!embed->isChecked());
            const auto compact = QJsonDocument::fromJson(text->toPlainText().toUtf8(), &error);
            QCOMPARE(error.error, QJsonParseError::NoError);
            QCOMPARE(compact.object(), expectedFeedback);
            QVERIFY(!compact.object().contains("image"));
            copy->click();
            QTRY_COMPARE(clipboardJson().object(),
                         expectedFeedback);
            artifact(*dialog, "compact-export.png");
            exportChecked = true;
            dialog->reject();
        });
        exportButton->click();
        QVERIFY(exportChecked);
        QVERIFY(editor.document().dirty);

        const QString path = dir.filePath("review.edithere");
        bool selectedFile = false, saveError = false;
        QTimer chooseFile;
        connect(&chooseFile, &QTimer::timeout, &editor, [&] {
            auto active = QApplication::activeModalWidget();
            if (auto dialog = qobject_cast<QFileDialog *>(active); dialog && !selectedFile) {
                selectedFile = true;
                // selectFile() preserves focused text in a visible dialog; fill the field explicitly.
                auto filename = dialog->findChild<QLineEdit *>("fileNameEdit");
                QVERIFY(filename);
                filename->setText(path);
                QCOMPARE(dialog->selectedFiles(), QStringList({path}));
                QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
            } else if (auto error = qobject_cast<QMessageBox *>(active)) {
                saveError = true;
                error->accept();
            }
        });
        QTimer::singleShot(3000, &editor, [] {
            if (auto active = QApplication::activeModalWidget())
                active->close();
        });
        chooseFile.start(20);
        const bool saved = editor.saveProject();
        chooseFile.stop();
        QVERIFY(selectedFile && !saveError && saved);
        QVERIFY(!editor.document().dirty);
        QVERIFY(!QFileInfo::exists(dir.filePath("review.png")));
        QFile savedJson(path);
        QVERIFY(savedJson.open(QIODevice::ReadOnly));
        QJsonParseError error;
        const auto parsed = QJsonDocument::fromJson(savedJson.readAll(), &error);
        QCOMPARE(error.error, QJsonParseError::NoError);
        QCOMPARE(parsed.object()["schemaVersion"].toString(), QString("3.0.0"));
        QCOMPARE(parsed.object()["tool"].toString(), QString("EditHere"));
        QVERIFY(parsed.object()["capture"].isObject());
        QVERIFY(parsed.object()["layout"].isObject());
        QCOMPARE(parsed.object()["annotations"].toArray().size(), 2);
        const auto restored = loadDocument(path);
        QVERIFY(restored.layout.has_value());
        QCOMPARE(restored.png, document.png);
        QCOMPARE(renderLayout(restored.image, *restored.layout), expectedImage);
        QCOMPARE(restored.notes.size(), 2);
        QVERIFY(restored.notes[0].isPoint);
        QCOMPARE(restored.notes[0].point, QPoint(500, 105));
        QCOMPARE(restored.notes[0].comment, point.comment);
        QVERIFY(!restored.notes[1].isPoint);
        QCOMPARE(restored.notes[1].rect, rectangle.rect);
        QCOMPARE(restored.notes[1].comment, rectangle.comment);
        QCOMPARE(exportFeedback(restored), expectedFeedback);

        bool unexpectedPrompt = false;
        QTimer rejectUnexpectedPrompt;
        connect(&rejectUnexpectedPrompt, &QTimer::timeout, &editor, [&] {
            if (auto active = QApplication::activeModalWidget()) {
                unexpectedPrompt = true;
                active->close();
            }
        });
        rejectUnexpectedPrompt.start(20);
        editor.close();
        rejectUnexpectedPrompt.stop();
        QVERIFY(!unexpectedPrompt);
        QVERIFY(!editor.hasDocument());
        QVERIFY(!editor.document().layout.has_value());
        QVERIFY(editor.layoutCanvas() == nullptr);
        QVERIFY(canvas.isNull());
        QVERIFY(!editor.isVisible());
    }
    void previewLimitDoesNotLoseExportedFeedback() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        // A legal comment can make the optional preview taller than its image limit.
        auto document = gridDocument();
        Note note;
        note.point = {120, 100};
        note.comment = QString("line\n").repeated(1900);
        document.notes.append(note);
        document.dirty = true;
        QVERIFY_EXCEPTION_THROWN(previewImage(document), std::runtime_error);
        Editor editor;
        editor.setDocument(document);
        bool exported = false, choseDirectory = false;
        QTimer watchdog;
        watchdog.setInterval(5000);
        connect(&watchdog, &QTimer::timeout, &editor, [] {
            if (auto active = QApplication::activeModalWidget())
                active->close();
        });
        watchdog.start();
        QTimer::singleShot(60, &editor, [&] {
            auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            auto save = dialog->findChild<QPushButton *>("saveFeedbackBundle");
            auto status = dialog->findChild<QLabel *>("exportStatus");
            auto embed = dialog->findChild<QCheckBox *>("embedOriginal");
            QVERIFY(save && status && embed);
            // Exercise both standalone and paired-image feedback.
            for (bool embedded : {true, false}) {
                embed->setChecked(embedded);
                QTimer::singleShot(60, dialog, [&] {
                    auto chooser = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
                    QVERIFY(chooser);
                    choseDirectory = true;
                    chooser->setDirectory(directory.path());
                    QMetaObject::invokeMethod(chooser, "accept", Qt::DirectConnection);
                });
                save->click();
                QVERIFY(choseDirectory);
                QVERIFY(status->text().contains("JSON 与原图已保存"));
                QVERIFY(status->text().contains("批注预览未保存"));
            }
            auto folders = QDir(directory.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            QCOMPARE(folders.size(), 2);
            for (const auto &folder : folders) {
                const QDir output(QDir(directory.path()).filePath(folder));
                QVERIFY(output.exists("feedback.json"));
                QVERIFY(output.exists("feedback.png"));
                QVERIFY(!output.exists("annotations.png"));
                const auto restored = loadDocument(output.filePath("feedback.json"));
                QCOMPARE(restored.image, document.image);
                QCOMPARE(restored.notes.size(), 1);
                QCOMPARE(restored.notes[0].point, note.point);
                QCOMPARE(restored.notes[0].comment, note.comment);
            }
            exported = true;
            dialog->reject();
        });
        editor.exportJson();
        watchdog.stop();
        QVERIFY(exported);
        QVERIFY(editor.document().dirty);
        editor.hide();
    }
    void invalidExportKeepsUnsavedWork() {
        QTemporaryDir dir;
        auto document = gridDocument();
        document.layout = createLayout(document.image.size(), document.candidates);
        document.layout->pieces[0].destination.moveLeft(900);
        document.dirty = true;
        Editor editor;
        editor.setDocument(document);
        bool rejectedExport = false;
        QTimer::singleShot(80, [&] {
            auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            QTimer::singleShot(3000, dialog, &QDialog::reject);
            auto json = dialog->findChild<QPlainTextEdit *>();
            QVERIFY(json);
            bool copyDisabled = false, saveDisabled = false;
            for (auto button : dialog->findChildren<QPushButton *>()) {
                if (button->text() == "复制 JSON")
                    copyDisabled = !button->isEnabled();
                if (button->text() == "保存 JSON 与图片")
                    saveDisabled = !button->isEnabled();
            }
            rejectedExport = json->toPlainText().isEmpty() && copyDisabled && saveDisabled;
            dialog->reject();
        });
        editor.exportJson();
        QVERIFY(rejectedExport);
        QVERIFY(editor.document().dirty);
        QVERIFY(editor.document().layout == document.layout);

        const QString path = dir.filePath("invalid-project.edithere");
        bool selectedFile = false, errorShown = false;
        QTimer driver;
        connect(&driver, &QTimer::timeout, &editor, [&] {
            auto active = QApplication::activeModalWidget();
            if (auto dialog = qobject_cast<QFileDialog *>(active); dialog && !selectedFile) {
                selectedFile = true;
                // selectFile() preserves focused text in a visible dialog; fill the field explicitly.
                auto filename = dialog->findChild<QLineEdit *>("fileNameEdit");
                QVERIFY(filename);
                filename->setText(path);
                QCOMPARE(dialog->selectedFiles(), QStringList({path}));
                QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
            } else if (auto error = qobject_cast<QMessageBox *>(active)) {
                errorShown = true;
                error->accept();
            }
        });
        QTimer::singleShot(3000, &editor, [] {
            if (auto active = QApplication::activeModalWidget())
                active->close();
        });
        driver.start(20);
        const bool saved = editor.saveProject();
        driver.stop();
        QVERIFY(selectedFile && errorShown);
        QVERIFY(!saved);
        QVERIFY(!QFileInfo::exists(path));
        QVERIFY(!QFileInfo::exists(dir.filePath("invalid-project.png")));
        QVERIFY(editor.document().dirty);
        QVERIFY(editor.document().layout == document.layout);
        editor.hide();
    }
    void screenCropUsesImagePixels() {
        QImage image = exampleImage();
        ScreenFrame frame{"test", {0, 0, 560, 360}, {0, 0, 1120, 720}, image, true};
        Overlay overlay(frame);
        overlay.show();
        overlay.setFixedSize(560, 360);
        QTest::qWait(50);
        QSignalSpy accepted(&overlay, &Overlay::accepted);
        QTest::mousePress(&overlay, Qt::LeftButton, Qt::NoModifier, {30, 70});
        QTest::mouseMove(&overlay, {300, 260});
        artifact(overlay, "crop-overlay.png");
        QCOMPARE(accepted.count(), 0);
        QTest::mouseRelease(&overlay, Qt::LeftButton, Qt::NoModifier, {300, 260});
        // Releasing the button settles the region instead of finishing the capture: the
        // bar beside it is where copying, pinning, saving, reading and 批注 live, so
        // nothing reaches the editor until 批注 is chosen.
        QCOMPARE(accepted.count(), 0);
        QVERIFY(!overlay.findChildren<QPushButton *>().isEmpty());
        QTest::keyClick(&overlay, Qt::Key_Return);
        QCOMPARE(accepted.count(), 1);
        QCOMPARE(accepted.first().first().toRect(), QRect(60, 140, 540, 380));
        QTest::mouseClick(&overlay, Qt::LeftButton, Qt::NoModifier, {100, 100});
        QCOMPARE(accepted.count(), 1);
        overlay.hide();
    }
    // REG-068: the region a capture came from was forgotten the moment the picture was
    // made, so pinning it dropped it in the middle of the screen. The window has to be
    // able to say where its region is on screen, in the units the window manager uses
    // rather than the pixels the screen was read at.
    void aCapturedRegionKnowsWhereItCameFrom() {
        QImage image = exampleImage();
        // The screen is measured at half the pixels it was read at, which is what a
        // display scaled by two looks like.
        ScreenFrame frame{"placement", {0, 0, 560, 360}, {0, 0, 1120, 720}, image, true};
        Overlay overlay(frame);
        overlay.show();
        overlay.setFixedSize(560, 360);
        QTest::qWait(50);
        QTest::mousePress(&overlay, Qt::LeftButton, Qt::NoModifier, {30, 70});
        QTest::mouseMove(&overlay, {300, 260});
        QTest::mouseRelease(&overlay, Qt::LeftButton, Qt::NoModifier, {300, 260});
        const QRect selection = overlay.selection();
        QCOMPARE(selection, QRect(60, 140, 540, 380));
        const QRect placement = overlay.placementFor(selection.size());
        QCOMPARE(placement.size(), QSize(270, 190));
        QCOMPARE(placement.topLeft(), QPoint(30, 70));
        // A shadow makes the picture bigger than the region, and then the pin is
        // centred on the region instead of starting at its corner, so the picture it
        // holds still covers the thing it is a picture of.
        CaptureStyle shaded;
        shaded.shadow = true;
        shaded.shadowStrength = 60;
        const int reach = captureShadowRadius(shaded.shadow, shaded.shadowStrength);
        const QRect padded = overlay.placementFor(composeCapture(image.copy(selection), shaded).size());
        QCOMPARE(padded.size(), QSize(270 + reach, 190 + reach));
        // The halo reaches past the region on every side, so the body inside the
        // picture is what has to land on the region. It does, to within the pixel the
        // two halves of the rounding disagree by.
        const QRect body = padded.adjusted(reach / 2, reach / 2, -reach / 2, -reach / 2);
        QVERIFY2(std::abs(body.x() - placement.x()) <= 1 && std::abs(body.y() - placement.y()) <= 1 &&
                     std::abs(body.width() - placement.width()) <= 1 &&
                     std::abs(body.height() - placement.height()) <= 1,
                 "the shadowed picture has to sit on the region it was taken from");
        // A picture with no size has no place either.
        QVERIFY(overlay.placementFor(QSize(0, 0)).isEmpty());
        overlay.hide();

        // With nothing chosen there is nowhere to put anything.
        Overlay empty(frame);
        QVERIFY(empty.placementFor(QSize(10, 10)).isEmpty());
        empty.hide();
    }
    // REG-083: the halo was drawn on the capture window, which meant that while the
    // reader was still looking for a block a blue glow followed the pointer over the
    // whole screen. A shadow belongs to a pinned picture and nowhere else, so the
    // window has to leave it out — even with the strength slider turned up.
    void theCaptureWindowCastsNoShadowAroundTheRegion() {
        // A flat light screen, so the only thing that can darken a pixel is the halo.
        QImage image(1120, 720, QImage::Format_ARGB32);
        image.fill(QColor(240, 240, 240));
        ScreenFrame frame{"shadow", {0, 0, 560, 360}, {0, 0, 1120, 720}, image, true};
        Overlay overlay(frame);
        overlay.show();
        overlay.setFixedSize(560, 360);
        QTest::qWait(50);
        QTest::mousePress(&overlay, Qt::LeftButton, Qt::NoModifier, {100, 100});
        QTest::mouseMove(&overlay, {300, 260});
        QTest::mouseRelease(&overlay, Qt::LeftButton, Qt::NoModifier, {300, 260});
        QVERIFY(!overlay.selection().isEmpty());
        // A capture starts with a shadow on, so "without one" has to be asked for.
        CaptureStyle bare;
        bare.shadow = false;
        overlay.setStyle(bare);
        QTest::qWait(30);
        const QImage plain = overlay.grab().toImage().convertedTo(QImage::Format_ARGB32);
        // A point just outside the region, on its left, where the halo has to reach
        // and where nothing else is drawn: the bar sits above the region and the
        // column of style tools to its right.
        const QPoint probe(overlay.selection().x() / 2 - 5, overlay.selection().center().y() / 2);
        CaptureStyle shaded = bare;
        shaded.shadow = true;
        shaded.shadowStrength = 60;
        overlay.setStyle(shaded);
        QTest::qWait(50);
        const QImage withShadow = overlay.grab().toImage().convertedTo(QImage::Format_ARGB32);
        artifact(overlay, "shadow-preview.png");
        QVERIFY2(plain.rect().contains(probe) && withShadow.rect().contains(probe),
                 "the probe has to be inside what was rendered");
        QVERIFY2(withShadow.pixelColor(probe) == plain.pixelColor(probe),
                 "a shadow on the capture window follows the pointer over the whole "
                 "screen while a region is still being looked for; it belongs to a pin");
        overlay.hide();
    }
    // REG-083: the magnifier and the colour readout used to disappear the moment the
    // region settled, so pointing at the picture that had just been taken showed
    // nothing until C was pressed. Reading a colour off your own capture is the common
    // case, not a mode.
    void theMagnifierStaysUpOverTheRegionThatWasJustTaken() {
        QImage image(1120, 720, QImage::Format_ARGB32);
        image.fill(QColor(240, 240, 240));
        ScreenFrame frame{"magnifier", {0, 0, 560, 360}, {0, 0, 1120, 720}, image, true};
        Overlay overlay(frame);
        overlay.show();
        overlay.setFixedSize(560, 360);
        QTest::qWait(50);
        QTest::mousePress(&overlay, Qt::LeftButton, Qt::NoModifier, {100, 100});
        QTest::mouseMove(&overlay, {300, 260});
        QTest::mouseRelease(&overlay, Qt::LeftButton, Qt::NoModifier, {300, 260});
        QVERIFY(!overlay.selection().isEmpty());
        // Away from the region there is nothing of the user's to read, so it stays down.
        movePointerTo(overlay, {520, 40});
        QTest::qWait(30);
        const QImage away = overlay.grab().toImage().convertedTo(QImage::Format_ARGB32);
        // Back over the picture that was just taken it is up again, without a keystroke.
        movePointerTo(overlay, {200, 180});
        QTest::qWait(30);
        const QImage over = overlay.grab().toImage().convertedTo(QImage::Format_ARGB32);
        artifact(overlay, "magnifier-settled.png");
        const int before = countDark(away, 60);
        const int after = countDark(over, 60);
        QVERIFY2(after > before + 5000,
                 qPrintable(QString("the panel has to appear over the region (away %1, over %2)")
                                .arg(before)
                                .arg(after)));
        overlay.hide();
    }
    // The enlargement is where the aim is judged, so the blue frame of the recognised
    // block has to be inside it, magnified with the pixels it belongs to rather than
    // left at the size it has on the screen.
    void theMagnifierCarriesTheFrameItIsAimingAt() {
        QImage image = exampleImage();
        ScreenFrame frame{"magnifier", {0, 0, 560, 360}, {0, 0, 1120, 720}, image, true};
        Overlay overlay(frame);
        overlay.show();
        overlay.setFixedSize(560, 360);
        // The block finder runs off the main thread, so the frames have to be waited
        // for before the pointer is put on one of them.
        QTest::qWait(400);
        // The panel is placed the way the window places it: beside the pointer, and
        // flipped when it would fall off an edge.
        // The pointer goes on the left edge of the screen. The enlargement only covers
        // about twenty pixels around the pointer, so an edge is the one place a frame
        // can be seen to be magnified rather than merely present.
        const QPoint at(0, 225);
        movePointerTo(overlay, at);
        QTest::qWait(80);
        const QImage shot = overlay.grab().toImage().convertedTo(QImage::Format_ARGB32);
        artifact(overlay, "magnifier-frame.png");
        const QRect panel = magnifierPanel(overlay, at, QSize(560, 360));
        const QRect zoom(panel.topLeft() + QPoint(magnifierPanelPadding, magnifierPanelPadding),
                         QSize(magnifierPanelWidth - magnifierPanelPadding * 2, magnifierZoomHeight));
        QVERIFY2(shot.rect().contains(zoom), "the enlargement has to be inside what was rendered");
        int marked = 0;
        for (int row = zoom.top(); row < zoom.bottom(); ++row)
            for (int column = zoom.left(); column < zoom.right(); ++column)
                if (isAccentColour(shot.pixelColor(column, row)))
                    ++marked;
        // The frame is on the screen at its own size as well, so the count has to come
        // from inside the enlargement: that is the part of the picture the pointer is
        // reading, and a frame missing from it is a frame the pointer cannot use.
        QVERIFY2(marked > 0, "the enlargement shows the pixels without the frame that names them");
        overlay.hide();
    }
    // REG-086: the enlargement used to be a second look at the grab with a hairline
    // drawn back on top of it, so the blue frame came out thin however far the pixels
    // under it had been stretched. It is a glass held over the window instead: what is
    // on the window, frame included, has to come out enlarged with those pixels.
    void theEnlargementMagnifiesTheFrameRatherThanRedrawingIt() {
        QImage image(1120, 720, QImage::Format_ARGB32);
        image.fill(QColor(240, 240, 240));
        ScreenFrame frame{"magnifier", {0, 0, 560, 360}, {0, 0, 1120, 720}, image, true};
        Overlay overlay(frame);
        overlay.show();
        overlay.setFixedSize(560, 360);
        QTest::qWait(50);
        QTest::mousePress(&overlay, Qt::LeftButton, Qt::NoModifier, {100, 100});
        QTest::mouseMove(&overlay, {300, 260});
        QTest::mouseRelease(&overlay, Qt::LeftButton, Qt::NoModifier, {300, 260});
        QVERIFY(!overlay.selection().isEmpty());
        // The pointer goes on the left edge of the region, which is where the frame is.
        const QPoint at(overlay.selection().x() / 2, overlay.selection().center().y() / 2);
        movePointerTo(overlay, at);
        QTest::qWait(50);
        const QImage shot = overlay.grab().toImage().convertedTo(QImage::Format_ARGB32);
        artifact(overlay, "magnifier-thick-frame.png");
        const QRect panel = magnifierPanel(overlay, at, QSize(560, 360));
        const QRect zoom(panel.topLeft() + QPoint(magnifierPanelPadding, magnifierPanelPadding),
                         QSize(magnifierPanelWidth - magnifierPanelPadding * 2, magnifierZoomHeight));
        QVERIFY2(shot.rect().contains(zoom), "the enlargement has to be inside what was rendered");
        int marked = 0;
        for (int row = zoom.top(); row < zoom.bottom(); ++row)
            for (int column = zoom.left(); column < zoom.right(); ++column)
                if (isAccentColour(shot.pixelColor(column, row)))
                    ++marked;
        // The frame is two pixels wide on the screen and the enlargement is fifteen
        // times, so it fills a band of the panel. A hairline redrawn at its own width
        // is about two hundred pixels of it; the band is several thousand.
        QVERIFY2(marked > 2000,
                 qPrintable(QString("the frame has to be enlarged with the pixels it marks (%1)")
                                .arg(marked)));
        overlay.hide();
    }
    // REG-090: an arrow key took its step in pixels of the grab and then rounded the
    // answer back into pixels of the pointer. On a scaled display that rounding
    // sometimes landed on the pixel the step had started from, which is a key that
    // looks dead; and it rounded each axis on its own, which is a key that moves the
    // pointer two ways at once, the first press going askew and the ones after it
    // going straight because the askew part had been rounded out of them. A step is one
    // whole pixel of the pointer now, on the axis that was pressed and nowhere else.
    void arrowKeysMoveThePointerOneStepAtATime() {
        // A display at one and a quarter: the grab is 700 pixels across and the window
        // is 560, which is the 125% setting where the rounding showed. Whole-number
        // scalings hide the fault, which is why it took one of these to see it.
        QImage image(700, 450, QImage::Format_ARGB32);
        image.fill(QColor(240, 240, 240));
        ScreenFrame frame{"steps", {0, 0, 560, 360}, {0, 0, 700, 450}, image, true};
        Overlay overlay(frame);
        overlay.show();
        overlay.setFixedSize(560, 360);
        QTRY_VERIFY_WITH_TIMEOUT(overlay.findChildren<QFutureWatcherBase *>().isEmpty(), 5000);
        QVERIFY(overlay.selection().isEmpty());
        // Neither does a mouse land on a whole pixel: at one and a quarter the only
        // places it can be put are fifths of a pixel apart. Starting from such a point
        // is what used to send the first press off at an angle.
        putPointerOn(overlay, QPointF(200.8, 179.2));
        QTest::qWait(50);
        // What comes back after the pointer has been asked to move: the system puts it
        // on the nearest place it can address — a fifth of a pixel short of the one
        // that was asked for — and then reports that place as a move of the mouse. The
        // report is not the user, and taking it as one is what pulled the aim back and
        // made a key do nothing.
        const auto echo = [&] {
            const QPoint at = overlay.pointer();
            const QPointF rounded(std::floor(at.x() * 1.25) / 1.25,
                                  std::floor(at.y() * 1.25) / 1.25);
            QMouseEvent move(QEvent::MouseMove, rounded, rounded, Qt::NoButton, Qt::NoButton,
                             Qt::NoModifier);
            QApplication::sendEvent(&overlay, &move);
            QVERIFY2(overlay.pointer() == at,
                     qPrintable(QString("the answer of the system must not move the pointer "
                                        "back off %1,%2 (it is at %3,%4)")
                                    .arg(at.x())
                                    .arg(at.y())
                                    .arg(overlay.pointer().x())
                                    .arg(overlay.pointer().y())));
        };
        const QPoint start = overlay.pointer();
        const QPoint drawn = crosshair(
            overlay.grab().toImage().convertedTo(QImage::Format_ARGB32), 150, 240);
        QVERIFY2(std::abs(drawn.x() - start.x()) <= 2 && std::abs(drawn.y() - start.y()) <= 2,
                 qPrintable(QString("the crosshair is drawn where the pointer is (%1,%2 and %3,%4)")
                                .arg(drawn.x())
                                .arg(drawn.y())
                                .arg(start.x())
                                .arg(start.y())));
        // Right, up, left, down and back again, in runs: every press has to move the
        // pointer one step, and no press may touch the axis that was not pressed.
        const QVector<QPair<Qt::Key, QPoint>> presses{
            {Qt::Key_Right, {1, 0}},  {Qt::Key_Right, {1, 0}}, {Qt::Key_Up, {0, -1}},
            {Qt::Key_Up, {0, -1}},    {Qt::Key_Up, {0, -1}},   {Qt::Key_Left, {-1, 0}},
            {Qt::Key_Down, {0, 1}},   {Qt::Key_Down, {0, 1}},  {Qt::Key_Left, {-1, 0}},
            {Qt::Key_Right, {1, 0}}};
        QPoint asked = start;
        for (const auto &press : presses) {
            QTest::keyClick(&overlay, press.first);
            QTest::qWait(20);
            const QPoint at = overlay.pointer();
            const QPoint moved(at.x() - asked.x(), at.y() - asked.y());
            const bool sideways = press.second.x() != 0;
            const int along = sideways ? moved.x() : moved.y();
            const int across = sideways ? moved.y() : moved.x();
            const int expected = sideways ? press.second.x() : press.second.y();
            // A press that moves nothing is one that was rounded away. A press that
            // moves the other axis as well is the two axes being rounded each on its
            // own. A press that moves two steps is a step that was taken too coarse.
            // None of the three may happen, however fine the pixels underneath are.
            QVERIFY2(across == 0 && along == expected,
                     qPrintable(QString("one press is one step on the axis that was pressed and "
                                        "nothing on the other (moved %1,%2, expected %3,%4)")
                                    .arg(moved.x())
                                    .arg(moved.y())
                                    .arg(press.second.x())
                                    .arg(press.second.y())));
            asked = at;
            echo();
        }
        // Ten presses, ten steps: the pointer has to have arrived where they asked for,
        // not hovered about the place because every other press was taken back again.
        QCOMPARE(overlay.pointer(), start + QPoint(1, -1));
        const QPoint ended = crosshair(
            overlay.grab().toImage().convertedTo(QImage::Format_ARGB32), 150, 240);
        QVERIFY2(std::abs(ended.x() - overlay.pointer().x()) <= 2 &&
                     std::abs(ended.y() - overlay.pointer().y()) <= 2,
                 qPrintable(QString("the crosshair follows the pointer to %1,%2 (it is at %3,%4)")
                                .arg(overlay.pointer().x())
                                .arg(overlay.pointer().y())
                                .arg(ended.x())
                                .arg(ended.y())));
        artifact(overlay, "magnifier-arrow-steps.png");
        overlay.hide();
    }
    // REG-092: the level the wheel asked for was dropped again as soon as the pointer
    // really moved. Every move threw the blocks away and looked afresh, and a fresh
    // look offers the smallest block under the pointer, which is how a whole row
    // turned back into a single cell the moment the pointer travelled into one.
    void theLevelPickedWithTheWheelIsKeptWhileMoving() {
        ScreenFrame frame{"kept", {0, 0, 800, 600}, {0, 0, 800, 600}, tableShot(), true};
        Overlay overlay(frame);
        overlay.show();
        overlay.setFixedSize(800, 600);
        QTRY_VERIFY_WITH_TIMEOUT(overlay.findChildren<QFutureWatcherBase *>().isEmpty(), 5000);
        const QPoint first(330, 210);
        movePointerTo(overlay, first);
        QTest::qWait(200);
        QVERIFY(overlay.hovered().has_value());
        const QRect cell = overlay.hovered()->bounds;
        QWheelEvent turn(QPointF(first), QPointF(overlay.mapToGlobal(first)), QPoint(),
                         QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(&overlay, &turn);
        QTest::qWait(50);
        QVERIFY(overlay.hovered().has_value());
        const QRect row = overlay.hovered()->bounds;
        QVERIFY2(row != cell && sameBlock(row, QRect(70, 160, 600, 100)),
                 qPrintable(QString("the wheel picks the row rather than the cell (%1,%2 %3x%4)")
                                .arg(row.x())
                                .arg(row.y())
                                .arg(row.width())
                                .arg(row.height())));
        // Now to another cell of the same table, far away from the first one: the row
        // is still the level on offer there, not the cell that was arrived in.
        const QPoint second(530, 410);
        movePointerTo(overlay, second);
        QTest::qWait(100);
        QVERIFY2(overlay.hovered() && sameBlock(overlay.hovered()->bounds, QRect(70, 360, 600, 100)),
                 qPrintable(QString("the level picked with the wheel is still the one offered "
                                    "there (%1,%2 %3x%4, wanted 70,360 600x100)")
                                .arg(overlay.hovered() ? overlay.hovered()->bounds.x() : -1)
                                .arg(overlay.hovered() ? overlay.hovered()->bounds.y() : -1)
                                .arg(overlay.hovered() ? overlay.hovered()->bounds.width() : -1)
                                .arg(overlay.hovered() ? overlay.hovered()->bounds.height() : -1)));
        artifact(overlay, "wheel-level-is-kept.png");
        overlay.hide();
    }
    // REG-092: fine-tuning with the arrow keys re-chose the block from scratch at every
    // step, so a block that had been settled on turned into whatever smaller block the
    // pointer had travelled into — and then had to be found all over again. A mouse
    // that travels is not fine-tuning, and there the block still follows the pointer.
    void fineTuningWithTheArrowKeysKeepsTheBlockOnOffer() {
        ScreenFrame frame{"fine", {0, 0, 800, 600}, {0, 0, 800, 600}, tableShot(), true};
        Overlay overlay(frame);
        overlay.show();
        overlay.setFixedSize(800, 600);
        QTRY_VERIFY_WITH_TIMEOUT(overlay.findChildren<QFutureWatcherBase *>().isEmpty(), 5000);
        // Just off the left edge of the table, where the screen itself is the block.
        const QPoint outside(65, 210);
        movePointerTo(overlay, outside);
        // The look for the blocks of the screen runs outside this window, and its answer
        // arrives when it arrives: it must not land in the middle of the fine-tuning
        // and be mistaken for what the keys did.
        QTRY_VERIFY_WITH_TIMEOUT(overlay.findChildren<QProcess *>().isEmpty(), 5000);
        QTest::qWait(200);
        QVERIFY(overlay.hovered().has_value());
        const QRect whole = overlay.hovered()->bounds;
        QVERIFY2(sameBlock(whole, QRect(0, 0, 800, 600)),
                 qPrintable(QString("off the table the screen itself is offered (%1,%2 %3x%4)")
                                .arg(whole.x())
                                .arg(whole.y())
                                .arg(whole.width())
                                .arg(whole.height())));
        // Ten presses of the right arrow, which is more than a tremble and lands inside
        // the table: fine-tuning must not hand back the cell that was arrived in.
        for (int i = 0; i < 10; ++i)
            QTest::keyClick(&overlay, Qt::Key_Right);
        QVERIFY2(overlay.hovered() && sameBlock(overlay.hovered()->bounds, whole),
                 qPrintable(QString("fine-tuning keeps the block on offer (%1,%2 %3x%4, was %5,%6 "
                                    "%7x%8)")
                                .arg(overlay.hovered() ? overlay.hovered()->bounds.x() : -1)
                                .arg(overlay.hovered() ? overlay.hovered()->bounds.y() : -1)
                                .arg(overlay.hovered() ? overlay.hovered()->bounds.width() : -1)
                                .arg(overlay.hovered() ? overlay.hovered()->bounds.height() : -1)
                                .arg(whole.x())
                                .arg(whole.y())
                                .arg(whole.width())
                                .arg(whole.height())));
        // A mouse that travels somewhere else is not fine-tuning: the block follows it,
        // which is the whole point of finding blocks by hovering.
        movePointerTo(overlay, QPoint(330, 210));
        QTest::qWait(100);
        QVERIFY2(overlay.hovered() && !sameBlock(overlay.hovered()->bounds, whole),
                 qPrintable(QString("moving the mouse takes the block under the pointer (%1,%2 "
                                    "%3x%4)")
                                .arg(overlay.hovered() ? overlay.hovered()->bounds.x() : -1)
                                .arg(overlay.hovered() ? overlay.hovered()->bounds.y() : -1)
                                .arg(overlay.hovered() ? overlay.hovered()->bounds.width() : -1)
                                .arg(overlay.hovered() ? overlay.hovered()->bounds.height() : -1)));
        artifact(overlay, "fine-tuning-keeps-the-block.png");
        overlay.hide();
    }
    // REG-091: the block the wheel had picked out did not survive an arrow key. Being
    // asked to move the pointer is answered with a move event of the pointer's own,
    // and that event started the search for a block all over again, which put the
    // smallest block under the pointer back and threw away the bigger one the wheel
    // had just picked. The answer of the system is not the user moving the mouse, so
    // the search is not started again for it.
    void theLayerPickedWithTheWheelSurvivesAnArrowKey() {
        // A table, because it is the ordinary shape that offers several blocks at one
        // spot — a cell inside a row inside the whole table — which is what gives the
        // wheel something to choose between.
        ScreenFrame frame{"layer", {0, 0, 800, 600}, {0, 0, 800, 600}, tableShot(), true};
        Overlay overlay(frame);
        overlay.show();
        overlay.setFixedSize(800, 600);
        QTRY_VERIFY_WITH_TIMEOUT(overlay.findChildren<QFutureWatcherBase *>().isEmpty(), 5000);
        // A cell of the table: several blocks contain this spot, one inside the next.
        const QPoint spot(330, 210);
        movePointerTo(overlay, spot);
        QTest::qWait(200);
        QVERIFY2(overlay.hovered().has_value(),
                 qPrintable(QString("a block is offered at %1,%2").arg(spot.x()).arg(spot.y())));
        const QRect smallest = overlay.hovered()->bounds;
        // The wheel walks outwards through the blocks that contain the pointer: the
        // cell, then the row it sits in, then the table.
        QWheelEvent turn(QPointF(spot), QPointF(overlay.mapToGlobal(spot)), QPoint(), QPoint(0, 120),
                         Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(&overlay, &turn);
        QTest::qWait(50);
        QVERIFY(overlay.hovered().has_value());
        const QRect bigger = overlay.hovered()->bounds;
        QVERIFY2(bigger != smallest,
                 qPrintable(QString("the wheel picks another block than the one first offered "
                                    "(%1,%2 %3x%4 and %5,%6 %7x%8)")
                                .arg(smallest.x())
                                .arg(smallest.y())
                                .arg(smallest.width())
                                .arg(smallest.height())
                                .arg(bigger.x())
                                .arg(bigger.y())
                                .arg(bigger.width())
                                .arg(bigger.height())));
        // Now an arrow key, which asks for the pointer to be put one step along. What
        // comes back is the answer of the system rather than the user, and it must not
        // start the search again: the block the wheel picked is still the one on offer.
        QTest::keyClick(&overlay, Qt::Key_Right);
        QTest::qWait(50);
        const QPoint at = overlay.pointer();
        QMouseEvent echo(QEvent::MouseMove, QPointF(at), QPointF(at), Qt::NoButton, Qt::NoButton,
                         Qt::NoModifier);
        QApplication::sendEvent(&overlay, &echo);
        QTest::qWait(50);
        QVERIFY2(overlay.hovered() && overlay.hovered()->bounds == bigger,
                 qPrintable(QString("the block picked with the wheel is still the one offered "
                                    "(%1,%2 %3x%4, but now %5,%6 %7x%8)")
                                .arg(bigger.x())
                                .arg(bigger.y())
                                .arg(bigger.width())
                                .arg(bigger.height())
                                .arg(overlay.hovered() ? overlay.hovered()->bounds.x() : -1)
                                .arg(overlay.hovered() ? overlay.hovered()->bounds.y() : -1)
                                .arg(overlay.hovered() ? overlay.hovered()->bounds.width() : -1)
                                .arg(overlay.hovered() ? overlay.hovered()->bounds.height() : -1)));
        artifact(overlay, "layer-survives-arrow.png");
        overlay.hide();
    }
    // REG-089: while a block is still being looked for the arrows did nothing at all,
    // so the only way to point at a pixel was to move the mouse over it — the one
    // gesture that cannot land on one. The arrows aim the pointer before a capture just
    // as they do after one.
    void arrowsMoveThePointerBeforeAnythingHasBeenTaken() {
        QImage image(1120, 720, QImage::Format_ARGB32);
        // A screen that changes from column to column, so a pointer that has moved
        // shows a different enlargement even when nothing else about it has.
        for (int column = 0; column < image.width(); ++column)
            for (int row = 0; row < image.height(); ++row)
                image.setPixelColor(column, row, QColor(column % 256, 200, 200));
        ScreenFrame frame{"arrows", {0, 0, 560, 360}, {0, 0, 1120, 720}, image, true};
        Overlay overlay(frame);
        overlay.show();
        overlay.setFixedSize(560, 360);
        QTRY_VERIFY_WITH_TIMEOUT(overlay.findChildren<QFutureWatcherBase *>().isEmpty(), 5000);
        movePointerTo(overlay, {200, 180});
        QTest::qWait(50);
        // Nothing taken, nothing being drawn: this is the state the arrows were missing from.
        QVERIFY(overlay.selection().isEmpty());
        const QRect panel = magnifierPanel(overlay, QPoint(200, 180), QSize(560, 360));
        const QRect zoom(panel.topLeft() + QPoint(magnifierPanelPadding, magnifierPanelPadding),
                         QSize(magnifierPanelWidth - magnifierPanelPadding * 2, magnifierZoomHeight));
        const QImage before = overlay.grab().toImage().convertedTo(QImage::Format_ARGB32);
        QTest::keyClick(&overlay, Qt::Key_Right);
        QTest::qWait(50);
        const QImage after = overlay.grab().toImage().convertedTo(QImage::Format_ARGB32);
        artifact(overlay, "magnifier-arrow-before-capture.png");
        // The key aims, it does not take anything: there is still no region.
        QVERIFY(overlay.selection().isEmpty());
        QVERIFY2(before.copy(zoom) != after.copy(zoom),
                 "the enlargement is aimed by the pointer, so an arrow key has to move the "
                 "pointer while a block is still being looked for");
        overlay.hide();
    }
    // REG-088: the panel is painted by the window, but the bar and the column of tools
    // are its children and are painted over it, so a panel placed without looking at
    // them ends up half behind the toolbar — exactly the moment a colour is being read.
    void theMagnifierPanelKeepsClearOfTheTools() {
        QImage image(1120, 720, QImage::Format_ARGB32);
        image.fill(QColor(240, 240, 240));
        ScreenFrame frame{"magnifier", {0, 0, 560, 360}, {0, 0, 1120, 720}, image, true};
        Overlay overlay(frame);
        overlay.show();
        overlay.setFixedSize(560, 360);
        QTest::qWait(50);
        QTest::mousePress(&overlay, Qt::LeftButton, Qt::NoModifier, {100, 100});
        QTest::mouseMove(&overlay, {300, 260});
        QTest::mouseRelease(&overlay, Qt::LeftButton, Qt::NoModifier, {300, 260});
        QVERIFY(!overlay.selection().isEmpty());
        const QPoint at(overlay.selection().x() / 2, overlay.selection().center().y() / 2);
        movePointerTo(overlay, at);
        QTest::qWait(50);
        const auto *bar = overlay.findChild<QWidget *>(QStringLiteral("captureToolbar"));
        QVERIFY2(bar != nullptr && bar->isVisible(), "the bar has to be up for this to mean anything");
        // Without looking at the tools this is where the panel would go, and it is on
        // the bar: that is the situation being guarded against, not an assumption.
        QVERIFY2(magnifierPlacement(at, QSize(560, 360), {}).toRect().intersects(bar->geometry()),
                 "the panel has to want the bar's place before stepping aside means anything");
        const QRect panel = magnifierPanel(overlay, at, QSize(560, 360));
        QVERIFY2(!panel.intersects(bar->geometry()),
                 "the panel is drawn by the window and the bar is drawn over it, so the "
                 "panel has to be placed somewhere the bar is not");
        // And in the pixels: the pointer sits on the left edge of the region, so the
        // enlarged frame is a band that runs the whole way down the enlargement. The
        // only thing allowed to break it is the grid, which is one line every cell;
        // anything painted across the panel takes out a whole stretch of rows.
        const QImage shot = overlay.grab().toImage().convertedTo(QImage::Format_ARGB32);
        artifact(overlay, "magnifier-clear-of-the-bar.png");
        const QRect zoom(panel.topLeft() + QPoint(magnifierPanelPadding, magnifierPanelPadding),
                         QSize(magnifierPanelWidth - magnifierPanelPadding * 2, magnifierZoomHeight));
        QVERIFY2(shot.rect().contains(zoom), "the enlargement has to be inside what was rendered");
        int rows = 0;
        for (int row = zoom.top(); row < zoom.bottom(); ++row) {
            for (int column = zoom.left(); column < zoom.right(); ++column)
                if (isAccentColour(shot.pixelColor(column, row))) {
                    ++rows;
                    break;
                }
        }
        QVERIFY2(rows >= zoom.height() * 4 / 5,
                 qPrintable(QString("the enlargement has to be readable from top to bottom "
                                    "(%1 of %2 rows show the frame)")
                                .arg(rows)
                                .arg(zoom.height())));
        overlay.hide();
    }
    // REG-087: an arrow key used to slide the whole region one pixel, which meant a
    // region that had been placed to the pixel slid away from where it was put and
    // there was no way to aim at one pixel of it. The arrows move the pointer now:
    // the region stays where the user left it and the aim is what travels.
    void arrowsMoveThePointerRatherThanTheRegion() {
        QImage image(1120, 720, QImage::Format_ARGB32);
        // A screen that changes from column to column, so a pointer that has moved
        // shows a different enlargement even when nothing else about it has.
        for (int column = 0; column < image.width(); ++column)
            for (int row = 0; row < image.height(); ++row)
                image.setPixelColor(column, row, QColor(column % 256, 200, 200));
        ScreenFrame frame{"arrows", {0, 0, 560, 360}, {0, 0, 1120, 720}, image, true};
        Overlay overlay(frame);
        overlay.show();
        overlay.setFixedSize(560, 360);
        QTRY_VERIFY_WITH_TIMEOUT(overlay.findChildren<QFutureWatcherBase *>().isEmpty(), 5000);
        QTest::mousePress(&overlay, Qt::LeftButton, Qt::NoModifier, {100, 100});
        QTest::mouseMove(&overlay, {300, 260});
        QTest::mouseRelease(&overlay, Qt::LeftButton, Qt::NoModifier, {300, 260});
        const QRect settled = overlay.selection();
        QVERIFY(!settled.isEmpty());
        // Inside the region, which is what brings the enlargement up.
        movePointerTo(overlay, {200, 180});
        QTest::qWait(50);
        const QRect panel = magnifierPanel(overlay, QPoint(200, 180), QSize(560, 360));
        const QRect zoom(panel.topLeft() + QPoint(magnifierPanelPadding, magnifierPanelPadding),
                         QSize(magnifierPanelWidth - magnifierPanelPadding * 2, magnifierZoomHeight));
        const QImage before = overlay.grab().toImage().convertedTo(QImage::Format_ARGB32);
        QTest::keyClick(&overlay, Qt::Key_Right);
        QTest::qWait(50);
        const QImage after = overlay.grab().toImage().convertedTo(QImage::Format_ARGB32);
        artifact(overlay, "magnifier-arrow.png");
        QVERIFY2(overlay.selection() == settled,
                 "an arrow key must not move the region that has just been taken");
        QVERIFY2(before.copy(zoom) != after.copy(zoom),
                 "the arrow key has to move the pointer the enlargement is aimed with");
        overlay.hide();
    }
    // REG-076: a shadow is drawn for the eye. Reading text used to be handed the
    // decorated picture, so every line box was measured from an origin that had been
    // pushed out by the halo and the recogniser was asked to look at a dark border.
    void textIsReadFromTheRegionRatherThanFromItsDecoration() {
        QImage image = exampleImage();
        ScreenFrame frame{"ocr-source", {0, 0, 560, 360}, {0, 0, 1120, 720}, image, true};
        Overlay overlay(frame);
        overlay.show();
        overlay.setFixedSize(560, 360);
        QTest::qWait(50);
        QTest::mousePress(&overlay, Qt::LeftButton, Qt::NoModifier, {30, 70});
        QTest::mouseMove(&overlay, {300, 260});
        QTest::mouseRelease(&overlay, Qt::LeftButton, Qt::NoModifier, {300, 260});
        const QImage bare = overlay.selectionPixels();
        QCOMPARE(bare, image.copy(overlay.selection()));
        // The decorated picture is the one the picture-making actions get, and it is
        // bigger by the halo; the bare one has to stay bare either way.
        CaptureStyle shaded;
        shaded.cornerRadius = 12;
        overlay.setStyle(shaded);
        const int reach = captureShadowRadius(shaded.shadow, shaded.shadowStrength);
        QVERIFY(reach > 0);
        QCOMPARE(overlay.selectionImage().size(), bare.size() + QSize(reach * 2, reach * 2));
        QCOMPARE(overlay.selectionPixels(), bare);
        overlay.hide();
    }
    void detectedBlockClickImmediatelyStartsAnnotation() {
        QImage image(160, 120, QImage::Format_ARGB32);
        image.fill(Qt::white);
        ScreenFrame frame{"click-test", {0, 0, 160, 120}, {}, image, false};
        Overlay overlay(frame);
        overlay.show();
        QSignalSpy accepted(&overlay, &Overlay::accepted);
        QTRY_VERIFY_WITH_TIMEOUT(overlay.findChildren<QFutureWatcherBase *>().isEmpty(), 5000);
        QTest::mouseMove(&overlay, {80, 60});
        QTest::mousePress(&overlay, Qt::LeftButton, Qt::NoModifier, {80, 60});
        QCOMPARE(accepted.count(), 0);
        QTest::mouseRelease(&overlay, Qt::LeftButton, Qt::NoModifier, {80, 60});
        // The block becomes the region, and 批注 is what takes it into the editor.
        QCOMPARE(accepted.count(), 0);
        QVERIFY(!overlay.findChildren<QPushButton *>().isEmpty());
        QTest::keyClick(&overlay, Qt::Key_Return);
        QCOMPARE(accepted.count(), 1);
        QCOMPARE(accepted.first().first().toRect(), QRect(0, 0, 160, 120));
        QTest::mouseClick(&overlay, Qt::LeftButton, Qt::NoModifier, {80, 60});
        QCOMPARE(accepted.count(), 1);
        overlay.hide();

        Overlay cancelledOverlay(frame);
        cancelledOverlay.show();
        QSignalSpy cancelled(&cancelledOverlay, &Overlay::cancelled);
        QSignalSpy notAccepted(&cancelledOverlay, &Overlay::accepted);
        QTest::keyClick(&cancelledOverlay, Qt::Key_Escape);
        QCOMPARE(cancelled.count(), 1);
        QCOMPARE(notAccepted.count(), 0);
        cancelledOverlay.hide();
    }
};
QTEST_MAIN(UiTests)
#include "ui_test.moc"
