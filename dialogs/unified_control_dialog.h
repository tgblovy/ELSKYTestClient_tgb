#ifndef UNIFIED_CONTROL_DIALOG_H
#define UNIFIED_CONTROL_DIALOG_H

#include <QDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include "unified_control_manager.h"
#include <QGroupBox>
#include <QLineEdit>

class UnifiedControlDialog : public QDialog
{
    Q_OBJECT

public:
    explicit UnifiedControlDialog(const QList<PeerInfo> &peers,
                                   const QString &currentTag,
                                   QWidget *parent = nullptr);

signals:
    void executeCommand(const QString &command, const QStringList &params);

private slots:
    void onExecute();
    void onRefreshPeers();
    void onCycleCheckChanged(int state);
    void onSelectAllPeers(int state);
    void onConnectServerCheckChanged(int state);

private:
    void setupUI(const QList<PeerInfo> &peers);
    void buildCommandAndSend();

    // 节点列表
    QListWidget *peerListWidget;
    QCheckBox *selectAllCheckBox;

    // 操作选项
    QCheckBox *shutdownCheck;
    QCheckBox *rebootCheck;
    QCheckBox *suspendCheck;
    QCheckBox *hibernateCheck;
    QCheckBox *networkSpeedCheck;
    QCheckBox *startCycleCheck;
    QCheckBox *stopCycleCheck;
    QCheckBox *connectServerCheck;
    QCheckBox *autoStartCheck;
    QCheckBox *alwaysOnTopCheck;
    QCheckBox *comTestCheck;
    QCheckBox *agingTestCheck;
    QCheckBox *stopAgingTestCheck;
    QCheckBox *startWakeOnLanCheck;
    QCheckBox *stopWakeOnLanCheck;
    QCheckBox *deviceDetectCheck;
    QCheckBox *clearLogsCheck;
    QCheckBox *reloadDeviceCheck;
    QCheckBox *clearIllegalShutdownCheck;

    // 循环配置
    QComboBox *cycleOperationCombo;
    QSpinBox *cycleIntervalSpin;
    QSpinBox *cycleCountSpin;

    // 服务器IP输入
    QLineEdit *serverIPEdit;

    // 信息
    QLabel *peerCountLabel;
    QString m_currentTag;
};

#endif // UNIFIED_CONTROL_DIALOG_H
