// CsvEdit — port of CsvLint/CsvEdit.cs edit operations. Host-free: every
// operation takes the document text (+ the editor's EOL string) and returns
// the new document text; the plugin applies it via SCI_SETTEXT.
// (ConvertToSQL/XML/JSON port in the convert-data milestone.)
#pragma once
#include <string>
#include <vector>
#include "CsvDefinition.h"

namespace CsvEdit {

/// "value with spaces" -> "value_with_spaces" (script identifiers).
std::string StringToVariable(std::string strinput);

/// ReformatDataFile: change separator / datetime mask / decimal symbol,
/// replace value-embedded line breaks, optionally align columns vertically.
std::string ReformatDataFile(const std::string &text, CsvDefinition &csvdef,
                             const std::string &reformatSeparator, bool updateSeparator,
                             const std::string &reformatDatTime, const std::string &reformatDecimal,
                             const std::string &replaceCrLf, bool align,
                             const std::string &CRLF);

/// SortData on column: ascending/descending, by value or by value length.
/// Fills `outError` (sort index out of bounds) instead of showing UI.
std::string SortData(const std::string &text, CsvDefinition &csvdef,
                     int SortIdx, bool AscDesc, bool OnValue,
                     const std::string &CRLF, std::string *outError);

/// ColumnSplit / add column. SplitCode: 1=Pad 2=Search&Replace
/// 3=Split valid/invalid 4=Split on character 5=Split on position.
/// `outNewDef` receives the definition with the inserted/removed columns.
std::string ColumnSplit(const std::string &text, CsvDefinition &csvdef,
                        int ColumnIndex, int SplitCode,
                        const std::string &Parameter1, const std::string &Parameter2,
                        bool bRemove, const std::string &CRLF, CsvDefinition &outNewDef);

/// SelectColumns: keep/rearrange `sel_idx`; `outNewDef` = the new definition.
std::string SelectColumns(const std::string &text, CsvDefinition &csvdef,
                          const std::vector<int> &sel_idx, const std::string &CRLF,
                          CsvDefinition &outNewDef);

}  // namespace CsvEdit

/// Format a parsed date back through a C#-style mask (yyyy/yy, MM/M, dd/d,
/// HH/H, mm, ss, fff + literals) — DateTime.ToString(mask) parity for the
/// masks this plugin generates.
std::string formatDateTime(const struct ParsedDateTime &dt, const std::string &mask);
