#include "capturesession.h"
#include "capturetoolbar.h"
#include "model.h"
#include "pinwindow.h"
#include "scrollcapture.h"
#include "scrollstitch.h"
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
    void overlapIsFoundBetweenConsecutiveFrames() {
        const QImage full = stripedImage(64, 200);
        const QImage above = full.copy(0, 0, 64, 120);
        const QImage below = full.copy(0, 100, 64, 100);
        const auto match = matchVerticalOverlap(above, below, 4, 120);
        QVERIFY2(match.found, "two frames of the same page were not recognised as overlapping");
        QCOMPARE(match.overlap, 20);
        QVERIFY2(match.score < 1.0, "identical rows should score as an exact match");
        // The search is bounded by what the caller allows.
        QCOMPARE(matchVerticalOverlap(above, below, 4, 10).found, false);
        QCOMPARE(matchVerticalOverlap(above, below, 30, 120).found, false);
        // Pictures of different widths cannot share rows at all.
        QCOMPARE(matchVerticalOverlap(above, full.copy(0, 100, 48, 100), 4, 100).found, false);
        QCOMPARE(matchVerticalOverlap(QImage(), below, 4, 100).found, false);
    }
    void stitchingRebuildsTheOriginalPicture() {
        const QImage full = stripedImage(64, 200);
        QImage picture = full.copy(0, 0, 64, 120);
        // The second frame starts twenty rows above the end of the first, so twenty
        // rows are shared and everything below them is new: the return value counts
        // the rows that were actually added.
        QCOMPARE(appendScrolledFrame(&picture, full.copy(0, 100, 64, 100)), 80);
        QCOMPARE(picture.height(), 200);
        QCOMPARE(picture.convertToFormat(QImage::Format_ARGB32), full);
        // The same frame again adds nothing, which is how the end of a page is seen.
        QCOMPARE(appendScrolledFrame(&picture, full.copy(0, 100, 64, 100)), -1);
        QCOMPARE(picture.height(), 200);
        // Frames that share nothing are refused instead of being glued on with a
        // visible seam.
        QCOMPARE(appendScrolledFrame(&picture, noisyImage(64, 100, 7)), -1);
        QCOMPARE(picture.height(), 200);
        QCOMPARE(appendScrolledFrame(&picture, noisyImage(48, 100, 7)), -1);
        QCOMPARE(appendScrolledFrame(nullptr, full.copy(0, 0, 64, 100)), -1);
        QImage empty;
        QCOMPARE(appendScrolledFrame(&empty, full.copy(0, 0, 64, 100)), -1);
        // A frame shorter than the smallest useful overlap cannot be placed.
        QCOMPARE(appendScrolledFrame(&picture, full.copy(0, 100, 64, 4)), -1);
    }
    void aPageThatStopsScrollingKeepsTheLastPicture() {
        const QImage full = stripedImage(64, 120);
        QImage picture = full;
        // The window reached the bottom and keeps showing the same rows.
        for (int attempt = 0; attempt < 3; ++attempt)
            QCOMPARE(appendScrolledFrame(&picture, full), -1);
        QCOMPARE(picture, full);
    }
    // A region of a page as the screen shows it: a fixed header, a fixed footer, and
    // the rows of `content` starting at `top` in between.
    static QImage bandedFrame(int width, int height, int header, int footer, const QImage &content,
                              int top, int jitter = 0) {
        QImage frame(width, height, QImage::Format_ARGB32);
        quint32 state = 9u;
        for (int row = 0; row < height; ++row) {
            QRgb color = qRgb(0, 0, 0);
            if (row < header)
                color = qRgb(200, 30, 40);
            else if (row >= height - footer)
                color = qRgb(20, 60, 200);
            else
                color = content.pixel(0, top + (row - header));
            if (jitter != 0 && row >= header && row < height - footer) {
                state = state * 1664525u + 1013904223u;
                const int shift = int((state >> 16) % quint32(jitter * 2 + 1)) - jitter;
                color = qRgb(std::clamp(qRed(color) + shift, 0, 255),
                             std::clamp(qGreen(color) + shift, 0, 255),
                             std::clamp(qBlue(color) + shift, 0, 255));
            }
            for (int column = 0; column < width; ++column)
                frame.setPixel(column, row, color);
        }
        return frame;
    }
    void aFixedHeaderAndFooterDoNotStopTheStitch() {
        const QImage content = stripedImage(64, 300);
        constexpr int kHeader = 12;
        constexpr int kFooter = 8;
        constexpr int kHeight = 100;
        constexpr int kStep = 20;
        ScrollStitcher stitcher;
        stitcher.reset(bandedFrame(64, kHeight, kHeader, kFooter, content, 0));
        // Without the bands a frame that starts with a header can never be lined up
        // with the rows the picture ends on, which is what made a long capture of any
        // page with a toolbar come back as one frame.
        QImage naive = bandedFrame(64, kHeight, kHeader, kFooter, content, 0);
        QCOMPARE(appendScrolledFrame(&naive, bandedFrame(64, kHeight, kHeader, kFooter, content, kStep)),
                 -1);
        int frames = 0;
        for (int top = kStep; top + kHeight - kHeader - kFooter <= content.height(); top += kStep) {
            QVERIFY2(stitcher.add(bandedFrame(64, kHeight, kHeader, kFooter, content, top)) > 0,
                     "a frame of a page with a fixed header and footer was not placed");
            ++frames;
        }
        QCOMPARE(frames, 11);
        const QImage picture = stitcher.picture();
        // The header and the footer are part of the page once each, and everything
        // between them is the whole of the content.
        QCOMPARE(picture.height(), kHeader + content.height() + kFooter);
        QCOMPARE(picture.pixel(4, 0), qRgb(200, 30, 40));
        QCOMPARE(picture.pixel(4, kHeader), content.pixel(0, 0));
        QCOMPARE(picture.pixel(4, kHeader + content.height() - 1), content.pixel(0, content.height() - 1));
        QCOMPARE(picture.pixel(4, picture.height() - 1), qRgb(20, 60, 200));
        QVERIFY2(!stitcher.partial(), "an exact page should not need a rough match");
    }
    void aRejectedFrameDoesNotChangeThePictureOrBands() {
        const QImage content = noisyImage(64, 300, 71);
        const QImage first = bandedFrame(64, 100, 12, 8, content, 0);
        ScrollStitcher stitcher;
        stitcher.reset(first);
        QImage unrelated = noisyImage(64, 100, 97);
        for (int y = 92; y < 100; ++y)
            for (int x = 0; x < 64; ++x)
                unrelated.setPixel(x, y, qRgb(21, 61, 201));
        QCOMPARE(stitcher.add(unrelated), -1);
        QCOMPARE(stitcher.picture(), first);
        QCOMPARE(stitcher.lastFrame(), first);
        QCOMPARE(stitcher.bands().top, 0);
        QCOMPARE(stitcher.bands().bottom, 0);
        QCOMPARE(stitcher.add(bandedFrame(64, 100, 12, 8, content, 20)), 20);
        QCOMPARE(stitcher.height(), 120);
    }
    void horizontalCaptureRebuildsPixelsInTheirOriginalOrientation() {
        const QImage content = noisyImage(220, 64, 13);
        ScrollCapture session;
        session.begin(content.copy(0, 0, 100, 64), 1, Qt::Horizontal);
        for (int x = 20; x <= 120; x += 20)
            QCOMPARE(session.take(content.copy(x, 0, 100, 64)), ScrollCapture::Outcome::Added);
        QCOMPARE(session.picture(), content);
        QCOMPARE(session.take(content.copy(120, 0, 100, 64)), ScrollCapture::Outcome::Repeat);
    }
    void aPausedCaptureContinuesWithoutLosingItsFrames() {
        const QImage content = noisyImage(64, 140, 17);
        ScrollCapture session;
        session.begin(content.copy(0, 0, 64, 100));
        QCOMPARE(session.take(content.copy(0, 20, 64, 100)), ScrollCapture::Outcome::Added);
        session.pause();
        QVERIFY(!session.running());
        QCOMPARE(session.frames(), 1);
        session.resume();
        QCOMPARE(session.take(content.copy(0, 40, 64, 100)), ScrollCapture::Outcome::Added);
        QCOMPARE(session.frames(), 2);
        QCOMPARE(session.picture(), content);
    }
    void theBandsOfAFrameSayWhatDidNotMove() {
        const QImage content = stripedImage(64, 300);
        const QImage previous = bandedFrame(64, 100, 12, 8, content, 0);
        const QImage next = bandedFrame(64, 100, 12, 8, content, 20);
        const ScrollBands bands = fixedBands(previous, next);
        QCOMPARE(bands.top, 12);
        QCOMPARE(bands.bottom, 8);
        // A page that did not move at all reports its whole height as fixed, which is
        // how a capture knows to stop.
        const ScrollBands still = fixedBands(previous, previous);
        QCOMPARE(still.top + still.bottom, previous.height());
        // Pictures that cannot be compared have no bands.
        QCOMPARE(fixedBands(previous, noisyImage(48, 100, 3)).top, 0);
        QCOMPARE(fixedBands(QImage(), next).bottom, 0);
    }
    void aFrameWithSomethingMovingInItIsStillPlaced() {
        const QImage content = stripedImage(64, 300);
        const QImage first = content.copy(0, 0, 64, 100);
        ScrollStitcher stitcher;
        stitcher.reset(first);
        // Fifty rows of the same page, only noisier: a video, or a page that redrew
        // itself slightly differently. An exact match refuses it, a rough one places
        // it, and the run says afterwards that it was rough.
        const QImage next = bandedFrame(64, 100, 0, 0, content, 20, 20);
        QCOMPARE(matchVerticalOverlap(first, next, 8, 100).found, false);
        QVERIFY2(stitcher.add(next) > 0, "a frame that only roughly matched was thrown away");
        QVERIFY2(stitcher.partial(), "a rough placement has to be reported");
        QCOMPARE(stitcher.picture().height(), 120);
    }
    void aStitcherThatPlacedNothingStillHasItsFirstFrame() {
        const QImage content = stripedImage(64, 300);
        const QImage first = bandedFrame(64, 100, 12, 8, content, 0);
        ScrollStitcher stitcher;
        stitcher.reset(first);
        // Nothing has been added yet, so the picture is the frame as it was selected,
        // fixed bands and all.
        QCOMPARE(stitcher.picture().height(), first.height());
        // The same frame again has nothing new in it.
        QCOMPARE(stitcher.add(first), -1);
        QCOMPARE(stitcher.picture().height(), first.height());
        QCOMPARE(stitcher.height(), first.height());
    }
    void twoLooksAtTheSamePlaceSayWhetherThePageMoved() {
        const QImage content = stripedImage(64, 300);
        const QImage first = content.copy(0, 0, 64, 100);
        QCOMPARE(frameDifference(first, first), 0.0);
        QVERIFY2(frameDifference(first, content.copy(0, 20, 64, 100)) > 1.0,
                 "a page that moved has to look different");
        // Two pictures that cannot be compared are treated as completely different,
        // so a frame is never accepted on the strength of an empty comparison.
        QVERIFY(frameDifference(QImage(), first) > 1.0e8);
        QVERIFY(frameDifference(first, content.copy(0, 0, 32, 100)) > 1.0e8);
    }
    void unrelatedFramesAreNotStitched() {
        const QImage first = noisyImage(64, 120, 11);
        const QImage second = noisyImage(64, 100, 22);
        QVERIFY(!matchVerticalOverlap(first, second, 4, 100).found);
    }
    // A long capture is now driven a frame at a time by whoever is reading the screen,
    // rather than running a timer of its own to its limit. What the session has to get
    // right is telling the three cases apart: a frame that adds rows, a frame that is
    // the same page again, and a frame that cannot be placed at all.
    void aFrameAtATimeGrowsTheLongPicture() {
        const QImage content = stripedImage(64, 300);
        ScrollCapture session;
        session.begin(content.copy(0, 0, 64, 100));
        QVERIFY(session.running());
        QCOMPARE(session.height(), 100);
        QCOMPARE(session.frames(), 0);
        // The page moved twenty rows, so the frame repeats all but twenty of the rows the
        // picture already ends on and twenty more are added.
        QCOMPARE(session.take(content.copy(0, 20, 64, 100)), ScrollCapture::Outcome::Added);
        QCOMPARE(session.height(), 120);
        QCOMPARE(session.frames(), 1);
        QVERIFY(session.picture().convertToFormat(QImage::Format_ARGB32) ==
                content.copy(0, 0, 64, 120).convertToFormat(QImage::Format_ARGB32));
        // The same frame again means the page has run out, which is not a failure.
        QCOMPARE(session.take(content.copy(0, 20, 64, 100)), ScrollCapture::Outcome::Repeat);
        QVERIFY(session.running());
        QCOMPARE(session.height(), 120);
    }
    void aFrameThatCannotBePlacedIsToldApartFromTheEndOfThePage() {
        const QImage content = stripedImage(64, 300);
        ScrollCapture session;
        session.begin(content.copy(0, 0, 64, 100));
        // A page that jumped somewhere else shares nothing with the picture, and a seam
        // placed on a guess would be worse than stopping.
        QCOMPARE(session.take(noisyImage(64, 100, 31)), ScrollCapture::Outcome::Failed);
        QVERIFY2(!session.partial(), "a frame that was refused is not a rough placement");
        QCOMPARE(session.height(), 100);
    }
    // REG-102: a run over something that does not scroll has to be told apart from a run
    // that made a long picture. The picture is never missing — it starts as the frame the
    // region was taken from — so "the picture is null" cannot be the test; the number of
    // frames that were actually placed is.
    void aRunWhereNothingWasPlacedPlacedNoFrames() {
        const QImage content = stripedImage(64, 300);
        ScrollCapture session;
        session.begin(content.copy(0, 0, 64, 100));
        QVERIFY2(!session.picture().isNull(), "the first frame is the picture before anything is added");
        QCOMPARE(session.frames(), 0);
        // The page did not move, twice: the run is over, and no frame was ever placed.
        QCOMPARE(session.take(content.copy(0, 0, 64, 100)), ScrollCapture::Outcome::Repeat);
        QCOMPARE(session.take(content.copy(0, 0, 64, 100)), ScrollCapture::Outcome::Repeat);
        QCOMPARE(session.frames(), 0);
        QCOMPARE(session.height(), 100);
        // The count is what says nothing was stitched, and it survives until the session
        // is stopped — the caller reads it before stopping exactly for that reason.
        session.stop();
        QCOMPARE(session.frames(), 0);
    }
    void aLongCaptureStopsAtItsLimit() {
        const QImage content = stripedImage(64, 300);
        ScrollCapture session;
        session.begin(content.copy(0, 0, 64, 100));
        int added = 0;
        for (int top = 20; top + 100 <= content.height(); top += 20) {
            if (session.take(content.copy(0, top, 64, 100)) != ScrollCapture::Outcome::Added)
                break;
            ++added;
        }
        QCOMPARE(added, 10);
        QCOMPARE(session.height(), 300);
        // The page has run out: a frame that shares too little with the picture to be
        // placed is refused rather than glued on with a visible seam.
        QCOMPARE(session.take(noisyImage(64, 100, 41)), ScrollCapture::Outcome::Failed);
        QCOMPARE(session.height(), 300);
    }
    // A page that never ends is what the frame limit is for: the height limit alone would
    // let a run grow for as long as the target answered.
    void aLongCaptureHasAFrameLimit() {
        QVERIFY(scrollFrameLimit() > 1);
        QVERIFY(scrollFrameLimit() <= 200);
        QCOMPARE(ScrollStitcher::maxHeight(), 20000);
        // A page that keeps producing rows, for far longer than a run is allowed to go.
        const QImage content = stripedImage(64, 40 * scrollFrameLimit());
        ScrollCapture session;
        session.begin(content.copy(0, 0, 64, 100));
        int taken = 0;
        for (int top = 20; top + 100 <= content.height() && !session.atLimit(); top += 20) {
            if (session.take(content.copy(0, top, 64, 100)) == ScrollCapture::Outcome::Added)
                ++taken;
            else
                break;
        }
        QCOMPARE(taken, scrollFrameLimit());
        QVERIFY2(session.atLimit(), "a run that took every frame it was allowed is not at its limit");
        // At the limit nothing more is added, and the session says so rather than growing
        // past what it promised.
        const int height = session.height();
        QCOMPARE(session.take(content.copy(0, 20 * scrollFrameLimit(), 64, 100)),
                 ScrollCapture::Outcome::Repeat);
        QCOMPARE(session.height(), height);
        QVERIFY(!session.notice().isEmpty());
    }
    void aStoppedLongCaptureTakesNothingMore() {
        const QImage content = stripedImage(64, 300);
        ScrollCapture session;
        session.begin(content.copy(0, 0, 64, 100));
        QCOMPARE(session.take(content.copy(0, 20, 64, 100)), ScrollCapture::Outcome::Added);
        session.stop();
        QVERIFY(!session.running());
        QCOMPARE(session.take(content.copy(0, 40, 64, 100)), ScrollCapture::Outcome::Failed);
        QCOMPARE(session.height(), 120);
    }
    // The panel a long capture grows in sits on the side the picture grows towards, so
    // the part of the screen the user is reading is not covered by it.
    void theLongPictureGrowsWhereItIsSeen() {
        const QSizeF window(560, 360);
        const QRectF tall(100, 20, 200, 130);
        const QRectF down = scrollPreviewPlacement(tall, window, Qt::Vertical);
        QVERIFY2(!down.isEmpty(), "there is room beside the region for the panel");
        QCOMPARE(down.size(), QSizeF(scrollPreviewWidth, scrollPreviewHeight));
        QVERIFY2(down.left() >= tall.right(), "a picture growing downwards is shown beside the region");
        QVERIFY(down.right() <= window.width());
        const QRectF shallow(100, 8, 260, 60);
        const QRectF sideways = scrollPreviewPlacement(shallow, window, Qt::Horizontal);
        QVERIFY2(!sideways.isEmpty(), "there is room under the region for the panel");
        QVERIFY2(sideways.top() >= shallow.bottom(), "a picture growing sideways is shown under the region");
        QVERIFY(sideways.bottom() <= window.height());
    }
    void theLongPicturePanelIsAlwaysOnTheScreen() {
        const QSizeF window(560, 360);
        // A region against the right edge leaves no room on the side a downwards picture
        // would want, and one against the bottom edge none for a sideways one: the panel
        // flips to the other side rather than hanging off the screen.
        const QRectF right(400, 40, 140, 200);
        const QRectF flipped = scrollPreviewPlacement(right, window, Qt::Vertical);
        QVERIFY2(!flipped.isEmpty(), "the panel flipped to the other side rather than giving up");
        QVERIFY(flipped.left() >= 0);
        QVERIFY(flipped.right() <= window.width());
        const QRectF bottom(60, 240, 200, 100);
        const QRectF above = scrollPreviewPlacement(bottom, window, Qt::Horizontal);
        QVERIFY2(!above.isEmpty(), "the panel flipped above the region");
        QVERIFY(above.top() >= 0);
        QVERIFY(above.bottom() <= window.height());
    }
    // REG-094 in reverse: a panel that cannot be put anywhere clear of the tools is not
    // put nowhere-at-all-but-on-top-of-them, it is not drawn. A half-hidden readout is
    // worse than one the user has to move the region to see.
    void theLongPicturePanelWouldRatherVanishThanHideBehindATool() {
        const QRectF region(100, 60, 200, 240);
        const QSizeF window(560, 360);
        // Tools that take the whole window leave nowhere clear for the panel.
        const QVector<QRectF> everywhere{QRectF(0, 0, 560, 360)};
        QVERIFY2(scrollPreviewPlacement(region, window, Qt::Vertical, everywhere).isEmpty(),
                 "a panel with nowhere clear to go has to be dropped, not drawn over a tool");
        // One tool on the far side is stepped around rather than swallowed.
        const QVector<QRectF> oneTool{QRectF(0, 0, 560, 40)};
        const QRectF placed = scrollPreviewPlacement(region, window, Qt::Horizontal, oneTool);
        QVERIFY2(!placed.isEmpty(), "a single tool is stepped around, not a reason to give up");
        QVERIFY2(!placed.intersects(oneTool.first()), "the panel has to keep off the tool");
    }
    void aLongPictureSaysItsLengthTheWayAPersonReadsIt() {
        QCOMPARE(scrollLengthText(0), QStringLiteral("0 px"));
        QCOMPARE(scrollLengthText(320), QStringLiteral("320 px"));
        QCOMPARE(scrollLengthText(9999), QStringLiteral("9999 px"));
        // Past four digits the exact number stops being worth reading, so it is given in
        // tens of thousands with one decimal.
        QCOMPARE(scrollLengthText(10000), QStringLiteral("1.0 万像素"));
        QCOMPARE(scrollLengthText(12345), QStringLiteral("1.2 万像素"));
        QCOMPARE(scrollLengthText(20000), QStringLiteral("2.0 万像素"));
    }
};
QTEST_MAIN(CaptureTests)
#include "capture_test.moc"
