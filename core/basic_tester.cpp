#include "basic_tester.h"
#include "system_controller.h"
#include "../dialogs/com_test_dialog.h"
#include "../dialogs/usb_speed_dialog.h"

#include <QMessageBox>
#include <QApplication>

BasicTester::BasicTester(QObject *parent)
    : QObject(parent)
    , basicTestCombo(nullptr)
    , executeButton(nullptr)
{
}

void BasicTester::setupUI(QComboBox *combo, QPushButton *button)
{
    basicTestCombo = combo;
    executeButton = button;

    connect(executeButton, &QPushButton::clicked, this, &BasicTester::executeTest);
}

void BasicTester::executeTest()
{
    if (!basicTestCombo) return;

    QString selectedTest = basicTestCombo->currentText();
    emit logMessage(QString("执行基础测试: %1").arg(selectedTest));

    if (selectedTest == "关机") {
        onShutdown();
    } else if (selectedTest == "重启") {
        onReboot();
    } else if (selectedTest == "睡眠") {
        onSuspend();
    } else if (selectedTest == "休眠") {
        onHibernate();
    } else if (selectedTest == "COM测试") {
        onComTest();
    } else if (selectedTest == "U盘测速") {
        onUsbSpeedTest();
    }
}

void BasicTester::onShutdown()
{
    emit logMessage("执行关机操作");
    SystemController::shutdown();
}

void BasicTester::onReboot()
{
    emit logMessage("执行重启操作");
    SystemController::reboot();
}

void BasicTester::onSuspend()
{
    emit logMessage("执行睡眠操作");
    SystemController::suspend();
}

void BasicTester::onHibernate()
{
    emit logMessage("执行休眠操作");
    SystemController::hibernate();
}

void BasicTester::onComTest()
{
#ifdef QT_SERIALPORT_LIB
    emit logMessage("打开COM端口测试对话框");
    ComTestDialog *dialog = new ComTestDialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    // 设置对话框为应用程序模态，确保显示在最前面
    dialog->setWindowModality(Qt::ApplicationModal);
    dialog->setWindowFlags(dialog->windowFlags() | Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);

    dialog->show();
    dialog->raise();
    dialog->activateWindow();
#else
    emit logMessage("COM测试功能不可用 - 未编译串口支持");
    QMessageBox msgBox;
    msgBox.setWindowTitle("功能不可用");
    msgBox.setText("COM测试功能在当前系统中不可用。\n请安装 QtSerialPort 库。");
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setWindowModality(Qt::ApplicationModal);
    msgBox.exec();
#endif
}

void BasicTester::onUsbSpeedTest()
{
    emit logMessage("打开U盘速度测试对话框");
    UsbSpeedTestDialog *dialog = new UsbSpeedTestDialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    // 设置对话框为应用程序模态，确保显示在最前面
    dialog->setWindowModality(Qt::ApplicationModal);
    dialog->setWindowFlags(dialog->windowFlags() | Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);

    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}
