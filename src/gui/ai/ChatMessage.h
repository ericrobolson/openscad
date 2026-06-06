#pragma once

#include <QString>
#include <vector>

struct ChatMessage {
  enum class Role { System, User, Assistant };
  Role role;
  QString content;
};

using ChatHistory = std::vector<ChatMessage>;
