#ifndef LOGGER_H
#define LOGGER_H

#include <QObject>
#include <QTextEdit>
#include <QPushButton>
#include <QString>  // 确保包含QString

class Logger : public QObject
{
    Q_OBJECT

public:
    explicit Logger(QObject *parent = nullptr);

    void setupUI(QTextEdit *textEdit, QPushButton *clearButton);
    void addMessage(const QString &message);
    void saveLogToFile();
    void loadLogFromFile();

private slots:
    void onClearLogs();

private:
    QTextEdit *logTextEdit;
    QPushButton *clearLogsButton;
    QString logFilePath;
};

#endif // LOGGER_H
