#include "Includes/Common.hlsl"
#include "Includes/neutwo.hlsl"
// ============================================================================
// SHADER: 0x4517077B.ps_5_0.hlsl
// SOURCE: Granblue Fantasy Relink
// TYPE: Pixel Shader (Image Blend/Multiply Operation)
// ============================================================================
// SUMMARY:
// This shader performs a blend/multiply operation between two textures:
// - g_Texture0: The base/background texture (sampled at SV_Position)
// - g_ImageTexture: The overlay texture (sampled at UV coordinates)
//
// The shader supports 11 different blend modes (controlled by filterType_):
//   0: Additive blend (multiply overlay by color, add to base)
//   1: Multiply blend (multiply overlay color with base)
//   2: Screen blend (inverse multiply)
//   3: Overlay blend (mix of multiply and screen based on base)
//   4: Hard light (overlay with different weighting)
//   5: Difference (absolute difference between colors)
//   6: Exclusion (similar to difference but softer)
//   7: Color dodge (brightening effect)
//   8: Color burn (darkening effect)
//   9: Linear dodge (additive)
//  10: Linear burn (subtractive)
//  11-18: Various other blend modes including saturation, hue, etc.
//
// The materialColor.yzw controls the intensity/multiplier for the overlay.
// ============================================================================

cbuffer ParamBuffer : register(b1)
{
   float4 materialColor : packoffset(c0); // Blend color/intensity (xyz = multiplier, w = unused)
   float4x4 coordMatrix : packoffset(c1); // Coordinate transformation matrix
   int filterType_ : packoffset(c5);      // Blend mode selector (0-18+)
}

SamplerState g_ImageTextureSampler_s : register(s1);
Texture2D<float4> g_Texture0 : register(t0);     // Base texture
Texture2D<float4> g_ImageTexture : register(t1); // Overlay texture

#define cmp -

void main(float4 v0: SV_Position0,
          float2 v1: TEXCOORD0,      // Screen position (x,y = pixel coords)
          out float4 o0: SV_Target0) // Output color
{
   float4 r0, r1, r2, r3, r4, r5, r6, r7,
       r8; // Temporary registers  // ==========================================================================
   // STEP 1: Sample both textures
   // ==========================================================================

   // Load base texture directly at pixel position (no filtering)
   r0.xy = (int2)v0.xy;                        // Convert screen pos to integer texel coords
   r0.zw = float2(0, 0);                       // ZW = 0 for Load() method (no LOD)
   int3 pixelCoord = int3((int2)v0.xy, 0);     // Integer pixel coordinates for Load()
   r0.xyzw = g_Texture0.Load(pixelCoord).xyzw; // Sample base texture (unfiltered)
   // o0 = r0;                                     // debug: output base texture color before blending
   // return;

   // Keep vanilla blend math in gamma space (assembly-faithful):
   // do not transform t0/t1 before the switch-based blend operation.
   // Sample overlay texture with texture sampler (filtered)
   r1.xyzw = g_ImageTexture.Sample(g_ImageTextureSampler_s, v1.xy).xyzw;
   // Multiply overlay by material color intensity
   r2.xyzw = materialColor.yzwx * r1.yzwx; // r2 = overlay * materialColor (swizzled)

   // ==========================================================================
   // STEP 2: Apply blend mode based on filterType_
   // ==========================================================================

   switch (filterType_) // filterType_ value determines blend mode
   {
   // ------------------------------------------------------------------------
   // CASE 0: ADDITIVE BLEND
   // Formula: result = overlay * color + base
   // Effect: Brightens by adding overlay on top of base
   // ------------------------------------------------------------------------
   case 0:
      r3.xyz = r1.xyz * materialColor.xyz + -r0.xyz; // r3 = (overlay * color) - base
      o0.xyz = r1.www * r3.xyz + r0.xyz;             // o0 = overlay.w * r3 + base
      o0.w = 1;                                      // Output alpha = 1
      break;

   // ------------------------------------------------------------------------
   // CASE 1: MULTIPLY BLEND
   // Formula: result = base * (1 - overlay) + (overlay * color * min(overlay, base) - base)
   // Effect: Darkens by multiplying base with overlay
   // ------------------------------------------------------------------------
   case 1:
      r3.xyz = min(r2.wxy, r0.xyz);      // r3 = min(overlay*color, base)
      r3.xyz = r3.xyz + -r0.xyz;         // r3 = r3 - base
      o0.xyz = r1.www * r3.xyz + r0.xyz; // o0 = overlay.w * r3 + base
      o0.w = r0.w;                       // Preserve base alpha
      break;

   // ------------------------------------------------------------------------
   // CASE 2: SCREEN BLEND
   // Formula: result = base * (overlay * color)
   // Effect: Inverse multiply - brightens both textures
   // ------------------------------------------------------------------------
   case 2:
      o0.xyz = r2.wxy * r0.xyz; // o0 = (overlay * color) * base
      o0.w = 1;
      break;

   // ------------------------------------------------------------------------
   // CASE 3: OVERLAY BLEND
   // Formula: If base < 0.5: multiply, else: screen
   // Effect: Combines multiply and screen based on base color brightness
   // ------------------------------------------------------------------------
   case 3:
      r3.xyz = cmp(r2.wxy == float3(0, 0, 0)); // Check for zero overlay
      r4.xyz = float3(1, 1, 1) + -r0.xyz;      // r4 = 1 - base
      r4.xyz = r4.xyz / r2.wxy;                // r4 = (1 - base) / (overlay * color)
      r4.xyz = float3(1, 1, 1) + -r4.xyz;      // r4 = 1 - r4
      r4.xyz = max(float3(0, 0, 0), r4.xyz);   // Clamp to [0,1]
      r3.xyz = r3.xyz ? r2.wxy : r4.xyz;       // If overlay=0: use overlay, else: use calculated
      r3.xyz = min(r3.xyz, r0.xyz);            // r3 = min(r3, base)
      r3.xyz = r3.xyz + -r0.xyz;               // r3 = r3 - base
      o0.xyz = r1.www * r3.xyz + r0.xyz;       // o0 = overlay.w * r3 + base
      o0.w = r0.w;
      break;

   // ------------------------------------------------------------------------
   // CASE 4: HARD LIGHT BLEND
   // Formula: Similar to overlay but swapped conditions
   // Effect: Harsher version of overlay blend
   // ------------------------------------------------------------------------
   case 4:
      r3.xyz = r1.xyz * materialColor.xyz + r0.xyz; // r3 = overlay * color + base
      r3.xyz = float3(-1, -1, -1) + r3.xyz;         // r3 = r3 - 1
      r3.xyz = max(float3(0, 0, 0), r3.xyz);        // Clamp to [0,1]
      r3.xyz = r3.xyz + -r0.xyz;                    // r3 = r3 - base
      o0.xyz = r1.www * r3.xyz + r0.xyz;            // o0 = overlay.w * r3 + base
      o0.w = r0.w;
      break;

   // ------------------------------------------------------------------------
   // CASE 5: DIFFERENCE BLEND
   // Formula: |base - overlay * color|
   // Effect: Shows absolute difference between colors
   // ------------------------------------------------------------------------
   case 5:
      r3.x = dot(r0.xyz, float3(0.3, 0.59, 0.11)); // Convert base to luminance
      r3.y = dot(r2.wxy, float3(0.3, 0.59, 0.11)); // Convert overlay*color to luminance
      r3.x = cmp(r3.x < r3.y);                     // Check if base < overlay
      r3.xyz = r3.xxx ? r0.xyz : r2.wxy;           // If base < overlay: use base, else: use overlay
      r3.xyz = r3.xyz + -r0.xyz;                   // r3 = r3 - base
      o0.xyz = r1.www * r3.xyz + r0.xyz;           // o0 = overlay.w * r3 + base
      o0.w = r0.w;
      break;

   // ------------------------------------------------------------------------
   // CASE 6: EXCLUSION BLEND
   // Formula: base + overlay*color - 2*base*(overlay*color)
   // Effect: Similar to difference but softer, lower contrast
   // ------------------------------------------------------------------------
   case 6:
      r3.xyz = max(r2.wxy, r0.xyz);      // r3 = max(overlay*color, base)
      r3.xyz = r3.xyz + -r0.xyz;         // r3 = r3 - base
      o0.xyz = r1.www * r3.xyz + r0.xyz; // o0 = overlay.w * r3 + base
      o0.w = r0.w;
      break;

   // ------------------------------------------------------------------------
   // CASE 7: COLOR DODGE BLEND
   // Formula: base / (1 - overlay*color)
   // Effect: Brightens base by blending with overlay (lightening effect)
   // ------------------------------------------------------------------------
   case 7:
      r3.xyz = float3(1, 1, 1) + -r0.xyz;                     // r3 = 1 - base
      r4.xyz = -r1.xyz * materialColor.xyz + float3(1, 1, 1); // r4 = 1 - overlay*color
      r3.xyz = -r3.xyz * r4.xyz + -r0.xyz;                    // r3 = -(1-base)*(1-overlay*color) - base
      r3.xyz = float3(1, 1, 1) + r3.xyz;                      // r3 = 1 + r3
      o0.xyz = r1.www * r3.xyz + r0.xyz;                      // o0 = overlay.w * r3 + base
      o0.w = r0.w;
      break;

   // ------------------------------------------------------------------------
   // CASE 8: COLOR BURN BLEND
   // Formula: 1 - (1 - base) / (overlay*color)
   // Effect: Darkens base by blending with overlay (burning effect)
   // ------------------------------------------------------------------------
   case 8:
      r3.xyz = cmp(r2.wxy == float3(1, 1, 1));                // Check if overlay*color = 1
      r4.xyz = -r1.xyz * materialColor.xyz + float3(1, 1, 1); // r4 = 1 - overlay*color
      r4.xyz = r0.xyz / r4.xyz;                               // r4 = base / (1 - overlay*color)
      r4.xyz = min(float3(1, 1, 1), r4.xyz);                  // Clamp to [0,1]
      r3.xyz = r3.xyz ? r2.wxy : r4.xyz;                      // If overlay=1: use overlay, else: use calculated
      r3.xyz = r3.xyz + -r0.xyz;                              // r3 = r3 - base
      o0.xyz = r1.www * r3.xyz + r0.xyz;                      // o0 = overlay.w * r3 + base
      o0.w = r0.w;
      break;

   // ------------------------------------------------------------------------
   // CASE 9: LINEAR DODGE (ADDITIVE) BLEND
   // Formula: base + overlay*color
   // Effect: Simple additive blending - brightens image
   // ------------------------------------------------------------------------
   case 9:
      o0.xyz = r1.www * r2.wxy + r0.xyz; // o0 = overlay.w * (overlay*color) + base
      o0.w = r0.w;
      break;

   // ------------------------------------------------------------------------
   // CASE 10: LINEAR BURN BLEND
   // Formula: base + overlay*color - 1 (clamped)
   // Effect: Subtractive blending - darkens image
   // ------------------------------------------------------------------------
   case 10:                                        // Note: Original has duplicate case 1 labels, this is likely case 10
      r3.x = dot(r0.xyz, float3(0.3, 0.59, 0.11)); // Convert base to luminance
      r3.y = dot(r2.wxy, float3(0.3, 0.59, 0.11)); // Convert overlay*color to luminance
      r3.x = cmp(r3.y < r3.x);                     // Check if overlay < base
      r3.xyz = r3.xxx ? r0.xyz : r2.wxy;           // If overlay < base: use base, else: use overlay
      r3.xyz = r3.xyz + -r0.xyz;                   // r3 = r3 - base
      o0.xyz = r1.www * r3.xyz + r0.xyz;           // o0 = overlay.w * r3 + base
      o0.w = r0.w;
      break;

   // ------------------------------------------------------------------------
   // CASE 11-18: ADDITIONAL BLEND MODES
   // Various complex blend operations including invert, bit-wise, and
   // advanced color manipulations
   // ------------------------------------------------------------------------

   // CASE 11: Complex blend with dot products
   case 11:
      r3.xyz = cmp(r0.xyz < float3(0.5, 0.5, 0.5));           // Check if base < 0.5
      r3.w = dot(r2.ww, r0.xx);                               // Dot product of components
      r4.xyz = float3(1, 1, 1) + -r0.xyz;                     // r4 = 1 - base
      r4.xyz = r4.xyz + r4.xyz;                               // r4 = 2 * (1 - base)
      r5.xyz = -r1.xyz * materialColor.xyz + float3(1, 1, 1); // r5 = 1 - overlay*color
      r4.xyz = -r4.xyz * r5.xyz + float3(1, 1, 1);            // r4 = 1 - 2*(1-base)*(1-overlay*color)
      r5.x = r3.x ? r3.w : r4.x;                              // Conditional selection
      r3.x = dot(r2.xx, r0.yy);
      r5.y = r3.y ? r3.x : r4.y;
      r3.x = dot(r2.yy, r0.zz);
      r5.z = r3.z ? r3.x : r4.z;
      r3.xyz = r5.xyz + -r0.xyz;
      o0.xyz = r1.www * r3.xyz + r0.xyz;
      o0.w = r0.w;
      break;

   // CASE 12: Quadratic blend mode
   case 12:
      r3.xyz = cmp(r2.wxy < float3(0.5, 0.5, 0.5));           // Check if overlay*color < 0.5
      r4.xyz = r0.xyz + r0.xyz;                               // r4 = 2 * base
      r5.xyz = r0.xyz * r0.xyz;                               // r5 = base^2
      r6.xyz = -r2.wxy * float3(2, 2, 2) + float3(1, 1, 1);   // r6 = 1 - 2*(overlay*color)
      r5.xyz = r6.xyz * r5.xyz;                               // r5 = (1-2*overlay*color) * base^2
      r5.xyz = r4.xyz * r2.wxy + r5.xyz;                      // r5 = 2*base*(overlay*color) + r5
      r6.xyz = sqrt(r0.xyz);                                  // r6 = sqrt(base)
      r7.xyz = r2.wxy * float3(2, 2, 2) + float3(-1, -1, -1); // r7 = 2*(overlay*color) - 1
      r8.xyz = -r1.xyz * materialColor.xyz + float3(1, 1, 1); // r8 = 1 - overlay*color
      r4.xyz = r8.xyz * r4.xyz;                               // r4 = (1-overlay*color) * 2*base
      r4.xyz = r6.xyz * r7.xyz + r4.xyz;                      // r4 = sqrt(base)*(2*overlay*color-1) + r4
      r3.xyz = r3.xyz ? r5.xyz : r4.xyz;                      // Conditional selection
      r3.xyz = r3.xyz + -r0.xyz;
      o0.xyz = r1.www * r3.xyz + r0.xyz;
      o0.w = r0.w;
      break;

   // CASE 13: Another complex blend with dot products
   case 13:
      r3.xyz = cmp(r2.wxy < float3(0.5, 0.5, 0.5));           // Check if overlay*color < 0.5
      r3.w = dot(r0.xx, r2.ww);                               // Dot product
      r4.xyz = -r1.xyz * materialColor.xyz + float3(1, 1, 1); // r4 = 1 - overlay*color
      r4.xyz = r4.xyz + r4.xyz;                               // r4 = 2 * (1 - overlay*color)
      r5.xyz = float3(1, 1, 1) + -r0.xyz;                     // r5 = 1 - base
      r4.xyz = -r4.xyz * r5.xyz + float3(1, 1, 1);            // r4 = 1 - 2*(1-overlay*color)*(1-base)
      r5.x = r3.x ? r3.w : r4.x;                              // Conditional selection
      r3.x = dot(r0.yy, r2.xx);
      r5.y = r3.y ? r3.x : r4.y;
      r3.x = dot(r0.zz, r2.yy);
      r5.z = r3.z ? r3.x : r4.z;
      r3.xyz = r5.xyz + -r0.xyz;
      o0.xyz = r1.www * r3.xyz + r0.xyz;
      o0.w = r0.w;
      break;

   // CASE 14: Saturation-like blend
   case 14:
      r3.xyz = cmp(r2.wxy < float3(0.5, 0.5, 0.5)); // Check if overlay*color < 0.5
      r4.xyz = r2.wxy + r2.wxy;                     // r4 = 2 * (overlay*color)
      r5.xyz = cmp(r2.wxy == float3(0, 0, 0));      // Check if overlay*color = 0
      r6.xyz = float3(1, 1, 1) + -r0.xyz;           // r6 = 1 - base
      r6.xyz = r6.xyz / r4.xyz;                     // r6 = (1-base) / (2*overlay*color)
      r6.xyz = float3(1, 1, 1) + -r6.xyz;           // r6 = 1 - r6
      r6.xyz = max(float3(0, 0, 0), r6.xyz);        // Clamp to [0,1]
      r4.xyz = r5.xyz ? r4.xyz : r6.xyz;            // If overlay=0: use 2*overlay, else: use calculated
      r5.xyz = r1.xyz * materialColor.xyz + float3(-0.5, -0.5, -0.5);
      r6.xyz = r5.xyz + r5.xyz;                      // r6 = 2 * r5
      r7.xyz = cmp(r5.xyz == float3(0.5, 0.5, 0.5)); // Check condition
      r5.xyz = -r5.xyz * float3(2, 2, 2) + float3(1, 1, 1);
      r5.xyz = r0.xyz / r5.xyz;
      r5.xyz = min(float3(1, 1, 1), r5.xyz);
      r5.xyz = r7.xyz ? r6.xyz : r5.xyz;
      r3.xyz = r3.xyz ? r4.xyz : r5.xyz;
      r3.xyz = r3.xyz + -r0.xyz;
      o0.xyz = r1.www * r3.xyz + r0.xyz;
      o0.w = r0.w;
      break;

   // CASE 15: Hard mix blend
   case 15:
      r3.xyz = cmp(r2.wxy < float3(0.5, 0.5, 0.5)); // Check if overlay*color < 0.5
      r4.xyz = r2.wxy * float3(2, 2, 2) + r0.xyz;   // r4 = 2*(overlay*color) + base
      r4.xyz = float3(-1, -1, -1) + r4.xyz;         // r4 = r4 - 1
      r4.xyz = max(float3(0, 0, 0), r4.xyz);        // Clamp to [0,1]
      r5.xyz = r1.xyz * materialColor.xyz + float3(-0.5, -0.5, -0.5);
      r5.xyz = r5.xyz * float3(2, 2, 2) + r0.xyz; // r5 = 2*(overlay*color-0.5) + base
      r3.xyz = r3.xyz ? r4.xyz : r5.xyz;          // Conditional selection
      r3.xyz = r3.xyz + -r0.xyz;
      o0.xyz = r1.www * r3.xyz + r0.xyz;
      o0.w = r0.w;
      break;

   // CASE 16: Soft mix blend
   case 16:
      r3.xyz = cmp(r2.wxy < float3(0.5, 0.5, 0.5)); // Check if overlay*color < 0.5
      r4.xyz = r2.wxy + r2.wxy;                     // r4 = 2 * (overlay*color)
      r4.xyz = min(r4.xyz, r0.xyz);                 // r4 = min(2*overlay*color, base)
      r5.xyz = r1.xyz * materialColor.xyz + float3(-0.5, -0.5, -0.5);
      r5.xyz = r5.xyz + r5.xyz;          // r5 = 2 * (overlay*color - 0.5)
      r5.xyz = max(r5.xyz, r0.xyz);      // r5 = max(r5, base)
      r3.xyz = r3.xyz ? r4.xyz : r5.xyz; // Conditional selection
      r3.xyz = r3.xyz + -r0.xyz;
      o0.xyz = r1.www * r3.xyz + r0.xyz;
      o0.w = r0.w;
      break;

   // CASE 17: Binary blend
   case 17:
      r3.xyz = cmp(r2.wxy < float3(0.5, 0.5, 0.5)); // Check if overlay*color < 0.5
      r4.xyz = r2.wxy + r2.wxy;                     // r4 = 2 * (overlay*color)
      r5.xyz = cmp(r2.wxy == float3(0, 0, 0));      // Check if overlay*color = 0
      r6.xyz = float3(1, 1, 1) + -r0.xyz;           // r6 = 1 - base
      r6.xyz = r6.xyz / r4.xyz;                     // r6 = (1-base) / (2*overlay*color)
      r6.xyz = float3(1, 1, 1) + -r6.xyz;           // r6 = 1 - r6
      r6.xyz = max(float3(0, 0, 0), r6.xyz);        // Clamp to [0,1]
      r4.xyz = r5.xyz ? r4.xyz : r6.xyz;            // Conditional selection
      r5.xyz = r1.xyz * materialColor.xyz + float3(-0.5, -0.5, -0.5);
      r6.xyz = r5.xyz + r5.xyz;                      // r6 = 2 * r5
      r7.xyz = cmp(r5.xyz == float3(0.5, 0.5, 0.5)); // Check condition
      r5.xyz = -r5.xyz * float3(2, 2, 2) + float3(1, 1, 1);
      r5.xyz = r0.xyz / r5.xyz;
      r5.xyz = min(float3(1, 1, 1), r5.xyz);
      r5.xyz = r7.xyz ? r6.xyz : r5.xyz;
      r3.xyz = r3.xyz ? r4.xyz : r5.xyz;
      r3.xyz = cmp(r3.xyz < float3(0.5, 0.5, 0.5));        // Final threshold
      r3.xyz = r3.xyz ? float3(0, 0, 0) : float3(1, 1, 1); // Binarize to black or white
      r3.xyz = r3.xyz + -r0.xyz;
      o0.xyz = r1.www * r3.xyz + r0.xyz;
      o0.w = r0.w;
      break;

   // CASE 18: Absolute difference blend
   case 18:
      r3.xyz = -r1.xyz * materialColor.xyz + r0.xyz; // r3 = base - overlay*color
      r3.xyz = abs(r3.xyz) + -r0.xyz;                // r3 = |base - overlay*color| - base
      o0.xyz = r1.www * r3.xyz + r0.xyz;             // o0 = overlay.w * r3 + base
      o0.w = r0.w;
      break;

   // CASE 19: Subtractive blend
   case 19:
      r3.xyz = r1.xyz * materialColor.xyz + r0.xyz; // r3 = overlay*color + base
      r4.xyz = r2.wxy * r0.xyz;                     // r4 = (overlay*color) * base
      r3.xyz = -r4.xyz * float3(2, 2, 2) + r3.xyz;  // r3 = overlay*color + base - 4*(overlay*color)*base
      r3.xyz = r3.xyz + -r0.xyz;
      o0.xyz = r1.www * r3.xyz + r0.xyz;
      o0.w = r0.w;
      break;

   // CASE 20: Soft light variant
   case 20:
      r3.xyz = -r1.xyz * materialColor.xyz + r0.xyz; // r3 = base - overlay*color
      r3.xyz = max(float3(0, 0, 0), r3.xyz);         // Clamp to [0,1]
      r3.xyz = r3.xyz + -r0.xyz;
      o0.xyz = r1.www * r3.xyz + r0.xyz;
      o0.w = r0.w;
      break;

   // CASE 21: Divide blend
   case 21:
      r1.xyz = r1.xyz * materialColor.xyz + float3(9.99999997e-07, 9.99999997e-07, 9.99999997e-07);
      r1.xyz = r0.xyz / r1.xyz; // r1 = base / (overlay*color + epsilon)
      r1.xyz = r1.xyz + -r0.xyz;
      o0.xyz = r1.www * r1.xyz + r0.xyz;
      o0.w = r0.w;
      break;

   // CASE 22-25: HSL/HSV Color Space Blends
   // These cases implement complex color space conversions for
   // saturation, hue, and value blending operations

   // CASE 22: Saturation blend
   case 22:
      r1.x = cmp(r0.y < r0.z);           // Compare G < B
      r1.xy = r1.xx ? r0.zy : r0.yz;     // Swap if needed
      r3.x = cmp(r0.x < r1.x);           // Compare R < max
      r1.z = r0.x;                       // Save min
      r1.xyz = r3.xxx ? r1.xyz : r1.zyx; // Order RGB
      r1.y = min(r1.z, r1.y);            // Get min
      r1.y = r1.x + -r1.y;               // r1.y = max - min (chroma)
      r1.z = 1.00000001e-10 + r1.x;      // r1.z = max + epsilon
      r1.y = r1.y / r1.z;                // Normalize
      r1.z = cmp(r2.x < r2.y);           // Check overlay comparison
      r3.xy = r2.yx;                     // Swap overlay
      r3.zw = float2(-1, 0.666666985);   // Constants for hue calculation
      r4.xy = r3.yx;
      r4.zw = float2(0, -0.333332986);
      r3.xyzw = r1.zzzz ? r3.xyzw : r4.xyzw; // Select based on condition
      r1.z = cmp(r2.w < r3.x);
      r2.xyz = r3.xyw;
      r3.xyw = r2.wyx;
      r3.xyzw = r1.zzzz ? r2.xyzw : r3.xyzw;
      r1.z = min(r3.w, r3.y);
      r1.z = r3.x + -r1.z;
      r3.x = r3.w + -r3.y;
      r1.z = r1.z * 6 + 1.00000001e-10;
      r1.z = rcp(r1.z);
      r1.z = r3.x * r1.z + r3.z;
      r3.xyz = abs(r1.zzz) * float3(6, 6, 6) + float3(-3, -2, -4);
      r3.xyz = saturate(abs(r3.xyz) * float3(1, -1, -1) + float3(-1, 2, 2));
      r3.xyz = float3(-1, -1, -1) + r3.xyz;
      r3.xyz = r3.xyz * r1.yyy + float3(1, 1, 1);
      r1.xyz = r3.xyz * r1.xxx + -r0.xyz;
      o0.xyz = r1.www * r1.xyz + r0.xyz;
      o0.w = r0.w;
      break;

   // CASE 23: Hue blend
   case 23:
      r1.x = cmp(r0.y < r0.z);
      r3.xy = r0.zy;
      r3.zw = float2(-1, 0.666666985);
      r4.xy = r3.yx;
      r4.zw = float2(0, -0.333332986);
      r3.xyzw = r1.xxxx ? r3.xyzw : r4.xyzw;
      r1.x = cmp(r0.x < r3.x);
      r4.xyz = r3.xyw;
      r4.w = r0.x;
      r3.xyw = r4.wyx;
      r3.xyzw = r1.xxxx ? r4.xyzw : r3.xyzw;
      r1.x = min(r3.w, r3.y);
      r1.x = r3.x + -r1.x;
      r1.y = r3.w + -r3.y;
      r1.z = r1.x * 6 + 1.00000001e-10;
      r1.z = rcp(r1.z);
      r1.y = r1.y * r1.z + r3.z;
      r1.x = -r1.x * 0.5 + r3.x;
      r1.z = cmp(r2.x < r2.y);
      r2.xy = r1.zz ? r2.yx : r2.xy;
      r1.z = cmp(r2.w < r2.x);
      r3.xyz = r1.zzz ? r2.xyw : r2.wyx;
      r1.z = min(r3.z, r3.y);
      r1.z = r3.x + -r1.z;
      r3.x = -r1.z * 0.5 + r3.x;
      r3.x = r3.x * 2 + -1;
      r3.x = 1 + -abs(r3.x);
      r3.x = rcp(r3.x);
      r1.z = saturate(r3.x * r1.z);
      r3.xyz = abs(r1.yyy) * float3(6, 6, 6) + float3(-3, -2, -4);
      r3.xyz = saturate(abs(r3.xyz) * float3(1, -1, -1) + float3(-1, 2, 2));
      r1.y = r1.x * 2 + -1;
      r1.y = 1 + -abs(r1.y);
      r1.y = r1.y * r1.z;
      r3.xyz = float3(-0.5, -0.5, -0.5) + r3.xyz;
      r1.xyz = r3.xyz * r1.yyy + r1.xxx;
      r1.xyz = r1.xyz + -r0.xyz;
      o0.xyz = r1.www * r1.xyz + r0.xyz;
      o0.w = r0.w;
      break;

   // CASE 24: Value blend
   case 24:
      r1.x = cmp(r2.x < r2.y);
      r3.xy = r2.yx;
      r3.zw = float2(-1, 0.666666985);
      r4.xy = r3.yx;
      r4.zw = float2(0, -0.333332986);
      r3.xyzw = r1.xxxx ? r3.xyzw : r4.xyzw;
      r1.x = cmp(r2.w < r3.x);
      r2.xyz = r3.xyw;
      r3.xyw = r2.wyx;
      r3.xyzw = r1.xxxx ? r2.xyzw : r3.xyzw;
      r1.x = min(r3.w, r3.y);
      r1.x = r3.x + -r1.x;
      r1.y = r3.w + -r3.y;
      r1.z = r1.x * 6 + 1.00000001e-10;
      r1.z = rcp(r1.z);
      r1.y = r1.y * r1.z + r3.z;
      r1.z = -r1.x * 0.5 + r3.x;
      r2.z = r1.z * 2 + -1;
      r2.z = 1 + -abs(r2.z);
      r2.z = rcp(r2.z);
      r1.x = saturate(r2.z * r1.x);
      r2.z = cmp(r0.y < r0.z);
      r3.xy = r2.zz ? r0.zy : r0.yz;
      r2.z = cmp(r0.x < r3.x);
      r3.z = r0.x;
      r3.xyz = r2.zzz ? r3.xyz : r3.zyx;
      r2.z = min(r3.z, r3.y);
      r2.z = r3.x + -r2.z;
      r2.z = -r2.z * 0.5 + r3.x;
      r1.z = r2.z * r1.z;
      r3.xyz = abs(r1.yyy) * float3(6, 6, 6) + float3(-3, -2, -4);
      r3.xyz = saturate(abs(r3.xyz) * float3(1, -1, -1) + float3(-1, 2, 2));
      r1.y = r1.z * 2 + -1;
      r1.y = 1 + -abs(r1.y);
      r1.x = r1.y * r1.x;
      r3.xyz = float3(-0.5, -0.5, -0.5) + r3.xyz;
      r1.xyz = r3.xyz * r1.xxx + r1.zzz;
      r1.xyz = r1.xyz + -r0.xyz;
      o0.xyz = r1.www * r1.xyz + r0.xyz;
      o0.w = r0.w;
      break;

   // CASE 25: Combined HSL blend
   case 25:
      r1.x = cmp(r0.y < r0.z);
      r3.xy = r0.zy;
      r3.zw = float2(-1, 0.666666985);
      r4.xy = r3.yx;
      r4.zw = float2(0, -0.333332986);
      r3.xyzw = r1.xxxx ? r3.xyzw : r4.xyzw;
      r1.x = cmp(r0.x < r3.x);
      r4.xyz = r3.xyw;
      r4.w = r0.x;
      r3.xyw = r4.wyx;
      r3.xyzw = r1.xxxx ? r4.xyzw : r3.xyzw;
      r1.x = min(r3.w, r3.y);
      r1.x = r3.x + -r1.x;
      r1.y = r3.w + -r3.y;
      r1.z = r1.x * 6 + 1.00000001e-10;
      r1.z = rcp(r1.z);
      r1.y = r1.y * r1.z + r3.z;
      r1.z = -r1.x * 0.5 + r3.x;
      r1.z = r1.z * 2 + -1;
      r1.z = 1 + -abs(r1.z);
      r1.z = rcp(r1.z);
      r1.x = saturate(r1.x * r1.z);
      r1.z = cmp(r2.x < r2.y);
      r2.xy = r1.zz ? r2.yx : r2.xy;
      r1.z = cmp(r2.w < r2.x);
      r2.xyz = r1.zzz ? r2.xyw : r2.wyx;
      r1.z = min(r2.z, r2.y);
      r1.z = r2.x + -r1.z;
      r1.z = -r1.z * 0.5 + r2.x;
      r2.xyz = abs(r1.yyy) * float3(6, 6, 6) + float3(-3, -2, -4);
      r2.xyz = saturate(abs(r2.xyz) * float3(1, -1, -1) + float3(-1, 2, 2));
      r1.y = r1.z * 2 + -1;
      r1.y = 1 + -abs(r1.y);
      r1.x = r1.y * r1.x;
      r2.xyz = float3(-0.5, -0.5, -0.5) + r2.xyz;
      r1.xyz = r2.xyz * r1.xxx + r1.zzz;
      r1.xyz = r1.xyz + -r0.xyz;
      o0.xyz = r1.www * r1.xyz + r0.xyz;
      o0.w = r0.w;
      break;

   // ------------------------------------------------------------------------
   // DEFAULT: Fallback case
   // If an invalid filterType_ is provided, output a solid red color
   // ------------------------------------------------------------------------
   default:
      o0.xyzw = float4(1, 0, 0, 1);
      break;
   }
   // Fallback output: red with full alpha

   return;
}
