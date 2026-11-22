#include "SnowScene.h"
#include "SnowProps.h"

#include <QImage>
#include <QPainter>
#include <QTest>

namespace {
const QSize screen(840, 540);
const QRect dock(70, 470, 700, 50);

void tickFor(SnowScene &scene, double seconds)
{
    while (seconds > 0) {
        const double dt = qMin(.01, seconds);
        scene.advance(dt);
        seconds -= dt;
    }
}

void fillDock(SnowScene &scene, const QRect &surface = dock)
{
    scene.setLayout(screen, surface, {});
    for (int x = surface.left(); x <= surface.right(); x += 8)
        scene.settleSnow({double(x), 423}, {double(x), 471}, 33);
}

QImage render(const SnowScene &scene)
{
    QImage frame(screen, QImage::Format_ARGB32_Premultiplied);
    frame.fill(Qt::transparent);
    QPainter painter(&frame);
    scene.paint(painter);
    painter.end();
    return frame;
}

bool form(SnowScene &scene, double time, int source, int sweep, const QPointF &drop)
{
    if (!scene.press({double(source), 455}, time))
        return false;
    scene.move({double(source + sweep), 455}, time + .28);
    scene.move({double(source + sweep), drop.y()}, time + .55);
    scene.move(drop, time + .85);
    tickFor(scene, .31);
    return scene.release(drop, time + 1.17);
}

bool aimedShot(SnowScene &scene, double time, int y)
{
    const QPointF source(140, 455);
    if (!scene.press(source, time) || !scene.release(source, time + .1) || !scene.aiming())
        return false;
    const QPointF target(581, y);
    scene.hover(target);
    return scene.fireAt(target);
}
} // namespace

class SnowSceneTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void snowman();
    void smallerBallSnapsWithoutDecorations();
    void wrongOrderRollsOff();
    void smallBallDoesNotSinkIntoLargeBall();
    void ballLandsOnDockAndRepaints();
    void restingBallCollectsMoreSnow();
    void releasedBallKeepsItsMomentum();
    void aimedSnowballWorksAtBallLimit();
    void splatLifecycle();
    void splatLimit();
    void aimFollowsCursorAndCanBeCancelled();
    void brokenSnowmanLeavesDrift();
    void partialSnowmanShatters_data();
    void partialSnowmanShatters();
    void singleBallBreaksIntoSnow();
    void saturatedDriftsHaveSlopingEdgesAndCrests();
    void shedSnowBuildsFloorGradually();
    void treeSnowAndLightsAreLocal();
    void hoverAndPlowRemoveSnow();
    void plowCrushesLooseBallsAndCarriesSnow();
    void secondPlowStartsAfterDelay();
    void fasterSnowCallsPlowEarlier();
    void oversizedSnowman();
    void garlandsFollowVisibleWindowFrames();
    void activeWindowCanShowGarlandWithoutStackingList();
    void windowAtScreenTopCollectsSnow();
    void windowHoverStaysClickThrough();
    void newSurfacesStayEmptyUntilSnowLands();
    void treeCollectsAndShedsSnow();
    void treeClearsWideAndPartialDocks();
    void depositsOnVisibleSurface();
    void driftsUseTheirVisibleCrest();
    void windowDriftsPreserveSnowAndShedIt();
    void asciiGlyphsUsePixelAlignedSprites();
};

void SnowSceneTest::snowman()
{
    SnowScene scene;
    fillDock(scene);

    QVERIFY(!scene.press({350, 220}, 0));
    QVERIFY(scene.inputRegion().contains(QPoint(112, 455)));
    const QRect cut(104, 430, 28, 40);
    const QRegion before = scene.inputRegion() & cut;
    QVERIFY(form(scene, 1, 112, 100, {300, 370}));
    tickFor(scene, .8);
    QVERIFY(!(before - scene.inputRegion()).isEmpty());
    QVERIFY(scene.inputRegion().contains(QPoint(300, 419))); // большое основание

    QVERIFY(form(scene, 3, 390, 30, {300, 399}));
    tickFor(scene, .4);
    QVERIFY(scene.inputRegion().contains(QPoint(300, 384))); // средний ком
    QVERIFY(scene.inputRegion().contains(QPoint(300, 424))); // правый клик по основанию
    QVERIFY(!scene.press({300, 424}, 4)); // левый клик не вытаскивает основание

    QVERIFY(form(scene, 5, 510, 0, {300, 355}));
    tickFor(scene, .75);
    QVERIFY(scene.inputRegion().contains(QPoint(300, 352)));
    QVERIFY(scene.inputRegion().contains(QPoint(300, 398)));
    const QImage snowman = render(scene);
    const QColor carrot = snowman.pixelColor(305, 352);
    QVERIFY(carrot.red() > 210 && carrot.green() < 160 && carrot.blue() < 100);
    QVERIFY(snowman.pixelColor(300, 391).red() < 80);
    const QPoint sparkle(332, 353);
    QVERIFY(snowman.pixelColor(sparkle).alpha() > 0);
    QVERIFY(scene.press({300, 352}, 7));
    QVERIFY(scene.advance(0).contains(sparkle)); // съём головы должен стереть и дальние искры
}

void SnowSceneTest::smallBallDoesNotSinkIntoLargeBall()
{
    SnowScene scene;
    fillDock(scene, QRect(70, 470, 205, 50));
    QVERIFY(form(scene, 1, 112, 100, {300, 455}));
    tickFor(scene, .8);
    const QRegion base = scene.inputRegion();
    QVERIFY(form(scene, 3, 250, 0, {325, 518}));
    tickFor(scene, 1.2);
    const QRegion input = scene.inputRegion();
    QVERIFY(input.contains(QPoint(300, 512)));
    QVERIFY(!(input - base).contains(QPoint(325, 529)));
    QVERIFY((input - base).intersects(QRect(338, 522, 105, 14)));
}

void SnowSceneTest::wrongOrderRollsOff()
{
    SnowScene scene;
    fillDock(scene);
    QVERIFY(form(scene, 1, 220, 0, {310, 407}));
    tickFor(scene, .7);
    QVERIFY(form(scene, 3, 475, 120, {310, 395}));
    tickFor(scene, 1.5);

    const QRegion input = scene.inputRegion();
    QVERIFY(input.contains(QPoint(310, 459)));
    QVERIFY(!input.contains(QPoint(310, 417)));
    QVERIFY(input.intersects(QRect(349, 415, 100, 34)) ||
            input.intersects(QRect(171, 415, 100, 34)));
}

void SnowSceneTest::smallerBallSnapsWithoutDecorations()
{
    SnowScene scene;
    fillDock(scene);
    QVERIFY(form(scene, 1, 220, 30, {290, 395}));
    tickFor(scene, .8);
    QVERIFY(form(scene, 3, 465, 0, {290, 424}));
    tickFor(scene, .9);
    QVERIFY(scene.inputRegion().contains(QPoint(290, 424)));
    QVERIFY(scene.inputRegion().contains(QPoint(298, 435)));
    QVERIFY(render(scene).pixelColor(293, 425).blue() > 150);

    scene.startPlow();
    tickFor(scene, 2.3);
    QVERIFY(!scene.inputRegion().contains(QPoint(290, 424)));
}

void SnowSceneTest::restingBallCollectsMoreSnow()
{
    SnowScene scene;
    fillDock(scene);
    QVERIFY(form(scene, 1, 110, 0, {41, 385}));
    tickFor(scene, .85);
    const QRect ballArea(10, 485, 58, 55);
    const QRect before = (scene.inputRegion() & ballArea).boundingRect();
    QVERIFY(!before.isEmpty());

    QVERIFY(scene.press(before.center(), 3));
    scene.move({390, 440}, 3.24);
    scene.move({520, 440}, 3.55);
    scene.move({575, 320}, 3.85);
    tickFor(scene, .35);
    QVERIFY(scene.release({575, 320}, 4.22));
    QVERIFY(!scene.aiming());
    tickFor(scene, 1.3);

    const QRect grown = (scene.inputRegion() & QRect(490, 220, 170, 202)).boundingRect();
    QVERIFY(grown.width() > before.width() + 12);
}

void SnowSceneTest::splatLifecycle()
{
    SnowScene scene;
    fillDock(scene);
    QVERIFY(aimedShot(scene, 1, 220));
    tickFor(scene, .10);
    QVERIFY(render(scene).pixelColor(580, 220).alpha() == 0);
    QVERIFY(!scene.inputRegion().contains(QPoint(349, 200)));
    tickFor(scene, .21);
    QVERIFY(render(scene).pixelColor(581, 220).alpha() > 160);
    QVERIFY(!scene.inputRegion().contains(QPoint(581, 220)));

    tickFor(scene, 1.1);
    QVERIFY(render(scene).pixelColor(581, 220).alpha() > 160);
    tickFor(scene, 2.2);
    QVERIFY(render(scene).pixelColor(581, 257).alpha() > 160);
    tickFor(scene, 2.0);
    QVERIFY(render(scene).pixelColor(581, 257).alpha() == 0);
}

void SnowSceneTest::splatLimit()
{
    SnowScene scene;
    for (int i = 0; i < 6; ++i) {
        fillDock(scene);
        QVERIFY(aimedShot(scene, i * 2, 55 + i * 57));
        tickFor(scene, .26);
    }
    QVERIFY(render(scene).pixelColor(581, 55).alpha() > 160);

    fillDock(scene);
    QVERIFY(aimedShot(scene, 12, 55 + 6 * 57));
    tickFor(scene, .31);
    const QImage frame = render(scene);
    for (int i = 1; i < 7; ++i)
        QVERIFY(frame.pixelColor(581, 55 + i * 57).alpha() > 160);
    QVERIFY(frame.pixelColor(581, 55).alpha() < 160);
}

void SnowSceneTest::newSurfacesStayEmptyUntilSnowLands()
{
    SnowScene scene;
    const QRect maximized(0, 0, screen.width(), dock.y());
    scene.setLayout(screen, {}, maximized);
    QVERIFY(!scene.inputRegion().contains(QPoint(40, screen.height() - 2)));
    QCOMPARE(render(scene).pixelColor(40, screen.height() - 2).alpha(), 0);

    QVERIFY(scene.settleSnow({40, 510}, {40, 542}, 30));
    const QPoint floorSnow(40, screen.height() - 2);
    QVERIFY(scene.inputRegion().contains(floorSnow));

    scene.setLayout(screen, dock, maximized);
    const QPoint dockSnow(420, dock.y() - 2);
    QCOMPARE(render(scene).pixelColor(dockSnow).alpha(), 0);
    QVERIFY(!scene.inputRegion().contains(dockSnow));
    QVERIFY(scene.inputRegion().contains(floorSnow));
    QVERIFY(scene.settleSnow({420, 440}, {420, 472}, 30));
    QVERIFY(scene.inputRegion().contains(dockSnow));
    QVERIFY(render(scene).pixelColor(dockSnow).alpha() > 0);

    scene.setLayout(QSize(screen.width() + 80, screen.height()), dock, maximized);
    QVERIFY(scene.inputRegion().contains(floorSnow));
    QVERIFY(scene.inputRegion().contains(dockSnow));
    QVERIFY(!scene.inputRegion().contains(QPoint(screen.width() + 40, floorSnow.y())));
}

void SnowSceneTest::treeCollectsAndShedsSnow()
{
    SnowScene scene;
    scene.setLayout(screen, QRect(0, 485, screen.width(), 55), {});
    const QPointF branch(774, 390);
    const QRect crest(763, 377, 21, 13);
    scene.setGarlandsEnabled(false);
    const QImage dryTree = render(scene);
    QVERIFY(qAlpha(dryTree.pixel(774, 400)) > 0);

    for (const int offset : {-16, 0, 16})
        QVERIFY(scene.settleSnow(branch + QPointF(offset, -18), branch + QPointF(offset, 6), 32));
    const QImage snowyTree = render(scene);
    QVERIFY(snowyTree.copy(crest) != dryTree.copy(crest));
    QVERIFY(scene.inputRegion().contains(QPoint(774, 385)));

    const auto canopy = SnowProps::treeCanopy(QRect(724, 353, 100, 132));
    QPoint snowyEdge;
    for (int x = 755; x < 770 && snowyEdge.isNull(); ++x) {
        for (int y = 384; y < 389; ++y) {
            const QPoint point(x, y);
            if (!canopy.contains(point) && snowyTree.pixelColor(point).alpha() > 160 &&
                dryTree.pixelColor(point).alpha() == 0) {
                snowyEdge = point;
                break;
            }
        }
    }
    QVERIFY(!snowyEdge.isNull());
    scene.hoverWindows({730, 400});
    scene.hoverWindows(snowyEdge);
    QCOMPARE(render(scene).copy(crest), snowyTree.copy(crest));

    QVERIFY(scene.inputRegion().contains(snowyEdge));
    QVERIFY(scene.breakSnow(snowyEdge));
    scene.advance(.065);
    QVERIFY(render(scene).copy(733, 389, 90, 54) != dryTree.copy(733, 389, 90, 54));
    tickFor(scene, 1.4);
    QCOMPARE(render(scene).copy(crest), dryTree.copy(crest));
}

void SnowSceneTest::treeClearsWideAndPartialDocks()
{
    SnowScene scene;
    // Панель широкая и заходит под левый край ёлки, не доходя до её центра.
    scene.setLayout(screen, QRect(0, 425, 750, 115), {});
    const QImage frame = render(scene);
    QVERIFY(qAlpha(frame.pixel(774, 365)) > 0);
    for (int y = 425; y < screen.height(); ++y) {
        for (int x = 720; x < screen.width(); ++x)
            QCOMPARE(qAlpha(frame.pixel(x, y)), 0);
    }
    QVERIFY(scene.settleSnow({774, 309}, {774, 335}, 24));
}

void SnowSceneTest::depositsOnVisibleSurface()
{
    SnowScene scene;
    scene.setLayout(screen, dock, QRect(330, 210, 150, 170));
    QVERIFY(scene.settleSnow({420, 205}, {420, 216}, 18));
    QVERIFY(render(scene).pixelColor(420, 204).alpha() > 0);
    QVERIFY(!scene.inputRegion().contains(QPoint(420, 204)));
    QVERIFY(scene.settleSnow({670, 422}, {670, 490}, 18));
    QVERIFY(scene.inputRegion().contains(QPoint(670, 463)));
    QVERIFY(!scene.inputRegion().contains(QPoint(670, 475)));
}

void SnowSceneTest::aimFollowsCursorAndCanBeCancelled()
{
    SnowScene scene;
    fillDock(scene);
    const QPointF source(140, 455);
    QVERIFY(scene.press(source, 1));
    QVERIFY(scene.release(source, 1.09));
    QVERIFY(scene.aiming());
    const QPoint target(360, 250);
    scene.hover(target);
    QVERIFY(scene.inputRegion().contains(target));
    QVERIFY(render(scene).pixelColor(target).alpha() > 200);
    QVERIFY(!scene.press(target, 2));
    scene.cancelAim();
    QVERIFY(!scene.aiming());
    QVERIFY(!scene.inputRegion().contains(target));
    QCOMPARE(render(scene).pixelColor(target).alpha(), 0);
}

void SnowSceneTest::brokenSnowmanLeavesDrift()
{
    SnowScene scene;
    fillDock(scene);
    QVERIFY(form(scene, 1, 112, 100, {300, 370}));
    tickFor(scene, .8);
    QVERIFY(!scene.aiming());
    QVERIFY(form(scene, 3, 390, 30, {300, 399}));
    tickFor(scene, .4);
    QVERIFY(form(scene, 5, 510, 0, {300, 355}));
    tickFor(scene, 1);
    const QRegion before = scene.inputRegion() & QRect(240, 428, 120, 42);
    QVERIFY(scene.breakSnow({300, 398}));
    QVERIFY(!scene.inputRegion().contains(QPoint(300, 352)));

    QVERIFY(!scene.breakSnow({300, 445}));
    const QImage first = render(scene).copy(250, 315, 100, 104);
    tickFor(scene, .15);
    QVERIFY(first != render(scene).copy(250, 315, 100, 104));
    tickFor(scene, 2);
    QCOMPARE(render(scene).pixelColor(300, 352).alpha(), 0);
    QVERIFY(!((scene.inputRegion() - before) & QRect(240, 428, 120, 42)).isEmpty());
}

void SnowSceneTest::partialSnowmanShatters_data()
{
    QTest::addColumn<int>("hit");
    QTest::newRow("base") << 0;
    QTest::newRow("top") << 1;
    QTest::newRow("plow") << 2;
}

void SnowSceneTest::partialSnowmanShatters()
{
    QFETCH(int, hit);
    SnowScene scene;
    fillDock(scene);
    QVERIFY(form(scene, 1, 112, 100, {300, 370}));
    tickFor(scene, .8);
    QVERIFY(form(scene, 3, 390, 30, {300, 399}));
    tickFor(scene, .4);
    QVERIFY(scene.inputRegion().contains(QPoint(300, 384)));
    if (hit == 2) {
        scene.startPlow();
        tickFor(scene, 1.9);
        QVERIFY(!scene.inputRegion().contains(QPoint(300, 384)));
    } else {
        QVERIFY(scene.breakSnow(hit == 0 ? QPointF(300, 420) : QPointF(300, 384)));
    }
    QVERIFY(!scene.breakSnow({300, 420}));
    QVERIFY(!scene.inputRegion().contains(QPoint(300, 384)));
}

void SnowSceneTest::hoverAndPlowRemoveSnow()
{
    SnowScene scene;
    fillDock(scene);
    const QRegion dockBefore = scene.inputRegion();
    scene.hover({200, 450});
    scene.hover({227, 450});
    const QRegion disturbed = scene.advance(0);
    QVERIFY(disturbed.intersects(QRect(195, 420, 67, 40)));
    QVERIFY(!(dockBefore - scene.inputRegion()).isEmpty());
    QVERIFY(!scene.dragging());
    QVERIFY(!scene.aiming());

    const QRect swept(150, 430, 110, 40);
    const QRegion beforePlow = scene.inputRegion() & swept;
    scene.startPlow();
    tickFor(scene, 1.9);
    QVERIFY(!(beforePlow - scene.inputRegion()).isEmpty());
}

void SnowSceneTest::singleBallBreaksIntoSnow()
{
    SnowScene scene;
    fillDock(scene, QRect(70, 470, 205, 50));
    QVERIFY(form(scene, 1, 112, 20, {354, 390}));
    tickFor(scene, 1);
    const QPointF ball(354, screen.height() - 13);
    QVERIFY(scene.inputRegion().contains(ball.toPoint()));
    QVERIFY(scene.breakSnow(ball));
    QVERIFY(!scene.breakSnow(ball));
    tickFor(scene, 1.2);
    QVERIFY(scene.inputRegion().intersects(QRect(320, screen.height() - 6, 68, 6)));
}

void SnowSceneTest::saturatedDriftsHaveSlopingEdgesAndCrests()
{
    SnowScene scene;
    const SnowSurface app{QStringLiteral("roof"), QRect(25, 145, 600, 100)};
    scene.setLayout(screen, {}, {}, {app});
    for (int x = app.rect.left(); x < app.rect.right(); x += 4)
        scene.settleSnow({double(x), 95}, {double(x), 146}, 120);
    const QImage frame = render(scene);
    const auto depth = [&](int x) {
        int y = app.rect.top() - 1;
        while (y >= 95 && frame.pixelColor(x, y).alpha() >= 128)
            --y;
        return app.rect.top() - 1 - y;
    };
    QVERIFY(depth(app.rect.left() + 3) < 3);
    QVERIFY(depth(app.rect.right() - 3) < 3);
    QVERIFY(depth(app.rect.left() + 24) > 8);
    int low = 100, high = 0;
    for (int x = 80; x < 565; x += 8) {
        low = qMin(low, depth(x));
        high = qMax(high, depth(x));
    }
    QVERIFY(high - low >= 4);
}

void SnowSceneTest::shedSnowBuildsFloorGradually()
{
    SnowScene scene;
    const SnowSurface app{QStringLiteral("heavy"), QRect(12, 430, 600, 65)};
    scene.setLayout(screen, {}, {}, {app});
    for (int x = 30; x < 600; x += 5)
        scene.settleSnow({double(x), 389}, {double(x), 431}, 100);
    scene.setLayout(screen, {}, {});
    const QRect strip(6, screen.height() - 40, 610, 40);
    QVERIFY(!scene.inputRegion().intersects(strip));
    int previous = 0;
    for (int i = 0; i < 330; ++i) {
        scene.advance(.016);
        const QRect snow = (scene.inputRegion() & strip).boundingRect();
        const int height = snow.isEmpty() ? 0 : screen.height() - snow.top();
        QVERIFY(height <= previous + (previous == 0 ? 4 : 2));
        previous = height;
    }
    QVERIFY(previous >= 8);
}

void SnowSceneTest::treeSnowAndLightsAreLocal()
{
    SnowScene scene;
    scene.setLayout(screen, QRect(0, 485, screen.width(), 55), {});
    const QRect tree(724, 353, 100, 132);
    const QImage lit = render(scene);
    scene.advance(.37);
    QVERIFY(render(scene).copy(tree) != lit.copy(tree));
    QVERIFY(scene.setGarlandsEnabled(false).intersects(tree));
    const QImage dry = render(scene);
    QVERIFY(lit.copy(tree) != dry.copy(tree));
    QVERIFY(scene.settleSnow({754, 352}, {754, 450}, 5));
    const QImage snow = render(scene);
    QVERIFY(dry.copy(744, 360, 25, 90) != snow.copy(744, 360, 25, 90));
    QCOMPARE(dry.copy(776, 353, 48, 132), snow.copy(776, 353, 48, 132));
}

void SnowSceneTest::plowCrushesLooseBallsAndCarriesSnow()
{
    SnowScene scene;
    scene.setLayout(screen, dock, {});
    for (int x = dock.left(); x < 195; x += 8)
        QVERIFY(scene.settleSnow({double(x), 420}, {double(x), 471}, 30));
    for (int x = 415; x < 545; x += 8)
        QVERIFY(scene.settleSnow({double(x), 420}, {double(x), 471}, 23));
    QVERIFY(form(scene, 1, 124, 0, {38, 350}));
    tickFor(scene, 1);
    QVERIFY(form(scene, 3, 430, 100, {390, 350}));
    tickFor(scene, 1);
    const QPoint floorBall(40, 525);
    const QPoint dockBall(390, 420);
    QVERIFY(scene.inputRegion().contains(floorBall));
    QVERIFY(scene.inputRegion().contains(dockBall));

    SnowScene empty;
    empty.setLayout(screen, dock, {});
    empty.startPlow();
    scene.startPlow();
    tickFor(scene, 1.6);
    tickFor(empty, 1.6);
    QVERIFY(!scene.inputRegion().contains(floorBall));
    const QPoint pile(276, 454);
    QVERIFY(render(scene).pixelColor(pile).alpha() > 180);
    QVERIFY(render(empty).pixelColor(pile).alpha() < 100);
    tickFor(scene, 2.1);
    QVERIFY(!scene.inputRegion().contains(dockBall));
}

void SnowSceneTest::secondPlowStartsAfterDelay()
{
    const auto hasPlow = [](const QImage &frame, const QRect &area) {
        for (int y = area.top(); y <= area.bottom(); ++y) {
            for (int x = area.left(); x <= area.right(); ++x) {
                const QColor pixel = frame.pixelColor(x, y);
                if (pixel.alpha() > 210 && pixel.red() > 180 && pixel.green() < 165 && pixel.blue() < 65)
                    return true;
            }
        }
        return false;
    };

    SnowScene single, tandem;
    single.setLayout(screen, {}, {});
    tandem.setLayout(screen, {}, {});
    tandem.setSecondPlowEnabled(true);
    single.startPlow();
    tandem.startPlow();
    const QRect entry(10, 507, 100, 32);
    const QRect ahead(190, 507, 185, 32);
    tickFor(tandem, .75);
    QVERIFY(hasPlow(render(tandem), entry));
    tickFor(tandem, .63);
    QVERIFY(hasPlow(render(tandem), ahead));
    QVERIFY(!hasPlow(render(tandem), entry));
    tickFor(tandem, .5);
    tickFor(single, 1.88);
    QVERIFY(hasPlow(render(tandem), ahead));
    QVERIFY(hasPlow(render(tandem), entry));
    QVERIFY(!hasPlow(render(single), entry));
}

void SnowSceneTest::fasterSnowCallsPlowEarlier()
{
    SnowScene normal, fast;
    normal.setLayout(screen, {}, {});
    fast.setLayout(screen, {}, {});
    fast.setPlowCadence(4);
    normal.advance(12.1);
    fast.advance(12.1);
    tickFor(normal, .7);
    tickFor(fast, .7);
    const QPoint cabin(65, screen.height() - 17);
    QCOMPARE(render(normal).pixelColor(cabin).alpha(), 0);
    QVERIFY(render(fast).pixelColor(cabin).alpha() > 200);
}

void SnowSceneTest::oversizedSnowman()
{
    SnowScene scene;
    fillDock(scene);
    QVERIFY(form(scene, 1, 100, 530, {420, 300}));
    tickFor(scene, 1);
    QVERIFY(scene.inputRegion().contains(QPoint(420, 385)));
    QVERIFY(render(scene).pixelColor(420, 385).alpha() > 170);

    fillDock(scene);
    QVERIFY(form(scene, 3, 100, 380, {420, 328}));
    tickFor(scene, .8);
    fillDock(scene);
    QVERIFY(form(scene, 5, 100, 220, {420, 255}));
    tickFor(scene, 1);
    QVERIFY(scene.inputRegion().contains(QPoint(420, 215)));
    QVERIFY(scene.breakSnow({420, 328}));
}

void SnowSceneTest::garlandsFollowVisibleWindowFrames()
{
    SnowScene scene;
    SnowSurface app{QStringLiteral("app"), QRect(90, 180, 520, 220)};
    const SnowSurface fullscreen{QStringLiteral("fullscreen"), QRect(295, 135, 160, 150), false, true};
    const QRect visible(100, 182, 150, 18);
    const QRect covered(310, 182, 120, 18);
    const QRect fullscreenTop(310, 135, 120, 19);
    scene.setLayout(screen, dock, {});
    const QImage blank = render(scene);

    scene.setLayout(screen, dock, app.rect, {app, fullscreen});
    QCOMPARE(render(scene).copy(visible), blank.copy(visible));
    scene.advance(.03);
    QCOMPARE(render(scene).copy(visible), blank.copy(visible));
    QVERIFY(scene.advance(.24).intersects(visible));
    const QImage frame = render(scene);
    QVERIFY(frame.copy(visible) != blank.copy(visible));
    QCOMPARE(frame.copy(covered), blank.copy(covered));
    QCOMPARE(frame.copy(fullscreenTop), blank.copy(fullscreenTop));
    QVERIFY(!scene.inputRegion().intersects(visible));
    QVERIFY(frame.pixelColor(app.rect.left(), app.rect.y() + 6).alpha() > 0);
    QVERIFY(frame.pixelColor(app.rect.right(), app.rect.y() + 6).alpha() > 0);

    app.rect.translate(0, 40);
    scene.setLayout(screen, dock, app.rect, {app, fullscreen});
    const QRect moved = visible.translated(0, 40);
    const QRegion dirty = scene.advance(0);
    QVERIFY(dirty.intersects(visible));
    QVERIFY(dirty.intersects(moved));
    const QImage afterMove = render(scene);
    QCOMPARE(afterMove.copy(visible), blank.copy(visible));
    QVERIFY(afterMove.copy(moved) != blank.copy(moved));

    scene.setLayout(screen, dock, {}, {fullscreen});
    QVERIFY(scene.advance(0).intersects(moved));
    QCOMPARE(render(scene).copy(moved), blank.copy(moved));
}

void SnowSceneTest::windowHoverStaysClickThrough()
{
    SnowScene scene;
    const SnowSurface window{QStringLiteral("hover"), QRect(140, 245, 330, 160)};
    scene.setLayout(screen, dock, window.rect, {window});
    for (int x = 200; x <= 272; x += 8)
        QVERIFY(scene.settleSnow({double(x), 208}, {double(x), 246}, 36));
    scene.advance(0);

    const QPoint point(245, 230);
    QVERIFY(!scene.inputRegion().contains(point));
    scene.hoverWindows({215, 230});
    scene.hoverWindows(point);
    QVERIFY(scene.advance(0).intersects(QRect(210, 210, 66, 34)));
    QVERIFY(!scene.inputRegion().contains(point));
}

void SnowSceneTest::activeWindowCanShowGarlandWithoutStackingList()
{
    SnowScene scene;
    const QRect app(100, 135, 460, 290);
    const QRect strip(app.x(), app.y(), app.width(), 24);
    scene.setLayout(screen, dock, {});
    const QImage blank = render(scene).copy(strip);
    scene.setLayout(screen, dock, app);
    QCOMPARE(render(scene).copy(strip), blank);
    scene.advance(.26);
    QVERIFY(render(scene).copy(strip) != blank);
    const QRegion damage = scene.setGarlandsEnabled(false);
    QVERIFY(damage.intersects(strip));
    QCOMPARE(render(scene).copy(strip), blank);
}

void SnowSceneTest::windowAtScreenTopCollectsSnow()
{
    SnowScene scene;
    const SnowSurface app{QStringLiteral("top-aligned"), QRect(100, 0, screen.width() - 200, dock.y())};
    scene.setGarlandsEnabled(false);
    scene.setLayout(screen, dock, app.rect, {app});
    QCOMPARE(render(scene).pixelColor(420, 5).alpha(), 0);
    QVERIFY(scene.settleSnow({420, 0}, {420, 12}, 20));
    QVERIFY(render(scene).pixelColor(420, 5).alpha() > 0);
    QVERIFY(!scene.inputRegion().contains(QPoint(420, 5)));

    SnowScene fullscreen;
    fullscreen.setLayout(screen, dock, app.rect, {}, true);
    QVERIFY(!fullscreen.settleSnow({420, 0}, {420, 12}, 20));
    QCOMPARE(render(fullscreen).pixelColor(420, 5).alpha(), 0);
}

void SnowSceneTest::ballLandsOnDockAndRepaints()
{
    for (const int y : {100, 120}) {
        SnowScene scene;
        fillDock(scene);
        const QPointF drop(580, y);
        QVERIFY(scene.press({140, 455}, 0));
        scene.move({140, drop.y()}, .2);
        scene.move(drop, .4);
        tickFor(scene, .2);
        QVERIFY(scene.release(drop, .6));
        scene.advance(.05);

        QImage before = render(scene);
        for (int i = 0; i < 25; ++i) {
            const QRegion dirty = scene.advance(.05);
            const QImage after = render(scene);
            for (int row = 410; row < screen.height(); ++row) {
                for (int x = 560; x < 597; ++x) {
                    if (before.pixel(x, row) != after.pixel(x, row))
                        QVERIFY(dirty.contains(QPoint(x, row)));
                }
            }
            before = after;
        }

        QVERIFY(before.pixelColor(577, 447).alpha() > 0);
        QVERIFY(!scene.inputRegion().contains(QPoint(577, 528)));
    }
}

void SnowSceneTest::releasedBallKeepsItsMomentum()
{
    SnowScene scene;
    fillDock(scene);
    const auto ballCenter = [&scene] {
        const QImage image = render(scene);
        QRect bounds;
        for (int y = 180; y < 420; ++y) {
            for (int x = 100; x < 500; ++x) {
                if (qAlpha(image.pixel(x, y)) > 160)
                    bounds |= QRect(x, y, 1, 1);
            }
        }
        return bounds.center();
    };
    QVERIFY(scene.press({140, 455}, 0));
    scene.move({140, 340}, .2);
    tickFor(scene, .28);
    scene.move({245, 290}, .24);
    scene.advance(.016);
    const QPoint beforeRelease = ballCenter();
    QVERIFY(beforeRelease.x() > 140 && beforeRelease.x() < 210);

    QVERIFY(scene.release({245, 290}, .26));
    tickFor(scene, .12);
    const QPoint inFlight = ballCenter();
    QVERIFY(inFlight.x() > beforeRelease.x() + 25);
    QVERIFY(inFlight.y() < beforeRelease.y() - 12);
    tickFor(scene, .45);
    const QPoint falling = ballCenter();
    QVERIFY(falling.x() > inFlight.x());
    QVERIFY(falling.y() > inFlight.y());
}

void SnowSceneTest::aimedSnowballWorksAtBallLimit()
{
    SnowScene scene;
    for (int i = 0; i < 34; ++i) {
        fillDock(scene);
        const double x = qMin(777, 27 + i * 24 + (i >= 4 ? 48 : 0));
        QVERIFY(form(scene, 2 * i, 140, 0, {x, 300}));
        tickFor(scene, .9);
    }
    fillDock(scene);
    const QPointF source(140, 455);
    QVERIFY(scene.inputRegion().contains(source.toPoint()));
    const QRect farFloor(802, 510, 38, 30);
    const QRegion before = scene.inputRegion() & farFloor;
    QVERIFY(scene.press(source, 70));
    scene.move({90, 455}, 70.2);
    const QPointF target(815, 150);
    scene.move(target, 70.4);
    tickFor(scene, .3);
    QVERIFY(scene.release(target, 70.7));
    QVERIFY(!scene.dragging());
    QVERIFY(!scene.aiming());
    QCOMPARE(render(scene).pixelColor(target.toPoint()).alpha(), 0);
    tickFor(scene, 1.2);
    QCOMPARE(scene.inputRegion() & farFloor, before);

    QVERIFY(scene.press(source, 72));
    QVERIFY(scene.release(source, 72.1));
    QVERIFY(scene.aiming());
    QVERIFY(scene.fireAt({410, 140}));
}

void SnowSceneTest::driftsUseTheirVisibleCrest()
{
    SnowScene scene;
    fillDock(scene);
    QVERIFY(scene.settleSnow({305, 425}, {305, 458}, 10));
    QVERIFY(scene.inputRegion().contains(QPoint(305, 455)));
    QVERIFY(!scene.inputRegion().contains(QPoint(305, 483)));
    QVERIFY(render(scene).pixelColor(305, 455).alpha() > 0);
}

void SnowSceneTest::windowDriftsPreserveSnowAndShedIt()
{
    SnowScene scene;
    SnowSurface lower{QStringLiteral("low"), QRect(90, 230, 360, 175)};
    const SnowSurface upper{QStringLiteral("up"), QRect(250, 185, 175, 150)};
    scene.setLayout(screen, dock, lower.rect, {lower, upper});
    QVERIFY(scene.settleSnow({178, 200}, {178, 232}, 60));
    QVERIFY(!scene.settleSnow({320, 150}, {320, 235}, 60, false, true));
    QVERIFY(render(scene).pixelColor(320, 177).alpha() > 0);
    QVERIFY(render(scene).pixelColor(178, 222).alpha() > 0);
    QCOMPARE(render(scene).pixelColor(320, 224).alpha(), 0);

    scene.setLayout(screen, dock, lower.rect, {upper, lower});
    QVERIFY(render(scene).pixelColor(320, 224).alpha() > 0);

    scene.setLayout(screen, dock, upper.rect, {lower, upper});
    QVERIFY(render(scene).pixelColor(178, 222).alpha() > 0);

    lower.rect.translate(3, 2);
    scene.setLayout(screen, dock, upper.rect, {lower, upper});
    QVERIFY(render(scene).pixelColor(180, 224).alpha() > 0);

    const QRegion dockBefore = scene.inputRegion();
    scene.setLayout(screen, dock, upper.rect, {upper});
    QVERIFY(scene.advance(0).contains(QPoint(180, 224)));
    tickFor(scene, .6);
    const QImage image = render(scene);
    bool sawFallingSnow = false;
    for (int x = 112; x < 235 && !sawFallingSnow; ++x)
        for (int y = 254; y < 321 && !sawFallingSnow; ++y)
            sawFallingSnow = image.pixelColor(x, y).alpha() > 20;
    QVERIFY(sawFallingSnow);

    tickFor(scene, 2);
    QVERIFY(!(scene.inputRegion() - dockBefore).isEmpty());
}

void SnowSceneTest::asciiGlyphsUsePixelAlignedSprites()
{
    QImage image(48, 30, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    SnowCrystal::paint(painter, {9.2, 10.2}, 3, 1, SnowCrystal::Glyph::Plus);
    SnowCrystal::paint(painter, {23, 10}, 3, .5, SnowCrystal::Glyph::Cross);
    SnowCrystal::paint(painter, {39, 12}, 1, 1, SnowCrystal::Glyph::Asterisk);
    SnowCrystal::paint(painter, {9, 23}, 6, 1, SnowCrystal::Glyph::Asterisk);
    SnowCrystal::paintNatural(painter, {30, 23}, 5);
    painter.end();

    QCOMPARE(SnowCrystal::bounds({9.2, 10.2}, 3), QRect(7, 8, 5, 5));
    QCOMPARE(image.pixelColor(7, 10).alpha(), 255); // поперечина '+'
    QCOMPARE(image.pixelColor(7, 8).alpha(), 0);
    QVERIFY(qAbs(image.pixelColor(21, 8).alpha() - 128) <= 9); // диагональ 'x'
    QCOMPARE(image.pixelColor(23, 8).alpha(), 0);
    QCOMPARE(image.pixelColor(39, 12).alpha(), 255); // дальняя точка
    QCOMPARE(image.pixelColor(40, 12).alpha(), 0);
    QCOMPARE(image.pixelColor(4, 18).alpha(), 255); // масштабированная '*'
    QCOMPARE(image.pixelColor(6, 18).alpha(), 0);
    QCOMPARE(SnowCrystal::naturalBounds({30, 23}, 5), QRect(25, 18, 11, 11));
    QVERIFY(image.pixelColor(30, 23).alpha() > 140);
    QVERIFY(image.pixelColor(25, 18).alpha() < 20);
}

QTEST_APPLESS_MAIN(SnowSceneTest)
#include "SnowSceneTest.moc"
