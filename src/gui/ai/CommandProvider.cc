#include "gui/ai/CommandProvider.h"
#include "gui/ai/AISettings.h"
#include <QDir>
#include <QProcessEnvironment>

CommandProvider::CommandProvider(QObject *parent) : QObject(parent)
{
  timeoutTimer_ = new QTimer(this);
  timeoutTimer_->setSingleShot(true);
  connect(timeoutTimer_, &QTimer::timeout, this, &CommandProvider::onTimeout);
}

CommandProvider::~CommandProvider()
{
  cancel();
}

void CommandProvider::sendRequest(const QString& prompt,
                                 const ChatHistory& context,
                                 const QString& systemPrompt,
                                 const QString& editorContents,
                                 const QStringList& screenshotPaths)
{
  if (isRunning()) {
    emit errorOccurred("A request is already in progress");
    return;
  }

  nlohmann::json prof = AISettings::activeProfile();
  QString command = QString::fromStdString(prof.value("command", ""));
  if (command.trimmed().isEmpty()) {
    emit errorOccurred("No command configured. Open Preferences > AI to set a command.");
    return;
  }

  accumulatedOutput_.clear();
  finished_ = false;
  process_ = new QProcess(this);

  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  std::string apiKey = prof.value("apiKey", "");
  if (!apiKey.empty()) {
    QString key = QString::fromStdString(apiKey);
    env.insert("OPENAI_API_KEY", key);
    env.insert("ANTHROPIC_API_KEY", key);
  }
  process_->setProcessEnvironment(env);
  process_->setWorkingDirectory(QDir::tempPath());

  connect(process_, &QProcess::readyReadStandardOutput, this, &CommandProvider::onReadyReadStdout);
  connect(process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
          this, &CommandProvider::onProcessFinished);
  connect(process_, &QProcess::errorOccurred, this, &CommandProvider::onProcessError);

  auto parts = QProcess::splitCommand(command);
  if (parts.isEmpty()) {
    emit errorOccurred("Invalid command: " + command);
    cleanup();
    return;
  }

  QString prog = parts.first().toLower();
  bool isClaude = prog == "claude" || prog.endsWith("/claude");

  if (isClaude) {
    if (!systemPrompt.isEmpty()) {
      parts.append("--system-prompt");
      parts.append(systemPrompt);
    }
    if (!screenshotPaths.isEmpty()) {
      parts.append("--allowed-tools");
      parts.append("Read");
    }
  }

  QString program = parts.takeFirst();
  process_->start(program, parts);

  if (!process_->waitForStarted(5000)) {
    emit errorOccurred("Failed to start command: " + command);
    cleanup();
    return;
  }

  QString input = buildInput(prompt, context, systemPrompt, editorContents, screenshotPaths);
  process_->write(input.toUtf8());
  process_->closeWriteChannel();

  int timeoutMs = AISettings::activeParam("timeout", "300000").toInt();
  if (timeoutMs <= 0) timeoutMs = DEFAULT_TIMEOUT_MS;
  timeoutTimer_->start(timeoutMs);
}

void CommandProvider::cancel()
{
  if (!isRunning()) return;
  timeoutTimer_->stop();
  if (process_) {
    process_->kill();
    process_->waitForFinished(3000);
  }
  cleanup();
}

bool CommandProvider::isRunning() const
{
  return process_ && process_->state() != QProcess::NotRunning;
}

void CommandProvider::onReadyReadStdout()
{
  if (!process_) return;
  QByteArray data = process_->readAllStandardOutput();
  accumulatedOutput_ += QString::fromUtf8(data);
  emit partialResponse(accumulatedOutput_);
}

void CommandProvider::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
  timeoutTimer_->stop();
  if (finished_) return;
  finished_ = true;

  QByteArray remaining = process_ ? process_->readAllStandardOutput() : QByteArray();
  if (!remaining.isEmpty()) {
    accumulatedOutput_ += QString::fromUtf8(remaining);
  }

  if (status == QProcess::CrashExit || exitCode != 0) {
    QString stderr_out = process_ ? QString::fromUtf8(process_->readAllStandardError()) : "";
    QString errorStr = process_ ? process_->errorString() : "";

    QStringList quotedArgs;
    if (process_) {
      quotedArgs.append(process_->program());
      for (const auto& arg : process_->arguments()) {
        if (arg.contains(' ') || arg.contains('"')) {
          quotedArgs.append("\"" + QString(arg).replace("\"", "\\\"") + "\"");
        } else {
          quotedArgs.append(arg);
        }
      }
    }
    QString cmd = quotedArgs.isEmpty() ? "(unknown)" : quotedArgs.join(" ");

    QString detail;
    detail += "Command: " + cmd + "\n";
    detail += "Exit code: " + QString::number(exitCode) + "\n";
    detail += "Exit status: " + QString(status == QProcess::CrashExit ? "Crashed (signal " + QString::number(exitCode) + ")" : "Failed") + "\n";
    if (!errorStr.isEmpty()) detail += "Error: " + errorStr + "\n";
    if (!stderr_out.trimmed().isEmpty()) detail += "\nStderr:\n" + stderr_out.trimmed() + "\n";
    if (!accumulatedOutput_.trimmed().isEmpty()) detail += "\nStdout:\n" + accumulatedOutput_.trimmed() + "\n";

    emit errorOccurred(detail);
  } else {
    emit responseComplete(accumulatedOutput_);
  }

  cleanup();
}

void CommandProvider::onProcessError(QProcess::ProcessError error)
{
  if (error != QProcess::FailedToStart) return;
  timeoutTimer_->stop();
  if (finished_) return;
  finished_ = true;
  QString msg = process_ ? process_->errorString() : "Unknown process error";
  emit errorOccurred(msg);
  cleanup();
}

void CommandProvider::onTimeout()
{
  if (finished_) return;
  finished_ = true;
  if (isRunning()) {
    process_->kill();
    process_->waitForFinished(3000);
  }
  emit errorOccurred("Request timed out after " +
    QString::number(timeoutTimer_->interval() / 1000) +
    "s. Increase the 'timeout' parameter in Preferences > AI to allow more time.");
  cleanup();
}

QString CommandProvider::buildInput(const QString& prompt,
                                   const ChatHistory& context,
                                   const QString& systemPrompt,
                                   const QString& editorContents,
                                   const QStringList& screenshotPaths) const
{
  QString input;

  if (!systemPrompt.isEmpty()) {
    input += "Instructions: " + systemPrompt + "\n\n";
  }

  if (!editorContents.isEmpty()) {
    input += "Current OpenSCAD code in the editor:\n```\n" + editorContents + "\n```\n\n";
  }

  if (!screenshotPaths.isEmpty()) {
    input += "Attached screenshots of the current 3D render (use the Read tool to view these image files):\n";
    for (const auto& path : screenshotPaths) {
      input += "- " + path + "\n";
    }
    input += "\n";
  }

  if (!context.empty()) {
    for (const auto& msg : context) {
      switch (msg.role) {
      case ChatMessage::Role::User:
        input += "User: " + msg.content + "\n\n";
        break;
      case ChatMessage::Role::Assistant:
        input += "Assistant: " + msg.content + "\n\n";
        break;
      default:
        break;
      }
    }
  }

  input += prompt;
  return input;
}

void CommandProvider::cleanup()
{
  if (process_) {
    process_->deleteLater();
    process_ = nullptr;
  }
}
