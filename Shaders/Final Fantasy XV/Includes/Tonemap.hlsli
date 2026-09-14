#include "neutwo.hlsl"
#include "Common.hlsl"
#include "../../Includes/ColorGradingLUT.hlsl"

// Internal helper for log safety
float SafeLog(float x) { return log(max(x, 1e-6)); }

// ============================== FFXV tonemap curve ==============================
// y(x) = n46 * ln( T * u^p + 1 ) - n49,   with u = inv * ln(a*x + b),  a = 39.8107185
// (b = ZeroSlope, inv = TenPowDispositionTimesTwoPowHighRange_PlusOne_Log_Inverse,
//  p = Param_n37, T = TenPowLogHighRangePlusContrastMinusOne)
// Extended HDR: above a pivot point the curve is replaced by its tangent line there,
// to restore the dynamic range the game's (SDR-like) curve compresses away.

// --- Forward tonemapping ---
float FFXV(float x, float ZeroSlope, float InvLog, float Param_n37, float Contrast, float Param_n46, float Param_n49)
{
    const float a = 39.8107185;
    float u = InvLog * log(a * x + ZeroSlope);
    float up = pow(u, Param_n37);
    return Param_n46 * log(Contrast * up + 1.0) - Param_n49;
}

float3 FFXV(float3 color, float ZeroSlope, float InvLog, float Param_n37, float Contrast, float Param_n46, float Param_n49)
{
    return float3(
        FFXV(color.r, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46, Param_n49),
        FFXV(color.g, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46, Param_n49),
        FFXV(color.b, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46, Param_n49)
    );
}

// --- Inverse tonemapping ---
float FFXV_Inverse(float y, float ZeroSlope, float InvLog, float Param_n37, float Contrast, float Param_n46, float Param_n49)
{
    float up = (exp((y + Param_n49) / Param_n46) - 1.0) / Contrast;
    float u  = pow(max(up, 0.0), 1.0 / Param_n37);
    return (exp(u / InvLog) - ZeroSlope) / 39.8107185;
}

float3 FFXV_Inverse(float3 y, float ZeroSlope, float InvLog, float Param_n37, float Contrast, float Param_n46, float Param_n49)
{
    return float3(
        FFXV_Inverse(y.r, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46, Param_n49),
        FFXV_Inverse(y.g, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46, Param_n49),
        FFXV_Inverse(y.b, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46, Param_n49)
    );
}

// --- Recover the game's SDR curve tuning from the HDR-mode uploads ---
// The game computes the HDR tuning from the SDR tuning by a fixed rule (verified
// bit-identical across multiple scene captures, SDR vs HDR display mode):
//   (K_hdr + 1) = 3.988391 * (K_sdr + 1)
//   10^D_hdr    = 2 * 10^D_sdr          (D = Disposition, inside InvLog)
//   p_hdr       = p_sdr + 0.34526
//   n46_hdr     = 0.507594 * n46_sdr
//   n49_hdr     = 0.606713 * n49_sdr
//   ZeroSlope, HighRange: identical in both modes
// Inverting gives the SDR curve from whatever HDR params the game uploads, so the
// SDR look is preserved even if the game ever changes its constants.
void RecoverSDRParams(
    float Contrast_hdr, float InvLog_hdr, float ZeroSlope_hdr, float Param_n37_hdr,
    float Param_n46_hdr, float Param_n49_hdr,
    out float Contrast, out float InvLog, out float ZeroSlope,
    out float Param_n37, out float Param_n46, out float Param_n49)
{
    Contrast  = (Contrast_hdr + 1.0) / 3.988391 - 1.0;
    InvLog    = 1.0 / log((exp(1.0 / InvLog_hdr) - 1.0) / 2.0 + 1.0);
    ZeroSlope = ZeroSlope_hdr;
    Param_n37 = Param_n37_hdr - 0.34526;
    Param_n46 = Param_n46_hdr / 0.507594;
    Param_n49 = Param_n49_hdr / 0.606713;
}

// ============================== Derivatives ==============================
// Analytic derivatives of the FFXV curve, optimized for GPU execution.
// Let q = inv * a / (a*x + b) (the inner derivative, shared by all orders):
//   y'  = n46 * T * p * u^(p-1) * q / (1 + T*u^p)
//   y'' = k * ( ((p-1) - ln(a*x+b)) - T*p*u^p / (1 + T*u^p) ) / (1 + T*u^p),
//     with k = n46 * T * p * u^(p-2) * q^2
// Each needs only ONE pow() with a variable exponent (vs 3 in the naive form),
// plus a single log() and division.
// Note: these were numerically validated to match the naive formulation to ~1e-7
// relative error (see "tonemap_test.cpp" alongside this file).

float FFXV_d1(float x, float ZeroSlope, float InvLog, float Param_n37, float Contrast, float Param_n46)
{
    const float a = 39.8107185;
    float axb = a * x + ZeroSlope;
    float q = InvLog * a / axb;
    float u = InvLog * log(axb);
    float upm1 = pow(u, Param_n37 - 1.0); // u^(p-1)
    float up = u * upm1; // u^p
    float denom = 1.0 + Contrast * up;
    return Param_n46 * Contrast * Param_n37 * upm1 * q / denom;
}

float FFXV_d2(float x, float ZeroSlope, float InvLog, float Param_n37, float Contrast, float Param_n46)
{
    const float a = 39.8107185;
    float axb = max(a * x + ZeroSlope, 1e-6);
    float g = log(axb);
    float gSafe = max(g, 1e-6); // prevent u < 0 at low x where log(axb) < 0
    float q = InvLog * a / axb;
    float u = InvLog * gSafe;
    float upm2 = pow(u, Param_n37 - 2.0); // u^(p-2)
    float up = u * u * upm2; // u^p
    float denom = 1.0 + Contrast * up;
    float k = Param_n46 * Contrast * Param_n37 * upm2 * (q * q);
    return k * ((Param_n37 - 1.0) - gSafe - Contrast * Param_n37 * up / denom) / denom;
}

// --- Third derivative (numerical, central differences of y'') ---
// Only used by the highest precision root finding level.
float FFXV_d3(float x, float ZeroSlope, float InvLog, float Param_n37, float Contrast, float Param_n46)
{
    float eps = max(x * 1e-4, 1e-7);
    return (FFXV_d2(x + eps, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46) -
            FFXV_d2(x - eps, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46)) / (2.0 * eps);
}

// ============================== Root finding ==============================
// Finds the y''=0 inflection ("precision" 1) or y'''=0 max-curvature ("precision" 2)
// point of the FFXV curve in [xmin, xmax].
// Phase 1: log-space scan seeds a guaranteed sign-change bracket.
// Phase 2: pure log-space (geometric midpoint) bisection - one eval per iteration,
// no Newton (its derivative-of-derivative evaluations cost far more than the extra
// bisection iterations needed for the same accuracy, and were the main GPU cost).
// Returns -1 if no sign change is found in [xmin, xmax].
float Find_Inflection(float xmin, float xmax, int scanSteps, int bisectIters,
                    float ZeroSlope, float InvLog, float Param_n37, float Contrast, float Param_n46,
                    int rootOrder)
{
    float logMin = log(xmin + 1e-6);
    float logMax = log(xmax);

    float xPrev = exp(logMin);
    float fPrev = FFXV_d2(xPrev, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46);

    float xl = xPrev, fl = fPrev;
    float xr = xPrev, fr = fPrev;
    bool found = false;

    [loop]
    for (int i = 1; i <= scanSteps; i++) {
        float x = exp(lerp(logMin, logMax, (float)i / (float)scanSteps));
        float f = FFXV_d2(x, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46);

        if ((fPrev <= 0 && f >= 0) || (fPrev >= 0 && f <= 0)) {
            xl = xPrev; fl = fPrev;
            xr = x;    fr = f;
            found = true;
            break;
        }
        xPrev = x; fPrev = f;
    }

    if (!found) return -1.0;

    // Pure bisection on the bracketed y''=0 root (geometric midpoint in log space)
    [loop]
    for (int it = 0; it < bisectIters; it++) {
        float xm = sqrt(xl * xr);
        float f = FFXV_d2(xm, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46);
        if ((fl <= 0 && f >= 0) || (fl >= 0 && f <= 0)) {
            xr = xm; fr = f;
        } else {
            xl = xm; fl = f;
        }
    }

    float inflection = sqrt(xl * xr);
    if (rootOrder != 3) return inflection;

    // Phase 3: search for the y'''=0 (max curvature) root on [inflection, xmax]
    logMin = log(inflection + 1e-6);
    xPrev = inflection;
    fPrev = FFXV_d3(xPrev, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46);

    xl = xPrev; fl = fPrev;
    xr = xPrev; fr = fPrev;
    found = false;

    [loop]
    for (int i = 1; i <= scanSteps; i++) {
        float xScan = exp(lerp(logMin, logMax, (float)i / (float)scanSteps));
        float f = FFXV_d3(xScan, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46);

        if ((fPrev <= 0 && f >= 0) || (fPrev >= 0 && f <= 0)) {
            xl = xPrev; fl = fPrev;
            xr = xScan; fr = f;
            found = true;
            break;
        }
        xPrev = xScan; fPrev = f;
    }

    if (!found) return inflection;

    // Phase 4: pure bisection on the bracketed y'''=0 root
    [loop]
    for (int it = 0; it < bisectIters; it++) {
        float xm = sqrt(xl * xr);
        float f = FFXV_d3(xm, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46);
        if ((fl <= 0 && f >= 0) || (fl >= 0 && f <= 0)) {
            xr = xm; fr = f;
        } else {
            xl = xm; fl = f;
        }
    }

    return sqrt(xl * xr);
}

// --- The Final Extended Tonemapper ---
float FFXV_Extended(float x, float base, float ZeroSlope, float InvLog, float Param_n37, float Contrast, float Param_n46, float Param_n49, float inflection)
{
    float pivot_x = inflection;
    float pivot_y = FFXV(pivot_x, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46, Param_n49);
    float slope   = FFXV_d1(pivot_x, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46);

    float extended = slope * (x - pivot_x) + pivot_y;

    return (x > pivot_x) ? extended : base;
}

// Float3 variant
float3 FFXV_Extended(float3 color, float3 base, float ZeroSlope, float InvLog, float Param_n37, float Contrast, float Param_n46, float Param_n49, float inflection)
{
    return float3(
        FFXV_Extended(color.r, base.r, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46, Param_n49, inflection),
        FFXV_Extended(color.g, base.g, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46, Param_n49, inflection),
        FFXV_Extended(color.b, base.b, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46, Param_n49, inflection)
    );
}

// ============================== HDR tonemap + grading ==============================
// "FFXV_TONEMAP_PRECISION":
// 0 - Simple: fixed 0.18 pivot (cheap, renodx default. Dynamic range extension is mild)
// 1 - High: y''=0 inflection pivot (root finding, second derivative)
// 2 - Very High: y'''=0 max-curvature pivot (root finding, also on the third derivative)
// Can be overridden at runtime through the "FFXV_TONEMAP_PRECISION" shader define
// (a compile time setting, changing it triggers a shader recompilation).
#ifndef FFXV_TONEMAP_PRECISION
#define FFXV_TONEMAP_PRECISION 2
#endif

// Runs the whole HDR tonemap extension on the game's curve.
// The pivot (and thus the root finding) only depends on the frame-constant curve
// parameters, so it's computed once (scalar), never per channel/pixel colour.
// Note: scanSteps/bisectIters were tuned against "tonemap_test.cpp": 24 bisection
// iterations land within ~1e-5 of the reference (Newton) root, i.e. below fp32 noise.
float3 FFXV_TonemapExtended(float3 untonemapped,
                          float ZeroSlope, float InvLog, float Param_n37,
                          float Contrast, float Param_n46, float Param_n49)
{
#if FFXV_TONEMAP_PRECISION == 0
    const float inflection = 0.18;
#else
    float inflection = Find_Inflection(0.0, 1.0, 16, 24, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46, FFXV_TONEMAP_PRECISION == 2 ? 3 : 2);
#endif
    return FFXV_Extended(untonemapped, FFXV(untonemapped, ZeroSlope, InvLog, Param_n37, Contrast, Param_n46, Param_n49), ZeroSlope, InvLog, Param_n37, Contrast, Param_n46, Param_n49, inflection);
}

float3 ApplyTonemapAndGrading(float3 color, bool title)
{
    float3 tonemapped_color = renodx::tonemap::neutwo::PerChannel(color, PEAK_NITS/GAME_NITS);
    // tonemapped_color = max(0, tonemapped_color);
    return tonemapped_color;
}
