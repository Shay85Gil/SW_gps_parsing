/*
 * dedup.cpp — Temporal and spatial deduplication of GPS records
 */

#include "dedup.h"

#include <cmath>
#include <map>
#include <string>

std::vector<GpsRecord>
dedup_last_write_wins(const std::vector<GpsRecord>& records)
{
    // Ordered map keeps timestamps sorted chronologically (lexicographic on
    // HHMMSS.sss strings works because the format is fixed-width).
    std::map<std::string, GpsRecord> seen;
    for (const auto& r : records)
        seen[r.timestamp] = r;  // last write wins

    std::vector<GpsRecord> result;
    result.reserve(seen.size());
    for (auto& [key, rec] : seen)
        result.push_back(std::move(rec));
    return result;
}

std::vector<GpsRecord>
dedup_spatial(const std::vector<GpsRecord>& records, double epsilon)
{
    if (records.empty()) return {};

    std::vector<GpsRecord> result;
    result.reserve(records.size());
    result.push_back(records.front());

    for (std::size_t i = 1; i < records.size(); ++i) {
        const auto& prev = result.back();
        const auto& cur  = records[i];
        if (std::fabs(cur.stream.latitude  - prev.stream.latitude)  > epsilon ||
            std::fabs(cur.stream.longitude - prev.stream.longitude) > epsilon) {
            result.push_back(cur);
        }
    }
    return result;
}
