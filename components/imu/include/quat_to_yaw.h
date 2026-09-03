#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Convert a BNO085 Rotation Vector unit quaternion (Q14 fixed point: each
 * int16 component divided by 2^14 gives the real value) into a yaw / heading
 * in tenths of a degree, normalised to [0, 3599]. PURE: no IDF, no I/O, only
 * math.h. Host-testable.
 *
 * Yaw is taken about the Z axis:
 *   yaw = atan2(2*(real*k + i*j), 1 - 2*(j*j + k*k))
 * then wrapped into [0, 360) and scaled by 10.
 *
 * @param q_i     quaternion i component (Q14)
 * @param q_j     quaternion j component (Q14)
 * @param q_k     quaternion k component (Q14)
 * @param q_real  quaternion real (w) component (Q14)
 * @return heading in degrees * 10, in [0, 3599].
 */
uint16_t quat_to_yaw_deg10(int16_t q_i, int16_t q_j, int16_t q_k,
                           int16_t q_real);

/**
 * Convert a mathematical yaw (as returned by quat_to_yaw_deg10 -- CCW-positive
 * from the world X axis / East, per the BNO085 ENU Rotation Vector frame) into
 * compass-heading convention (CW-positive from North), matching
 * geo_bearing_deg10 and everything downstream that compares a heading against
 * a bearing. This is a REFLECTION around the 45 deg axis (equidistant between
 * East and North), i.e. compass = 90 - yaw_math (mod 360) -- NOT a plain
 * negation around 0/north. Confirmed on hardware: rotating the hull left
 * showed the raw yaw moving as if it were a RIGHT turn (a mirrored rotational
 * sense), and a single-point compass check happened to land near the 45 deg
 * axis, not near 0/180 -- ruling out a 0-axis mirror. PURE, host-testable.
 *
 * @param yaw_deg10  raw output of quat_to_yaw_deg10, degrees * 10, [0, 3599].
 * @return compass heading, degrees * 10, [0, 3599].
 */
uint16_t yaw_to_compass_heading_deg10(uint16_t yaw_deg10);

#ifdef __cplusplus
}
#endif
