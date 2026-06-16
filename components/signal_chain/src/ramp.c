#include "ramp.h"

#include <stdint.h>

/* Move `current` up toward `target` (target > current) by at most `step`. */
static int32_t step_up(int32_t current, int32_t target, int32_t step)
{
    int32_t next = current + step;
    return next > target ? target : next;
}

/* Move `current` down toward `target` (target < current) by at most `step`. */
static int32_t step_down(int32_t current, int32_t target, int32_t step)
{
    int32_t next = current - step;
    return next < target ? target : next;
}

int32_t ramp_step(int32_t current, int32_t target, int32_t rate_up,
                  int32_t rate_down)
{
    if (target > current) {
        return step_up(current, target, rate_up);
    }
    if (target < current) {
        return step_down(current, target, rate_down);
    }
    return current;
}

int32_t slew_step(int32_t current, int32_t target, int32_t rate)
{
    return ramp_step(current, target, rate, rate);
}
