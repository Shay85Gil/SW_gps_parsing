/*
 * nmea_parser.cpp — NMEA sentence validation and $GPRMC extraction
 */

#include "nmea_parser.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// ─── Internal constants ─────────────────────────────────────────────────────

static constexpr double      kKnotsToMps = 0.514444;
static constexpr std::size_t kMaxFields  = 20;

// ─── Internal helpers ───────────────────────────────────────────────────────

/// Split a string by a single-character delimiter.
static std::vector<std::string> split(const std::string& s, char delim)
{
    std::vector<std::string> tokens;
    tokens.reserve(kMaxFields);
    std::string::size_type start = 0;
    std::string::size_type end   = 0;
    while ((end = s.find(delim, start)) != std::string::npos) {
        tokens.emplace_back(s, start, end - start);
        start = end + 1;
    }
    tokens.emplace_back(s, start);
    return tokens;
}

/// Convert NMEA coordinate format DDMM.MMMM(…) to decimal degrees.
static double nmea_to_decimal(const std::string& raw, char hem)
{
    if (raw.empty()) return std::nan("");

    auto dot = raw.find('.');
    if (dot == std::string::npos || dot < 2)
        return std::nan("");

    double degrees = 0.0;
    double minutes = 0.0;
    try {
        degrees = std::stod(raw.substr(0, dot - 2));
        minutes = std::stod(raw.substr(dot - 2));
    } catch (...) {
        return std::nan("");
    }

    double dd = degrees + minutes / 60.0;
    if (hem == 'S' || hem == 'W')
        dd = -dd;
    return dd;
}

// ─── Public API ─────────────────────────────────────────────────────────────

ChecksumResult verify_checksum(const std::string& sentence)
{
    if (sentence.empty() || sentence[0] != '$')
        return ChecksumResult::kIncomplete;

    auto star = sentence.rfind('*');
    if (star == std::string::npos || star + 3 > sentence.size())
        return ChecksumResult::kIncomplete;

    uint8_t computed = 0;
    for (std::size_t i = 1; i < star; ++i)
        computed ^= static_cast<uint8_t>(sentence[i]);

    unsigned expected = 0;
    if (std::sscanf(sentence.c_str() + star + 1, "%02X", &expected) != 1)
        return ChecksumResult::kIncomplete;

    return (computed == static_cast<uint8_t>(expected))
               ? ChecksumResult::kOk
               : ChecksumResult::kMismatch;
}

bool is_not_relevant(const std::string& sentence)
{
    auto comma = sentence.find(',');
    if (comma == std::string::npos)
        return false;

    auto id = sentence.substr(0, comma);
    return id == "$GPGSA" || id == "$GPGGA" ||
           id == "$GNGSA" || id == "$GNGGA";
}

bool parse_gprmc(const std::string& sentence, NmeaRecord& out)
{
    auto star = sentence.rfind('*');
    std::string body = (star != std::string::npos) ? sentence.substr(0, star)
                                                   : sentence;

    auto fields = split(body, ',');

    if (fields.size() < 8)
        return false;

    if (fields[0] != "$GPRMC" && fields[0] != "$GNRMC")
        return false;

    if (fields[2].empty() || fields[2][0] != 'A')
        return false;

    if (fields[1].empty())
        return false;

    if (fields[4].size() != 1 || fields[6].size() != 1)
        return false;

    double lat = nmea_to_decimal(fields[3], fields[4][0]);
    double lon = nmea_to_decimal(fields[5], fields[6][0]);
    if (std::isnan(lat) || std::isnan(lon))
        return false;

    double speed_knots = 0.0;
    if (!fields[7].empty()) {
        try {
            speed_knots = std::stod(fields[7]);
        } catch (...) {
            speed_knots = 0.0;
        }
    }

    out.timestamp = fields[1];
    out.latitude  = lat;
    out.longitude = lon;
    out.speed_mps = speed_knots * kKnotsToMps;
    return true;
}
