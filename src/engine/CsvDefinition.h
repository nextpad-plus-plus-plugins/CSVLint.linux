// CsvDefinition — port of CsvLint/CsvDefinition.cs: file + column metadata,
// ini-lines (schema.ini dialect) parse/serialize, and the CSV/fixed-width
// line parser the whole plugin runs on. Method names and logic follow the C#
// original so future upstream fixes can be diffed across.
#pragma once
#include <string>
#include <vector>
#include <map>
#include "CsvSettings.h"
#include "TextReader.h"

enum class CsvScanState { None = 0, QuickScan = 1, FullScan = 2, LoadIni = 3, TooBig = 99 };

enum class ColumnType { Unknown = 0, Integer = 1, Decimal = 2, String = 4, DateTime = 8 };

/// Digit-aware string compare (StrCmpLogicalW parity): digit runs compare as
/// numbers, so "fu_3mnd" < "fu_12mnd".
int naturalCompare(const std::string &a, const std::string &b);

struct CsvColumn {
    int         Index = 0;
    std::string Name;
    int         MaxWidth = 50;
    ColumnType  DataType = ColumnType::String;
    std::string Mask;
    char        DecimalSymbol = '.';
    int         Decimals = 0;
    std::string sTag;       // decimal: thousand+decimal chars ",." or ".,"
    int         iTag = -1;  // decimal: max decimal places
    bool        isCodedValue = false;
    std::vector<std::string> CodedList;

    explicit CsvColumn(int idx);
    CsvColumn(int idx, const std::string &name, int maxwidth, ColumnType datatype,
              const std::string &mask);

    void Initialize();
    void UpdateDateTimeMask(const std::string &newmask);
    /// `slcodes` in first-seen order (C# Dictionary enumeration parity).
    void AddCodedValues(const std::vector<std::pair<std::string, int>> &slcodes);
};

struct CsvDefinition {
    CsvScanState ScanState = CsvScanState::None;
    char         Separator = '\0';          // '\0' = fixed width
    std::string  DateTimeFormat;
    char         DecimalSymbol = '\0';
    int          NumberDigits = 0;
    bool         NumberLeadingZeros = true;
    bool         ColNameHeader = true;
    char         TextQualifier = '"';
    int          SkipLines = 0;
    char         CommentChar = '\0';
    std::vector<int>       FieldWidths;
    std::vector<CsvColumn> Fields;

    /// Line number bookkeeping during parsing (comments + quoted newlines).
    int ParseCurrentLine = 0;

    CsvDefinition() = default;
    explicit CsvDefinition(char separator) : Separator(separator) {}

    /// Parse ini lines from the docked window's metadata text box.
    /// On duplicate keys, fills `outError` and leaves the definition empty.
    static CsvDefinition FromIniLines(const std::string &inilines, std::string *outError);

    void AddColumn(const std::string &name = "Col", int maxwidth = 50,
                   ColumnType datatype = ColumnType::String, const std::string &mask = "");
    void AddColumn(int idx, const std::string &name, int maxwidth, ColumnType datatype,
                   const std::string &mask);
    void RemoveColumn(int index);

    std::string GetIniLines();

    /// "labvalue (2)" -> "labvalue", postfix 2; no parentheses -> postfix -1.
    static std::string SplitColumnNamePostfix(const std::string &namein, int *postfix);
    /// Unique name suggestion: returns the base name and, via `postfix`, the
    /// number to append ("labvalue (4)") or -1 when already unique.
    std::string GetUniqueColumnName(const std::string &fieldname, int *postfix) const;

    int SkipCommentLinesAtStart(TextReader &strdata);
    std::vector<std::string> ParseNextLine(TextReader &strdata, bool &iscomment);
    std::string ConstructHeader() const;
    std::string ConstructLine(const std::vector<std::string> &values, bool iscomment) const;
    std::string GetColumnWidths(bool abspos) const;

private:
    void CsvDefInitFromKeys(const std::vector<std::pair<std::string, std::string>> &inikeys);
};

/// CsvEdit.ApplyQuotesToString / RemoveQuotesToString (needed by the parser
/// and line construction; the rest of CsvEdit ports in the edit phase).
std::string ApplyQuotesToString(std::string strinput, char separator, ColumnType dataType);
std::string RemoveQuotesToString(const std::string &strinput);
