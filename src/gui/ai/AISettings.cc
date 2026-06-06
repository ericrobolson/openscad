#include "gui/ai/AISettings.h"
#include "platform/PlatformUtils.h"
#include <QDir>
#include <QFile>

namespace AISettings {

QString settingsFilePath()
{
  QString configPath = QString::fromStdString(PlatformUtils::userConfigPath());
  if (configPath.isEmpty()) {
    configPath = QDir::homePath() + "/.openscad";
  }
  QDir().mkpath(configPath);
  return configPath + "/ai_settings.json";
}

nlohmann::json read()
{
  QFile file(settingsFilePath());
  if (!file.open(QIODevice::ReadOnly)) {
    return nlohmann::json::object();
  }
  QByteArray data = file.readAll();
  file.close();
  auto j = nlohmann::json::parse(data.constData(), nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    return nlohmann::json::object();
  }
  return j;
}

void write(const nlohmann::json& j)
{
  QFile file(settingsFilePath());
  if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    std::string s = j.dump(4);
    file.write(s.c_str(), s.length());
    file.close();
  }
}

nlohmann::json activeProfile()
{
  nlohmann::json settings = read();
  std::string active = settings.value("activeProfile", "");
  if (active.empty()) return nlohmann::json::object();
  nlohmann::json profiles = settings.value("profiles", nlohmann::json::object());
  return profiles.value(active, nlohmann::json::object());
}

QString activeParam(const QString& key, const QString& defaultVal)
{
  nlohmann::json prof = activeProfile();
  nlohmann::json params = prof.value("params", nlohmann::json::object());
  std::string k = key.toStdString();
  if (!params.contains(k)) return defaultVal;
  auto& val = params[k];
  if (val.is_string()) return QString::fromStdString(val.get<std::string>());
  if (val.is_number_integer()) return QString::number(val.get<int>());
  if (val.is_number_float()) return QString::number(val.get<double>());
  if (val.is_boolean()) return val.get<bool>() ? "true" : "false";
  return defaultVal;
}

bool isConfigured()
{
  nlohmann::json prof = activeProfile();
  std::string command = prof.value("command", "");
  if (!command.empty()) return true;
  std::string endpoint = prof.value("endpoint", "");
  std::string apiKey = prof.value("apiKey", "");
  return !endpoint.empty() && !apiKey.empty();
}

} // namespace AISettings
