// CsvGenerateCode — port of CsvLint/CsvGenerateCode.cs: metadata and script
// generators (schema.ini, W3C-style schema JSON, data dictionary CSV, Python
// pandas, R, PowerShell). Host-free: options in, generated text out.
#pragma once
#include <string>
#include <vector>
#include "CsvDefinition.h"

struct CsvGenerateOptions {
    std::string filePath;       // full path of the data file
    std::string fileName;       // last path component
    std::string fileDir;        // directory (no trailing slash)
    std::vector<std::string> commentLines;   // ScriptInfo header lines
    int exampleYear = 2026;     // used in commented filter examples
};

namespace CsvGenerateCode {
std::string SchemaIni(CsvDefinition &csvdef, const CsvGenerateOptions &opt);
std::string SchemaJSON(CsvDefinition &csvdef, const CsvGenerateOptions &opt);
std::string DatadictionaryCSV(CsvDefinition &csvdef, const CsvGenerateOptions &opt);
std::string PythonPanda(CsvDefinition &csvdef, const CsvGenerateOptions &opt);
std::string RScript(CsvDefinition &csvdef, const CsvGenerateOptions &opt);
std::string PowerShell(CsvDefinition &csvdef, const CsvGenerateOptions &opt);
}  // namespace CsvGenerateCode
