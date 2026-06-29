#include "geo_math.h"

#include <math.h>

/* Mean Earth radius (WGS-84 sphere approximation), metres. */
#define EARTH_RADIUS_M 6371000.0

#define DEG_TO_RAD (M_PI / 180.0)
#define RAD_TO_DEG (180.0 / M_PI)

/* GPS coordinates arrive as degrees * 1e7. */
#define E7_PER_DEG 1.0e7

/* Metres travelled per degree of latitude on the reference sphere
 * (R * pi / 180). One degree of longitude is this scaled by cos(latitude). */
#define METERS_PER_DEG_LAT (EARTH_RADIUS_M * DEG_TO_RAD)

#define DEG_FULL_TURN 360.0
#define DEG10_PER_DEG 10.0
#define DEG10_FULL_TURN 3600

geo_offset geo_offset_m(int32_t lat_e7, int32_t lon_e7, int32_t ref_lat_e7,
                        int32_t ref_lon_e7)
{
    double d_lat_deg = ((double)lat_e7 - (double)ref_lat_e7) / E7_PER_DEG;
    double d_lon_deg = ((double)lon_e7 - (double)ref_lon_e7) / E7_PER_DEG;
    double ref_lat_rad = ((double)ref_lat_e7 / E7_PER_DEG) * DEG_TO_RAD;

    geo_offset off;
    off.north_m = (float)(d_lat_deg * METERS_PER_DEG_LAT);
    off.east_m = (float)(d_lon_deg * METERS_PER_DEG_LAT * cos(ref_lat_rad));
    return off;
}

float geo_distance_m(geo_offset off)
{
    return (float)hypot((double)off.north_m, (double)off.east_m);
}

uint16_t geo_bearing_deg10(geo_offset off)
{
    /* No direction for a zero vector: define it as due north (0). */
    if (off.north_m == 0.0f && off.east_m == 0.0f) {
        return 0;
    }

    /* atan2(east, north): 0 rad = north, +pi/2 = east (clockwise compass). */
    double bearing_rad = atan2((double)off.east_m, (double)off.north_m);
    double bearing_deg = bearing_rad * RAD_TO_DEG;
    if (bearing_deg < 0.0) {
        bearing_deg += DEG_FULL_TURN; /* wrap atan2's [-180,180) into [0,360) */
    }

    int deg10 = (int)lround(bearing_deg * DEG10_PER_DEG);
    if (deg10 >= DEG10_FULL_TURN) {
        deg10 -= DEG10_FULL_TURN; /* guard the rounding boundary at 360.0 */
    }
    if (deg10 < 0) {
        deg10 += DEG10_FULL_TURN;
    }
    return (uint16_t)deg10;
}
