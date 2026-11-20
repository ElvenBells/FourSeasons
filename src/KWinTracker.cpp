#include "KWinTracker.h"
#include "Constants.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
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

    // Окно оверлея опознаём по app_id/WM_CLASS ИЛИ по уникальному заголовку.
    function isOverlay(w) {
        if (!w) return false;
        return str(w.resourceClass).toLowerCase() === APP_ID || str(w.caption).indexOf(TITLE) === 0;
    }

    // Окна, вокруг которых «дырку» в снегу делать не нужно.
    function isIgnored(w) {
        return !w || isOverlay(w) || w.desktopWindow || w.dock || w.specialWindow || w.minimized;
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
            if (index < 0 || isOverlay(client) || client.minimized || client.hidden || client.deleted)
                continue;
            if (!client.dock && (client.desktopWindow || client.specialWindow || !onCurrentDesktop(client)))
                continue;
            var rect = geometry(client);
            if (rect !== "0") {
                parts.push((client.dock ? "D" : "W") + windowIds[index] + ":" + rect);
                if (TRACK_CURSOR && !client.dock) {
                    var frame = client.frameGeometry || client.geometry;
                    if (frame.y >= OVERLAY.y + 3 && frame.y < OVERLAY.y + OVERLAY.height)
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
        var near = false;
        for (var i = 0; i < cursorAreas.length; ++i) {
            var r = cursorAreas[i];
            if (p.x >= r.x && p.x < r.x + r.width && p.y >= r.y - 32 && p.y <= r.y + 2) {
                near = true;
                break;
            }
        }
        if (near || cursorNearSnow)
            callDBus(SERVICE, PATH, IFACE, "cursorMessage", TOKEN, Math.round(p.x) + "," + Math.round(p.y));
        cursorNearSnow = near;
    }

    // Прижимаем окно оверлея: поверх всех, без панели задач, на весь экран.
    function setupOverlay(w) {
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
        if (!w || watched.indexOf(w) >= 0) return;
        watched.push(w);
        var id = str(w.internalId);
        windowIds.push(/^\{?[0-9a-fA-F-]{36}\}?$/.test(id) ? id : TOKEN + "-" + nextId++);
        if (isOverlay(w)) return;
        function changed() {
            var active = getActive();
            if (w === active) send(active);
            else scheduleGeometry();
        }
        if (w.frameGeometryChanged) w.frameGeometryChanged.connect(changed);
        if (w.minimizedChanged) w.minimizedChanged.connect(changed);
        if (w.hiddenChanged) w.hiddenChanged.connect(scheduleGeometry);
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
        m_surfaces.clear();
    }
    publishGeometry();
}

void KWinTracker::publishGeometry()
{
    emit activeRectChanged(m_activeRect);
    emit surfacesChanged(m_surfaces);
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
    const QRect activeRect = parseRect(parts.first());
    if (activeRect.isEmpty() && parts.first() != QLatin1String("0"))
        return;
    QList<SnowSurface> surfaces;
    for (qsizetype i = 1; i < parts.size(); ++i) {
        const QString &entry = parts.at(i);
        const qsizetype separator = entry.indexOf(QLatin1Char(':'));
        if (separator <= 1 || (entry.front() != QLatin1Char('D') && entry.front() != QLatin1Char('W')))
            continue;
        const QRect rect = parseRect(entry.mid(separator + 1));
        if (!rect.isEmpty())
            surfaces.append({entry.mid(1, separator - 1), rect, entry.front() == QLatin1Char('D')});
    }
    const bool activeChanged = m_activeRect != activeRect;
    const bool surfacesUpdated = m_surfaces != surfaces;
    m_activeRect = activeRect;
    m_surfaces = surfaces;
    if (!m_receivedInitialGeometry) {
        m_receivedInitialGeometry = true;
        emit initialGeometryReceived();
    }
    if (m_scriptLoaded && activeChanged)
        emit activeRectChanged(m_activeRect);
    if (m_scriptLoaded && surfacesUpdated)
        emit surfacesChanged(m_surfaces);
}

void KWinTracker::cursorMessage(const QString &scriptToken, const QString &message)
{
    if (!m_scriptLoaded || scriptToken != m_scriptToken)
        return;
    const qsizetype comma = message.indexOf(QLatin1Char(','));
    if (comma < 0)
        return;
    bool xOk = false, yOk = false;
    const int x = QStringView(message).left(comma).toInt(&xOk);
    const int y = QStringView(message).mid(comma + 1).toInt(&yOk);
    if (xOk && yOk)
        emit cursorPosChanged(QPoint(x, y));
}
