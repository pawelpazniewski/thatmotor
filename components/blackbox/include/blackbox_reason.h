#pragma once

#include <stdbool.h>

#include "blackbox_record.h" /* blackbox_end_reason */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Infer why a spot-lock/goto session dropped to OFF, for the tail samples'
 * end_reason. Pure decision (host-tested): the recorder computes the booleans
 * from the snapshot + active params and calls this.
 *
 * Priority mirrors the control loop's failsafe precedence: a lost-RC failsafe or
 * an operator disarm outranks any in-ARMED override; and within ARMED a manual
 * stick beats a CH3 preempt beats a goto that merely ended (cancel / arrival).
 *
 * @param is_failsafe    Control state is FAILSAFE (RC lost).
 * @param is_armed       Control state is ARMED (still armed but hold dropped).
 * @param sticks_neutral Both control sticks are within their neutral bands.
 * @param ch3_high       The CH3 spot-lock switch reads high (engaged/preempt).
 * @param was_goto       The dropped session was app-goto owned (vs CH3 hold).
 * @return The inferred end reason.
 */
blackbox_end_reason blackbox_end_reason_decide(bool is_failsafe, bool is_armed,
                                               bool sticks_neutral, bool ch3_high,
                                               bool was_goto);

#ifdef __cplusplus
}
#endif
