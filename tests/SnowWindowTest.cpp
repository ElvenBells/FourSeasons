#include "SnowWindow.h"

#include <QImage>
#include <QPainter>
#include <QTest>

namespace {
bool hasVisiblePixels(SnowWindow &window, const QRect &area)
{
    const QImage image = window.grab(area).toImage().convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(row[x]) > 0)
                return true;
        }
    }
    return false;
}

QRect floorStrip(const SnowWindow &window)
{
    return {0, window.height() - 4, window.width() / 2, 4};
}

void simulate(SeasonalWeather &weather, int frames)
{
    for (int frame = 0; frame < frames; ++frame)
        weather.advance(.05, {});
}

QPoint bigBubble(const QRegion &region, const QRect &scan)
{
    for (int y = scan.top(); y <= scan.bottom(); y += 3)
        for (int x = scan.left(); x <= scan.right(); x += 3)
            if ((QRegion(QRect(x - 11, y - 11, 23, 23)) - region).isEmpty())
                return {x, y};
    return {-1, -1};
}
} // namespace

class SnowWindowTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void visibleFullscreenKeepsSnowPaused();
    void maximizedWindowAbovePanelPausesWinter();
    void snowfallStartsAtTheTop();
    void panelPopupHasGarlandInFullscreen();
    void windowedMinimizeAndRestoreKeepFallingSnow();
    void preferencesApplyImmediately();
    void switchingSeasonsReplacesWinterEffects();
    void autumnOptionsAndFullscreenPause();
    void accumulatedLeavesCanBeMovedAndSwept();
    void bubblesPopOnClickAndAtWindowEdges();
    void rainMakesAPlaceForPaperBoat();
    void seasonalPrecipitationKeepsFalling_data();
    void seasonalPrecipitationKeepsFalling();
    void leavesAccumulateBehindOverlappingWindows_data();
    void leavesAccumulateBehindOverlappingWindows();
    void accumulatedPilesBurst_data();
    void accumulatedPilesBurst();
};

void SnowWindowTest::visibleFullscreenKeepsSnowPaused()
{
    SnowWindow window;
    window.show();
    const QRect screen = window.screenRect();
    const SnowSurface game{QStringLiteral("fullscreen"), screen, false, true};
    const SnowSurface dialog{QStringLiteral("dialog"), QRect(screen.center(), QSize(120, 80))};
    const int half = screen.width() / 2;
    const SnowSurface left{QStringLiteral("left"), QRect(screen.topLeft(), QSize(half, screen.height()))};
    const SnowSurface right{QStringLiteral("right"), screen.adjusted(half, 0, 0, 0)};
    const SnowSurface otherScreen{QStringLiteral("other"), screen.translated(screen.width(), 0), false, true};
    const QRect dialogGarland(dialog.rect.topLeft() - screen.topLeft(), QSize(dialog.rect.width(), 24));

    window.setSnowIntensity(0);
    window.setSurfaces({otherScreen, dialog});
    QTRY_VERIFY(hasVisiblePixels(window, dialogGarland));

    window.setActiveRect(dialog.rect, false);
    window.setSurfaces({left, game, dialog});
    QTRY_VERIFY(hasVisiblePixels(window, dialogGarland));
    QVERIFY(!hasVisiblePixels(window, QRect(0, 0, dialogGarland.left(), dialogGarland.top())));

    window.setSurfaces({dialog, game});
    QTRY_VERIFY(!hasVisiblePixels(window, dialogGarland));
    window.setSurfaces({game, dialog});
    QTRY_VERIFY(hasVisiblePixels(window, dialogGarland));

    window.setGarlandsEnabled(false);
    QTRY_VERIFY(!hasVisiblePixels(window, dialogGarland));
    window.setGarlandsEnabled(true);
    QTRY_VERIFY(hasVisiblePixels(window, dialogGarland));

    window.setSurfaces({game, left, right, dialog});
    QTRY_VERIFY(hasVisiblePixels(window, dialogGarland));
    window.setSurfaces({dialog});
    QTRY_VERIFY(hasVisiblePixels(window, dialogGarland));
}

void SnowWindowTest::panelPopupHasGarlandInFullscreen()
{
    SnowWindow window;
    window.setSnowIntensity(0);
    window.show();
    const QRect screen = window.screenRect();
    const SnowSurface game{QStringLiteral("game"), screen, false, true};
    const SnowSurface popup{QStringLiteral("launcher"), QRect(screen.topLeft() + QPoint(10, 140),
                           QSize(220, 240)), false, false, false, true};
    const QRect strip(10, 140, 220, 24);
    window.setActiveRect(game.rect, true);
    window.setSurfaces({game, popup});
    QTRY_VERIFY(hasVisiblePixels(window, strip));
    QVERIFY(!hasVisiblePixels(window, window.rect().adjusted(0, 180, 0, 0)));
    window.setSurfaces({game});
    QTRY_VERIFY(!hasVisiblePixels(window, strip));
}

void SnowWindowTest::snowfallStartsAtTheTop()
{
    SnowWindow window;
    window.show();
    const QRect screen = window.screenRect();
    const QRect top(0, 0, window.width(), 60);
    const QRect middle(0, window.height() / 3, window.width(), window.height() / 3);
    QVERIFY(!hasVisiblePixels(window, middle));
    QVERIFY(!hasVisiblePixels(window, floorStrip(window)));
    QTRY_VERIFY_WITH_TIMEOUT(hasVisiblePixels(window, top), 2000);
    QVERIFY(!hasVisiblePixels(window, middle));

    window.setActiveRect(screen, true);
    QTRY_VERIFY(!hasVisiblePixels(window, window.rect()));
    window.setActiveRect({}, false);
    QTRY_VERIFY_WITH_TIMEOUT(hasVisiblePixels(window, top), 2000);
    QVERIFY(!hasVisiblePixels(window, middle));
    QVERIFY(!hasVisiblePixels(window, floorStrip(window)));
}

void SnowWindowTest::maximizedWindowAbovePanelPausesWinter()
{
    SnowWindow window;
    window.show();
    window.setSnowIntensity(0);
    const QRect screen = window.screenRect();
    const SnowSurface panel{QStringLiteral("panel"), screen.adjusted(0, screen.height() - 40, 0, 0), true};
    const SnowSurface maximized{QStringLiteral("terminal"), screen.adjusted(0, 0, 0, -40)};
    window.setSurfaces({maximized, panel});
    window.setActiveRect(maximized.rect, false);
    QTRY_VERIFY(!hasVisiblePixels(window, window.rect()));
    const SnowSurface restored{maximized.id, maximized.rect.adjusted(60, 44, -80, -52)};
    window.setSurfaces({restored, panel});
    window.setActiveRect(restored.rect, false);
    const QRect garland(restored.rect.x() - screen.x(), restored.rect.y() - screen.y(), restored.rect.width(), 20);
    QTRY_VERIFY(hasVisiblePixels(window, garland));
}

void SnowWindowTest::windowedMinimizeAndRestoreKeepFallingSnow()
{
    SnowWindow window;
    window.resize(280, 160);
    window.show();
    const QRect screen = window.screenRect();
    const SnowSurface app{QStringLiteral("app"), QRect(screen.topLeft() + QPoint(160, 115), QSize(100, 50))};
    const QRect middle(0, 55, 140, 70);
    const QRect garland(162, 117, 96, 16);
    window.setSnowIntensity(500);
    window.setActiveRect(app.rect, false);
    window.setSurfaces({app});
    QTRY_VERIFY_WITH_TIMEOUT(hasVisiblePixels(window, middle), 4000);
    QTRY_VERIFY(hasVisiblePixels(window, garland));

    window.setSurfaces({});
    window.setActiveRect({}, false);
    // Дожидаемся обновления раскладки, пока пролетавшие снежинки ещё находятся в средней части.
    QTest::qWait(60);
    QVERIFY(hasVisiblePixels(window, middle));

    window.setSurfaces({app});
    window.setActiveRect(app.rect, false);
    QTest::qWait(60);
    QVERIFY(hasVisiblePixels(window, middle));
}

void SnowWindowTest::preferencesApplyImmediately()
{
    SnowWindow window;
    window.show();
    const QRect top(0, 0, window.width(), 60);
    const SnowSurface app{QStringLiteral("app"),
                          QRect(window.screenRect().topLeft() + QPoint(25, 130), QSize(240, 180))};
    const QRect garland(25, 130, 240, 24);
    window.setSurfaces({app});
    QTRY_VERIFY(hasVisiblePixels(window, garland));
    QTRY_VERIFY_WITH_TIMEOUT(hasVisiblePixels(window, top), 2000);

    window.setSnowIntensity(0);
    QVERIFY(!hasVisiblePixels(window, top));
    QVERIFY(hasVisiblePixels(window, garland));
    window.setGarlandsEnabled(false);
    QVERIFY(!hasVisiblePixels(window, garland));

    window.setGarlandsEnabled(true);
    QVERIFY(hasVisiblePixels(window, garland));
    window.setSnowIntensity(44);
    QTRY_VERIFY_WITH_TIMEOUT(hasVisiblePixels(window, top), 2000);
}

void SnowWindowTest::switchingSeasonsReplacesWinterEffects()
{
    SnowWindow window;
    window.show();
    window.setSnowIntensity(0);
    const SnowSurface app{QStringLiteral("app"),
                          QRect(window.screenRect().topLeft() + QPoint(25, 135), QSize(240, 180))};
    const QRect garland(25, 135, 240, 24);
    const QRect top(0, 0, window.width(), 50);
    const QRect bottom(0, window.height() - 65, window.width(), 65);
    window.setSurfaces({app});
    QTRY_VERIFY(hasVisiblePixels(window, garland));

    window.setSeason(Season::Spring);
    QVERIFY(!hasVisiblePixels(window, garland));
    QTRY_VERIFY_WITH_TIMEOUT(hasVisiblePixels(window, top), 3000);
    QVERIFY(hasVisiblePixels(window, bottom));  // весенний сад остаётся у земли

    window.setSeason(Season::Summer);
    // Дневные лучи остаются справа, лепестки при переключении исчезают.
    QVERIFY(!hasVisiblePixels(window, QRect(0, 0, window.width() / 4, 40)));
    QTRY_VERIFY_WITH_TIMEOUT(hasVisiblePixels(window, bottom), 4000);

    window.setSeason(Season::Winter);
    QVERIFY(hasVisiblePixels(window, garland));
    QVERIFY(!hasVisiblePixels(window, top));
}

void SnowWindowTest::autumnOptionsAndFullscreenPause()
{
    SnowWindow window;
    window.show();
    const QRect screen = window.screenRect();
    const QRect top(0, 0, window.width(), 60);
    const QRect middle(0, window.height() / 3, window.width(), window.height() / 3);
    window.setAutumnOptions({true, false});
    window.setSeason(Season::Autumn);
    QTRY_VERIFY_WITH_TIMEOUT(hasVisiblePixels(window, middle), 3000);

    window.setAutumnOptions({false, true});
    QVERIFY(!hasVisiblePixels(window, middle));
    QTRY_VERIFY_WITH_TIMEOUT(hasVisiblePixels(window, top), 3000);
    window.setAutumnOptions({false, false});
    QVERIFY(window.autumnOptions().leaves);

    window.setActiveRect(screen, true);
    QTRY_VERIFY(!hasVisiblePixels(window, window.rect()));
    window.setSeason(Season::Spring);
    QTest::qWait(240);
    QVERIFY(!hasVisiblePixels(window, window.rect()));
    window.setActiveRect({}, false);
    QTRY_VERIFY_WITH_TIMEOUT(hasVisiblePixels(window, top), 3000);
    QVERIFY(!hasVisiblePixels(window, middle));
}

void SnowWindowTest::accumulatedLeavesCanBeMovedAndSwept()
{
    SeasonalWeather weather;
    weather.setSize({380, 190});
    weather.setGround({0, 162, 380, 28});
    weather.setSeason(Season::Autumn);
    for (int frame = 0; frame < 250 && weather.inputRegion().isEmpty(); ++frame)
        weather.advance(.05, {});
    const QRegion leaves = weather.inputRegion();
    QVERIFY(!leaves.isEmpty());
    QVERIFY((leaves & QRect(0, 162, 380, 28)).isEmpty());

    const QPoint point = leaves.boundingRect().center();
    weather.movePointer(point - QPoint(24, 0), false);
    weather.movePointer(point + QPoint(24, 0), false);
    QVERIFY(!(leaves - weather.inputRegion()).isEmpty());
    simulate(weather, 250);
    weather.setTool(SeasonTool::Broom);
    for (int x = 8; x < 380; x += 25)
        QVERIFY(weather.press(QPointF(x, 154)));
    weather.setTool(SeasonTool::Passive);
    QVERIFY(weather.inputRegion().isEmpty());

    weather.setSeason(Season::Spring);
    simulate(weather, 300);
    QVERIFY(!weather.inputRegion().isEmpty());
}

void SnowWindowTest::bubblesPopOnClickAndAtWindowEdges()
{
    SeasonalWeather weather;
    weather.setSize({410, 230});
    weather.setSeason(Season::Summer);
    weather.setBubbles(true);
    QPoint bubble(-1, -1);
    for (int frame = 0; frame < 180 && bubble.x() < 0; ++frame) {
        weather.advance(.05, {});
        bubble = bigBubble(weather.inputRegion(), QRect(20, 117, 370, 80));
    }
    QVERIFY(bubble.x() >= 0);
    const QRegion before = weather.inputRegion();
    QVERIFY(weather.press(bubble));
    QVERIFY(!(before - weather.inputRegion()).isEmpty());

    weather.setBubbles(false);
    weather.setWindows({{QStringLiteral("window"), QRect(-100, 62, 610, 50)}});
    weather.setBubbles(true);
    simulate(weather, 260);
    QCOMPARE(bigBubble(weather.inputRegion(), QRect(0, 0, 410, 95)), QPoint(-1, -1));
}

void SnowWindowTest::rainMakesAPlaceForPaperBoat()
{
    SeasonalWeather weather;
    weather.setSize({400, 200});
    weather.setSeason(Season::Autumn);
    QVERIFY(!weather.launchBoat());
    weather.setOptions({true, true});
    simulate(weather, 110);
    QVERIFY(weather.launchBoat());
    weather.setOptions({false, true});
    simulate(weather, 1300);
    QVERIFY(!weather.launchBoat());
}

void SnowWindowTest::seasonalPrecipitationKeepsFalling_data()
{
    QTest::addColumn<bool>("spring");
    QTest::addColumn<bool>("rain");
    QTest::addColumn<bool>("leaves");
    QTest::newRow("leaves") << false << false << true;
    QTest::newRow("petals") << true << false << false;
    QTest::newRow("rain") << false << true << false;
    QTest::newRow("rain and leaves") << false << true << true;
}

void SnowWindowTest::seasonalPrecipitationKeepsFalling()
{
    QFETCH(bool, spring);
    QFETCH(bool, rain);
    QFETCH(bool, leaves);
    const QSize size(480, 300);
    const QRect band(0, 12, size.width(), 42);
    SeasonalWeather weather;
    weather.setSize(size);
    weather.setOptions({rain, leaves});
    weather.setSeason(spring ? Season::Spring : Season::Autumn);
    weather.setPrecipitation({500, 190, 65});

    const auto containsPrecipitation = [&] {
        QImage image(band.size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.translate(-band.topLeft());
        weather.paint(painter, QRegion(band));
        painter.end();
        for (int row = 0; row < image.height(); ++row) {
            const auto *pixels = reinterpret_cast<const QRgb *>(image.constScanLine(row));
            for (int x = 0; x < image.width(); ++x)
                if (qAlpha(pixels[x]) > 24)
                    return true;
        }
        return false;
    };

    QVERIFY(!containsPrecipitation());
    for (int frame = 0; frame < 425; ++frame) {
        weather.advance(.016, {});
        if (frame > 42 && frame % 13 == 0)
            QVERIFY(containsPrecipitation());
    }
    weather.setPrecipitation({0, 190, 65});
    weather.advance(.016, {});
    QVERIFY(!containsPrecipitation());
}

void SnowWindowTest::leavesAccumulateBehindOverlappingWindows_data()
{
    QTest::addColumn<bool>("spring");
    QTest::newRow("leaves") << false;
    QTest::newRow("petals") << true;
}

void SnowWindowTest::leavesAccumulateBehindOverlappingWindows()
{
    QFETCH(bool, spring);
    SeasonalWeather weather;
    weather.setSeason(spring ? Season::Spring : Season::Autumn);
    weather.setSize(QSize(480, 320));
    weather.setPrecipitation({500, 250, 0});
    const SnowSurface lower{QStringLiteral("lower"), QRect(64, 145, 340, 132)};
    const SnowSurface upper{QStringLiteral("upper"), QRect(120, 64, 265, 118)};
    const QRect sample(168, 117, 130, 29);
    weather.setWindows({lower, upper});
    simulate(weather, 170);

    const auto paintLedge = [&] {
        QImage image(sample.size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.translate(-sample.topLeft());
        weather.paintWindowLeaves(painter, QRegion(sample));
        painter.end();
        return image;
    };
    const QImage hidden = paintLedge();
    weather.setWindows({lower});
    const QImage exposed = paintLedge();
    QVERIFY(exposed != hidden);
    weather.movePointer({152, 137}, false);
    weather.movePointer({318, 137}, false);
    QVERIFY(paintLedge() != exposed);
}

void SnowWindowTest::accumulatedPilesBurst_data()
{
    QTest::addColumn<bool>("spring");
    QTest::newRow("leaves") << false;
    QTest::newRow("petals") << true;
}

void SnowWindowTest::accumulatedPilesBurst()
{
    QFETCH(bool, spring);
    SeasonalWeather weather;
    const QSize size(480, 320);
    weather.setSize(size);
    weather.setSeason(spring ? Season::Spring : Season::Autumn);
    weather.setPrecipitation({500, 400, 0});
    simulate(weather, 170);
    weather.setPrecipitation({0, 400, 0});

    QPoint hit;
    for (int x = 18; x < size.width() - 18; x += 4) {
        const QPoint point(x, size.height() - 18);
        if (weather.inputRegion().contains(point) && weather.breakPile(point)) {
            hit = point;
            break;
        }
    }
    QVERIFY(!hit.isNull());
    QVERIFY(!weather.breakPile(hit));

    simulate(weather, 5);
    const QRect cloud(hit.x() - 50, size.height() - 75, 100, 35);
    QImage image(cloud.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.translate(-cloud.topLeft());
    weather.paintLeafOverlays(painter, QRegion(cloud));
    painter.end();
    QImage blank(image.size(), image.format());
    blank.fill(Qt::transparent);
    QVERIFY(image != blank);
}

QTEST_MAIN(SnowWindowTest)
#include "SnowWindowTest.moc"
