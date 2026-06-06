#pragma once

#include <QUndoCommand>
#include <QString>
#include <functional>

class ChatTurnCommand : public QUndoCommand
{
public:
  ChatTurnCommand(int turnIndex,
                  const QString& userText,
                  const QString& assistantText,
                  const QString& previousEditorContent,
                  const QString& appliedCode,
                  std::function<void(int, const QString&, const QString&)> addFn,
                  std::function<void(int)> removeFn,
                  std::function<void(const QString&)> setEditorFn,
                  std::function<void()> renderFn)
    : turnIndex_(turnIndex), userText_(userText), assistantText_(assistantText),
      previousEditorContent_(previousEditorContent), appliedCode_(appliedCode),
      addFn_(std::move(addFn)), removeFn_(std::move(removeFn)),
      setEditorFn_(std::move(setEditorFn)), renderFn_(std::move(renderFn))
  {
    setText("Chat turn");
  }

  void undo() override
  {
    removeFn_(turnIndex_);
    setEditorFn_(previousEditorContent_);
    renderFn_();
  }

  void redo() override
  {
    if (firstRedo_) {
      firstRedo_ = false;
      return;
    }
    addFn_(turnIndex_, userText_, assistantText_);
    setEditorFn_(appliedCode_);
    renderFn_();
  }

  void updateAssistantText(const QString& text) { assistantText_ = text; }
  void updateAppliedCode(const QString& code) { appliedCode_ = code; }
  int turnIndex() const { return turnIndex_; }

private:
  int turnIndex_;
  QString userText_;
  QString assistantText_;
  QString previousEditorContent_;
  QString appliedCode_;
  std::function<void(int, const QString&, const QString&)> addFn_;
  std::function<void(int)> removeFn_;
  std::function<void(const QString&)> setEditorFn_;
  std::function<void()> renderFn_;
  bool firstRedo_ = true;
};
