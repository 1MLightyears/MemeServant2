// 声明 Windows 当前用户开机启动项的读写辅助函数。
#ifndef PLATFORMS_WIN_WINAUTOSTART_H
#define PLATFORMS_WIN_WINAUTOSTART_H

// 使用当前用户注册表管理开机启动。
/// 写入或删除当前用户的 MemeServant2 开机启动注册表项。
bool setWindowsAutoStart(bool enabled);

#endif
