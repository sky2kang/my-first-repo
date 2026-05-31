/* SPDX-License-Identifier: MIT */
/* Tests for digital beamforming weight application (oru/beamform.h). */
#include "oru/beamform.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static double mag(oru_iq16_t s)
{
    return sqrt((double)s.i * s.i + (double)s.q * s.q);
}

static void test_unity_passthrough(void)
{
    bf_table_t t;
    assert(bf_init(&t, 2) == ORU_OK);

    /* both antennas unity (1.0 + j0) */
    bf_weight_t w[2] = { { BF_UNITY, 0 }, { BF_UNITY, 0 } };
    assert(bf_set_beam(&t, 5, w) == ORU_OK);

    oru_iq16_t in[4] = { {1000, -500}, {-2000, 800}, {300, 300}, {0, 0} };
    oru_iq16_t a0[4], a1[4];
    oru_iq16_t *out[BF_MAX_ANTENNAS] = { a0, a1, NULL, NULL };
    assert(bf_apply(&t, 5, in, 4, out) == ORU_OK);

    /* unity weight reproduces input within rounding (Q1.15 unity is 32767/32768) */
    for (int k = 0; k < 4; k++) {
        assert(abs(a0[k].i - in[k].i) <= 1);
        assert(abs(a0[k].q - in[k].q) <= 1);
        assert(abs(a1[k].i - in[k].i) <= 1);
    }
}

static void test_phase_preserves_magnitude(void)
{
    bf_table_t t;
    assert(bf_init(&t, 1) == ORU_OK);

    /* pure phase weight: j (0 + j1.0) -> rotates by 90 degrees */
    bf_weight_t w[1] = { { 0, BF_UNITY } };
    assert(bf_set_beam(&t, 1, w) == ORU_OK);

    oru_iq16_t in[1] = { {1000, 0} };
    oru_iq16_t a0[1];
    oru_iq16_t *out[BF_MAX_ANTENNAS] = { a0, NULL, NULL, NULL };
    assert(bf_apply(&t, 1, in, 1, out) == ORU_OK);

    /* (1000 + j0) * (j1) = (0 + j1000) */
    assert(abs(a0[0].i - 0) <= 1);
    assert(abs(a0[0].q - 1000) <= 1);
    /* magnitude preserved */
    assert(fabs(mag(a0[0]) - mag(in[0])) < 2.0);
}

static void test_steering_broadside(void)
{
    bf_table_t t;
    assert(bf_init(&t, 4) == ORU_OK);

    /* broadside (0 deg): all antennas get the same in-phase weight */
    assert(bf_set_steering_beam(&t, 10, 0.0) == ORU_OK);
    const bf_beam_t *b = bf_get_beam(&t, 10);
    assert(b && b->num_ant == 4);
    for (uint8_t a = 0; a < 4; a++) {
        assert(abs(b->w[a].re - BF_UNITY) <= 1);
        assert(abs(b->w[a].im) <= 1);
    }

    /* applying to a tone yields identical outputs on all antennas */
    oru_iq16_t in[2] = { {2000, 1000}, {-1000, 500} };
    oru_iq16_t a0[2], a1[2], a2[2], a3[2];
    oru_iq16_t *out[BF_MAX_ANTENNAS] = { a0, a1, a2, a3 };
    assert(bf_apply(&t, 10, in, 2, out) == ORU_OK);
    for (int k = 0; k < 2; k++) {
        assert(a0[k].i == a1[k].i && a1[k].i == a2[k].i && a2[k].i == a3[k].i);
        assert(a0[k].q == a1[k].q && a1[k].q == a2[k].q && a2[k].q == a3[k].q);
    }
}

static void test_steering_progressive_phase(void)
{
    bf_table_t t;
    assert(bf_init(&t, 4) == ORU_OK);

    /* 30 deg steering: per-antenna phase increment phi = pi*sin(30) = pi/2.
     * antenna 0 -> 0 rad, antenna 1 -> 90 deg, etc. */
    assert(bf_set_steering_beam(&t, 2, 30.0) == ORU_OK);
    const bf_beam_t *b = bf_get_beam(&t, 2);
    assert(b);

    /* antenna 0 is real unity; antenna 1 should be ~ +j (cos90,sin90) */
    assert(abs(b->w[0].re - BF_UNITY) <= 1 && abs(b->w[0].im) <= 1);
    assert(abs(b->w[1].re) <= 200 && abs(b->w[1].im - BF_UNITY) <= 200);
    /* all weights unit magnitude */
    for (uint8_t a = 0; a < 4; a++) {
        double m = sqrt((double)b->w[a].re * b->w[a].re +
                        (double)b->w[a].im * b->w[a].im);
        assert(fabs(m - BF_UNITY) < 64.0);
    }
}

static void test_unknown_beam(void)
{
    bf_table_t t;
    assert(bf_init(&t, 2) == ORU_OK);
    oru_iq16_t in[1] = { {1, 1} };
    oru_iq16_t a0[1], a1[1];
    oru_iq16_t *out[BF_MAX_ANTENNAS] = { a0, a1, NULL, NULL };
    assert(bf_apply(&t, 999, in, 1, out) == ORU_ERR_PARAM);
}

int main(void)
{
    test_unity_passthrough();
    test_phase_preserves_magnitude();
    test_steering_broadside();
    test_steering_progressive_phase();
    test_unknown_beam();
    printf("test_beamform: PASS\n");
    return 0;
}
