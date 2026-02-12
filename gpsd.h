/* gpsd.h - minimal sample API for lat/lon/speed extraction
 *
 * This is a simplified, gpsd-inspired header intended for:
 *  - consumers that only need latitude, longitude, and speed
 *  - a validity bitmask ("set") to avoid trusting missing fields
 *
 * Units:
 *  - latitude/longitude: degrees (WGS84), +N/+E, -S/-W
 *  - speed: meters/second (m/s)
 */

#ifndef SAMPLE_GPSD_H
#define SAMPLE_GPSD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Bitmask type indicating which fields in gps_data_t are valid */
typedef uint64_t gps_mask_t;

/* Field validity flags (set bits in gps_data_t.set) */
enum {
    GPS_SET_NONE   = 0,
    GPS_SET_LATLON = (1ULL << 0),   /* fix.latitude & fix.longitude valid */
    GPS_SET_SPEED  = (1ULL << 1),   /* fix.speed valid */
    GPS_SET_TIME   = (1ULL << 2),   /* fix.time valid (optional) */
    GPS_SET_MODE   = (1ULL << 3),   /* fix.mode valid (optional) */
};

/* Fix modes (common convention) */
typedef enum {
    MODE_NO_FIX = 1,   /* no valid fix */
    MODE_2D     = 2,   /* lat/lon valid */
    MODE_3D     = 3    /* lat/lon/alt valid (alt omitted in this minimal header) */
} fix_mode_t;

/* Navigation solution / current fix */
typedef struct gps_fix_t {
    double time;       /* seconds since Unix epoch or another consistent timescale */
    double latitude;   /* degrees */
    double longitude;  /* degrees */
    double speed;      /* m/s */
    fix_mode_t mode;   /* MODE_NO_FIX / MODE_2D / MODE_3D */
} gps_fix_t;

/* Top-level container returned by a reader (gpsd client, NMEA parser, etc.) */
typedef struct gps_data_t {
    gps_mask_t set;    /* OR of GPS_SET_* flags indicating valid fields */
    gps_fix_t  fix;    /* most recent fix */
    int        status; /* optional: receiver status (implementation-defined) */
} gps_data_t;

/* Helper: check if lat/lon are valid */
static inline int gps_has_latlon(const gps_data_t *d) {
    return (d && ((d->set & GPS_SET_LATLON) != 0) && (d->fix.mode >= MODE_2D));
}

/* Helper: check if speed is valid */
static inline int gps_has_speed(const gps_data_t *d) {
    return (d && ((d->set & GPS_SET_SPEED) != 0) && (d->fix.mode >= MODE_2D));
}

/* Helper: safe getters (return 0 on success, -1 if not available) */
static inline int gps_get_latlon(const gps_data_t *d, double *lat, double *lon) {
    if (!gps_has_latlon(d) || !lat || !lon) return -1;
    *lat = d->fix.latitude;
    *lon = d->fix.longitude;
    return 0;
}

static inline int gps_get_speed_mps(const gps_data_t *d, double *speed_mps) {
    if (!gps_has_speed(d) || !speed_mps) return -1;
    *speed_mps = d->fix.speed;
    return 0;
}

/* Optional conversion helpers */
static inline double gps_mps_to_kmh(double mps) { return mps * 3.6; }
static inline double gps_mps_to_knots(double mps) { return mps * 1.9438444924406048; }

#ifdef __cplusplus
}
#endif

#endif /* SAMPLE_GPSD_H */

