/*
 * nmea_parser.h — NMEA sentence validation and $GPRMC extraction
 */

#ifndef NMEA_PARSER_H
#define NMEA_PARSER_H

#include "gpsd_config.h"
#include "gpsd.h"

#include <string>
#include <vector>

/// GPS record using gpsd types for coordinate/speed storage.
///   - ntrip_stream_t holds latitude and longitude
///   - gps_data_t     holds speed (via .fix.speed)
///   - timestamp       is the HHMMSS.sss dedup key
struct GpsRecord {
    std::string    timestamp;  // HHMMSS.sss — the unique dedup key
    ntrip_stream_t stream;     // .latitude, .longitude (decimal degrees)
    gps_data_t     gpsdata;    // .fix.speed (metres per second)
};

/// Tri-state result for checksum verification.
enum class ChecksumResult {
    kOk,            // well-formed sentence, checksum matches
    kIncomplete,    // structurally incomplete (missing '$', '*', or hex digits)
    kMismatch       // well-formed but the checksum value doesn't match
};

/// Pass 1 — verify NMEA checksum ($…*HH).
/// Distinguishes structurally incomplete lines from genuine checksum mismatches.
ChecksumResult verify_checksum(const std::string& sentence);

/// Return true if the sentence ID is a known-but-unsupported type
/// ($GPGGA, $GPGSA and their $GN multi-constellation variants).
bool is_not_relevant(const std::string& sentence);

/// Pass 2 — parse a $GPRMC / $GNRMC sentence that already passed the
/// checksum.  Returns true and fills `out` on success; false otherwise.
bool parse_gprmc(const std::string& sentence, GpsRecord& out);

#endif // NMEA_PARSER_H
