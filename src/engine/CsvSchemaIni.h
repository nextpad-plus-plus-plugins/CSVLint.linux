// CsvSchemaIni — port of CsvLint/CsvSchemaIni.cs: read/write the data file's
// sibling schema.ini, one "[filename]" section per data file.
#pragma once
#include <string>
#include <vector>

namespace CsvSchemaIni {

/// Key/value pairs from the "[file]" section of schema.ini next to
/// `filePath`, in file order; empty when there is no schema.ini / section.
std::vector<std::pair<std::string, std::string>> ReadIniSection(const std::string &filePath);

/// Same section as raw ini lines joined \r\n (ready for CsvDefinition::FromIniLines).
std::string ReadIniSectionLines(const std::string &filePath);

/// Replace or append the "[file]" section with `inikeys` (the GetIniLines
/// output). Returns false + errmsg when the file cannot be written.
bool WriteIniSection(const std::string &filePath, const std::string &inikeys,
                     std::string &errmsg);

}  // namespace CsvSchemaIni
