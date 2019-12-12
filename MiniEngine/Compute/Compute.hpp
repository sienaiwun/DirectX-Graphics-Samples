#pragma once

#define Noise_B 0x1000

#include "pch.h"


#include "GameCore.h"
#include "GraphicsCore.h"
#include "hlsl.hpp"
#include "CameraController.h"
#include "BufferManager.h"
#include "Camera.h"
#include "GpuBuffer.h"
#include "CommandContext.h"
#include "SamplerManager.h"
#include "TemporalEffects.h"
#include "MotionBlur.h"
#include "DepthOfField.h"
#include "PostEffects.h"
#include "SSAO.h"
#include "FXAA.h"
#include "SkyPass.hpp"
#include "SystemTime.h"
#include "TextRenderer.h"
#include "ShadowCamera.h"
#include "ParticleEffectManager.h"
#include "GameInput.h"


using namespace GameCore;
using namespace Math;
using namespace Graphics;



class Compute : public GameCore::IGameApp
{
public:
    Compute(void) {}

    virtual void Startup(void) override;
    virtual void Cleanup(void) override;

    virtual void Update(float deltaT) override;
    virtual void RenderScene(void) override;
};

inline float sCurve(const float t) { return t * t * (3 - 2 * t); }
inline float lerp(const float u, const float v, const float x) { return u + x * (v - u); }

//----------------------------------------------------------------------------
// Noise
//----------------------------------------------------------------------------
#define Noise_B 0x1000
#define Noise_BM 0xff

#define Noise_N 0x1000
#define Noise_NP 12
#define Noise_NM 0xfff

#define setup(i,b0,b1,r0,r1)\
	t = i + Noise_N;\
	b0 = ((int) t) & Noise_BM;\
	b1 = (b0 + 1) & Noise_BM;\
	r0 = t - (int) t;\
	r1 = r0 - 1;

static int Noise_p[Noise_B + Noise_B + 2];
static float Noise_g3[Noise_B + Noise_B + 2][3];
static float Noise_g2[Noise_B + Noise_B + 2][2];
static float Noise_g1[Noise_B + Noise_B + 2];

static void normalize2(float v[2])
{
    float s = 1.0f / sqrtf(v[0] * v[0] + v[1] * v[1]);
    v[0] *= s;
    v[1] *= s;
}

static void normalize3(float v[3])
{
    float s = 1.0f / sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    v[0] *= s;
    v[1] *= s;
    v[2] *= s;
}

inline float noise1(const float x)
{
    int bx0, bx1;
    float rx0, rx1, sx, t, u, v;

    setup(x, bx0, bx1, rx0, rx1);

    sx = sCurve(rx0);

    u = rx0 * Noise_g1[Noise_p[bx0]];
    v = rx1 * Noise_g1[Noise_p[bx1]];

    return lerp(sx, u, v);
}

#define at2(rx,ry) (rx * q[0] + ry * q[1])
inline float noise2(const float x, const float y)
{
    int bx0, bx1, by0, by1, b00, b10, b01, b11;
    float rx0, rx1, ry0, ry1, *q, sx, sy, a, b, t, u, v;
    int i, j;

    setup(x, bx0, bx1, rx0, rx1);
    setup(y, by0, by1, ry0, ry1);

    i = Noise_p[bx0];
    j = Noise_p[bx1];

    b00 = Noise_p[i + by0];
    b10 = Noise_p[j + by0];
    b01 = Noise_p[i + by1];
    b11 = Noise_p[j + by1];

    sx = sCurve(rx0);
    sy = sCurve(ry0);

    q = Noise_g2[b00]; u = at2(rx0, ry0);
    q = Noise_g2[b10]; v = at2(rx1, ry0);
    a = lerp(sx, u, v);

    q = Noise_g2[b01]; u = at2(rx0, ry1);
    q = Noise_g2[b11]; v = at2(rx1, ry1);
    b = lerp(sx, u, v);

    return lerp(sy, a, b);
}
#undef at2

#define at3(rx,ry,rz) (rx * q[0] + ry * q[1] + rz * q[2])
inline float noise3(const float x, const float y, const float z)
{
    int bx0, bx1, by0, by1, bz0, bz1, b00, b10, b01, b11;
    float rx0, rx1, ry0, ry1, rz0, rz1, *q, sy, sz, a, b, c, d, t, u, v;
    int i, j;

    setup(x, bx0, bx1, rx0, rx1);
    setup(y, by0, by1, ry0, ry1);
    setup(z, bz0, bz1, rz0, rz1);

    i = Noise_p[bx0];
    j = Noise_p[bx1];

    b00 = Noise_p[i + by0];
    b10 = Noise_p[j + by0];
    b01 = Noise_p[i + by1];
    b11 = Noise_p[j + by1];

    t = sCurve(rx0);
    sy = sCurve(ry0);
    sz = sCurve(rz0);

    q = Noise_g3[b00 + bz0]; u = at3(rx0, ry0, rz0);
    q = Noise_g3[b10 + bz0]; v = at3(rx1, ry0, rz0);
    a = lerp(t, u, v);

    q = Noise_g3[b01 + bz0]; u = at3(rx0, ry1, rz0);
    q = Noise_g3[b11 + bz0]; v = at3(rx1, ry1, rz0);
    b = lerp(t, u, v);

    c = lerp(sy, a, b);

    q = Noise_g3[b00 + bz1]; u = at3(rx0, ry0, rz1);
    q = Noise_g3[b10 + bz1]; v = at3(rx1, ry0, rz1);
    a = lerp(t, u, v);

    q = Noise_g3[b01 + bz1]; u = at3(rx0, ry1, rz1);
    q = Noise_g3[b11 + bz1]; v = at3(rx1, ry1, rz1);
    b = lerp(t, u, v);

    d = lerp(sy, a, b);

    return lerp(sz, c, d);
}
#undef at3

inline float turbulence2(const float x, const float y, float freq)
{
    float t = 0.0f;

    do {
        t += noise2(freq * x, freq * y) / freq;
        freq *= 0.5f;
    } while (freq >= 1.0f);

    return t;
}

inline float turbulence3(const float x, const float y, const float z, float freq)
{
    float t = 0.0f;

    do {
        t += noise3(freq * x, freq * y, freq * z) / freq;
        freq *= 0.5f;
    } while (freq >= 1.0f);

    return t;
}

inline float tileableNoise1(const float x, const float w)
{
    return (noise1(x)     * (w - x) +
        noise1(x - w) *      x) / w;
}
inline float tileableNoise2(const float x, const float y, const float w, const float h)
{
    return (noise2(x, y)     * (w - x) * (h - y) +
        noise2(x - w, y)     *      x  * (h - y) +
        noise2(x, y - h) * (w - x) *      y +
        noise2(x - w, y - h) *      x  *      y) / (w * h);
}
inline float tileableNoise3(const float x, const float y, const float z, const float w, const float h, const float d)
{
    return (noise3(x, y, z)     * (w - x) * (h - y) * (d - z) +
        noise3(x - w, y, z)     *      x  * (h - y) * (d - z) +
        noise3(x, y - h, z)     * (w - x) *      y  * (d - z) +
        noise3(x - w, y - h, z)     *      x  *      y  * (d - z) +
        noise3(x, y, z - d) * (w - x) * (h - y) *      z +
        noise3(x - w, y, z - d) *      x  * (h - y) *      z +
        noise3(x, y - h, z - d) * (w - x) *      y  *      z +
        noise3(x - w, y - h, z - d) *      x  *      y  *      z) / (w * h * d);
}

inline float tileableTurbulence2(const float x, const float y, const float w, const float h, float freq)
{
    float t = 0.0f;

    do {
        t += tileableNoise2(freq * x, freq * y, w * freq, h * freq) / freq;
        freq *= 0.5f;
    } while (freq >= 1.0f);

    return t;
}
inline float tileableTurbulence3(const float x, const float y, const float z, const float w, const float h, const float d, float freq)
{
    float t = 0.0f;

    do {
        t += tileableNoise3(freq * x, freq * y, freq * z, w * freq, h * freq, d * freq) / freq;
        freq *= 0.5f;
    } while (freq >= 1.0f);

    return t;
}

inline void initNoise()
{
    int i, j, k;

    for (i = 0; i < Noise_B; i++) {
        Noise_p[i] = i;

        Noise_g1[i] = (float)((rand() % (Noise_B + Noise_B)) - Noise_B) / Noise_B;

        for (j = 0; j < 2; j++)
            Noise_g2[i][j] = (float)((rand() % (Noise_B + Noise_B)) - Noise_B) / Noise_B;
        normalize2(Noise_g2[i]);

        for (j = 0; j < 3; j++)
            Noise_g3[i][j] = (float)((rand() % (Noise_B + Noise_B)) - Noise_B) / Noise_B;
        normalize3(Noise_g3[i]);
    }

    while (--i) {
        k = Noise_p[i];
        Noise_p[i] = Noise_p[j = rand() % Noise_B];
        Noise_p[j] = k;
    }

    for (i = 0; i < Noise_B + 2; i++) {
        Noise_p[Noise_B + i] = Noise_p[i];
        Noise_g1[Noise_B + i] = Noise_g1[i];
        for (j = 0; j < 2; j++)
            Noise_g2[Noise_B + i][j] = Noise_g2[i][j];
        for (j = 0; j < 3; j++)
            Noise_g3[Noise_B + i][j] = Noise_g3[i][j];
    }
}
const float gTimeScale = 0.2f;


void UpdateMu(float deltaTime, float t[4], float a[4], float b[4])
{
    *t += gTimeScale * deltaTime;

    if (*t >= 1.0f)
    {
        *t = 0.0f;

        a[0] = b[0];
        a[1] = b[1];
        a[2] = b[2];
        a[3] = b[3];

        b[0] = 2.0f * rand() / (float)RAND_MAX - 1.0f;
        b[1] = 2.0f * rand() / (float)RAND_MAX - 1.0f;
        b[2] = 2.0f * rand() / (float)RAND_MAX - 1.0f;
        b[3] = 2.0f * rand() / (float)RAND_MAX - 1.0f;
    }
}

void Interpolate(float m[4], float t, float a[4], float b[4])
{
    int i;
    for (i = 0; i < 4; i++)
        m[i] = (1.0f - t) * a[i] + t * b[i];
}

void RandomColor(float v[4])
{
    do
    {
        v[0] = 2.0f * rand() / (float)RAND_MAX - 1.0f;
        v[1] = 2.0f * rand() / (float)RAND_MAX - 1.0f;
        v[2] = 2.0f * rand() / (float)RAND_MAX - 1.0f;
    } while (v[0] < 0 && v[1] < 0 && v[2] < 0);    // prevent black colors
    v[3] = 1.0f;
}

void UpdateColor(float deltaTime, float t[4], float a[4], float b[4])
{
    *t += gTimeScale * deltaTime;

    if (*t >= 1.0f)
    {
        *t = 0.0f;

        a[0] = b[0];
        a[1] = b[1];
        a[2] = b[2];
        a[3] = b[3];

        RandomColor(b);
    }
}

#undef Noise_B
#undef Noise_BM
#undef Noise_NP
#undef Noise_NM
#undef setup