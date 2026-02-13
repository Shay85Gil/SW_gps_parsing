/*
 * output.h — GPS struct access and route URL generation
 */

#ifndef OUTPUT_H
#define OUTPUT_H

#include "nmea_parser.h"

#include <string>
#include <vector>

/// Build a Google Maps directions URL from an ordered list of waypoints.
std::string build_google_maps_url(const std::vector<GpsRecord>& route);

#endif // OUTPUT_H
