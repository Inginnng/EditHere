#include "capturesession.h"
#include "capturetoolbar.h"
#include "model.h"
#include "pinwindow.h"
#include "ui.h"
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <memory>
using namespace h2d;
class CaptureTests : public QObject {
    Q_OBJECT
    // Every row gets a colour of its own, so no wrong overlap can accidentally look
    // like the right one. The three multipliers are coprime with the modulus, which
    // is what keeps the mapping from repeating within the height of a test picture.
    static QImage stripedImage(int width, int height) {
        QImage image(width, height, QImage::Format_ARGB32);
        for (int row = 0; row < height; ++row) {
            const QRgb color = qRgb(row % 251, (row * 7) % 251, (row * 13) % 251);
            for (int column = 0; column < width; ++column)
                image.setPixel(column, row, color);
        }
        return image;
    }
    // A capture starts with a shadow on, so a test that wants the bare pixels has to
    // ask for them instead of relying on what a default style happens to be.
    static CaptureStyle bareStyle() {
        CaptureStyle style;
        style.shadow = false;
        return style;
    }
    // Deterministic noise, so two pictures have nothing in common.
    static QImage noisyImage(int width, int height, quint32 seed) {
        QImage image(width, height, QImage::Format_ARGB32);
        quint32 state = seed;
        for (int row = 0; row < height; ++row) {
            for (int column = 0; column < width; ++column) {
                state = state * 1664525u + 1013904223u;
                image.setPixel(column, row, qRgb((state >> 16) & 0xff, (state >> 8) & 0xff, state & 0xff));
            }
        }
        return image;
    }
  private slots:
    void initTestCase() {
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() == "offscreen") {
            const QString fonts = qEnvironmentVariable("WINDIR") + "/Fonts/";
            QVERIFY(QFontDatabase::addApplicationFont(fonts + "segoeui.ttf") >= 0);
            QVERIFY(QFontDatabase::addApplicationFont(fonts + "msyh.ttc") >= 0);
        }
#endif
    }

    void ratiosKnowTheirSizeAndName() {
        QCOMPARE(captureRatioSize(CaptureRatio::Free), QSize());
        QCOMPARE(captureRatioSize(CaptureRatio::Square), QSize(1, 1));
        QCOMPARE(captureRatioSize(CaptureRatio::SixteenNine), QSize(16, 9));
        QCOMPARE(captureRatioSize(CaptureRatio::Custom, 21, 9), QSize(21, 9));
        // A custom ratio of nothing still has to describe a shape.
        QCOMPARE(captureRatioSize(CaptureRatio::Custom, 0, 0), QSize(1, 1));
        QCOMPARE(captureRatioLabel(CaptureRatio::ThreeTwo), QStringLiteral("3:2"));
        QCOMPARE(captureRatioLabel(CaptureRatio::Custom, 21, 9), QStringLiteral("21:9"));
        QCOMPARE(captureRatioLabel(CaptureRatio::NineSixteen), QStringLiteral("9:16"));
        QVERIFY(!captureRatioLabel(CaptureRatio::Free).isEmpty());
    }
    void aLockedRatioShapesTheDrag() {
        QCOMPARE(captureRectFromDrag(QPoint(10, 10), QPoint(110, 50), CaptureRatio::Square),
                 QRect(10, 10, 100, 100));
        // Dragging towards the top left keeps the anchor pinned: the region grows the
        // same way whichever direction the pointer went.
        const QRect upwards = captureRectFromDrag(QPoint(200, 200), QPoint(100, 160), CaptureRatio::Square);
        QCOMPARE(upwards.width(), 100);
        QCOMPARE(upwards.height(), 100);
        QCOMPARE(upwards.right(), 199);
        QCOMPARE(upwards.bottom(), 199);
        // 16:9 over a wide drag is decided by the width.
        const QRect wide = captureRectFromDrag(QPoint(0, 0), QPoint(160, 40), CaptureRatio::SixteenNine);
        QCOMPARE(wide, QRect(0, 0, 160, 90));
        // A drag with no extent still describes a shape rather than nothing.
        const QRect dot = captureRectFromDrag(QPoint(5, 5), QPoint(5, 5), CaptureRatio::ThreeTwo);
        QVERIFY(!dot.isEmpty());
        QCOMPARE(dot, QRect(5, 5, 1, 1));
    }
    void anUnlockedDragAgreesWithTheModel() {
        // The window draws the region with dragRect() while the pointer is down and
        // commits it through captureRectFromDrag(), so the two have to describe the
        // same rectangle or the picture would not be of what was on screen. The model
        // clamps to the screen as it draws, so a drag that runs off the edge is
        // compared once both have been confined to it.
        const QSize bounds(1920, 1080);
        // The model clamps to the screen as it draws, so the same confinement is
        // applied here. It is written out rather than intersected(), because Qt turns
        // an empty rectangle into a null one and a zero-extent drag has to keep the
        // corner it was made at.
        const auto confine = [&bounds](QRect r) {
            const int left = std::clamp(r.left(), 0, bounds.width());
            const int top = std::clamp(r.top(), 0, bounds.height());
            const int right = std::clamp(r.left() + r.width(), 0, bounds.width());
            const int bottom = std::clamp(r.top() + r.height(), 0, bounds.height());
            return QRect(left, top, right - left, bottom - top);
        };
        for (const auto &pair : QVector<QPair<QPoint, QPoint>>{{QPoint(10, 20), QPoint(210, 120)},
                                                              {QPoint(210, 120), QPoint(10, 20)},
                                                              {QPoint(500, 500), QPoint(500, 500)},
                                                              {QPoint(1900, 20), QPoint(2100, 1300)}}) {
            QCOMPARE(confine(captureRectFromDrag(pair.first, pair.second, CaptureRatio::Free)),
                     dragRect(pair.first, pair.second, bounds));
        }
    }
    void reshapingKeepsTheCornerAndTheWidth() {
        const QRect current(40, 60, 300, 100);
        const QRect square = captureRectWithRatio(current, CaptureRatio::Square);
        QCOMPARE(square.topLeft(), current.topLeft());
        QCOMPARE(square.width(), 300);
        QCOMPARE(square.height(), 300);
        // Free means the rectangle is left exactly as it is.
        QCOMPARE(captureRectWithRatio(current, CaptureRatio::Free), current);
        QCOMPARE(captureRectWithRatio(QRect(), CaptureRatio::Square), QRect());
        // A ratio that would collapse the selection still leaves one row.
        const QRect thin = captureRectWithRatio(QRect(0, 0, 4, 100), CaptureRatio::NineSixteen);
        QCOMPARE(thin.width(), 4);
        QVERIFY(thin.height() >= 1);
    }
    void aTypedSizeSetsTheWidthAndTheRatioDecidesTheHeight() {
        const QRect current(10, 20, 100, 100);
        const QRect free = captureRectWithSize(current, QSize(640, 480), CaptureRatio::Free);
        QCOMPARE(free, QRect(10, 20, 640, 480));
        // With a locked ratio the two numbers cannot contradict each other, so the
        // height follows from the width instead of being taken as typed.
        const QRect locked = captureRectWithSize(current, QSize(640, 480), CaptureRatio::SixteenNine);
        QCOMPARE(locked.topLeft(), current.topLeft());
        QCOMPARE(locked.width(), 640);
        QCOMPARE(locked.height(), 360);
        // A size of zero would make the selection disappear, which is never wanted.
        const QRect empty = captureRectWithSize(current, QSize(0, 0), CaptureRatio::Free);
        QCOMPARE(empty.size(), QSize(1, 1));
    }
    // REG-068: a pinned region used to be centred on the screen, so the picture the
    // user had just framed appeared nowhere near the thing it was a picture of. A pin
    // that came from a capture belongs where that capture came from.
    void aPinnedRegionKeepsThePlaceItWasTakenFrom() {
        // A region that was 200x150 on a display the window manager measures at half
        // the pixels the screen was read at, which is what a scaled display looks like.
        QImage picture(400, 300, QImage::Format_ARGB32);
        picture.fill(Qt::red);
        const QRect taken(120, 80, 200, 150);
        PinWindow window(picture, taken);
        QCOMPARE(window.placement(), taken);
        QVERIFY(!window.image().isNull());
        // It is drawn at the size the region was on screen, not at twice that, and it
        // is put at the corner it came from rather than in the middle of the screen.
        QCOMPARE(window.size(), taken.size());
        QCOMPARE(window.pos(), taken.topLeft());
        // A pin with no place to go back to is the editor's kind: it has nothing to
        // remember and keeps the picture at its own size.
        PinWindow centred(picture);
        QVERIFY(centred.placement().isEmpty());
        QCOMPARE(centred.baseSize(), picture.size());
        window.hide();
        centred.hide();
    }
    void aStyleThatChangesNothingReturnsTheSamePixels() {
        const QImage region = stripedImage(40, 30);
        const QImage composed = composeCapture(region, bareStyle());
        // The pixels come back untouched rather than copied, which keeps the common
        // case cheap and the identity exact.
        QCOMPARE(composed.constBits(), region.constBits());
        QVERIFY(composeCapture(QImage(), bareStyle()).isNull());
        // A radius too large for the picture is clamped rather than refused.
        QImage small = stripedImage(6, 4);
        CaptureStyle huge = bareStyle();
        huge.cornerRadius = 500;
        QCOMPARE(composeCapture(small, huge).size(), small.size());
    }
    void roundedCornersAndAShadowChangeTheEdges() {
        const QImage region = stripedImage(60, 40);
        CaptureStyle rounded = bareStyle();
        rounded.cornerRadius = 12;
        const QImage withCorners = composeCapture(region, rounded);
        QCOMPARE(withCorners.size(), region.size());
        QCOMPARE(withCorners.pixelColor(0, 0).alpha(), 0);
        QCOMPARE(withCorners.pixelColor(30, 20).alpha(), 255);
        QCOMPARE(withCorners.pixelColor(30, 20).rgb(), region.pixelColor(30, 20).rgb());
        // A shadow needs room around the picture, so the result grows by its radius
        // on every side and the middle stays where it was. The strength is the only
        // number the panel offers, so the radius is asked for rather than assumed.
        CaptureStyle shadowed;
        shadowed.shadow = true;
        shadowed.shadowStrength = 60;
        const int reach = captureShadowRadius(shadowed.shadow, shadowed.shadowStrength);
        QVERIFY2(reach > 0, "a shadow at this strength has to reach somewhere");
        const QImage withShadow = composeCapture(region, shadowed);
        QCOMPARE(withShadow.size(), region.size() + QSize(reach * 2, reach * 2));
        QCOMPARE(withShadow.pixelColor(reach, reach).rgb(), region.pixelColor(0, 0).rgb());
        QCOMPARE(withShadow.pixelColor(reach, reach).alpha(), 255);
        // The outermost pixel of the halo is where it has finished fading, so it is
        // transparent; one step further in it has to be on show at last.
        QCOMPARE(withShadow.pixelColor(0, reach).alpha(), 0);
        QVERIFY2(withShadow.pixelColor(reach / 2, reach).alpha() > 0, "the halo has to be visible");
        QVERIFY2(withShadow.pixelColor(reach / 2, reach).alpha() < 255,
                 "the halo has to be softer than the picture");
        // A border darkens the edge just inside the picture.
        const QImage region2 = stripedImage(60, 40);
        CaptureStyle bordered = bareStyle();
        bordered.border = true;
        bordered.borderWidth = 4;
        bordered.borderColor = QColor(255, 0, 0, 255);
        const QImage withBorder = composeCapture(region2, bordered);
        QCOMPARE(withBorder.size(), region2.size());
        QCOMPARE(withBorder.pixelColor(30, 1).rgb(), QColor(255, 0, 0, 255).rgb());
        QCOMPARE(withBorder.pixelColor(30, 20).rgb(), region2.pixelColor(30, 20).rgb());
    }
    // REG-066: the first shadow was built by stacking rounded rings of falling
    // transparency, and it could not be seen at all. The outermost ring was already
    // almost transparent and the innermost was painted underneath the picture, so what
    // survived was a step rather than a falloff, and only a step of nearly nothing.
    // A shadow has to be measured along the way out, not just at the ends.
    void theHaloFadesSmoothlyAndIsNeverInvisible() {
        const QImage region = stripedImage(80, 60);
        CaptureStyle style;
        style.shadow = true;
        style.shadowStrength = 60;
        const int reach = captureShadowRadius(style.shadow, style.shadowStrength);
        const QImage shaded = composeCapture(region, style);
        QCOMPARE(shaded.size(), region.size() + QSize(reach * 2, reach * 2));
        // Walking out from the edge of the picture the halo must fall: never rising,
        // and never flat all the way to nothing.
        const int middle = shaded.height() / 2;
        int previous = 256;
        int visible = 0;
        for (int x = reach - 1; x >= 0; --x) {
            const int alpha = shaded.pixelColor(x, middle).alpha();
            QVERIFY2(alpha <= previous, "the halo must not get darker further from the picture");
            if (alpha > 0)
                ++visible;
            previous = alpha;
        }
        QVERIFY2(visible >= reach / 2,
                 "more than a sliver of the halo has to be visible, which is what went wrong");
        // It also has to be strongest right where it leaves the picture.
        QVERIFY2(shaded.pixelColor(reach - 1, middle).alpha() > shaded.pixelColor(0, middle).alpha(),
                 "the halo has to be strongest against the picture");
        // Off is off: nothing is added and nothing is shifted. A capture starts with a
        // shadow, so "off" has to be asked for rather than assumed.
        CaptureStyle off = bareStyle();
        QCOMPARE(composeCapture(region, off).constBits(), region.constBits());
        off.shadow = true;
        off.shadowStrength = 0;
        QCOMPARE(captureShadowRadius(off.shadow, off.shadowStrength), 0);
        QCOMPARE(composeCapture(region, off).constBits(), region.constBits());
    }
    // REG-070: a shadow that only ever appeared in the copied file was a shadow nobody
    // could tell was on, which is how it went unnoticed twice. The capture window has
    // to be able to draw the halo around the region before anything is taken.
    void theShadowIsDrawnAroundTheRegionBeforeItIsTaken() {
        const QSize body(80, 60);
        // And a capture has to start with one on, which is the point: a shadow that has
        // to be found in a panel first is a shadow nobody ever sees.
        CaptureStyle fresh;
        QVERIFY(fresh.shadow);
        QVERIFY(!composeShadowPreview(body, fresh).isNull());
        CaptureStyle off = bareStyle();
        QVERIFY(composeShadowPreview(body, off).isNull());
        CaptureStyle style = bareStyle();
        style.shadow = true;
        style.shadowStrength = 60;
        const int reach = captureShadowRadius(style.shadow, style.shadowStrength);
        const QImage halo = composeShadowPreview(body, style);
        QCOMPARE(halo.size(), body + QSize(reach * 2, reach * 2));
        // The middle is cut out, because what is under the picture is the picture and
        // not the halo at its darkest.
        QCOMPARE(halo.pixelColor(reach + 40, reach + 30).alpha(), 0);
        // It reaches every side, and it reaches the corners too: a halo measured from
        // a capsule rather than from the rectangle leaves those bare, which on a wide
        // picture is most of the outline.
        QVERIFY(halo.pixelColor(reach / 2, reach + 30).alpha() > 0);
        QVERIFY(halo.pixelColor(halo.width() - 1 - reach / 2, reach + 30).alpha() > 0);
        QVERIFY(halo.pixelColor(reach + 40, reach / 2).alpha() > 0);
        QVERIFY(halo.pixelColor(reach + 40, halo.height() - 1 - reach / 2).alpha() > 0);
        QVERIFY2(halo.pixelColor(reach / 2, reach / 2).alpha() > 0,
                 "the corner of a rectangle has to cast a shadow too");
        // Nothing to cast one is nothing to draw.
        QVERIFY(composeShadowPreview(QSize(), style).isNull());
    }
    // REG-077: the halo was cast in black, so it read as a grey smudge rather than as
    // anything to do with this program. It is cast in the accent the interface is
    // drawn in, which is the same blue the selection is outlined in.
    void aShadowIsCastInTheAccentRatherThanInBlack() {
        CaptureStyle style; // a capture starts with one on
        QVERIFY(style.shadow);
        const QColor tint = style.shadowTint();
        QVERIFY(tint.alpha() > 0);
        QCOMPARE(tint.rgb(), accent().rgb());
        QVERIFY2(tint.blue() > tint.red() + 40, "the halo has to read as blue, not as grey");
        // Only the colour differs: the reach and the falloff are the same halo as when
        // it was black, so it is still a shadow and not a coloured border.
        CaptureStyle black = style;
        black.shadowColor = QColor(0, 0, 0);
        QCOMPARE(black.shadowRadius(), style.shadowRadius());
        QCOMPARE(black.shadowTint().alpha(), tint.alpha());
        QCOMPARE(black.shadowTint().rgb(), QColor(0, 0, 0).rgb());
        // Off is still off whatever the colour is.
        CaptureStyle off = style;
        off.shadow = false;
        QCOMPARE(off.shadowTint().alpha(), 0);
    }
    // REG-078: the halo was rebuilt on every repaint, and a repaint happens on every
    // move of the pointer. On a full-screen selection that is a couple of million
    // square roots per frame, which is what made the capture window stutter.
    void theSameHaloIsNotBuiltTwice() {
        CaptureStyle style; // a capture starts with a shadow on
        const QSize body(1200, 800);
        const QImage first = composeShadowPreview(body, style);
        QVERIFY(!first.isNull());
        const QImage second = composeShadowPreview(body, style);
        // The very same pixels handed back, not a fresh copy of them.
        QCOMPARE(first.constBits(), second.constBits());
        // A different region or a different style is genuinely a different halo, and
        // asking for one of those is what makes the next ask for this one a rebuild.
        CaptureStyle weaker = style;
        weaker.shadowStrength = 20;
        QVERIFY(composeShadowPreview(body, weaker).constBits() != first.constBits());
        QVERIFY(composeShadowPreview(QSize(1201, 800), style).constBits() != first.constBits());
        // And with the shadow off there is no halo to keep at all.
        CaptureStyle off = bareStyle();
        QVERIFY(composeShadowPreview(body, off).isNull());
    }
    // REG-071: a pin of a capture came out soft, which looks like a compression
    // artefact and is not one. The picture is made of device pixels and the window is
    // measured in logical ones; drawing the first into a bitmap the size of the second
    // threw away three pixels out of four on a scaled display and then interpolated
    // them back, which no amount of quality in the copy can undo.
    void aPinnedRegionKeepsEveryPixelItWasGiven() {
        // 400x300 pixels out of a region the window manager measured at 200x150.
        QImage picture(400, 300, QImage::Format_ARGB32);
        picture.fill(Qt::red);
        PinWindow window(picture, QRect(120, 80, 200, 150));
        QCOMPARE(window.size(), QSize(200, 150));
        QCOMPARE(window.display().size(), picture.size());
        QCOMPARE(window.display().devicePixelRatio(), 2.0);
        window.hide();
    }
    // REG-072: the pin was the one place a captured picture could not be handed to the
    // editor from. Annotating is the reason the program exists, so it leads the menu.
    void aPinnedPictureCanAlwaysBeAnnotated() {
        QImage picture(40, 30, QImage::Format_ARGB32);
        picture.fill(Qt::blue);
        PinWindow window(picture);
        QSignalSpy annotated(&window, &PinWindow::annotateRequested);
        const std::unique_ptr<QMenu> menu(window.createContextMenu());
        const auto actions = menu->actions();
        QVERIFY(!actions.isEmpty());
        QCOMPARE(actions.first()->text(), QStringLiteral("批注"));
        actions.first()->trigger();
        QCOMPARE(annotated.count(), 1);
        // What is handed over is the picture as it is shown, shadow and all, and not a
        // copy the editor has to guess the size of.
        QCOMPARE(annotated.at(0).at(0).value<QImage>().size(), picture.size());
        window.hide();
    }
    // REG-074: a double click on a pin used to copy it. Copying already has Ctrl+C and
    // a menu entry; what a window this small needs is a way to be put away, and that
    // is what a double click is for everywhere else.
    void aDoubleClickPutsAPinAway() {
        QImage picture(40, 30, QImage::Format_ARGB32);
        picture.fill(Qt::blue);
        PinWindow window(picture);
        window.show();
        QVERIFY(window.isVisible());
        QSignalSpy copied(&window, &PinWindow::copyRequested);
        QTest::mouseDClick(&window, Qt::LeftButton, Qt::NoModifier, {20, 15});
        QVERIFY(!window.isVisible());
        QCOMPARE(copied.count(), 0);
    }
    // REG-075: with a shadow on by default the picture a pin holds is bigger than the
    // screenshot inside it. The editor is handed the screenshot, because a margin of
    // nothing is not something to mark up.
    void aPinnedPictureIsAnnotatedWithoutItsShadow() {
        // A 60x50 screenshot inside a 5px halo on every side.
        QImage picture(70, 60, QImage::Format_ARGB32);
        picture.fill(Qt::transparent);
        PinWindow window(picture);
        QCOMPARE(window.editableImage().size(), picture.size());
        window.setDecoration(5);
        QCOMPARE(window.decoration(), 5);
        QCOMPARE(window.editableImage().size(), QSize(60, 50));
        // A margin that makes no sense is refused rather than cropped into nothing.
        window.setDecoration(-4);
        QCOMPARE(window.decoration(), 0);
        window.setDecoration(400);
        QCOMPARE(window.editableImage().size(), picture.size());
        window.hide();
    }
    static QAction *actionCalled(QMenu *menu, const QString &text) {
        for (auto *action : menu->actions())
            if (action->text() == text)
                return action;
        return nullptr;
    }
    // Dragging an edge is how a pin is resized: the menu no longer carries zoom
    // buttons for it, because a picture this size is grabbed by its border.
    void anEdgeOfAPinIsDraggedToResizeIt() {
        QImage picture(200, 150, QImage::Format_ARGB32);
        picture.fill(Qt::green);
        PinWindow window(picture);
        window.show();
        const QSize before = window.size();
        const QPoint grab(before.width() - 2, before.height() / 2);
        QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, grab);
        // The pointer near an edge announces itself before the button goes down, so
        // the cursor is what says a corner can be pulled.
        QCOMPARE(window.cursor().shape(), Qt::SizeHorCursor);
        QTest::mouseMove(&window, grab + QPoint(40, 0));
        QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, grab + QPoint(40, 0));
        QVERIFY2(window.width() > before.width(), "dragging the right edge did not widen the pin");
        // A drag is a change of zoom, so the wheel carries on from where it stopped
        // rather than jumping back to the size the pin arrived at.
        QCOMPARE(window.display().width(), qRound(window.width() * window.display().devicePixelRatio()));
        // Dragged inwards it stops at a size that can still be grabbed.
        const QPoint middle(window.width() / 2, window.height() - 2);
        QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, middle);
        QTest::mouseMove(&window, middle + QPoint(0, -4000));
        QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, middle + QPoint(0, -4000));
        QVERIFY2(window.height() >= 24, "a pin can be dragged down to a size it cannot be grabbed by");
        window.hide();
    }
    // A pinned picture is a reference, and a reference is read at the angle it is
    // needed at rather than at the angle it was taken at.
    void aPinCanBeTurnedAndTurnedOver() {
        QImage picture(200, 150, QImage::Format_ARGB32);
        picture.fill(Qt::red);
        for (int row = 0; row < picture.height(); ++row)
            for (int column = picture.width() / 2; column < picture.width(); ++column)
                picture.setPixel(column, row, qRgb(0, 0, 255));
        PinWindow window(picture);
        window.show();
        window.rotate(1);
        QCOMPARE(window.image().size(), QSize(150, 200));
        // Four quarters is the way it started, and nothing is left turned by accident.
        window.rotate(3);
        QCOMPARE(window.image().size(), QSize(200, 150));
        window.flip(true);
        QCOMPARE(window.image().size(), QSize(200, 150));
        // What was on the right is now on the left, which is the whole of what a
        // mirror has to do.
        QCOMPARE(window.image().pixelColor(1, 10), QColor(0, 0, 255));
        QCOMPARE(window.image().pixelColor(picture.width() - 2, 10), QColor(255, 0, 0));
        window.hide();
    }
    // The shadow a pin was taken with can be taken off afterwards: it is part of the
    // style, not part of the screenshot.
    void theShadowOfAPinCanBeSwitchedOffAndOn() {
        const QImage region = stripedImage(60, 40);
        CaptureStyle style = bareStyle();
        style.shadow = true;
        style.shadowStrength = 42;
        PinWindow window(composeCapture(region, style));
        window.setUndecorated(region, style);
        window.setDecoration(style.shadowRadius());
        window.show();
        QVERIFY2(window.canToggleShadow(), "a pin of a capture has to be able to lose its shadow");
        QVERIFY(window.shadowEnabled());
        const QSize decorated = window.image().size();
        QVERIFY(decorated.width() > region.width());
        window.setShadowEnabled(false);
        QCOMPARE(window.image().size(), region.size());
        QVERIFY(!window.shadowEnabled());
        window.setShadowEnabled(true);
        QCOMPARE(window.image().size(), decorated);
        // Annotating still gets the screenshot inside the halo either way.
        QCOMPARE(window.editableImage().size(), region.size());
        window.hide();
    }
    // A pin used to wear a solid accent line around the very outside of the window,
    // which is not the outside of the picture: the window is the picture plus the
    // shadow's reach, so the line landed out in the halo and a soft shadow arrived
    // framed by a hard rectangle. The shadow is what sets a pin apart from the
    // desktop; the line is only for a pin that has no shadow to do it.
    void aPinWithAShadowIsEdgedByTheShadowAndNotByALine() {
        const QImage region = stripedImage(60, 40);
        CaptureStyle style = bareStyle();
        style.shadow = true;
        style.shadowStrength = 100;
        PinWindow window(composeCapture(region, style));
        window.setUndecorated(region, style);
        window.setDecoration(style.shadowRadius());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const int halo = window.decoration();
        QVERIFY2(halo >= 8, "the shadow has to be wide enough for the two to be told apart");
        const QImage shot = window.grab().toImage().convertedTo(QImage::Format_ARGB32);
        QVERIFY2(shot.width() > halo * 3, "the halo has to be a margin, not the whole pin");
        // The outermost columns are where the shadow has all but faded out. A line
        // drawn there would make them opaque accent instead of nearly nothing.
        const int middle = shot.height() / 2;
        QVERIFY2(qAlpha(shot.pixel(0, middle)) < 200,
                 qPrintable(QStringLiteral("the left rim of a pin is halo, not a drawn line (alpha %1)")
                                .arg(qAlpha(shot.pixel(0, middle)))));
        QVERIFY2(qAlpha(shot.pixel(shot.width() - 1, middle)) < 200,
                 qPrintable(QStringLiteral("the right rim of a pin is halo, not a drawn line (alpha %1)")
                                .arg(qAlpha(shot.pixel(shot.width() - 1, middle)))));
        // The picture under the halo is still there, which is what makes the margin
        // read as a shadow rather than as an empty window.
        QCOMPARE(qAlpha(shot.pixel(shot.width() / 2, middle)), 255);
        // Taking the shadow off leaves a plain picture and nothing else: no line is
        // drawn in either state, because a pin with no shadow is meant to sit on a
        // desktop of its own colour without betraying where it ends.
        window.setShadowEnabled(false);
        const QImage flat = window.grab().toImage().convertedTo(QImage::Format_ARGB32);
        QCOMPARE(flat.size(), region.size());
        CaptureStyle off = style;
        off.shadow = false;
        const QImage plain = composeCapture(region, off).convertedTo(QImage::Format_ARGB32);
        QCOMPARE(flat.size(), plain.size());
        int extra = 0;
        for (int y = 0; y < flat.height() && extra == 0; ++y)
            for (int x = 0; x < flat.width(); ++x)
                if (flat.pixel(x, y) != plain.pixel(x, y))
                    ++extra;
        QVERIFY2(extra == 0, qPrintable(QStringLiteral(
                     "a pin with no shadow shows the picture and nothing drawn on top of it "
                     "(%1 pixel(s) differ)").arg(extra)));
        window.hide();
    }
    // Several pins can be on screen at once, so the shadow is what says which one the
    // user is on: the accent while it is the window being used, grey once the pointer
    // has gone elsewhere. A pin whose shadow is off shows neither — turning it off is
    // asking for a plain picture, not for a grey halo.
    void aPinSaysWhetherItIsTheOneBeingUsedByTheColourOfItsShadow() {
        const QImage region = stripedImage(60, 40);
        CaptureStyle style = bareStyle();
        style.shadow = true;
        style.shadowStrength = 100;
        PinWindow window(composeCapture(region, style));
        window.setUndecorated(region, style);
        window.setDecoration(style.shadowRadius());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const QColor themeAccent = accent();
        QVERIFY(window.isActive());
        // Read in the halo, well clear of the picture and of the window's rim.
        const QPoint probe(window.width() / 2, 2);
        const QImage lit = window.grab().toImage().convertedTo(QImage::Format_ARGB32);
        const QColor litPixel = QColor::fromRgb(lit.pixel(probe));
        QVERIFY2(litPixel.blue() > litPixel.red() + 8,
                 qPrintable(QStringLiteral("the shadow of the pin being used is the accent (%1)")
                                .arg(litPixel.name())));
        QVERIFY2(std::abs(litPixel.blue() - themeAccent.blue()) < 90,
                 qPrintable(QStringLiteral("the lit shadow is cast in the accent (%1 vs %2)")
                                .arg(litPixel.name(), themeAccent.name())));
        // Going to another window takes the accent off it and leaves a grey halo.
        window.setActive(false);
        const QImage idle = window.grab().toImage().convertedTo(QImage::Format_ARGB32);
        const QColor idlePixel = QColor::fromRgb(idle.pixel(probe));
        QVERIFY2(std::abs(idlePixel.blue() - idlePixel.red()) <= 12,
                 qPrintable(QStringLiteral("the shadow of a pin left behind is grey (%1)")
                                .arg(idlePixel.name())));
        QVERIFY2(qAlpha(idle.pixel(probe)) > 0, "the grey halo is still a shadow, not nothing");
        // Coming back puts the accent back.
        window.setActive(true);
        const QImage again = window.grab().toImage().convertedTo(QImage::Format_ARGB32);
        const QColor againPixel = QColor::fromRgb(again.pixel(probe));
        QVERIFY2(againPixel.blue() > againPixel.red() + 8,
                 qPrintable(QStringLiteral("coming back to a pin relights its shadow (%1)")
                                .arg(againPixel.name())));
        // With the shadow off there is no halo to colour either way.
        window.setShadowEnabled(false);
        window.setActive(false);
        const QImage flat = window.grab().toImage().convertedTo(QImage::Format_ARGB32);
        QVERIFY2(flat.size() == region.size(), "a shadowless pin is the picture and nothing else");
        window.hide();
    }
    void aPinOffersMoreThanCopying() {
        QImage picture(40, 30, QImage::Format_ARGB32);
        picture.fill(Qt::blue);
        PinWindow window(picture);
        const std::unique_ptr<QMenu> menu(window.createContextMenu());
        for (const QString &wanted : {QStringLiteral("旋转"), QStringLiteral("透明度"),
                                      QStringLiteral("阴影"), QStringLiteral("缩放")})
            QVERIFY2(actionCalled(menu.get(), wanted) != nullptr,
                     qPrintable(QStringLiteral("the menu is missing %1").arg(wanted)));
        // A picture that arrived without a style has no shadow to turn off, so the
        // entry is there but greyed out rather than quietly doing nothing.
        QVERIFY(!window.canToggleShadow());
        QVERIFY(!actionCalled(menu.get(), QStringLiteral("阴影"))->isEnabled());
        // 批注 leads: a pin is a place to keep a picture until it is marked up.
        QCOMPARE(menu->actions().first()->text(), QStringLiteral("批注"));
        window.hide();
    }
    // 贴图 was offered twice, once on the bar above the region and once in the column
    // of style tools beside it, so the same action had two homes and neither of them
    // looked like the way to do it.
    void pinningIsOfferedInOnePlaceOnly() {
        CaptureToolbar bar;
        CaptureSidebar sidebar;
        const auto count = [](QWidget &host) {
            int found = 0;
            for (auto *button : host.findChildren<QPushButton *>())
                if (button->property("glyphName").toString() == QLatin1String("pin"))
                    ++found;
            return found;
        };
        QCOMPARE(count(bar), 1);
        QCOMPARE(count(sidebar), 0);
    }
    // REG-073: annotating came out as one glyph among seven, so the one thing the
    // program is for looked like just another tool. It is the only button that is
    // boxed in the accent colour, and it is boxed wherever it appears.
    void annotatingIsTheOneActionDressedAsTheWayOnward() {
        CaptureToolbar bar;
        const auto buttons = bar.findChildren<QPushButton *>();
        QVERIFY(buttons.size() > 1);
        QPushButton *primary = nullptr;
        int marked = 0;
        for (auto *button : buttons) {
            if (!button->property("primary").toBool())
                continue;
            primary = button;
            ++marked;
        }
        QVERIFY2(primary != nullptr, "annotating has to be marked out from the other tools");
        QCOMPARE(marked, 1);
        QCOMPARE(primary->property("glyphName").toString(), QStringLiteral("edit"));
        QCOMPARE(primary->accessibleName(), QStringLiteral("批注"));
    }
    // REG-067: the four ways of writing a colour were not all reachable, so a copied
    // colour could only ever be one of two things. Every one of them has to name the
    // same colour, and cycling has to come back to where it started.
    void everyColourFormatNamesTheSameColour() {
        const QColor colour(79, 138, 182);
        QCOMPARE(captureColourText(colour, ColourFormat::Hex), QStringLiteral("#4f8ab6"));
        QCOMPARE(captureColourText(colour, ColourFormat::Rgb), QStringLiteral("RGB 79, 138, 182"));
        QVERIFY(captureColourText(colour, ColourFormat::Hsv).startsWith(QStringLiteral("HSV ")));
        QVERIFY(captureColourText(colour, ColourFormat::Hsl).startsWith(QStringLiteral("HSL ")));
        // A grey has no hue at all; that is written down as 0 rather than as a negative
        // number or a wrap-around.
        QVERIFY(captureColourText(QColor(128, 128, 128), ColourFormat::Hsv)
                    .startsWith(QStringLiteral("HSV 0,")));
        // An invalid colour has no text rather than a misleading one.
        QVERIFY(captureColourText(QColor(), ColourFormat::Hex).isEmpty());
        ColourFormat format = ColourFormat::Rgb;
        for (int step = 0; step < 4; ++step)
            format = nextColourFormat(format);
        QCOMPARE(format, ColourFormat::Rgb);
        QCOMPARE(captureColourFormatLabel(ColourFormat::Hsl), QStringLiteral("HSL"));
    }
    void historyKeepsTheNewestCapturesAndForgetsTheOldest() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        CaptureHistory history(directory.filePath(QStringLiteral("captures")));
        QVERIFY(history.at(0).isNull());
        QCOMPARE(history.count(), 0);
        // Empty pictures are refused rather than stored as a broken entry.
        QVERIFY(!history.add(QImage()));
        const int total = CaptureHistory::imageLimit() + 5;
        for (int index = 0; index < total; ++index) {
            QImage picture(4, 4, QImage::Format_ARGB32);
            picture.fill(QColor(index % 251, 0, 0, 255));
            QVERIFY(history.add(picture));
        }
        QCOMPARE(history.count(), CaptureHistory::imageLimit());
        // The newest entry is the last one stored, which only holds if every capture
        // got a name of its own even when they were taken in the same millisecond.
        QCOMPARE(history.at(0).pixelColor(0, 0).red(), (total - 1) % 251);
        QCOMPARE(history.at(1).pixelColor(0, 0).red(), (total - 2) % 251);
        history.clear();
        QCOMPARE(history.count(), 0);
        QVERIFY(history.at(0).isNull());
    }
    void historyRemembersSelectionsNewestFirstOnceEach() {
        QTemporaryDir directory;
        const QString captures = directory.filePath(QStringLiteral("captures"));
        CaptureHistory history(captures);
        QVERIFY(history.selections().isEmpty());
        // A selection of nothing is never worth remembering.
        history.rememberSelection(QRect());
        QVERIFY(history.selections().isEmpty());
        const QRect first(10, 20, 30, 40);
        const QRect second(50, 60, 70, 80);
        history.rememberSelection(first);
        history.rememberSelection(second);
        QCOMPARE(history.selections(), (QVector<QRect>{second, first}));
        // Picking the same rectangle again moves it to the front instead of adding a
        // duplicate, and so does picking it from a fresh run.
        history.rememberSelection(first);
        QCOMPARE(history.selections(), (QVector<QRect>{first, second}));
        CaptureHistory reopened(captures);
        QCOMPARE(reopened.selections(), (QVector<QRect>{first, second}));
        for (int index = 0; index < CaptureHistory::selectionLimit() + 6; ++index)
            reopened.rememberSelection(QRect(index, index, 8, 8));
        QCOMPARE(reopened.selections().size(), CaptureHistory::selectionLimit());
        QCOMPARE(reopened.selections().first(), QRect(CaptureHistory::selectionLimit() + 5,
                                                      CaptureHistory::selectionLimit() + 5, 8, 8));
        // A directory with no index reads as "nothing remembered" rather than as an
        // error, and so does one that was left half written.
        CaptureHistory fresh(directory.filePath(QStringLiteral("other")));
        QVERIFY(fresh.selections().isEmpty());
        QVERIFY(fresh.at(0).isNull());
        QVERIFY(QDir().mkpath(fresh.directory()));
        QFile damaged(QDir(fresh.directory()).filePath(QStringLiteral("selections.json")));
        QVERIFY(damaged.open(QIODevice::WriteOnly));
        damaged.write("[{\"x\":");
        damaged.close();
        QVERIFY(fresh.selections().isEmpty());
    }
};
QTEST_MAIN(CaptureTests)
#include "capture_test.moc"
