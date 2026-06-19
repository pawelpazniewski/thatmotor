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

#ifdef __cplusplus
}
#endif
