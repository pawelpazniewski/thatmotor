#include "blackbox_reason.h"

blackbox_end_reason blackbox_end_reason_decide(bool is_failsafe, bool is_armed,
                                               bool sticks_neutral, bool ch3_high,
                                               bool was_goto)
{
    if (is_failsafe) {
        return BLACKBOX_END_FAILSAFE; /* RC lost: failsafe outranks everything */
    }
    if (!is_armed) {
        return BLACKBOX_END_DISARM; /* operator disarmed (or deploy/calib) */
    }
    /* Still ARMED but the hold dropped: a manual override took over. */
    if (!sticks_neutral) {
        return BLACKBOX_END_STICK; /* either stick moved off neutral */
    }
    if (ch3_high) {
        return BLACKBOX_END_CH3; /* CH3 preempt / switch still engaged */
    }
    if (was_goto) {
        return BLACKBOX_END_GOTO_CANCEL; /* app goto ended (cancel / arrival) */
    }
    return BLACKBOX_END_OTHER;
}
