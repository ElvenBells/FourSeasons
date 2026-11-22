#include "KWinTracker.h"
#include "Constants.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryFile>
#include <QUuid>

namespace {

// --- адреса KWin ---------------------------------------------------------
const QString kKWinService    = QStringLiteral("org.kde.KWin");
const QString kKWinPath       = QStringLiteral("/KWin");
const QString kScriptingPath  = QStringLiteral("/Scripting");
const QString kScriptingIface = QStringLiteral("org.kde.kwin.Scripting");
const QString kScriptIface    = QStringLiteral("org.kde.kwin.Script");
const QString kPluginName     = QStringLiteral("snowoverlay-tracker");

// --- наш собственный D-Bus сервис (его вызывает KWin-скрипт) --------------
const QString kService   = QStringLiteral("org.snowoverlay.SnowOverlay");
const QString kPath      = QStringLiteral("/Tracker");
const QString kInterface = QStringLiteral("org.snowoverlay.Tracker"); // = Q_CLASSINFO в KWinTracker.h

// Синхронный вызов метода KWin. QDBusInterface здесь не используем:
// он делает интроспекцию и «ломается», если объекта по угаданному пути нет.
QDBusMessage callKWin(const QString &path, const QString &iface, const QString &method,
                      const QVariantList &args = {})
{
    QDBusMessage msg = QDBusMessage::createMethodCall(kKWinService, path, iface, method);
    msg.setArguments(args);
    return QDBusConnection::sessionBus().call(msg, QDBus::Block, 2000);
}

// ---------------------------------------------------------------------------
// Скрипт для KWin. Только var/function (без let/const/стрелочных функций) —
// так он гарантированно работает и в Plasma 5.27, и в Plasma 6.
// Плейсхолдеры @...@ подставляются в KWinTracker::buildScript().
// ---------------------------------------------------------------------------
const char kScriptTemplate[] = R"JS(
// SnowOverlay: служебный скрипт KWin. Создаётся приложением автоматически.
(function () {
    var SERVICE = "@SERVICE@";
    var PATH    = "@PATH@";
    var IFACE   = "@IFACE@";
    var APP_ID  = "@APPID@";
    var TITLE   = "@TITLE@";
    var TOKEN   = "@TOKEN@";
    var TRACK_CURSOR = @TRACKCURSOR@;
    var OVERLAY = { x: @OX@, y: @OY@, width: @OW@, height: @OH@ };

    var W = workspace;
    var watched = [];        // окна, на сигналы которых мы уже подписаны
    var windowIds = [];
    var nextId = 1;
    var lastActive = null;   // последнее активное окно, не являющееся оверлеем
    var lastMsg = "";
    var focusTimer = new QTimer();
    focusTimer.singleShot = true;
    var geometryTimer = new QTimer();
    geometryTimer.singleShot = true;
    var cursorTimer = new QTimer();
    cursorTimer.singleShot = true;
    var cursorAreas = [];
    var cursorNearSnow = false;
    var cursorEverywhere = false;
    var lastCursorProbe = 0;

    // ---- совместимость: Plasma 5.27 (client*) и Plasma 6 (window*) ----
    function getActive()  { return (W.activeWindow !== undefined) ? W.activeWindow : W.activeClient; }
    function setActive(w) { if (W.activeWindow !== undefined) W.activeWindow = w; else W.activeClient = w; }
    function allWindows() { return (typeof W.windowList === "function") ? W.windowList() : W.clientList(); }
    var sigActivated = W.windowActivated || W.clientActivated;
    var sigAdded     = W.windowAdded     || W.clientAdded;
    var sigRemoved   = W.windowRemoved   || W.clientRemoved;

    function str(v) {
        try { return (v === undefined || v === null) ? "" : String(v); } catch (e) { return ""; }
    }

    // Qt на X11 дописывает к заголовку отображаемое имя приложения.
    // Проверка класса отделяет оверлей от чужих окон с похожим заголовком.
    function isOverlay(w) {
        if (!w) return false;
        return str(w.resourceClass).toLowerCase() === APP_ID && str(w.caption).indexOf(TITLE) === 0;
    }

    // Окна, вокруг которых «дырку» в снегу делать не нужно.
    function isIgnored(w) {
        return !w || isOverlay(w) || w.onScreenDisplay || w.desktopWindow || w.dock || w.specialWindow || w.appletPopup ||
            w.minimized || w.hidden || w.readyForPainting === false || w.hiddenByShowDesktop;
    }

    function onCurrentDesktop(c) {
        if (c.onAllDesktops) return true;
        if (typeof W.currentDesktop === "number") return c.desktop === W.currentDesktop;   // Plasma 5
        if (!c.desktops) return true;
        for (var i = 0; i < c.desktops.length; ++i)                                         // Plasma 6
            if (c.desktops[i] === W.currentDesktop) return true;
        return false;
    }

    function geometry(w) {
        var g = w.frameGeometry || w.geometry;
        if (!g || g.width < 1 || g.height < 1) return "0";
        return "1," + Math.round(g.x) + "," + Math.round(g.y) + ","
            + Math.round(g.width) + "," + Math.round(g.height);
    }

    function send(w) {
        var parts = [isIgnored(w) ? "0" : geometry(w)];
        if (parts[0] !== "0" && w.fullScreen)
            parts[0] = "F:" + parts[0];
        var areas = [];
        var order = W.stackingOrder;
        if (!order || !order.length) {
            order = watched.slice();
            var activeIndex = order.indexOf(w);
            if (activeIndex >= 0) order.push(order.splice(activeIndex, 1)[0]);
        }
        for (var i = 0; i < order.length; ++i) {
            var client = order[i];
            var index = watched.indexOf(client);
            var shell = str(client.resourceClass).toLowerCase();
            var plasma = shell === "plasmashell" || shell === "org.kde.plasmashell";
            var popup = plasma && (client.appletPopup || (typeof client.appletPopup === "undefined" &&
                (client.popupMenu || client.dialog) && !client.tooltip));
            if (index < 0 || (client.managed === false && !popup) || isOverlay(client) || client.onScreenDisplay || client.minimized || client.hidden ||
                client.hiddenByShowDesktop || client.readyForPainting === false || client.deleted)
                continue;
            if (!client.dock && (client.desktopWindow || (client.specialWindow && !popup) || !onCurrentDesktop(client)))
                continue;
            var rect = geometry(client);
            if (rect !== "0") {
                var type = popup ? "P" : client.dock ? "D" : client.fullScreen ? "F" : client.maximizeMode === 3 ? "M" : "W";
                parts.push(type + windowIds[index] + ":" + rect);
                if (TRACK_CURSOR && !client.dock && !client.fullScreen) {
                    var frame = client.frameGeometry || client.geometry;
                    if (frame.y >= OVERLAY.y && frame.y < OVERLAY.y + OVERLAY.height)
                        areas.push(frame);
                }
            }
        }
        cursorAreas = areas;
        var msg = parts.join(";");
        if (msg === lastMsg) return;
        lastMsg = msg;
        callDBus(SERVICE, PATH, IFACE, "geometryMessage", TOKEN, msg);
    }

    function scheduleGeometry() {
        if (!geometryTimer.active)
            geometryTimer.start(16);
    }

    function reportCursor() {
        var p = W.cursorPos;
        if (!p) return;
        // Учитываем ёлку и запас по высоте нижней панели.
        var right = OVERLAY.x + OVERLAY.width;
        var bottom = OVERLAY.y + OVERLAY.height;
        var treeAreaHeight = 132 + Math.ceil(OVERLAY.height * .28);
        var near = p.x >= right - 144 && p.x < right && p.y >= bottom - treeAreaHeight && p.y < bottom;
        for (var i = 0; !near && i < cursorAreas.length; ++i) {
            var r = cursorAreas[i];
            if (p.x >= r.x && p.x < r.x + r.width && p.y >= r.y - 32 &&
                p.y <= Math.max(r.y + 2, OVERLAY.y + 8)) {
                near = true;
                break;
            }
        }
        var now = Date.now();
        if (near || cursorNearSnow || cursorEverywhere || now - lastCursorProbe >= 350) {
            lastCursorProbe = now;
            callDBus(SERVICE, PATH, IFACE, "cursorMessage", TOKEN, Math.round(p.x) + "," + Math.round(p.y),
                function (enabled) { cursorEverywhere = enabled === true; });
        }
        cursorNearSnow = near;
    }

    // Прижимаем окно оверлея: поверх всех, без панели задач, на весь экран.
    function setupOverlay(w) {
        if (w.managed === false) return; // X11-оверлей уже выставлен самим клиентом.
        try {
            w.keepAbove = true;
            w.skipTaskbar = true;
            w.skipPager = true;
            w.skipSwitcher = true;
            w.noBorder = true;
            w.onAllDesktops = true;
            if (OVERLAY.width > 0 && OVERLAY.height > 0)
                w.frameGeometry = { x: OVERLAY.x, y: OVERLAY.y, width: OVERLAY.width, height: OVERLAY.height };
        } catch (e) {
            print("SnowOverlay: не удалось настроить окно оверлея: " + e);
        }
    }

    function watch(w) {
        if (!w || w.onScreenDisplay || watched.indexOf(w) >= 0) return;
        watched.push(w);
        var id = str(w.internalId);
        windowIds.push(/^\{?[0-9a-fA-F-]{36}\}?$/.test(id) ? id : TOKEN + "-" + nextId++);
        if (isOverlay(w)) return;
        if (str(w.resourceClass).toLowerCase() === APP_ID && w.captionChanged) {
            w.captionChanged.connect(function () {
                if (isOverlay(w)) setupOverlay(w);
            });
        }
        function changed() {
            var active = getActive();
            if (w === active) send(active);
            else scheduleGeometry();
        }
        if (w.frameGeometryChanged) w.frameGeometryChanged.connect(changed);
        if (w.minimizedChanged) w.minimizedChanged.connect(scheduleGeometry);
        if (w.fullScreenChanged) w.fullScreenChanged.connect(scheduleGeometry);
        if (w.maximizedChanged) w.maximizedChanged.connect(scheduleGeometry);
        if (w.hiddenChanged) w.hiddenChanged.connect(scheduleGeometry);
        if (w.windowShown) w.windowShown.connect(scheduleGeometry);
        if (w.windowHidden) w.windowHidden.connect(scheduleGeometry);
        if (w.readyForPaintingChanged) w.readyForPaintingChanged.connect(scheduleGeometry);
        if (w.hiddenByShowDesktopChanged) w.hiddenByShowDesktopChanged.connect(scheduleGeometry);
        if (w.desktopsChanged) w.desktopsChanged.connect(scheduleGeometry);
        if (w.stackingOrderChanged) w.stackingOrderChanged.connect(scheduleGeometry);
    }

    function settleFocus() {
        var a = getActive();
        if (isOverlay(a) && lastActive && !lastActive.minimized && onCurrentDesktop(lastActive)) {
            setActive(lastActive);
            if (!isOverlay(getActive()))
                return;
        }
        if (!a || isOverlay(a)) {
            lastActive = null;
            send(null);
        }
    }

    function onActivated() {
        var a = getActive();
        // KWin может сначала прислать null, а затем активировать оверлей.
        // Сохраняем предыдущее окно до конца текущего переключения фокуса.
        if (!a || isOverlay(a)) {
            focusTimer.start(0);
            return;
        }
        focusTimer.stop();
        lastActive = a;
        watch(a);
        send(a);
    }

    function onAdded(w) {
        watch(w);
        if (isOverlay(w)) setupOverlay(w);
        else scheduleGeometry();
    }

    function onRemoved(w) {
        if (w === lastActive) lastActive = null;
        var i = watched.indexOf(w);
        if (i >= 0) {
            watched.splice(i, 1);
            windowIds.splice(i, 1);
            scheduleGeometry();
        }
    }

    // ---- старт ----
    var list = allWindows();
    for (var i = 0; i < list.length; ++i) {
        watch(list[i]);
        if (isOverlay(list[i])) setupOverlay(list[i]);
    }
    sigAdded.connect(onAdded);
    sigRemoved.connect(onRemoved);
    sigActivated.connect(onActivated);
    W.currentDesktopChanged.connect(function () { send(getActive()); });
    if (W.stackingOrderChanged) W.stackingOrderChanged.connect(scheduleGeometry);
    focusTimer.timeout.connect(settleFocus);
    geometryTimer.timeout.connect(function () {
        var active = getActive();
        send(isOverlay(active) ? lastActive : active);
    });
    if (TRACK_CURSOR && W.cursorPosChanged) {
        cursorTimer.timeout.connect(reportCursor);
        W.cursorPosChanged.connect(function () {
            if (!cursorTimer.active)
                cursorTimer.start(32);
        });
    }

    var a0 = getActive();
    if (!isOverlay(a0)) lastActive = a0;
    send(a0);
    print("SnowOverlay: KWin-скрипт запущен");
})();
)JS";

} // namespace

// ===========================================================================

KWinTracker::KWinTracker(QObject *parent)
    : ActiveWindowTracker(parent)
{
    m_reloadTimer.setSingleShot(true);
    connect(&m_reloadTimer, &QTimer::timeout, this, &KWinTracker::reloadScript);
}

KWinTracker::~KWinTracker()
{
    stop();
}

void KWinTracker::setOverlayGeometry(const QRect &globalRect)
{
    if (m_overlay == globalRect)
        return;
    m_overlay = globalRect;
    if (m_scriptLoaded)
        m_reloadTimer.start(0);
}

void KWinTracker::reloadScript()
{
    unloadScript();
    if (!loadScript()) {
        m_activeRect = {};
        m_activeFullscreen = false;
        m_surfaces.clear();
    }
    publishGeometry();
}

void KWinTracker::publishGeometry()
{
    emit activeRectChanged(m_showingDesktop ? QRect() : m_activeRect, !m_showingDesktop && m_activeFullscreen);
    emit surfacesChanged(visibleSurfaces());
}

QList<SnowSurface> KWinTracker::visibleSurfaces() const
{
    if (!m_showingDesktop)
        return m_surfaces;
    QList<SnowSurface> docks;
    for (const SnowSurface &surface : m_surfaces) {
        if (surface.dock || surface.panelPopup)
            docks.append(surface);
    }
    return docks;
}

void KWinTracker::showingDesktopChanged(bool showing)
{
    if (m_showingDesktop == showing)
        return;
    m_showingDesktop = showing;
    if (m_scriptLoaded)
        publishGeometry();
}

bool KWinTracker::start()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;

    // Нет KWin (GNOME, Sway, …) — этот бэкенд не подходит.
    QDBusConnectionInterface *busIface = bus.interface();
    if (!busIface || !busIface->isServiceRegistered(kKWinService).value())
        return false;

    if (!bus.registerService(kService)) {
        qWarning() << "[SnowOverlay] D-Bus имя" << kService
                   << "уже занято — возможно, SnowOverlay уже запущен.";
        return false;
    }
    if (!bus.registerObject(kPath, this, QDBusConnection::ExportScriptableSlots)) {
        qWarning() << "[SnowOverlay] Не удалось зарегистрировать D-Bus объект" << kPath;
        bus.unregisterService(kService);
        return false;
    }
    m_dbusRegistered = true;
    bus.connect(kKWinService, kKWinPath, kKWinService, QStringLiteral("showingDesktopChanged"),
                this, SLOT(showingDesktopChanged(bool)));
    const auto desktopMode = callKWin(kKWinPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                      QStringLiteral("Get"), {kKWinService, QStringLiteral("showingDesktop")});
    if (desktopMode.type() == QDBusMessage::ReplyMessage && !desktopMode.arguments().isEmpty())
        m_showingDesktop = desktopMode.arguments().constFirst().value<QDBusVariant>().variant().toBool();

    // Удаляем скрипт, который мог остаться в KWin после аварийного выхода.
    unloadScript();
    if (!loadScript()) {
        stop();
        return false;
    }
    // Фабрика подключает получателя activeRectChanged после возврата из start().
    QTimer::singleShot(0, this, [this] {
        if (m_scriptLoaded)
            publishGeometry();
    });
    return true;
}

QString KWinTracker::buildScript() const
{
    QString s = QString::fromUtf8(kScriptTemplate);
    s.replace(QStringLiteral("@SERVICE@"), kService);
    s.replace(QStringLiteral("@PATH@"), kPath);
    s.replace(QStringLiteral("@IFACE@"), kInterface);
    s.replace(QStringLiteral("@APPID@"), QString::fromLatin1(SnowIds::kAppId));
    s.replace(QStringLiteral("@TITLE@"), QString::fromLatin1(SnowIds::kWindowTitle));
    s.replace(QStringLiteral("@TOKEN@"), m_scriptToken);
    s.replace(QStringLiteral("@TRACKCURSOR@"), QGuiApplication::platformName().startsWith(QLatin1String("wayland"))
              ? QStringLiteral("true") : QStringLiteral("false"));
    s.replace(QStringLiteral("@OX@"), QString::number(m_overlay.x()));
    s.replace(QStringLiteral("@OY@"), QString::number(m_overlay.y()));
    s.replace(QStringLiteral("@OW@"), QString::number(m_overlay.width()));
    s.replace(QStringLiteral("@OH@"), QString::number(m_overlay.height()));
    return s;
}

bool KWinTracker::loadScript()
{
    // KWin принимает только путь к файлу, поэтому пишем скрипт на диск.
    QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (dir.isEmpty())
        dir = QDir::tempPath();
    QTemporaryFile file(QDir(dir).filePath(QStringLiteral("snowoverlay-kwin-tracker-XXXXXX.js")));
    const QRect scriptGeometry = m_overlay;
    m_scriptToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_receivedInitialGeometry = false;
    const QByteArray script = buildScript().toUtf8();
    if (!file.open() || file.write(script) != script.size() || !file.flush()) {
        qWarning() << "[SnowOverlay] Не могу записать KWin-скрипт:" << file.errorString();
        return false;
    }
    file.close();

    const QDBusMessage reply = callKWin(kScriptingPath, kScriptingIface,
                                        QStringLiteral("loadScript"), {file.fileName(), kPluginName});
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        qWarning() << "[SnowOverlay] KWin не принял скрипт:" << reply.errorMessage();
        return false;
    }
    const int id = reply.arguments().constFirst().toInt();
    if (id < 0) {
        qWarning() << "[SnowOverlay] KWin отказался загружать скрипт (id =" << id << ")";
        return false;
    }

    // Путь D-Bus объекта скрипта зависит от версии KWin: пробуем оба известных варианта.
    bool started = false;
    const QStringList candidates = {
        QStringLiteral("/Scripting/Script%1").arg(id),
        QStringLiteral("/%1").arg(id),
    };
    for (const QString &objPath : candidates) {
        const QDBusMessage r = callKWin(objPath, kScriptIface, QStringLiteral("run"));
        if (r.type() == QDBusMessage::ReplyMessage) {
            started = true;
            break;
        }
    }
    if (!started) {
        qWarning() << "[SnowOverlay] Не удалось запустить скрипт в KWin (id =" << id << ")";
        unloadScript();
        return false;
    }

    // run() может ответить успехом даже после ошибки выполнения JavaScript.
    // Скрипт подтверждает запуск первой геометрией после подключения сигналов.
    if (!m_receivedInitialGeometry) {
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        connect(this, &KWinTracker::initialGeometryReceived, &loop, &QEventLoop::quit);
        timeout.start(1000);
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    }
    if (!m_receivedInitialGeometry) {
        qWarning() << "[SnowOverlay] Скрипт KWin не подтвердил запуск.";
        unloadScript();
        return false;
    }

    m_scriptLoaded = true;
    if (m_overlay != scriptGeometry)
        m_reloadTimer.start(0);
    return true;
}

void KWinTracker::unloadScript()
{
    m_scriptLoaded = false;
    m_scriptToken.clear();
    callKWin(kScriptingPath, kScriptingIface, QStringLiteral("unloadScript"), {kPluginName});
}

void KWinTracker::stop()
{
    m_reloadTimer.stop();
    if (m_scriptLoaded)
        unloadScript();
    if (m_dbusRegistered) {
        QDBusConnection bus = QDBusConnection::sessionBus();
        bus.disconnect(kKWinService, kKWinPath, kKWinService, QStringLiteral("showingDesktopChanged"),
                       this, SLOT(showingDesktopChanged(bool)));
        bus.unregisterObject(kPath);
        bus.unregisterService(kService);
        m_dbusRegistered = false;
    }
}

static QRect parseRect(const QString &message)
{
    const QStringList parts = message.split(QLatin1Char(','));
    if (parts.size() != 5 || parts.at(0) != QLatin1String("1"))
        return {};
    bool xOk, yOk, widthOk, heightOk;
    const int x = parts.at(1).toInt(&xOk);
    const int y = parts.at(2).toInt(&yOk);
    const int width = parts.at(3).toInt(&widthOk);
    const int height = parts.at(4).toInt(&heightOk);
    if (!xOk || !yOk || !widthOk || !heightOk || width <= 0 || height <= 0)
        return {};
    return QRect(x, y, width, height);
}

void KWinTracker::geometryMessage(const QString &scriptToken, const QString &message)
{
    if (m_scriptToken.isEmpty() || scriptToken != m_scriptToken)
        return;
    const QStringList parts = message.split(QLatin1Char(';'));
    const QString &active = parts.first();
    const bool fullscreen = active.startsWith(QLatin1String("F:"));
    const QRect activeRect = parseRect(fullscreen ? active.mid(2) : active);
    if (activeRect.isEmpty() && active != QLatin1String("0"))
        return;
    QList<SnowSurface> surfaces;
    for (qsizetype i = 1; i < parts.size(); ++i) {
        const QString &entry = parts.at(i);
        const qsizetype separator = entry.indexOf(QLatin1Char(':'));
        if (separator <= 1)
            continue;
        const QChar type = entry.front();
        if (type != QLatin1Char('D') && type != QLatin1Char('F') && type != QLatin1Char('W') && type != QLatin1Char('P') &&
            type != QLatin1Char('M'))
            continue;
        const QRect rect = parseRect(entry.mid(separator + 1));
        if (!rect.isEmpty())
            surfaces.append({entry.mid(1, separator - 1), rect, type == QLatin1Char('D'),
                             type == QLatin1Char('F'), type == QLatin1Char('M'), type == QLatin1Char('P')});
    }
    const bool activeChanged = m_activeRect != activeRect || m_activeFullscreen != fullscreen;
    const bool surfacesUpdated = m_surfaces != surfaces;
    m_activeRect = activeRect;
    m_activeFullscreen = fullscreen;
    m_surfaces = surfaces;
    if (!m_receivedInitialGeometry) {
        m_receivedInitialGeometry = true;
        emit initialGeometryReceived();
    }
    if (m_scriptLoaded && activeChanged && !m_showingDesktop)
        emit activeRectChanged(m_activeRect, m_activeFullscreen);
    if (m_scriptLoaded && surfacesUpdated)
        emit surfacesChanged(visibleSurfaces());
}

bool KWinTracker::cursorMessage(const QString &scriptToken, const QString &message)
{
    if (!m_scriptLoaded || scriptToken != m_scriptToken)
        return false;
    const qsizetype comma = message.indexOf(QLatin1Char(','));
    if (comma < 0)
        return false;
    bool xOk = false, yOk = false;
    const int x = QStringView(message).left(comma).toInt(&xOk);
    const int y = QStringView(message).mid(comma + 1).toInt(&yOk);
    if (xOk && yOk)
        emit cursorPosChanged(QPoint(x, y));
    return m_seasonalCursorTracking;
}
