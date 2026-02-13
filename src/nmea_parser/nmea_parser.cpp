/*
 * nmea_parser.cpp — NMEA sentence validation and $GPRMC extraction
 */

#include "nmea_parser.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// ─── NMEA protocol constants ────────────────────────────────────────────────

static constexpr char        kNmeaStart       = '$';
static constexpr char        kNmeaChecksumSep = '*';
static constexpr char        kNmeaFieldDelim  = ',';
static constexpr std::size_t kChecksumHexLen  = 2;  // two hex digits after '*'

// ─── GPRMC field indices (0-based, after splitting on ',') ──────────────────

static constexpr std::size_t kFieldSentenceId = 0;
static constexpr std::size_t kFieldTime       = 1;
static constexpr std::size_t kFieldStatus     = 2;
static constexpr std::size_t kFieldLat        = 3;
static constexpr std::size_t kFieldNS         = 4;
static constexpr std::size_t kFieldLon        = 5;
static constexpr std::size_t kFieldEW         = 6;
static constexpr std::size_t kFieldSpeed      = 7;
static constexpr std::size_t kGprmcMinFields  = kFieldSpeed + 1;  // need at least 8

// ─── Sentence identifiers ──────────────────────────────────────────────────

static constexpr const char* kIdGPRMC = "$GPRMC";
static constexpr const char* kIdGNRMC = "$GNRMC";
static constexpr const char* kIdGPGSA = "$GPGSA";
static constexpr const char* kIdGPGGA = "$GPGGA";
static constexpr const char* kIdGNGSA = "$GNGSA";
static constexpr const char* kIdGNGGA = "$GNGGA";

// ─── Parsing constants ─────────────────────────────────────────────────────

static constexpr char   kStatusActive     = 'A';   // RMC status: Active (valid fix)
static constexpr char   kHemSouth         = 'S';
static constexpr char   kHemWest          = 'W';
static constexpr double kKnotsToMps       = 0.514444;
static constexpr double kMinutesPerDegree = 60.0;
static constexpr std::size_t kMinuteDigitWidth = 2;  // "MM" portion before decimal
static constexpr std::size_t kHemFieldLen = 1;        // single-char hemisphere field

// ─── Internal limits ────────────────────────────────────────────────────────

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
    if (dot == std::string::npos || dot < kMinuteDigitWidth)
        return std::nan("");

    double degrees = 0.0;
    double minutes = 0.0;
    try {
        degrees = std::stod(raw.substr(0, dot - kMinuteDigitWidth));
        minutes = std::stod(raw.substr(dot - kMinuteDigitWidth));
    } catch (...) {
        return std::nan("");
    }

    double dd = degrees + minutes / kMinutesPerDegree;
    if (hem == kHemSouth || hem == kHemWest)
        dd = -dd;
    return dd;
}

// ─── Public API ─────────────────────────────────────────────────────────────

ChecksumResult verify_checksum(const std::string& sentence)
{
    if (sentence.empty() || sentence[0] != kNmeaStart)
        return ChecksumResult::kIncomplete;

    auto star = sentence.rfind(kNmeaChecksumSep);
    if (star == std::string::npos ||
        star + 1 + kChecksumHexLen > sentence.size())
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
    auto comma = sentence.find(kNmeaFieldDelim);
    if (comma == std::string::npos)
        return false;

    auto id = sentence.substr(0, comma);
    return id == kIdGPGSA || id == kIdGPGGA ||
           id == kIdGNGSA || id == kIdGNGGA;
}

bool parse_gprmc(const std::string& sentence, GpsRecord& out)
{
    auto star = sentence.rfind(kNmeaChecksumSep);
    std::string body = (star != std::string::npos) ? sentence.substr(0, star)
                                                   : sentence;

    auto fields = split(body, kNmeaFieldDelim);

    if (fields.size() < kGprmcMinFields)
        return false;

    if (fields[kFieldSentenceId] != kIdGPRMC &&
        fields[kFieldSentenceId] != kIdGNRMC)
        return false;

    if (fields[kFieldStatus].empty() ||
        fields[kFieldStatus][0] != kStatusActive)
        return false;

    if (fields[kFieldTime].empty())
        return false;

    if (fields[kFieldNS].size() != kHemFieldLen ||
        fields[kFieldEW].size() != kHemFieldLen)
        return false;

    double lat = nmea_to_decimal(fields[kFieldLat], fields[kFieldNS][0]);
    double lon = nmea_to_decimal(fields[kFieldLon], fields[kFieldEW][0]);
    if (std::isnan(lat) || std::isnan(lon))
        return false;

    double speed_knots = 0.0;
    if (!fields[kFieldSpeed].empty()) {
        try {
            speed_knots = std::stod(fields[kFieldSpeed]);
        } catch (...) {
            speed_knots = 0.0;
        }
    }

    out.timestamp          = fields[kFieldTime];
    out.stream.latitude    = lat;
    out.stream.longitude   = lon;
    out.gpsdata.fix.speed  = speed_knots * kKnotsToMps;
    return true;
}
