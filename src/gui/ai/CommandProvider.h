#pragma once

#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QString>
#include <functional>
#include "gui/ai/ChatMessage.h"

class CommandProvider : public QObject
{
  Q_OBJECT

public:
  explicit CommandProvider(QObject *parent = nullptr);
  ~CommandProvider() override;

  void sendRequest(const QString& prompt,
                   const ChatHistory& context,
                   const QString& systemPrompt,
                   const QString& editorContents = {},
                   const QStringList& screenshotPaths = {});
  void cancel();
  bool isRunning() const;

signals:
  void partialResponse(const QString& text);
  void responseComplete(const QString& fullResponse);
  void errorOccurred(const QString& errorMessage);

private slots:
  void onReadyReadStdout();
  void onProcessFinished(int exitCode, QProcess::ExitStatus status);
  void onProcessError(QProcess::ProcessError error);
  void onTimeout();

private:
  QString buildInput(const QString& prompt,
                     const ChatHistory& context,
                     const QString& systemPrompt,
                     const QString& editorContents,
                     const QStringList& screenshotPaths) const;
  void cleanup();

  QProcess *process_ = nullptr;
  QTimer *timeoutTimer_ = nullptr;
  QString accumulatedOutput_;
  bool finished_ = false;
  static constexpr int DEFAULT_TIMEOUT_MS = 300000;
};
