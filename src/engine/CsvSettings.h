// CsvSettings — plain-struct port of Tools/Settings.cs (defaults from its
// DefaultValue attributes). Persistence is host-side; the engine only reads.
#pragma once
#include <string>
#include <ctime>

struct CsvSettings {
    // Analyze
    char        CommentCharacter   = '#';
    int         DecimalDigitsMax   = 20;
    bool        DecimalLeadingZero = true;
    int         ErrorTolerance     = 1;       // percent (persisted form)
    float       ErrorTolerancePerc = 0.01f;   // ErrorTolerance=1 (percent) -> 0.01
    int         IntegerDigitsMax   = 12;
    int         UniqueValuesMax    = 15;
    int         YearMaximum        = 2050;
    int         YearMinimum        = 1900;
    // Edit
    int         ReformatQuotes     = 0;       // 0=minimal 1=spaces 2=strings 3=non-numeric 4=all
    bool        TrimValues         = true;
    int         TwoDigitYearMax    = 0;       // 0 = "CurrentYear" (resolved in ctor)
    std::string TwoDigitYearMaxStr = "CurrentYear";  // persisted form ("CurrentYear" or a year)
    // Dialog persistence ("UserDialogs" ini section)
    bool        AutoDetectColumns = true;  // panel "auto-matic" checkbox
    int         DataConvertType  = 0;    // 0=SQL 1=XML 2=JSON
    std::string DataConvertName;         // table name ("" = from file name)
    int         DataConvertSQL   = 0;    // 0=MySQL 1=MS-SQL 2=PostgreSQL
    int         DataConvertBatch = 1000;
    int         MetadataType     = 0;    // 0=ini 1=json 2=datadict 3=py 4=R 5=ps
    // General
    int         AutoSyntaxLimit    = 1024 * 1024;
    char        DefaultQuoteChar   = '"';
    std::string NullKeyword        = "NaN";
    bool        SeparatorColor     = false;
    std::string Separators         = ",;\t|"; // _charSeparators (",;\\t|" unescaped)
    bool        TransparentCursor  = true;

    CsvSettings() {
        std::time_t t = std::time(nullptr);
        std::tm tmv{};
        localtime_r(&t, &tmv);
        TwoDigitYearMax = tmv.tm_year + 1900;  // "CurrentYear" default
    }
};

/// Engine-wide settings instance (the plugin fills it from persisted config).
CsvSettings &csvSettings();
