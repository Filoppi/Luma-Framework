#pragma once

// HMA lens flare + glare passes, ported from Pretty Post Process (https://github.com/EscapeEntertainment/PrettyPostProcess @ 8bc14ff9).
// Copyright (c) 2021-2022 Escape Entertainment & Froyok. BSD-3-Clause-Clear, full text in Shaders/Hitman Absolution/LICENSE_PrettyPostProcess.txt
// Header-only. Included by main.cpp after core.hpp. Shaders: Shaders/Hitman Absolution/Luma_HMA_Flare_*.hlsl (shared cbuffer in Includes/HMAFlare.hlsl).

struct HMAFlareSettings
{
   float intensity = 1.f;       // FlareIntensity multiplier
   float threshold = 1.f;       // exposed scene units
   float knee = 0.5f;           // 0..1
   bool glare = false;          // separate toggle, default off until ghost/halo reviewed
   float glare_intensity = 1.f; // multiplier on upstream GlareIntensity (0.02)
};

struct HMAFlareTarget
{
   ComPtr<ID3D11Texture2D> tex;
   ComPtr<ID3D11ShaderResourceView> srv;
   ComPtr<ID3D11RenderTargetView> rtv;
   UINT w = 0, h = 0;
};

static void HMAFlare_ResetTarget(HMAFlareTarget& t)
{
   t.tex.reset();
   t.srv.reset();
   t.rtv.reset();
   t.w = 0;
   t.h = 0;
}

struct HMAFlareResources
{
   // mip0/feature: W/2 x H/2. glare: W/2 x H/2. mip1/halo/blur_q/blur_q2: W/4 x H/4. blur_e: W/8 x H/8. final_flare: W x H (full scene res).
   // blur_q2 is the last Kawase Up output; mip0 is reused as the blurred feature (mip0 is dead after Ghosts).
   HMAFlareTarget mip0, mip1, halo, feature, blur_q, blur_e, blur_q2, glare, final_flare;
   ComPtr<ID3D11Buffer> cb;
   ComPtr<ID3D11SamplerState> smp_clamp, smp_border;
   ComPtr<ID3D11BlendState> blend_add;
   ComPtr<ID3D11RasterizerState> raster_nocull;

   void Release()
   {
      HMAFlare_ResetTarget(mip0);
      HMAFlare_ResetTarget(mip1);
      HMAFlare_ResetTarget(halo);
      HMAFlare_ResetTarget(feature);
      HMAFlare_ResetTarget(blur_q);
      HMAFlare_ResetTarget(blur_e);
      HMAFlare_ResetTarget(blur_q2);
      HMAFlare_ResetTarget(glare);
      HMAFlare_ResetTarget(final_flare);
      cb.reset();
      smp_clamp.reset();
      smp_border.reset();
      blend_add.reset();
      raster_nocull.reset();
   }
};

// Mirrors the "HMAFlareCB" cbuffer in Shaders/Hitman Absolution/Includes/HMAFlare.hlsl (14 members, all float4, 28 float4 = 448 bytes).
struct HMAFlareCBData
{
   float OutSize[4];        // xy = output RT size (px), zw = 1/xy
   float InSize[4];         // xy = t0 size (px), zw = 1/xy
   float GhostColors[8][4];
   float GhostScales[8][4]; // x = scale
   float GhostParams[4];    // x Intensity, y ChromaShift, z Compression, w unused
   float HaloParams[4];     // x Width, y Mask, z Compression, w Intensity (already * HaloChainWeight)
   float HaloParams2[4];    // x ChromaShift
   float ThresholdParams[4];// x Threshold, y Knee
   float GlareParams[4];    // x GlareIntensity (0.02 * user mult), y GlareDivider (60)
   float GlareTint[4];
   float GlareScales[4];
   float GlareTile[4];      // xy TileCount (mip1 size / 2), zw BufferSize (mip1 size)
   float MixParams[4];      // x FlareIntensity, y GlareValid (0/1), z FadeValid (0/1)
   float FlareTint[4];
};
static_assert(sizeof(HMAFlareCBData) == 448, "HMAFlareCBData must match HMAFlareCB in HMAFlare.hlsl");

// Upstream bloom chain weight the halo would have received: (1-0.85)*0.85/7 (Up[1] lerp, Up[0] lerp, BloomIntensity 1/PassAmount).
static constexpr float HMA_FLARE_HALO_CHAIN_WEIGHT = (1.f - 0.85f) * 0.85f / 7.f;

// Registers the flare shader permutations (called from OnInit).
static void HMA_RegisterFlareShaders()
{
   native_shaders_definitions.emplace(CompileTimeStringHash("HMA Flare Prepare"), ShaderDefinition{ "Luma_HMA_Flare_Prepare", reshade::api::pipeline_subobject_type::pixel_shader });
   native_shaders_definitions.emplace(CompileTimeStringHash("HMA Flare Downsample"), ShaderDefinition{ "Luma_HMA_Flare_Downsample", reshade::api::pipeline_subobject_type::pixel_shader });
   native_shaders_definitions.emplace(CompileTimeStringHash("HMA Flare Halo"), ShaderDefinition{ "Luma_HMA_Flare_Halo", reshade::api::pipeline_subobject_type::pixel_shader });
   native_shaders_definitions.emplace(CompileTimeStringHash("HMA Flare Ghosts"), ShaderDefinition{ "Luma_HMA_Flare_Ghosts", reshade::api::pipeline_subobject_type::pixel_shader });
   native_shaders_definitions.emplace(CompileTimeStringHash("HMA Flare Kawase Down"), ShaderDefinition{ "Luma_HMA_Flare_KawaseDown", reshade::api::pipeline_subobject_type::pixel_shader });
   native_shaders_definitions.emplace(CompileTimeStringHash("HMA Flare Kawase Up"), ShaderDefinition{ "Luma_HMA_Flare_KawaseUp", reshade::api::pipeline_subobject_type::pixel_shader });
   native_shaders_definitions.emplace(CompileTimeStringHash("HMA Flare Glare VS"), ShaderDefinition{ "Luma_HMA_Flare_Glare_VS", reshade::api::pipeline_subobject_type::vertex_shader });
   native_shaders_definitions.emplace(CompileTimeStringHash("HMA Flare Glare PS"), ShaderDefinition{ "Luma_HMA_Flare_Glare_PS", reshade::api::pipeline_subobject_type::pixel_shader });
   native_shaders_definitions.emplace(CompileTimeStringHash("HMA Flare Mix"), ShaderDefinition{ "Luma_HMA_Flare_Mix", reshade::api::pipeline_subobject_type::pixel_shader });
}

static ID3D11PixelShader* HMAFlare_FindPS(DeviceData& device_data, uint32_t hash)
{
   auto it = device_data.native_pixel_shaders.find(hash);
   return (it != device_data.native_pixel_shaders.end()) ? it->second.get() : nullptr;
}

static ID3D11VertexShader* HMAFlare_FindVS(DeviceData& device_data, uint32_t hash)
{
   auto it = device_data.native_vertex_shaders.find(hash);
   return (it != device_data.native_vertex_shaders.end()) ? it->second.get() : nullptr;
}

// Reads the size of a non-MSAA 2D texture behind an SRV.
static bool HMAFlare_GetSRVSize(ID3D11ShaderResourceView* srv, UINT& w, UINT& h)
{
   ComPtr<ID3D11Resource> resource;
   srv->GetResource(resource.put());
   if (!resource)
      return false;
   ComPtr<ID3D11Texture2D> tex;
   if (FAILED(resource->QueryInterface(tex.put())))
      return false;
   D3D11_TEXTURE2D_DESC desc = {};
   tex->GetDesc(&desc);
   if (desc.Width == 0 || desc.Height == 0 || desc.SampleDesc.Count != 1)
      return false;
   w = desc.Width;
   h = desc.Height;
   return true;
}

static UINT HMAFlare_Div(UINT v, UINT d)
{
   UINT r = v / d;
   return r > 0 ? r : 1;
}

// (Re)creates an R16G16B16A16_FLOAT RT+SRV of the given size. Keeps the old one if it already matches.
static bool HMAFlare_EnsureTarget(ID3D11Device* device, HMAFlareTarget& t, UINT w, UINT h)
{
   if (t.tex && t.srv && t.rtv && t.w == w && t.h == h)
      return true;
   HMAFlare_ResetTarget(t);

   D3D11_TEXTURE2D_DESC desc = {};
   desc.Width = w;
   desc.Height = h;
   desc.MipLevels = 1;
   desc.ArraySize = 1;
   desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
   desc.SampleDesc.Count = 1;
   desc.Usage = D3D11_USAGE_DEFAULT;
   desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

   ComPtr<ID3D11Texture2D> tex;
   ComPtr<ID3D11ShaderResourceView> srv;
   ComPtr<ID3D11RenderTargetView> rtv;
   if (FAILED(device->CreateTexture2D(&desc, nullptr, tex.put()))
      || FAILED(device->CreateShaderResourceView(tex.get(), nullptr, srv.put()))
      || FAILED(device->CreateRenderTargetView(tex.get(), nullptr, rtv.put())))
      return false;

   t.tex = tex;
   t.srv = srv;
   t.rtv = rtv;
   t.w = w;
   t.h = h;
   return true;
}

// Creates the scene-size independent objects once: constant buffer, samplers (s0 clamp, s1 border with black border), additive blend, no-cull rasterizer.
static bool HMAFlare_EnsureStatics(ID3D11Device* device, HMAFlareResources& res)
{
   if (!res.cb)
   {
      D3D11_BUFFER_DESC desc = {};
      desc.ByteWidth = sizeof(HMAFlareCBData); // 448, multiple of 16
      desc.Usage = D3D11_USAGE_DYNAMIC;
      desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
      desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
      ComPtr<ID3D11Buffer> cb;
      if (FAILED(device->CreateBuffer(&desc, nullptr, cb.put())))
         return false;
      res.cb = cb;
   }

   if (!res.smp_clamp)
   {
      D3D11_SAMPLER_DESC desc = {};
      desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
      desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
      desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
      desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
      desc.MaxAnisotropy = 1;
      desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
      desc.MaxLOD = D3D11_FLOAT32_MAX;
      ComPtr<ID3D11SamplerState> smp;
      if (FAILED(device->CreateSamplerState(&desc, smp.put())))
         return false;
      res.smp_clamp = smp;
   }

   if (!res.smp_border)
   {
      D3D11_SAMPLER_DESC desc = {};
      desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
      desc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
      desc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
      desc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
      desc.BorderColor[0] = 0.f;
      desc.BorderColor[1] = 0.f;
      desc.BorderColor[2] = 0.f;
      desc.BorderColor[3] = 0.f;
      desc.MaxAnisotropy = 1;
      desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
      desc.MaxLOD = D3D11_FLOAT32_MAX;
      ComPtr<ID3D11SamplerState> smp;
      if (FAILED(device->CreateSamplerState(&desc, smp.put())))
         return false;
      res.smp_border = smp;
   }

   if (!res.blend_add)
   {
      D3D11_BLEND_DESC desc = {};
      D3D11_RENDER_TARGET_BLEND_DESC& rt = desc.RenderTarget[0];
      rt.BlendEnable = TRUE;
      rt.SrcBlend = D3D11_BLEND_ONE;
      rt.DestBlend = D3D11_BLEND_ONE;
      rt.BlendOp = D3D11_BLEND_OP_ADD;
      rt.SrcBlendAlpha = D3D11_BLEND_ONE;
      rt.DestBlendAlpha = D3D11_BLEND_ONE;
      rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
      rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
      ComPtr<ID3D11BlendState> blend;
      if (FAILED(device->CreateBlendState(&desc, blend.put())))
         return false;
      res.blend_add = blend;
   }

   if (!res.raster_nocull)
   {
      D3D11_RASTERIZER_DESC desc = {};
      desc.FillMode = D3D11_FILL_SOLID;
      desc.CullMode = D3D11_CULL_NONE;
      desc.DepthClipEnable = TRUE;
      ComPtr<ID3D11RasterizerState> raster;
      if (FAILED(device->CreateRasterizerState(&desc, raster.put())))
         return false;
      res.raster_nocull = raster;
   }

   return true;
}

// Nulls PS t0..t3 and VS t0. Called before every pass so no SRV is bound while its texture becomes the RT.
// The VS t0 unbind is not covered by the FullGraphics state stack (it only tracks PS SRVs), so HMA_DrawFlare
// saves and restores the game's VS t0 around the whole chain.
static void HMAFlare_UnbindInputs(ID3D11DeviceContext* ctx)
{
   ID3D11ShaderResourceView* ps_nulls[4] = {};
   ctx->PSSetShaderResources(0, 4, ps_nulls);
   ID3D11ShaderResourceView* vs_null = nullptr;
   ctx->VSSetShaderResources(0, 1, &vs_null);
}

static void HMAFlare_SetSize(float (&dst)[4], UINT w, UINT h)
{
   dst[0] = (float)w;
   dst[1] = (float)h;
   dst[2] = 1.f / (float)w;
   dst[3] = 1.f / (float)h;
}

static void HMAFlare_Upload(ID3D11DeviceContext* ctx, ID3D11Buffer* cb, const HMAFlareCBData& data)
{
   D3D11_MAPPED_SUBRESOURCE mapped = {};
   if (SUCCEEDED(ctx->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
   {
      std::memcpy(mapped.pData, &data, sizeof(data));
      ctx->Unmap(cb, 0);
   }
}

// Runs the 9 flare passes (Prepare -> Downsample -> Halo -> Ghosts -> Kawase x4 -> Glare -> Mix).
// Returns true and sets out_srv (non-owning, = res.final_flare.srv) on success. Returns false, with no GPU state changed, if any shader or input is missing.
static bool HMA_DrawFlare(ID3D11Device* device, ID3D11DeviceContext* ctx, DeviceData& device_data, HMAFlareResources& res, const HMAFlareSettings& settings,
   ID3D11ShaderResourceView* scene_srv, ID3D11ShaderResourceView* fade_srv, ID3D11ShaderResourceView* exposure_srv, ID3D11Buffer* game_cb4,
   ID3D11ShaderResourceView** out_srv)
{
   if (out_srv == nullptr)
      return false;
   *out_srv = nullptr;
   if (device == nullptr || ctx == nullptr || scene_srv == nullptr || game_cb4 == nullptr)
      return false;

   // Shaders: all looked up before anything is bound.
   ID3D11PixelShader* ps_prepare = HMAFlare_FindPS(device_data, CompileTimeStringHash("HMA Flare Prepare"));
   ID3D11PixelShader* ps_downsample = HMAFlare_FindPS(device_data, CompileTimeStringHash("HMA Flare Downsample"));
   ID3D11PixelShader* ps_halo = HMAFlare_FindPS(device_data, CompileTimeStringHash("HMA Flare Halo"));
   ID3D11PixelShader* ps_ghosts = HMAFlare_FindPS(device_data, CompileTimeStringHash("HMA Flare Ghosts"));
   ID3D11PixelShader* ps_kawase_down = HMAFlare_FindPS(device_data, CompileTimeStringHash("HMA Flare Kawase Down"));
   ID3D11PixelShader* ps_kawase_up = HMAFlare_FindPS(device_data, CompileTimeStringHash("HMA Flare Kawase Up"));
   ID3D11VertexShader* vs_glare = HMAFlare_FindVS(device_data, CompileTimeStringHash("HMA Flare Glare VS"));
   ID3D11PixelShader* ps_glare = HMAFlare_FindPS(device_data, CompileTimeStringHash("HMA Flare Glare PS"));
   ID3D11PixelShader* ps_mix = HMAFlare_FindPS(device_data, CompileTimeStringHash("HMA Flare Mix"));
   ID3D11VertexShader* vs_copy = HMAFlare_FindVS(device_data, CompileTimeStringHash("Copy VS"));
   if (!ps_prepare || !ps_downsample || !ps_halo || !ps_ghosts || !ps_kawase_down || !ps_kawase_up
      || !vs_glare || !ps_glare || !ps_mix || !vs_copy)
      return false;

   UINT scene_w = 0, scene_h = 0;
   if (!HMAFlare_GetSRVSize(scene_srv, scene_w, scene_h))
      return false;

   // Targets (recreated on scene size change) and scene-independent objects.
   const UINT half_w = HMAFlare_Div(scene_w, 2), half_h = HMAFlare_Div(scene_h, 2);
   const UINT quarter_w = HMAFlare_Div(scene_w, 4), quarter_h = HMAFlare_Div(scene_h, 4);
   const UINT eighth_w = HMAFlare_Div(scene_w, 8), eighth_h = HMAFlare_Div(scene_h, 8);
   if (!HMAFlare_EnsureStatics(device, res))
      return false;
   if (!HMAFlare_EnsureTarget(device, res.mip0, half_w, half_h)
      || !HMAFlare_EnsureTarget(device, res.mip1, quarter_w, quarter_h)
      || !HMAFlare_EnsureTarget(device, res.halo, quarter_w, quarter_h)
      || !HMAFlare_EnsureTarget(device, res.feature, half_w, half_h)
      || !HMAFlare_EnsureTarget(device, res.blur_q, quarter_w, quarter_h)
      || !HMAFlare_EnsureTarget(device, res.blur_e, eighth_w, eighth_h)
      || !HMAFlare_EnsureTarget(device, res.blur_q2, quarter_w, quarter_h)
      || !HMAFlare_EnsureTarget(device, res.final_flare, scene_w, scene_h))
      return false;
   if (settings.glare && !HMAFlare_EnsureTarget(device, res.glare, half_w, half_h))
      return false;

   // Constant values per frame (shipped asset defaults, see SPEC_upstream section 4).
   HMAFlareCBData cbd = {};
   static constexpr float ghost_colors[8][4] = {
      { 1.f, 0.8f, 0.4f, 1.f }, { 1.f, 1.f, 0.6f, 1.f }, { 0.8f, 0.8f, 1.f, 1.f }, { 0.5f, 1.f, 0.4f, 1.f },
      { 0.5f, 0.8f, 1.f, 1.f }, { 0.9f, 1.f, 0.8f, 1.f }, { 1.f, 0.8f, 0.4f, 1.f }, { 0.9f, 0.7f, 0.7f, 1.f } };
   static constexpr float ghost_scales[8] = { -0.5923960f, 2.5f, -1.f, 0.25f, 0.4f, -0.4f, -0.2f, -0.1f };
   for (uint32_t i = 0; i < 8; i++)
   {
      for (uint32_t c = 0; c < 4; c++)
         cbd.GhostColors[i][c] = ghost_colors[i][c];
      cbd.GhostScales[i][0] = ghost_scales[i];
   }
   cbd.GhostParams[0] = 0.2f;   // Intensity
   cbd.GhostParams[1] = 0.096f; // ChromaShift
   cbd.GhostParams[2] = 1.0f;   // Compression
   cbd.HaloParams[0] = 0.45f;   // Width
   cbd.HaloParams[1] = 0.2f;    // Mask
   cbd.HaloParams[2] = 5.0f;    // Compression
   cbd.HaloParams[3] = 0.6f * HMA_FLARE_HALO_CHAIN_WEIGHT; // Intensity
   cbd.HaloParams2[0] = 0.01f;  // ChromaShift
   cbd.ThresholdParams[0] = settings.threshold;
   cbd.ThresholdParams[1] = settings.knee;
   cbd.GlareParams[0] = 0.02f * settings.glare_intensity;
   cbd.GlareParams[1] = 60.f;   // GlareDivider
   for (uint32_t c = 0; c < 4; c++)
   {
      cbd.GlareTint[c] = 1.f;
      cbd.GlareScales[c] = 1.f;
   }
   cbd.MixParams[0] = settings.intensity;
   cbd.MixParams[1] = settings.glare ? 1.f : 0.f;
   cbd.MixParams[2] = fade_srv ? 1.f : 0.f;
   cbd.FlareTint[0] = 1.f;
   cbd.FlareTint[1] = 0.85f;
   cbd.FlareTint[2] = 0.7f;
   // Glare tiles from mip1 (W/4 x H/4): TileCount = size / 2, BufferSize = size.
   cbd.GlareTile[0] = (float)(quarter_w / 2);
   cbd.GlareTile[1] = (float)(quarter_h / 2);
   cbd.GlareTile[2] = (float)quarter_w;
   cbd.GlareTile[3] = (float)quarter_h;

   DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack;
   draw_state_stack.Cache(ctx, device_data.uav_max_count);

   // The FullGraphics stack does not track VS SRVs, and every pass nulls VS t0 ("HMAFlare_UnbindInputs"),
   // so keep the game's VS t0 ourselves for the restore at the end of the chain.
   ComPtr<ID3D11ShaderResourceView> prev_game_vs_srv;
   ctx->VSGetShaderResources(0, 1, prev_game_vs_srv.put());

   // Samplers for every pass: s0 = clamp, s1 = border (passes that don't use s0/s1 ignore them).
   ID3D11SamplerState* samplers[2] = { res.smp_clamp.get(), res.smp_border.get() };
   ctx->PSSetSamplers(0, 2, samplers);

   ID3D11Buffer* cb = res.cb.get();
   // Fullscreen pass helper. Optional t1/t3 are bound after the inputs are cleared. DrawCustomPixelShader binds src at t0.
   auto draw_fullscreen = [&](ID3D11PixelShader* ps, ID3D11ShaderResourceView* src, UINT src_w, UINT src_h, HMAFlareTarget& dst, const char* name,
      ID3D11ShaderResourceView* t1, ID3D11ShaderResourceView* t3)
   {
      ctx->OMSetRenderTargets(0, nullptr, nullptr); // The previous pass's output is still bound as RTV: D3D11 would null it when bound as an SRV
      HMAFlare_UnbindInputs(ctx);
      HMAFlare_SetSize(cbd.OutSize, dst.w, dst.h);
      HMAFlare_SetSize(cbd.InSize, src_w, src_h);
      HMAFlare_Upload(ctx, cb, cbd);
      ctx->PSSetConstantBuffers(0, 1, &cb);
      ctx->VSSetConstantBuffers(0, 1, &cb);
      if (t1)
         ctx->PSSetShaderResources(1, 1, &t1);
      if (t3)
         ctx->PSSetShaderResources(3, 1, &t3);
      DrawCustomPixelShader(ctx, device_data.default_depth_stencil_state.get(), device_data.default_blend_state.get(), nullptr,
         vs_copy, ps, src, dst.rtv.get(), dst.w, dst.h, true, name);
   };

   // 1. Prepare: scene -> mip0 (W/2). Needs the exposure at t7, game cb4 at b4 and the Luma settings at b13.
   {
      // t7 is only read when AutoExposureMode != 0 (GetHMAExposure), so it is bound only when available.
      if (exposure_srv)
      {
         ID3D11ShaderResourceView* exposure = exposure_srv;
         ctx->PSSetShaderResources(7, 1, &exposure);
      }
      ID3D11Buffer* cb4 = game_cb4;
      ctx->PSSetConstantBuffers(4, 1, &cb4);
      ID3D11Buffer* luma_cb = device_data.luma_global_settings.get();
      if (luma_cb)
         ctx->PSSetConstantBuffers(13, 1, &luma_cb);
      draw_fullscreen(ps_prepare, scene_srv, scene_w, scene_h, res.mip0, "HMA Flare Prepare", nullptr, nullptr);
      ID3D11ShaderResourceView* exposure_null = nullptr;
      ctx->PSSetShaderResources(7, 1, &exposure_null);
   }

   // 2. Downsample: mip0 -> mip1 (W/4).
   draw_fullscreen(ps_downsample, res.mip0.srv.get(), res.mip0.w, res.mip0.h, res.mip1, "HMA Flare Downsample", nullptr, nullptr);

   // 3. Halo: mip1 -> halo (W/4).
   draw_fullscreen(ps_halo, res.mip1.srv.get(), res.mip1.w, res.mip1.h, res.halo, "HMA Flare Halo", nullptr, nullptr);

   // 4. Ghosts: mip0 (t0) + halo (t1) -> feature (W/2).
   draw_fullscreen(ps_ghosts, res.mip0.srv.get(), res.mip0.w, res.mip0.h, res.feature, "HMA Flare Ghosts", res.halo.srv.get(), nullptr);

   // 5-8. Kawase blur (2 steps): feature -> blur_q (W/4) -> blur_e (W/8) -> blur_q2 (W/4) -> mip0 (W/2, reused as the blurred feature).
   draw_fullscreen(ps_kawase_down, res.feature.srv.get(), res.feature.w, res.feature.h, res.blur_q, "HMA Flare Kawase Down", nullptr, nullptr);
   draw_fullscreen(ps_kawase_down, res.blur_q.srv.get(), res.blur_q.w, res.blur_q.h, res.blur_e, "HMA Flare Kawase Down", nullptr, nullptr);
   draw_fullscreen(ps_kawase_up, res.blur_e.srv.get(), res.blur_e.w, res.blur_e.h, res.blur_q2, "HMA Flare Kawase Up", nullptr, nullptr);
   draw_fullscreen(ps_kawase_up, res.blur_q2.srv.get(), res.blur_q2.w, res.blur_q2.h, res.mip0, "HMA Flare Kawase Up", nullptr, nullptr);

   // 9. Glare (optional): instanced quads expanded in the VS from mip1 (VS t0). Additive into the cleared glare RT (W/2).
   if (settings.glare)
   {
      const UINT tiles_x = (UINT)cbd.GlareTile[0];
      const UINT tiles_y = (UINT)cbd.GlareTile[1];
      if (tiles_x > 0 && tiles_y > 0)
      {
         // Cache the state the stack does not cover: VS s1 and the IA vertex buffer (VS t0 is saved chain-wide in HMA_DrawFlare).
         ComPtr<ID3D11SamplerState> prev_vs_smp;
         ctx->VSGetSamplers(1, 1, prev_vs_smp.put());
         ComPtr<ID3D11Buffer> prev_vb;
         UINT prev_vb_stride = 0, prev_vb_offset = 0;
         ctx->IAGetVertexBuffers(0, 1, prev_vb.put(), &prev_vb_stride, &prev_vb_offset);

         HMAFlare_UnbindInputs(ctx);
         const FLOAT zero_color[4] = { 0.f, 0.f, 0.f, 0.f };
         ctx->ClearRenderTargetView(res.glare.rtv.get(), zero_color);

         ID3D11ShaderResourceView* vs_src = res.mip1.srv.get();
         ctx->VSSetShaderResources(0, 1, &vs_src);
         ID3D11SamplerState* vs_smp = res.smp_border.get();
         ctx->VSSetSamplers(1, 1, &vs_smp);

         HMAFlare_SetSize(cbd.OutSize, res.glare.w, res.glare.h);
         HMAFlare_SetSize(cbd.InSize, res.mip1.w, res.mip1.h);
         HMAFlare_Upload(ctx, cb, cbd);
         ctx->VSSetConstantBuffers(0, 1, &cb);
         ctx->PSSetConstantBuffers(0, 1, &cb);

         ID3D11RenderTargetView* glare_rtv = res.glare.rtv.get();
         ctx->OMSetRenderTargets(1, &glare_rtv, nullptr);
         D3D11_VIEWPORT viewport = {};
         viewport.TopLeftX = 0.f;
         viewport.TopLeftY = 0.f;
         viewport.Width = (float)res.glare.w;
         viewport.Height = (float)res.glare.h;
         viewport.MinDepth = 0.f;
         viewport.MaxDepth = 1.f;
         ctx->RSSetViewports(1, &viewport);
         const FLOAT blend_factor[4] = { 1.f, 1.f, 1.f, 1.f };
         ctx->OMSetBlendState(res.blend_add.get(), blend_factor, 0xFFFFFFFF);
         ctx->RSSetState(res.raster_nocull.get());
         ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
         ctx->IASetInputLayout(nullptr);
         ID3D11Buffer* null_vb = nullptr;
         UINT zero = 0;
         ctx->IASetVertexBuffers(0, 1, &null_vb, &zero, &zero);
         ctx->VSSetShader(vs_glare, nullptr, 0);
         ctx->PSSetShader(ps_glare, nullptr, 0);

         ctx->DrawInstanced(6, tiles_x * tiles_y * 4, 0, 0);
         Trace::CustomDraw(ctx, "HMA Flare Glare", 6);

         // Restore the uncovered state.
         ID3D11SamplerState* prev_smp_raw = prev_vs_smp.get();
         ctx->VSSetSamplers(1, 1, &prev_smp_raw);
         ID3D11Buffer* prev_vb_raw = prev_vb.get();
         ctx->IASetVertexBuffers(0, 1, &prev_vb_raw, &prev_vb_stride, &prev_vb_offset);
      }
   }

   // 10. Mix: blurred feature (mip0, t0) + glare (t1, optional) + game fade (t3, optional) -> final_flare (W, full scene res).
   draw_fullscreen(ps_mix, res.mip0.srv.get(), res.mip0.w, res.mip0.h, res.final_flare, "HMA Flare Mix",
      settings.glare ? res.glare.srv.get() : nullptr, fade_srv);

   draw_state_stack.Restore(ctx);

   // Restore the game's VS t0 that the passes nulled (not tracked by the state stack).
   ID3D11ShaderResourceView* game_vs_srv = prev_game_vs_srv.get();
   ctx->VSSetShaderResources(0, 1, &game_vs_srv);

   *out_srv = res.final_flare.srv.get();
   return true;
}
