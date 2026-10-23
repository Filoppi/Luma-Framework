// aka 0x54B1FF52

// parse CUSTOM_ESM_DEPTH_NEW_SIZE index to size
#if CUSTOM_ESM_DEPTH_NEW_SIZE == 1
    #undef CUSTOM_ESM_DEPTH_NEW_SIZE
    #define CUSTOM_ESM_DEPTH_NEW_SIZE 1024
#elif CUSTOM_ESM_DEPTH_NEW_SIZE == 2
    #undef CUSTOM_ESM_DEPTH_NEW_SIZE
    #define CUSTOM_ESM_DEPTH_NEW_SIZE 2048
#elif CUSTOM_ESM_DEPTH_NEW_SIZE == 3
    #undef CUSTOM_ESM_DEPTH_NEW_SIZE
    #define CUSTOM_ESM_DEPTH_NEW_SIZE 4096
#elif CUSTOM_ESM_DEPTH_NEW_SIZE == 4
    #undef CUSTOM_ESM_DEPTH_NEW_SIZE
    #define CUSTOM_ESM_DEPTH_NEW_SIZE 8192
#endif

// bypass CUSTOM_ESM_DEPTH_FILTERREDUCE if too low
#if CUSTOM_ESM_DEPTH_NEW_SIZE <= 2048
    #undef CUSTOM_ESM_DEPTH_FILTERREDUCE
    #define CUSTOM_ESM_DEPTH_FILTERREDUCE 0
#endif

cbuffer cb0_buf : register(b0)
{
    // float4 g_params
    float2 cb0_m0 : packoffset(c0); // direction x or y
    // 1st pass    | 2nd pass    |
    // 0.000488281 | 0           | (1/2048 = 0.000488281)
    // 0           | 0.000488281 | (NPR skips 1st pass, outputs even lower to 1024, so 1/1024 = 0.000976562)

    float2 cb0_m1 : packoffset(c0.z); // bias
    // 0.00251256
    // 0.00251256

    // float4 g_gauss[2] weights
    float4 cb0_m2 : packoffset(c1);
    float4 cb0_m3 : packoffset(c2);
    // 0.199471 (doesn't seem to change between PVs, but if push comes to shove, find sigma)
    // 0.176033 (sigma = 2 https://www.desmos.com/calculator/mesxth4dcc)
    // 0.120985 (same for NPR)
    // 0.0647588
    // 0.0269955
    // 0.00876415
    // 0.00221592
    // 0.000436341
};

// (for unroll)
static const float gaussian_default[8] = {0.199471, 0.176033, 0.120985, 0.0647588, 0.0269955, 0.00876415, 0.00221592, 0.000436341};

SamplerState s0 : register(s0);
Texture2D<float4> t0 : register(t0);

#include "./common1.hlsl"

static float2 TEXCOORD;
static float SV_Target;

struct SPIRV_Cross_Input
{
    float4 SV_Position : SV_Position;
    float2 TEXCOORD : TEXCOORD0;
};

struct SPIRV_Cross_Output
{
    float SV_Target : SV_Target0;
};

float get_gaussian_weight(float x, float s)
{
    return exp(-x * x * rcp(2.0 * s * s));
}

float normalize_sample(float x) {
#if CUSTOM_ESM_DEPTH_NORMALIZE // NPR horizontal axis only
    return x * 0.5 + 0.5;
#else
    return x;
#endif
}

void frag_main()
{
    const bool isNPRFirstPass = cb0_m0.x < 0.0009766 && cb0_m0.y == 0;

    float4 _55 = t0.SampleLevel(s0, float2(TEXCOORD.x, TEXCOORD.y), 0.0f); // x only, samples shadows dsv (1 = far, 0 = near)
    _55.x = normalize_sample(_55.x);

#if DEVELOPMENT
    // verify cb0_m2.x == 0.199471
    if (abs(cb0_m2.x - 0.199471) > 0.00001) {
        SV_Target = 0; // fully shadow
        return;
    }
#endif

    float _56 = _55.x; 
    if (_56 > 0.999000012874603271484375f) // edges to sky
    {
        // SV_Target = 0; return;

        // quick 2 taps
        float4 _72 = t0.SampleLevel(s0, float2(TEXCOORD.x + cb0_m0.x, TEXCOORD.y + cb0_m0.y), 0.0f);
        float _73 = normalize_sample(_72.x);
        bool _74 = _73 < 0.999000012874603271484375f;
        float _76 = _74 ? (_73 + 1.1000000085914507508277893066406e-05f) : 1.1000000085914507508277893066406e-05f;
        float4 _82 = t0.SampleLevel(s0, float2(TEXCOORD.x - cb0_m0.x, TEXCOORD.y - cb0_m0.y), 0.0f);
        float _83 = normalize_sample(_82.x);
        bool _84 = _83 < 0.999000012874603271484375f;
        SV_Target = ((_84 ? (_83 + _76) : _76) / (_84 ? (_74 ? 2.000010013580322265625f : 1.000010013580322265625f) : (_74 ? 1.000010013580322265625f : 9.9999997473787516355514526367188e-06f))) + cb0_m1.x;
        // (doesn't seem to be artifacting at higher res)
    }
    else // faces
    {
        // SV_Target = 1; return;

#if CUSTOM_ESM_DEPTH_NEW_SIZE == 0
        // original 16-tap Gaussian blur

        // backup to use when tap sample is sky, added bias to center sample
        float _97 = _56 + cb0_m1.y; 
       
        // 2 taps
        float _106 = TEXCOORD.x + cb0_m0.x;
        float _107 = TEXCOORD.y + cb0_m0.y;
        float _108 = TEXCOORD.x - cb0_m0.x;
        float _109 = TEXCOORD.y - cb0_m0.y;
        float4 _112 = t0.SampleLevel(s0, float2(_106, _107), 0.0f);
        float _113 = normalize_sample(_112.x);
        float4 _122 = t0.SampleLevel(s0, float2(_108, _109), 0.0f);
        float _123 = normalize_sample(_122.x);

        // 2 taps
        float _127 = _106 + cb0_m0.x;
        float _128 = cb0_m0.y + _107;
        float _129 = _108 - cb0_m0.x;
        float _130 = _109 - cb0_m0.y;
        float4 _133 = t0.SampleLevel(s0, float2(_127, _128), 0.0f);
        float _134 = normalize_sample(_133.x);
        float4 _143 = t0.SampleLevel(s0, float2(_129, _130), 0.0f);
        float _144 = normalize_sample(_143.x);

        // 2 taps
        float _148 = _127 + cb0_m0.x;
        float _149 = cb0_m0.y + _128;
        float _150 = _129 - cb0_m0.x;
        float _151 = _130 - cb0_m0.y;
        float4 _154 = t0.SampleLevel(s0, float2(_148, _149), 0.0f);
        float _155 = normalize_sample(_154.x);
        float4 _164 = t0.SampleLevel(s0, float2(_150, _151), 0.0f);
        float _165 = normalize_sample(_164.x);

        // 2 taps
        float _169 = _148 + cb0_m0.x;
        float _170 = cb0_m0.y + _149;
        float _171 = _150 - cb0_m0.x;
        float _172 = _151 - cb0_m0.y;
        float4 _175 = t0.SampleLevel(s0, float2(_169, _170), 0.0f);
        float _176 = normalize_sample(_175.x);
        float4 _185 = t0.SampleLevel(s0, float2(_171, _172), 0.0f);
        float _186 = normalize_sample(_185.x);

        // 2 taps
        float _190 = _169 + cb0_m0.x;
        float _191 = cb0_m0.y + _170;
        float _192 = _171 - cb0_m0.x;
        float _193 = _172 - cb0_m0.y;
        float4 _196 = t0.SampleLevel(s0, float2(_190, _191), 0.0f);
        float _197 = normalize_sample(_196.x);
        float4 _206 = t0.SampleLevel(s0, float2(_192, _193), 0.0f);
        float _207 = normalize_sample(_206.x);

        // 2 taps
        float _211 = _190 + cb0_m0.x;
        float _212 = cb0_m0.y + _191;
        float _213 = _192 - cb0_m0.x;
        float _214 = _193 - cb0_m0.y;
        float4 _217 = t0.SampleLevel(s0, float2(_211, _212), 0.0f);
        float _218 = normalize_sample(_217.x);
        float4 _227 = t0.SampleLevel(s0, float2(_213, _214), 0.0f);
        float _228 = normalize_sample(_227.x);

        // 1 tap
        float4 _238 = t0.SampleLevel(s0, float2(_211 + cb0_m0.x, cb0_m0.y + _212), 0.0f);
        float _239 = normalize_sample(_238.x);

        // (decomp decided to some weighted sum here)
        float _244 = mad(cb0_m3.w, (_239 > 0.999000012874603271484375f) ? _97 : _239, mad(cb0_m3.z, (_228 > 0.999000012874603271484375f) ? _97 : _228, mad(cb0_m3.z, (_218 > 0.999000012874603271484375f) ? _97 : _218, mad(cb0_m3.y, (_207 > 0.999000012874603271484375f) ? _97 : _207, mad(cb0_m3.y, (_197 > 0.999000012874603271484375f) ? _97 : _197, mad((_186 > 0.999000012874603271484375f) ? _97 : _186, cb0_m3.x, mad((_176 > 0.999000012874603271484375f) ? _97 : _176, cb0_m3.x, mad(cb0_m2.w, (_165 > 0.999000012874603271484375f) ? _97 : _165, mad(cb0_m2.w, (_155 > 0.999000012874603271484375f) ? _97 : _155, mad(cb0_m2.z, (_144 > 0.999000012874603271484375f) ? _97 : _144, mad(cb0_m2.z, (_134 > 0.999000012874603271484375f) ? _97 : _134, mad(cb0_m2.y, (_123 > 0.999000012874603271484375f) ? _97 : _123, mad(cb0_m2.y, (_113 > 0.999000012874603271484375f) ? _97 : _113, mad(_56, cb0_m2.x, 1.1000000085914507508277893066406e-05f))))))))))))));

        // 1 tap
        float4 _248 = t0.SampleLevel(s0, float2(_213 - cb0_m0.x, _214 - cb0_m0.y), 0.0f);
        float _249 = normalize_sample(_248.x);

        // combine final with sum & divide safe by total weight
        SV_Target = mad(cb0_m3.w, (_249 > 0.999000012874603271484375f) ? _97 : _249, _244) / (mad(cb0_m3.w, 2.0f, mad(cb0_m3.z, 2.0f, mad(cb0_m3.y, 2.0f, mad(cb0_m3.x, 2.0f, mad(cb0_m2.w, 2.0f, mad(cb0_m2.z, 2.0f, mad(cb0_m2.y, 2.0f, cb0_m2.x))))))) + 9.9999997473787516355514526367188e-06f);
#else
        const uint newSize = CUSTOM_ESM_DEPTH_NEW_SIZE;
        const float newSizeRatio = newSize / min(2048.f, newSize); // TODO: NPR is for 1024 old output, do we care? we cant unroll if dynamic
        const float newSizeRatioInv = 1 / newSizeRatio;

        const float centerBiased = _56 + cb0_m1.y; // backup to use when tap sample is sky
        float cSum = _56; // color accumulator/sum, init = center
        float wSum = 1.0; // weight accumulator/sum, init = fully center

        // reduce radius and makeup by increased sigma
        const float radiusReduce = CUSTOM_ESM_DEPTH_FILTERREDUCE + 1;
        const float sigmaIncrease = radiusReduce - 1;
        const float sigmaIncreaseUser = CUSTOM_ESM_DEPTH_SIGMAINCREASE * 0.5;

        const float sigma = (2 * newSizeRatio) + sigmaIncreaseUser + sigmaIncrease; // TODO: expose to user customization, but this will stop unrolling gaussian blur weights
        const float radius = 7 / radiusReduce * newSizeRatio;
        float2 step = cb0_m0 * newSizeRatioInv; // which axis?
        [unroll] for (float i = 1; i <= radius; i++)
        {
            // pos & neg
            float a = t0.SampleLevel(s0, TEXCOORD + step * i, 0).x;
            float b = t0.SampleLevel(s0, TEXCOORD - step * i, 0).x;

            // normalize
            a = normalize_sample(a);
            b = normalize_sample(b);

            // weight
            float w = get_gaussian_weight(i, sigma);

            // color accum: use if not sky
            cSum += w * ((a > 0.999) ? centerBiased : a);
            cSum += w * ((b > 0.999) ? centerBiased : b);

            // weight accum: 2 samples
            wSum += 2 * w;
        }

        // weighted sum safe
        SV_Target = cSum / (wSum + 1e-5);
#endif

// #if CUSTOM_ESM_DEPTH_NEW_SIZE == 0
//         const float radius = 8; // like original 16 taps
//         const float sigma = 2;
//         float2 step = cb0_m0;
// #else
//         const uint newSize = CUSTOM_ESM_DEPTH_NEW_SIZE;
//         const float newSizeRatio = newSize / 2048.f;
//         const float newSizeRatioInv = 1 / newSizeRatio;
//         const float radius = ceil(7 * newSizeRatio); // 7 or 8, idk
//         const float sigma = 2 * newSizeRatio;
//         float2 step = cb0_m0 * newSizeRatioInv; // 1/4096 along the active axis
// #endif
//         float centerBiased = _56 + cb0_m1.y; // backup to use when tap sample is sky
//         float cSum = _56; // color accumulator/sum, init = center
//         float wSum = 1.0; // weight accumulator/sum, init = fully center
// 
//         [unroll] for (float i = 1; i <= radius; i++)
//         {
//             // weight (follows original cached in g_gauss)
//             float w = get_gaussian_weight(i, sigma);
// 
//             // pos & neg
//             float a = t0.SampleLevel(s0, TEXCOORD + step * i, 0).x;
//             float b = t0.SampleLevel(s0, TEXCOORD - step * i, 0).x;
// 
//             // color accum: use if not sky
//             cSum += w * ((a > 0.999) ? centerBiased : a);
//             cSum += w * ((b > 0.999) ? centerBiased : b);
// 
//             // weight accum: 2 samples
//             wSum += 2 * w;
//         }
// 
//         // weighted sum safe
//         SV_Target = cSum / (wSum + 1e-5);
    }
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    TEXCOORD = stage_input.TEXCOORD;
    frag_main();

    SPIRV_Cross_Output stage_output;
    stage_output.SV_Target = SV_Target;
    return stage_output;
}
