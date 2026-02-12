/*
 * main.cpp — GNSS NMEA-file processor (C++17, production-grade)
 *
 * Reads one or more NMEA files supplied on the command line, extracts $GPRMC
 * sentences through a two-pass validation pipeline, deduplicates points with
 * last-write-wins + spatial-epsilon filtering, populates gps_data_t, and
 * emits a coordinate table plus a Google Maps directions URL.
 *
 * Build:  make            (Linux / MinGW on Windows)
 * Usage:  ./nmea_parser file1.nmea [file2.nmea …]
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "gpsd.h"

// ─── Configuration ──────────────────────────────────────────────────────────

// Spatial deduplication epsilon in decimal degrees.
// ~1e-5 deg ≈ 1.1 m at the equator — filters GPS jitter while preserving
// any meaningful movement.
static constexpr double kSpatialEpsilon = 1e-5;

// NMEA speed is in knots; convert to m/s.
static constexpr double kKnotsToMps = 0.514444;

// Maximum fields we expect in any supported sentence.
static constexpr std::size_t kMaxFields = 20;

// ─── Helpers ────────────────────────────────────────────────────────────────

/// Split a string by a single-character delimiter into a pre-sized vector.
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
///   @param raw   e.g. "4807.038" or "01131.000"
///   @param hem   'N', 'S', 'E', or 'W'
///   @return decimal degrees (+N/+E, −S/−W), or NaN on failure.
static double nmea_to_decimal(const std::string& raw, char hem)
{
    if (raw.empty()) return std::nan("");

    // Find the decimal point to locate the boundary between degrees+minutes.
    auto dot = raw.find('.');
    if (dot == std::string::npos || dot < 2)
        return std::nan("");

    // Everything before (dot-2) is degrees; the rest is minutes.
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

// ─── Pass-1: Checksum verification ─────────────────────────────────────────

/// Tri-state result for checksum verification.
enum class ChecksumResult {
    kOk,            // well-formed sentence, checksum matches
    kIncomplete,    // structurally incomplete (missing '$', '*', or hex digits)
    kMismatch       // well-formed but the checksum value doesn't match
};

/// Verify the NMEA checksum.  Expected format: $....*HH
/// Distinguishes between structurally incomplete lines and lines whose
/// checksum simply doesn't match, so the caller can bucket them separately.
static ChecksumResult verify_checksum(const std::string& sentence)
{
    if (sentence.empty() || sentence[0] != '$')
        return ChecksumResult::kIncomplete;

    auto star = sentence.rfind('*');
    if (star == std::string::npos || star + 3 > sentence.size())
        return ChecksumResult::kIncomplete;

    uint8_t computed = 0;
    for (std::size_t i = 1; i < star; ++i)
        computed ^= static_cast<uint8_t>(sentence[i]);

    // Parse the two hex characters after '*'.
    unsigned expected = 0;
    if (std::sscanf(sentence.c_str() + star + 1, "%02X", &expected) != 1)
        return ChecksumResult::kIncomplete;

    return (computed == static_cast<uint8_t>(expected))
               ? ChecksumResult::kOk
               : ChecksumResult::kMismatch;
}

// ─── Pass-2: Field-level validation & extraction ────────────────────────────

/// Intermediate record produced by a successful parse.
struct NmeaRecord {
    std::string timestamp;  // HHMMSS.sss — the unique key
    double      latitude;   // decimal degrees
    double      longitude;  // decimal degrees
    double      speed_mps;  // metres per second
};

/// Parse a $GPRMC sentence that already passed the checksum test.
/// Returns true and fills `out` on success; false otherwise.
///
/// $GPRMC reference (field indices 0-based after split on ','):
///   0  – sentence ID ($GPRMC)
///   1  – UTC time  (HHMMSS.sss)
///   2  – Status    ('A' = active, 'V' = void)
///   3  – Latitude  (DDMM.MMMM)
///   4  – N/S
///   5  – Longitude (DDDMM.MMMM)
///   6  – E/W
///   7  – Speed over ground (knots)
///   8  – Track angle (degrees, unused here)
///   9  – Date      (DDMMYY)
///   10 – Magnetic variation (optional)
///   11 – E/W of mag var (optional)
///   12 – Mode indicator / checksum tail (optional in older firmware)
static bool parse_gprmc(const std::string& sentence, NmeaRecord& out)
{
    // Strip the checksum tail (*HH) before splitting so the last real field
    // is not polluted.
    auto star = sentence.rfind('*');
    std::string body = (star != std::string::npos) ? sentence.substr(0, star)
                                                   : sentence;

    auto fields = split(body, ',');

    // Minimum field count: we need indices 0–7 at least.
    if (fields.size() < 8)
        return false;

    // Sentence ID check.
    if (fields[0] != "$GPRMC" && fields[0] != "$GNRMC")
        return false;

    // Status must be 'A' (active fix).
    if (fields[2].empty() || fields[2][0] != 'A')
        return false;

    // Timestamp must be present.
    if (fields[1].empty())
        return false;

    // Hemisphere indicators must be single characters.
    if (fields[4].size() != 1 || fields[6].size() != 1)
        return false;

    double lat = nmea_to_decimal(fields[3], fields[4][0]);
    double lon = nmea_to_decimal(fields[5], fields[6][0]);
    if (std::isnan(lat) || std::isnan(lon))
        return false;

    // Speed (knots → m/s).  Missing speed is treated as zero.
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

// ─── Deduplication ──────────────────────────────────────────────────────────

/// Apply last-write-wins on the timestamp key.
/// Input order is preserved — later entries silently overwrite earlier ones.
static std::vector<NmeaRecord>
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

/// Remove spatially-duplicate points (jitter suppression).
/// A point is kept only if it is farther than `epsilon` degrees from the
/// previously kept point in either latitude or longitude.
static std::vector<NmeaRecord>
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

// ─── Output helpers ─────────────────────────────────────────────────────────

/// Populate a gps_data_t from an NmeaRecord.
static gps_data_t to_gps_data(const NmeaRecord& r)
{
    gps_data_t d{};
    d.set            = GPS_SET_LATLON | GPS_SET_SPEED;
    d.fix.latitude   = r.latitude;
    d.fix.longitude  = r.longitude;
    d.fix.speed      = r.speed_mps;
    d.fix.mode       = MODE_2D;
    d.status         = 1;  // valid fix
    return d;
}

/// Build a Google Maps directions URL from an ordered list of waypoints.
/// Format: https://www.google.com/maps/dir/lat1,lon1/lat2,lon2/…
static std::string build_google_maps_url(const std::vector<NmeaRecord>& route)
{
    if (route.empty()) return {};

    std::ostringstream url;
    url << std::fixed << std::setprecision(6);
    url << "https://www.google.com/maps/dir";
    for (const auto& pt : route)
        url << '/' << pt.latitude << ',' << pt.longitude;
    return url.str();
}

// ─── Main ───────────────────────────────────────────────────────────────────

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <file.nmea> [file2.nmea …]\n";
        return 1;
    }

    // ── Stage 1: Read & parse ───────────────────────────────────────────
    std::vector<NmeaRecord> all_records;
    std::size_t lines_total    = 0;
    std::size_t checksum_fail  = 0;
    std::size_t not_relevant   = 0;
    std::size_t parse_fail     = 0;

    for (int argi = 1; argi < argc; ++argi) {
        std::ifstream ifs(argv[argi]);
        if (!ifs) {
            std::cerr << "Warning: cannot open '" << argv[argi]
                      << "', skipping.\n";
            continue;
        }

        std::string line;
        while (std::getline(ifs, line)) {
            // Strip trailing CR (files may use \r\n line endings).
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                continue;

            ++lines_total;

            // Pass 1 — checksum.
            auto cs_result = verify_checksum(line);
            if (cs_result == ChecksumResult::kIncomplete) {
                ++parse_fail;
                continue;
            }
            if (cs_result == ChecksumResult::kMismatch) {
                ++checksum_fail;
                continue;
            }

            // Classify known-but-unsupported sentence types before full
            // parsing so they get their own counter instead of inflating
            // the parse-failure number.
            {
                auto comma = line.find(',');
                if (comma != std::string::npos) {
                    auto id = line.substr(0, comma);
                    if (id == "$GPGSA" || id == "$GPGGA" ||
                        id == "$GNGSA" || id == "$GNGGA") {
                        ++not_relevant;
                        continue;
                    }
                }
            }

            // Pass 2 — field validation & extraction.
            NmeaRecord rec;
            if (!parse_gprmc(line, rec)) {
                ++parse_fail;
                continue;
            }

            all_records.push_back(std::move(rec));
        }
    }

    // ── Stage 2: Deduplication ──────────────────────────────────────────
    auto deduped = dedup_last_write_wins(all_records);
    auto route   = dedup_spatial(deduped, kSpatialEpsilon);

    // ── Stage 3: Populate gps_data_t & print ────────────────────────────
    std::cout << "=== Processing Summary ===\n"
              << "  Total lines read     : " << lines_total << '\n'
              << "  Checksum failures    : " << checksum_fail << '\n'
              << "  Not relevant (skipped): " << not_relevant << '\n'
              << "  Parse/validation fail: " << parse_fail << '\n'
              << "  Valid records parsed : " << all_records.size() << '\n'
              << "  After timestamp dedup: " << deduped.size() << '\n'
              << "  After spatial dedup  : " << route.size() << '\n'
              << '\n';

    if (route.empty()) {
        std::cout << "No valid GPS points found.\n";
        return 0;
    }

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "=== Route Points ===\n";
    std::cout << std::left
              << std::setw(6)  << "#"
              << std::setw(14) << "Latitude"
              << std::setw(14) << "Longitude"
              << "Speed (m/s)\n";
    std::cout << std::string(44, '-') << '\n';

    for (std::size_t i = 0; i < route.size(); ++i) {
        gps_data_t gd = to_gps_data(route[i]);

        double lat = 0.0, lon = 0.0, spd = 0.0;
        gps_get_latlon(&gd, &lat, &lon);
        gps_get_speed_mps(&gd, &spd);

        std::cout << std::setw(6)  << (i + 1)
                  << std::setw(14) << lat
                  << std::setw(14) << lon
                  << spd << '\n';
    }

    std::cout << "\n=== Google Maps URL ===\n"
              << build_google_maps_url(route) << '\n';

    return 0;
}
