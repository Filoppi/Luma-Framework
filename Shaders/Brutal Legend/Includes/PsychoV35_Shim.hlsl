// Minimal shim providing the RenoDX gamut symbols used by PsychoV35.hlsl.
// Gamut data (CIE 170-2 halfspace edges) copied verbatim from RenoDX (https://github.com/clshortfuse/renodx),
// src/shaders/color/gamut.hlsl at commit 8d360960a8bdeccada1a1babf7f02215c15c82e8.
// Copyright (c) 2025 Carlos Lopez Jr. Licensed under the MIT License; the full notice is in PsychoV35.hlsl.
#ifndef RENODX_SHADERS_BRUTAL_LEGEND_PSYCHOV35_SHIM_HLSL_
#define RENODX_SHADERS_BRUTAL_LEGEND_PSYCHOV35_SHIM_HLSL_

namespace renodx {
namespace color {

static const float3x3 BT2020_TO_BT709_MAT = float3x3(
    1.6604910f, -0.5876411f, -0.0728499f,
    -0.1245505f, 1.1328999f, -0.0083494f,
    -0.0181508f, -0.1005789f, 1.1187297f);

namespace gamut {

static const int CIE1702_EDGE_COUNT = 7;
static const float2 CIE1702_HALFSPACE_NORMALS[CIE1702_EDGE_COUNT] = {
  float2(-0.043889f, -0.006807f),
  float2(-0.007821f, -0.008564f),
  float2(-0.000604f, -0.007942f),
  float2(0.f, -0.080835f),
  float2(0.953597f, 0.307020f),
  float2(-0.060969f, 0.019752f),
  float2(-0.106895f, 0.004035f),
};
static const float CIE1702_HALFSPACE_NUMERATORS[CIE1702_EDGE_COUNT] = {
  0.0065035249f,
  0.00104900495f,
  0.000207697044f,
  0.00165556648f,
  0.252472349f,
  0.0241967351f,
  0.0199621232f,
};

}  // namespace gamut
}  // namespace color
}  // namespace renodx

#endif  // RENODX_SHADERS_BRUTAL_LEGEND_PSYCHOV35_SHIM_HLSL_
