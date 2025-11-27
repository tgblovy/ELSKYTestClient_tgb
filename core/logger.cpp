#include "logger.h"
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QMessageBox>
#include <QDir>

Logger::Logger(QObject *parent)
    : QObject(parent)
    , logTextEdit(nullptr)
    , clearLogsButton(nullptr)
{
    QString homeDir = QDir::homePath();
    logFilePath = homeDir + "/.elsky_test_client.log";
}

void Logger::setupUI(QTextEdit *textEdit, QPushButton *button)
{
    logTextEdit = textEdit;
    clearLogsButton = button;

    connect(clearLogsButton, &QPushButton::clicked, this, &Logger::onClearLogs);

    // 加载历史日志
    loadLogFromFile();
}

void Logger::addMessage(const QString &message)
{
    if (!logTextEdit) return;

    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    QString logEntry = QString("[%1] %2").arg(timestamp, message);

    // 添加到界面
    logTextEdit->append(logEntry);

    // 保存到文件
    QFile file(logFilePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        out << logEntry << "\n";
        file.close();
    }

    // 自动滚动到底部
    QTextCursor cursor = logTextEdit->textCursor();
    cursor.movePosition(QTextCursor::End);
    logTextEdit->setTextCursor(cursor);
}

void Logger::saveLogToFile()
{
    if (!logTextEdit) return;

    QFile file(logFilePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << logTextEdit->toPlainText();
        file.close();
    }
}

void Logger::loadLogFromFile()
{
    if (!logTextEdit) return;

    QFile file(logFilePath);
    if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        QString logContent = in.readAll();
        if (!logContent.isEmpty()) {
            logTextEdit->setPlainText(logContent);
            // 滚动到底部
            QTextCursor cursor = logTextEdit->textCursor();
            cursor.movePosition(QTextCursor::End);
            logTextEdit->setTextCursor(cursor);
        }
        file.close();
    }
}

void Logger::onClearLogs()
{
    if (!logTextEdit || logTextEdit->document()->isEmpty()) {
        QMessageBox::information(nullptr, "提示", "日志已经为空");
        return;
    }

    int result = QMessageBox::question(nullptr, "确认清除",
                                      "确定要清除所有日志吗？\n此操作不可恢复。",
                                      QMessageBox::Yes | QMessageBox::No,
                                      QMessageBox::No);

    if (result == QMessageBox::Yes) {
        logTextEdit->clear();

        // 同时清除日志文件
        QFile file(logFilePath);
        if (file.exists()) {
            file.remove();
        }

        addMessage("日志已清除");
    }
}
