#include "gui/ai/AIDock.h"
#include "gui/ai/ChatWidget.h"
#include <QShowEvent>

AIDock::AIDock(QWidget *parent) : Dock(parent)
{
  chatWidget_ = new ChatWidget(this);
  setWidget(chatWidget_);
}

AIDock::~AIDock()
{
}

void AIDock::showEvent(QShowEvent *event)
{
  Dock::showEvent(event);
  chatWidget_->checkConfiguration();
}
