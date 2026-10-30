#include "wide.c"
#include <stdio.h>
TIDE_ROWS void probe_axis_chunk(tide_world *tide_w, const uint32_t tide_from, const uint32_t tide_to, const tide_entity *restrict tide_ent, const tide_entity *restrict tide_scn, const tide_rows_chunk *tide_selection, uint8_t *restrict tide_w3_0, uint8_t *restrict tide_w3_1, uint8_t *restrict tide_w3_2, uint8_t *restrict tide_w3_3, uint8_t *restrict tide_w3_4, uint8_t *restrict tide_w3_5, uint8_t *restrict tide_w3_6, uint8_t *restrict tide_w3_7, uint8_t *restrict tide_w3_8, uint8_t *restrict tide_w3_9, uint8_t *restrict tide_w3_10, uint8_t *restrict tide_w3_11)
{
    (void)tide_ent; (void)tide_scn;
    const uint16_t *tide_selected = tide_selection->selected;
    const float dt = tide_w->Time.dt;
    { const uint32_t base = tide_from, stop = tide_to;
        for (uint32_t pick = base; pick < stop; pick++) {
            const uint32_t i = tide_selected[pick];
            float p; memcpy(&p, tide_w3_0 + (size_t)i * 4u, 4u);
            float v; memcpy(&v, tide_w3_3 + (size_t)i * 4u, 4u);
            float push; memcpy(&push, tide_w3_6 + (size_t)i * 4u, 4u);
            float drag; memcpy(&drag, tide_w3_9 + (size_t)i * 4u, 4u);
            v = (v + push * dt) * (1.0f - drag * dt);
            p = p + v * dt;
            memcpy(tide_w3_0 + (size_t)i * 4u, &p, 4u);
            memcpy(tide_w3_3 + (size_t)i * 4u, &v, 4u);
        }
        for (uint32_t pick = base; pick < stop; pick++) {
            const uint32_t i = tide_selected[pick];
            float p; memcpy(&p, tide_w3_1 + (size_t)i * 4u, 4u);
            float v; memcpy(&v, tide_w3_4 + (size_t)i * 4u, 4u);
            float push; memcpy(&push, tide_w3_7 + (size_t)i * 4u, 4u);
            float drag; memcpy(&drag, tide_w3_9 + (size_t)i * 4u, 4u);
            v = (v + push * dt) * (1.0f - drag * dt);
            p = p + v * dt;
            float radius; memcpy(&radius, tide_w3_11 + (size_t)i * 4u, 4u);
            if (p < radius) {
                float bounce; memcpy(&bounce, tide_w3_10 + (size_t)i * 4u, 4u);
                p = radius * 2.0f - p; v = -v * bounce;
            }
            memcpy(tide_w3_1 + (size_t)i * 4u, &p, 4u);
            memcpy(tide_w3_4 + (size_t)i * 4u, &v, 4u);
        }
        for (uint32_t pick = base; pick < stop; pick++) {
            const uint32_t i = tide_selected[pick];
            float p; memcpy(&p, tide_w3_2 + (size_t)i * 4u, 4u);
            float v; memcpy(&v, tide_w3_5 + (size_t)i * 4u, 4u);
            float push; memcpy(&push, tide_w3_8 + (size_t)i * 4u, 4u);
            float drag; memcpy(&drag, tide_w3_9 + (size_t)i * 4u, 4u);
            v = (v + push * dt) * (1.0f - drag * dt);
            p = p + v * dt;
            memcpy(tide_w3_2 + (size_t)i * 4u, &p, 4u);
            memcpy(tide_w3_5 + (size_t)i * 4u, &v, 4u);
        }
    }
}
TIDE_ROWS void probe_axis16(tide_world *tide_w, const uint32_t tide_from, const uint32_t tide_to, const tide_entity *restrict tide_ent, const tide_entity *restrict tide_scn, const tide_rows_chunk *tide_selection, uint8_t *restrict tide_w3_0, uint8_t *restrict tide_w3_1, uint8_t *restrict tide_w3_2, uint8_t *restrict tide_w3_3, uint8_t *restrict tide_w3_4, uint8_t *restrict tide_w3_5, uint8_t *restrict tide_w3_6, uint8_t *restrict tide_w3_7, uint8_t *restrict tide_w3_8, uint8_t *restrict tide_w3_9, uint8_t *restrict tide_w3_10, uint8_t *restrict tide_w3_11)
{
    (void)tide_ent; (void)tide_scn;
    const uint16_t *tide_selected = tide_selection->selected;
    const float dt = tide_w->Time.dt;
    for (uint32_t base = tide_from; base < tide_to; base += 16u) {
        const uint32_t stop = tide_to - base < 16u ? tide_to : base + 16u;
        for (uint32_t pick = base; pick < stop; pick++) {
            const uint32_t i = tide_selected[pick];
            float p; memcpy(&p, tide_w3_0 + (size_t)i * 4u, 4u);
            float v; memcpy(&v, tide_w3_3 + (size_t)i * 4u, 4u);
            float push; memcpy(&push, tide_w3_6 + (size_t)i * 4u, 4u);
            float drag; memcpy(&drag, tide_w3_9 + (size_t)i * 4u, 4u);
            v = (v + push * dt) * (1.0f - drag * dt);
            p = p + v * dt;
            memcpy(tide_w3_0 + (size_t)i * 4u, &p, 4u);
            memcpy(tide_w3_3 + (size_t)i * 4u, &v, 4u);
        }
        for (uint32_t pick = base; pick < stop; pick++) {
            const uint32_t i = tide_selected[pick];
            float p; memcpy(&p, tide_w3_1 + (size_t)i * 4u, 4u);
            float v; memcpy(&v, tide_w3_4 + (size_t)i * 4u, 4u);
            float push; memcpy(&push, tide_w3_7 + (size_t)i * 4u, 4u);
            float drag; memcpy(&drag, tide_w3_9 + (size_t)i * 4u, 4u);
            v = (v + push * dt) * (1.0f - drag * dt);
            p = p + v * dt;
            float radius; memcpy(&radius, tide_w3_11 + (size_t)i * 4u, 4u);
            if (p < radius) {
                float bounce; memcpy(&bounce, tide_w3_10 + (size_t)i * 4u, 4u);
                p = radius * 2.0f - p; v = -v * bounce;
            }
            memcpy(tide_w3_1 + (size_t)i * 4u, &p, 4u);
            memcpy(tide_w3_4 + (size_t)i * 4u, &v, 4u);
        }
        for (uint32_t pick = base; pick < stop; pick++) {
            const uint32_t i = tide_selected[pick];
            float p; memcpy(&p, tide_w3_2 + (size_t)i * 4u, 4u);
            float v; memcpy(&v, tide_w3_5 + (size_t)i * 4u, 4u);
            float push; memcpy(&push, tide_w3_8 + (size_t)i * 4u, 4u);
            float drag; memcpy(&drag, tide_w3_9 + (size_t)i * 4u, 4u);
            v = (v + push * dt) * (1.0f - drag * dt);
            p = p + v * dt;
            memcpy(tide_w3_2 + (size_t)i * 4u, &p, 4u);
            memcpy(tide_w3_5 + (size_t)i * 4u, &v, 4u);
        }
    }
}
TIDE_ROWS void probe_axis64(tide_world *tide_w, const uint32_t tide_from, const uint32_t tide_to, const tide_entity *restrict tide_ent, const tide_entity *restrict tide_scn, const tide_rows_chunk *tide_selection, uint8_t *restrict tide_w3_0, uint8_t *restrict tide_w3_1, uint8_t *restrict tide_w3_2, uint8_t *restrict tide_w3_3, uint8_t *restrict tide_w3_4, uint8_t *restrict tide_w3_5, uint8_t *restrict tide_w3_6, uint8_t *restrict tide_w3_7, uint8_t *restrict tide_w3_8, uint8_t *restrict tide_w3_9, uint8_t *restrict tide_w3_10, uint8_t *restrict tide_w3_11)
{
    (void)tide_ent; (void)tide_scn;
    const uint16_t *tide_selected = tide_selection->selected;
    const float dt = tide_w->Time.dt;
    for (uint32_t base = tide_from; base < tide_to; base += 64u) {
        const uint32_t stop = tide_to - base < 64u ? tide_to : base + 64u;
        for (uint32_t pick = base; pick < stop; pick++) {
            const uint32_t i = tide_selected[pick];
            float p; memcpy(&p, tide_w3_0 + (size_t)i * 4u, 4u);
            float v; memcpy(&v, tide_w3_3 + (size_t)i * 4u, 4u);
            float push; memcpy(&push, tide_w3_6 + (size_t)i * 4u, 4u);
            float drag; memcpy(&drag, tide_w3_9 + (size_t)i * 4u, 4u);
            v = (v + push * dt) * (1.0f - drag * dt);
            p = p + v * dt;
            memcpy(tide_w3_0 + (size_t)i * 4u, &p, 4u);
            memcpy(tide_w3_3 + (size_t)i * 4u, &v, 4u);
        }
        for (uint32_t pick = base; pick < stop; pick++) {
            const uint32_t i = tide_selected[pick];
            float p; memcpy(&p, tide_w3_1 + (size_t)i * 4u, 4u);
            float v; memcpy(&v, tide_w3_4 + (size_t)i * 4u, 4u);
            float push; memcpy(&push, tide_w3_7 + (size_t)i * 4u, 4u);
            float drag; memcpy(&drag, tide_w3_9 + (size_t)i * 4u, 4u);
            v = (v + push * dt) * (1.0f - drag * dt);
            p = p + v * dt;
            float radius; memcpy(&radius, tide_w3_11 + (size_t)i * 4u, 4u);
            if (p < radius) {
                float bounce; memcpy(&bounce, tide_w3_10 + (size_t)i * 4u, 4u);
                p = radius * 2.0f - p; v = -v * bounce;
            }
            memcpy(tide_w3_1 + (size_t)i * 4u, &p, 4u);
            memcpy(tide_w3_4 + (size_t)i * 4u, &v, 4u);
        }
        for (uint32_t pick = base; pick < stop; pick++) {
            const uint32_t i = tide_selected[pick];
            float p; memcpy(&p, tide_w3_2 + (size_t)i * 4u, 4u);
            float v; memcpy(&v, tide_w3_5 + (size_t)i * 4u, 4u);
            float push; memcpy(&push, tide_w3_8 + (size_t)i * 4u, 4u);
            float drag; memcpy(&drag, tide_w3_9 + (size_t)i * 4u, 4u);
            v = (v + push * dt) * (1.0f - drag * dt);
            p = p + v * dt;
            memcpy(tide_w3_2 + (size_t)i * 4u, &p, 4u);
            memcpy(tide_w3_5 + (size_t)i * 4u, &v, 4u);
        }
    }
}
TIDE_ROWS void probe_simd4(tide_world *tide_w, const uint32_t tide_from, const uint32_t tide_to, const tide_entity *restrict tide_ent, const tide_entity *restrict tide_scn, const tide_rows_chunk *tide_selection, uint8_t *restrict tide_w3_0, uint8_t *restrict tide_w3_1, uint8_t *restrict tide_w3_2, uint8_t *restrict tide_w3_3, uint8_t *restrict tide_w3_4, uint8_t *restrict tide_w3_5, uint8_t *restrict tide_w3_6, uint8_t *restrict tide_w3_7, uint8_t *restrict tide_w3_8, uint8_t *restrict tide_w3_9, uint8_t *restrict tide_w3_10, uint8_t *restrict tide_w3_11)
{
    (void)tide_ent; (void)tide_scn;
    const uint16_t *tide_selected = tide_selection->selected;
    const float dt = tide_w->Time.dt;
    typedef float v4 __attribute__((ext_vector_type(4)));
    uint32_t pick = tide_from;
    for (; tide_to - pick >= 4u; pick += 4u) {
        const uint32_t ids[4] = {tide_selected[pick], tide_selected[pick+1u], tide_selected[pick+2u], tide_selected[pick+3u]};
        v4 f0;
        { float v; memcpy(&v, tide_w3_0 + (size_t)ids[0] * 4u, 4u); f0[0] = v; }
        { float v; memcpy(&v, tide_w3_0 + (size_t)ids[1] * 4u, 4u); f0[1] = v; }
        { float v; memcpy(&v, tide_w3_0 + (size_t)ids[2] * 4u, 4u); f0[2] = v; }
        { float v; memcpy(&v, tide_w3_0 + (size_t)ids[3] * 4u, 4u); f0[3] = v; }
        v4 f1;
        { float v; memcpy(&v, tide_w3_1 + (size_t)ids[0] * 4u, 4u); f1[0] = v; }
        { float v; memcpy(&v, tide_w3_1 + (size_t)ids[1] * 4u, 4u); f1[1] = v; }
        { float v; memcpy(&v, tide_w3_1 + (size_t)ids[2] * 4u, 4u); f1[2] = v; }
        { float v; memcpy(&v, tide_w3_1 + (size_t)ids[3] * 4u, 4u); f1[3] = v; }
        v4 f2;
        { float v; memcpy(&v, tide_w3_2 + (size_t)ids[0] * 4u, 4u); f2[0] = v; }
        { float v; memcpy(&v, tide_w3_2 + (size_t)ids[1] * 4u, 4u); f2[1] = v; }
        { float v; memcpy(&v, tide_w3_2 + (size_t)ids[2] * 4u, 4u); f2[2] = v; }
        { float v; memcpy(&v, tide_w3_2 + (size_t)ids[3] * 4u, 4u); f2[3] = v; }
        v4 f3;
        { float v; memcpy(&v, tide_w3_3 + (size_t)ids[0] * 4u, 4u); f3[0] = v; }
        { float v; memcpy(&v, tide_w3_3 + (size_t)ids[1] * 4u, 4u); f3[1] = v; }
        { float v; memcpy(&v, tide_w3_3 + (size_t)ids[2] * 4u, 4u); f3[2] = v; }
        { float v; memcpy(&v, tide_w3_3 + (size_t)ids[3] * 4u, 4u); f3[3] = v; }
        v4 f4;
        { float v; memcpy(&v, tide_w3_4 + (size_t)ids[0] * 4u, 4u); f4[0] = v; }
        { float v; memcpy(&v, tide_w3_4 + (size_t)ids[1] * 4u, 4u); f4[1] = v; }
        { float v; memcpy(&v, tide_w3_4 + (size_t)ids[2] * 4u, 4u); f4[2] = v; }
        { float v; memcpy(&v, tide_w3_4 + (size_t)ids[3] * 4u, 4u); f4[3] = v; }
        v4 f5;
        { float v; memcpy(&v, tide_w3_5 + (size_t)ids[0] * 4u, 4u); f5[0] = v; }
        { float v; memcpy(&v, tide_w3_5 + (size_t)ids[1] * 4u, 4u); f5[1] = v; }
        { float v; memcpy(&v, tide_w3_5 + (size_t)ids[2] * 4u, 4u); f5[2] = v; }
        { float v; memcpy(&v, tide_w3_5 + (size_t)ids[3] * 4u, 4u); f5[3] = v; }
        v4 f6;
        { float v; memcpy(&v, tide_w3_6 + (size_t)ids[0] * 4u, 4u); f6[0] = v; }
        { float v; memcpy(&v, tide_w3_6 + (size_t)ids[1] * 4u, 4u); f6[1] = v; }
        { float v; memcpy(&v, tide_w3_6 + (size_t)ids[2] * 4u, 4u); f6[2] = v; }
        { float v; memcpy(&v, tide_w3_6 + (size_t)ids[3] * 4u, 4u); f6[3] = v; }
        v4 f7;
        { float v; memcpy(&v, tide_w3_7 + (size_t)ids[0] * 4u, 4u); f7[0] = v; }
        { float v; memcpy(&v, tide_w3_7 + (size_t)ids[1] * 4u, 4u); f7[1] = v; }
        { float v; memcpy(&v, tide_w3_7 + (size_t)ids[2] * 4u, 4u); f7[2] = v; }
        { float v; memcpy(&v, tide_w3_7 + (size_t)ids[3] * 4u, 4u); f7[3] = v; }
        v4 f8;
        { float v; memcpy(&v, tide_w3_8 + (size_t)ids[0] * 4u, 4u); f8[0] = v; }
        { float v; memcpy(&v, tide_w3_8 + (size_t)ids[1] * 4u, 4u); f8[1] = v; }
        { float v; memcpy(&v, tide_w3_8 + (size_t)ids[2] * 4u, 4u); f8[2] = v; }
        { float v; memcpy(&v, tide_w3_8 + (size_t)ids[3] * 4u, 4u); f8[3] = v; }
        v4 f9;
        { float v; memcpy(&v, tide_w3_9 + (size_t)ids[0] * 4u, 4u); f9[0] = v; }
        { float v; memcpy(&v, tide_w3_9 + (size_t)ids[1] * 4u, 4u); f9[1] = v; }
        { float v; memcpy(&v, tide_w3_9 + (size_t)ids[2] * 4u, 4u); f9[2] = v; }
        { float v; memcpy(&v, tide_w3_9 + (size_t)ids[3] * 4u, 4u); f9[3] = v; }
        v4 f10;
        { float v; memcpy(&v, tide_w3_10 + (size_t)ids[0] * 4u, 4u); f10[0] = v; }
        { float v; memcpy(&v, tide_w3_10 + (size_t)ids[1] * 4u, 4u); f10[1] = v; }
        { float v; memcpy(&v, tide_w3_10 + (size_t)ids[2] * 4u, 4u); f10[2] = v; }
        { float v; memcpy(&v, tide_w3_10 + (size_t)ids[3] * 4u, 4u); f10[3] = v; }
        v4 f11;
        { float v; memcpy(&v, tide_w3_11 + (size_t)ids[0] * 4u, 4u); f11[0] = v; }
        { float v; memcpy(&v, tide_w3_11 + (size_t)ids[1] * 4u, 4u); f11[1] = v; }
        { float v; memcpy(&v, tide_w3_11 + (size_t)ids[2] * 4u, 4u); f11[2] = v; }
        { float v; memcpy(&v, tide_w3_11 + (size_t)ids[3] * 4u, 4u); f11[3] = v; }
        const v4 drag = (v4)(1.0f) - f9 * (v4)(dt);
        f3 = (f3 + f6 * (v4)(dt)) * drag;
        f0 = f0 + f3 * (v4)(dt);
        f4 = (f4 + f7 * (v4)(dt)) * drag;
        f1 = f1 + f4 * (v4)(dt);
        f5 = (f5 + f8 * (v4)(dt)) * drag;
        f2 = f2 + f5 * (v4)(dt);
        const __typeof__(f1 < f11) hit = f1 < f11;
        f1 = hit ? f11 * (v4)(2.0f) - f1 : f1;
        f4 = hit ? -f4 * f10 : f4;
        { float v = f0[0]; memcpy(tide_w3_0 + (size_t)ids[0] * 4u, &v, 4u); }
        { float v = f0[1]; memcpy(tide_w3_0 + (size_t)ids[1] * 4u, &v, 4u); }
        { float v = f0[2]; memcpy(tide_w3_0 + (size_t)ids[2] * 4u, &v, 4u); }
        { float v = f0[3]; memcpy(tide_w3_0 + (size_t)ids[3] * 4u, &v, 4u); }
        { float v = f1[0]; memcpy(tide_w3_1 + (size_t)ids[0] * 4u, &v, 4u); }
        { float v = f1[1]; memcpy(tide_w3_1 + (size_t)ids[1] * 4u, &v, 4u); }
        { float v = f1[2]; memcpy(tide_w3_1 + (size_t)ids[2] * 4u, &v, 4u); }
        { float v = f1[3]; memcpy(tide_w3_1 + (size_t)ids[3] * 4u, &v, 4u); }
        { float v = f2[0]; memcpy(tide_w3_2 + (size_t)ids[0] * 4u, &v, 4u); }
        { float v = f2[1]; memcpy(tide_w3_2 + (size_t)ids[1] * 4u, &v, 4u); }
        { float v = f2[2]; memcpy(tide_w3_2 + (size_t)ids[2] * 4u, &v, 4u); }
        { float v = f2[3]; memcpy(tide_w3_2 + (size_t)ids[3] * 4u, &v, 4u); }
        { float v = f3[0]; memcpy(tide_w3_3 + (size_t)ids[0] * 4u, &v, 4u); }
        { float v = f3[1]; memcpy(tide_w3_3 + (size_t)ids[1] * 4u, &v, 4u); }
        { float v = f3[2]; memcpy(tide_w3_3 + (size_t)ids[2] * 4u, &v, 4u); }
        { float v = f3[3]; memcpy(tide_w3_3 + (size_t)ids[3] * 4u, &v, 4u); }
        { float v = f4[0]; memcpy(tide_w3_4 + (size_t)ids[0] * 4u, &v, 4u); }
        { float v = f4[1]; memcpy(tide_w3_4 + (size_t)ids[1] * 4u, &v, 4u); }
        { float v = f4[2]; memcpy(tide_w3_4 + (size_t)ids[2] * 4u, &v, 4u); }
        { float v = f4[3]; memcpy(tide_w3_4 + (size_t)ids[3] * 4u, &v, 4u); }
        { float v = f5[0]; memcpy(tide_w3_5 + (size_t)ids[0] * 4u, &v, 4u); }
        { float v = f5[1]; memcpy(tide_w3_5 + (size_t)ids[1] * 4u, &v, 4u); }
        { float v = f5[2]; memcpy(tide_w3_5 + (size_t)ids[2] * 4u, &v, 4u); }
        { float v = f5[3]; memcpy(tide_w3_5 + (size_t)ids[3] * 4u, &v, 4u); }
    }
    for (; pick < tide_to; pick++) {
        const uint32_t i = tide_selected[pick];
        {
            float p; memcpy(&p, tide_w3_0 + (size_t)i * 4u, 4u);
            float v; memcpy(&v, tide_w3_3 + (size_t)i * 4u, 4u);
            float push; memcpy(&push, tide_w3_6 + (size_t)i * 4u, 4u);
            float drag; memcpy(&drag, tide_w3_9 + (size_t)i * 4u, 4u);
            v = (v + push * dt) * (1.0f - drag * dt);
            p = p + v * dt;
            memcpy(tide_w3_0 + (size_t)i * 4u, &p, 4u);
            memcpy(tide_w3_3 + (size_t)i * 4u, &v, 4u);
        }
        {
            float p; memcpy(&p, tide_w3_1 + (size_t)i * 4u, 4u);
            float v; memcpy(&v, tide_w3_4 + (size_t)i * 4u, 4u);
            float push; memcpy(&push, tide_w3_7 + (size_t)i * 4u, 4u);
            float drag; memcpy(&drag, tide_w3_9 + (size_t)i * 4u, 4u);
            v = (v + push * dt) * (1.0f - drag * dt);
            p = p + v * dt;
            float radius; memcpy(&radius, tide_w3_11 + (size_t)i * 4u, 4u);
            if (p < radius) {
                float bounce; memcpy(&bounce, tide_w3_10 + (size_t)i * 4u, 4u);
                p = radius * 2.0f - p; v = -v * bounce;
            }
            memcpy(tide_w3_1 + (size_t)i * 4u, &p, 4u);
            memcpy(tide_w3_4 + (size_t)i * 4u, &v, 4u);
        }
        {
            float p; memcpy(&p, tide_w3_2 + (size_t)i * 4u, 4u);
            float v; memcpy(&v, tide_w3_5 + (size_t)i * 4u, 4u);
            float push; memcpy(&push, tide_w3_8 + (size_t)i * 4u, 4u);
            float drag; memcpy(&drag, tide_w3_9 + (size_t)i * 4u, 4u);
            v = (v + push * dt) * (1.0f - drag * dt);
            p = p + v * dt;
            memcpy(tide_w3_2 + (size_t)i * 4u, &p, 4u);
            memcpy(tide_w3_5 + (size_t)i * 4u, &v, 4u);
        }
    }
}
TIDE_ROWS void probe_simd_blocks4(tide_world *tide_w, const uint32_t tide_from, const uint32_t tide_to, const tide_entity *restrict tide_ent, const tide_entity *restrict tide_scn, const tide_rows_chunk *tide_selection, uint8_t *restrict tide_w3_0, uint8_t *restrict tide_w3_1, uint8_t *restrict tide_w3_2, uint8_t *restrict tide_w3_3, uint8_t *restrict tide_w3_4, uint8_t *restrict tide_w3_5, uint8_t *restrict tide_w3_6, uint8_t *restrict tide_w3_7, uint8_t *restrict tide_w3_8, uint8_t *restrict tide_w3_9, uint8_t *restrict tide_w3_10, uint8_t *restrict tide_w3_11)
{
    (void)tide_ent; (void)tide_scn;
    const uint16_t *tide_selected = tide_selection->selected;
    const float dt = tide_w->Time.dt;
    typedef float v4 __attribute__((ext_vector_type(4)));
    uint32_t pick = tide_from;
    for (; pick < tide_to;) {
        const uint32_t base = tide_selected[pick] & ~3u;
        if (tide_selection->rows - base < 4u) break;
        if (base < tide_selected[tide_from] || base + 3u > tide_selected[tide_to - 1u]) {
            const uint32_t i = tide_selected[pick++];
            {
            float p; memcpy(&p, tide_w3_0 + (size_t)i * 4u, 4u);
            float v; memcpy(&v, tide_w3_3 + (size_t)i * 4u, 4u);
            float push; memcpy(&push, tide_w3_6 + (size_t)i * 4u, 4u);
            float drag; memcpy(&drag, tide_w3_9 + (size_t)i * 4u, 4u);
            v = (v + push * dt) * (1.0f - drag * dt);
            p = p + v * dt;
            memcpy(tide_w3_0 + (size_t)i * 4u, &p, 4u);
            memcpy(tide_w3_3 + (size_t)i * 4u, &v, 4u);
            }
            {
            float p; memcpy(&p, tide_w3_1 + (size_t)i * 4u, 4u);
            float v; memcpy(&v, tide_w3_4 + (size_t)i * 4u, 4u);
            float push; memcpy(&push, tide_w3_7 + (size_t)i * 4u, 4u);
            float drag; memcpy(&drag, tide_w3_9 + (size_t)i * 4u, 4u);
            v = (v + push * dt) * (1.0f - drag * dt);
            p = p + v * dt;
            float radius; memcpy(&radius, tide_w3_11 + (size_t)i * 4u, 4u);
            if (p < radius) {
                float bounce; memcpy(&bounce, tide_w3_10 + (size_t)i * 4u, 4u);
                p = radius * 2.0f - p; v = -v * bounce;
            }
            memcpy(tide_w3_1 + (size_t)i * 4u, &p, 4u);
            memcpy(tide_w3_4 + (size_t)i * 4u, &v, 4u);
            }
            {
            float p; memcpy(&p, tide_w3_2 + (size_t)i * 4u, 4u);
            float v; memcpy(&v, tide_w3_5 + (size_t)i * 4u, 4u);
            float push; memcpy(&push, tide_w3_8 + (size_t)i * 4u, 4u);
            float drag; memcpy(&drag, tide_w3_9 + (size_t)i * 4u, 4u);
            v = (v + push * dt) * (1.0f - drag * dt);
            p = p + v * dt;
            memcpy(tide_w3_2 + (size_t)i * 4u, &p, 4u);
            memcpy(tide_w3_5 + (size_t)i * 4u, &v, 4u);
            }
            continue;
        }
        typedef int m4 __attribute__((ext_vector_type(4)));
        m4 active = (m4)(0);
        do { active[tide_selected[pick] - base] = -1; pick++; } while (pick < tide_to && (tide_selected[pick] & ~3u) == base);
        v4 f0; memcpy(&f0, tide_w3_0 + (size_t)base * 4u, 16u);
        const v4 original0 = f0;
        v4 f1; memcpy(&f1, tide_w3_1 + (size_t)base * 4u, 16u);
        const v4 original1 = f1;
        v4 f2; memcpy(&f2, tide_w3_2 + (size_t)base * 4u, 16u);
        const v4 original2 = f2;
        v4 f3; memcpy(&f3, tide_w3_3 + (size_t)base * 4u, 16u);
        const v4 original3 = f3;
        v4 f4; memcpy(&f4, tide_w3_4 + (size_t)base * 4u, 16u);
        const v4 original4 = f4;
        v4 f5; memcpy(&f5, tide_w3_5 + (size_t)base * 4u, 16u);
        const v4 original5 = f5;
        v4 f6; memcpy(&f6, tide_w3_6 + (size_t)base * 4u, 16u);
        v4 f7; memcpy(&f7, tide_w3_7 + (size_t)base * 4u, 16u);
        v4 f8; memcpy(&f8, tide_w3_8 + (size_t)base * 4u, 16u);
        v4 f9; memcpy(&f9, tide_w3_9 + (size_t)base * 4u, 16u);
        v4 f10; memcpy(&f10, tide_w3_10 + (size_t)base * 4u, 16u);
        v4 f11; memcpy(&f11, tide_w3_11 + (size_t)base * 4u, 16u);
        const v4 drag = (v4)(1.0f) - f9 * (v4)(dt);
        f3 = (f3 + f6 * (v4)(dt)) * drag;
        f0 = f0 + f3 * (v4)(dt);
        f4 = (f4 + f7 * (v4)(dt)) * drag;
        f1 = f1 + f4 * (v4)(dt);
        f5 = (f5 + f8 * (v4)(dt)) * drag;
        f2 = f2 + f5 * (v4)(dt);
        const __typeof__(f1 < f11) hit = f1 < f11;
        f1 = hit ? f11 * (v4)(2.0f) - f1 : f1;
        f4 = hit ? -f4 * f10 : f4;
        f0 = active ? f0 : original0;
        memcpy(tide_w3_0 + (size_t)base * 4u, &f0, 16u);
        f1 = active ? f1 : original1;
        memcpy(tide_w3_1 + (size_t)base * 4u, &f1, 16u);
        f2 = active ? f2 : original2;
        memcpy(tide_w3_2 + (size_t)base * 4u, &f2, 16u);
        f3 = active ? f3 : original3;
        memcpy(tide_w3_3 + (size_t)base * 4u, &f3, 16u);
        f4 = active ? f4 : original4;
        memcpy(tide_w3_4 + (size_t)base * 4u, &f4, 16u);
        f5 = active ? f5 : original5;
        memcpy(tide_w3_5 + (size_t)base * 4u, &f5, 16u);
    }
    for (; pick < tide_to; pick++) {
        const uint32_t i = tide_selected[pick];
        {
            float p; memcpy(&p, tide_w3_0 + (size_t)i * 4u, 4u);
            float v; memcpy(&v, tide_w3_3 + (size_t)i * 4u, 4u);
            float push; memcpy(&push, tide_w3_6 + (size_t)i * 4u, 4u);
            float drag; memcpy(&drag, tide_w3_9 + (size_t)i * 4u, 4u);
            v = (v + push * dt) * (1.0f - drag * dt);
            p = p + v * dt;
            memcpy(tide_w3_0 + (size_t)i * 4u, &p, 4u);
            memcpy(tide_w3_3 + (size_t)i * 4u, &v, 4u);
        }
        {
            float p; memcpy(&p, tide_w3_1 + (size_t)i * 4u, 4u);
            float v; memcpy(&v, tide_w3_4 + (size_t)i * 4u, 4u);
            float push; memcpy(&push, tide_w3_7 + (size_t)i * 4u, 4u);
            float drag; memcpy(&drag, tide_w3_9 + (size_t)i * 4u, 4u);
            v = (v + push * dt) * (1.0f - drag * dt);
            p = p + v * dt;
            float radius; memcpy(&radius, tide_w3_11 + (size_t)i * 4u, 4u);
            if (p < radius) {
                float bounce; memcpy(&bounce, tide_w3_10 + (size_t)i * 4u, 4u);
                p = radius * 2.0f - p; v = -v * bounce;
            }
            memcpy(tide_w3_1 + (size_t)i * 4u, &p, 4u);
            memcpy(tide_w3_4 + (size_t)i * 4u, &v, 4u);
        }
        {
            float p; memcpy(&p, tide_w3_2 + (size_t)i * 4u, 4u);
            float v; memcpy(&v, tide_w3_5 + (size_t)i * 4u, 4u);
            float push; memcpy(&push, tide_w3_8 + (size_t)i * 4u, 4u);
            float drag; memcpy(&drag, tide_w3_9 + (size_t)i * 4u, 4u);
            v = (v + push * dt) * (1.0f - drag * dt);
            p = p + v * dt;
            memcpy(tide_w3_2 + (size_t)i * 4u, &p, 4u);
            memcpy(tide_w3_5 + (size_t)i * 4u, &v, 4u);
        }
    }
}

static float before[12][2048], after[12][2048];
static tide_entity entities[2048];
static tide_world world;
int main(void) {
    const uint32_t sizes[] = {1,2,3,4,5,15,16,17,65,127,128,129,2047,2048};
    unsigned checks = 0;
    world.Time.dt = 1.0f / 60.0f;
    for (unsigned size = 0; size < sizeof sizes / sizeof *sizes; size++) {
        for (uint32_t pattern = 0; pattern < 32; pattern++) {
            tide_rows_chunk selection = {0};
            selection.rows = sizes[size];
            for (uint32_t i = 0; i < selection.rows; i++) {
                if (selection.pass < 127u && ((i * 2654435761u + pattern * 2246822519u) % 101u) < pattern)
                    selection.selected[selection.pass++] = (uint16_t)i;
            }
            for (unsigned piece = 0; piece < 4; piece++) {
                uint32_t first = selection.pass * piece / 4;
                uint32_t last = selection.pass * (piece + 1) / 4;

                for (unsigned l = 0; l < 12; l++) for (unsigned i = 0; i < 2048; i++) {
                    float value = (float)((int)((i * 37u + l * 101u + pattern) % 997u) - 498) * 0.01f;
                    if (i % 11u == 0) value = -0.0f;
                    before[l][i] = after[l][i] = value;
                }
                for (unsigned tick = 0; tick < 20; tick++) {
                    tide_rows_Shift_6(&world, first, last, entities, entities, &selection, (uint8_t *)before[0], (uint8_t *)before[1], (uint8_t *)before[2], (uint8_t *)before[3], (uint8_t *)before[4], (uint8_t *)before[5], (uint8_t *)before[6], (uint8_t *)before[7], (uint8_t *)before[8], (uint8_t *)before[9], (uint8_t *)before[10], (uint8_t *)before[11]);
                    probe_axis_chunk(&world, first, last, entities, entities, &selection, (uint8_t *)after[0], (uint8_t *)after[1], (uint8_t *)after[2], (uint8_t *)after[3], (uint8_t *)after[4], (uint8_t *)after[5], (uint8_t *)after[6], (uint8_t *)after[7], (uint8_t *)after[8], (uint8_t *)after[9], (uint8_t *)after[10], (uint8_t *)after[11]);
                    if (memcmp(before, after, sizeof before)) { fprintf(stderr, "axis_chunk mismatch size %u pattern %u piece %u tick %u\n", selection.rows, pattern, piece, tick); return 1; }
                    checks++;
                }

                for (unsigned l = 0; l < 12; l++) for (unsigned i = 0; i < 2048; i++) {
                    float value = (float)((int)((i * 37u + l * 101u + pattern) % 997u) - 498) * 0.01f;
                    if (i % 11u == 0) value = -0.0f;
                    before[l][i] = after[l][i] = value;
                }
                for (unsigned tick = 0; tick < 20; tick++) {
                    tide_rows_Shift_6(&world, first, last, entities, entities, &selection, (uint8_t *)before[0], (uint8_t *)before[1], (uint8_t *)before[2], (uint8_t *)before[3], (uint8_t *)before[4], (uint8_t *)before[5], (uint8_t *)before[6], (uint8_t *)before[7], (uint8_t *)before[8], (uint8_t *)before[9], (uint8_t *)before[10], (uint8_t *)before[11]);
                    probe_axis16(&world, first, last, entities, entities, &selection, (uint8_t *)after[0], (uint8_t *)after[1], (uint8_t *)after[2], (uint8_t *)after[3], (uint8_t *)after[4], (uint8_t *)after[5], (uint8_t *)after[6], (uint8_t *)after[7], (uint8_t *)after[8], (uint8_t *)after[9], (uint8_t *)after[10], (uint8_t *)after[11]);
                    if (memcmp(before, after, sizeof before)) { fprintf(stderr, "axis16 mismatch size %u pattern %u piece %u tick %u\n", selection.rows, pattern, piece, tick); return 1; }
                    checks++;
                }

                for (unsigned l = 0; l < 12; l++) for (unsigned i = 0; i < 2048; i++) {
                    float value = (float)((int)((i * 37u + l * 101u + pattern) % 997u) - 498) * 0.01f;
                    if (i % 11u == 0) value = -0.0f;
                    before[l][i] = after[l][i] = value;
                }
                for (unsigned tick = 0; tick < 20; tick++) {
                    tide_rows_Shift_6(&world, first, last, entities, entities, &selection, (uint8_t *)before[0], (uint8_t *)before[1], (uint8_t *)before[2], (uint8_t *)before[3], (uint8_t *)before[4], (uint8_t *)before[5], (uint8_t *)before[6], (uint8_t *)before[7], (uint8_t *)before[8], (uint8_t *)before[9], (uint8_t *)before[10], (uint8_t *)before[11]);
                    probe_axis64(&world, first, last, entities, entities, &selection, (uint8_t *)after[0], (uint8_t *)after[1], (uint8_t *)after[2], (uint8_t *)after[3], (uint8_t *)after[4], (uint8_t *)after[5], (uint8_t *)after[6], (uint8_t *)after[7], (uint8_t *)after[8], (uint8_t *)after[9], (uint8_t *)after[10], (uint8_t *)after[11]);
                    if (memcmp(before, after, sizeof before)) { fprintf(stderr, "axis64 mismatch size %u pattern %u piece %u tick %u\n", selection.rows, pattern, piece, tick); return 1; }
                    checks++;
                }

                for (unsigned l = 0; l < 12; l++) for (unsigned i = 0; i < 2048; i++) {
                    float value = (float)((int)((i * 37u + l * 101u + pattern) % 997u) - 498) * 0.01f;
                    if (i % 11u == 0) value = -0.0f;
                    before[l][i] = after[l][i] = value;
                }
                for (unsigned tick = 0; tick < 20; tick++) {
                    tide_rows_Shift_6(&world, first, last, entities, entities, &selection, (uint8_t *)before[0], (uint8_t *)before[1], (uint8_t *)before[2], (uint8_t *)before[3], (uint8_t *)before[4], (uint8_t *)before[5], (uint8_t *)before[6], (uint8_t *)before[7], (uint8_t *)before[8], (uint8_t *)before[9], (uint8_t *)before[10], (uint8_t *)before[11]);
                    probe_simd4(&world, first, last, entities, entities, &selection, (uint8_t *)after[0], (uint8_t *)after[1], (uint8_t *)after[2], (uint8_t *)after[3], (uint8_t *)after[4], (uint8_t *)after[5], (uint8_t *)after[6], (uint8_t *)after[7], (uint8_t *)after[8], (uint8_t *)after[9], (uint8_t *)after[10], (uint8_t *)after[11]);
                    if (memcmp(before, after, sizeof before)) { fprintf(stderr, "simd4 mismatch size %u pattern %u piece %u tick %u\n", selection.rows, pattern, piece, tick); return 1; }
                    checks++;
                }

                for (unsigned l = 0; l < 12; l++) for (unsigned i = 0; i < 2048; i++) {
                    float value = (float)((int)((i * 37u + l * 101u + pattern) % 997u) - 498) * 0.01f;
                    if (i % 11u == 0) value = -0.0f;
                    before[l][i] = after[l][i] = value;
                }
                for (unsigned tick = 0; tick < 20; tick++) {
                    tide_rows_Shift_6(&world, first, last, entities, entities, &selection, (uint8_t *)before[0], (uint8_t *)before[1], (uint8_t *)before[2], (uint8_t *)before[3], (uint8_t *)before[4], (uint8_t *)before[5], (uint8_t *)before[6], (uint8_t *)before[7], (uint8_t *)before[8], (uint8_t *)before[9], (uint8_t *)before[10], (uint8_t *)before[11]);
                    probe_simd_blocks4(&world, first, last, entities, entities, &selection, (uint8_t *)after[0], (uint8_t *)after[1], (uint8_t *)after[2], (uint8_t *)after[3], (uint8_t *)after[4], (uint8_t *)after[5], (uint8_t *)after[6], (uint8_t *)after[7], (uint8_t *)after[8], (uint8_t *)after[9], (uint8_t *)after[10], (uint8_t *)after[11]);
                    if (memcmp(before, after, sizeof before)) { fprintf(stderr, "simd_blocks4 mismatch size %u pattern %u piece %u tick %u\n", selection.rows, pattern, piece, tick); return 1; }
                    checks++;
                }
            }
        }
    }
    printf("%u exact lane comparisons passed\n", checks);
    return 0;
}
