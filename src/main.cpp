#include "ActiveWindowTracker.h"
#include "Constants.h"
#include "SnowWindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QMenu>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSlider>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTimer>

#include <algorithm>

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

#ifdef SNOW_HAS_LAYER_SHELL
int runPanelGarland(QApplication &app);
#endif

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
#ifdef SNOW_HAS_LAYER_SHELL
    if (app.arguments().value(1) == QLatin1String("--panel-garland"))
        return runPanelGarland(app);
#endif
    QApplication::setApplicationName(QString::fromLatin1(SnowIds::kAppId));
    QApplication::setApplicationDisplayName(QStringLiteral("SnowOverlay"));
    // app_id окна в Wayland берётся из desktopFileName.
    QGuiApplication::setDesktopFileName(QString::fromLatin1(SnowIds::kAppId));
    const QIcon applicationIcon(QStringLiteral(":/assets/snowoverlay.svg"));
    app.setWindowIcon(applicationIcon);
    // Завершение через трей или команду --quit.
    QApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QObject::tr("Сезонный оверлей для рабочего стола"));
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
    QSettings settings(QStringLiteral("FourSeasons"), app.applicationName());
    const struct {
        Season season;
        const char *key;
        QString label;
    } seasons[] = {
        {Season::Winter, "winter", QObject::tr("Зима")},
        {Season::Spring, "spring", QObject::tr("Весна")},
        {Season::Summer, "summer", QObject::tr("Лето")},
        {Season::Autumn, "autumn", QObject::tr("Осень")},
    };
    const QString savedSeason = settings.value(QStringLiteral("season")).toString();
    window.setAutumnOptions({settings.value(QStringLiteral("autumnRain"), false).toBool(),
                             settings.value(QStringLiteral("autumnLeaves"), true).toBool()});
    window.setSpringRain(settings.value(QStringLiteral("springRain"), false).toBool());
    window.setBubbles(settings.value(QStringLiteral("summerBubbles"), false).toBool());
    for (const auto &entry : seasons) {
        if (savedSeason == QLatin1String(entry.key)) {
            window.setSeason(entry.season);
            break;
        }
    }
    const bool garlandsEnabled = settings.value(QStringLiteral("garlandsEnabled"), true).toBool();
    bool oldIntensityValid = false;
    const int oldIntensity = settings.value(QStringLiteral("snowIntensity"), 2).toInt(&oldIntensityValid);
    const int oldPresets[] = {0, 44, 100, 167};
    const int defaultIntensity = oldIntensityValid && oldIntensity >= 0 && oldIntensity <= 3
        ? oldPresets[oldIntensity] : 100;
    const auto readSliderValue = [&settings](const QString &key, int fallback) {
        bool valid = false;
        const int percent = settings.value(key, fallback).toInt(&valid);
        return valid ? percent : fallback;
    };
    using Precipitation::Settings;
    QString previousPrefix = QStringLiteral("snow");
    if (window.season() == Season::Spring)
        previousPrefix = QStringLiteral("petal");
    else if (window.season() == Season::Autumn)
        previousPrefix = window.autumnOptions().leaves ? QStringLiteral("leaf") : QStringLiteral("rain");

    const QString prefix = QStringLiteral("precipitation");
    const auto loadSetting = [&](const QString &suffix, int fallback, int minimum, int maximum) {
        return qBound(minimum, readSliderValue(prefix + suffix,
                      readSliderValue(previousPrefix + suffix, readSliderValue(QStringLiteral("snow") + suffix, fallback))),
                      maximum);
    };
    Settings precipitation{
        loadSetting(QStringLiteral("IntensityPercent"), defaultIntensity, 0, SnowSettings::maxIntensity),
        loadSetting(QStringLiteral("SpeedPercent"), 100, SnowSettings::minSpeed, SnowSettings::maxSpeed),
        loadSetting(QStringLiteral("WindPercent"), 0, -SnowSettings::maxWind, SnowSettings::maxWind),
    };
    const auto applyPrecipitation = [&] {
        window.setPrecipitation(precipitation);
        settings.setValue(prefix + QStringLiteral("IntensityPercent"), precipitation.intensity);
        settings.setValue(prefix + QStringLiteral("SpeedPercent"), precipitation.speed);
        settings.setValue(prefix + QStringLiteral("WindPercent"), precipitation.wind);
    };
    applyPrecipitation();
    window.setGarlandsEnabled(garlandsEnabled);

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
        QObject::connect(&window, &SnowWindow::seasonalCursorTrackingChanged,
                         tracker, &ActiveWindowTracker::setSeasonalCursorTracking);
        tracker->setSeasonalCursorTracking(window.season() != Season::Winter);
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
    QMenu *seasonMenu = trayMenu.addMenu(QObject::tr("Время года"));
    QActionGroup seasonGroup(seasonMenu);
    for (const auto &entry : seasons) {
        QAction *action = seasonMenu->addAction(entry.label);
        seasonGroup.addAction(action);
        action->setCheckable(true);
        action->setChecked(window.season() == entry.season);
        action->setData(QString::fromLatin1(entry.key));
    }
    QMenu *autumnMenu = trayMenu.addMenu(QObject::tr("Эффекты осени"));
    QAction *rainAction = autumnMenu->addAction(QObject::tr("Дождь"));
    QAction *leavesAction = autumnMenu->addAction(QObject::tr("Листопад"));
    rainAction->setCheckable(true);
    leavesAction->setCheckable(true);
    const auto syncAutumnActions = [&] {
        const AutumnOptions options = window.autumnOptions();
        rainAction->setChecked(options.rain);
        rainAction->setEnabled(!options.rain || options.leaves);
        leavesAction->setChecked(options.leaves);
        leavesAction->setEnabled(!options.leaves || options.rain);
    };
    const auto changeAutumnOptions = [&] {
        window.setAutumnOptions({rainAction->isChecked(), leavesAction->isChecked()});
        syncAutumnActions();
        const AutumnOptions options = window.autumnOptions();
        settings.setValue(QStringLiteral("autumnRain"), options.rain);
        settings.setValue(QStringLiteral("autumnLeaves"), options.leaves);
    };
    syncAutumnActions();
    QObject::connect(rainAction, &QAction::triggered, autumnMenu, changeAutumnOptions);
    QObject::connect(leavesAction, &QAction::triggered, autumnMenu, changeAutumnOptions);

    QMenu *springMenu = trayMenu.addMenu(QObject::tr("Весенний сад"));
    QMenu *summerMenu = trayMenu.addMenu(QObject::tr("Летние эффекты"));
    const auto addTool = [&window](QMenu *menu, const QString &label, SeasonTool tool) {
        QAction *action = menu->addAction(label);
        QObject::connect(action, &QAction::triggered, &window, [&window, tool] { window.setSeasonTool(tool); });
    };
    const auto addToggle = [&window, &settings](QMenu *menu, const QString &label, const QString &key,
                                                void (SnowWindow::*apply)(bool)) {
        QAction *action = menu->addAction(label);
        action->setCheckable(true);
        action->setChecked(!key.isEmpty() && settings.value(key, false).toBool());
        QObject::connect(action, &QAction::toggled, &window, [&window, &settings, key, apply](bool enabled) {
            (window.*apply)(enabled);
            if (!key.isEmpty())
                settings.setValue(key, enabled);
        });
        return action;
    };
    autumnMenu->addSeparator();
    addTool(autumnMenu, QObject::tr("Мести листья"), SeasonTool::Broom);
    addTool(autumnMenu, QObject::tr("Сгрести листья"), SeasonTool::Rake);
    QAction *blower = autumnMenu->addAction(QObject::tr("Вызвать воздуходувку"));
    QObject::connect(blower, &QAction::triggered, &window, &SnowWindow::startLeafBlower);
    addTool(autumnMenu, QObject::tr("Приготовить жёлудь"), SeasonTool::Acorn);
    QAction *autumnBoat = autumnMenu->addAction(QObject::tr("Пустить бумажный кораблик"));
    addTool(autumnMenu, QObject::tr("Убрать инструмент"), SeasonTool::Passive);

    addToggle(springMenu, QObject::tr("Весенний дождь"), QStringLiteral("springRain"), &SnowWindow::setSpringRain);
    addTool(springMenu, QObject::tr("Посадить семена"), SeasonTool::Plant);
    addTool(springMenu, QObject::tr("Полить цветы"), SeasonTool::Water);
    QAction *springBoat = springMenu->addAction(QObject::tr("Пустить бумажный кораблик"));
    addTool(springMenu, QObject::tr("Убрать инструмент"), SeasonTool::Passive);

    addToggle(summerMenu, QObject::tr("Мыльные пузыри"), QStringLiteral("summerBubbles"), &SnowWindow::setBubbles);
    QAction *fan = addToggle(summerMenu, QObject::tr("Вентилятор"), {}, &SnowWindow::setSeasonFan);
    QAction *jar = addToggle(summerMenu, QObject::tr("Банка для светлячков"), {}, &SnowWindow::setFireflyJar);
    addTool(summerMenu, QObject::tr("Полить цветы"), SeasonTool::Water);
    addTool(summerMenu, QObject::tr("Убрать инструмент"), SeasonTool::Passive);

    QMenu *settingsMenu = trayMenu.addMenu(QObject::tr("Настройки"));
    QAction *garlandsAction = settingsMenu->addAction(QObject::tr("Гирлянды"));
    garlandsAction->setCheckable(true);
    garlandsAction->setChecked(garlandsEnabled);
    QObject::connect(garlandsAction, &QAction::toggled, &window, [&window, &settings](bool enabled) {
        window.setGarlandsEnabled(enabled);
        settings.setValue(QStringLiteral("garlandsEnabled"), enabled);
    });

    QDialog precipitationDialog;
    precipitationDialog.setWindowTitle(QObject::tr("Настройки осадков"));
    precipitationDialog.setWindowIcon(applicationIcon);
    precipitationDialog.setMinimumWidth(420);
    QFormLayout form(&precipitationDialog);
    const auto addSlider = [&](const QString &title, int minimum, int maximum, int Settings::*member) {
        auto *row = new QHBoxLayout;
        auto *slider = new QSlider(Qt::Horizontal);
        slider->setAccessibleName(title);
        slider->setRange(minimum, maximum);
        slider->setValue(precipitation.*member);
        const auto format = [minimum](int value) {
            if (minimum >= 0)
                return QObject::tr("%1%").arg(value);
            if (value == 0)
                return QObject::tr("Штиль");
            return value < 0 ? QObject::tr("← %1%").arg(-value) : QObject::tr("→ %1%").arg(value);
        };
        auto *label = new QLabel(format(slider->value()));
        label->setMinimumWidth(std::max({label->fontMetrics().horizontalAdvance(format(minimum)),
                                         label->fontMetrics().horizontalAdvance(format(0)),
                                         label->fontMetrics().horizontalAdvance(format(maximum))}));
        row->addWidget(slider, 1);
        row->addWidget(label);
        form.addRow(title, row);
        QObject::connect(slider, &QSlider::valueChanged, &precipitationDialog,
                         [&precipitation, &applyPrecipitation, label, member, format](int value) {
            precipitation.*member = value;
            applyPrecipitation();
            label->setText(format(value));
        });
    };
    addSlider(QObject::tr("Интенсивность"), 0, SnowSettings::maxIntensity, &Settings::intensity);
    addSlider(QObject::tr("Скорость падения"), SnowSettings::minSpeed, SnowSettings::maxSpeed, &Settings::speed);
    addSlider(QObject::tr("Ветер"), -SnowSettings::maxWind, SnowSettings::maxWind, &Settings::wind);
    QDialogButtonBox buttons(QDialogButtonBox::Close);
    buttons.button(QDialogButtonBox::Close)->setText(QObject::tr("Закрыть"));
    form.addRow(&buttons);
    QObject::connect(&buttons, &QDialogButtonBox::rejected, &precipitationDialog, &QDialog::close);
    QAction *precipitationAction = settingsMenu->addAction(QObject::tr("Настройки осадков…"));
    QObject::connect(precipitationAction, &QAction::triggered, &precipitationDialog, [&] {
        precipitationDialog.show();
        precipitationDialog.raise();
        precipitationDialog.activateWindow();
    });

    QAction *plowAction = trayMenu.addAction(QObject::tr("Вызвать снегоуборщик"));
    QObject::connect(plowAction, &QAction::triggered, &window, &SnowWindow::startPlow);
    QSystemTrayIcon tray;
    const auto syncSeasonActions = [&] {
        const bool winter = window.season() == Season::Winter;
        garlandsAction->setVisible(winter);
        plowAction->setVisible(winter);
        autumnMenu->menuAction()->setVisible(window.season() == Season::Autumn);
        springMenu->menuAction()->setVisible(window.season() == Season::Spring);
        const bool summer = window.season() == Season::Summer;
        settingsMenu->menuAction()->setVisible(!summer);
        summerMenu->menuAction()->setVisible(summer);
        if (!summer) {
            fan->setChecked(false);
            jar->setChecked(false);
        }
        if (summer)
            precipitationDialog.hide();
        for (const auto &entry : seasons) {
            if (entry.season == window.season()) {
                tray.setIcon(QIcon(QStringLiteral(":/assets/%1.svg").arg(QString::fromLatin1(entry.key))));
                break;
            }
        }
    };
    syncSeasonActions();
    QObject::connect(&seasonGroup, &QActionGroup::triggered, &trayMenu, [&](QAction *action) {
        const QString key = action->data().toString();
        for (const auto &entry : seasons) {
            if (key == QLatin1String(entry.key)) {
                window.setSeason(entry.season);
                settings.setValue(QStringLiteral("season"), key);
                syncSeasonActions();
                break;
            }
        }
    });
    trayMenu.addSeparator();
    QAction *quitAction = trayMenu.addAction(QObject::tr("Выход"));
    QObject::connect(quitAction, &QAction::triggered, &app, &QApplication::quit);

    tray.setToolTip(QStringLiteral("SnowOverlay"));
    tray.setContextMenu(&trayMenu);
    tray.show();
    for (QAction *boat : {springBoat, autumnBoat}) {
        QObject::connect(boat, &QAction::triggered, &tray, [&] {
            if (!window.launchPaperBoat())
                tray.showMessage(QObject::tr("Бумажный кораблик"),
                                 QObject::tr("Подожди, пока дождь наполнит лужи."));
        });
    }

    return app.exec();
}
