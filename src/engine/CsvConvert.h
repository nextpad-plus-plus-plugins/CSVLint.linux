// CsvConvert — port of CsvEdit.cs ConvertToSQL/XML/JSON. Host-free: takes
// document text + definition + options, returns the generated script.
#pragma once
#include <string>
#include <vector>
#include "CsvDefinition.h"

struct CsvConvertOptions {
    int         sqlDialect = 0;    // 0=MySQL/MariaDB 1=MS-SQL 2=PostgreSQL
    int         batchSize  = 1000; // SQL insert batch size
    std::string tableName;         // empty = derive from file name
    std::string fileNameNoExt;     // for the default table name + ScriptInfo
    std::vector<std::string> commentLines;   // ScriptInfo header lines
};

namespace CsvConvert {

/// CREATE TABLE + batched INSERTs + comment scaffolding.
/// NOTE (C# parity): may downgrade Integer enum columns to String in `csvdef`.
std::string ToSQL(const std::string &text, CsvDefinition &csvdef,
                  const CsvConvertOptions &opt);

std::string ToXML(const std::string &text, CsvDefinition &csvdef,
                  const CsvConvertOptions &opt);

std::string ToJSON(const std::string &text, CsvDefinition &csvdef,
                   const CsvConvertOptions &opt);

}  // namespace CsvConvert
