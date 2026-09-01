// Medal of Honor: Airborne - Luma HDR mod (Unreal Engine 3 2007, 32-bit, DX9 -> D3D11 via dgVoodoo2).
// Hashes are dgVoodoo-TRANSLATED and change per wrapper build: 2.87.3 and 2.81.3 are keyed (2.81.3 emits ps_4_0),
// any other build needs a re-dump.
// The post chain is fp16 with scene DEPTH in the alpha: no depth SRV, no motion vectors, so DLSS/DLAA is impossible.
// One draw ends the frame (DoF composite + bloom + grade + output gamma) into an 8-bit canvas the HUD blends onto;
// its two saturate()s clip 19.4% of a bright frame, which is what the HDR block recovers.

// No DEVELOPMENT auto-debugger MessageBox on DLL attach: invisible under borderless/fullscreen and it blocks the
// loader (ReShade times out the addon load -> error 1114). Same failure as BL2/TW2 under dgVoodoo.
#define DISABLE_AUTO_DEBUGGER 1

#define GAME_MEDAL_OF_HONOR_AIRBORNE 1

#define ENABLE_NGX 0 // NGX is x64-only and the game is 32-bit (and there are no motion vectors anyway)
#define ENABLE_FIDELITY_SK 0
#define GEOMETRY_SHADER_SUPPORT 0
// The game ships no AA at all (no option, no post AA pass in the dump, sampleCount 1 everywhere), so SMAA adds
// rather than replaces. Core auto-registers the 6 "SMAA ..." passes from Luma_SMAA_impl.hlsl.
#define ENABLE_SMAA 1
// Luma's multi-scale HDR bloom pyramid REPLACES the game's own quarter-res bright-pass glow (which the replaced
// gather pass then stops writing). Core auto-registers the 4 "Bloom ..." passes from Luma_Bloom_impl.hlsl.
#define ENABLE_BLOOM 1
// SMAA runs POST-final-grade, before the HUD draws on the canvas, via the post-draw callback. Outside DEVELOPMENT
// this define is what makes "original_draw_dispatch_func" non-null; without it the callback silently never fires.
#define ENABLE_POST_DRAW_DISPATCH_CALLBACK 1

#include "..\..\Core\core.hpp"
#include <shellapi.h> // ShellExecuteA for About links (system() hangs the render thread in exclusive fullscreen)

// The two shaders that can end the frame, both replaced; "bAllowDepthOfField" in MOHASettings.ini picks which one.
// Separate register maps, hence two replacements - missing one puts gamma-encoded SDR into the scRGB swapchain.
static constexpr uint32_t kUberPostHash = 0xB9548800;        // UberPost_0xB9548800.ps_5_0.hlsl
static constexpr uint32_t kGammaCorrectionHash = 0x52B868E0; // GammaCorrection_0x52B868E0.ps_5_0.hlsl

// UE3 DOFAndBloomGather, REPLACED: it writes the game's bloom and its DoF blur SUMMED into one quarter-res target,
// so the vanilla glow can only be switched off inside this shader — anything downstream takes DoF with it.
static constexpr uint32_t kDofBloomGatherHash = 0x33BD72CF;

// The same four passes under dgVoodoo 2.81.3, which emits ps_4_0 and therefore different hashes. Dump-verified as
// signature-identical (same interpolators, t/s registers, cb4 rows), so replacements and slot captures are shared.
static constexpr uint32_t kUberPostHash_v281 = 0x32F77C2B;
static constexpr uint32_t kGammaCorrectionHash_v281 = 0x1B319406;
static constexpr uint32_t kDofBloomGatherHash_v281 = 0x4146E600;

// Luma bloom pyramid mip 0, read by the grade replacements at register(t6): clear of the two slots those
// shaders actually declare (t0 scene, t1 blur).
static constexpr uint32_t kLumaBloomSlot = 6;
// One sigma per mip, count taken FROM the array so the two cannot drift (MELE). BL2's set with the LAST octave
// halved: unhalved it spans ~16.6 px at 4K vs the engine's ~13.5 px widest kernel, and reads as haze.
static float g_bloom_sigmas[] = {1.5f, 2.f, 2.f, 2.f, 1.f, 0.5f};
static constexpr int kBloomNMips = (int)std::size(g_bloom_sigmas);

// User settings, persisted in the [Luma] config section (LoadConfigs) unless noted otherwise.
static bool g_hide_ui = false; // hide the game's HUD (for clean screenshots); session-only, never persisted
#if ENABLE_SMAA
static bool g_smaa_enable = true;
static bool g_smaa_predication = true;      // predicate SMAA on geometry, using the depth in the scene buffer's alpha
static float g_smaa_pred_tolerance = 0.02f; // plane deviation counted as a full edge, as a fraction of view depth
// RCAS sharpen on the SMAA output, opt-in at 0 (BL2/TW2 precedent): how much sharpening is wanted is a
// preference, not a target. At 0 the pass does not run and its full-resolution intermediate is never allocated.
static float g_rcas_sharpness = 0.f;
#if DEVELOPMENT
// Calibration aid for g_smaa_pred_tolerance, the only free parameter here: predication's effect is the ABSENCE of
// smearing, which the eye reads badly and worse in motion, so judge the mask itself instead of the frame.
static bool g_smaa_pred_debug = false; // show the predication mask instead of the antialiased frame
#endif
#endif

// Luma HDR bloom. Mirrored into GameSettings.LumaBloomEnable, which both the grade (composite) and the replaced
// gather (stop writing the vanilla glow) read — so this single switch really swaps one bloom for the other.
static bool g_luma_bloom_enable = true;
// RAW slider, driving the Luma pyramid only (the vanilla glow shares a buffer with the DoF blur, see the blur
// term in Luma_MOHA_Tonemap.hlsl). GameSettings.BloomIntensity is DERIVED from it in OnPresent, its sole writer.
static float g_bloom_intensity = 1.f;
// Divisor for the engine's per-area Bloom_Scale (rationale in the "Bloom Scale Reference" tooltip). 1.0 is the only
// value measured so far; if the engine holds the scale constant everywhere the ratio is always 1 and this is inert.
static float g_bloom_scale_ref = 1.f;

struct MedalOfHonorAirborneGameDeviceData final : public GameDeviceData
{
   // Repaired blend states, keyed by the ORIGINAL desc (DXHR precedent). Keying by desc rather than by the
   // source state's pointer means a released state can't leave a stale key that a later allocation reuses.
   struct BlendDescCompare
   {
      bool operator()(const D3D11_BLEND_DESC& a, const D3D11_BLEND_DESC& b) const
      {
         return memcmp(&a, &b, sizeof(D3D11_BLEND_DESC)) < 0;
      }
   };
   std::map<D3D11_BLEND_DESC, ComPtr<ID3D11BlendState>, BlendDescCompare> fixed_blend_states;

   bool has_drawn_tonemap = false;
   // Wrapper-build telemetry: an unkeyed dgVoodoo build fails SILENTLY (format-keyed upgrades still fire: fp16
   // canvas, no replacements). Latched across frames, reported once after warmup (OnPresent).
   bool ever_matched_final_pass = false;
   bool build_check_done = false;
   uint32_t frames_presented = 0;
#if DEVELOPMENT
   // Format-upgrade diagnostics, one-shot per DEVICE rather than per process: dgVoodoo recreates the device on
   // resolution and display-mode changes, which is exactly when the fp16 mirror is worth re-reading.
   bool diag_logged_gather = false;
   bool diag_logged_rt = false;
#endif
   // The canvas the final color pass wrote into this frame, captured from its bound RTV. Consumed by Hide UI
   // (see OnDrawOrDispatch) and by the SMAA hook. Released every Present.
   ComPtr<ID3D11Resource> canvas_res;

   // Non-owning view onto core's DrawBloom mip 0 (AddRef'd by DrawBloom; the pyramid itself is core-managed and
   // released with the swapchain). Rebuilt every frame the feature is on.
   ComPtr<ID3D11ShaderResourceView> srv_luma_bloom;

   // The scene buffer captured from t0 of the hooked final pass. Its ALPHA carries linear depth, which is what
   // SMAA predication reads. Released every Present.
   ComPtr<ID3D11ShaderResourceView> srv_scene;

#if ENABLE_SMAA
   // ---- SMAA (TW2/BL2 shape, see RunPostFinalGradeSMAA) ----
   // Metrics CB (b1) = (1/w, 1/h, w, h) + (predication scale, 0, 0, 0).
   ComPtr<ID3D11Buffer> cb_smaa_metrics;
   uint32_t smaa_metrics_w = 0, smaa_metrics_h = 0;
   float smaa_metrics_pred_scale = -1.f;
   uint32_t smaa_core_w = 0, smaa_core_h = 0;
   // SRV-readable snapshot of the canvas; the chain writes the canvas, so it must sample this copy instead.
   ComPtr<ID3D11Texture2D> tex_input;
   ComPtr<ID3D11ShaderResourceView> srv_input;
   uint32_t smaa_temps_w = 0, smaa_temps_h = 0;

   // SMAA predication: srv_scene's alpha turned into an edge-ness mask by the depth-extract CS.
   ComPtr<ID3D11Buffer> cb_pred;
   float pred_tolerance = -1.f;
   ComPtr<ID3D11Texture2D> tex_pred;
   ComPtr<ID3D11UnorderedAccessView> uav_pred;
   ComPtr<ID3D11ShaderResourceView> srv_pred;
   uint32_t pred_w = 0, pred_h = 0;

   void ReleasePredicationScratch()
   {
      uav_pred.reset();
      srv_pred.reset();
      tex_pred.reset();
      pred_w = pred_h = 0;
   }

   // RCAS. The intermediate exists ONLY while sharpening is on: with the slider at 0 the SMAA chain renders
   // straight into the canvas, which saves both this full-resolution copy and a write-back.
   ComPtr<ID3D11Buffer> cb_sharpen;
   uint32_t sharpen_w = 0, sharpen_h = 0;
   float sharpen_amount = -1.f;
   ComPtr<ID3D11Texture2D> tex_smaa_out;
   ComPtr<ID3D11RenderTargetView> tex_smaa_out_rtv;
   ComPtr<ID3D11ShaderResourceView> tex_smaa_out_srv;
   uint32_t smaa_out_w = 0, smaa_out_h = 0;

   void ReleaseSharpenScratch()
   {
      tex_smaa_out_rtv.reset();
      tex_smaa_out_srv.reset();
      tex_smaa_out.reset();
      smaa_out_w = smaa_out_h = 0;
   }

   // Turning the feature off must give the address space back: MOHA.exe is not large-address-aware (2 GB) and this
   // snapshot alone is ~66 MB at 4K, with core's own SMAA intermediates on top.
   void ReleaseSMAAScratch()
   {
      srv_input.reset();
      tex_input.reset();
      smaa_temps_w = smaa_temps_h = 0;
      ReleasePredicationScratch();
      ReleaseSharpenScratch();
   }
#endif

   // Deferred constant-buffer readback: copy at the draw, map the copy made two frames earlier with a non-blocking
   // Map, because the synchronous form would stall the GPU every frame. One ring per consumer (only bloom today).
   struct DeferredCBRing
   {
      static constexpr uint32_t kSlots = 3;
      ComPtr<ID3D11Buffer> staging[kSlots];
      uint32_t bytes = 0;
      uint32_t writes = 0;
   };

   // The engine's own per-area bloom scale (TrackBloomScale), off the gather pass. Negative until the first
   // successful readback, which selects the neutral fallback.
   DeferredCBRing bloom_cb_ring;
   float bloom_scale_live = -1.f;
   bool bloom_scale_captured_this_frame = false; // one ring advance per frame, re-armed at Present
};

class MedalOfHonorAirborne final : public Game
{
   // Pass identity by shader hash, folding both supported dgVoodoo builds (2.87.3 ps_5_0 + 2.81.3 ps_4_0).
   // Same shape as the two sibling ports on this wrapper (TW2 ContainsPixelShader/IsTonemap, BL2 IsBL2Tonemap).
   static bool ContainsPixelShader(const ShaderHashesList<OneShaderPerPipeline>& shader_hashes, uint32_t hash, uint32_t hash_v281)
   {
      return shader_hashes.Contains(hash, reshade::api::shader_stage::pixel) || shader_hashes.Contains(hash_v281, reshade::api::shader_stage::pixel);
   }

   static bool IsDofBloomGather(const ShaderHashesList<OneShaderPerPipeline>& hashes)
   {
      return ContainsPixelShader(hashes, kDofBloomGatherHash, kDofBloomGatherHash_v281);
   }

   // Exactly one of the two runs per frame, decided by the game's DoF setting (see the hash declarations).
   static bool IsFinalColorPass(const ShaderHashesList<OneShaderPerPipeline>& hashes)
   {
      return ContainsPixelShader(hashes, kUberPostHash, kUberPostHash_v281) || ContainsPixelShader(hashes, kGammaCorrectionHash, kGammaCorrectionHash_v281);
   }

   static MedalOfHonorAirborneGameDeviceData& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<MedalOfHonorAirborneGameDeviceData*>(device_data.game);
   }

   // Named injected shaders live in unordered_maps the render thread otherwise only reads: look them up with
   // "find" (operator[] would default-insert on a miss, mutating a map core's draw helpers read concurrently).
   template <typename ShaderMap>
   static auto FindShader(const ShaderMap& shaders, uint32_t name)
   {
      const auto it = shaders.find(name);
      return it != shaders.end() ? it->second.get() : nullptr;
   }
   template <typename ShaderMap>
   static bool AllShadersReady(const ShaderMap& shaders, std::initializer_list<uint32_t> names)
   {
      for (const uint32_t name : names)
      {
         if (FindShader(shaders, name) == nullptr)
            return false;
      }
      return true;
   }

   static bool CreateImmutableCB(ID3D11Device* device, const void* data, UINT size, ComPtr<ID3D11Buffer>& out)
   {
      out.reset();
      D3D11_BUFFER_DESC bd = {};
      bd.ByteWidth = size;
      bd.Usage = D3D11_USAGE_IMMUTABLE;
      bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
      D3D11_SUBRESOURCE_DATA sd = {};
      sd.pSysMem = data;
      return SUCCEEDED(device->CreateBuffer(&bd, &sd, out.put()));
   }

   // The SMAA snapshot must pass the canvas' live format: CopyResource requires source and destination formats
   // to match.
   static bool CreateDefaultTex(ID3D11Device* device, uint32_t w, uint32_t h, UINT bind_flags, ComPtr<ID3D11Texture2D>& out, DXGI_FORMAT format)
   {
      out.reset();
      D3D11_TEXTURE2D_DESC td = {};
      td.Width = w;
      td.Height = h;
      td.MipLevels = 1;
      td.ArraySize = 1;
      td.Format = format;
      td.SampleDesc.Count = 1;
      td.Usage = D3D11_USAGE_DEFAULT;
      td.BindFlags = bind_flags;
      return SUCCEEDED(device->CreateTexture2D(&td, nullptr, out.put()));
   }

   // One cbuffer row from the copy made two frames ago. False when there is nothing to read yet: fresh ring, failed
   // allocation, or a slot still in flight. Two frames of latency is nothing against an area's bloom scale.
   static bool ReadCBRowDeferred(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, ID3D11Buffer* cb,
      MedalOfHonorAirborneGameDeviceData::DeferredCBRing& ring, uint32_t row, float out[4])
   {
      if (cb == nullptr)
         return false;
      D3D11_BUFFER_DESC bd = {};
      cb->GetDesc(&bd);
      if (bd.ByteWidth < (row + 1) * 16)
         return false;

      // Copying the whole buffer (a few KB) rather than a byte range: a full-resource copy is the path already
      // proven on this game's dgVoodoo-created constant buffers, and the size is irrelevant at this rate.
      if (ring.bytes != bd.ByteWidth)
      {
         D3D11_BUFFER_DESC sd = {};
         sd.ByteWidth = bd.ByteWidth;
         sd.Usage = D3D11_USAGE_STAGING;
         sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
         for (auto& s : ring.staging)
         {
            s.reset();
            if (FAILED(native_device->CreateBuffer(&sd, nullptr, s.put())))
            {
               for (auto& r : ring.staging) // drop a partial allocation rather than run on half a ring
                  r.reset();
               ring.bytes = 0;
               return false;
            }
         }
         ring.bytes = bd.ByteWidth;
         ring.writes = 0;
      }

      constexpr uint32_t kSlots = MedalOfHonorAirborneGameDeviceData::DeferredCBRing::kSlots;
      native_device_context->CopyResource(ring.staging[ring.writes % kSlots].get(), cb);
      ring.writes++;
      if (ring.writes < kSlots)
         return false; // nothing old enough to read yet

      // The slot about to be overwritten next is the oldest one, i.e. the copy issued kSlots frames ago.
      ID3D11Buffer* oldest = ring.staging[ring.writes % kSlots].get();
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      if (FAILED(native_device_context->Map(oldest, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped)) || mapped.pData == nullptr)
         return false; // still in flight; try again next frame rather than blocking
      std::memcpy(out, (const uint8_t*)mapped.pData + (size_t)row * 16, 16);
      native_device_context->Unmap(oldest, 0);
      return true;
   }

   // Follow the artist's per-area bloom: UE3 authors Bloom_Scale per PostProcessVolume and the gather gets it in
   // cb4[10].x, keeping area-to-area variation. Donor: MELE. Threshold is a literal 1.0 in the shader.
   static void TrackBloomScale(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, MedalOfHonorAirborneGameDeviceData& gd)
   {
      ComPtr<ID3D11Buffer> cb;
      native_device_context->PSGetConstantBuffers(4, 1, cb.put());
      float row[4];
      if (!ReadCBRowDeferred(native_device, native_device_context, cb.get(), gd.bloom_cb_ring, 10, row))
         return;
      // Reject implausible readback data rather than let it reach the frame.
      if (row[0] >= 0.f && row[0] < 100.f)
         gd.bloom_scale_live = row[0];
   }

   // dgVoodoo sometimes leaves blending ENABLED on a secondary render target while RT0 has it off; D3D9 has one
   // global blend state and only per-RT write masks, so the game never asked for it and that target is corrupted.
   // Diagnosed in TW2 on the same wrapper (water PS 0xDA16C815: RT1 is the linear depth fog reads). Repair = copy
   // RT0's blend fields onto the offenders, keeping write masks; the inverse shape is only logged, since enabling
   // blending could only add damage. Returns true iff it ran the original draw itself (caller returns Replaced).
   static bool FixImpossiblePerRTBlend(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, MedalOfHonorAirborneGameDeviceData& gd, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, std::function<void()>* original_draw_dispatch_func)
   {
      // Our own injected passes set their blend state deliberately. Re-issuing the draw is the only way to apply
      // a different state, so without that callback there is nothing to do.
      if (is_custom_pass || (stages & reshade::api::shader_stage::pixel) == 0 || original_draw_dispatch_func == nullptr)
         return false;

      ComPtr<ID3D11BlendState> blend_state;
      FLOAT blend_factor[4];
      UINT sample_mask = 0;
      native_device_context->OMGetBlendState(blend_state.put(), blend_factor, &sample_mask);
      if (!blend_state)
         return false; // no state object = default (blending off everywhere)

      D3D11_BLEND_DESC bd;
      blend_state->GetDesc(&bd);
      if (!bd.IndependentBlendEnable)
         return false; // one state for all targets: already D3D9-shaped

      const bool rt0_blending = bd.RenderTarget[0].BlendEnable != FALSE;
#if !DEVELOPMENT
      // Only the "RT0 off, RTn on" shape is ever repaired, so outside DEVELOPMENT (where the inverse shape is
      // logged) a blending RT0 has nothing to do here: skip the descriptor scan and the render-target query both.
      if (rt0_blending)
         return false;
#endif

      // Only BOUND targets count: the wrapper leaves stale BlendEnable in unused slots, so the descriptor alone
      // matches nearly every single-target draw and would take over other hooks' passes.
      bool disagreement = false;
      for (UINT i = 1; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT && !disagreement; i++)
         disagreement = bd.RenderTarget[i].BlendEnable != bd.RenderTarget[0].BlendEnable;
      if (!disagreement)
         return false;

      ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
      native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, nullptr);
      bool bound[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
      for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
      {
         bound[i] = rtvs[i] != nullptr;
         if (rtvs[i])
            rtvs[i]->Release(); // OMGetRenderTargets hands back references; only the bound/not-bound answer is kept
      }

      // RT0's blend bit is loop-invariant, so the two shapes are mutually exclusive: one flag out of the loop.
      bool bound_disagreement = false;
      for (UINT i = 1; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT && !bound_disagreement; i++)
         bound_disagreement = bound[i] && bd.RenderTarget[i].BlendEnable != bd.RenderTarget[0].BlendEnable;
      const bool needs_fix = bound_disagreement && !rt0_blending;
      [[maybe_unused]] const bool inverse_shape = bound_disagreement && rt0_blending;

#if DEVELOPMENT
      // One line per distinct shader, so a play session reports every pass that carries this.
      if (needs_fix || inverse_shape)
      {
         // Locked: this function deliberately runs on every context (see its header), so two can reach the set.
         static std::mutex logged_shaders_mutex;
         static std::unordered_set<uint64_t> logged_shaders;
         const uint64_t pixel_shader_hash = original_shader_hashes.pixel_shaders[0];
         const std::scoped_lock logged_shaders_lock(logged_shaders_mutex);
         if (logged_shaders.emplace(pixel_shader_hash).second)
         {
            reshade::log::message(reshade::log::level::warning,
               std::format("[MOHA-BlendFix] impossible per-RT blend state (dgVoodoo artefact) on pixel shader 0x{:X} - {}", pixel_shader_hash, needs_fix ? "repaired" : "inverse shape, left alone").c_str());
         }
      }
#endif

      if (!needs_fix)
         return false;

      ComPtr<ID3D11BlendState> fixed_state;
      if (const auto it = gd.fixed_blend_states.find(bd); it != gd.fixed_blend_states.end())
      {
         fixed_state = it->second;
      }
      else
      {
         D3D11_BLEND_DESC fixed_desc = bd;
         for (UINT i = 1; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
         {
            if (fixed_desc.RenderTarget[i].BlendEnable == bd.RenderTarget[0].BlendEnable)
               continue;
            const UINT8 write_mask = fixed_desc.RenderTarget[i].RenderTargetWriteMask; // legal per-RT in D3D9, keep it
            fixed_desc.RenderTarget[i] = bd.RenderTarget[0];
            fixed_desc.RenderTarget[i].RenderTargetWriteMask = write_mask;
         }
         if (FAILED(native_device->CreateBlendState(&fixed_desc, fixed_state.put())) || !fixed_state)
            return false; // leave the draw untouched rather than run it half-applied
         gd.fixed_blend_states[bd] = fixed_state;
      }

      native_device_context->OMSetBlendState(fixed_state.get(), blend_factor, sample_mask);
      (*original_draw_dispatch_func)();
      native_device_context->OMSetBlendState(blend_state.get(), blend_factor, sample_mask); // hand the game back its own state
      return true;
   }

   // The resource behind the currently bound RTV 0, or null. Identifies the canvas at the final color pass, and
   // tests whether a later draw targets that same canvas. Not DEVELOPMENT-only: Hide UI needs it to ship.
   static ComPtr<ID3D11Resource> GetBoundRenderTargetResource(ID3D11DeviceContext* native_device_context)
   {
      ComPtr<ID3D11Resource> res;
      ComPtr<ID3D11RenderTargetView> rtv;
      native_device_context->OMGetRenderTargets(1, rtv.put(), nullptr);
      if (rtv)
         rtv->GetResource(res.put());
      return res;
   }

#if DEVELOPMENT
   // The only honest read of an indirect format upgrade: the devkit sees the original handle and reports
   // r8g8b8a8_typeless / isResourceUpgraded:false either way (BL2). Core rebinds substituted RTVs first.
   static void DumpBoundRenderTarget(ID3D11DeviceContext* native_device_context, const char* label)
   {
      char msg[256];
      ComPtr<ID3D11RenderTargetView> rtv;
      native_device_context->OMGetRenderTargets(1, rtv.put(), nullptr);
      if (!rtv)
      {
         std::snprintf(msg, sizeof(msg), "[Luma] MOHA DIAG: %s RTV0 NOT BOUND", label);
         reshade::log::message(reshade::log::level::info, msg);
         return;
      }
      uint4 size;
      DXGI_FORMAT format;
      GetResourceInfo(rtv.get(), size, format);
      std::snprintf(msg, sizeof(msg), "[Luma] MOHA DIAG: %s RTV0 %ux%u res_fmt %s -> upgrade %s",
         label, size.x, size.y, GetFormatNameSafe(format),
         format == DXGI_FORMAT_R16G16B16A16_FLOAT ? "OK (fp16)" : "MISSING (not fp16)");
      reshade::log::message(reshade::log::level::info, msg);
   }
#endif // DEVELOPMENT

public:
   void OnInit(bool async) override
   {
      // Game-specific toggles consumed by the replaced pass (Luma_MOHA_Tonemap.hlsl).
      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"TONEMAP_TYPE", '1', true, false, "0 - SDR: Vanilla (clamped reference)\n1 - HDR: recover highlights + DICE display map"},
      };
      shader_defines_data.append_range(game_shader_defines_data);
      assert(shader_defines_data.size() < MAX_SHADER_DEFINES);

#if ENABLE_SMAA
      // The 6 SMAA passes are auto-registered by core from Luma_SMAA_impl. Only the predication CS is ours: it
      // turns the scene buffer's alpha (linear depth) into an R16F edge-ness signal in [0,1].
      native_shaders_definitions.emplace(CompileTimeStringHash("MOHA Depth Extract CS"),
         ShaderDefinition("Luma_MOHA_DepthExtract", reshade::api::pipeline_subobject_type::compute_shader));
      // RCAS sharpen PS, drawn via core "Copy VS" + DrawCustomPixelShader after SMAA.
      native_shaders_definitions.emplace(CompileTimeStringHash("MOHA Sharpen PS"),
         ShaderDefinition{"Luma_MOHA_Sharpen", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "sharpen_ps"});
#endif

      // Buffers stay in GAMMA space: the gamma-SDR HUD blends onto this canvas and a linear buffer washes it out.
      // Replaced passes pre-scale by Game/UIPaperWhite (UI_DRAW_TYPE 2); core composition encodes scRGB.
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(EARLY_DISPLAY_ENCODING_HASH).SetDefaultValue('0');
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('1'); // game shipped gamma-2.2 SDR
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(GAMUT_MAPPING_TYPE_HASH).SetDefaultValue('1'); // gamut-map wild colors in composition
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('2');       // HUD gets its own UIPaperWhite + gamma blend

      // dgVoodoo binds b0-b5 only (measured on every captured draw), so b12/b13 are free for Luma.
      // luma_data is used by the Display Composition; luma_ui stays off (UI drawn by the game).
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;
      luma_ui_cbuffer_index = -1;

      // Manual Scene + UI Paper White sliders instead of the OS HDR reference level. Core gates the separate
      // "UI Paper White" slider on UI_DRAW_TYPE >= 1 && !use_os_reference_white_level.
      use_os_reference_white_level = false;

      // User grade controls (read in Luma_MOHA_Tonemap.hlsl via LumaSettings.GameSettings). All vanilla by default.
      default_luma_global_game_settings.Exposure = 1.f; // multiplier (1x)
      default_luma_global_game_settings.Saturation = 1.f;
      default_luma_global_game_settings.HighlightDechroma = 0.f; // off by default; only the mandatory DICE/gamut desaturation applies
      default_luma_global_game_settings.BloomIntensity = 1.f;
      default_luma_global_game_settings.Contrast = 1.f;
      default_luma_global_game_settings.Dithering = 1.f; // subtle anti-banding on by default
      default_luma_global_game_settings.LumaBloomEnable = ENABLE_BLOOM ? 1.f : 0.f;
      // 1.0 is exactly where the game's own bright-pass sits, and the scene peaks at ~3.9 — only real sources bloom.
      default_luma_global_game_settings.BloomThreshold = 1.f;
      // Light AutoHDR on the Bink movie pass (Video_0x1AAC12AD): movies bypass the scene passes entirely, so
      // without it they sit flat at paper white. The pair is BL2's calibrated one (peak ~165 nits at 0.5).
      default_luma_global_game_settings.VideoAutoHDREnable = 1.f;
      default_luma_global_game_settings.VideoAutoHDRBoost = 0.5f;
      cb_luma_global_settings.GameSettings = default_luma_global_game_settings;
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new MedalOfHonorAirborneGameDeviceData;
   }

   // Core calls this at device destruction but never frees "device_data.game", so the allocation is ours (TW2/BL2).
   // GameDeviceData has no virtual destructor: delete through the concrete type or members leak.
   void OnDestroyDeviceData(DeviceData& device_data) override
   {
      delete static_cast<MedalOfHonorAirborneGameDeviceData*>(device_data.game);
      device_data.game = nullptr;
   }

#if ENABLE_BLOOM
   // Core's DrawKarisAverage output: full-res fp16, ~66 MB at 4K (it inherits the scene texture's size). Core
   // drops only the UAV, and only on swapchain init, so a feature-off toggle has to release both views itself.
   // Guarded with BLOOM, not SMAA: the Karis average is a bloom resource and its only caller is the bloom-off
   // release in OnPresent, so pairing it with SMAA made ENABLE_SMAA 0 + ENABLE_BLOOM 1 fail to compile.
   static void ReleaseCoreKarisAverage(DeviceData& device_data)
   {
      auto& mr = device_data.managed_resources;
      mr.unordered_access_views[CompileTimeStringHash("luma_karis_average")].reset();
      mr.shader_resource_views[CompileTimeStringHash("luma_karis_average")].reset();
   }
#endif

#if ENABLE_SMAA
   // Core's DrawSMAA intermediates, ~83 MB at 4K, sized from the RTV handed to them and dropped only on swapchain
   // init, not this canvas' resize. The SRVs hold their own reference, so release both.
   static void ReleaseCoreSMAAIntermediates(DeviceData& device_data)
   {
      auto& mr = device_data.managed_resources;
      mr.depth_stencil_views[CompileTimeStringHash("smaa_dsv")].reset();
      mr.render_target_views[CompileTimeStringHash("smaa_edge_detection")].reset();
      mr.render_target_views[CompileTimeStringHash("smaa_blending_weight_calculation")].reset();
      mr.shader_resource_views[CompileTimeStringHash("smaa_edge_detection")].reset();
      mr.shader_resource_views[CompileTimeStringHash("smaa_blending_weight_calculation")].reset();
   }

   // SMAA on the graded gamma canvas from the post-draw callback, so it lands after the grade and before the HUD.
   // TW2/BL2 chain: snapshot -> SRV -> DrawSMAA, last pass into the canvas RTV. No-op if incomplete.
   void RunPostFinalGradeSMAA(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data, MedalOfHonorAirborneGameDeviceData& gd, ID3D11Resource* canvas_res, ID3D11RenderTargetView* canvas_rtv)
   {
      uint4 cinfo{};
      DXGI_FORMAT cfmt = DXGI_FORMAT_UNKNOWN;
      GetResourceInfo(canvas_res, cinfo, cfmt);
      const uint32_t w = cinfo.x, h = cinfo.y;
      if (w == 0 || h == 0 || cfmt == DXGI_FORMAT_UNKNOWN)
         return;

      // Shader-readiness gate (async loader / dev live-reload): skip SMAA this frame if anything is missing.
      const bool smaa_ready =
         AllShadersReady(device_data.native_pixel_shaders, {CompileTimeStringHash("SMAA Edge Detection PS"), CompileTimeStringHash("SMAA Blending Weight Calculation PS"), CompileTimeStringHash("SMAA Neighborhood Blending PS")}) && AllShadersReady(device_data.native_vertex_shaders, {CompileTimeStringHash("SMAA Edge Detection VS"), CompileTimeStringHash("SMAA Blending Weight Calculation VS"), CompileTimeStringHash("SMAA Neighborhood Blending VS")});
      if (!smaa_ready)
         return;

      // Drop DrawSMAA's core-managed intermediates on resolution change so they recreate at the new size.
      if (gd.smaa_core_w != w || gd.smaa_core_h != h)
      {
         ReleaseCoreSMAAIntermediates(device_data);
         gd.smaa_core_w = w;
         gd.smaa_core_h = h;
      }

      // Edge-ness from the scene alpha (linear depth). Scale and mask fall back together: 2.0 with a null mask would
      // raise the threshold frame-wide. The CS maps texels 1:1, hence the size check.
      auto* pred_cs = FindShader(device_data.native_compute_shaders, CompileTimeStringHash("MOHA Depth Extract CS"));
      bool pred_ok = g_smaa_predication && gd.srv_scene.get() != nullptr && pred_cs != nullptr;
      if (pred_ok)
      {
         uint4 sinfo{};
         DXGI_FORMAT sfmt = DXGI_FORMAT_UNKNOWN;
         GetResourceInfo(gd.srv_scene.get(), sinfo, sfmt);
         pred_ok = sinfo.x == w && sinfo.y == h;
      }
      if (pred_ok)
      {
         if (!gd.cb_pred || gd.pred_tolerance != g_smaa_pred_tolerance)
         {
            const float p[4] = {g_smaa_pred_tolerance, 0.f, 0.f, 0.f};
            if (CreateImmutableCB(native_device, p, sizeof(p), gd.cb_pred))
               gd.pred_tolerance = g_smaa_pred_tolerance;
         }
         if (!gd.tex_pred || gd.pred_w != w || gd.pred_h != h)
         {
            gd.ReleasePredicationScratch();
            if (CreateDefaultTex(native_device, w, h, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, gd.tex_pred, DXGI_FORMAT_R16_FLOAT))
            {
               native_device->CreateUnorderedAccessView(gd.tex_pred.get(), nullptr, gd.uav_pred.put());
               native_device->CreateShaderResourceView(gd.tex_pred.get(), nullptr, gd.srv_pred.put());
               gd.pred_w = w;
               gd.pred_h = h;
            }
         }
         pred_ok = gd.cb_pred && gd.uav_pred && gd.srv_pred;
      }

      const float pred_scale = pred_ok ? 2.f : 1.f;
      if (!gd.cb_smaa_metrics || gd.smaa_metrics_w != w || gd.smaa_metrics_h != h || gd.smaa_metrics_pred_scale != pred_scale)
      {
         const float metrics[8] = {1.f / (float)w, 1.f / (float)h, (float)w, (float)h, pred_scale, 0.f, 0.f, 0.f};
         if (CreateImmutableCB(native_device, metrics, sizeof(metrics), gd.cb_smaa_metrics))
         {
            gd.smaa_metrics_w = w;
            gd.smaa_metrics_h = h;
            gd.smaa_metrics_pred_scale = pred_scale;
         }
      }
      if (!gd.cb_smaa_metrics)
         return;

      // RCAS decides the chain's SHAPE, so resolve it before allocating anything: with sharpening off the last
      // SMAA pass writes the canvas directly, which removes both a full-frame write-back and the intermediate.
      // Core's fullscreen "Copy VS", shared by RCAS below and by the predication debug view.
      auto* copy_vs = FindShader(device_data.native_vertex_shaders, CompileTimeStringHash("Copy VS"));
      auto* sharpen_ps = FindShader(device_data.native_pixel_shaders, CompileTimeStringHash("MOHA Sharpen PS"));
      bool do_sharpen = g_rcas_sharpness > 0.f && copy_vs != nullptr && sharpen_ps != nullptr;
      if (do_sharpen)
      {
         if (!gd.cb_sharpen || gd.sharpen_w != w || gd.sharpen_h != h || gd.sharpen_amount != g_rcas_sharpness)
         {
            const float sp[4] = {(float)w, (float)h, g_rcas_sharpness, 0.f};
            if (CreateImmutableCB(native_device, sp, sizeof(sp), gd.cb_sharpen))
            {
               gd.sharpen_w = w;
               gd.sharpen_h = h;
               gd.sharpen_amount = g_rcas_sharpness;
            }
         }
         if (!gd.tex_smaa_out || gd.smaa_out_w != w || gd.smaa_out_h != h)
         {
            gd.ReleaseSharpenScratch();
            if (CreateDefaultTex(native_device, w, h, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, gd.tex_smaa_out, cfmt))
            {
               native_device->CreateRenderTargetView(gd.tex_smaa_out.get(), nullptr, gd.tex_smaa_out_rtv.put());
               native_device->CreateShaderResourceView(gd.tex_smaa_out.get(), nullptr, gd.tex_smaa_out_srv.put());
               gd.smaa_out_w = w;
               gd.smaa_out_h = h;
            }
         }
         if (!gd.cb_sharpen || !gd.tex_smaa_out_rtv || !gd.tex_smaa_out_srv)
            do_sharpen = false; // allocation failed: fall back to the un-sharpened chain rather than dropping SMAA
      }

      if (!gd.tex_input || gd.smaa_temps_w != w || gd.smaa_temps_h != h)
      {
         gd.srv_input.reset();
         gd.tex_input.reset();
         if (CreateDefaultTex(native_device, w, h, D3D11_BIND_SHADER_RESOURCE, gd.tex_input, cfmt))
         {
            native_device->CreateShaderResourceView(gd.tex_input.get(), nullptr, gd.srv_input.put());
            gd.smaa_temps_w = w;
            gd.smaa_temps_h = h;
         }
      }
      if (!gd.srv_input)
         return;

      native_device_context->CopyResource(gd.tex_input.get(), canvas_res);

      // Scene alpha (linear depth) -> plane-deviation edge-ness in R16F; see Luma_MOHA_DepthExtract.hlsl for why
      // this is an edge test rather than a depth rescale.
      if (pred_ok)
      {
         DrawStateStack<DrawStateStackType::Compute> pred_cs_state;
         pred_cs_state.Cache(native_device_context, device_data.uav_max_count);

         ID3D11ShaderResourceView* ps_srv = gd.srv_scene.get();
         ID3D11UnorderedAccessView* ps_uav = gd.uav_pred.get();
         ID3D11Buffer* ps_cb = gd.cb_pred.get();
         native_device_context->CSSetShaderResources(0, 1, &ps_srv);
         native_device_context->CSSetUnorderedAccessViews(0, 1, &ps_uav, nullptr);
         native_device_context->CSSetConstantBuffers(0, 1, &ps_cb);
         native_device_context->CSSetShader(pred_cs, nullptr, 0);
         native_device_context->Dispatch((w + 7) / 8, (h + 7) / 8, 1);

         pred_cs_state.Restore(native_device_context);
      }

#if DEVELOPMENT
      // Calibration aid, driven from the Anti-Aliasing section. Reads the mask that was just written.
      if (pred_ok && g_smaa_pred_debug)
      {
         auto* copy_ps = FindShader(device_data.native_pixel_shaders, CompileTimeStringHash("Copy PS"));
         if (copy_vs != nullptr && copy_ps != nullptr)
         {
            // The mask is single-channel, so the core copy lands it in RED — unmistakably a debug view. Replaces
            // the antialiased frame rather than blending over it, hence the early return.
            DrawStateStack<DrawStateStackType::FullGraphics> debug_state;
            debug_state.Cache(native_device_context, device_data.uav_max_count);
            DrawCustomPixelShader(native_device_context, device_data.default_depth_stencil_state.get(), device_data.default_blend_state.get(), nullptr,
               copy_vs, copy_ps, gd.srv_pred.get(), canvas_rtv, w, h, false);
            debug_state.Restore(native_device_context);
            return;
         }
      }
#endif

      // Metrics CB at VS+PS b1 (DrawSMAA restores VS/PS/SRVs/RTs, but not cbuffers).
      ComPtr<ID3D11Buffer> vs_cb1_orig, ps_cb1_orig;
      native_device_context->VSGetConstantBuffers(1, 1, vs_cb1_orig.put());
      native_device_context->PSGetConstantBuffers(1, 1, ps_cb1_orig.put());
      ID3D11Buffer* mcb = gd.cb_smaa_metrics.get();
      native_device_context->VSSetConstantBuffers(1, 1, &mcb);
      native_device_context->PSSetConstantBuffers(1, 1, &mcb);

      // Reading the canvas as the target is safe: the chain samples the snapshot, never the canvas itself.
      DrawSMAA(native_device, native_device_context, device_data, do_sharpen ? gd.tex_smaa_out_rtv.get() : canvas_rtv, gd.srv_input.get(), gd.srv_input.get(), pred_ok ? gd.srv_pred.get() : nullptr /*predication signal*/);

      // RCAS on the SMAA output, written into the canvas.
      if (do_sharpen)
      {
         DrawStateStack<DrawStateStackType::FullGraphics> sharpen_state;
         sharpen_state.Cache(native_device_context, device_data.uav_max_count);

         ID3D11Buffer* scb = gd.cb_sharpen.get();
         native_device_context->PSSetConstantBuffers(0, 1, &scb);
         DrawCustomPixelShader(native_device_context, device_data.default_depth_stencil_state.get(), device_data.default_blend_state.get(), nullptr,
            copy_vs, sharpen_ps, gd.tex_smaa_out_srv.get(), canvas_rtv, w, h, false);

         sharpen_state.Restore(native_device_context);
      }

      ID3D11Buffer* vcb = vs_cb1_orig.get();
      ID3D11Buffer* pcb = ps_cb1_orig.get();
      native_device_context->VSSetConstantBuffers(1, 1, &vcb);
      native_device_context->PSSetConstantBuffers(1, 1, &pcb);
   }
#endif // ENABLE_SMAA

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& gd = GetGameDeviceData(device_data);

      const bool is_immediate = native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE;

      // Hide HUD: cancel draws that run AFTER the final color pass AND target the same canvas. The render-target test
      // is load-bearing - "everything after the tonemap" also swallows the present blit.
      if (g_hide_ui && is_immediate && !is_custom_pass && gd.has_drawn_tonemap && gd.canvas_res)
      {
         ComPtr<ID3D11Resource> rt = GetBoundRenderTargetResource(native_device_context);
         if (rt.get() == gd.canvas_res.get())
            return DrawOrDispatchOverrideType::Replaced;
      }

#if DEVELOPMENT
      // Producer-side gate for the bloom buffer upgrade: the devkit cannot see an indirect upgrade at all, so the
      // render target bound at this draw is the only place the fp16 mirror is observable.
      if (is_immediate && !gd.diag_logged_gather && IsDofBloomGather(original_shader_hashes))
      {
         gd.diag_logged_gather = true;
         DumpBoundRenderTarget(native_device_context, "bloom buffer");
      }
#endif

#if ENABLE_BLOOM
      // The gather is where the engine hands over its per-area Bloom_Scale, so it is where we take it. Once per
      // frame: a second capture would advance the ring twice and read a slot that is still in flight.
      if (g_luma_bloom_enable && is_immediate && !gd.bloom_scale_captured_this_frame && IsDofBloomGather(original_shader_hashes))
      {
         gd.bloom_scale_captured_this_frame = true;
         TrackBloomScale(native_device, native_device_context, gd);
      }
#endif

      // Wrapper-build telemetry, deliberately AHEAD of the gate below: an unkeyed dgVoodoo build has to be
      // reported whichever context recorded the pass (the warning itself fires in OnPresent).
      if (!gd.ever_matched_final_pass && IsFinalColorPass(original_shader_hashes))
         gd.ever_matched_final_pass = true;

      // The final color pass ends main post processing. Read the hash list before any early-out: is_custom_pass is
      // true for hash-replaced passes too. Gated on is_immediate (BL GOTY does the same).
      if (is_immediate && !gd.has_drawn_tonemap && IsFinalColorPass(original_shader_hashes))
      {
         gd.has_drawn_tonemap = true;
         device_data.has_drawn_main_post_processing = true;

         // Push LumaSettings HERE, at the seam, not inside a feature block: every consumer below reads b13 (the
         // bloom prefilter's BloomThreshold, and the grade itself), and the SMAA path runs the original draw and
         // returns Replaced, which makes core skip its own upload for this draw entirely (core.hpp: the override
         // check returns before SetLumaConstantBuffers). With this inside the bloom block, turning Luma bloom off
         // left the grade reading the previous upload. Above the state stack too: Restore() rolls PS constant
         // buffers back wholesale. "updated_cbuffers" is left alone so core still uploads on non-Replaced frames.
         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaSettings);

         // Remember what this pass draws into: Hide UI needs the resource identity, SMAA needs the view. Captured
         // every frame because an indirect upgrade or a resolution change can swap the mirror.
         ComPtr<ID3D11RenderTargetView> canvas_rtv;
         native_device_context->OMGetRenderTargets(1, canvas_rtv.put(), nullptr);
         gd.canvas_res.reset();
         if (canvas_rtv)
            canvas_rtv->GetResource(gd.canvas_res.put());

         // The scene is bound at t0 right here, and its alpha is the depth SMAA predication reads — so unlike the
         // TW2 donor there is no separate capture pass to keep in sync with this one.
         gd.srv_scene.reset();
         native_device_context->PSGetShaderResources(0, 1, gd.srv_scene.put());

#if DEVELOPMENT
         // Primary gate for the canvas upgrade: with indirect upgrades the devkit only ever sees the original
         // r8g8b8a8 resource, so the bound render target here is the only honest read of the fp16 mirror.
         if (!gd.diag_logged_rt)
         {
            gd.diag_logged_rt = true;
            DumpBoundRenderTarget(native_device_context, "canvas");
         }
#endif

#if ENABLE_BLOOM
         // Luma bloom pyramid off the fp16 LINEAR scene at t0, pre-glow by construction (the halo is added later, in
         // the grade). Karis average first: no TAA, so fireflies die spatially.
         gd.srv_luma_bloom.reset(); // DrawBloom AddRef's its mip 0 into this
         if (g_luma_bloom_enable && gd.srv_scene)
         {
            DrawStateStack<DrawStateStackType::FullGraphics> bloom_state;
            bloom_state.Cache(native_device_context, device_data.uav_max_count);

            ComPtr<ID3D11ShaderResourceView> srv_karis;
            DrawKarisAverage(native_device, native_device_context, device_data, gd.srv_scene.get(), srv_karis.put());
            if (srv_karis)
               DrawBloom(native_device, native_device_context, device_data, srv_karis.get(), kBloomNMips, g_bloom_sigmas, gd.srv_luma_bloom.put());

            bloom_state.Restore(native_device_context);
         }
         {
            // Bound every frame, null included: the composite is gated on LumaBloomEnable, not on the slot, and
            // dgVoodoo's placeholder would be sampled as garbage. The composite ADDS this.
            ID3D11ShaderResourceView* bloom_srv = gd.srv_luma_bloom.get();
            native_device_context->PSSetShaderResources(kLumaBloomSlot, 1, &bloom_srv);
         }
#endif

#if ENABLE_SMAA
         // Run the grade ourselves, then SMAA on its output, so the antialiasing lands before the HUD. Falls back to
         // a plain draw when the callback is unavailable (one frame without AA) rather than skipping the grade.
         if (g_smaa_enable && original_draw_dispatch_func != nullptr && canvas_rtv && gd.canvas_res)
         {
            (*original_draw_dispatch_func)();
            RunPostFinalGradeSMAA(native_device, native_device_context, device_data, gd, gd.canvas_res.get(), canvas_rtv.get());
            return DrawOrDispatchOverrideType::Replaced; // we ran the original draw ourselves
         }
#endif
      }

      // Blend repair LAST on purpose: it re-issues the draw, so it must yield to every hook above or it runs vanilla
      // a pass another hook meant to take over (it disabled a hook in TW2).
      if (FixImpossiblePerRTBlend(native_device, native_device_context, gd, stages, original_shader_hashes, is_custom_pass, original_draw_dispatch_func))
         return DrawOrDispatchOverrideType::Replaced;

      return DrawOrDispatchOverrideType::None; // never cancel the original draw (the replacement is by hash)
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& gd = GetGameDeviceData(device_data);

      // One-shot telemetry (BL GOTY precedent). The frame budget is warmup only: the menu runs the same pass, so
      // a few frames are enough.
      constexpr uint32_t kBuildCheckFrame = 120;
      if (!gd.build_check_done && ++gd.frames_presented >= kBuildCheckFrame)
      {
         gd.build_check_done = true;
         if (!gd.ever_matched_final_pass)
            reshade::log::message(reshade::log::level::warning,
               "[Luma] MOHA: no keyed final color pass seen after warmup -- the dgVoodoo build is probably neither 2.87.3 nor 2.81.3, so every shader replacement is inactive (re-dump the shaders for it).");
      }

      gd.has_drawn_tonemap = false;
      gd.canvas_res.reset(); // do not hold a reference across frames: it would outlive a resize or a mirror swap
      // Core never clears this, so leaving it set would claim a tonemapped scene on frames with no final pass
      // (movies, loading). Inert here: consumers need enable_ui_separation (off).
      device_data.has_drawn_main_post_processing = false;
      gd.srv_scene.reset(); // recaptured at the final pass every frame; never hold it across one

#if ENABLE_BLOOM
      gd.bloom_scale_captured_this_frame = false; // re-arm the once-per-frame ring advance

      // Give the address space back when the pyramid is off, on the render thread. Unconditional while off: resetting
      // empty entries is two map lookups. Only core's Karis buffer is reachable.
      if (!g_luma_bloom_enable)
         ReleaseCoreKarisAverage(device_data);

      // THE SOLE WRITER of the effective BloomIntensity. The UI only ever touches the raw slider and the enable
      // flag; if it wrote this field too, the two would fight each other while the slider is dragged.
      {
         auto& gs = cb_luma_global_settings.GameSettings;
         float effective = g_bloom_intensity;
         // Only while the Luma pyramid is the active bloom: with it off the vanilla glow already carries the engine's
         // area scale from inside the gather, so riding the ratio on top would count it twice.
         if (g_luma_bloom_enable && gd.bloom_scale_live >= 0.f && g_bloom_scale_ref > 1e-4f)
         {
            float ratio = gd.bloom_scale_live / g_bloom_scale_ref;
            ratio = std::clamp(ratio, 0.f, 4.f);
            effective = g_bloom_intensity * ratio;
         }
         if (std::abs(gs.BloomIntensity - effective) > 1e-4f)
         {
            gs.BloomIntensity = effective;
            device_data.cb_luma_global_settings_dirty = true;
         }
      }
#endif

#if ENABLE_SMAA
      // Give the address space back when a feature is off. Done here rather than in the ImGui handler so the
      // release happens on the render thread, never while a frame is mid-flight.
      if (!g_smaa_enable && gd.tex_input)
      {
         gd.ReleaseSMAAScratch();
         ReleaseCoreSMAAIntermediates(device_data);
         gd.smaa_core_w = gd.smaa_core_h = 0; // core recreates lazily; keep our latch from claiming they are current
      }
      else
      {
         if (!g_smaa_predication && gd.tex_pred)
            gd.ReleasePredicationScratch();
         if (g_rcas_sharpness <= 0.f && gd.tex_smaa_out)
            gd.ReleaseSharpenScratch();
      }
#endif
   }

   void LoadConfigs() override
   {
      // Grade sliders (cb_luma_global_settings_dirty is already true at init -> uploaded on first frame).
      reshade::get_config_value(nullptr, NAME, "Exposure", cb_luma_global_settings.GameSettings.Exposure);
      reshade::get_config_value(nullptr, NAME, "Saturation", cb_luma_global_settings.GameSettings.Saturation);
      reshade::get_config_value(nullptr, NAME, "HighlightsDesaturation", cb_luma_global_settings.GameSettings.HighlightDechroma);
#if ENABLE_BLOOM
      // The RAW slider; OnPresent derives GameSettings.BloomIntensity from it (see the sole-writer block there).
      // Inside the guard because it drives the Luma pyramid alone: with no pyramid there is nothing for it to scale.
      reshade::get_config_value(nullptr, NAME, "BloomIntensity", g_bloom_intensity);
      cb_luma_global_settings.GameSettings.BloomIntensity = g_bloom_intensity; // until the first Present
      reshade::get_config_value(nullptr, NAME, "LumaBloomEnable", g_luma_bloom_enable);
      cb_luma_global_settings.GameSettings.LumaBloomEnable = g_luma_bloom_enable ? 1.f : 0.f; // mirror to both shaders
      reshade::get_config_value(nullptr, NAME, "BloomThreshold", cb_luma_global_settings.GameSettings.BloomThreshold);
      reshade::get_config_value(nullptr, NAME, "BloomScaleRef", g_bloom_scale_ref);
#endif
      reshade::get_config_value(nullptr, NAME, "Contrast", cb_luma_global_settings.GameSettings.Contrast);
      reshade::get_config_value(nullptr, NAME, "Dithering", cb_luma_global_settings.GameSettings.Dithering);
      reshade::get_config_value(nullptr, NAME, "VideoAutoHDREnable", cb_luma_global_settings.GameSettings.VideoAutoHDREnable);
      reshade::get_config_value(nullptr, NAME, "VideoAutoHDRBoost", cb_luma_global_settings.GameSettings.VideoAutoHDRBoost);
#if ENABLE_SMAA
      reshade::get_config_value(nullptr, NAME, "SMAAEnable", g_smaa_enable);
      reshade::get_config_value(nullptr, NAME, "SMAAPredication", g_smaa_predication);
      reshade::get_config_value(nullptr, NAME, "SMAAPredicationTolerance", g_smaa_pred_tolerance);
      reshade::get_config_value(nullptr, NAME, "RCASSharpness", g_rcas_sharpness);
#endif
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
#if ENABLE_SMAA
      ImGui::SeparatorText("Anti-Aliasing");
      if (ImGui::Checkbox("SMAA Enable", &g_smaa_enable))
         reshade::set_config_value(nullptr, NAME, "SMAAEnable", g_smaa_enable);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Adds SMAA anti-aliasing (the game has none of its own).");
      if (g_smaa_enable)
      {
#if DEVELOPMENT
         // Predication is not a preference: it only relaxes the edge threshold back to base ULTRA on geometry and
         // never below, so off is strictly worse. Kept as a bisect switch for devs, shipped on and out of sight.
         if (ImGui::Checkbox("SMAA Predication", &g_smaa_predication))
            reshade::set_config_value(nullptr, NAME, "SMAAPredication", g_smaa_predication);
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Finds edges by geometry (scene depth) instead of by brightness alone.\nKeeps textures sharp while still antialiasing real silhouettes.");
         if (ImGui::SliderFloat("SMAA Predication Tolerance", &g_smaa_pred_tolerance, 0.002f, 0.2f, "%.3f", ImGuiSliderFlags_Logarithmic))
            reshade::set_config_value(nullptr, NAME, "SMAAPredicationTolerance", g_smaa_pred_tolerance);
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How far a surface may deviate from its local plane before it counts as an edge,\nas a fraction of view depth. Lower = more edges. This is the calibration lever,\nnot the SMAA threshold. Logarithmic: the parameter is relative.");
         ImGui::Checkbox("SMAA Predication Debug View", &g_smaa_pred_debug);
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Show the predication mask (red) instead of the frame.\nWant: black on flat surfaces, red across silhouettes.\nAll red = tolerance too low (predication is doing nothing).\nAll black = too high (silhouettes never regain sensitivity).");
#endif

         if (ImGui::SliderFloat("RCAS Sharpness", &g_rcas_sharpness, 0.f, 1.f))
            reshade::set_config_value(nullptr, NAME, "RCASSharpness", g_rcas_sharpness);
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Sharpening applied on top of SMAA (0 = off).");
         DrawResetButton(g_rcas_sharpness, 0.f, "RCASSharpness"); // writes the config itself (Serialize defaults true)
      }
#endif

      // --- HDR grade (read in Luma_MOHA_Tonemap.hlsl via LumaSettings.GameSettings; HDR tonemap path only) ---
      auto& gs = cb_luma_global_settings.GameSettings;
      ImGui::SeparatorText("Grade");

      if (ImGui::SliderFloat("Exposure", &gs.Exposure, 0.f, 2.f))
      {
         reshade::set_config_value(nullptr, NAME, "Exposure", gs.Exposure);
         device_data.cb_luma_global_settings_dirty = true;
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Overall image brightness (1 = vanilla).");
      if (DrawResetButton(gs.Exposure, default_luma_global_game_settings.Exposure, "Exposure"))
         device_data.cb_luma_global_settings_dirty = true;

      if (ImGui::SliderFloat("Contrast", &gs.Contrast, 0.f, 2.f))
      {
         reshade::set_config_value(nullptr, NAME, "Contrast", gs.Contrast);
         device_data.cb_luma_global_settings_dirty = true;
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Overall image contrast, HDR only (1 = vanilla).");
      if (DrawResetButton(gs.Contrast, default_luma_global_game_settings.Contrast, "Contrast"))
         device_data.cb_luma_global_settings_dirty = true;

      if (ImGui::SliderFloat("Saturation", &gs.Saturation, 0.f, 2.f))
      {
         reshade::set_config_value(nullptr, NAME, "Saturation", gs.Saturation);
         device_data.cb_luma_global_settings_dirty = true;
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Color saturation, HDR only (1 = vanilla).");
      if (DrawResetButton(gs.Saturation, default_luma_global_game_settings.Saturation, "Saturation"))
         device_data.cb_luma_global_settings_dirty = true;

      if (ImGui::SliderFloat("Highlights Desaturation", &gs.HighlightDechroma, 0.f, 1.f))
      {
         reshade::set_config_value(nullptr, NAME, "HighlightsDesaturation", gs.HighlightDechroma);
         device_data.cb_luma_global_settings_dirty = true;
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("How soon bright sources fade to neutral white, HDR only (0 = keep color at any brightness).");
      if (DrawResetButton(gs.HighlightDechroma, default_luma_global_game_settings.HighlightDechroma, "HighlightsDesaturation"))
         device_data.cb_luma_global_settings_dirty = true;

#if ENABLE_BLOOM
      ImGui::SeparatorText("Bloom");
      if (ImGui::Checkbox("Luma Bloom Enable", &g_luma_bloom_enable))
      {
         reshade::set_config_value(nullptr, NAME, "LumaBloomEnable", g_luma_bloom_enable);
         gs.LumaBloomEnable = g_luma_bloom_enable ? 1.f : 0.f;
         device_data.cb_luma_global_settings_dirty = true;
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Replaces the game's bloom with a wider, softer HDR bloom.");

      // Everything below drives the Luma pyramid and nothing else, so it all greys out with it: no slider here
      // can reach the game's own glow.
      ImGui::BeginDisabled(!g_luma_bloom_enable);

      // Raw slider only — the effective value is derived in OnPresent, which is its sole writer.
      if (ImGui::SliderFloat("Bloom Intensity", &g_bloom_intensity, 0.f, 2.f))
         reshade::set_config_value(nullptr, NAME, "BloomIntensity", g_bloom_intensity);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Bloom strength (1 = vanilla, 0 = none).");
      DrawResetButton(g_bloom_intensity, default_luma_global_game_settings.BloomIntensity, "BloomIntensity"); // writes the config itself (Serialize defaults true)

#if DEVELOPMENT
      if (ImGui::SliderFloat("Bloom Threshold", &gs.BloomThreshold, 0.f, 4.f, "%.2f"))
      {
         reshade::set_config_value(nullptr, NAME, "BloomThreshold", gs.BloomThreshold);
         device_data.cb_luma_global_settings_dirty = true;
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Linear scene brightness where bloom starts. 1.0 is where the game's own bright-pass sits,\nand the scene peaks around 3.9, so only real light sources glow.\nNear 0 the whole scene glows — that is the failure mode, not a setting.");
      if (DrawResetButton(gs.BloomThreshold, default_luma_global_game_settings.BloomThreshold, "BloomThreshold"))
         device_data.cb_luma_global_settings_dirty = true;

      if (ImGui::SliderFloat("Bloom Scale Reference", &g_bloom_scale_ref, 0.05f, 8.f, "%.3f", ImGuiSliderFlags_Logarithmic))
         reshade::set_config_value(nullptr, NAME, "BloomScaleRef", g_bloom_scale_ref);
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("The engine's own per-area bloom scale at which the Intensity slider reading 1 is correct.\nThe captured scale is applied as a ratio against this, never absolutely, because the two\npipelines do not carry the same energy. Calibrate in a scene whose glow already looks right.");
      {
         // Says at a glance whether the capture ever succeeded, and whether the engine actually authors this per
         // area: a scale_live that never moves between locations means the mechanism is inert by construction.
         const auto* gd_ui = static_cast<const MedalOfHonorAirborneGameDeviceData*>(device_data.game);
         const float live = gd_ui != nullptr ? gd_ui->bloom_scale_live : -1.f;
         ImGui::Text("  scale_live %.4f (-1 = not captured) | ref %.4f | effective %.4f", live, g_bloom_scale_ref, gs.BloomIntensity);
      }
#endif // DEVELOPMENT
      ImGui::EndDisabled();
#endif // ENABLE_BLOOM

      ImGui::SeparatorText("Effects");
      // Read in Video_0x1AAC12AD.ps_5_0.hlsl. Inert in SDR by construction (peak == paper white there makes
      // PumboAutoHDR an identity), so no display-mode gate is needed on either side.
      bool video_auto_hdr = gs.VideoAutoHDREnable > 0.5f;
      if (ImGui::Checkbox("Video AutoHDR", &video_auto_hdr))
      {
         gs.VideoAutoHDREnable = video_auto_hdr ? 1.f : 0.f;
         reshade::set_config_value(nullptr, NAME, "VideoAutoHDREnable", gs.VideoAutoHDREnable);
         device_data.cb_luma_global_settings_dirty = true;
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Adds HDR highlights to pre-rendered videos (HDR only).");

      ImGui::BeginDisabled(!video_auto_hdr);
      if (ImGui::SliderFloat("Video HDR Boost", &gs.VideoAutoHDRBoost, 0.f, 1.f))
      {
         reshade::set_config_value(nullptr, NAME, "VideoAutoHDRBoost", gs.VideoAutoHDRBoost);
         device_data.cb_luma_global_settings_dirty = true;
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Video highlight strength (0 = off).");
      if (DrawResetButton(gs.VideoAutoHDRBoost, default_luma_global_game_settings.VideoAutoHDRBoost, "VideoAutoHDRBoost"))
         device_data.cb_luma_global_settings_dirty = true;
      ImGui::EndDisabled();

      bool dithering = gs.Dithering > 0.5f;
      if (ImGui::Checkbox("Dithering", &dithering))
      {
         gs.Dithering = dithering ? 1.f : 0.f;
         reshade::set_config_value(nullptr, NAME, "Dithering", gs.Dithering);
         device_data.cb_luma_global_settings_dirty = true;
      }
      if (ImGui::IsItemHovered())
         // No "(HDR output)" qualifier: this dither is gated on TONEMAP_TYPE, not on the display mode, so it runs
         // in SDR too whenever that mode is on (as in the sibling Witcher 2 port).
         ImGui::SetTooltip("Reduces gradient banding.");

      ImGui::SeparatorText("UI");
      ImGui::Checkbox("Hide Gameplay UI", &g_hide_ui); // Session-only: a stuck "on" would look like a broken HUD.
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Disables the in-game UI.");
   }

   void PrintImGuiAbout() override
   {
      ImGui::PushTextWrapPos(0.f);
      ImGui::Text(
         "Luma for \"Medal of Honor: Airborne\" is developed by DristoforColumb and is open source and free.\n"
         "It adds native HDR, HDR bloom, SMAA anti-aliasing, and 16x anisotropic filtering.\n"
         "It runs through dgVoodoo2 (DirectX 9 -> 11).\n"
         "Do NOT run another HDR mod (e.g. RenoDX) alongside it.\n"
         "Thanks to the Luma team and contributors.\n"
         "If you enjoy it, consider donating.");
      ImGui::PopTextWrapPos();

      ImGui::NewLine();
      ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(70, 134, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(70 + 9, 134 + 9, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(70 + 18, 134 + 18, 0, 255));
      static const std::string donation_link = std::string("Buy DristoforColumb a Coffee on ko-fi ") + std::string(ICON_FK_OK);
      if (ImGui::Button(donation_link.c_str()))
         ShellExecuteA(nullptr, "open", "https://ko-fi.com/dristoforcolumb", nullptr, nullptr, SW_SHOWNORMAL);
      ImGui::PopStyleColor(3);

      ImGui::NewLine();
      static const std::string social_link = std::string("Join our \"HDR Den\" Discord ") + std::string(ICON_FK_SEARCH);
      if (ImGui::Button(social_link.c_str()))
      {
         // Unique link for Luma's HDR Den (tracks the origin of people joining); do not share for other purposes.
         static const std::string discord_link = std::string("https://discord.gg/J9fM") + std::string("3EVuEZ");
         ShellExecuteA(nullptr, "open", discord_link.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
      }
      static const std::string contributing_link = std::string("Contribute on Github ") + std::string(ICON_FK_FILE_CODE);
      if (ImGui::Button(contributing_link.c_str()))
         ShellExecuteA(nullptr, "open", "https://github.com/Filoppi/Luma-Framework", nullptr, nullptr, SW_SHOWNORMAL);

      ImGui::NewLine();
      ImGui::Text("Build Date: %s %s", __DATE__, __TIME__);

      ImGui::NewLine();
      ImGui::Text("Credits:"
                  "\n\nMain:"
                  "\nDristoforColumb"
                  "\n\nThird Party:"
                  "\nReShade"
                  "\nImGui"
                  "\nRenoDX (HDR tonemap method)"
                  "\nDICE (HDR tonemapper)"
                  "\nOklab (hue/chroma restoration)"
                  "\nSMAA (Iryoku)"
                  "\nAMD FidelityFX (RCAS)"
                  "\ndgVoodoo2 (DirectX 9 -> 11 wrapper, required)");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      const char* project_name = PROJECT_NAME;
      const char* cleared_project_name = (project_name[0] == '_') ? (project_name + 1) : project_name;

      uint32_t mod_version = 1;
      Globals::SetGlobals(cleared_project_name, "Medal of Honor Airborne Luma HDR mod", "", mod_version);
      // Finished: HDR, bloom, SMAA and AF all work, nothing known blocks play, and no further features are
      // planned. Publishing therefore drops the "proceed at your own risk" warning WorkInProgress raises.
      Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::Finished;

      // scRGB fp16 swapchain (the game's backbuffer is 8-bit).
      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;

      // Exclusive fullscreen -> borderless. "force_borderless" is what matters on top of the core default: it
      // covers LEAVING fullscreen too, so the window cannot come back with a title bar after alt-tab (TW2).
      prevent_fullscreen_state = true;
      force_borderless = true;

      // Two families clip the HDR signal, both upgraded INDIRECTLY (a mirror substituted at bind): changing a
      // dgVoodoo resource's creation format breaks the translator's bookkeeping - black screen even in menus (MEA
      // class). The canvas is r8g8b8a8_typeless; the DoF/bloom chain is r16g16b16a16_typeless the game VIEWS as
      // unorm, so writes clamped at 1.0. Keyed by FORMAT, not by shader hash: blits (0xE64861E9) interleave with the
      // blurs and the last writer before the grade is a blit. The size filters are load-bearing too: 2 GB here, and
      // TW2 hit bad_alloc at 4K on a broad list.
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
      enable_indirect_texture_format_upgrades = true; // creation-time mirrors, substituted at bind (BL2/TW2 scheme)
      enable_chain_indirect_texture_format_upgrades = ChainTextureFormatUpgradesType::DirectDependencies;
      texture_upgrade_formats = {
         reshade::api::format::r8g8b8a8_typeless,     // dgVoodoo's D3D9 backbuffer surface (the LDR canvas)
         reshade::api::format::r16g16b16a16_typeless, // DoF/bloom gather, blur and blit targets, viewed as unorm
      };
      // "No1Px" is mandatory under dgVoodoo (BL2): the wrapper binds 1x1 placeholders in every unused sampler slot
      // and a 1x1 trivially passes the aspect filter, so core would mirror those too (and assert in DEVELOPMENT).
      texture_format_upgrades_2d_size_filters = (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio | (uint32_t)TextureFormatUpgrades2DSizeFilters::No1Px;

      // AF16x, an addition rather than an upgrade: the 2007 video menu has no anisotropic option at all. The
      // load-bearing line is "force_upgrade_linear_samplers" - core otherwise rewrites only ANISOTROPIC samplers and
      // this wrapper binds none (TW2 census), which made mode 4 alone a no-op there. Mip LOD bias stays 0: no TAA, so
      // it would buy shimmer.
      enable_samplers_upgrade = true; // boot-time only (cannot be changed after device creation)
      samplers_upgrade_mode = 4;
      force_upgrade_linear_samplers = true;

      game = new MedalOfHonorAirborne();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}
