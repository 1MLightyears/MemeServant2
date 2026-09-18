// 托盘是常驻生命周期入口；配置窗口X只隐藏，退出必须显式选择。
#include "app/application.h"

#include <QApplication>
#include <QAction>
#include <QIcon>
#include <QMenu>
#include <QMessageBox>
#include <QSystemTrayIcon>
#include <QTimer>

#include "app/appcontroller.h"
#include "core/appstrings.h"
#include "ui/appstyle.h"
#include "ui/capturetoast.h"
#include "ui/settingswindow.h"

// 保存控制器指针；托盘在 start 中创建，设置窗口在首次打开时才构造。
Application::Application(AppController *controller, QObject *parent)
    : QObject(parent), m_controller(controller)
{
}

// 首次打开设置时才构造窗口：常驻托盘期间不必保留六个设置页的控件树。
SettingsWindow *Application::ensureSettings()
{
    if (!m_settings)
        m_settings = new SettingsWindow(m_controller);
    return m_settings;
}

// 创建托盘菜单、连接控制器信号并显示首次启动通知。
bool Application::start()
{
    // 托盘、菜单和通知共用同一个 QIcon：多尺寸 ICO 只解码一次，避免每条通知重新读取。
    m_trayIcon = QIcon(QStringLiteral(":/icons/icon.ico"));
    // 全局样式表仍在启动时就位，保证设置窗口之外的对话框（QMessageBox 等）外观一致；
    // 设置窗口本身按需构造，常驻托盘期间不承担它的控件树。
    AppStyle::applyTheme();
    m_tray = new QSystemTrayIcon(m_trayIcon, this);
    // QSystemTrayIcon::setContextMenu 不接管菜单所有权，但这里不能给菜单挂一个非 QWidget 的
    // QObject 父对象：Qt 6 的 QWidget::parentWidget() 是
    //     return static_cast<QWidget *>(QObject::parent());
    // 不检查类型，那样 parentWidget() 会返回一个假 QWidget*，托盘弹出菜单时按父控件定位
    // 会直接崩溃。保持无父对象的顶层窗口，让菜单随进程退出一起回收。
    auto *menu = new QMenu;
    QAction *settingsAction = menu->addAction(AppStrings::settingsMenu());
    QAction *pauseAction = menu->addAction(AppStrings::pauseCaptureMenu());
    menu->addSeparator();
    QAction *exitAction = menu->addAction(AppStrings::exitMenu());
    m_tray->setContextMenu(menu);
    m_tray->setToolTip(AppStrings::readyTooltip());
    // 托盘菜单和单实例唤醒共用同一入口，设置窗口按需创建。
    const auto showSettings = [this]() {
        SettingsWindow *window = ensureSettings();
        window->show();
        window->raise();
        window->activateWindow();
    };
    connect(settingsAction, &QAction::triggered, this, showSettings);
    connect(m_controller, &AppController::settingsRequested, this, showSettings);
    connect(pauseAction, &QAction::triggered, this, [this, pauseAction]() {
        AppConfig config = m_controller->config();
        config.captureEnabled = !config.captureEnabled;
        QString error;
        if (m_controller->saveConfiguration(config, &error))
            pauseAction->setText(config.captureEnabled ? AppStrings::pauseCaptureMenu()
                                                       : AppStrings::resumeCaptureMenu());
    });
    connect(exitAction, &QAction::triggered, qApp, &QApplication::quit);
    connect(m_controller, &AppController::notificationShown, this,
            [this](const QString &title, const QString &message, bool withSound) {
                if (withSound)
                    QApplication::beep();
                m_tray->showMessage(title, message, m_trayIcon, 4000);
            });
    connect(m_controller, &AppController::databaseErrorOccurred, this, [this](const QString &message) {
        QMessageBox::critical(nullptr, AppStrings::databaseErrorTitle(),
                              message + AppStrings::databaseErrorAdvice());
        QApplication::quit();
    });
    m_tray->show();
    QTimer::singleShot(700, this, [this]() {
        if (m_controller->startupHotkeyConflict()) {
            QApplication::beep();
            m_tray->showMessage(AppStrings::applicationName(), AppStrings::globalHotkeyUnavailable(),
                                m_trayIcon, 6000);
        } else {
            m_tray->showMessage(AppStrings::applicationName(), AppStrings::readyNotification(),
                                m_trayIcon, 4000);
        }
    });
    return true;
}
