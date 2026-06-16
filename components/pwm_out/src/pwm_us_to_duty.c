#include "pwm_us_to_duty.h"

#include <stdint.h>

uint32_t pwm_us_to_duty(uint32_t value_us)
{
    /* Round-to-nearest in integer math: (a + b/2) / b.
     * value_us * PWM_DUTY_MAX fits in uint64_t (max ~ 20000 * 65536). */
    uint64_t numerator = (uint64_t)value_us * (uint64_t)PWM_DUTY_MAX;
    uint64_t duty = (numerator + (PWM_PERIOD_US / 2U)) / PWM_PERIOD_US;
    return (uint32_t)duty;
}
