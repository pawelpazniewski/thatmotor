#include "goto_grab.h"

/** Whether a sampled fix is inside the valid geographic range (garbage guard). */
static bool grab_in_range(int32_t lat_e7, int32_t lon_e7)
{
    bool lat_ok = lat_e7 >= GOTO_GRAB_LAT_E7_MIN && lat_e7 <= GOTO_GRAB_LAT_E7_MAX;
    bool lon_ok = lon_e7 >= GOTO_GRAB_LON_E7_MIN && lon_e7 <= GOTO_GRAB_LON_E7_MAX;
    return lat_ok && lon_ok;
}

goto_grab_decision goto_grab_decide(bool fresh, bool fix, int32_t lat_e7,
                                    int32_t lon_e7)
{
    goto_grab_decision d = {
        .engage = fresh && fix && grab_in_range(lat_e7, lon_e7),
        .lat_e7 = lat_e7,
        .lon_e7 = lon_e7,
    };
    return d;
}
