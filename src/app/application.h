// 声明 Qt 应用外壳，负责托盘菜单、设置窗口和控制器生命周期。
#ifndef APP_APPLICATION_H
#define APP_APPLICATION_H

// 维护系统托盘、通知、设置窗口和控制器之间的连接。
#include <QObject>

class AppController;
class QSystemTrayIcon;
class SettingsWindow;

class Application : public QObject
{
    Q_OBJECT
public:
    explicit Application(AppController *controller, QObject *parent = nullptr);
    /// 创建托盘菜单并连接控制器信号；成功后进入常驻状态。
    bool start();

private:
    AppController *m_controller = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    SettingsWindow *m_settings = nullptr;
};

#endif
