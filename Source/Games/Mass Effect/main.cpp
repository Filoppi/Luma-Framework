// Mass Effect (2007) Luma HDR mod (UE3 2007, 32-bit, DX9 -> D3D11 via dgVoodoo2), cloned from the MoH Airborne port;
// hashes are dgVoodoo-translated, 2.87.3 and 2.81.3 keyed. No depth SRV, no motion vectors: DLSS/DLAA impossible.

// No auto-debugger MessageBox: invisible under borderless and it blocks the loader (ReShade error 1114, as BL2/TW2).
#define DISABLE_AUTO_DEBUGGER 1

#define GAME_MASS_EFFECT 1

#define ENABLE_NGX 0 // NGX is x64-only and the game is 32-bit (and there are no motion vectors anyway)
#define ENABLE_FIDELITY_SK 0
#define GEOMETRY_SHADER_SUPPORT 0
// The game ships no AA at all (no option, no post AA pass, sampleCount 1), so SMAA adds rather than replaces.
#define ENABLE_SMAA 1
// Replaces the game's quarter-res bright-pass glow, which the replaced gather then stops writing.
#define ENABLE_BLOOM 1
// Outside DEVELOPMENT only this define makes original_draw_dispatch_func non-null; without it the callback never fires.
#define ENABLE_POST_DRAW_DISPATCH_CALLBACK 1

#include "..\..\Core\core.hpp"
#include <shellapi.h> // ShellExecuteA for About links (system() hangs the render thread in exclusive fullscreen)
#include <unordered_set>

// Both replaced, separate register maps. The gamma correction pass ends every frame; the uber runs first when it
// runs at all - the engine skips it in elevators and some loading scenes, which b12 UberRanThisFrame reports.
static constexpr uint32_t kUberPostHash = 0xAC8341E0;        // HDR grade -> fp16 intermediate
static constexpr uint32_t kGammaCorrectionHash = 0x17CE0932; // canvas encode, the FINAL pass
// Engine copy scene B -> A. Not replaced; DEVELOPMENT dumps its gamma row: a non-1 exponent would move the encode here.
static constexpr uint32_t kCopyPassHash = 0x1E37D75B;

// UE3 DOFAndBloomGather, REPLACED: bloom and DoF blur share one quarter-res target, so only it can drop the glow.
static constexpr uint32_t kDofBloomGatherHash = 0x56854256;  // QualityBloom=TRUE, 16 taps
static constexpr uint32_t kDofBloomGather4Hash = 0x28F8DB16; // QualityBloom=FALSE, 4 taps
// UE3 FilterPixelShader, 9 taps, NOT replaced: PS cb4[10..18] weights and VS cb4[25..29] offsets set the glow radius.
static constexpr uint32_t kBloomFilterHash = 0x6A1129DF;

// The same passes under dgVoodoo 2.81.3 (emits ps_4_0, so different hashes). Only C++-keyed hashes need a constant.
static constexpr uint32_t kUberPostHash_v281 = 0x786BC3B3;
static constexpr uint32_t kGammaCorrectionHash_v281 = 0x3BEF1CD6;
static constexpr uint32_t kCopyPassHash_v281 = 0xDDEAEB7C;
static constexpr uint32_t kDofBloomGatherHash_v281 = 0x4B65EEAE;
static constexpr uint32_t kDofBloomGather4Hash_v281 = 0xAA369C00;
static constexpr uint32_t kBloomFilterHash_v281 = 0x464E33BB;

// Luma bloom pyramid mip 0 for the grade replacements; clear of t0 (scene) and t1 (blur), the only slots they declare.
static constexpr uint32_t kLumaBloomSlot = 6;
// One sigma per mip, count taken FROM the array so the two cannot drift (MELE). Blended 0.5/0.5 = energy-preserving.
static constexpr float g_bloom_sigmas[] = {1.5f, 2.f, 2.f, 2.f, 1.f, 0.5f};

static bool g_hide_ui = false; // session-only, never persisted
#if ENABLE_SMAA
static bool g_smaa_enable = true;
static bool g_smaa_predication = true;      // on geometry, from the depth in the scene buffer's alpha
static float g_smaa_pred_tolerance = 0.02f; // a fraction of view depth
// RCAS on the SMAA output, opt-in (BL2/TW2): at 0 the pass never runs and its full-res intermediate is not allocated.
static float g_rcas_sharpness = 0.f;
#if DEVELOPMENT
// Calibration aid: predication's effect is the ABSENCE of smearing, which the eye misjudges - judge the mask instead.
static bool g_smaa_pred_debug = false;   // show the predication mask instead of the antialiased frame
static bool g_smaa_pred_measure = false; // one-shot: log the mask's coverage above 0.5 and percentiles
#endif
#endif
#if DEVELOPMENT
// One-shot dump of the copy/uber/gamma cb4 grade rows, disarmed by the gamma pass. On demand: each dump blocks.
static bool g_dump_pass_cb = false;
#endif

// Mirrored into GameSettings.LumaBloomEnable for both the grade and the replaced gather: one switch swaps the blooms.
static bool g_luma_bloom_enable = true;

struct MassEffectGameDeviceData final : public GameDeviceData
{
   // Repaired blend states, keyed by the ORIGINAL desc (DXHR) so a released state cannot leave a stale key behind.
   struct BlendDescCompare
   {
      bool operator()(const D3D11_BLEND_DESC& a, const D3D11_BLEND_DESC& b) const
      {
         return memcmp(&a, &b, sizeof(D3D11_BLEND_DESC)) < 0;
      }
   };
   std::map<D3D11_BLEND_DESC, ComPtr<ID3D11BlendState>, BlendDescCompare> fixed_blend_states;

   bool uber_ran_this_frame = false; // bloom injected, scene captured
   // "Canvas captured, SMAA done" is core's device_data.has_drawn_main_post_processing, set at the final pass.
   // An unkeyed dgVoodoo build fails SILENTLY (format upgrades still fire, replacements don't); reported after warmup.
   bool ever_matched_final_pass = false;
   uint32_t frames_presented = 0;
#if DEVELOPMENT
   // One-shot per DEVICE, not per process: dgVoodoo recreates the device on a resize, which is worth re-reading.
   bool diag_logged_gather = false;
   bool diag_logged_rt = false;
   // Vanilla bloom kernel capture, one-shot: the two filter draws of one frame (H then V) and the gather's constants.
   uint32_t diag_filter_dumps = 0;
   // HUD family: each lands on the fp16 mirror unclamped and needs a saturating UI_* replacement. Unreplaced PS only.
   std::unordered_set<uint32_t> diag_post_final_ps;
#endif
   // Deferred cbuffer readback (MoHA/MELE): copy at the draw, Map the copy from two frames ago without waiting.
   struct DeferredCBRing
   {
      static constexpr uint32_t kSlots = 3;
      ComPtr<ID3D11Buffer> staging[kSlots];
      uint32_t bytes = 0;
      uint32_t writes = 0;
      // One advance per frame (re-armed at Present): more would read copies still in flight; the Map never lands.
      bool advanced_this_frame = false;
   };
   // Gamma pass 1/display gamma (cb4[11].x, measured 0.625 = 1/1.6; uber and copy are 1.0), sent via GameSettings.
   DeferredCBRing gamma_cb_ring;
   float gamma_inverse_live = 0.f;
   bool gamma_inverse_valid = false;
   // Gather BloomScale (cb4[11].x, measured 0.1), same scheme. Per post-process volume in UE3, so it changes per area.
   DeferredCBRing bloom_cb_ring;
   float bloom_scale_live = 0.f;
   // Validity is a FLAG, not the sign: bloom off in a volume reports BloomScale 0, a real value the shader must get.
   bool bloom_scale_valid = false;

   // The canvas the final color pass wrote, from its bound RTV. Used by Hide UI and SMAA. Released every Present.
   ComPtr<ID3D11Resource> canvas_res;

   // Non-owning view onto core's DrawBloom mip 0 (core-managed, released with the swapchain). Rebuilt every frame.
   ComPtr<ID3D11ShaderResourceView> srv_luma_bloom;

   // Scene buffer from t0 of the hooked final pass. Its ALPHA carries linear depth, which SMAA predication reads.
   ComPtr<ID3D11ShaderResourceView> srv_scene;

#if ENABLE_SMAA
   // ---- SMAA (TW2/BL2 shape, see RunPostFinalGradeSMAA) ----
   // Metrics CB (b1) = (1/w, 1/h, w, h) + (predication scale, 0, 0, 0).
   // Canvas size every resource below (and core's DrawSMAA intermediates) was created for. A change releases them all
   // at once; each is then recreated on first use, so no resource tracks a size of its own.
   uint32_t smaa_w = 0, smaa_h = 0;
   ComPtr<ID3D11Buffer> cb_smaa_metrics;
   float smaa_metrics_pred_scale = -1.f;
   // SRV-readable snapshot of the canvas; the chain writes the canvas, so it must sample this copy instead.
   ComPtr<ID3D11Texture2D> tex_input;
   ComPtr<ID3D11ShaderResourceView> srv_input;
   // Its linear-light decode, for the neighborhood blend.
   ComPtr<ID3D11Texture2D> tex_input_linear;
   ComPtr<ID3D11UnorderedAccessView> uav_input_linear;
   ComPtr<ID3D11ShaderResourceView> srv_input_linear;

   // SMAA predication: srv_scene's alpha turned into an edge-ness mask by the depth-extract CS.
   ComPtr<ID3D11Buffer> cb_pred;
   float pred_tolerance = -1.f;
   ComPtr<ID3D11Texture2D> tex_pred;
   ComPtr<ID3D11UnorderedAccessView> uav_pred;
   ComPtr<ID3D11ShaderResourceView> srv_pred;

   void ReleasePredicationScratch()
   {
      uav_pred.reset();
      srv_pred.reset();
      tex_pred.reset();
   }

   // RCAS. The intermediate exists ONLY while sharpening is on: at 0 the last SMAA pass writes the canvas directly.
   ComPtr<ID3D11Buffer> cb_sharpen;
   float sharpen_amount = -1.f;
   ComPtr<ID3D11Texture2D> tex_smaa_out;
   ComPtr<ID3D11RenderTargetView> tex_smaa_out_rtv;
   ComPtr<ID3D11ShaderResourceView> tex_smaa_out_srv;

   void ReleaseSharpenScratch()
   {
      tex_smaa_out_rtv.reset();
      tex_smaa_out_srv.reset();
      tex_smaa_out.reset();
   }

   // Turning the feature off must give the address space back: ~132 MB at 4K here, and a stock exe is capped at 2 GB.
   void ReleaseSMAAScratch()
   {
      srv_input.reset();
      tex_input.reset();
      uav_input_linear.reset();
      srv_input_linear.reset();
      tex_input_linear.reset();
      ReleasePredicationScratch();
      ReleaseSharpenScratch();
   }
#endif
};

class MassEffect final : public Game
{
   // Pass identity by hash, folding both dgVoodoo builds (MoHA ContainsPixelShader, TW2 IsTonemap, BL2 IsBL2Tonemap).
   static bool ContainsPixelShader(const ShaderHashesList<OneShaderPerPipeline>& hashes, uint32_t hash, uint32_t hash_v281)
   {
      return hashes.Contains(hash, reshade::api::shader_stage::pixel) || hashes.Contains(hash_v281, reshade::api::shader_stage::pixel);
   }

   static bool IsDofBloomGather(const ShaderHashesList<OneShaderPerPipeline>& hashes)
   {
      return ContainsPixelShader(hashes, kDofBloomGatherHash, kDofBloomGatherHash_v281) || ContainsPixelShader(hashes, kDofBloomGather4Hash, kDofBloomGather4Hash_v281);
   }

   static bool IsUberPostPass(const ShaderHashesList<OneShaderPerPipeline>& hashes)
   {
      return ContainsPixelShader(hashes, kUberPostHash, kUberPostHash_v281);
   }

   // The last colour pass before the HUD: it writes the canvas, so the capture, Hide UI and SMAA all key on it.
   static bool IsFinalColorPass(const ShaderHashesList<OneShaderPerPipeline>& hashes)
   {
      return ContainsPixelShader(hashes, kGammaCorrectionHash, kGammaCorrectionHash_v281);
   }

   static MassEffectGameDeviceData& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<MassEffectGameDeviceData*>(device_data.game);
   }

   // Look injected shaders up with find(): operator[] default-inserts on a miss, mutating a map core's draw helpers
   // read.
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

   // Pass the canvas' live format: CopyResource requires source and destination to match.
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

   // One cbuffer row from the copy two frames ago. False when unreadable: fresh ring, failed alloc or slot in flight.
   static bool ReadCBRowDeferred(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, ID3D11Buffer* cb,
      MassEffectGameDeviceData::DeferredCBRing& ring, uint32_t row, float out[4])
   {
      if (cb == nullptr)
         return false;
      D3D11_BUFFER_DESC bd = {};
      cb->GetDesc(&bd);
      if (bd.ByteWidth < (row + 1) * 16)
         return false;

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

      if (ring.advanced_this_frame)
         return false; // see DeferredCBRing::advanced_this_frame

      constexpr uint32_t kSlots = MassEffectGameDeviceData::DeferredCBRing::kSlots;
      native_device_context->CopyResource(ring.staging[ring.writes % kSlots].get(), cb);
      ring.writes++;
      ring.advanced_this_frame = true;
      if (ring.writes < kSlots)
         return false; // nothing old enough to read yet

      // The next slot to be overwritten is the oldest: the copy issued kSlots - 1 frames ago.
      ID3D11Buffer* oldest = ring.staging[ring.writes % kSlots].get();
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      if (FAILED(native_device_context->Map(oldest, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped)) || mapped.pData == nullptr)
         return false; // still in flight; try again next frame rather than blocking
      std::memcpy(out, (const uint8_t*)mapped.pData + (size_t)row * 16, 16);
      native_device_context->Unmap(oldest, 0);
      return true;
   }

   // Track one row of dgVoodoo's PS constant mirror (b4), deferred so the Map never stalls. The row differs per pass.
   static void TrackCB4Row(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context,
      MassEffectGameDeviceData::DeferredCBRing& ring, uint32_t row_index, float min_valid, float max_valid, float* out, bool* out_valid)
   {
      ComPtr<ID3D11Buffer> cb;
      native_device_context->PSGetConstantBuffers(4, 1, cb.put());
      float row[4];
      if (!ReadCBRowDeferred(native_device, native_device_context, cb.get(), ring, row_index, row))
         return;
      if (row[0] >= min_valid && row[0] <= max_valid)
      {
         *out = row[0];
         *out_valid = true; // separate from the value: min_valid can legitimately BE 0 (bloom)
      }
   }

   // dgVoodoo leaves blending ON for a secondary RT with RT0 off; D3D9 has one global state (TW2 water 0xDA16C815).
   static bool FixImpossiblePerRTBlend(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, MassEffectGameDeviceData& gd, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, std::function<void()>* original_draw_dispatch_func)
   {
      // Injected passes set their blend deliberately; without the dispatch func the draw cannot be re-issued.
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
      // Only "RT0 off, RTn on" is repaired, so outside DEVELOPMENT a blending RT0 skips the scan and the RT query.
      if (rt0_blending)
         return false;
#endif

      // Only BOUND targets count: stale BlendEnable in unused slots would match nearly every single-target draw.
      bool disagreement = false;
      for (UINT i = 1; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT && !disagreement; i++)
         disagreement = bd.RenderTarget[i].BlendEnable != bd.RenderTarget[0].BlendEnable;
      if (!disagreement)
         return false;

      // RT0's blend bit is loop-invariant, so the two shapes are mutually exclusive: one flag out of the loop.
      ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
      native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, nullptr);
      bool bound_disagreement = false;
      for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
      {
         if (rtvs[i] == nullptr)
            continue;
         bound_disagreement |= i > 0 && bd.RenderTarget[i].BlendEnable != bd.RenderTarget[0].BlendEnable;
         rtvs[i]->Release(); // references handed back; only bound/not-bound matters
      }
      const bool needs_fix = bound_disagreement && !rt0_blending;
      [[maybe_unused]] const bool inverse_shape = bound_disagreement && rt0_blending;

#if DEVELOPMENT
      // One line per distinct shader. Locked: this function runs on every context, so two can reach the set.
      if (needs_fix || inverse_shape)
      {
         static std::mutex logged_shaders_mutex;
         static std::unordered_set<uint64_t> logged_shaders;
         const uint64_t pixel_shader_hash = original_shader_hashes.pixel_shaders[0];
         const std::scoped_lock logged_shaders_lock(logged_shaders_mutex);
         if (logged_shaders.emplace(pixel_shader_hash).second)
         {
            reshade::log::message(reshade::log::level::warning,
               std::format("[ME1-BlendFix] impossible per-RT blend state (dgVoodoo artefact) on pixel shader 0x{:X} - {}", pixel_shader_hash, needs_fix ? "repaired" : "inverse shape, left alone").c_str());
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

   // The resource behind bound RTV 0. Identifies the canvas and tests later draws against it; Hide UI needs it to ship.
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
   // The only honest read of an indirect upgrade: the devkit reports the original handle either way (BL2).
   static void DumpBoundRenderTarget(ID3D11DeviceContext* native_device_context, const char* label)
   {
      char msg[256];
      ComPtr<ID3D11RenderTargetView> rtv;
      native_device_context->OMGetRenderTargets(1, rtv.put(), nullptr);
      if (!rtv)
      {
         std::snprintf(msg, sizeof(msg), "[Luma] ME1 DIAG: %s RTV0 NOT BOUND", label);
         reshade::log::message(reshade::log::level::info, msg);
         return;
      }
      uint4 size;
      DXGI_FORMAT format;
      GetResourceInfo(rtv.get(), size, format);
      std::snprintf(msg, sizeof(msg), "[Luma] ME1 DIAG: %s RTV0 %ux%u res_fmt %s -> upgrade %s",
         label, size.x, size.y, GetFormatNameSafe(format),
         format == DXGI_FORMAT_R16G16B16A16_FLOAT ? "OK (fp16)" : "MISSING (not fp16)");
      reshade::log::message(reshade::log::level::info, msg);
   }

   // One-shot synchronous readback of b4: the devkit shows the buffers but not their contents. Stalls once per pass.
   static void DumpConstantRows(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, const char* label, uint32_t first_row, uint32_t row_count, bool vertex_stage = false)
   {
      ComPtr<ID3D11Buffer> cb;
      if (vertex_stage)
         native_device_context->VSGetConstantBuffers(4, 1, cb.put());
      else
         native_device_context->PSGetConstantBuffers(4, 1, cb.put());
      if (!cb)
         return;
      D3D11_BUFFER_DESC bd = {};
      cb->GetDesc(&bd);
      if (bd.ByteWidth < (first_row + row_count) * 16)
         return;
      D3D11_BUFFER_DESC sd = {};
      sd.ByteWidth = bd.ByteWidth;
      sd.Usage = D3D11_USAGE_STAGING;
      sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
      ComPtr<ID3D11Buffer> staging;
      if (FAILED(native_device->CreateBuffer(&sd, nullptr, staging.put())))
         return;
      native_device_context->CopyResource(staging.get(), cb.get());
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      if (FAILED(native_device_context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)) || mapped.pData == nullptr)
         return;
      const float* rows = (const float*)mapped.pData;
      for (uint32_t r = first_row; r < first_row + row_count; r++)
      {
         char msg[256];
         std::snprintf(msg, sizeof(msg), "[Luma] ME1 DIAG: %s cb4[%u] = %.6f %.6f %.6f %.6f", label, r, rows[r * 4 + 0], rows[r * 4 + 1], rows[r * 4 + 2], rows[r * 4 + 3]);
         reshade::log::message(reshade::log::level::info, msg);
      }
      native_device_context->Unmap(staging.get(), 0);
   }

#if ENABLE_SMAA
   // Calibration readback (MoHA method): coverage above 0.5 = geometry fraction, typically ~1%. One-shot; stalls.
   static void MeasurePredicationMask(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, ID3D11Texture2D* pred)
   {
      // Desc taken off the mask, not rebuilt: CopyResource requires agreement and the walk must cover what was copied.
      D3D11_TEXTURE2D_DESC td;
      pred->GetDesc(&td);
      td.Usage = D3D11_USAGE_STAGING;
      td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
      td.BindFlags = 0;
      td.MiscFlags = 0;
      ComPtr<ID3D11Texture2D> staging;
      if (FAILED(native_device->CreateTexture2D(&td, nullptr, staging.put())))
         return;
      native_device_context->CopyResource(staging.get(), pred);
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      if (FAILED(native_device_context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)) || mapped.pData == nullptr)
         return;

      constexpr int kBuckets = 1024;
      uint64_t histogram[kBuckets] = {};
      uint64_t total = 0, above_half = 0, nans = 0;
      double sum = 0.0;
      for (uint32_t y = 0; y < td.Height; y++)
      {
         const uint16_t* row = reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(mapped.pData) + (size_t)y * mapped.RowPitch);
         for (uint32_t x = 0; x < td.Width; x++)
         {
            const float v = DirectX::PackedVector::XMConvertHalfToFloat(row[x]);
            if (std::isnan(v))
            {
               nans++; // the CS met inf - inf in the scene alpha: counted, kept out of the mean
               continue;
            }
            total++;
            sum += v;
            if (v > 0.5f)
               above_half++;
            histogram[std::clamp((int)(v * kBuckets), 0, kBuckets - 1)]++;
         }
      }
      native_device_context->Unmap(staging.get(), 0);
      if (total == 0)
         return;

      auto percentile = [&](double fraction)
      {
         const uint64_t target = (uint64_t)(fraction * (double)total);
         uint64_t running = 0;
         for (int i = 0; i < kBuckets; i++)
         {
            running += histogram[i];
            if (running >= target)
               return (float)(i + 1) / (float)kBuckets;
         }
         return 1.f;
      };
      char msg[512];
      std::snprintf(msg, sizeof(msg), "[Luma] ME1 DIAG: SMAA predication mask %ux%u tolerance %.4f -> above 0.5 = %.2f%% | mean %.4f | p50 %.4f p90 %.4f p99 %.4f | NaN %llu",
         td.Width, td.Height, g_smaa_pred_tolerance, 100.0 * (double)above_half / (double)total, sum / (double)total, percentile(0.50), percentile(0.90), percentile(0.99), (unsigned long long)nans);
      reshade::log::message(reshade::log::level::info, msg);
   }
#endif // ENABLE_SMAA
#endif // DEVELOPMENT

public:
   void OnInit(bool async) override
   {
      // Game-specific toggles consumed by both replaced passes (Luma_ME1_Tonemap.hlsl).
      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"TONEMAP_TYPE", '1', true, false, "0 - SDR: Vanilla (clamped reference)\n1 - HDR: extended native grade + MacLeod-Boynton hue + DICE display map"},
      };
      shader_defines_data.append_range(game_shader_defines_data);
      assert(shader_defines_data.size() < MAX_SHADER_DEFINES);

#if ENABLE_SMAA
      // Core auto-registers the 6 SMAA passes. Added here: the linear decode the neighborhood blend reads, and the
      // predication CS turning scene alpha into R16F edge-ness in [0,1].
      native_shaders_definitions.emplace(CompileTimeStringHash("ME1 SMAA Linearize CS"),
         ShaderDefinition("Luma_ME1_SMAALinearize", reshade::api::pipeline_subobject_type::compute_shader));
      native_shaders_definitions.emplace(CompileTimeStringHash("ME1 Depth Extract CS"),
         ShaderDefinition("Luma_ME1_DepthExtract", reshade::api::pipeline_subobject_type::compute_shader));
      // RCAS PS, drawn via core "Copy VS" + DrawCustomPixelShader.
      native_shaders_definitions.emplace(CompileTimeStringHash("ME1 Sharpen PS"),
         ShaderDefinition{"Luma_ME1_Sharpen", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "sharpen_ps"});
#endif

      // Address-space ceiling, PROBED: no LARGE_ADDRESS_AWARE (0x0102 measured) = 2 GB, Luma's 4K scratch ~365 MB.
      static bool address_space_checked = false;
      if (!address_space_checked)
      {
         address_space_checked = true;
         SYSTEM_INFO sys_info = {};
         GetSystemInfo(&sys_info);
         if (reinterpret_cast<uintptr_t>(sys_info.lpMaximumApplicationAddress) < 0x80000000u)
            reshade::log::message(reshade::log::level::warning,
               "[Luma] ME1: this executable is NOT large-address-aware, so the process is capped at 2 GB of address "
               "space. Luma's own scratch is ~365 MB at 4K, on top of dgVoodoo's copies: expect std::bad_alloc (UE3 "
               "reports it as a rendering GPF) with 4K texture packs, with the devkit, or both. Patch the "
               "LARGE_ADDRESS_AWARE bit, or lower the resolution / turn off SMAA and HDR bloom.");
      }

      // GAMMA space: the gamma-SDR HUD blends onto this canvas and linear washes it out. Composition encodes scRGB.
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(EARLY_DISPLAY_ENCODING_HASH).SetDefaultValue('0');
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('1'); // game shipped gamma-2.2 SDR
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(GAMUT_MAPPING_TYPE_HASH).SetDefaultValue('1'); // gamut-map wild colors in composition
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('2');       // HUD gets its own UIPaperWhite + gamma blend

      // dgVoodoo's D3D9 mirrors are b3/b4. b11 (core DrawBloom's own constants) and b12/b13 are taken as free, as in
      // MoHA; the full slot occupancy is not measured for this game.
      // luma_ui stays off: the game draws its own UI.
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;
      luma_ui_cbuffer_index = -1;

      // Manual paper-white sliders, not the OS reference level. Core gates the UI one on UI_DRAW_TYPE >= 1.
      use_os_reference_white_level = false;

      default_luma_global_game_settings.Exposure = 1.f;
      default_luma_global_game_settings.Saturation = 1.f;
      default_luma_global_game_settings.HighlightDechroma = 0.f; // off by default; only the mandatory DICE/gamut desaturation applies
      default_luma_global_game_settings.BloomIntensity = 1.f;
      default_luma_global_game_settings.Contrast = 1.f;
      default_luma_global_game_settings.Dithering = 1.f; // on by default
      default_luma_global_game_settings.LumaBloomEnable = ENABLE_BLOOM ? 1.f : 0.f;
      // 1.0 is exactly where the game's own bright-pass sits (DofBloomGather_0x56854256: any channel > 1.0).
      default_luma_global_game_settings.BloomThreshold = 1.f;
      // Light AutoHDR on the Bink pass (Video_0x1A82565B): movies bypass the scene passes. BL2's calibrated pair: at 0.5
      // the peak is 165/80 = ~2x paper white.
      default_luma_global_game_settings.VideoAutoHDREnable = 1.f;
      default_luma_global_game_settings.VideoAutoHDRBoost = 0.5f;
      // Until the first readback lands (~3 frames): the shipped DisplayGamma 1.6 (DefaultEngine.ini), measured 0.625.
      default_luma_global_game_settings.DisplayGammaInverse = 0.625f;
      // Until the gather reports: the value measured on the Citadel.
      default_luma_global_game_settings.BloomScaleLive = 0.1f;
      cb_luma_global_settings.GameSettings = default_luma_global_game_settings;
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new MassEffectGameDeviceData;
   }

   // Core's default deletes through GameDeviceData*, which has no virtual destructor: delete the concrete type.
   void OnDestroyDeviceData(DeviceData& device_data) override
   {
      delete static_cast<MassEffectGameDeviceData*>(device_data.game);
      device_data.game = nullptr;
   }

   // b12 at the gamma seam: whether the uber ran (skipped in elevators), which the gamma replacement branches on.
   void UpdateLumaInstanceDataCB(CB::LumaInstanceDataPadded& data, CommandListData& cmd_list_data, DeviceData& device_data) override
   {
      data.GameData.UberRanThisFrame = GetGameDeviceData(device_data).uber_ran_this_frame ? 1.f : 0.f;
   }

#if ENABLE_SMAA
   // Core's DrawSMAA intermediates, ~83 MB at 4K, dropped only on swapchain init. Views hold references: release all.
   static void ReleaseCoreSMAAIntermediates(DeviceData& device_data)
   {
      auto& mr = device_data.managed_resources;
      mr.depth_stencil_views[CompileTimeStringHash("smaa_dsv")].reset();
      mr.render_target_views[CompileTimeStringHash("smaa_edge_detection")].reset();
      mr.render_target_views[CompileTimeStringHash("smaa_blending_weight_calculation")].reset();
      mr.shader_resource_views[CompileTimeStringHash("smaa_edge_detection")].reset();
      mr.shader_resource_views[CompileTimeStringHash("smaa_blending_weight_calculation")].reset();
   }

   // SMAA after the grade, before the HUD (TW2/BL2 chain): snapshot -> linear decode -> DrawSMAA -> [RCAS] -> canvas.
   void RunPostFinalGradeSMAA(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, DeviceData& device_data, MassEffectGameDeviceData& gd, ID3D11Resource* canvas_res, ID3D11RenderTargetView* canvas_rtv)
   {
      uint4 cinfo{};
      DXGI_FORMAT cfmt = DXGI_FORMAT_UNKNOWN;
      GetResourceInfo(canvas_res, cinfo, cfmt);
      const uint32_t w = cinfo.x, h = cinfo.y;
      if (w == 0 || h == 0 || cfmt == DXGI_FORMAT_UNKNOWN)
         return;

      // Shader-readiness gate (async loader / dev live-reload): skip SMAA this frame if anything is missing.
      auto* linearize_cs = FindShader(device_data.native_compute_shaders, CompileTimeStringHash("ME1 SMAA Linearize CS"));
      const bool smaa_ready = linearize_cs != nullptr &&
                              AllShadersReady(device_data.native_pixel_shaders, {CompileTimeStringHash("SMAA Edge Detection PS"), CompileTimeStringHash("SMAA Blending Weight Calculation PS"), CompileTimeStringHash("SMAA Neighborhood Blending PS")}) && AllShadersReady(device_data.native_vertex_shaders, {CompileTimeStringHash("SMAA Edge Detection VS"), CompileTimeStringHash("SMAA Blending Weight Calculation VS"), CompileTimeStringHash("SMAA Neighborhood Blending VS")});
      if (!smaa_ready)
         return;

      // Resolution change: drop every size-bound resource, ours and DrawSMAA's core-managed intermediates, so each is
      // recreated at the new size (below, or by core). The predication CB does not depend on the size and stays.
      if (gd.smaa_w != w || gd.smaa_h != h)
      {
         gd.ReleaseSMAAScratch();
         gd.cb_smaa_metrics.reset();
         gd.cb_sharpen.reset();
         ReleaseCoreSMAAIntermediates(device_data);
         gd.smaa_w = w;
         gd.smaa_h = h;
      }

      // Scale and mask fall back together: 2.0 with a null mask raises the threshold frame-wide. CS maps texels 1:1.
      auto* pred_cs = FindShader(device_data.native_compute_shaders, CompileTimeStringHash("ME1 Depth Extract CS"));
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
         if (!gd.uav_pred || !gd.srv_pred)
         {
            gd.ReleasePredicationScratch();
            if (CreateDefaultTex(native_device, w, h, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, gd.tex_pred, DXGI_FORMAT_R16_FLOAT))
            {
               native_device->CreateUnorderedAccessView(gd.tex_pred.get(), nullptr, gd.uav_pred.put());
               native_device->CreateShaderResourceView(gd.tex_pred.get(), nullptr, gd.srv_pred.put());
            }
         }
         pred_ok = gd.cb_pred && gd.uav_pred && gd.srv_pred;
      }

      const float pred_scale = pred_ok ? 2.f : 1.f;
      if (!gd.cb_smaa_metrics || gd.smaa_metrics_pred_scale != pred_scale)
      {
         const float metrics[8] = {1.f / (float)w, 1.f / (float)h, (float)w, (float)h, pred_scale, 0.f, 0.f, 0.f};
         if (CreateImmutableCB(native_device, metrics, sizeof(metrics), gd.cb_smaa_metrics))
            gd.smaa_metrics_pred_scale = pred_scale;
      }
      if (!gd.cb_smaa_metrics)
         return;

      // RCAS decides the chain's SHAPE: with sharpening off the last SMAA pass writes the canvas, saving a write-back.
      auto* copy_vs = FindShader(device_data.native_vertex_shaders, CompileTimeStringHash("Copy VS"));
      auto* sharpen_ps = FindShader(device_data.native_pixel_shaders, CompileTimeStringHash("ME1 Sharpen PS"));
      bool do_sharpen = g_rcas_sharpness > 0.f && copy_vs != nullptr && sharpen_ps != nullptr;
      if (do_sharpen)
      {
         if (!gd.cb_sharpen || gd.sharpen_amount != g_rcas_sharpness)
         {
            const float sp[4] = {(float)w, (float)h, g_rcas_sharpness, 0.f};
            if (CreateImmutableCB(native_device, sp, sizeof(sp), gd.cb_sharpen))
               gd.sharpen_amount = g_rcas_sharpness;
         }
         if (!gd.tex_smaa_out_rtv || !gd.tex_smaa_out_srv)
         {
            gd.ReleaseSharpenScratch();
            if (CreateDefaultTex(native_device, w, h, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, gd.tex_smaa_out, cfmt))
            {
               native_device->CreateRenderTargetView(gd.tex_smaa_out.get(), nullptr, gd.tex_smaa_out_rtv.put());
               native_device->CreateShaderResourceView(gd.tex_smaa_out.get(), nullptr, gd.tex_smaa_out_srv.put());
            }
         }
         if (!gd.cb_sharpen || !gd.tex_smaa_out_rtv || !gd.tex_smaa_out_srv)
            do_sharpen = false; // allocation failed: fall back to the un-sharpened chain rather than dropping SMAA
      }

      if (!gd.srv_input || !gd.uav_input_linear || !gd.srv_input_linear)
      {
         gd.srv_input.reset();
         gd.tex_input.reset();
         gd.uav_input_linear.reset();
         gd.srv_input_linear.reset();
         if (CreateDefaultTex(native_device, w, h, D3D11_BIND_SHADER_RESOURCE, gd.tex_input, cfmt) &&
             CreateDefaultTex(native_device, w, h, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, gd.tex_input_linear, DXGI_FORMAT_R16G16B16A16_FLOAT))
         {
            native_device->CreateShaderResourceView(gd.tex_input.get(), nullptr, gd.srv_input.put());
            native_device->CreateUnorderedAccessView(gd.tex_input_linear.get(), nullptr, gd.uav_input_linear.put());
            native_device->CreateShaderResourceView(gd.tex_input_linear.get(), nullptr, gd.srv_input_linear.put());
         }
      }
      if (!gd.srv_input || !gd.uav_input_linear || !gd.srv_input_linear)
         return;

      native_device_context->CopyResource(gd.tex_input.get(), canvas_res);

      // Linear-light decode of the snapshot for the neighborhood blend (Luma_ME1_SMAALinearize.hlsl).
      {
         DrawStateStack<DrawStateStackType::Compute> linearize_state;
         linearize_state.Cache(native_device_context, device_data.uav_max_count);
         ID3D11ShaderResourceView* lin_srv = gd.srv_input.get();
         ID3D11UnorderedAccessView* lin_uav = gd.uav_input_linear.get();
         native_device_context->CSSetUnorderedAccessViews(0, 1, &lin_uav, nullptr);
         native_device_context->CSSetShaderResources(0, 1, &lin_srv);
         native_device_context->CSSetShader(linearize_cs, nullptr, 0);
         native_device_context->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
         linearize_state.Restore(native_device_context);
      }

      // Scene alpha -> plane-deviation edge-ness (R16F); why an edge test, not a rescale: Luma_ME1_DepthExtract.hlsl.
      if (pred_ok)
      {
         DrawStateStack<DrawStateStackType::Compute> pred_cs_state;
         pred_cs_state.Cache(native_device_context, device_data.uav_max_count);

         ID3D11ShaderResourceView* pred_srv = gd.srv_scene.get();
         ID3D11UnorderedAccessView* pred_uav = gd.uav_pred.get();
         ID3D11Buffer* pred_cb = gd.cb_pred.get();
         native_device_context->CSSetShaderResources(0, 1, &pred_srv);
         native_device_context->CSSetUnorderedAccessViews(0, 1, &pred_uav, nullptr);
         native_device_context->CSSetConstantBuffers(0, 1, &pred_cb);
         native_device_context->CSSetShader(pred_cs, nullptr, 0);
         native_device_context->Dispatch((w + 7) / 8, (h + 7) / 8, 1);

         pred_cs_state.Restore(native_device_context);
      }

#if DEVELOPMENT
      // Calibration aids: both read the mask just written.
      if (g_smaa_pred_measure)
      {
         g_smaa_pred_measure = false;
         if (pred_ok)
            MeasurePredicationMask(native_device, native_device_context, gd.tex_pred.get());
         else
            reshade::log::message(reshade::log::level::warning, "[Luma] ME1 DIAG: SMAA predication mask not measured: predication inactive this frame (no scene capture, size mismatch or CS missing)");
      }
      if (pred_ok && g_smaa_pred_debug)
      {
         auto* copy_ps = FindShader(device_data.native_pixel_shaders, CompileTimeStringHash("Copy PS"));
         if (copy_vs != nullptr && copy_ps != nullptr)
         {
            // Single-channel, so the copy shows it in RED. It replaces the frame, hence the early return.
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
      DrawSMAA(native_device, native_device_context, device_data, do_sharpen ? gd.tex_smaa_out_rtv.get() : canvas_rtv, gd.srv_input_linear.get(), gd.srv_input.get(), pred_ok ? gd.srv_pred.get() : nullptr /*predication signal*/);

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

      // Hide HUD: cancel post-final draws targeting the same canvas - the render-target test is the load-bearing half.
      // The UI families are hash-replaced, so is_custom_pass is true for them: an original PS hash (not UINT64_MAX)
      // is what separates a replaced game draw from one of Luma's own injected passes.
      if (g_hide_ui && is_immediate && (!is_custom_pass || original_shader_hashes.pixel_shaders[0] != UINT64_MAX) && device_data.has_drawn_main_post_processing && gd.canvas_res)
      {
         ComPtr<ID3D11Resource> rt = GetBoundRenderTargetResource(native_device_context);
         if (rt.get() == gd.canvas_res.get())
            return DrawOrDispatchOverrideType::Replaced;
      }

#if ENABLE_BLOOM
      // The gather runs before the uber: read BloomScale here, deferred. Bloom-only, so it follows the bloom switch.
      if (g_luma_bloom_enable && is_immediate && !gd.bloom_cb_ring.advanced_this_frame && IsDofBloomGather(original_shader_hashes))
         TrackCB4Row(native_device, native_device_context, gd.bloom_cb_ring, 11, 0.f, 4.f, &gd.bloom_scale_live, &gd.bloom_scale_valid); // engine BloomScale, measured 0.1
#endif

#if DEVELOPMENT
      // HUD permutation net: logs unreplaced post-final canvas draws. UI_*.hlsl cover eight families x both dgVoodoo
      // builds; the net is complete only FOR WHAT DREW.
      if (is_immediate && !is_custom_pass && device_data.has_drawn_main_post_processing && gd.canvas_res && original_shader_hashes.pixel_shaders[0] != UINT64_MAX)
      {
         const uint32_t ps_hash = (uint32_t)original_shader_hashes.pixel_shaders[0];
         if (!gd.diag_post_final_ps.contains(ps_hash) && GetBoundRenderTargetResource(native_device_context).get() == gd.canvas_res.get())
         {
            gd.diag_post_final_ps.insert(ps_hash);
            ComPtr<ID3D11BlendState> blend_state;
            float blend_factor[4];
            UINT sample_mask;
            native_device_context->OMGetBlendState(blend_state.put(), blend_factor, &sample_mask);
            D3D11_BLEND_DESC blend_desc = {};
            if (blend_state)
               blend_state->GetDesc(&blend_desc);
            const D3D11_RENDER_TARGET_BLEND_DESC& rt0 = blend_desc.RenderTarget[0];
            char label[128];
            std::snprintf(label, sizeof(label), "post-final canvas PS 0x%08X UNREPLACED blend %u src %u dst %u srcA %u dstA %u", ps_hash, rt0.BlendEnable, rt0.SrcBlend, rt0.DestBlend, rt0.SrcBlendAlpha, rt0.DestBlendAlpha);
            DumpConstantRows(native_device, native_device_context, label, 10, 2);
         }
      }

      // The devkit cannot see an indirect upgrade, so the RT bound here is the only view of the fp16 mirror.
      if (is_immediate && !gd.diag_logged_gather && IsDofBloomGather(original_shader_hashes))
      {
         gd.diag_logged_gather = true;
         DumpBoundRenderTarget(native_device_context, "bloom buffer");
         // Vanilla bloom model: BloomScale (PS row 11.x) and 16 tap offsets (VS rows 20..27, xy/wz pairs, UV units).
         DumpConstantRows(native_device, native_device_context, "gather PS", 8, 4);
         DumpConstantRows(native_device, native_device_context, "gather VS", 20, 8, true);
      }
      // The separable blur: 9 weights (PS rows 10..18), 4 offset pairs (VS rows 25..29), both directions of one frame.
      if (is_immediate && gd.diag_filter_dumps < 2 && ContainsPixelShader(original_shader_hashes, kBloomFilterHash, kBloomFilterHash_v281))
      {
         gd.diag_filter_dumps++;
         DumpConstantRows(native_device, native_device_context, gd.diag_filter_dumps == 1 ? "filter#1 PS" : "filter#2 PS", 10, 9);
         DumpConstantRows(native_device, native_device_context, gd.diag_filter_dumps == 1 ? "filter#1 VS" : "filter#2 VS", 25, 5, true);
      }
#endif

      // Deliberately AHEAD of the gate below: an unkeyed build must be reported whichever context saw the pass.
      if (!gd.ever_matched_final_pass && (IsFinalColorPass(original_shader_hashes) || IsUberPostPass(original_shader_hashes)))
         gd.ever_matched_final_pass = true;

#if DEVELOPMENT
      // Which pass encodes: uber row 15.w, copy row 8, gamma row 11.x - measured 1.0 / 1.0 / 0.625.
      if (is_immediate && g_dump_pass_cb && ContainsPixelShader(original_shader_hashes, kCopyPassHash, kCopyPassHash_v281))
         DumpConstantRows(native_device, native_device_context, "copy 0x1E37D75B", 8, 1);
#endif

      // The uber pass. Read the hash list before any early-out: is_custom_pass is true for hash-replaced passes too.
      if (is_immediate && !gd.uber_ran_this_frame && IsUberPostPass(original_shader_hashes))
      {
         gd.uber_ran_this_frame = true;

         // Push LumaSettings at the seam, not in a feature block (MoHA): bloom off left the grade on a stale upload.
         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaSettings);

         // Scene at t0 here (fp16, alpha = linear depth): the bloom and SMAA predication source, no extra pass.
         gd.srv_scene.reset();
         native_device_context->PSGetShaderResources(0, 1, gd.srv_scene.put());

#if DEVELOPMENT
         if (g_dump_pass_cb)
         {
            DumpBoundRenderTarget(native_device_context, "uber target (scene B)");
            DumpConstantRows(native_device, native_device_context, "uber 0xAC8341E0", 8, 9);
         }
#endif

#if ENABLE_BLOOM
         // Pyramid off the fp16 LINEAR scene at t0, pre-glow. Karis average first: no TAA, so fireflies die spatially.
         gd.srv_luma_bloom.reset(); // DrawBloom AddRef's its mip 0 into this
         if (g_luma_bloom_enable && gd.srv_scene)
         {
            DrawStateStack<DrawStateStackType::FullGraphics> bloom_state;
            bloom_state.Cache(native_device_context, device_data.uav_max_count);

            ComPtr<ID3D11ShaderResourceView> srv_karis;
            DrawKarisAverage(native_device, native_device_context, device_data, gd.srv_scene.get(), srv_karis.put());
            // The sigmas are in mip texels, so nothing here is resolution-dependent.
            if (srv_karis)
               DrawBloom(native_device, native_device_context, device_data, srv_karis.get(), (int)std::size(g_bloom_sigmas), g_bloom_sigmas, gd.srv_luma_bloom.put());

            bloom_state.Restore(native_device_context);
         }
         {
            // Bound every frame, null included (composite gates on LumaBloomEnable): a stale slot samples garbage.
            ID3D11ShaderResourceView* bloom_srv = gd.srv_luma_bloom.get();
            native_device_context->PSSetShaderResources(kLumaBloomSlot, 1, &bloom_srv);
         }
#endif
      }

      if (is_immediate && !device_data.has_drawn_main_post_processing && IsFinalColorPass(original_shader_hashes))
      {
         device_data.has_drawn_main_post_processing = true;

         // Same seam rule: this pass reads LumaSettings and LumaData, and the SMAA path skips core's upload.
         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaSettings);
         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaData);

         // Gamma-only frames: nothing captured the scene for predication, and this pass reads the RAW fp16 scene at t0.
         if (!gd.uber_ran_this_frame)
         {
            gd.srv_scene.reset();
            native_device_context->PSGetShaderResources(0, 1, gd.srv_scene.put());
         }

         // Hide UI needs the resource, SMAA the view. Recaptured each frame: upgrades and resizes swap the mirror.
         ComPtr<ID3D11RenderTargetView> canvas_rtv;
         native_device_context->OMGetRenderTargets(1, canvas_rtv.put(), nullptr);
         gd.canvas_res.reset();
         if (canvas_rtv)
            canvas_rtv->GetResource(gd.canvas_res.put());

#if DEVELOPMENT
         // The devkit only sees the original r8g8b8a8 resource; the render target bound here is the only honest read.
         if (!gd.diag_logged_rt)
         {
            gd.diag_logged_rt = true;
            DumpBoundRenderTarget(native_device_context, "canvas");
         }
         if (g_dump_pass_cb)
         {
            DumpConstantRows(native_device, native_device_context, "gamma 0x17CE0932", 8, 4);
            g_dump_pass_cb = false; // last of the frame's three dumps (copy -> uber -> gamma), so disarm here
         }
#endif

         // The display gamma this pass applies, for the grade's SDR reference. Once per frame, via the gate above.
         TrackCB4Row(native_device, native_device_context, gd.gamma_cb_ring, 11, 0.25f, 1.f, &gd.gamma_inverse_live, &gd.gamma_inverse_valid); // 1/gamma for gamma in [1, 4]

#if ENABLE_SMAA
         // Run the pass ourselves, then SMAA, so AA lands before the HUD. Otherwise the game draws it without AA.
         if (g_smaa_enable && original_draw_dispatch_func != nullptr && canvas_rtv && gd.canvas_res)
         {
            (*original_draw_dispatch_func)();
            RunPostFinalGradeSMAA(native_device, native_device_context, device_data, gd, gd.canvas_res.get(), canvas_rtv.get());
            return DrawOrDispatchOverrideType::Replaced; // we ran the original draw ourselves
         }
#endif
      }

      // Blend repair LAST: it re-issues the draw, so it must yield to every hook above (it disabled one in TW2).
      if (FixImpossiblePerRTBlend(native_device, native_device_context, gd, stages, original_shader_hashes, is_custom_pass, original_draw_dispatch_func))
         return DrawOrDispatchOverrideType::Replaced;

      return DrawOrDispatchOverrideType::None; // never cancel the original draw (the replacement is by hash)
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& gd = GetGameDeviceData(device_data);

      // One-shot telemetry (BL GOTY). Warmup budget only: the menu runs the same pass, so a few frames are enough.
      constexpr uint32_t kBuildCheckFrame = 120;
      if (gd.frames_presented < kBuildCheckFrame && ++gd.frames_presented == kBuildCheckFrame && !gd.ever_matched_final_pass)
         reshade::log::message(reshade::log::level::warning,
            "[Luma] ME1: no keyed final color pass seen after warmup -- the dgVoodoo build is probably neither 2.87.3 nor 2.81.3, so every shader replacement is inactive (re-dump the shaders for it).");

      gd.uber_ran_this_frame = false;
      gd.gamma_cb_ring.advanced_this_frame = false; // re-arm the once-per-frame ring advances
      gd.bloom_cb_ring.advanced_this_frame = false;

      // Publish the gamma pass's inverse display gamma to the grade (GameSettings is the only channel between passes).
      if (gd.gamma_inverse_valid && std::abs(cb_luma_global_settings.GameSettings.DisplayGammaInverse - gd.gamma_inverse_live) > 1e-4f)
      {
         cb_luma_global_settings.GameSettings.DisplayGammaInverse = gd.gamma_inverse_live;
         device_data.cb_luma_global_settings_dirty = true;
      }
#if ENABLE_BLOOM
      // Same for the gather's BloomScale, the gain that makes Bloom Intensity 1 vanilla strength.
      if (gd.bloom_scale_valid && std::abs(cb_luma_global_settings.GameSettings.BloomScaleLive - gd.bloom_scale_live) > 1e-5f)
      {
         cb_luma_global_settings.GameSettings.BloomScaleLive = gd.bloom_scale_live;
         device_data.cb_luma_global_settings_dirty = true;
      }
#endif

      gd.canvas_res.reset(); // do not hold a reference across frames: it would outlive a resize or a mirror swap
      // Core never clears this: left set, it would claim a tonemapped scene on movie and loading frames.
      device_data.has_drawn_main_post_processing = false;
      gd.srv_scene.reset(); // recaptured every frame; never held across one

#if ENABLE_BLOOM
      // Give the address space back on the render thread: core's DrawKarisAverage output, ~66 MB at 4K. Core drops only
      // the UAV on swapchain init, so both views go here. Unconditional while off: resetting empty entries is two lookups.
      if (!g_luma_bloom_enable)
      {
         auto& mr = device_data.managed_resources;
         mr.unordered_access_views[CompileTimeStringHash("luma_karis_average")].reset();
         mr.shader_resource_views[CompileTimeStringHash("luma_karis_average")].reset();
      }
#endif

#if ENABLE_SMAA
      // Released here, not in the ImGui handler, so it happens on the render thread and never mid-frame.
      if (!g_smaa_enable && gd.tex_input)
      {
         gd.ReleaseSMAAScratch();
         ReleaseCoreSMAAIntermediates(device_data);
         gd.smaa_w = gd.smaa_h = 0; // core recreates lazily; keep the latch from claiming anything is current
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
      // Inside the guard because it drives the Luma pyramid alone: with no pyramid there is nothing for it to scale.
      reshade::get_config_value(nullptr, NAME, "BloomIntensity", cb_luma_global_settings.GameSettings.BloomIntensity);
      reshade::get_config_value(nullptr, NAME, "LumaBloomEnable", g_luma_bloom_enable);
      cb_luma_global_settings.GameSettings.LumaBloomEnable = g_luma_bloom_enable ? 1.f : 0.f; // mirror to both shaders
      reshade::get_config_value(nullptr, NAME, "BloomThreshold", cb_luma_global_settings.GameSettings.BloomThreshold);
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
         // Not a preference: on geometry it relaxes the threshold back to base ULTRA, never below. A bisect switch.
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
         if (ImGui::Button("Measure Predication Mask"))
            g_smaa_pred_measure = true;
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Log the mask's coverage above 0.5 plus percentiles to ReShade.log.\nReal silhouettes are ~1% of a typical frame; a working mask barely moves across a 20x tolerance sweep.\nStalls the GPU for one frame.");
#endif

         if (ImGui::SliderFloat("RCAS Sharpness", &g_rcas_sharpness, 0.f, 1.f))
            reshade::set_config_value(nullptr, NAME, "RCASSharpness", g_rcas_sharpness);
         if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Sharpening applied on top of SMAA (0 = off).");
         DrawResetButton(g_rcas_sharpness, 0.f, "RCASSharpness"); // writes the config itself (Serialize defaults true)
      }
#endif

      // --- Grade (read in Luma_ME1_Tonemap.hlsl via LumaSettings.GameSettings). HDR tonemap path only except Exposure
      // and the bloom fields, which apply on the vanilla SDR path too (bloom only when the uber pass runs). ---
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
         // "How far", not "how soon": DICE's ramp starts at a third of peak, so the slider sets depth, not onset.
         ImGui::SetTooltip("How far the brightest sources fade to neutral white, HDR only (0 = keep color at any brightness).");
      if (DrawResetButton(gs.HighlightDechroma, default_luma_global_game_settings.HighlightDechroma, "HighlightsDesaturation"))
         device_data.cb_luma_global_settings_dirty = true;

#if DEVELOPMENT
      if (ImGui::Button("Dump Pass Constants"))
         g_dump_pass_cb = true;
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Log the copy/uber/gamma passes' cb4 grade rows for the next frame to ReShade.log.\nMeasured 1.0 / 1.0 / 0.625 - press it on a gameplay frame, not on a loading fade.\nEach dump blocks on a GPU read.");
#endif

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

      // Everything below drives the Luma pyramid and greys out with it: nothing here reaches the game's own glow.
      ImGui::BeginDisabled(!g_luma_bloom_enable);

      if (ImGui::SliderFloat("Bloom Intensity", &gs.BloomIntensity, 0.f, 2.f))
      {
         reshade::set_config_value(nullptr, NAME, "BloomIntensity", gs.BloomIntensity);
         device_data.cb_luma_global_settings_dirty = true;
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Bloom strength (1 = vanilla, 0 = none).");
      if (DrawResetButton(gs.BloomIntensity, default_luma_global_game_settings.BloomIntensity, "BloomIntensity"))
         device_data.cb_luma_global_settings_dirty = true;

#if DEVELOPMENT
      if (ImGui::SliderFloat("Bloom Threshold", &gs.BloomThreshold, 0.f, 4.f, "%.2f"))
      {
         reshade::set_config_value(nullptr, NAME, "BloomThreshold", gs.BloomThreshold);
         device_data.cb_luma_global_settings_dirty = true;
      }
      if (ImGui::IsItemHovered())
         ImGui::SetTooltip("Linear scene brightness where bloom starts. 1.0 is where the game's own bright-pass sits.\nNear 0 the whole scene glows — that is the failure mode, not a setting.");
      if (DrawResetButton(gs.BloomThreshold, default_luma_global_game_settings.BloomThreshold, "BloomThreshold"))
         device_data.cb_luma_global_settings_dirty = true;

#endif // DEVELOPMENT
      ImGui::EndDisabled();
#endif // ENABLE_BLOOM

      ImGui::SeparatorText("Effects");
      // Read in Video_0x1A82565B.ps_5_0.hlsl. Inert in SDR: peak == paper white makes PumboAutoHDR an identity.
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
         "Luma for \"Mass Effect\" (2007) is developed by DristoforColumb and is open source and free.\n"
         "It adds HDR, HDR bloom, SMAA anti-aliasing, and 16x anisotropic filtering.\n"
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
                  "\nMacLeod-Boynton hue emulation (RenoDX)"
                  "\nSMAA (Iryoku)"
                  "\nAMD FidelityFX (RCAS)"
                  "\ndgVoodoo2 by Dege (DirectX 9 -> 11 wrapper, required)");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      const char* project_name = PROJECT_NAME;
      const char* cleared_project_name = (project_name[0] == '_') ? (project_name + 1) : project_name;

      uint32_t mod_version = 1;
      Globals::SetGlobals(cleared_project_name, "Mass Effect (2007) Luma HDR mod", "", mod_version);
      Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::Finished;

      // scRGB fp16 swapchain (the game's backbuffer is 8-bit).
      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      // The brightness slider is a D3D9 SetGammaRamp that dgVoodoo forwards to the OS ramp, distorting scRGB.
      // The game's DisplayGamma 1.6 is already applied in the grade.
      allow_disabling_gamma_ramp = true;

      // force_borderless also covers LEAVING fullscreen, so alt-tab cannot restore a title bar (TW2).
      prevent_fullscreen_state = true;
      force_borderless = true;

      // Two HDR-clipping families, upgraded INDIRECTLY: changing a dgVoodoo creation format black-screens it (MEA).
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
      enable_indirect_texture_format_upgrades = true; // creation-time mirrors, substituted at bind (BL2/TW2 scheme)
      enable_chain_indirect_texture_format_upgrades = ChainTextureFormatUpgradesType::DirectDependencies;
      texture_upgrade_formats = {
         reshade::api::format::r8g8b8a8_typeless,     // dgVoodoo's D3D9 backbuffer surface (the LDR canvas)
         reshade::api::format::r16g16b16a16_typeless, // DoF/bloom gather, blur and blit targets, viewed as unorm
      };
      // "No1Px" is mandatory under dgVoodoo (BL2): 1x1 placeholders fill unused slots and pass the aspect filter.
      texture_format_upgrades_2d_size_filters = (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio | (uint32_t)TextureFormatUpgrades2DSizeFilters::No1Px;

      // AF16x. force_upgrade_linear_samplers is load-bearing: otherwise core rewrites only anisotropic samplers (TW2).
      enable_samplers_upgrade = true; // boot-time only (cannot be changed after device creation)
      samplers_upgrade_mode = 4;
      force_upgrade_linear_samplers = true;

      game = new MassEffect();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}
