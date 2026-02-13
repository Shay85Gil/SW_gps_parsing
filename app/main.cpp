/*
 * main.cpp — GNSS NMEA-file processor (C++17, production-grade)
 *
 * Thin orchestrator: reads NMEA files, delegates to nmea_parser for
 * validation/extraction, dedup for filtering, and output for presentation.
 *
 * Build:  make            (Linux / MinGW on Windows)
 * Usage:  ./nmea_parser file1.nmea [file2.nmea …]
 */

#include "nmea_parser.h"
#include "dedup.h"
#include "output.h"
#include "gpsd_config.h"
#include "gpsd.h"

#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

// ─── Display formatting constants ──────────────────────────────────────────

static constexpr int kDisplayPrecision = 6;
static constexpr int kColIndex         = 6;
static constexpr int kColCoord         = 14;
static constexpr int kSeparatorWidth   = 44;

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <file.nmea> [file2.nmea …]\n";
        return 1;
    }

    // ── Stage 1: Read & parse ───────────────────────────────────────────
    std::vector<GpsRecord> all_records;
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

            // Classify known-but-unsupported sentence types.
            if (is_not_relevant(line)) {
                ++not_relevant;
                continue;
            }

            // Pass 2 — field validation & extraction.
            GpsRecord rec{};
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

    // ── Stage 3: Populate gpsd structs & print ──────────────────────────
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

    std::cout << std::fixed << std::setprecision(kDisplayPrecision);
    std::cout << "=== Route Points ===\n";
    std::cout << std::left
              << std::setw(kColIndex) << "#"
              << std::setw(kColCoord) << "Latitude"
              << std::setw(kColCoord) << "Longitude"
              << "Speed (m/s)\n";
    std::cout << std::string(kSeparatorWidth, '-') << '\n';

    for (std::size_t i = 0; i < route.size(); ++i) {
        // Access lat/lon from ntrip_stream_t, speed from gps_device_t
        const ntrip_stream_t& ns = route[i].stream;
        gps_device_t dev{};
        dev.gpsdata.fix.speed = route[i].gpsdata.fix.speed;

        std::cout << std::setw(kColIndex) << (i + 1)
                  << std::setw(kColCoord) << ns.latitude
                  << std::setw(kColCoord) << ns.longitude
                  << dev.gpsdata.fix.speed << '\n';
    }

    std::cout << "\n=== Google Maps URL ===\n"
              << build_google_maps_url(route) << '\n';

    return 0;
}
