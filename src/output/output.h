/*
 * output.h — gps_data_t population and route URL generation
 */

#ifndef OUTPUT_H
#define OUTPUT_H

#include "nmea_parser/nmea_parser.h"
#include "gpsd/gpsd.h"

#include <string>
#include <vector>

/// Populate a gps_data_t from an NmeaRecord.
gps_data_t to_gps_data(const NmeaRecord& r);

/// Build a Google Maps directions URL from an ordered list of waypoints.
std::string build_google_maps_url(const std::vector<NmeaRecord>& route);

#endif // OUTPUT_H
