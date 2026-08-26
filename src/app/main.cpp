// 程序入口负责初始化 Qt 应用、日志、控制器和托盘生命周期。
#include <QApplication>
#include <QMessageBox>

#include "app/application.h"
#include "app/appcontroller.h"
#include "core/appstrings.h"

// 创建 Qt 应用并在控制器初始化成功后进入事件循环。
int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("MemeServant2"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setQuitOnLastWindowClosed(false);

    AppController controller;
    QString error;
    if (!controller.initialize(&error)) {
        if (error != QStringLiteral("MemeServant2已在运行。"))
            QMessageBox::critical(nullptr, AppStrings::startupFailureTitle(), error);
        return 1;
    }
    Application shell(&controller);
    if (!shell.start())
        return 2;
    return QApplication::exec();
}
