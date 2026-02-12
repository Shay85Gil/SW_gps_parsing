/*
 * dedup.cpp — Temporal and spatial deduplication of NMEA records
 */

#include "dedup.h"

#include <cmath>
#include <map>
#include <string>

std::vector<NmeaRecord>
dedup_last_write_wins(const std::vector<NmeaRecord>& records)
{
    // Ordered map keeps timestamps sorted chronologically (lexicographic on
    // HHMMSS.sss strings works because the format is fixed-width).
    std::map<std::string, NmeaRecord> seen;
    for (const auto& r : records)
        seen[r.timestamp] = r;  // last write wins

    std::vector<NmeaRecord> result;
    result.reserve(seen.size());
    for (auto& [key, rec] : seen)
        result.push_back(std::move(rec));
    return result;
}

std::vector<NmeaRecord>
dedup_spatial(const std::vector<NmeaRecord>& records, double epsilon)
{
    if (records.empty()) return {};

    std::vector<NmeaRecord> result;
    result.reserve(records.size());
    result.push_back(records.front());

    for (std::size_t i = 1; i < records.size(); ++i) {
        const auto& prev = result.back();
        const auto& cur  = records[i];
        if (std::fabs(cur.latitude  - prev.latitude)  > epsilon ||
            std::fabs(cur.longitude - prev.longitude) > epsilon) {
            result.push_back(cur);
        }
    }
    return result;
}
