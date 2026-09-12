#include "inlinenoteedit.h"

#include <QApplication>
#include <QDir>
#include <QFontDatabase>
#include <QGraphicsEffect>
#include <QLineEdit>
#include <QScrollBar>
#include <QTest>
#include <QTextBlock>
#include <QVBoxLayout>
#include <QWheelEvent>

using namespace h2d;

class InlineNoteEditTests : public QObject {
    Q_OBJECT

    static void prepare(InlineNoteEdit &edit, int width = 320) {
        edit.setStyleSheet("QPlainTextEdit { border: 1px solid transparent; padding: 5px; font-size: 12px; background: transparent; }");
        edit.resize(width, 60);
        edit.show();
        QApplication::processEvents();
        edit.clearFocus();
        edit.setExpanded(false);
    }

  private slots:
    void initTestCase() {
#ifdef Q_OS_WIN
        QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");
        QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");
        QApplication::setFont(QFont("Microsoft YaHei UI", 9));
#endif
    }

    void shortNotesDoNotCollapseAndWrappedLinesDo() {
        InlineNoteEdit edit;
        prepare(edit);
        edit.setPlainText(QStringLiteral("第一行\n第二行"));
        QVERIFY(!edit.needsCollapse());
        QVERIFY(!edit.viewport()->graphicsEffect()->isEnabled());
        const int twoLineHeight = edit.height();
        edit.setPlainText(QStringLiteral("第一行\n第二行\n第三行"));
        QVERIFY(edit.needsCollapse());
        QVERIFY(edit.viewport()->graphicsEffect()->isEnabled());
        QCOMPARE(edit.height(), twoLineHeight);
        int presentations = 0;
        edit.presentationChanged = [&] { ++presentations; };
        edit.setExpanded(true);
        QVERIFY(edit.isExpanded());
        QVERIFY(edit.height() > twoLineHeight);
        QVERIFY(!edit.viewport()->graphicsEffect()->isEnabled());
        QVERIFY(presentations > 0);
        edit.setPlainText(QStringLiteral("短批注"));
        QVERIFY(!edit.needsCollapse());
    }

    void expandedTextUsesOuterScrollingAndKeepsFullContentVisible() {
        InlineNoteEdit edit;
        prepare(edit);
        QStringList paragraphs;
        for (int i = 0; i < 60; ++i)
            paragraphs << QStringLiteral("第 %1 行：让整个页面的风格更加协调。这里保留完整描述。").arg(i + 1);
        edit.setPlainText(paragraphs.join('\n'));
        const int collapsedHeight = edit.height();
        edit.setExpanded(true);
        QApplication::processEvents();
        QVERIFY(edit.height() > 800);
        QCOMPARE(edit.verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
        QCOMPARE(edit.horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
        QCOMPARE(edit.verticalScrollBar()->maximum(), 0);
        QCOMPARE(edit.horizontalScrollBar()->maximum(), 0);
        edit.moveCursor(QTextCursor::End);
        QVERIFY(edit.viewport()->rect().contains(edit.cursorRect().center()));
        QCOMPARE(edit.verticalScrollBar()->value(), 0);
        edit.setExpanded(false);
        QCOMPARE(edit.height(), collapsedHeight);
        QCOMPARE(edit.verticalScrollBar()->value(), 0);
        QWheelEvent wheel(QPointF(20, 20), QPointF(edit.mapToGlobal(QPoint(20, 20))), {},
                          QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(edit.viewport(), &wheel);
        QVERIFY(!wheel.isAccepted());
        QCOMPARE(edit.verticalScrollBar()->value(), 0);
    }

    void focusExpandsAndOnlyOneSaveOrCancelCallbackRuns() {
        QWidget window;
        QVBoxLayout layout(&window);
        QLineEdit other;
        InlineNoteEdit edit;
        edit.setPlainText(QStringLiteral("第一行\n第二行\n第三行"));
        layout.addWidget(&other);
        layout.addWidget(&edit);
        window.resize(360, 200);
        window.show();
        other.setFocus();
        QApplication::processEvents();
        edit.setExpanded(false);
        int starts = 0, finishes = 0, cancels = 0;
        edit.started = [&] { ++starts; };
        edit.finished = [&] { ++finishes; };
        edit.cancelled = [&] { ++cancels; };
        edit.setFocus();
        QApplication::processEvents();
        QVERIFY(edit.isExpanded());
        QCOMPARE(starts, 1);
        other.setFocus();
        QCOMPARE(finishes, 1);
        QVERIFY(edit.isExpanded());
        edit.setFocus();
        QTest::keyClick(&edit, Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(finishes, 2);
        QCOMPARE(cancels, 0);
        edit.setFocus();
        QTest::keyClick(&edit, Qt::Key_Escape);
        QCOMPARE(finishes, 2);
        QCOMPARE(cancels, 1);
        edit.setFocus();
        QTest::keyClick(&edit, Qt::Key_Return, Qt::MetaModifier);
        QCOMPARE(finishes, 3);
    }

    void resizingRecountsVisualLinesAndExpandsWithoutAnInternalScrollRange() {
        InlineNoteEdit edit;
        prepare(edit, 460);
        edit.setPlainText(QStringLiteral("简洁的界面需要保留足够的呼吸感，色彩与排版应当传达一致的设计语言。"));
        QVERIFY(!edit.needsCollapse());
        edit.resize(130, edit.height());
        QApplication::processEvents();
        QVERIFY(edit.needsCollapse());
        edit.setExpanded(true);
        const int narrowHeight = edit.height();
        QCOMPARE(edit.verticalScrollBar()->maximum(), 0);
        edit.resize(460, edit.height());
        QApplication::processEvents();
        QVERIFY(!edit.needsCollapse());
        QVERIFY(edit.height() < narrowHeight);
        QCOMPARE(edit.verticalScrollBar()->maximum(), 0);
    }

    void collapsedFadeKeepsTheCardBackground() {
        QWidget card;
        card.setStyleSheet("QWidget { background: #254f60; } QPlainTextEdit { background: transparent; color: white; border: none; padding: 5px; font-size: 20px; }");
        QVBoxLayout layout(&card);
        InlineNoteEdit edit;
        edit.setPlainText(QStringLiteral("████████████\n████████████\n████████████"));
        layout.addWidget(&edit);
        card.resize(360, 140);
        card.show();
        QApplication::processEvents();
        edit.clearFocus();
        edit.setExpanded(false);
        QApplication::processEvents();
        const QImage image = card.grab().toImage().scaled(card.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        const QPoint bottomRight = edit.viewport()->mapTo(&card, QPoint(edit.viewport()->width() - 3,
                                                                        edit.viewport()->height() - 3));
        QCOMPARE(image.pixelColor(bottomRight), QColor("#254f60"));
        QTextCursor secondLine(edit.document()->findBlockByNumber(1));
        const QRect secondRect = edit.cursorRect(secondLine);
        const QPoint secondTop = edit.viewport()->mapTo(&card, secondRect.topLeft());
        const QColor fadeTop = image.pixelColor(secondTop + QPoint(5, 3));
        const QColor fadeBottom = image.pixelColor(secondTop + QPoint(5, secondRect.height() - 4));
        QVERIFY2(fadeTop.red() > fadeBottom.red() + 60,
                 "The second line must fade downward, not remain opaque.");
        const QString artifacts = qEnvironmentVariable("H2D_TEST_ARTIFACTS");
        if (!artifacts.isEmpty()) {
            QDir().mkpath(artifacts);
            QVERIFY(image.save(QDir(artifacts).filePath("inline-note-collapsed.png")));
            edit.setExpanded(true);
            QApplication::processEvents();
            QVERIFY(card.grab().save(QDir(artifacts).filePath("inline-note-expanded.png")));
        }
    }
};

QTEST_MAIN(InlineNoteEditTests)
#include "inlinenoteedit_test.moc"
