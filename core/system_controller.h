#ifndef SYSTEM_CONTROLLER_H
#define SYSTEM_CONTROLLER_H

#include <QString>  // 添加这行

class SystemController
{
public:
    static bool shutdown();
    static bool reboot();
    static bool suspend();
    static bool hibernate();
    // 带 RTC 自唤醒的休眠/休眠——给循环测试用,避免"休眠后无外部源唤醒"卡死
    // seconds: 期望休眠秒数,实际由 rtcwake 设置的 RTC 闹钟决定
    // 失败回退:rtcwake 不存在/权限不足 → 调普通 suspend()/hibernate()
    static bool suspendWithWake(int seconds);
    static bool hibernateWithWake(int seconds);

private:
    static bool executeSystemCommand(const QString &command);
};

#endif // SYSTEM_CONTROLLER_H
