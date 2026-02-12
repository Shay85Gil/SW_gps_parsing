/*
 * dedup.h — Temporal and spatial deduplication of NMEA records
 */

#ifndef DEDUP_H
#define DEDUP_H

#include "nmea_parser.h"

#include <vector>

/// Spatial deduplication epsilon in decimal degrees.
/// ~1e-5 deg ≈ 1.1 m at the equator.
inline constexpr double kSpatialEpsilon = 1e-5;

/// Last-write-wins: keep only the final record for each unique timestamp.
/// Returns records sorted chronologically by timestamp.
std::vector<NmeaRecord>
dedup_last_write_wins(const std::vector<NmeaRecord>& records);

/// Spatial deduplication: suppress GPS jitter by dropping points closer than
/// `epsilon` degrees (in either axis) from the previously kept point.
std::vector<NmeaRecord>
dedup_spatial(const std::vector<NmeaRecord>& records, double epsilon);

#endif // DEDUP_H
