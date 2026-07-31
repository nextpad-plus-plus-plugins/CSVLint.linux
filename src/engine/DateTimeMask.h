// DateTimeMask — replacement for C# DateTime.TryParseExact with the custom
// masks CSVLint generates: tokens d/dd, M/MM, yy/yyyy, H/HH, mm, ss, fff and
// literal separator characters. Exact-match semantics: single-letter tokens
// accept 1-2 digits, double-letter exactly 2, yyyy exactly 4, fff exactly 3;
// the whole value must be consumed; the date must be a real calendar date.
// "yy" resolves against TwoDigitYearMax (C# Calendar.TwoDigitYearMax).
#pragma once
#include <string>
#include <cstdint>

struct ParsedDateTime {
    int year = 0, month = 1, day = 1, hour = 0, minute = 0, second = 0, millis = 0;
    bool hasDate = false;

    /// Sortable encoding for min/max tracking (C# DateTime comparison parity).
    int64_t sortKey() const {
        return (((((int64_t)year * 100 + month) * 100 + day) * 100 + hour) * 100 + minute)
               * 100000 + second * 1000 + millis;
    }
};

/// Parse `value` against `mask` exactly. Returns false on any mismatch.
bool tryParseDateTimeExact(const std::string &value, const std::string &mask,
                           int twoDigitYearMax, ParsedDateTime &out);
