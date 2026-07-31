// CsvLintDialogs — app-modal parameter dialogs, ports of the WinForms
// dialogs: SortForm, ColumnSplitForm ("Add column"), ReformatForm,
// DetectColumnsForm (manual detect), ColumnsSelectForm. Each returns true on
// OK with the out-parameters filled (ShowDialog parity).
#pragma once
#include <string>
#include <vector>
#include "engine/CsvDefinition.h"

/// Shared synchronous alert / OK-Cancel confirm (NSAlert runModal parity).
void csvAlert(const std::string &title, const std::string &message);
bool csvConfirm(const std::string &title, const std::string &message);

/// About box (OK / Visit GitHub buttons, NSAlert two-button parity);
/// returns true when the user chose "Visit GitHub".
bool csvDlgAbout(const std::string &title, const std::string &body);

/// SortForm: column + ascending/descending + on value/length.
bool csvDlgSort(const CsvDefinition &csvdef, int &sortColumn, bool &sortAscending,
                bool &sortValue);

/// ColumnSplitForm: operation code 1=Pad 2=Search&Replace 3=Valid/Invalid
/// 4=Split on character 5=Split on position.
bool csvDlgColumnSplit(const CsvDefinition &csvdef, int &splitCode, int &splitColumn,
                       std::string &param1, std::string &param2, bool &removeOrg);

/// ReformatForm.
bool csvDlgReformat(const CsvDefinition &csvdef, std::string &newSeparator,
                    bool &updateSeparator, std::string &newDateTime,
                    std::string &newDecimal, std::string &replaceCrLf, bool &alignVert);

/// DetectColumnsForm (manual detection parameters).
bool csvDlgDetectColumns(char &sep, std::string &widths, bool &header, int &skip,
                         char &comm);

/// ColumnsSelectForm: ordered selection of column indexes.
bool csvDlgSelectColumns(const CsvDefinition &csvdef, std::vector<int> &selIdx);

/// DataConvertForm: type 0=SQL 1=XML 2=JSON + table name + SQL dialect+batch.
bool csvDlgDataConvert(int &convertType, std::string &tableName, int &sqlDialect,
                       int &batchSize);

/// MetaDataGenerateForm: 0=schema.ini 1=schema JSON 2=datadictionary CSV
/// 3=Python 4=R-script 5=PowerShell.
bool csvDlgGenerateMetadata(int &metadataType);

/// Settings window (Windows property-grid parity: Analyze/Edit/General).
/// Applies to csvSettings() on OK; returns true when the user confirmed.
bool csvDlgSettings(void);
