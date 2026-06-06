#pragma once

#include <QString>
#include "json/json.hpp"

namespace AISettings {

QString settingsFilePath();
nlohmann::json read();
void write(const nlohmann::json& j);

nlohmann::json activeProfile();
QString activeParam(const QString& key, const QString& defaultVal = {});
bool isConfigured();

} // namespace AISettings
