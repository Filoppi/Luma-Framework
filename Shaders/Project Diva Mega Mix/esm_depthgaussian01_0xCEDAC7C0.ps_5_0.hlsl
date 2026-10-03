// force vanilla
#ifdef CUSTOM_ESM_DEPTH_NEW_SIZE
  #undef CUSTOM_ESM_DEPTH_NEW_SIZE
  #define CUSTOM_ESM_DEPTH_NEW_SIZE 0
#endif

#ifdef CUSTOM_ESM_DEPTH_NORMALIZE
  #undef CUSTOM_ESM_DEPTH_NORMALIZE
#endif
#define CUSTOM_ESM_DEPTH_NORMALIZE 1

#include "Luma_MegaMix_ESMDepthGaussian.hlsl"

// cbuffer cb0_buf : register(b0)
// {
//     float2 cb0_m0 : packoffset(c0);
//     float2 cb0_m1 : packoffset(c0.z);
//     float4 cb0_m2 : packoffset(c1);
//     float4 cb0_m3 : packoffset(c2);
// };
// 
// SamplerState s0 : register(s1);
// Texture2D<float4> t0 : register(t2);
// 
// static float2 TEXCOORD;
// static float SV_Target;
// 
// struct SPIRV_Cross_Input
// {
//     float2 TEXCOORD : TEXCOORD1;
// };
// 
// struct SPIRV_Cross_Output
// {
//     float SV_Target : SV_Target0;
// };
// 
// void frag_main()
// {
//     float _59 = mad(t0.SampleLevel(s0, float2(TEXCOORD.x, TEXCOORD.y), 0.0f).x, 0.5f, 0.5f);
//     if (_59 > 0.999000012874603271484375f)
//     {
//         float4 _76 = t0.SampleLevel(s0, float2(TEXCOORD.x + cb0_m0.x, TEXCOORD.y + cb0_m0.y), 0.0f);
//         float _77 = _76.x;
//         bool _80 = mad(_77, 0.5f, 0.5f) < 0.999000012874603271484375f;
//         float _81 = _80 ? mad(_77, 0.5f, 0.500011026859283447265625f) : 1.1000000085914507508277893066406e-05f;
//         float _89 = mad(t0.SampleLevel(s0, float2(TEXCOORD.x - cb0_m0.x, TEXCOORD.y - cb0_m0.y), 0.0f).x, 0.5f, 0.5f);
//         bool _90 = _89 < 0.999000012874603271484375f;
//         SV_Target = ((_90 ? (_89 + _81) : _81) / (_90 ? (_80 ? 2.000010013580322265625f : 1.000010013580322265625f) : (_80 ? 1.000010013580322265625f : 9.9999997473787516355514526367188e-06f))) + cb0_m1.x;
//     }
//     else
//     {
//         float _103 = _59 + cb0_m1.y;
//         float _111 = TEXCOORD.x + cb0_m0.x;
//         float _112 = TEXCOORD.y + cb0_m0.y;
//         float _113 = TEXCOORD.x - cb0_m0.x;
//         float _114 = TEXCOORD.y - cb0_m0.y;
//         float _119 = mad(t0.SampleLevel(s0, float2(_111, _112), 0.0f).x, 0.5f, 0.5f);
//         float _130 = mad(t0.SampleLevel(s0, float2(_113, _114), 0.0f).x, 0.5f, 0.5f);
//         float _134 = _111 + cb0_m0.x;
//         float _135 = cb0_m0.y + _112;
//         float _136 = _113 - cb0_m0.x;
//         float _137 = _114 - cb0_m0.y;
//         float _142 = mad(t0.SampleLevel(s0, float2(_134, _135), 0.0f).x, 0.5f, 0.5f);
//         float _153 = mad(t0.SampleLevel(s0, float2(_136, _137), 0.0f).x, 0.5f, 0.5f);
//         float _157 = _134 + cb0_m0.x;
//         float _158 = cb0_m0.y + _135;
//         float _159 = _136 - cb0_m0.x;
//         float _160 = _137 - cb0_m0.y;
//         float _165 = mad(t0.SampleLevel(s0, float2(_157, _158), 0.0f).x, 0.5f, 0.5f);
//         float _176 = mad(t0.SampleLevel(s0, float2(_159, _160), 0.0f).x, 0.5f, 0.5f);
//         float _180 = _157 + cb0_m0.x;
//         float _181 = cb0_m0.y + _158;
//         float _182 = _159 - cb0_m0.x;
//         float _183 = _160 - cb0_m0.y;
//         float _188 = mad(t0.SampleLevel(s0, float2(_180, _181), 0.0f).x, 0.5f, 0.5f);
//         float _199 = mad(t0.SampleLevel(s0, float2(_182, _183), 0.0f).x, 0.5f, 0.5f);
//         float _203 = _180 + cb0_m0.x;
//         float _204 = cb0_m0.y + _181;
//         float _205 = _182 - cb0_m0.x;
//         float _206 = _183 - cb0_m0.y;
//         float _211 = mad(t0.SampleLevel(s0, float2(_203, _204), 0.0f).x, 0.5f, 0.5f);
//         float _222 = mad(t0.SampleLevel(s0, float2(_205, _206), 0.0f).x, 0.5f, 0.5f);
//         float _226 = _203 + cb0_m0.x;
//         float _227 = cb0_m0.y + _204;
//         float _228 = _205 - cb0_m0.x;
//         float _229 = _206 - cb0_m0.y;
//         float _234 = mad(t0.SampleLevel(s0, float2(_226, _227), 0.0f).x, 0.5f, 0.5f);
//         float _245 = mad(t0.SampleLevel(s0, float2(_228, _229), 0.0f).x, 0.5f, 0.5f);
//         float _257 = mad(t0.SampleLevel(s0, float2(_226 + cb0_m0.x, cb0_m0.y + _227), 0.0f).x, 0.5f, 0.5f);
//         float _262 = mad(cb0_m3.w, (_257 > 0.999000012874603271484375f) ? _103 : _257, mad(cb0_m3.z, (_245 > 0.999000012874603271484375f) ? _103 : _245, mad(cb0_m3.z, (_234 > 0.999000012874603271484375f) ? _103 : _234, mad(cb0_m3.y, (_222 > 0.999000012874603271484375f) ? _103 : _222, mad(cb0_m3.y, (_211 > 0.999000012874603271484375f) ? _103 : _211, mad((_199 > 0.999000012874603271484375f) ? _103 : _199, cb0_m3.x, mad((_188 > 0.999000012874603271484375f) ? _103 : _188, cb0_m3.x, mad(cb0_m2.w, (_176 > 0.999000012874603271484375f) ? _103 : _176, mad(cb0_m2.w, (_165 > 0.999000012874603271484375f) ? _103 : _165, mad(cb0_m2.z, (_153 > 0.999000012874603271484375f) ? _103 : _153, mad((_142 > 0.999000012874603271484375f) ? _103 : _142, cb0_m2.z, mad(cb0_m2.y, (_130 > 0.999000012874603271484375f) ? _103 : _130, mad(cb0_m2.y, (_119 > 0.999000012874603271484375f) ? _103 : _119, mad(_59, cb0_m2.x, 1.1000000085914507508277893066406e-05f))))))))))))));
//         float _268 = mad(t0.SampleLevel(s0, float2(_228 - cb0_m0.x, _229 - cb0_m0.y), 0.0f).x, 0.5f, 0.5f);
//         SV_Target = mad(cb0_m3.w, (_268 > 0.999000012874603271484375f) ? _103 : _268, _262) / (mad(cb0_m3.w, 2.0f, mad(cb0_m3.z, 2.0f, mad(cb0_m3.y, 2.0f, mad(cb0_m3.x, 2.0f, mad(cb0_m2.w, 2.0f, mad(cb0_m2.z, 2.0f, mad(cb0_m2.y, 2.0f, cb0_m2.x))))))) + 9.9999997473787516355514526367188e-06f);
//     }
// }
// 
// SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
// {
//     TEXCOORD = stage_input.TEXCOORD;
//     frag_main();
//     SPIRV_Cross_Output stage_output;
//     stage_output.SV_Target = SV_Target;
//     return stage_output;
// }
