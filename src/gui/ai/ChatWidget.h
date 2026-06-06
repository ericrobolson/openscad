#pragma once

#include <QWidget>
#include <QImage>
#include <QMap>
#include <QPair>
#include <QUndoStack>
#include <functional>
#include "gui/qtgettext.h"  // IWYU pragma: keep
#include "gui/ai/ChatMessage.h"
#include "ui_ChatWidget.h"

class CommandProvider;
class QLabel;
class QToolButton;
class QFlowLayout;

struct ScreenshotAttachment {
  QString name;
  QString filePath;
};

class MessageBubble : public QWidget
{
  Q_OBJECT
public:
  MessageBubble(const QString& text, bool isUser, QWidget *parent = nullptr);

  void updateText(const QString& text);
  QString text() const { return text_; }
  bool isUserMessage() const { return isUser_; }

signals:
  void copyRequested(const QString& text);
  void refetchRequested();
  void applyToEditorRequested(const QString& text);

private:
  void createActionButtons(QVBoxLayout *outerLayout);
  bool isDarkTheme() const;

  QLabel *label_ = nullptr;
  QFrame *bubbleFrame_ = nullptr;
  QString text_;
  bool isUser_;
};

class ChatWidget : public QWidget, public Ui::ChatWidget
{
  Q_OBJECT

public:
  ChatWidget(QWidget *parent = nullptr);
  virtual ~ChatWidget();

  void checkConfiguration();
  void setEditorContentGetter(std::function<QString()> getter);
  void setScreenshotGetter(std::function<QImage()> getter);

signals:
  void applyToEditorRequested(const QString& code);
  void renderRequested();

private slots:
  void onSendPressed();
  void onClearPressed();
  void onAttachScreenshot();
  void onPartialResponse(const QString& text);
  void onResponseComplete(const QString& fullResponse);
  void onProviderError(const QString& errorMessage);
  void onRefetchTurn(MessageBubble *aiBubble);

private:
  MessageBubble *addMessage(const QString& text, bool isUser);
  void clearMessages();
  void scrollToBottom();
  bool isDarkTheme() const;
  QString extractCode(const QString& text) const;

  void removeTurnFromUI(int turnIndex);
  void addTurnToUI(int turnIndex, const QString& userText, const QString& assistantText);

  CommandProvider *provider_ = nullptr;
  ChatHistory history_;
  QUndoStack *undoStack_ = nullptr;
  QMap<int, QPair<MessageBubble *, MessageBubble *>> turnBubbles_;
  int nextTurnIndex_ = 0;

  MessageBubble *currentResponseBubble_ = nullptr;
  QString currentPrompt_;
  QString editorContentBeforeTurn_;
  int currentTurnIndex_ = -1;
  bool isRefetching_ = false;
  bool configured_ = false;
  QWidget *configPromptWidget_ = nullptr;
  std::function<QString()> editorContentGetter_;
  std::function<QImage()> screenshotGetter_;
  QList<ScreenshotAttachment> pendingScreenshots_;
  QWidget *attachmentsWidget_ = nullptr;
  QVBoxLayout *attachmentsLayout_ = nullptr;
  int screenshotCounter_ = 0;

  void refreshAttachmentsDisplay();

  QPushButton *undoButton_ = nullptr;
  QPushButton *redoButton_ = nullptr;
  QPushButton *attachButton_ = nullptr;
};
