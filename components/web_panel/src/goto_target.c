#include "goto_target.h"

bool goto_target_valid(int32_t lat_e7, int32_t lon_e7)
{
    bool lat_ok = lat_e7 >= GOTO_LAT_E7_MIN && lat_e7 <= GOTO_LAT_E7_MAX;
    bool lon_ok = lon_e7 >= GOTO_LON_E7_MIN && lon_e7 <= GOTO_LON_E7_MAX;
    return lat_ok && lon_ok;
}
