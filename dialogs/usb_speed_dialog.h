#ifndef USB_SPEED_DIALOG_H
#define USB_SPEED_DIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QPushButton>
#include <QProgressBar>
#include <QTextEdit>
#include <QTimer>
#include <QProcess>

class UsbSpeedTestDialog : public QDialog
{
    Q_OBJECT

public:
    UsbSpeedTestDialog(QWidget *parent = nullptr);
    ~UsbSpeedTestDialog();

private slots:
    void onRefreshDrives();
    void onStartTest();
    void onTestFileSizeChanged(int index);
    void updateProgress();
    void onTestProcessReadyRead();
    void onTestProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onTestTimeout();

private:
    void setupUI();
    void detectUsbDrives();
    bool startDDTestAsRoot(const QString &drivePath, int testFileSizeMB);
    bool parseSpeedResult(const QString &output, double &writeSpeed, double &readSpeed);
    void finishTestUi(bool success, double writeSpeed, double readSpeed);
    QString createTestScript(const QString &drivePath, int testFileSizeMB);
    void clearCache();
    QString evaluatePerformance(double writeSpeed, double readSpeed);

    QComboBox *driveCombo;
    QComboBox *testFileSizeCombo;
    QPushButton *refreshButton;
    QPushButton *startTestButton;
    QProgressBar *progressBar;
    QTextEdit *resultEdit;

    QTimer *progressTimer;
    int progressValue;

    // 信号驱动测速状态（修复：原实现 waitForFinished(300000) 在 GUI 线程
    // 同步阻塞最长 5 分钟导致整个界面冻结）
    QProcess *testProcess = nullptr;   // 测试子进程（信号驱动，不阻塞）
    QTimer *testTimeoutTimer = nullptr; // 测试总超时定时器
    QString testOutput;                 // 累积的测试输出
    QString tempScriptPath;             // 临时测试脚本路径
    int currentTestSizeMB = 0;          // 当前测试文件大小
};

#endif // USB_SPEED_DIALOG_H
