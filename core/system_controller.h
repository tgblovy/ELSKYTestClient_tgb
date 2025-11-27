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

private:
    static bool executeSystemCommand(const QString &command);
};

#endif // SYSTEM_CONTROLLER_H
