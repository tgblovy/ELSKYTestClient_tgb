#include "basic_tester.h"
#include "system_controller.h"
#include "../dialogs/com_test_dialog.h"
#include "../dialogs/usb_speed_dialog.h"
#include "network_speed_tester.h"

#include <QMessageBox>
#include <QApplication>
#include <QProgressDialog>
#include <QCoreApplication>
#include <QDebug>

BasicTester::BasicTester(QObject *parent)
    : QObject(parent)
    , basicTestCombo(nullptr)
    , executeButton(nullptr)
{
}

// 移除析构函数实现

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
    } else if (selectedTest == "网络测速") {
        onNetworkSpeedTest();
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
    dialog->setWindowModality(Qt::ApplicationModal);
    dialog->setWindowFlags(dialog->windowFlags() | Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void BasicTester::onNetworkSpeedTest()
{
    emit logMessage("开始网络速度测试...");

    // 创建进度对话框
    QProgressDialog *progressDialog = new QProgressDialog();
    progressDialog->setWindowTitle("网络测速");
    progressDialog->setLabelText("正在准备网络测速...");
    progressDialog->setCancelButtonText("取消测试");
    progressDialog->setRange(0, 0); // 无限进度
    progressDialog->setMinimumDuration(0);
    progressDialog->setModal(true);
    progressDialog->show();

    QCoreApplication::processEvents();

    // 创建网络测速器
    NetworkSpeedTester *speedTester = new NetworkSpeedTester();

    // 连接信号槽
    connect(speedTester, &NetworkSpeedTester::testFinished, this,
        [this, progressDialog, speedTester](double downloadSpeed, double uploadSpeed) {

            qDebug() << "网络测速完成，下载:" << downloadSpeed << "MB/s, 上传:" << uploadSpeed << "MB/s";

            QString result = QString(
                "🎯 网络速度测试完成！\n\n"
                "📥 下载速度 (下行): %1 MB/s\n"
                "📤 上传速度 (上行): %2 MB/s\n\n"
                "💡 单位换算:\n"
                "   1 MB/s = 8 Mbps\n"
                "   下载速度: %3 Mbps\n"
                "   上传速度: %4 Mbps"
            ).arg(downloadSpeed, 0, 'f', 2)
             .arg(uploadSpeed, 0, 'f', 2)
             .arg(downloadSpeed * 8, 0, 'f', 2)
             .arg(uploadSpeed * 8, 0, 'f', 2);

            emit logMessage("网络测速完成");
            emit logMessage(QString("下载速度: %1 MB/s (%2 Mbps)").arg(downloadSpeed, 0, 'f', 2).arg(downloadSpeed * 8, 0, 'f', 2));
            emit logMessage(QString("上传速度: %1 MB/s (%2 Mbps)").arg(uploadSpeed, 0, 'f', 2).arg(uploadSpeed * 8, 0, 'f', 2));

            if (progressDialog && progressDialog->isVisible()) {
                progressDialog->close();
                progressDialog->deleteLater();
            }

            QMessageBox::information(nullptr, "🎯 网络测速结果", result);

            speedTester->deleteLater();
        });

    connect(speedTester, &NetworkSpeedTester::testProgress, this,
        [this, progressDialog](const QString &message) {
            qDebug() << "网络测速进度:" << message;
            emit logMessage("网络测速: " + message);

            if (progressDialog && progressDialog->isVisible()) {
                progressDialog->setLabelText(message);
                QCoreApplication::processEvents();
            }
        });

    connect(speedTester, &NetworkSpeedTester::testError, this,
        [this, progressDialog, speedTester](const QString &error) {
            qDebug() << "网络测速错误:" << error;
            emit logMessage("网络测速错误: " + error);

            if (progressDialog && progressDialog->isVisible()) {
                progressDialog->close();
                progressDialog->deleteLater();
            }

            QMessageBox::warning(nullptr, "网络测速错误",
                QString("网络测速失败:\n%1\n\n请检查网络连接后重试。").arg(error));

            speedTester->deleteLater();
        });

    connect(progressDialog, &QProgressDialog::canceled, this,
        [progressDialog, speedTester]() {
            qDebug() << "用户取消网络测速";
            speedTester->stopTest();

            if (progressDialog) {
                progressDialog->deleteLater();
            }
            speedTester->deleteLater();
        });

    // 延迟开始测试
    QTimer::singleShot(100, speedTester, &NetworkSpeedTester::startTest);
}
