#include "blackbox_sampler.h"

/* A substate is "off" only for the OFF value; ACTIVE and PAUSED are both live
 * (a session spans a contiguous non-OFF run and includes PAUSED, R2). */
static bool is_off(uint8_t substate)
{
    return substate == BLACKBOX_SPOT_LOCK_OFF;
}

blackbox_sampler_action blackbox_sampler_decide(uint8_t prev_substate,
                                                uint8_t cur_substate)
{
    bool prev_off = is_off(prev_substate);
    bool cur_off = is_off(cur_substate);

    if (prev_off && !cur_off) {
        return BLACKBOX_ACTION_START_SESSION;
    }
    if (!prev_off && !cur_off) {
        return BLACKBOX_ACTION_SAMPLE;
    }
    if (!prev_off && cur_off) {
        return BLACKBOX_ACTION_CLOSE_SESSION;
    }
    return BLACKBOX_ACTION_IDLE;
}
