// CsvValidate — port of CsvLint/CsvValidate.cs: validate data against a
// CsvDefinition and report errors ("** error line N: Column X value ...").
#pragma once
#include <string>
#include <vector>
#include "CsvDefinition.h"
#include "TextReader.h"

struct LogLine {
    std::string Message;
    int LineNumber;   // -1 = no line
    int Severity;     // -1 info, 0 warning, 1 error
    LogLine(std::string msg, int linenr, int sev)
        : Message(std::move(msg)), LineNumber(linenr), Severity(sev) {}
};

class CsvValidate {
public:
    /// Validate all data; `elapsed` (e.g. "00:00:00.123") is appended to the
    /// summary line by the caller since the engine stays clock-free.
    void ValidateData(TextReader &strdata, CsvDefinition &csvdef, const std::string &elapsed);

    std::string EvaluateDataValue(std::string val, const CsvColumn &coldef, int idx);
    bool EvaluateInteger(std::string val);
    bool EvaluateDecimal(std::string val, const CsvColumn &coldef, std::string &err);
    bool EvaluateDateTime(const std::string &val, const CsvColumn &coldef, std::string &err);

    std::string Report() const;

    const std::vector<LogLine> &log() const { return _log; }

private:
    std::vector<LogLine> _log;
};
