#include "ActiveWindowTracker.h"
#include "Constants.h"
#include "Season.h"
#include "SnowWindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QMenu>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QScreen>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTimer>

#ifdef SNOW_HAS_XLIB
#include <X11/Xlib.h>
#endif

static bool x11OverlaySupported()
{
#if defined(SNOW_HAS_XLIB) && QT_CONFIG(xcb)
    auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    Display *display = x11 ? x11->display() : nullptr;
    if (!display)
        return false;

    const QByteArray selection = "_NET_WM_CM_S" + QByteArray::number(DefaultScreen(display));
    if (XGetSelectionOwner(display, XInternAtom(display, selection.constData(), False)) != None)
        return true;

    // XWayland композитится через Wayland. Старые версии KWin не владеют
    // selection, а XWayland 22.x распознаётся по имени выхода RandR.
    int opcode, event, error;
    if (XQueryExtension(display, "XWAYLAND", &opcode, &event, &error))
        return true;
    if (const QScreen *screen = qGuiApp->primaryScreen()) {
        const QString name = screen->name();
        const QString prefix = QStringLiteral("XWAYLAND");
        if (name.startsWith(prefix)) {
            bool numbered;
            name.mid(prefix.size()).toUInt(&numbered);
            return numbered;
        }
    }
#endif
    return false;
}

// Иконка в трее рисуется кодом: белая снежинка на синем круге (внешние файлы не нужны).
static QIcon makeTrayIcon()
{
    QPixmap pm(64, 64);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(40, 110, 200));
    p.drawEllipse(2, 2, 60, 60);

    p.setPen(QPen(Qt::white, 4, Qt::SolidLine, Qt::RoundCap));
    p.translate(32, 32);
    for (int i = 0; i < 3; ++i) {      // три линии через центр = шесть лучей
        p.drawLine(0, -22, 0, 22);
        p.rotate(60);
    }
    p.end();

    return QIcon(pm);
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QString::fromLatin1(SnowIds::kAppId));
    QApplication::setApplicationDisplayName(QStringLiteral("SnowOverlay"));
    // app_id окна в Wayland берётся из desktopFileName — по нему KWin-скрипт найдёт оверлей.
    QGuiApplication::setDesktopFileName(QString::fromLatin1(SnowIds::kAppId));
    // Завершение через трей или команду --quit.
    QApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QObject::tr("Снежный оверлей для рабочего стола"));
    parser.addHelpOption();
    const QCommandLineOption quitOption(QStringLiteral("quit"), QObject::tr("Завершить запущенный SnowOverlay."));
    parser.addOption(quitOption);
    parser.process(app);

    const QString runtimeDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (runtimeDir.isEmpty() || !QDir().mkpath(runtimeDir)) {
        qCritical() << "[SnowOverlay] Не удалось подготовить каталог для управления приложением.";
        return 1;
    }
    const QString instancePath = QDir(runtimeDir).filePath(QStringLiteral("snowoverlay"));
    const QString socketPath = instancePath + QStringLiteral(".sock");
    if (parser.isSet(quitOption)) {
        QLocalSocket socket;
        socket.connectToServer(socketPath);
        if (!socket.waitForConnected(1000)) {
            qWarning().noquote() << "[SnowOverlay] Не удалось завершить запущенный экземпляр:" << socket.errorString();
            return 1;
        }
        if (socket.state() == QLocalSocket::ConnectedState && !socket.waitForDisconnected(3000)) {
            qWarning() << "[SnowOverlay] Запущенный экземпляр ещё не завершился.";
            return 1;
        }
        return 0;
    }

    QLockFile instanceLock(instancePath + QStringLiteral(".lock"));
    instanceLock.setStaleLockTime(0);
    if (!instanceLock.tryLock(0)) {
        if (instanceLock.error() == QLockFile::LockFailedError) {
            qInfo() << "[SnowOverlay] Уже запущен. Для выхода: SnowOverlay --quit";
            return 0;
        }
        qCritical() << "[SnowOverlay] Не удалось заблокировать запуск второго экземпляра.";
        return 1;
    }

    const QString platform = QGuiApplication::platformName();
    const bool xcb = platform == QLatin1String("xcb");
    qInfo().noquote() << "[SnowOverlay] Платформа Qt:" << platform;
    if (xcb && !x11OverlaySupported()) {
        qCritical() << "[SnowOverlay] Для прозрачного оверлея на X11 требуется активный композитор.";
        return 1;
    }

    // --- Окно со снегом ---------------------------------------------------
    SnowWindow window;

    // --- Определение активного окна (может быть nullptr — это нормально) ----
    ActiveWindowTracker *tracker = ActiveWindowTracker::create(&app, window.screenRect());
    if (tracker) {
        QObject::connect(tracker, &ActiveWindowTracker::activeRectChanged,
                         &window, &SnowWindow::setActiveRect);
        QObject::connect(tracker, &ActiveWindowTracker::surfacesChanged,
                         &window, &SnowWindow::setSurfaces);
        QObject::connect(tracker, &ActiveWindowTracker::cursorPosChanged,
                         &window, &SnowWindow::setGlobalCursorPos);
        QObject::connect(&window, &SnowWindow::overlayGeometryChanged,
                         tracker, &ActiveWindowTracker::setOverlayGeometry);
        // Корректно выгружаем KWin-скрипт при выходе.
        QObject::connect(&app, &QCoreApplication::aboutToQuit, tracker, &ActiveWindowTracker::stop);
    } else if (platform.startsWith(QLatin1String("wayland"))) {
        qCritical() << "[SnowOverlay] Не удалось запустить трекер KWin для Wayland."
                    << "Если доступен XWayland, попробуйте QT_QPA_PLATFORM=xcb.";
        return 1;
    }

    QLocalServer quitServer;
    QLocalServer::removeServer(socketPath); // Блокировка гарантирует, что сокет остался от предыдущего запуска.
    quitServer.setSocketOptions(QLocalServer::UserAccessOption);
    if (!quitServer.listen(socketPath)) {
        qCritical().noquote() << "[SnowOverlay] Не удалось открыть сокет для команды --quit:" << quitServer.errorString();
        return 1;
    }
    QObject::connect(&quitServer, &QLocalServer::newConnection, &app, &QApplication::quit);

    window.show();
    QTimer compositorCheck;
    if (xcb) {
        QObject::connect(&compositorCheck, &QTimer::timeout, &window, [&window] {
            if (!x11OverlaySupported()) {
                window.hide();
                qWarning() << "[SnowOverlay] Композитор X11 остановлен.";
                QCoreApplication::quit();
            }
        });
        compositorCheck.start(500);
    }

    // --- Управление оверлеем через системный трей ---------------------------
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        qInfo() << "[SnowOverlay] Системный трей недоступен. Для выхода: SnowOverlay --quit";

    QMenu trayMenu;

    // Сезон: два взаимоисключающих пункта.
    QActionGroup seasonGroup(&trayMenu);
    seasonGroup.setExclusive(true);
    QAction *winterAction = trayMenu.addAction(QObject::tr("Зима"));
    QAction *autumnAction = trayMenu.addAction(QObject::tr("Осень"));
    for (QAction *action : {winterAction, autumnAction}) {
        action->setCheckable(true);
        seasonGroup.addAction(action);
    }
    trayMenu.addSeparator();

    // Осень: дождь и листопад включаются независимо (можно и то и другое).
    QAction *rainAction = trayMenu.addAction(QObject::tr("Дождь"));
    QAction *leavesAction = trayMenu.addAction(QObject::tr("Листопад"));
    for (QAction *action : {rainAction, leavesAction})
        action->setCheckable(true);
    const AutumnOptions autumnOptions = window.autumnOptions();
    rainAction->setChecked(autumnOptions.rain);
    leavesAction->setChecked(autumnOptions.leaves);

    // Зима: снегоуборщик.
    QAction *plowAction = trayMenu.addAction(QObject::tr("Вызвать снегоуборщик"));
    QObject::connect(plowAction, &QAction::triggered, &window, &SnowWindow::startPlow);

    trayMenu.addSeparator();
    QAction *quitAction = trayMenu.addAction(QObject::tr("Выход"));
    QObject::connect(quitAction, &QAction::triggered, &app, &QApplication::quit);

    // Показывает только пункты текущего сезона. Последнюю включённую осадку снять нельзя:
    // осень без дождя и листьев была бы просто пустым экраном.
    const auto syncMenu = [&] {
        const bool autumn = window.season() == Season::Autumn;
        winterAction->setChecked(!autumn);
        autumnAction->setChecked(autumn);
        rainAction->setVisible(autumn);
        leavesAction->setVisible(autumn);
        plowAction->setVisible(!autumn);
        rainAction->setEnabled(!rainAction->isChecked() || leavesAction->isChecked());
        leavesAction->setEnabled(!leavesAction->isChecked() || rainAction->isChecked());
    };
    QObject::connect(&seasonGroup, &QActionGroup::triggered, &window, [&](QAction *action) {
        window.setSeason(action == autumnAction ? Season::Autumn : Season::Winter);
        syncMenu();
    });
    const auto applyAutumnOptions = [&] {
        window.setAutumnOptions({rainAction->isChecked(), leavesAction->isChecked()});
        syncMenu();
    };
    QObject::connect(rainAction, &QAction::toggled, &window, applyAutumnOptions);
    QObject::connect(leavesAction, &QAction::toggled, &window, applyAutumnOptions);
    syncMenu();

    QSystemTrayIcon tray(makeTrayIcon());
    tray.setToolTip(QStringLiteral("SnowOverlay"));
    tray.setContextMenu(&trayMenu);
    tray.show();

    return app.exec();
}
