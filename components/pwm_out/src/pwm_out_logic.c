#include "pwm_out_logic.h"

#include <stdint.h>

#include "pwm_us_to_duty.h"
#include "safety_clamp.h"

PwmOutLogicResult pwm_out_resolve_duty(int channel, uint32_t value_us,
                                       PwmWindow window, uint32_t *out_duty)
{
    if (channel < 0 || channel >= PWM_OUT_LOGIC_CHANNEL_COUNT) {
        return PWM_OUT_LOGIC_INVALID_CHANNEL;
    }

    /* SI-3: clamp is unconditionally applied before conversion. There is no
     * branch that reaches *out_duty without passing through clamp_pwm_us. */
    uint32_t clamped_us = clamp_pwm_us(value_us, window);
    *out_duty = pwm_us_to_duty(clamped_us);
    return PWM_OUT_LOGIC_OK;
}
