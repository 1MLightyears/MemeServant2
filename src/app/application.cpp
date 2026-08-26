// 托盘是常驻生命周期入口；配置窗口X只隐藏，退出必须显式选择。
#include "app/application.h"

#include <QApplication>
#include <QAction>
#include <QColor>
#include <QFont>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QSystemTrayIcon>
#include <QTimer>

#include "app/appcontroller.h"
#include "core/appstrings.h"
#include "ui/capturetoast.h"
#include "ui/settingswindow.h"

namespace {
// 生成托盘使用的简单“M”图标，避免依赖外部资源文件。
QIcon applicationTrayIcon()
{
    QPixmap pixmap(64, 64);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QColor(0, 120, 215));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(4, 4, 56, 56, 16, 16);
    painter.setPen(Qt::white);
    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(34);
    painter.setFont(font);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, QStringLiteral("M"));
    return QIcon(pixmap);
}
}

// 保存控制器指针；托盘和设置窗口在 start 中创建。
Application::Application(AppController *controller, QObject *parent)
    : QObject(parent), m_controller(controller)
{
}

// 创建托盘菜单、连接控制器信号并显示首次启动通知。
bool Application::start()
{
    m_settings = new SettingsWindow(m_controller);
    m_tray = new QSystemTrayIcon(applicationTrayIcon(), this);
    auto *menu = new QMenu;
    QAction *settingsAction = menu->addAction(AppStrings::settingsMenu());
    QAction *pauseAction = menu->addAction(AppStrings::pauseCaptureMenu());
    menu->addSeparator();
    QAction *exitAction = menu->addAction(AppStrings::exitMenu());
    m_tray->setContextMenu(menu);
    m_tray->setToolTip(AppStrings::readyTooltip());
    connect(settingsAction, &QAction::triggered, this, [this]() {
        m_settings->show(); m_settings->raise(); m_settings->activateWindow();
    });
    connect(m_controller, &AppController::settingsRequested, this, [this]() {
        m_settings->show(); m_settings->raise(); m_settings->activateWindow();
    });
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
                m_tray->showMessage(title, message, QSystemTrayIcon::Information, 4000);
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
                                QSystemTrayIcon::Warning, 6000);
        } else {
            m_tray->showMessage(AppStrings::applicationName(), AppStrings::readyNotification(),
                                QSystemTrayIcon::Information, 4000);
        }
    });
    return true;
}
