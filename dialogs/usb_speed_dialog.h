#ifndef USB_SPEED_DIALOG_H
#define USB_SPEED_DIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QPushButton>
#include <QProgressBar>
#include <QTextEdit>
#include <QTimer>

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

private:
    void setupUI();
    void detectUsbDrives();
    QString performSpeedTest();
    bool runDDTestAsRoot(const QString &drivePath, int testFileSizeMB, double &writeSpeed, double &readSpeed);
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
};

#endif // USB_SPEED_DIALOG_H
