#ifndef SRC_HARDWARE_BLENDS_EMULATION_HLSL
#define SRC_HARDWARE_BLENDS_EMULATION_HLSL

// Shared between c++ and hlsl, so the packing and the unpacking can never drift apart.
//
// When Luma composes a pass through a Rasterizer Ordered View instead of a render target, the fixed function output merger is taken out of the equation
// (there's no render target bound anymore), so the shader has to run the blend equation itself.
// To do that it needs the blend state the game had set, which we pack into "LumaData.CustomData1" ("D3D11_RENDER_TARGET_BLEND_DESC" of render target 0,
// which is the only one these passes ever use), while the constant blend factor (which isn't part of the desc, it's set by "OMSetBlendState()")
// goes into "LumaData.CustomData2" and "LumaData.CustomData3" as 4 halves.
//
// Bit layout of "LumaData.CustomData1" (31 bits of blend desc + 1 Luma flag):
//    [0]      BlendEnable
//    [1:5]    SrcBlend               (D3D11_BLEND)
//    [6:10]   DestBlend              (D3D11_BLEND)
//    [11:13]  BlendOp                (D3D11_BLEND_OP)
//    [14:18]  SrcBlendAlpha          (D3D11_BLEND)
//    [19:23]  DestBlendAlpha         (D3D11_BLEND)
//    [24:26]  BlendOpAlpha           (D3D11_BLEND_OP)
//    [27:30]  RenderTargetWriteMask  (D3D11_COLOR_WRITE_ENABLE)
//    [31]     Luma: emulate the blend in the shader (0 means the pass is drawing normally, through the output merger)
#define LUMA_BLEND_ENABLE_SHIFT     0u
#define LUMA_BLEND_ENABLE_MASK      0x1u
#define LUMA_BLEND_SRC_SHIFT        1u
#define LUMA_BLEND_SRC_MASK         0x1Fu
#define LUMA_BLEND_DEST_SHIFT       6u
#define LUMA_BLEND_DEST_MASK        0x1Fu
#define LUMA_BLEND_OP_SHIFT         11u
#define LUMA_BLEND_OP_MASK          0x7u
#define LUMA_BLEND_SRC_ALPHA_SHIFT  14u
#define LUMA_BLEND_SRC_ALPHA_MASK   0x1Fu
#define LUMA_BLEND_DEST_ALPHA_SHIFT 19u
#define LUMA_BLEND_DEST_ALPHA_MASK  0x1Fu
#define LUMA_BLEND_OP_ALPHA_SHIFT   24u
#define LUMA_BLEND_OP_ALPHA_MASK    0x7u
#define LUMA_BLEND_WRITE_MASK_SHIFT 27u
#define LUMA_BLEND_WRITE_MASK_MASK  0xFu
#define LUMA_BLEND_EMULATION_SHIFT  31u
#define LUMA_BLEND_EMULATION_MASK   0x1u

// Set on the packed value whenever the shader is expected to blend by itself (a packed blend state is never 0 otherwise, but relying on that would be fragile)
#define LUMA_BLEND_EMULATION_FLAG   (LUMA_BLEND_EMULATION_MASK << LUMA_BLEND_EMULATION_SHIFT)

#ifdef __cplusplus

// Packs the blend state of render target 0 for "LumaData.CustomData1" (this requires "d3d11.h", so include this after the Luma core).
// The returned value always has "LUMA_BLEND_EMULATION_FLAG" set, so shaders can simply test "LumaData.CustomData1" against it.
inline uint32_t PackRenderTargetBlendState(const D3D11_RENDER_TARGET_BLEND_DESC& blend_desc)
{
   uint32_t packed_blend_state = LUMA_BLEND_EMULATION_FLAG;
   packed_blend_state |= ((blend_desc.BlendEnable ? 1u : 0u)                    & LUMA_BLEND_ENABLE_MASK)     << LUMA_BLEND_ENABLE_SHIFT;
   packed_blend_state |= ((uint32_t)blend_desc.SrcBlend                         & LUMA_BLEND_SRC_MASK)        << LUMA_BLEND_SRC_SHIFT;
   packed_blend_state |= ((uint32_t)blend_desc.DestBlend                        & LUMA_BLEND_DEST_MASK)       << LUMA_BLEND_DEST_SHIFT;
   packed_blend_state |= ((uint32_t)blend_desc.BlendOp                          & LUMA_BLEND_OP_MASK)         << LUMA_BLEND_OP_SHIFT;
   packed_blend_state |= ((uint32_t)blend_desc.SrcBlendAlpha                    & LUMA_BLEND_SRC_ALPHA_MASK)  << LUMA_BLEND_SRC_ALPHA_SHIFT;
   packed_blend_state |= ((uint32_t)blend_desc.DestBlendAlpha                   & LUMA_BLEND_DEST_ALPHA_MASK) << LUMA_BLEND_DEST_ALPHA_SHIFT;
   packed_blend_state |= ((uint32_t)blend_desc.BlendOpAlpha                     & LUMA_BLEND_OP_ALPHA_MASK)   << LUMA_BLEND_OP_ALPHA_SHIFT;
   packed_blend_state |= ((uint32_t)blend_desc.RenderTargetWriteMask            & LUMA_BLEND_WRITE_MASK_MASK) << LUMA_BLEND_WRITE_MASK_SHIFT;
   // Every field needs to survive the packing, otherwise the emulated blend wouldn't match the hardware one
   ASSERT_ONCE((uint32_t)blend_desc.SrcBlend <= LUMA_BLEND_SRC_MASK);
   ASSERT_ONCE((uint32_t)blend_desc.DestBlend <= LUMA_BLEND_DEST_MASK);
   ASSERT_ONCE((uint32_t)blend_desc.BlendOp <= LUMA_BLEND_OP_MASK);
   ASSERT_ONCE((uint32_t)blend_desc.SrcBlendAlpha <= LUMA_BLEND_SRC_ALPHA_MASK);
   ASSERT_ONCE((uint32_t)blend_desc.DestBlendAlpha <= LUMA_BLEND_DEST_ALPHA_MASK);
   ASSERT_ONCE((uint32_t)blend_desc.BlendOpAlpha <= LUMA_BLEND_OP_ALPHA_MASK);
   ASSERT_ONCE((uint32_t)blend_desc.RenderTargetWriteMask <= LUMA_BLEND_WRITE_MASK_MASK);
   return packed_blend_state;
}

// The red and green channels of the constant blend factor, for "LumaData.CustomData2"
inline uint32_t PackBlendFactorRG(const FLOAT blend_factor[4])
{
   return (uint32_t)ConvertFloatToHalf(blend_factor[0]) | ((uint32_t)ConvertFloatToHalf(blend_factor[1]) << 16u);
}

// The blue and alpha channels of the constant blend factor, for "LumaData.CustomData3" (which is a float, so the bits are simply re-interpreted, "asuint()" reads them back)
inline float PackBlendFactorBA(const FLOAT blend_factor[4])
{
   const uint32_t packed_blend_factor = (uint32_t)ConvertFloatToHalf(blend_factor[2]) | ((uint32_t)ConvertFloatToHalf(blend_factor[3]) << 16u);
   return std::bit_cast<float>(packed_blend_factor);
}

#else // hlsl

// Mirrored from "d3d11.h" (hlsl has no access to it)
#define D3D11_BLEND_ZERO             1u
#define D3D11_BLEND_ONE              2u
#define D3D11_BLEND_SRC_COLOR        3u
#define D3D11_BLEND_INV_SRC_COLOR    4u
#define D3D11_BLEND_SRC_ALPHA        5u
#define D3D11_BLEND_INV_SRC_ALPHA    6u
#define D3D11_BLEND_DEST_ALPHA       7u
#define D3D11_BLEND_INV_DEST_ALPHA   8u
#define D3D11_BLEND_DEST_COLOR       9u
#define D3D11_BLEND_INV_DEST_COLOR   10u
#define D3D11_BLEND_SRC_ALPHA_SAT    11u
#define D3D11_BLEND_BLEND_FACTOR     14u
#define D3D11_BLEND_INV_BLEND_FACTOR 15u
#define D3D11_BLEND_SRC1_COLOR       16u
#define D3D11_BLEND_INV_SRC1_COLOR   17u
#define D3D11_BLEND_SRC1_ALPHA       18u
#define D3D11_BLEND_INV_SRC1_ALPHA   19u

#define D3D11_BLEND_OP_ADD           1u
#define D3D11_BLEND_OP_SUBTRACT      2u
#define D3D11_BLEND_OP_REV_SUBTRACT  3u
#define D3D11_BLEND_OP_MIN           4u
#define D3D11_BLEND_OP_MAX           5u

// Decoded fixed function blend state, independent of how the caller stores or supplies it.
// The Luma emulation flag controls pass routing and is not part of the blend equation.
struct HardwareBlendState
{
  bool blendEnable;
  uint srcBlend;
  uint dstBlend;
  uint blendOp;
  uint srcBlendAlpha;
  uint dstBlendAlpha;
  uint blendOpAlpha;
  uint writeMask;
  float4 constantBlendFactor;
};

// Unpacks the constant blend factor from "LumaData.CustomData2" and "LumaData.CustomData3"
float4 UnpackBlendFactor(uint packedRG, float packedBA)
{
  const uint rawBA = asuint(packedBA);
  return float4(f16tof32(packedRG & 0xFFFFu), f16tof32(packedRG >> 16u), f16tof32(rawBA & 0xFFFFu), f16tof32(rawBA >> 16u));
}

bool IsRenderTargetBlendEnabled(uint packedBlendState)
{
  return (packedBlendState & LUMA_BLEND_EMULATION_FLAG) != 0;
}

// Decodes "LumaData.CustomData1", "LumaData.CustomData2" and "LumaData.CustomData3" into the state used by the blend equation.
HardwareBlendState UnpackRenderTargetBlendState(uint packedBlendState, uint packedBlendFactorRG, float packedBlendFactorBA)
{
  HardwareBlendState state;
  state.blendEnable = ((packedBlendState >> LUMA_BLEND_ENABLE_SHIFT) & LUMA_BLEND_ENABLE_MASK) != 0;
  state.srcBlend = (packedBlendState >> LUMA_BLEND_SRC_SHIFT) & LUMA_BLEND_SRC_MASK;
  state.dstBlend = (packedBlendState >> LUMA_BLEND_DEST_SHIFT) & LUMA_BLEND_DEST_MASK;
  state.blendOp = (packedBlendState >> LUMA_BLEND_OP_SHIFT) & LUMA_BLEND_OP_MASK;
  state.srcBlendAlpha = (packedBlendState >> LUMA_BLEND_SRC_ALPHA_SHIFT) & LUMA_BLEND_SRC_ALPHA_MASK;
  state.dstBlendAlpha = (packedBlendState >> LUMA_BLEND_DEST_ALPHA_SHIFT) & LUMA_BLEND_DEST_ALPHA_MASK;
  state.blendOpAlpha = (packedBlendState >> LUMA_BLEND_OP_ALPHA_SHIFT) & LUMA_BLEND_OP_ALPHA_MASK;
  state.writeMask = (packedBlendState >> LUMA_BLEND_WRITE_MASK_SHIFT) & LUMA_BLEND_WRITE_MASK_MASK;
  state.constantBlendFactor = UnpackBlendFactor(packedBlendFactorRG, packedBlendFactorBA);
  return state;
}

// Evaluates a "D3D11_BLEND" into the factor the hardware would have multiplied by.
// The color equation uses ".rgb" and the alpha equation uses ".a" (DX doesn't allow color based blends on the alpha equation, so their alpha is left as a best guess).
float4 GetD3DBlendFactor(uint blend, float4 srcColor, float4 dstColor, float4 constantBlendFactor)
{
  float4 factor = 1.0; // Invalid states fall back on a pass through (which is what an unset "D3D11_BLEND" would do too)
  switch (blend)
  {
    case D3D11_BLEND_ZERO:             factor = 0.0; break;
    case D3D11_BLEND_ONE:              factor = 1.0; break;
    case D3D11_BLEND_SRC_COLOR:        factor = srcColor; break;
    case D3D11_BLEND_INV_SRC_COLOR:    factor = 1.0 - srcColor; break; // TODO: add a few modes to clamp values < 0
    case D3D11_BLEND_SRC_ALPHA:        factor = srcColor.a; break;
    case D3D11_BLEND_INV_SRC_ALPHA:    factor = (1.0 - srcColor.a); break;
    case D3D11_BLEND_DEST_ALPHA:       factor = dstColor.a; break;
    case D3D11_BLEND_INV_DEST_ALPHA:   factor = (1.0 - dstColor.a); break;
    case D3D11_BLEND_DEST_COLOR:       factor = dstColor; break;
    case D3D11_BLEND_INV_DEST_COLOR:   factor = 1.0 - dstColor; break;
    // "min(src alpha, 1 - dest alpha)" on the color channels, and 1 on alpha
    case D3D11_BLEND_SRC_ALPHA_SAT:    factor = float4((float3)min(srcColor.a, 1.0 - dstColor.a), 1.0); break;
    case D3D11_BLEND_BLEND_FACTOR:     factor = constantBlendFactor; break;
    case D3D11_BLEND_INV_BLEND_FACTOR: factor = 1.0 - constantBlendFactor; break;
    // Dual source blending, it can't be used by a shader that only writes one output (it'd be invalid state), so these simply fall back on the single source // TODO: implement?
    case D3D11_BLEND_SRC1_COLOR:       factor = srcColor; break;
    case D3D11_BLEND_INV_SRC1_COLOR:   factor = 1.0 - srcColor; break;
    case D3D11_BLEND_SRC1_ALPHA:       factor = srcColor.a; break;
    case D3D11_BLEND_INV_SRC1_ALPHA:   factor = (1.0 - srcColor.a); break;
  }
  return factor;
}

// Combines the two (already multiplied by their factors) terms of a blend equation.
// The alpha equation runs through here as well, by splatting its two scalars (the compiler scalarizes it back).
float3 ApplyD3DBlendOp(uint blendOp, float3 srcTerm, float3 dstTerm)
{
  float3 blendedTerm = srcTerm + dstTerm; // "D3D11_BLEND_OP_ADD" (and invalid states, which the hardware would treat as the default)
  switch (blendOp)
  {
    case D3D11_BLEND_OP_SUBTRACT:      blendedTerm = srcTerm - dstTerm; break;
    case D3D11_BLEND_OP_REV_SUBTRACT:  blendedTerm = dstTerm - srcTerm; break;
    case D3D11_BLEND_OP_MIN:           blendedTerm = min(srcTerm, dstTerm); break;
    case D3D11_BLEND_OP_MAX:           blendedTerm = max(srcTerm, dstTerm); break;
  }
  return blendedTerm;
}

// "D3D11_BLEND_OP_MIN" and "D3D11_BLEND_OP_MAX" ignore the blend factors (the hardware compares the raw source and destination)
bool DoesD3DBlendOpUseFactors(uint blendOp)
{
  return blendOp != D3D11_BLEND_OP_MIN && blendOp != D3D11_BLEND_OP_MAX;
}

float3 GetPositiveSourceRGBInfluence(HardwareBlendState state, float4 srcColor, float4 dstColor)
{
  float3 influence = 0.0;

  if (!state.blendEnable)
  {
    influence = 1.0;
  }
  // We ignore other blend ops
  else if (state.blendOp <= D3D11_BLEND_OP_ADD)
  {
    influence = saturate(GetD3DBlendFactor(state.srcBlend, srcColor, dstColor, state.constantBlendFactor).rgb);
  }

  return influence;
}

// Runs the whole fixed function output merger blend (of render target 0) in the shader, for passes that write through a UAV/ROV instead of a render target.
// "state" contains the decoded blend settings, "srcColor" is the color the pixel shader would have output,
// and "dstColor" is the current value of the render target. Note that the result is not clamped to the render target format range, callers can do that if they need to.
float4 EmulateHardwareBlend(HardwareBlendState state, float4 srcColor, float4 dstColor, bool allowWriteMask = true)
{
  float4 blendedColor = srcColor;

  if (state.blendEnable)
  {
    float3 srcTerm = srcColor.rgb;
    float3 dstTerm = dstColor.rgb;
    if (DoesD3DBlendOpUseFactors(state.blendOp))
    {
      srcTerm *= GetD3DBlendFactor(state.srcBlend, srcColor, dstColor, state.constantBlendFactor).rgb;
      dstTerm *= GetD3DBlendFactor(state.dstBlend, srcColor, dstColor, state.constantBlendFactor).rgb;
    }
    blendedColor.rgb = ApplyD3DBlendOp(state.blendOp, srcTerm, dstTerm);

    float srcTermAlpha = srcColor.a;
    float dstTermAlpha = dstColor.a;
    if (DoesD3DBlendOpUseFactors(state.blendOpAlpha))
    {
      srcTermAlpha *= GetD3DBlendFactor(state.srcBlendAlpha, srcColor, dstColor, state.constantBlendFactor).a;
      dstTermAlpha *= GetD3DBlendFactor(state.dstBlendAlpha, srcColor, dstColor, state.constantBlendFactor).a;
    }
    blendedColor.a = ApplyD3DBlendOp(state.blendOpAlpha, srcTermAlpha, dstTermAlpha).x;
  }

  if (allowWriteMask)
  {
    // Channels the render target write mask disables keep the destination value
    blendedColor = float4((state.writeMask & 1u) ? blendedColor.r : dstColor.r,
                          (state.writeMask & 2u) ? blendedColor.g : dstColor.g,
                          (state.writeMask & 4u) ? blendedColor.b : dstColor.b,
                          (state.writeMask & 8u) ? blendedColor.a : dstColor.a);
  }

  return blendedColor;
}

#endif // __cplusplus

#endif // SRC_HARDWARE_BLENDS_EMULATION_HLSL
