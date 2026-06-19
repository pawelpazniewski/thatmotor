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

void run_quat_to_yaw_tests(void)
{
    RUN_TEST(test_identity_quaternion_yields_zero_heading);
    RUN_TEST(test_ninety_degree_z_rotation_yields_90);
    RUN_TEST(test_one_eighty_degree_z_rotation_yields_180);
    RUN_TEST(test_negative_ninety_degree_z_rotation_wraps_to_270);
    RUN_TEST(test_result_always_in_range_for_negative_atan2);
}
