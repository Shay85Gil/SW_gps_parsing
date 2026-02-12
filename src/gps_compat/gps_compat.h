/*
 * gps_compat.h — Minimal self-contained subset of the gpsd public API (gps.h)
 *
 * Provides gps_data_t / gps_fix_t / gps_mask_t types and flag constants
 * using the same names and layout as the real gpsd public header, so code
 * written against this header is source-compatible with the full library.
 *
 * The real gpsd.h (daemon internals) is co-located in src/gpsd/ but
 * requires the complete gpsd build environment.  This header has zero
 * external dependencies beyond <stdint.h>.
 */

#ifndef GPS_COMPAT_H
#define GPS_COMPAT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Bitmask type indicating which fields in gps_data_t are valid */
typedef uint64_t gps_mask_t;

/* Field validity flags — names match the real gps.h */
#define TIME_SET        (1llu << 0)
#define MODE_SET        (1llu << 1)
#define LATLON_SET      (1llu << 4)
#define SPEED_SET       (1llu << 8)

/* Fix modes — values match the real gps.h */
#define MODE_NOT_SEEN   0
#define MODE_NO_FIX     1
#define MODE_2D         2
#define MODE_3D         3

/* Navigation fix */
struct gps_fix_t {
    double time;       /* seconds since Unix epoch */
    int    mode;       /* MODE_NOT_SEEN / MODE_NO_FIX / MODE_2D / MODE_3D */
    double latitude;   /* degrees (WGS84), +N / -S */
    double longitude;  /* degrees (WGS84), +E / -W */
    double speed;      /* m/s */
};

/* Top-level GPS data container */
struct gps_data_t {
    gps_mask_t       set;    /* OR of *_SET flags */
    struct gps_fix_t fix;    /* most recent fix */
    int              status; /* receiver status (implementation-defined) */
};

/* Conversion helpers */
static inline double gps_mps_to_kmh(double mps)   { return mps * 3.6; }
static inline double gps_mps_to_knots(double mps)  { return mps * 1.9438444924406048; }

#ifdef __cplusplus
}
#endif

#endif /* GPS_COMPAT_H */
