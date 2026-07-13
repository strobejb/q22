#ifndef HEXEDIT_SETTINGS_APPCONFIG_H
#define HEXEDIT_SETTINGS_APPCONFIG_H

#include <QString>

namespace AppSettings {

QString appConfigDir();
QString settingsFilePath();
void migrateLegacyConfig();

}

#endif // HEXEDIT_SETTINGS_APPCONFIG_H
