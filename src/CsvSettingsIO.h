// CsvSettingsIO — persistence for csvSettings(), mirroring the Windows
// SettingsBase ini format: "CSV Lint.ini" in the host plugins Config dir,
// sections per category ([Analyze]/[Edit]/[General]/[UserDialogs]), keys
// sorted by name, booleans as True/False, Separators in \t-escaped form.
#pragma once
#include "NppPluginInterfaceLinux.h"

void csvSettingsIOInit(NppData *data);
/// Load "CSV Lint.ini" into csvSettings() (no-op if the file doesn't exist).
void csvSettingsLoad(void);
/// Write csvSettings() back to "CSV Lint.ini".
void csvSettingsSave(void);

#ifdef __cplusplus
#include <string>
/// Set TwoDigitYearMaxStr + resolved TwoDigitYearMax ("CurrentYear" handling).
void csvSettingsApplyTwoDigitYearMax(const std::string &val);
/// Separators chars <-> "\t"-escaped display form (Settings.cs parity).
std::string csvSettingsEscapeSeparators(const std::string &chars);
std::string csvSettingsUnescapeSeparators(const std::string &escaped);
#endif
