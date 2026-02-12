/*
 * nmea_parser.h — NMEA sentence validation and $GPRMC extraction
 */

#ifndef NMEA_PARSER_H
#define NMEA_PARSER_H

#include <string>
#include <vector>

/// Intermediate record produced by a successful parse.
struct NmeaRecord {
    std::string timestamp;  // HHMMSS.sss — the unique dedup key
    double      latitude;   // decimal degrees
    double      longitude;  // decimal degrees
    double      speed_mps;  // metres per second
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
bool parse_gprmc(const std::string& sentence, NmeaRecord& out);

#endif // NMEA_PARSER_H
