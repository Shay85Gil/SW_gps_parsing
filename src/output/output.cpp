/*
 * output.cpp — gps_data_t population and route URL generation
 */

#include "output.h"

#include <iomanip>
#include <sstream>

// ─── Output constants ──────────────────────────────────────────────────────

static constexpr int         kCoordPrecision   = 6;
static constexpr int         kGpsStatusValid   = 1;
static constexpr const char* kGoogleMapsBase   = "https://www.google.com/maps/dir";

gps_data_t to_gps_data(const NmeaRecord& r)
{
    gps_data_t d{};
    d.set            = LATLON_SET | SPEED_SET;
    d.fix.latitude   = r.latitude;
    d.fix.longitude  = r.longitude;
    d.fix.speed      = r.speed_mps;
    d.fix.mode       = MODE_2D;
    d.status         = kGpsStatusValid;
    return d;
}

std::string build_google_maps_url(const std::vector<NmeaRecord>& route)
{
    if (route.empty()) return {};

    std::ostringstream url;
    url << std::fixed << std::setprecision(kCoordPrecision);
    url << kGoogleMapsBase;
    for (const auto& pt : route)
        url << '/' << pt.latitude << ',' << pt.longitude;
    return url.str();
}
