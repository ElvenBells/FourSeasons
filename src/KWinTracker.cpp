#include "KWinTracker.h"
#include "Constants.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QStringList>

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
    var OVERLAY = { x: @OX@, y: @OY@, width: @OW@, height: @OH@ };

    var W = workspace;
    var watched = [];        // окна, на сигналы которых мы уже подписаны
    var lastActive = null;   // последнее активное окно, не являющееся оверлеем
    var lastMsg = "";
    var guard = false;

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
        for (var i = 0; i < c.desktops.length; ++i)                                         // Plasma 6
            if (c.desktops[i] === W.currentDesktop) return true;
        return false;
    }

    function send(w) {
        var msg = "0";
        if (!isIgnored(w)) {
            var g = w.frameGeometry || w.geometry;   // рамка и заголовок включены
            if (g && g.width > 0 && g.height > 0)
                msg = "1," + Math.round(g.x) + "," + Math.round(g.y) + ","
                    + Math.round(g.width) + "," + Math.round(g.height);
        }
        if (msg === lastMsg) return;
        lastMsg = msg;
        callDBus(SERVICE, PATH, IFACE, "geometryMessage", msg);
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
        // Геометрию шлём только для активного окна: при перетаскивании «дырка» едет следом.
        if (w.frameGeometryChanged)
            w.frameGeometryChanged.connect(function () { if (w === getActive()) send(w); });
        if (w.minimizedChanged)
            w.minimizedChanged.connect(function () { if (w === getActive()) send(w); });
    }

    function onActivated() {
        var a = getActive();
        if (isOverlay(a)) {
            // Оверлей не должен владеть фокусом: возвращаем его прежнему окну
            // (только если оно на текущем рабочем столе — иначе KWin переключит стол).
            if (!guard && lastActive && !lastActive.minimized && onCurrentDesktop(lastActive)) {
                guard = true;
                try { setActive(lastActive); } catch (e) {}
                guard = false;
            }
            return;
        }
        lastActive = a;
        watch(a);
        send(a);
    }

    function onAdded(w) {
        watch(w);
        if (isOverlay(w)) setupOverlay(w);
    }

    function onRemoved(w) {
        if (w === lastActive) lastActive = null;
        var i = watched.indexOf(w);
        if (i >= 0) watched.splice(i, 1);
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
    // Экран изменился уже после запуска — перезагружаем скрипт с новой геометрией.
    if (m_scriptLoaded) {
        unloadScript();
        loadScript();
    }
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

    if (!loadScript()) {
        stop();
        return false;
    }
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
    m_scriptPath = dir + QStringLiteral("/snowoverlay-kwin-tracker.js");

    QFile file(m_scriptPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "[SnowOverlay] Не могу записать KWin-скрипт:" << m_scriptPath;
        return false;
    }
    file.write(buildScript().toUtf8());
    file.close();

    // Остался скрипт от прошлого запуска (например, после падения)? Выгружаем.
    callKWin(kScriptingPath, kScriptingIface, QStringLiteral("unloadScript"), {kPluginName});

    const QDBusMessage reply = callKWin(kScriptingPath, kScriptingIface,
                                        QStringLiteral("loadScript"), {m_scriptPath, kPluginName});
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

    m_scriptLoaded = true;
    return true;
}

void KWinTracker::unloadScript()
{
    callKWin(kScriptingPath, kScriptingIface, QStringLiteral("unloadScript"), {kPluginName});
    m_scriptLoaded = false;
}

void KWinTracker::stop()
{
    if (m_scriptLoaded)
        unloadScript();
    if (!m_scriptPath.isEmpty()) {
        QFile::remove(m_scriptPath);
        m_scriptPath.clear();
    }
    if (m_dbusRegistered) {
        QDBusConnection bus = QDBusConnection::sessionBus();
        bus.unregisterObject(kPath);
        bus.unregisterService(kService);
        m_dbusRegistered = false;
    }
}

void KWinTracker::geometryMessage(const QString &message)
{
    const QStringList p = message.split(QLatin1Char(','));
    if (p.size() == 5 && p.at(0) == QLatin1String("1")) {
        emit activeRectChanged(QRect(p.at(1).toInt(), p.at(2).toInt(),
                                     p.at(3).toInt(), p.at(4).toInt()));
    } else {
        emit activeRectChanged(QRect()); // активного окна нет → снег по всему экрану
    }
}
