#include "goto_target.h"

#include <math.h>

bool goto_target_valid(int32_t lat_e7, int32_t lon_e7)
{
    bool lat_ok = lat_e7 >= GOTO_LAT_E7_MIN && lat_e7 <= GOTO_LAT_E7_MAX;
    bool lon_ok = lon_e7 >= GOTO_LON_E7_MIN && lon_e7 <= GOTO_LON_E7_MAX;
    return lat_ok && lon_ok;
}

bool goto_target_from_double(double lat_d, double lon_d, int32_t *lat_e7,
                             int32_t *lon_e7)
{
    if (!isfinite(lat_d) || !isfinite(lon_d)) {
        return false; /* INF/NaN: casting to int32 would be undefined behavior */
    }
    if (lat_d < GOTO_LAT_E7_MIN || lat_d > GOTO_LAT_E7_MAX ||
        lon_d < GOTO_LON_E7_MIN || lon_d > GOTO_LON_E7_MAX) {
        return false; /* out of range in double domain, before any wrap on cast */
    }
    *lat_e7 = (int32_t)lat_d; /* now guaranteed to fit int32: no wrap */
    *lon_e7 = (int32_t)lon_d;
    return true;
}
