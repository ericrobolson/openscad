#include "gui/ai/ChatWidget.h"
#include "gui/ai/AISettings.h"
#include "gui/ai/ChatUndoCommands.h"
#include "gui/ai/CommandProvider.h"
#include "gui/qtgettext.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>

// --- MessageBubble ---

MessageBubble::MessageBubble(const QString& text, bool isUser, QWidget *parent)
  : QWidget(parent), text_(text), isUser_(isUser)
{
  auto *outerLayout = new QVBoxLayout(this);
  outerLayout->setContentsMargins(0, 4, 0, 4);
  outerLayout->setSpacing(2);

  auto *rowLayout = new QHBoxLayout();
  rowLayout->setContentsMargins(0, 0, 0, 0);

  bubbleFrame_ = new QFrame(this);
  bubbleFrame_->setFrameShape(QFrame::StyledPanel);

  bool dark = isDarkTheme();
  QString frameStyle;
  QString labelStyle;

  if (isUser) {
    if (dark) {
      frameStyle = "QFrame { background-color: #2563eb; border: none; border-radius: 12px; }";
      labelStyle = "QLabel { color: #ffffff; font-size: 10pt; }";
    } else {
      frameStyle = "QFrame { background-color: #3b82f6; border: none; border-radius: 12px; }";
      labelStyle = "QLabel { color: #ffffff; font-size: 10pt; }";
    }
    rowLayout->addStretch(1);
    rowLayout->addWidget(bubbleFrame_);
  } else {
    if (dark) {
      frameStyle = "QFrame { background-color: #374151; border: none; border-radius: 12px; }";
      labelStyle = "QLabel { color: #f3f4f6; font-size: 10pt; }";
    } else {
      frameStyle = "QFrame { background-color: #f3f4f6; border: none; border-radius: 12px; }";
      labelStyle = "QLabel { color: #1f2937; font-size: 10pt; }";
    }
    rowLayout->addWidget(bubbleFrame_);
    rowLayout->addStretch(1);
  }

  bubbleFrame_->setStyleSheet(frameStyle);

  auto *frameLayout = new QVBoxLayout(bubbleFrame_);
  frameLayout->setContentsMargins(10, 8, 10, 8);

  label_ = new QLabel(text, bubbleFrame_);
  label_->setWordWrap(true);
  label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  label_->setStyleSheet(labelStyle);

  frameLayout->addWidget(label_);
  outerLayout->addLayout(rowLayout);

  createActionButtons(outerLayout);
}

void MessageBubble::updateText(const QString& text)
{
  text_ = text;
  label_->setText(text);
}

void MessageBubble::createActionButtons(QVBoxLayout *outerLayout)
{
  auto *actionLayout = new QHBoxLayout();
  actionLayout->setContentsMargins(0, 0, 0, 0);
  actionLayout->setSpacing(4);

  auto makeButton = [this](const QString& label) {
    auto *btn = new QToolButton(this);
    btn->setText(label);
    btn->setAutoRaise(true);
    btn->setStyleSheet("QToolButton { font-size: 8pt; padding: 1px 4px; }");
    return btn;
  };

  if (isUser_) {
    actionLayout->addStretch(1);
  }

  auto *copyBtn = makeButton(_("Copy"));
  connect(copyBtn, &QToolButton::clicked, this, [this]() { emit copyRequested(text_); });
  actionLayout->addWidget(copyBtn);

  if (!isUser_) {
    auto *refetchBtn = makeButton(_("Refetch"));
    connect(refetchBtn, &QToolButton::clicked, this, [this]() { emit refetchRequested(); });
    actionLayout->addWidget(refetchBtn);

    auto *applyBtn = makeButton(_("Apply to Editor"));
    connect(applyBtn, &QToolButton::clicked, this, [this]() { emit applyToEditorRequested(text_); });
    actionLayout->addWidget(applyBtn);

    actionLayout->addStretch(1);
  }

  outerLayout->addLayout(actionLayout);
}

bool MessageBubble::isDarkTheme() const
{
  QPalette pal = QApplication::palette();
  return pal.color(QPalette::Window).lightness() < 128;
}

// --- ChatWidget ---

ChatWidget::ChatWidget(QWidget *parent) : QWidget(parent)
{
  setupUi(this);

  titleLabel->setText(_("AI Assistant"));
  clearButton->setText(_("Clear"));
  clearButton->setToolTip(_("Clear chat history"));
  sendButton->setText(_("Send"));

  inputField->setEnabled(false);
  sendButton->setEnabled(false);

  undoButton_ = new QPushButton(_("Undo"), headerWidget);
  undoButton_->setMaximumWidth(50);
  undoButton_->setEnabled(false);
  redoButton_ = new QPushButton(_("Redo"), headerWidget);
  redoButton_->setMaximumWidth(50);
  redoButton_->setEnabled(false);

  headerLayout->insertWidget(headerLayout->indexOf(clearButton), undoButton_);
  headerLayout->insertWidget(headerLayout->indexOf(clearButton), redoButton_);

  attachButton_ = new QPushButton(_("Screenshot"), inputWidget);
  attachButton_->setMaximumWidth(80);
  attachButton_->setToolTip(_("Attach a screenshot of the current 3D view"));
  inputLayout->insertWidget(inputLayout->indexOf(sendButton), attachButton_);

  attachmentsWidget_ = new QWidget(this);
  attachmentsLayout_ = new QVBoxLayout(attachmentsWidget_);
  attachmentsLayout_->setContentsMargins(4, 0, 4, 0);
  attachmentsLayout_->setSpacing(2);
  attachmentsWidget_->hide();
  mainLayout->insertWidget(mainLayout->indexOf(inputWidget), attachmentsWidget_);

  provider_ = new CommandProvider(this);
  undoStack_ = new QUndoStack(this);

  connect(sendButton, &QPushButton::clicked, this, &ChatWidget::onSendPressed);
  connect(inputField, &ChatInputEdit::sendPressed, this, &ChatWidget::onSendPressed);
  connect(clearButton, &QPushButton::clicked, this, &ChatWidget::onClearPressed);
  connect(attachButton_, &QPushButton::clicked, this, &ChatWidget::onAttachScreenshot);

  connect(undoButton_, &QPushButton::clicked, undoStack_, &QUndoStack::undo);
  connect(redoButton_, &QPushButton::clicked, undoStack_, &QUndoStack::redo);
  connect(undoStack_, &QUndoStack::canUndoChanged, undoButton_, &QPushButton::setEnabled);
  connect(undoStack_, &QUndoStack::canRedoChanged, redoButton_, &QPushButton::setEnabled);

  connect(provider_, &CommandProvider::partialResponse, this, &ChatWidget::onPartialResponse);
  connect(provider_, &CommandProvider::responseComplete, this, &ChatWidget::onResponseComplete);
  connect(provider_, &CommandProvider::errorOccurred, this, &ChatWidget::onProviderError);

  addMessage(_("Hello! I am your OpenSCAD AI assistant.\n\n"
               "Ask me to write some code, e.g. \"draw a sphere\" or \"create a box with a hole\"."),
             false);
}

ChatWidget::~ChatWidget()
{
  provider_->cancel();
}

void ChatWidget::checkConfiguration()
{
  configured_ = AISettings::isConfigured();

  if (!configured_) {
    inputField->setEnabled(false);
    sendButton->setEnabled(false);
    if (!configPromptWidget_) {
      configPromptWidget_ = new QWidget(scrollAreaWidgetContents);
      auto *layout = new QHBoxLayout(configPromptWidget_);
      layout->setContentsMargins(6, 6, 6, 6);
      auto *frame = new QFrame(configPromptWidget_);
      frame->setFrameShape(QFrame::StyledPanel);
      bool dark = isDarkTheme();
      frame->setStyleSheet(dark
        ? "QFrame { background-color: #92400e; border: none; border-radius: 8px; }"
        : "QFrame { background-color: #fef3c7; border: none; border-radius: 8px; }");
      auto *fl = new QVBoxLayout(frame);
      fl->setContentsMargins(10, 8, 10, 8);
      auto *label = new QLabel(
        _("AI provider is not configured.\n\nOpen Edit > Preferences > AI to set up a command "
          "(e.g. \"claude\", \"llm chat\") or an API endpoint and key."),
        frame);
      label->setWordWrap(true);
      label->setStyleSheet(dark
        ? "QLabel { color: #fde68a; font-size: 10pt; }"
        : "QLabel { color: #92400e; font-size: 10pt; }");
      fl->addWidget(label);
      layout->addWidget(frame);
      scrollLayout->insertWidget(scrollLayout->count() - 1, configPromptWidget_);
      scrollToBottom();
    }
  } else {
    inputField->setEnabled(true);
    sendButton->setEnabled(true);
    if (configPromptWidget_) {
      scrollLayout->removeWidget(configPromptWidget_);
      delete configPromptWidget_;
      configPromptWidget_ = nullptr;
    }
  }
}

void ChatWidget::setEditorContentGetter(std::function<QString()> getter)
{
  editorContentGetter_ = std::move(getter);
}

void ChatWidget::setScreenshotGetter(std::function<QImage()> getter)
{
  screenshotGetter_ = std::move(getter);
}

void ChatWidget::onAttachScreenshot()
{
  if (!screenshotGetter_) return;

  QImage img = screenshotGetter_();
  if (img.isNull()) return;

  screenshotCounter_++;
  QString name = QString("screenshot_%1").arg(screenshotCounter_);
  QString path = QDir::tempPath() + "/openscad_" + name + ".png";
  img.save(path, "PNG");

  pendingScreenshots_.append({name, path});
  refreshAttachmentsDisplay();
}

void ChatWidget::refreshAttachmentsDisplay()
{
  QLayoutItem *child;
  while (attachmentsLayout_->count() > 0) {
    child = attachmentsLayout_->takeAt(0);
    if (child->widget()) delete child->widget();
    delete child;
  }

  if (pendingScreenshots_.isEmpty()) {
    attachmentsWidget_->hide();
    return;
  }

  attachmentsWidget_->show();

  for (int i = 0; i < pendingScreenshots_.size(); ++i) {
    auto *row = new QWidget(attachmentsWidget_);
    auto *hl = new QHBoxLayout(row);
    hl->setContentsMargins(2, 1, 2, 1);
    hl->setSpacing(4);

    auto *label = new QLabel(pendingScreenshots_[i].name, row);
    label->setStyleSheet("QLabel { font-size: 9pt; }");
    hl->addWidget(label);

    auto *removeBtn = new QToolButton(row);
    removeBtn->setText("x");
    removeBtn->setAutoRaise(true);
    removeBtn->setStyleSheet("QToolButton { font-size: 8pt; padding: 0px 3px; }");
    int idx = i;
    connect(removeBtn, &QToolButton::clicked, this, [this, idx]() {
      if (idx < pendingScreenshots_.size()) {
        QFile::remove(pendingScreenshots_[idx].filePath);
        pendingScreenshots_.removeAt(idx);
        refreshAttachmentsDisplay();
      }
    });
    hl->addWidget(removeBtn);
    hl->addStretch(1);

    attachmentsLayout_->addWidget(row);
  }
}

void ChatWidget::onSendPressed()
{
  if (!configured_ || provider_->isRunning()) return;

  QString prompt = inputField->toPlainText().trimmed();
  if (prompt.isEmpty()) return;

  inputField->clear();
  isRefetching_ = false;

  editorContentBeforeTurn_ = editorContentGetter_ ? editorContentGetter_() : QString();

  MessageBubble *userBubble = addMessage(prompt, true);
  history_.push_back({ChatMessage::Role::User, prompt});

  currentResponseBubble_ = addMessage("...", false);
  currentPrompt_ = prompt;
  currentTurnIndex_ = nextTurnIndex_;

  turnBubbles_[currentTurnIndex_] = {userBubble, currentResponseBubble_};

  int contextLimit = AISettings::activeParam("context_limit", "10").toInt();
  ChatHistory contextSlice;
  int start = std::max(0, static_cast<int>(history_.size()) - contextLimit);
  for (int i = start; i < static_cast<int>(history_.size()) - 1; ++i) {
    contextSlice.push_back(history_[i]);
  }

  QString systemPrompt = AISettings::activeParam("system_prompt");
  QString editorContents = editorContentBeforeTurn_;

  QStringList screenshotPaths;
  for (const auto& s : pendingScreenshots_) {
    screenshotPaths.append(s.filePath);
  }
  pendingScreenshots_.clear();
  refreshAttachmentsDisplay();

  sendButton->setEnabled(false);
  provider_->sendRequest(prompt, contextSlice, systemPrompt, editorContents, screenshotPaths);
}

void ChatWidget::onPartialResponse(const QString& text)
{
  if (currentResponseBubble_) {
    currentResponseBubble_->updateText(text);
    scrollToBottom();
  }
}

void ChatWidget::onResponseComplete(const QString& fullResponse)
{
  if (currentResponseBubble_) {
    currentResponseBubble_->updateText(fullResponse);
  }

  QString code = extractCode(fullResponse);

  if (isRefetching_) {
    for (auto it = history_.begin(); it != history_.end(); ++it) {
      if (it->role == ChatMessage::Role::Assistant) {
        auto turnIt = turnBubbles_.find(currentTurnIndex_);
        if (turnIt != turnBubbles_.end() && turnIt.value().second == currentResponseBubble_) {
          it->content = fullResponse;
          break;
        }
      }
    }
    isRefetching_ = false;
  } else {
    history_.push_back({ChatMessage::Role::Assistant, fullResponse});

    auto *cmd = new ChatTurnCommand(
      currentTurnIndex_,
      currentPrompt_, fullResponse,
      editorContentBeforeTurn_, code,
      [this](int idx, const QString& u, const QString& a) { addTurnToUI(idx, u, a); },
      [this](int idx) { removeTurnFromUI(idx); },
      [this](const QString& content) { emit applyToEditorRequested(content); },
      [this]() { emit renderRequested(); }
    );
    undoStack_->push(cmd);
    nextTurnIndex_++;
  }

  currentResponseBubble_ = nullptr;
  currentPrompt_.clear();
  currentTurnIndex_ = -1;
  sendButton->setEnabled(true);
  scrollToBottom();

  if (!code.isEmpty()) {
    emit applyToEditorRequested(code);
    emit renderRequested();
  }
}

void ChatWidget::onProviderError(const QString& errorMessage)
{
  if (currentResponseBubble_) {
    scrollLayout->removeWidget(currentResponseBubble_);
    delete currentResponseBubble_;
    currentResponseBubble_ = nullptr;
  }

  auto *errorWidget = new QWidget(scrollAreaWidgetContents);
  auto *outerLayout = new QVBoxLayout(errorWidget);
  outerLayout->setContentsMargins(0, 4, 0, 4);
  outerLayout->setSpacing(2);

  auto *frame = new QFrame(errorWidget);
  frame->setFrameShape(QFrame::StyledPanel);
  bool dark = isDarkTheme();
  frame->setStyleSheet(dark
    ? "QFrame { background-color: #7f1d1d; border: none; border-radius: 12px; }"
    : "QFrame { background-color: #fef2f2; border: none; border-radius: 12px; }");

  auto *frameLayout = new QVBoxLayout(frame);
  frameLayout->setContentsMargins(10, 8, 10, 8);

  auto *label = new QLabel(errorMessage, frame);
  label->setWordWrap(true);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  label->setStyleSheet(dark
    ? "QLabel { color: #fca5a5; font-size: 10pt; }"
    : "QLabel { color: #991b1b; font-size: 10pt; }");
  frameLayout->addWidget(label);

  outerLayout->addWidget(frame);

  auto *btnLayout = new QHBoxLayout();
  btnLayout->setContentsMargins(0, 0, 0, 0);
  auto *copyBtn = new QToolButton(errorWidget);
  copyBtn->setText(_("Copy Error"));
  copyBtn->setAutoRaise(true);
  copyBtn->setStyleSheet("QToolButton { font-size: 8pt; padding: 1px 4px; }");
  connect(copyBtn, &QToolButton::clicked, errorWidget, [errorMessage]() {
    QApplication::clipboard()->setText(errorMessage);
  });
  btnLayout->addWidget(copyBtn);
  btnLayout->addStretch(1);
  outerLayout->addLayout(btnLayout);

  scrollLayout->insertWidget(scrollLayout->count() - 1, errorWidget);
  scrollToBottom();

  if (!history_.empty() && history_.back().role == ChatMessage::Role::User) {
    history_.pop_back();
  }

  if (currentTurnIndex_ >= 0) {
    auto it = turnBubbles_.find(currentTurnIndex_);
    if (it != turnBubbles_.end()) {
      if (it.value().first) {
        scrollLayout->removeWidget(it.value().first);
        delete it.value().first;
      }
      turnBubbles_.remove(currentTurnIndex_);
    }
  }

  currentPrompt_.clear();
  currentTurnIndex_ = -1;
  isRefetching_ = false;
  sendButton->setEnabled(true);
}

void ChatWidget::onRefetchTurn(MessageBubble *aiBubble)
{
  if (provider_->isRunning()) return;

  int turnIndex = -1;
  for (auto it = turnBubbles_.begin(); it != turnBubbles_.end(); ++it) {
    if (it.value().second == aiBubble) {
      turnIndex = it.key();
      break;
    }
  }
  if (turnIndex < 0) return;

  auto it = turnBubbles_.find(turnIndex);
  if (it == turnBubbles_.end() || !it.value().first) return;
  QString prompt = it.value().first->text();
  if (prompt.isEmpty()) return;

  // Remove all turns after this one
  QList<int> toRemove;
  for (auto ti = turnBubbles_.begin(); ti != turnBubbles_.end(); ++ti) {
    if (ti.key() > turnIndex) toRemove.append(ti.key());
  }
  for (int idx : toRemove) {
    auto pair = turnBubbles_[idx];
    if (pair.first) { scrollLayout->removeWidget(pair.first); delete pair.first; }
    if (pair.second) { scrollLayout->removeWidget(pair.second); delete pair.second; }
    turnBubbles_.remove(idx);
  }

  // Trim history to only include messages up to and including this turn's user message
  // Find the user message for this turn, remove everything after it
  for (int i = static_cast<int>(history_.size()) - 1; i >= 0; --i) {
    if (history_[i].role == ChatMessage::Role::User && history_[i].content == prompt) {
      history_.resize(i + 1);
      break;
    }
  }

  undoStack_->clear();
  nextTurnIndex_ = turnIndex + 1;

  isRefetching_ = true;
  editorContentBeforeTurn_ = editorContentGetter_ ? editorContentGetter_() : QString();
  aiBubble->updateText("...");
  currentResponseBubble_ = aiBubble;
  currentPrompt_ = prompt;
  currentTurnIndex_ = turnIndex;

  ChatHistory contextSlice;
  int contextLimit = AISettings::activeParam("context_limit", "10").toInt();
  int start = std::max(0, static_cast<int>(history_.size()) - contextLimit);
  for (int i = start; i < static_cast<int>(history_.size()) - 1; ++i) {
    contextSlice.push_back(history_[i]);
  }

  QString systemPrompt = AISettings::activeParam("system_prompt");

  sendButton->setEnabled(false);
  provider_->sendRequest(prompt, contextSlice, systemPrompt, editorContentBeforeTurn_);
}

void ChatWidget::onClearPressed()
{
  provider_->cancel();
  clearMessages();
  history_.clear();
  undoStack_->clear();
  turnBubbles_.clear();
  nextTurnIndex_ = 0;
  currentResponseBubble_ = nullptr;
  currentPrompt_.clear();
  currentTurnIndex_ = -1;
  sendButton->setEnabled(true);

  addMessage(_("Hello! I am your OpenSCAD AI assistant.\n\n"
               "Ask me to write some code, e.g. \"draw a sphere\" or \"create a box with a hole\"."),
             false);
}

MessageBubble *ChatWidget::addMessage(const QString& text, bool isUser)
{
  auto *bubble = new MessageBubble(text, isUser, scrollAreaWidgetContents);
  scrollLayout->insertWidget(scrollLayout->count() - 1, bubble);

  connect(bubble, &MessageBubble::copyRequested, this, [](const QString& t) {
    QApplication::clipboard()->setText(t);
  });

  if (!isUser) {
    connect(bubble, &MessageBubble::refetchRequested, this, [this, bubble]() {
      onRefetchTurn(bubble);
    });
    connect(bubble, &MessageBubble::applyToEditorRequested, this, [this](const QString& t) {
      emit applyToEditorRequested(extractCode(t));
    });
  }

  scrollToBottom();
  return bubble;
}

void ChatWidget::clearMessages()
{
  QLayoutItem *child;
  while (scrollLayout->count() > 1) {
    child = scrollLayout->takeAt(0);
    if (child->widget()) {
      delete child->widget();
    }
    delete child;
  }
}

void ChatWidget::scrollToBottom()
{
  QTimer::singleShot(50, this, [this]() {
    scrollArea->verticalScrollBar()->setValue(scrollArea->verticalScrollBar()->maximum());
  });
}

void ChatWidget::removeTurnFromUI(int turnIndex)
{
  auto it = turnBubbles_.find(turnIndex);
  if (it == turnBubbles_.end()) return;

  auto [userBubble, aiBubble] = it.value();
  if (userBubble) {
    scrollLayout->removeWidget(userBubble);
    userBubble->hide();
  }
  if (aiBubble) {
    scrollLayout->removeWidget(aiBubble);
    aiBubble->hide();
  }

  auto removeLastMatch = [](ChatHistory& h, ChatMessage::Role role, const QString& content) {
    for (int i = static_cast<int>(h.size()) - 1; i >= 0; --i) {
      if (h[i].role == role && h[i].content == content) {
        h.erase(h.begin() + i);
        return;
      }
    }
  };

  if (userBubble) removeLastMatch(history_, ChatMessage::Role::User, userBubble->text());
  if (aiBubble) removeLastMatch(history_, ChatMessage::Role::Assistant, aiBubble->text());
}

void ChatWidget::addTurnToUI(int turnIndex, const QString& userText, const QString& assistantText)
{
  auto it = turnBubbles_.find(turnIndex);
  if (it != turnBubbles_.end()) {
    auto [userBubble, aiBubble] = it.value();
    if (userBubble) {
      scrollLayout->insertWidget(scrollLayout->count() - 1, userBubble);
      userBubble->show();
    }
    if (aiBubble) {
      scrollLayout->insertWidget(scrollLayout->count() - 1, aiBubble);
      aiBubble->show();
    }
  } else {
    auto *userBubble = addMessage(userText, true);
    auto *aiBubble = addMessage(assistantText, false);
    turnBubbles_[turnIndex] = {userBubble, aiBubble};
  }

  history_.push_back({ChatMessage::Role::User, userText});
  history_.push_back({ChatMessage::Role::Assistant, assistantText});
  scrollToBottom();
}

bool ChatWidget::isDarkTheme() const
{
  QPalette pal = QApplication::palette();
  return pal.color(QPalette::Window).lightness() < 128;
}

QString ChatWidget::extractCode(const QString& text) const
{
  static QRegularExpression codeBlock("```(?:\\w+)?\\n([\\s\\S]*?)```");
  auto match = codeBlock.match(text);
  if (match.hasMatch()) {
    return match.captured(1).trimmed();
  }
  return text;
}
