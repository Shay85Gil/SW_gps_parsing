/*
 * output.cpp — route URL generation
 */

#include "output.h"

#include <iomanip>
#include <sstream>

// ─── Output constants ──────────────────────────────────────────────────────

static constexpr int         kCoordPrecision   = 6;
static constexpr const char* kGoogleMapsBase   = "https://www.google.com/maps/dir";

std::string build_google_maps_url(const std::vector<GpsRecord>& route)
{
    if (route.empty()) return {};

    std::ostringstream url;
    url << std::fixed << std::setprecision(kCoordPrecision);
    url << kGoogleMapsBase;
    for (const auto& pt : route)
        url << '/' << pt.stream.latitude << ',' << pt.stream.longitude;
    return url.str();
}
