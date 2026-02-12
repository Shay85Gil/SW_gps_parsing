/*
 * output.cpp — gps_data_t population and route URL generation
 */

#include "output.h"

#include <iomanip>
#include <sstream>

gps_data_t to_gps_data(const NmeaRecord& r)
{
    gps_data_t d{};
    d.set            = LATLON_SET | SPEED_SET;
    d.fix.latitude   = r.latitude;
    d.fix.longitude  = r.longitude;
    d.fix.speed      = r.speed_mps;
    d.fix.mode       = MODE_2D;
    d.status         = 1;  // valid fix
    return d;
}

std::string build_google_maps_url(const std::vector<NmeaRecord>& route)
{
    if (route.empty()) return {};

    std::ostringstream url;
    url << std::fixed << std::setprecision(6);
    url << "https://www.google.com/maps/dir";
    for (const auto& pt : route)
        url << '/' << pt.latitude << ',' << pt.longitude;
    return url.str();
}
