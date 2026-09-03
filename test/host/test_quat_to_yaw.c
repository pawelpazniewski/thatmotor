#include "quat_to_yaw.h"

#include "unity.h"

/* Q14 fixed-point helpers for the test fixtures. */
#define Q14_ONE 16384       /* 1.0 in Q14 */
#define Q14_SIN45 11585     /* sin(45 deg) = 0.70711 * 16384 ~= 11585 */
#define Q14_COS45 11585     /* cos(45 deg) = 0.70711 * 16384 ~= 11585 */

/* Q14 precision tolerance: +-1.0 deg expressed in deg10. */
#define TOL_DEG10 10

void test_identity_quaternion_yields_zero_heading(void)
{
    /* Arrange: identity (0,0,0,1) = no rotation. */
    /* Act */
    uint16_t deg10 = quat_to_yaw_deg10(0, 0, 0, Q14_ONE);

    /* Assert: yaw ~= 0. */
    TEST_ASSERT_INT_WITHIN(TOL_DEG10, 0, (int)deg10);
}

void test_ninety_degree_z_rotation_yields_90(void)
{
    /* Arrange: rotation of +90 deg about Z -> w=cos45, k=sin45. */
    /* Act */
    uint16_t deg10 = quat_to_yaw_deg10(0, 0, Q14_SIN45, Q14_COS45);

    /* Assert: yaw ~= 90.0 deg = 900 deg10. */
    TEST_ASSERT_INT_WITHIN(TOL_DEG10, 900, (int)deg10);
}

void test_one_eighty_degree_z_rotation_yields_180(void)
{
    /* Arrange: rotation of 180 deg about Z -> k=1.0, w=0. */
    /* Act */
    uint16_t deg10 = quat_to_yaw_deg10(0, 0, Q14_ONE, 0);

    /* Assert: yaw ~= 180.0 deg = 1800 deg10. */
    TEST_ASSERT_INT_WITHIN(TOL_DEG10, 1800, (int)deg10);
}

void test_negative_ninety_degree_z_rotation_wraps_to_270(void)
{
    /* Arrange: rotation of -90 deg about Z -> w=cos45, k=-sin45. atan2 returns
     * -90 deg which must wrap to +270 deg (the normalisation oracle). */
    /* Act */
    uint16_t deg10 = quat_to_yaw_deg10(0, 0, -Q14_SIN45, Q14_COS45);

    /* Assert: yaw ~= 270.0 deg = 2700 deg10. */
    TEST_ASSERT_INT_WITHIN(TOL_DEG10, 2700, (int)deg10);
}

void test_result_always_in_range_for_negative_atan2(void)
{
    /* Arrange: a clearly negative-yaw quaternion (-90) must never produce a
     * value outside [0, 3599] nor a negative wrap. */
    /* Act */
    uint16_t deg10 = quat_to_yaw_deg10(0, 0, -Q14_SIN45, Q14_COS45);

    /* Assert: normalised into the valid range. */
    TEST_ASSERT_TRUE(deg10 <= 3599);
    TEST_ASSERT_TRUE(deg10 >= 2690); /* near 270, definitely positive */
}

void test_compass_mirror_zero_yaw_is_ninety(void)
{
    /* Arrange/Act: yaw=0 (facing world East, math convention) is compass 90
     * deg -- kills both the identity mutant (would give 0) and the earlier,
     * wrong 0-axis mirror (would also give 0, since 0 is that mirror's fixed
     * point). */
    uint16_t deg10 = yaw_to_compass_heading_deg10(0);

    /* Assert */
    TEST_ASSERT_EQUAL_UINT16(900, deg10);
}

void test_compass_mirror_ninety_yaw_is_zero(void)
{
    /* Arrange/Act: yaw=90 deg (facing world North, math convention) is
     * compass 0 -- kills the earlier 0-axis mirror (would give 270, not 0). */
    uint16_t deg10 = yaw_to_compass_heading_deg10(900);

    /* Assert */
    TEST_ASSERT_EQUAL_UINT16(0, deg10);
}

void test_compass_mirror_axis_is_its_own_fixed_point(void)
{
    /* Arrange/Act: 45 deg is the reflection axis, so it maps to itself --
     * kills a mutant that reflects around any other axis (e.g. 0). */
    uint16_t deg10 = yaw_to_compass_heading_deg10(450);

    /* Assert */
    TEST_ASSERT_EQUAL_UINT16(450, deg10);
}

void test_compass_mirror_reverses_rotation_sense(void)
{
    /* Arrange: two yaw readings 10 deg apart (a CCW math-yaw increase, the
     * raw sensor's own convention). Act */
    uint16_t low = yaw_to_compass_heading_deg10(1000);
    uint16_t high = yaw_to_compass_heading_deg10(1100);

    /* Assert: compass heading must move the OPPOSITE way (DECREASE) -- this
     * is the exact on-hardware symptom: turning the hull one way showed up as
     * the other way in the raw yaw. An unmirrored (or non-reflecting) pass-
     * through would make `high` >= `low`, failing this. */
    TEST_ASSERT_TRUE(high < low);
    TEST_ASSERT_EQUAL_UINT16(100, (uint16_t)(low - high));
}

void test_compass_mirror_result_always_in_range(void)
{
    /* Arrange/Act: an input just past the axis must wrap forward into range,
     * not go negative (yaw=1 -> 900-1=899, no wrap needed, but yaw=2700
     * exercises the negative branch: 900-2700=-1800 -> wraps to 1800). */
    uint16_t below_axis = yaw_to_compass_heading_deg10(1);
    uint16_t wraps = yaw_to_compass_heading_deg10(2700);

    /* Assert */
    TEST_ASSERT_EQUAL_UINT16(899, below_axis);
    TEST_ASSERT_TRUE(wraps <= 3599);
    TEST_ASSERT_EQUAL_UINT16(1800, wraps);
}

void run_quat_to_yaw_tests(void)
{
    RUN_TEST(test_identity_quaternion_yields_zero_heading);
    RUN_TEST(test_ninety_degree_z_rotation_yields_90);
    RUN_TEST(test_one_eighty_degree_z_rotation_yields_180);
    RUN_TEST(test_negative_ninety_degree_z_rotation_wraps_to_270);
    RUN_TEST(test_result_always_in_range_for_negative_atan2);
    RUN_TEST(test_compass_mirror_zero_yaw_is_ninety);
    RUN_TEST(test_compass_mirror_ninety_yaw_is_zero);
    RUN_TEST(test_compass_mirror_axis_is_its_own_fixed_point);
    RUN_TEST(test_compass_mirror_reverses_rotation_sense);
    RUN_TEST(test_compass_mirror_result_always_in_range);
}
