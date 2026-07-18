// Mass Effect: Andromeda — Luma DLAA mod (Frostbite 3, D3D11).
// Injects DLAA (DLSS/FSR at native res, in-game AA = TAA) and replaces FXAA with SMAA (in-game AA = FXAA).

#define GAME_MASS_EFFECT_ANDROMEDA 1

// Frostbite input quirks: any message box (e.g. dev asserts) permanently breaks the game's
// input, and focus-loss handling interferes too.
#define DISABLE_AUTO_DEBUGGER 1
#define DISABLE_FOCUS_LOSS_SUPPRESSION 1
#define AVOID_INPUT_LOSS 1

#define ENABLE_NGX 1
#define ENABLE_FIDELITY_SK 1 // FSR 3 Native AA as a vendor-neutral alternative to DLAA (selectable in core's "Super Resolution" combo)
#define GEOMETRY_SHADER_SUPPORT 0
#define ENABLE_SMAA 1 // replaces the game's FXAA pass (FXAA AA mode) with SMAA
// The dialogue/cutscene DOF-variant resolve is run-native-then-override via original_draw_dispatch_func,
// which the core only populates with this enabled — otherwise it is null outside DEVELOPMENT builds and
// every DOF resolve silently bails to native TAA in Test/Publishing.
#define ENABLE_POST_DRAW_DISPATCH_CALLBACK 1

#include "..\..\Core\core.hpp"
#include <d3d11_1.h>  // ID3D11DeviceContext1 (bound-range CB queries)
#include <shellapi.h> // ShellExecuteA for the About-tab link buttons (system("start") hangs in exclusive fullscreen)

// TAA color-resolve CS — DLAA/FSR injection point. The game ships this resolve as a permutation matrix: 4
// logic variants × 2 GPU tile-sizes (32x16 = warp-32/NVIDIA, 8x8 = wave-64/AMD+Intel), and picks the tile by
// GPU arch at runtime — so different vendors dispatch DIFFERENT hashes of the SAME pass, instruction-for-
// instruction identical per pair (e.g. 0x70E49B83 (8x8) == 0xD7E13B2A (32x16)). Hooking only one perm
// silently no-ops SR on every other vendor, so we match the whole set. (Broader feature variants with extra
// t5/t6 SRVs + u4/u5 UAVs are NOT hooked until confirmed color-resolve, not a temporal SSR/AO pass; the
// DEV/TEST outlier logger below surfaces any we miss.)
static constexpr uint32_t kTAAResolveHashes[] = {
   0xD7E13B2A,
   0x70E49B83, // variant A  (32x16 / 8x8)
   0xFE348A4C,
   0x174F06D1, // variant B
   0x789DECF6,
   0x3D06A19E, // variant C
   0x960B6C89,
   0x1986BDD0, // variant D
   // E family = the same resolve quality ladder minus the t2 mask input / u0 mask output (96% identical
   // body, tile twins instruction-identical). Dispatched for the MAIN MENU / loading background scene —
   // hooked so DLAA covers the menu too. I/O contract is a strict subset of A-D (t0/t1/t3/t4 in, u2/u3 out).
   0x1C0D65CC,
   0x3DD7FD62, // variant E-A (32x16 / 8x8)
   0x40918D68,
   0xCEF1D745, // variant E-B
   0xCFB58943,
   0xA1AA9997, // variant E-C
   0x2F3D250F,
   0x633550DF, // variant E-D
};
// DOF variant of the resolve (dialogue / cutscene): additionally temporally filters the DOF CoC
// (t5/t6 r16f current+history in -> u4/u5 filtered+history out; u4 is consumed by the DOF-setup CS right
// after the resolve). We must NOT cancel this dispatch — u4/u5 would go stale and break the bokeh.
// Instead: run the native dispatch first (it writes u0/u4/u5 with the game's own math — zero quality
// loss), then run SR and overwrite only its u2/u3 color output.
static constexpr uint32_t kTAAResolveDofHashes[] = {
   0x42871661,
   0xA280FBF8, // variant DOF-A (32x16 / 8x8)
   0x6514D8F7,
   0x34C459FC, // variant DOF-B
   0x631EF4A0,
   0xEBA90095, // variant DOF-C
   0x65882783,
   0x3BEAC6F3, // variant DOF-D
};
static bool IsTAAResolveDof(const ShaderHashesList<OneShaderPerPipeline>& hashes)
{
   for (uint32_t h : kTAAResolveDofHashes)
      if (hashes.Contains(h, reshade::api::shader_stage::compute))
         return true;
   return false;
}
static bool IsTAAResolve(const ShaderHashesList<OneShaderPerPipeline>& hashes)
{
   for (uint32_t h : kTAAResolveHashes)
      if (hashes.Contains(h, reshade::api::shader_stage::compute))
         return true;
   return IsTAAResolveDof(hashes);
}
#if DEVELOPMENT || TEST
// First non-sentinel compute hash of a dispatch (diagnostic logging only).
static uint32_t FirstComputeHash(const ShaderHashesList<OneShaderPerPipeline>& hashes)
{
   for (auto h : hashes.compute_shaders)
      if (h != UINT64_MAX)
         return (uint32_t)h;
   return 0;
}
#endif

// SR output hand-off mode into the game's resolve target (u2/u3), re-evaluated at every hooked resolve
// (the game's "Buffer Format" setting swaps the target between rgba16f and r11g11b10_float).
enum class HandoffMode : int
{
   Incompatible = -1, // no safe copy path — bail to native TAA (SR off beats a black screen)
   DirectCopy = 0,    // CopySubresourceRegion (rgba16f fallback when the copy CS is unavailable)
   CsCopy = 1,        // CS copy through the game's own typed UAV (format-converting, preferred)
};

static constexpr uint32_t kFXAAHash = 0x5B81D1F2;    // FXAA PS — replaced with SMAA
static constexpr uint32_t kGbufferVS_A = 0xC089424D; // gbuffer VS that binds the main camera CB at VS slot 2
static constexpr uint32_t kGbufferVS_B = 0xFF93953D; // (reliable jitter capture point)
// DLSS far_plane stand-in: MEA's projection is reverse-Z INFINITE-far (no finite far). DLSS is insensitive to the
// exact large value (used only for depth linearization).
static constexpr float kCamFar = 100000.f;

// --- User-facing settings (persisted via ReShade config; loaded in LoadConfigs, saved on UI change). Kept as
// file-scope globals so LoadConfigs (pre-device) can populate them. ---
static bool g_smaa_enable = true;
static float g_smaa_sharpness = 0.f; // RCAS off by default
// MEA exposes no usable reactivity/bias source for SR, so bias_mask stays nullptr.

struct MassEffectAndromedaGameDeviceData final : public GameDeviceData
{
   // --- Diagnostics ---
   uint32_t taa_hits_this_frame = 0;
   uint64_t taa_hits_total = 0;
   bool logged_first_hit = false;

   // --- Camera state captured once per frame from the per-view camera CB (CPU map pointer + bound offset) ---
   bool cam_valid_this_frame = false;
   // map_recs caches the CPU map ptr of every large WRITE_NO_OVERWRITE DYNAMIC CB this frame (the camera ring among
   // them), deduped by handle. Camera is read straight from ptr+bound_offset — no GPU readback (zero stall).
   // map_recs_mutex is MANDATORY — the ring is mapped on both the immediate and Frostbite worker threads.
   // Reset each present.
   struct MapRec
   {
      uint64_t handle;
      const void* data;
      uint64_t size;
   };
   static constexpr int kMaxMaps = 16;
   MapRec map_recs[kMaxMaps] = {};
   int map_rec_count = 0;
   std::mutex map_recs_mutex;
   // Cache of the (stage, slot) where the camera CB was last found, to avoid probing all 6 combos every frame.
   int cam_probe_is_compute = -1; // -1 = unknown
   UINT cam_probe_slot = 0;
   float cam_jitter_clip_x = 0.f; // raw [6].z
   float cam_jitter_clip_y = 0.f; // raw [7].z
   float cam_near = 0.06f;        // [8].w
   float cam_proj_m00 = 0.f;      // [6].x (FOV discriminant)
   float cam_proj_m11 = 0.f;      // [7].y (FOV discriminant)
   // Immediate-context ID3D11DeviceContext1, QI'd once and cached (GetImmediateCtx1). The two call sites are
   // already gated on GetType()==IMMEDIATE; the immediate ctx is unique per device, so this is stable for the
   // device's life. Released in OnDestroyDeviceData (GameDeviceData has no virtual dtor — see that method).
   ComPtr<ID3D11DeviceContext1> imm_ctx1;

   // --- DLSS output texture (we own it; DLSS writes here, we copy into u2/u3) ---
   ComPtr<ID3D11Texture2D> tex_dlss_output;
   ComPtr<ID3D11ShaderResourceView> srv_dlss_output; // t0 of the format-converting hand-off CS
   uint32_t dlss_out_w = 0;
   uint32_t dlss_out_h = 0;
   bool first_dlss_frame = true;
   // u2 hand-off mode, evaluated at every hooked resolve — see HandoffMode.
   HandoffMode u2_mode = HandoffMode::DirectCopy;
   bool logged_u2_mode = false; // one-shot: log u2's actual desc + the chosen mode

   // --- Live dev knobs (tunable without rebuild). Default signs are the stable trail-free set:
   // MV flip X+Y, jitter flip Y; jitter flip X shakes. ---
   bool mvs_flip_x = true;     // -> MV X scale = -0.5*W
   bool mvs_flip_y = true;     // -> MV Y scale = +0.5*H
   float mvs_scale_mult = 1.f; // 0.25..4
   bool jitter_flip_x = false; // jitter X = -clipX*0.5*W (flipping X shakes — keep off)
   bool jitter_flip_y = true;  // jitter Y = +clipY*0.5*H (removes shimmer)
   float sharpness = -1.f;     // <0 = DLSS default
   bool auto_exposure = true;
   bool mvs_jittered = false;
   // One-shot diagnostics
   bool logged_dof_hit = false; // first DOF-variant resolve (run-native-then-override path engaged)
   bool logged_draw_result = false;
   bool logged_capture_fail = false;
   bool logged_draw_fail = false; // one-shot: first time Draw() returned false → recovery-reset latch fired
#if DEVELOPMENT || TEST
   bool logged_maps_full = false;  // one-shot kMaxMaps cap warning
   bool logged_samplers = false;   // one-shot AF/sampler-upgrade verification
   bool logged_probe_hit = false;  // one-shot: camera fallback probe found the per-view CB (logs stage+slot)
   bool logged_probe_miss = false; // one-shot: no camera at a hooked resolve → bailed to native TAA
   // Outlier TAA-resolve diagnostic: distinct compute hashes with a resolve-like binding (u2+u3+t1) that are NOT
   // in kTAAResolveHashes, logged once each in OnDrawOrDispatch. Mutex: dispatches fire on Frostbite worker
   // threads (same reason as map_recs_mutex).
   std::unordered_set<uint32_t> diag_resolve_hashes;
   std::mutex diag_resolve_mutex;
#endif

   // --- Edge-triggered situational logging (DEV/TEST only): one summary line in OnPresent that
   // re-prints ONLY when the situation changes — a different in-game AA mode, DLAA engaging/dropping, an
   // SMAA/sharpness change, a loading screen — so you can read each transition in ReShade.log without spam. ---
   bool dlss_ran_this_frame = false;  // set true only when DLSS Draw() actually succeeded this frame
   uint32_t fxaa_seen_this_frame = 0; // game's FXAA pass seen this frame (counted regardless of smaa_enable)
#if DEVELOPMENT || TEST
   // Snapshot of everything the state line reports; OnPresent re-logs only when the current snapshot differs.
   // aa_mode: -2 uninit / -1 none(Off/cutscene) / 0 TAA / 1 FXAA. One field per reported value (was a row of
   // parallel log_* shadows + a hand-written change expression).
   struct LogState
   {
      int aa_mode = -2, dlaa = -1, smaa_replaced = -1, sharpen = -1, sr_supp = -1, sr_type = -99, smaa_en = -1, cam_valid = -1;
      float bias = -999.f;
      bool DiffersFrom(const LogState& o) const
      {
         return aa_mode != o.aa_mode || dlaa != o.dlaa || smaa_replaced != o.smaa_replaced || sharpen != o.sharpen ||
                sr_supp != o.sr_supp || sr_type != o.sr_type || smaa_en != o.smaa_en ||
                cam_valid != o.cam_valid || fabsf(bias - o.bias) > 1e-4f;
      }
   } log_state;
#endif

   // --- SMAA (replaces the game's FXAA pass in FXAA AA mode) --- (enable/sharpness are file-scope globals)
   uint32_t fxaa_hits_this_frame = 0;
#if ENABLE_SMAA
   ComPtr<ID3D11Buffer> cb_smaa_metrics; // float4(1/W,1/H,W,H) at output res, bound at VS+PS b1
   uint32_t smaa_metrics_w = 0, smaa_metrics_h = 0;
   ComPtr<ID3D11Texture2D> tex_smaa_out;
   ComPtr<ID3D11RenderTargetView> tex_smaa_out_rtv;
   ComPtr<ID3D11ShaderResourceView> tex_smaa_out_srv;
   ComPtr<ID3D11Buffer> cb_sharpen;
   uint32_t smaa_out_w = 0, smaa_out_h = 0;
   uint32_t sharpen_w = 0, sharpen_h = 0; // cache key for cb_sharpen
   float sharpen_amount = -1.f;           // cache key for cb_sharpen
   // Size the core-managed DrawSMAA intermediates (smaa_dsv / edge / blend RTs) were last built at. Core sizes them
   // from the FIRST RTV and only rebuilds on swapchain re-init — not on MEA's in-game Resolution Scale change — so we
   // drop them ourselves on a size change (see the SMAA branch). 0 = not built yet.
   uint32_t smaa_core_w = 0, smaa_core_h = 0;
#endif
};

class MassEffectAndromeda final : public Game
{
   static MassEffectAndromedaGameDeviceData& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<MassEffectAndromedaGameDeviceData*>(device_data.game);
   }

   // Returns the cached immediate-context ID3D11DeviceContext1 if `ctx` IS the immediate ctx, else nullptr.
   // QI'd once and cached on gd (do NOT Release the returned ptr — gd owns it).
   static ID3D11DeviceContext1* GetImmediateCtx1(MassEffectAndromedaGameDeviceData& gd, ID3D11DeviceContext* ctx)
   {
      if (ctx->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE)
         return nullptr;
      if (!gd.imm_ctx1)
         ctx->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(gd.imm_ctx1.put()));
      return gd.imm_ctx1.get();
   }

   // Real pixel size of a bound texture resource — the trusted size under in-game Resolution Scale (core's
   // render/output_resolution = swapchain can't be trusted). Overwrites w/h only when the desc is readable, leaving
   // the caller's swapchain fallback otherwise. ONE source for the FXAA + TAA paths.
   static bool TryGetTex2DSize(ID3D11Resource* res, uint32_t& w, uint32_t& h)
   {
      if (!res)
         return false;
      ComPtr<ID3D11Texture2D> tex;
      if (FAILED(res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(tex.put()))))
         return false;
      D3D11_TEXTURE2D_DESC d = {};
      tex->GetDesc(&d);
      if (!d.Width || !d.Height)
         return false;
      w = d.Width;
      h = d.Height;
      return true;
   }

   // Create an IMMUTABLE constant buffer holding `size` bytes from `data`. Resets `out` first; returns true on
   // success (out null on failure). Shared by the SMAA RT_METRICS CB and the RCAS sharpen CB.
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

   // Create a DEFAULT-usage RGBA16F 2D texture (1 mip, 1 sample) of w×h with the given bind flags. Resets `out`
   // first; returns true on success. Views are created by the caller (they differ per use). Shared by the SMAA
   // intermediate (SRV|RTV) and the DLSS output (SRV|UAV) to avoid duplicating the desc setup.
   static bool CreateDefaultRGBA16FTex(ID3D11Device* device, uint32_t w, uint32_t h, UINT bind_flags, ComPtr<ID3D11Texture2D>& out)
   {
      out.reset();
      D3D11_TEXTURE2D_DESC td = {};
      td.Width = w;
      td.Height = h;
      td.MipLevels = 1;
      td.ArraySize = 1;
      td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
      td.SampleDesc.Count = 1;
      td.Usage = D3D11_USAGE_DEFAULT;
      td.BindFlags = bind_flags;
      return SUCCEEDED(device->CreateTexture2D(&td, nullptr, out.put()));
   }

   // Read the per-view camera CB bound at (cs,slot) from its cached CPU map ptr; if valid, cache jitter/proj/near
   // into gd. Validation: signature [5]==(0,0,0,1) and [9].z==-1; plus, when expected_w/h>0, [1].xy must match the
   // render res (rejects other views). Pass expected=0 to accept on signature alone (gbuffer VS = reliably the main
   // view). Caller MUST hold gd.map_recs_mutex.
   static bool TryStoreCamera(ID3D11DeviceContext1* ctx1, MassEffectAndromedaGameDeviceData& gd, bool cs, UINT slot, float expected_w, float expected_h)
   {
      ID3D11Buffer* cb = nullptr;
      UINT fc = 0, nc = 0;
      if (cs)
         ctx1->CSGetConstantBuffers1(slot, 1, &cb, &fc, &nc);
      else
         ctx1->VSGetConstantBuffers1(slot, 1, &cb, &fc, &nc);
      if (!cb)
         return false;
      const uint64_t h = reinterpret_cast<uint64_t>(cb);
      const uint32_t off = fc * 16u;
      cb->Release();
      for (int i = 0; i < gd.map_rec_count; ++i)
      {
         if (gd.map_recs[i].handle != h || gd.map_recs[i].data == nullptr || (uint64_t)off + 160u > gd.map_recs[i].size)
            continue; // need [off .. off+10 float4]; nullptr = banned (seen DISCARD-mapped → pointer unsafe)
         const float* r = reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(gd.map_recs[i].data) + off);
         if (expected_w > 0.f && (fabsf(r[1 * 4 + 0] - expected_w) > 1.f || fabsf(r[1 * 4 + 1] - expected_h) > 1.f))
            return false; // [1].xy == res
         if (fabsf(r[5 * 4 + 0]) > 1e-3f || fabsf(r[5 * 4 + 1]) > 1e-3f || fabsf(r[5 * 4 + 2]) > 1e-3f || fabsf(r[5 * 4 + 3] - 1.f) > 1e-3f)
            return false; // [5]==(0,0,0,1)
         if (fabsf(r[9 * 4 + 2] + 1.f) > 1e-2f)
            return false; // [9].z == -1 (reverse-Z m32)
         gd.cam_proj_m00 = r[6 * 4 + 0];
         gd.cam_proj_m11 = r[7 * 4 + 1];
         gd.cam_jitter_clip_x = r[6 * 4 + 2];
         gd.cam_jitter_clip_y = r[7 * 4 + 2];
         gd.cam_near = r[8 * 4 + 3];
         gd.cam_valid_this_frame = true;
         return true;
      }
      return false;
   }

   // Capture the MAIN camera at a gbuffer VS draw (slot 2 = full-res main view), once per frame — the reliable
   // jitter source (not at the TAA dispatch: it's a COMPUTE pass by which point VS slot 2 holds a half-res view).
   static void CaptureCameraAtGbuffer(ID3D11DeviceContext1* ctx1, MassEffectAndromedaGameDeviceData& gd)
   {
      std::lock_guard<std::mutex> lock(gd.map_recs_mutex);
      TryStoreCamera(ctx1, gd, false, 2, 0.f, 0.f); // signature-only: the gbuffer hash already identifies the main view
   }

   static void SelectCameraCPU(ID3D11DeviceContext1* ctx1, MassEffectAndromedaGameDeviceData& gd, const float2& render_res)
   {
      // Fallback probe (used only if the gbuffer capture missed this frame). Guard the whole probe: it reads
      // gd.map_recs/map_rec_count, which Frostbite worker threads mutate via OnMapBufferRegion.
      std::lock_guard<std::mutex> lock(gd.map_recs_mutex);
      auto try_slot = [&](bool cs, UINT slot) -> bool
      { return TryStoreCamera(ctx1, gd, cs, slot, render_res.x, render_res.y); };
      if (gd.cam_probe_is_compute >= 0 && try_slot(gd.cam_probe_is_compute != 0, gd.cam_probe_slot))
         return;
      // Slots 2/1/3 first (the observed camera slots), then the whole CB range — frames without a main
      // gbuffer pass (menus) bind the per-view CB at less common slots, if at all. TryStoreCamera's
      // validation (layout signature + render-res match) rejects non-camera CBs, so the wide scan is safe.
      static constexpr UINT kProbeSlots[] = {2, 1, 3, 0, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
      for (UINT slot : kProbeSlots)
         for (int cs = 0; cs <= 1; ++cs)
            if (try_slot(cs != 0, slot))
            {
               gd.cam_probe_is_compute = cs;
               gd.cam_probe_slot = slot;
#if DEVELOPMENT || TEST
               if (!gd.logged_probe_hit)
               {
                  gd.logged_probe_hit = true;
                  char b[96];
                  snprintf(b, sizeof(b), "MEA: camera fallback probe hit at %s b%u", cs ? "CS" : "VS", slot);
                  reshade::log::message(reshade::log::level::info, b);
               }
#endif
               return;
            }
   }

public: // OnMapBufferRegion is referenced from DllMain (DLL_PROCESS_DETACH unregister), so it must be public.
   // Cache the CPU map ptr of every large (≥64KB) WRITE_NO_OVERWRITE DYNAMIC CB (camera ring among them), deduped
   // by handle. NO_OVERWRITE only (allocation stays committed → post-Unmap read is safe). A DISCARD map of a
   // tracked handle BANS it (data=nullptr, kept in the array): DISCARD hands the driver a new allocation and can
   // free the old one, so the cached pointer is a use-after-free for the camera probe — and a buffer the game
   // EVER discard-maps is not the always-NO_OVERWRITE camera ring, so re-caching it later would just re-arm the
   // hazard (the wide slot probe crashed on exactly this in menus, where UI CBs are DISCARD-cycled every frame).
   // OnDestroyResource removes the record entirely, which also un-bans a handle the runtime later recycles.
   // Fires on Frostbite worker threads → map_recs_mutex.
   static void OnMapBufferRegion(reshade::api::device* device, reshade::api::resource resource, uint64_t offset, uint64_t size, reshade::api::map_access access, void** data)
   {
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      if (!device_data.game)
         return;
      if (access == reshade::api::map_access::write_discard)
      {
         auto& gd = GetGameDeviceData(device_data);
         std::lock_guard<std::mutex> lock(gd.map_recs_mutex);
         for (int i = 0; i < gd.map_rec_count; ++i)
            if (gd.map_recs[i].handle == resource.handle)
            {
               gd.map_recs[i].data = nullptr; // ban — see header comment
               break;
            }
         return;
      }
      if (access != reshade::api::map_access::write_only || data == nullptr || *data == nullptr)
         return; // NO_OVERWRITE only
      ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
      D3D11_BUFFER_DESC bd = {};
      buffer->GetDesc(&bd);
      if ((bd.BindFlags & D3D11_BIND_CONSTANT_BUFFER) == 0 || bd.Usage != D3D11_USAGE_DYNAMIC)
         return;
      if (bd.ByteWidth < 65536)
         return; // camera ring buffer is ~1MB; skip small per-draw CBs
      auto& gd = GetGameDeviceData(device_data);
      std::lock_guard<std::mutex> lock(gd.map_recs_mutex); // fires on Frostbite worker threads — see map_recs_mutex note
      for (int i = 0; i < gd.map_rec_count; ++i)           // dedupe by handle — keep latest pointer
         if (gd.map_recs[i].handle == resource.handle)
         {
            if (gd.map_recs[i].data != nullptr) // banned handles stay banned until destroyed
            {
               gd.map_recs[i].data = *data;
               gd.map_recs[i].size = bd.ByteWidth;
            }
            return;
         }
      if (gd.map_rec_count < MassEffectAndromedaGameDeviceData::kMaxMaps)
         gd.map_recs[gd.map_rec_count++] = {resource.handle, *data, (uint64_t)bd.ByteWidth};
#if DEVELOPMENT || TEST
      else if (!gd.logged_maps_full) // cap hit → the camera handle may be dropped this frame (would force DLSS reset)
      {
         gd.logged_maps_full = true;
         reshade::log::message(reshade::log::level::warning, "MEA: map_recs cap (kMaxMaps) hit — raise it if camera capture starts missing.");
      }
#endif
   }

   // Drop the record of a destroyed buffer: its cached map pointer is dead, and the runtime may recycle the
   // handle for a brand-new buffer (which must not inherit a stale pointer or a DISCARD ban).
   static void OnDestroyResource(reshade::api::device* device, reshade::api::resource resource)
   {
      DeviceData* device_data = device->get_private_data<DeviceData>();
      if (device_data == nullptr || !device_data->game)
         return;
      auto& gd = GetGameDeviceData(*device_data);
      std::lock_guard<std::mutex> lock(gd.map_recs_mutex);
      for (int i = 0; i < gd.map_rec_count; ++i)
         if (gd.map_recs[i].handle == resource.handle)
         {
            gd.map_recs[i] = gd.map_recs[--gd.map_rec_count];
            break;
         }
   }

   void OnInit(bool async) override
   {
      // DLAA-only: we replace no shaders and upload no Luma CBs → disable all three (-1 = unused).
      luma_settings_cbuffer_index = -1;
      luma_data_cbuffer_index = -1;
      luma_ui_cbuffer_index = -1;
#if ENABLE_SMAA
      // RCAS sharpening PS for the SMAA output (reuses core's "Copy VS" fullscreen vertex shader).
      native_shaders_definitions.emplace(CompileTimeStringHash("MEA Sharpen PS"),
         ShaderDefinition{"Luma_MEA_Sharpen", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, "sharpen_ps"});
#endif
#if ENABLE_SR
      // Format-converting SR output hand-off: the game's "Buffer Format" setting can switch the TAA resolve
      // target (u2/u3) to r11g11b10_float, where CopySubresourceRegion from our rgba16f output silently
      // no-ops (black scene, live UI). This CS writes through the game's own typed UAV instead.
      native_shaders_definitions.emplace(CompileTimeStringHash("MEA SR Output Copy CS"),
         ShaderDefinition{"Luma_MEA_CopyColor", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "copy_color_cs"});
      // History (u3) variant: encodes to the native resolve's history format (encoded-domain RGB x512) so
      // frames where the native resolve runs (SR<->native transitions, dialogue run-native) read honest
      // history instead of a de-facto reset.
      native_shaders_definitions.emplace(CompileTimeStringHash("MEA SR History Copy CS"),
         ShaderDefinition{"Luma_MEA_CopyColor", reshade::api::pipeline_subobject_type::compute_shader, nullptr, "copy_color_history_cs"});
#endif
      // Cache the camera ring-buffer's CPU map pointer (read at the TAA dispatch by bound offset in
      // SelectCameraCPU) — replaces the per-frame GPU readback stall. Must run in all configs.
      reshade::register_event<reshade::addon_event::map_buffer_region>(OnMapBufferRegion);
      reshade::register_event<reshade::addon_event::destroy_resource>(OnDestroyResource);
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new MassEffectAndromedaGameDeviceData;
   }

   void OnDestroyDeviceData(DeviceData& device_data) override
   {
      // GameDeviceData has no virtual destructor, so release members here before delete.
      if (device_data.game)
      {
         auto& gd = GetGameDeviceData(device_data);
         gd.imm_ctx1.reset();
         gd.tex_dlss_output.reset();
         gd.srv_dlss_output.reset();
#if ENABLE_SMAA
         // SMAA area/search textures + DS states now live in core's device_data.managed_resources (released by core).
         gd.cb_smaa_metrics.reset();
         gd.tex_smaa_out.reset();
         gd.tex_smaa_out_rtv.reset();
         gd.tex_smaa_out_srv.reset();
         gd.cb_sharpen.reset();
#endif
      }
      delete device_data.game;
      device_data.game = nullptr;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& gd = GetGameDeviceData(device_data);

      // OnDrawOrDispatch also fires on Frostbite deferred contexts (worker threads); GetType() is invariant for this
      // call, so evaluate it once. All immediacy gates below use this.
      const bool is_immediate = native_device_context->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE;

      // --- Camera capture (jitter/near/FOV) at a gbuffer VS draw, once per frame (retries on the next gbuffer draw
      // if this one's camera map hasn't landed). Immediate-only keeps cam_valid_this_frame single-threaded (no race).
      // Here, not at the COMPUTE TAA dispatch: by then VS slot 2 holds a half-res view's camera. ---
      if (is_immediate && !gd.cam_valid_this_frame &&
          (original_shader_hashes.Contains(kGbufferVS_A, reshade::api::shader_stage::vertex) ||
             original_shader_hashes.Contains(kGbufferVS_B, reshade::api::shader_stage::vertex)))
      {
         if (ID3D11DeviceContext1* ctx1 = GetImmediateCtx1(gd, native_device_context))
            CaptureCameraAtGbuffer(ctx1, gd);
      }

#if DEVELOPMENT || TEST
      // Count the game's FXAA pass independently of smaa_enable so the OnPresent state line can report
      // the in-game AA mode (TAA vs FXAA vs Off) even when our SMAA replacement is disabled.
      if (original_shader_hashes.Contains(kFXAAHash, reshade::api::shader_stage::pixel))
         gd.fxaa_seen_this_frame++;
#endif

#if ENABLE_SMAA
      // --- FXAA pass (in-game FXAA AA mode): replace with SMAA. ---
      // Mutually exclusive with the TAA/DLAA path (FXAA mode has no TAA dispatch, and vice versa).
      if (g_smaa_enable &&
          original_shader_hashes.Contains(kFXAAHash, reshade::api::shader_stage::pixel) &&
          is_immediate)
      {
         gd.fxaa_hits_this_frame++;

         ComPtr<ID3D11ShaderResourceView> srv_color;
         native_device_context->PSGetShaderResources(0, 1, srv_color.put()); // t0 = display-encoded color
         ComPtr<ID3D11RenderTargetView> rtv;
         native_device_context->OMGetRenderTargets(1, rtv.put(), nullptr); // output (separate resource)

         if (srv_color && rtv)
         {
            // Use the real RTV size (not swapchain) so SMAA RT_METRICS / sharpen CB / viewport all match the target
            // under in-game Resolution Scale. Falls back to swapchain size if the desc can't be read.
            uint32_t w = (uint32_t)device_data.output_resolution.x;
            uint32_t h = (uint32_t)device_data.output_resolution.y;
            {
               ComPtr<ID3D11Resource> rtv_res;
               rtv->GetResource(rtv_res.put());
               TryGetTex2DSize(rtv_res.get(), w, h);
            }

            // DrawSMAA sizes its edge/blend/DSV intermediates from the FIRST RTV and rebuilds them only
            // on swapchain re-init — not on a Resolution-Scale change. On a size change, drop the 3 core-managed views
            // so DrawSMAA recreates them at the new size.
            if (gd.smaa_core_w != w || gd.smaa_core_h != h)
            {
               auto& mr = device_data.managed_resources;
               mr.depth_stencil_views[CompileTimeStringHash("smaa_dsv")].reset();
               mr.render_target_views[CompileTimeStringHash("smaa_edge_detection")].reset();
               mr.render_target_views[CompileTimeStringHash("smaa_blending_weight_calculation")].reset();
#if DEVELOPMENT || TEST
               if (gd.smaa_core_w || gd.smaa_core_h) // skip the first-build transition (0 -> size); only log real resizes
               {
                  char b[160];
                  snprintf(b, sizeof(b), "MEA SMAA: resized core intermediates %ux%u -> %ux%u (Resolution-Scale change).", gd.smaa_core_w, gd.smaa_core_h, w, h);
                  reshade::log::message(reshade::log::level::info, b);
               }
#endif
               gd.smaa_core_w = w;
               gd.smaa_core_h = h;
            }

            // (Re)create the SMAA metrics CB on resolution change.
            if (!gd.cb_smaa_metrics || gd.smaa_metrics_w != w || gd.smaa_metrics_h != h)
            {
               const float metrics[4] = {1.f / (float)w, 1.f / (float)h, (float)w, (float)h};
               if (CreateImmutableCB(native_device, metrics, sizeof(metrics), gd.cb_smaa_metrics))
               {
                  gd.smaa_metrics_w = w;
                  gd.smaa_metrics_h = h;
               }
            }

            // SMAA shader-readiness gate. DrawSMAA fetches its edge/blend/
            // neighborhood shaders with map.at() and binds them; if a FXAA-mode frame fires before they finish
            // compiling (async loader / dev live-reload) the bound shaders are null → a no-op SMAA pass that still
            // returns Replaced = a blank/stale frame. If any are missing, fall through to the native FXAA instead.
            const bool smaa_shaders_ready =
               device_data.native_pixel_shaders[CompileTimeStringHash("SMAA Edge Detection PS")].get() != nullptr &&
               device_data.native_pixel_shaders[CompileTimeStringHash("SMAA Blending Weight Calculation PS")].get() != nullptr &&
               device_data.native_pixel_shaders[CompileTimeStringHash("SMAA Neighborhood Blending PS")].get() != nullptr &&
               device_data.native_vertex_shaders[CompileTimeStringHash("SMAA Edge Detection VS")].get() != nullptr &&
               device_data.native_vertex_shaders[CompileTimeStringHash("SMAA Blending Weight Calculation VS")].get() != nullptr &&
               device_data.native_vertex_shaders[CompileTimeStringHash("SMAA Neighborhood Blending VS")].get() != nullptr;

            if (gd.cb_smaa_metrics && smaa_shaders_ready)
            {
               // Bind the metrics CB at VS+PS b1 (DrawSMAA restores VS/PS/SRVs/RTs but NOT cbuffer slots).
               ComPtr<ID3D11Buffer> vs_cb1_orig, ps_cb1_orig;
               native_device_context->VSGetConstantBuffers(1, 1, vs_cb1_orig.put());
               native_device_context->PSGetConstantBuffers(1, 1, ps_cb1_orig.put());
               ID3D11Buffer* mcb = gd.cb_smaa_metrics.get();
               native_device_context->VSSetConstantBuffers(1, 1, &mcb);
               native_device_context->PSSetConstantBuffers(1, 1, &mcb);

               // MEA's FXAA input is already display-encoded → use it as both color and gamma; no predication.
               // With sharpening on, SMAA renders into a temp texture and an RCAS pass writes the final RTV.
               ID3D11RenderTargetView* smaa_target = rtv.get();
               // Only route SMAA through the temp texture if sharpening is on AND the RCAS shaders are
               // actually compiled — otherwise SMAA must write straight to the final RTV, or the image is lost.
               const bool sharpen_shaders_ready =
                  device_data.native_vertex_shaders[CompileTimeStringHash("Copy VS")].get() != nullptr &&
                  device_data.native_pixel_shaders[CompileTimeStringHash("MEA Sharpen PS")].get() != nullptr;
               bool do_sharpen = g_smaa_sharpness > 0.f && sharpen_shaders_ready;
               if (do_sharpen)
               {
                  if (!gd.tex_smaa_out || gd.smaa_out_w != w || gd.smaa_out_h != h)
                  {
                     gd.tex_smaa_out_rtv.reset();
                     gd.tex_smaa_out_srv.reset();
                     if (CreateDefaultRGBA16FTex(native_device, w, h, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, gd.tex_smaa_out))
                     {
                        native_device->CreateRenderTargetView(gd.tex_smaa_out.get(), nullptr, gd.tex_smaa_out_rtv.put());
                        native_device->CreateShaderResourceView(gd.tex_smaa_out.get(), nullptr, gd.tex_smaa_out_srv.put());
                        gd.smaa_out_w = w;
                        gd.smaa_out_h = h;
                     }
                  }
                  if (gd.tex_smaa_out_rtv && gd.tex_smaa_out_srv)
                  {
                     // (Re)create the RCAS CB up-front, BEFORE choosing smaa_target: if it fails we must fall back to
                     // SMAA-straight-to-RTV, else the final RTV is left unwritten (still Replaced) → stale/black.
                     if (!gd.cb_sharpen || gd.sharpen_w != w || gd.sharpen_h != h || gd.sharpen_amount != g_smaa_sharpness)
                     {
                        const float sp[4] = {(float)w, (float)h, g_smaa_sharpness, 0.f};
                        if (CreateImmutableCB(native_device, sp, sizeof(sp), gd.cb_sharpen))
                        {
                           gd.sharpen_w = w;
                           gd.sharpen_h = h;
                           gd.sharpen_amount = g_smaa_sharpness;
                        }
                     }
                     if (gd.cb_sharpen)
                        smaa_target = gd.tex_smaa_out_rtv.get();
                     else
                        do_sharpen = false;
                  }
                  else
                     do_sharpen = false;
               }

               DrawSMAA(native_device, native_device_context, device_data,
                  smaa_target, srv_color.get(), srv_color.get(), nullptr);

               if (do_sharpen)
               {
                  // cb_sharpen + both RCAS shaders are guaranteed non-null here: the do_sharpen gate above is set
                  // false if any is missing (SMAA then renders straight to the final RTV), so no re-guard.
                  auto* sharpen_vs = device_data.native_vertex_shaders[CompileTimeStringHash("Copy VS")].get();
                  auto* sharpen_ps = device_data.native_pixel_shaders[CompileTimeStringHash("MEA Sharpen PS")].get();
                  // DrawCustomPixelShader does NOT restore state → wrap in core's DrawStateStack<FullGraphics>
                  // (caches/restores all PS SRV/CB/sampler slots, IA, RS, scissors, viewport, blend, DS).
                  DrawStateStack<DrawStateStackType::FullGraphics> sharpen_state;
                  sharpen_state.Cache(native_device_context, device_data.uav_max_count);

                  ID3D11Buffer* scb = gd.cb_sharpen.get();
                  native_device_context->PSSetConstantBuffers(0, 1, &scb);
                  DrawCustomPixelShader(native_device_context, device_data.default_depth_stencil_state.get(), device_data.default_blend_state.get(), nullptr,
                     sharpen_vs, sharpen_ps, gd.tex_smaa_out_srv.get(), rtv.get(), w, h, false);

                  sharpen_state.Restore(native_device_context);
               }

               ID3D11Buffer* vcb = vs_cb1_orig.get();
               ID3D11Buffer* pcb = ps_cb1_orig.get();
               native_device_context->VSSetConstantBuffers(1, 1, &vcb);
               native_device_context->PSSetConstantBuffers(1, 1, &pcb);

               device_data.has_drawn_main_post_processing = true;
               return DrawOrDispatchOverrideType::Replaced; // cancel native FXAA
            }
         }
      }
#endif // ENABLE_SMAA

#if DEVELOPMENT || TEST
      // Outlier self-diagnostic: if the game dispatches a resolve-like compute (writes u2+u3, reads a
      // motion-vector-like t1) whose hash is NOT in kTAAResolveHashes, log it once. Surfaces a TAA-resolve perm
      // we don't yet hook (a feature variant, or a new GPU tile-size) so it can be verified + added — see the
      // perm-matrix note at kTAAResolveHashes.
      {
         const uint32_t outlier_cs = FirstComputeHash(original_shader_hashes);
         if (outlier_cs != 0 && !IsTAAResolve(original_shader_hashes))
         {
            ComPtr<ID3D11UnorderedAccessView> o_u2, o_u3;
            native_device_context->CSGetUnorderedAccessViews(2, 1, o_u2.put());
            native_device_context->CSGetUnorderedAccessViews(3, 1, o_u3.put());
            ComPtr<ID3D11ShaderResourceView> o_t1;
            native_device_context->CSGetShaderResources(1, 1, o_t1.put());
            if (o_u2 && o_u3 && o_t1) // TAA-resolve fingerprint: two ping-pong UAV outputs + MV-like t1 input
            {
               bool fresh;
               {
                  const std::lock_guard<std::mutex> lock(gd.diag_resolve_mutex);
                  fresh = gd.diag_resolve_hashes.insert(outlier_cs).second;
               }
               if (fresh)
               {
                  char b[176];
                  snprintf(b, sizeof(b), "MEA DIAG: unhooked resolve-like CS 0x%08X (u2+u3+t1 bound) — candidate TAA-resolve perm missing from the hooked set", outlier_cs);
                  reshade::log::message(reshade::log::level::warning, b);
               }
            }
         }
      }
#endif

      // --- TAA resolve dispatch: replace with DLSS. ---
      if (IsTAAResolve(original_shader_hashes))
      {
         // DOF variant: the native dispatch is manually re-issued below (its u4/u5 CoC outputs feed the DOF
         // chain); once that happened, every exit from this block must return Replaced, never None — the
         // fall-through dispatch would run the resolve a second time.
         const bool dof_variant = IsTAAResolveDof(original_shader_hashes);
         bool native_ran = false;
         gd.taa_hits_this_frame++;
         gd.taa_hits_total++;
         device_data.taa_detected = true; // game's TAA pass present this frame (feeds core's SR-engaged ✓ indicator)
#if DEVELOPMENT || TEST
         if (!gd.logged_first_hit)
         {
            gd.logged_first_hit = true;
            const uint32_t matched_cs = FirstComputeHash(original_shader_hashes);
            char hit_msg[128];
            snprintf(hit_msg, sizeof(hit_msg), "MEA: TAA resolve compute 0x%08X detected — SR injection point reached (perm-matrix hook).", matched_cs);
            reshade::log::message(reshade::log::level::info, hit_msg);
         }
#endif

#if ENABLE_SR
         if (device_data.sr_type != SR::Type::None && is_immediate)
         {
            auto* sr_instance_data = device_data.GetSRInstanceData();
            if (sr_instance_data)
            {
               // Capture the TAA's CS bindings FIRST — we need the real input texture size (and the camera CB
               // keyed to it) before configuring SR or probing the camera.
               ComPtr<ID3D11ShaderResourceView> srv_depth, srv_mvs, srv_color;
               native_device_context->CSGetShaderResources(0, 1, srv_depth.put()); // t0
               native_device_context->CSGetShaderResources(1, 1, srv_mvs.put());   // t1
               native_device_context->CSGetShaderResources(3, 1, srv_color.put()); // t3
               ComPtr<ID3D11UnorderedAccessView> uav_resolved, uav_history;
               native_device_context->CSGetUnorderedAccessViews(2, 1, uav_resolved.put()); // u2
               native_device_context->CSGetUnorderedAccessViews(3, 1, uav_history.put());  // u3

#if DEVELOPMENT || TEST
               if (!gd.logged_capture_fail && !(srv_depth && srv_mvs && srv_color && uav_resolved))
               {
                  gd.logged_capture_fail = true;
                  char b[160];
                  snprintf(b, sizeof(b), "MEA SR capture FAIL: depth=%d mvs=%d color=%d u2=%d u3=%d",
                     (int)(bool)srv_depth, (int)(bool)srv_mvs, (int)(bool)srv_color, (int)(bool)uav_resolved, (int)(bool)uav_history);
                  reshade::log::message(reshade::log::level::warning, b);
               }
#endif
               if (srv_depth && srv_mvs && srv_color && uav_resolved)
               {
                  ComPtr<ID3D11Resource> res_depth, res_mvs, res_color, res_u2, res_u3;
                  srv_depth->GetResource(res_depth.put());
                  srv_mvs->GetResource(res_mvs.put());
                  srv_color->GetResource(res_color.put());
                  uav_resolved->GetResource(res_u2.put());
                  if (uav_history)
                     uav_history->GetResource(res_u3.put());

                  // Derive the REAL render res from the t3 scene-color texture, not core's swapchain-sized
                  // render_resolution — under Resolution Scale they diverge and a wrong size breaks AA (camera-CB
                  // res-key mismatch + corner-only u2 copy). DLAA keeps render==output.
                  uint32_t scene_w = (uint32_t)device_data.output_resolution.x;
                  uint32_t scene_h = (uint32_t)device_data.output_resolution.y;
                  TryGetTex2DSize(res_color.get(), scene_w, scene_h);
                  const uint32_t out_w = scene_w, out_h = scene_h;
                  const float rw = (float)scene_w, rh = (float)scene_h;
                  const float2 actual_render_res((float)scene_w, (float)scene_h);

                  // Camera was normally captured at the gbuffer VS this frame. Only fall back to the (fragile)
                  // bound-CB probe if that missed (e.g. a frame with no main gbuffer pass).
                  if (!gd.cam_valid_this_frame)
                  {
                     if (ID3D11DeviceContext1* ctx1 = GetImmediateCtx1(gd, native_device_context))
                     {
                        SelectCameraCPU(ctx1, gd, actual_render_res);
                     }
                  }
                  if (!gd.cam_valid_this_frame)
                  {
                     // No camera this frame -> no jitter info. Running SR with jitter=0 while the scene IS
                     // jittered leaves the jitter uncompensated -> visible per-frame camera shake (menus /
                     // dialogue frames without a main gbuffer pass). Bail to the native resolve — it
                     // de-jitters itself. Must stay BEFORE the DOF-variant manual dispatch below, so None
                     // still means "the native dispatch runs exactly once, by the fall-through".
#if DEVELOPMENT || TEST
                     if (!gd.logged_probe_miss)
                     {
                        gd.logged_probe_miss = true;
                        reshade::log::message(reshade::log::level::info,
                           "MEA: no camera at hooked resolve (gbuffer capture + wide probe both missed) — native TAA for these frames.");
                     }
#endif
                     device_data.force_reset_sr = true; // don't blend across the gap when SR re-engages
                     return DrawOrDispatchOverrideType::None;
                  }

                  // Pick the u2 hand-off mode BEFORE running SR (re-evaluated when the resolve target changes).
                  // Preferred path is the copy CS: its typed store converts formats — the game's "Buffer Format"
                  // setting swaps u2/u3 between rgba16f and r11g11b10_float, where a plain CopySubresourceRegion
                  // silently no-ops → black scene with live UI — and u3 gets the native enc*512 history layout.
                  // Fallbacks: rgba16f → raw direct copy (CS missing); otherwise bail to native TAA (SR off
                  // beats a black screen).
                  // Evaluated every hooked dispatch (GetDesc is a cached-CPU-struct read): the transient pool can
                  // recycle a pointer for a differently-formatted texture, so a pointer key would go stale.
                  {
                     HandoffMode mode = HandoffMode::Incompatible;
                     D3D11_TEXTURE2D_DESC ud = {};
                     ComPtr<ID3D11Texture2D> u2_tex;
                     if (SUCCEEDED(res_u2->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(u2_tex.put()))))
                     {
                        u2_tex->GetDesc(&ud);
                        const bool dims_ok = ud.Width == out_w && ud.Height == out_h;
                        const bool fmt_rgba16f = ud.Format == DXGI_FORMAT_R16G16B16A16_FLOAT || ud.Format == DXGI_FORMAT_R16G16B16A16_TYPELESS;
                        const bool cs_ready =
                           device_data.native_compute_shaders[CompileTimeStringHash("MEA SR Output Copy CS")].get() != nullptr &&
                           device_data.native_compute_shaders[CompileTimeStringHash("MEA SR History Copy CS")].get() != nullptr;
                        if (dims_ok && cs_ready)
                           mode = HandoffMode::CsCopy;
                        else if (dims_ok && fmt_rgba16f)
                           mode = HandoffMode::DirectCopy; // copy CS unavailable — a raw copy still beats no SR at all
                     }
                     if (mode != gd.u2_mode || !gd.logged_u2_mode)
                     {
                        gd.logged_u2_mode = true;
                        char b[192];
                        snprintf(b, sizeof(b), "MEA SR hand-off: u2 fmt=%d %ux%u mips=%u vs SR out rgba16f %ux%u -> mode=%s",
                           (int)ud.Format, ud.Width, ud.Height, ud.MipLevels, out_w, out_h,
                           mode == HandoffMode::DirectCopy ? "direct-copy" : (mode == HandoffMode::CsCopy ? "cs-copy" : "INCOMPATIBLE (native TAA)"));
                        reshade::log::message(mode == HandoffMode::Incompatible ? reshade::log::level::warning : reshade::log::level::info, b);
                     }
                     gd.u2_mode = mode;
                  }
                  if (gd.u2_mode == HandoffMode::Incompatible)
                  {
                     // No safe hand-off — let the native resolve run (vanilla TAA) rather than black-screen.
                     device_data.force_reset_sr = true; // don't blend across the gap if SR later recovers
                     return DrawOrDispatchOverrideType::None;
                  }

                  if (dof_variant)
                  {
                     if (original_draw_dispatch_func == nullptr)
                     {
                        device_data.force_reset_sr = true;
                        return DrawOrDispatchOverrideType::None; // can't re-issue manually — let the native dispatch run itself
                     }
                     // Run the game's own resolve first: it writes u0 (mask) + u4/u5 (temporally filtered DOF
                     // CoC) with native math — zero quality loss. Its u2/u3 color output is overwritten by our
                     // SR copy below; the SR inputs (t0/t1/t3) are not mutated by it and stay bound across the
                     // dispatch.
                     (*original_draw_dispatch_func)();
                     native_ran = true;
#if DEVELOPMENT || TEST
                     if (!gd.logged_dof_hit)
                     {
                        gd.logged_dof_hit = true;
                        const uint32_t matched_cs = FirstComputeHash(original_shader_hashes);
                        char b[160];
                        snprintf(b, sizeof(b), "MEA: DOF-variant resolve 0x%08X — run-native-then-override (u4/u5 CoC preserved).", matched_cs);
                        reshade::log::message(reshade::log::level::info, b);
                     }
#endif
                  }

                  SR::SettingsData settings_data;
                  settings_data.output_width = out_w;
                  settings_data.output_height = out_h;
                  settings_data.render_width = (uint32_t)rw;
                  settings_data.render_height = (uint32_t)rh;
                  settings_data.dynamic_resolution = false;
                  settings_data.hdr = true;            // MEA scene color = linear HDR
                  settings_data.inverted_depth = true; // reverse-Z
                  settings_data.mvs_jittered = gd.mvs_jittered;
                  settings_data.mvs_x_scale = (gd.mvs_flip_x ? -1.f : 1.f) * gd.mvs_scale_mult * 0.5f * rw;
                  settings_data.mvs_y_scale = (gd.mvs_flip_y ? 1.f : -1.f) * gd.mvs_scale_mult * 0.5f * rh;
                  settings_data.auto_exposure = gd.auto_exposure;
                  settings_data.render_preset = dlss_render_preset;
                  sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, settings_data);

                  // (Re)create the DLSS output texture at output resolution.
                  const bool res_changed = (gd.dlss_out_w != out_w || gd.dlss_out_h != out_h);
                  if (!gd.tex_dlss_output || res_changed)
                  {
                     gd.srv_dlss_output.reset();
                     if (CreateDefaultRGBA16FTex(native_device, out_w, out_h, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, gd.tex_dlss_output))
                     {
                        if (SUCCEEDED(native_device->CreateShaderResourceView(gd.tex_dlss_output.get(), nullptr, gd.srv_dlss_output.put())))
                        {
                           gd.dlss_out_w = out_w;
                           gd.dlss_out_h = out_h;
                        }
                        else
                        {
                           gd.tex_dlss_output.reset(); // keep tex+SRV atomic — the CS hand-off needs both
                        }
                     }
                  }
                  if (gd.tex_dlss_output)
                  {
                     // Reset history only on genuine discontinuities, not gradual FOV changes — resetting every ramp
                     // frame starves the upscaler's accumulation; smooth FOV is left to its own history rejection.
                     const bool reset = device_data.force_reset_sr || gd.first_dlss_frame || res_changed;
                     device_data.force_reset_sr = false;

                     SR::SuperResolutionImpl::DrawData draw_data;
                     draw_data.source_color = res_color.get();
                     draw_data.output_color = gd.tex_dlss_output.get();
                     draw_data.motion_vectors = res_mvs.get();
                     draw_data.depth_buffer = res_depth.get();
                     draw_data.render_width = (uint32_t)rw;
                     draw_data.render_height = (uint32_t)rh;
                     draw_data.reset = reset;
                     draw_data.near_plane = gd.cam_near;
                     draw_data.far_plane = kCamFar;
                     // FSR consumes vert FOV (DLSS ignores) and HARD-ASSERTS on ≤0 → fall back to ~60° when m11==0
                     // (first SR frames, pre-capture; that frame is a reset anyway).
                     draw_data.vert_fov = gd.cam_proj_m11 > 0.f ? 2.f * atanf(1.f / gd.cam_proj_m11) : 1.047f;
                     // Jitter is guaranteed fresh here — the capture-miss case bailed to the native resolve above.
                     draw_data.jitter_x = (gd.jitter_flip_x ? 1.f : -1.f) * gd.cam_jitter_clip_x * 0.5f * rw;
                     draw_data.jitter_y = (gd.jitter_flip_y ? 1.f : -1.f) * gd.cam_jitter_clip_y * 0.5f * rh;
                     draw_data.frame_index = cb_luma_global_settings.FrameIndex;
                     draw_data.user_sharpness = gd.sharpness;
                     draw_data.bias_mask = nullptr; // no usable reactivity/bias source in MEA (see note at top)

                     const bool ok = sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context, draw_data);
#if DEVELOPMENT || TEST
                     if (!gd.logged_draw_result)
                     {
                        gd.logged_draw_result = true;
                        char b[200];
                        snprintf(b, sizeof(b), "MEA DLSS Draw ok=%d reset=%d sceneRes=%ux%u (swapchain=%.0fx%.0f) jitterPx=%.3f,%.3f mvScale=%.1f,%.1f near=%.4f",
                           (int)ok, (int)reset, out_w, out_h, device_data.output_resolution.x, device_data.output_resolution.y,
                           draw_data.jitter_x, draw_data.jitter_y,
                           settings_data.mvs_x_scale, settings_data.mvs_y_scale, draw_data.near_plane);
                        reshade::log::message(reshade::log::level::info, b);
                     }
#endif
                     if (ok)
                     {
                        if (gd.u2_mode == HandoffMode::CsCopy && gd.srv_dlss_output)
                        {
                           // Hand-off CS (preferred path): writes through the game's own typed UAVs (u2/u3 are
                           // r11g11b10_float under the alternate "Buffer Format"; the typed store converts, and
                           // writability is proven by the native resolve storing through these very views).
                           // Both CS are guaranteed by the u2_mode gate above.
                           auto* copy_cs = device_data.native_compute_shaders[CompileTimeStringHash("MEA SR Output Copy CS")].get();
                           DrawStateStack<DrawStateStackType::Compute> st;
                           st.Cache(native_device_context, device_data.uav_max_count);
                           native_device_context->CSSetShader(copy_cs, nullptr, 0);
                           // UAV first, then SRV (an SRV whose resource is still UAV-bound gets silently NULLed). Null the
                           // FULL range: besides the game's own u2/u3 bindings, the SR backend (NGX/FFX doesn't restore
                           // compute state) may have left tex_dlss_output on any UAV slot, which would silently null our
                           // t0 SRV of it below.
                           ID3D11UnorderedAccessView* null_uavs[D3D11_1_UAV_SLOT_COUNT] = {};
                           native_device_context->CSSetUnorderedAccessViews(0, device_data.uav_max_count, null_uavs, nullptr);
                           ID3D11UnorderedAccessView* uav = uav_resolved.get();
                           native_device_context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
                           ID3D11ShaderResourceView* srv = gd.srv_dlss_output.get();
                           native_device_context->CSSetShaderResources(0, 1, &srv);
                           native_device_context->Dispatch((out_w + 7) / 8, (out_h + 7) / 8, 1);
                           if (uav_history)
                           {
                              // u3 goes through the history variant: the native resolve stores history as
                              // encoded-domain RGB x512, not linear — see copy_color_history_cs.
                              auto* hist_cs = device_data.native_compute_shaders[CompileTimeStringHash("MEA SR History Copy CS")].get();
                              native_device_context->CSSetShader(hist_cs, nullptr, 0);
                              ID3D11UnorderedAccessView* uav_h = uav_history.get();
                              native_device_context->CSSetUnorderedAccessViews(0, 1, &uav_h, nullptr);
                              native_device_context->Dispatch((out_w + 7) / 8, (out_h + 7) / 8, 1);
                           }
                           st.Restore(native_device_context);
                        }
                        else
                        {
                           // Copy mip0 via CopySubresourceRegion, NOT CopyResource (u2 can be a 12-mip bloom-source at
                           // Motion Blur OFF; CopyResource needs identical mip count → silent fail → black).
                           native_device_context->CopySubresourceRegion(res_u2.get(), 0, 0, 0, 0, gd.tex_dlss_output.get(), 0, nullptr);
                           if (res_u3)
                              native_device_context->CopySubresourceRegion(res_u3.get(), 0, 0, 0, 0, gd.tex_dlss_output.get(), 0, nullptr);
                        }

                        gd.first_dlss_frame = false;
                        gd.dlss_ran_this_frame = true;
                        device_data.has_drawn_sr = true; // feeds core's SR-engaged ✓ indicator (copied to has_drawn_sr_imgui)
                        device_data.has_drawn_main_post_processing = true;
                        return DrawOrDispatchOverrideType::Replaced; // cancel native TAA
                     }
                     else
                     {
                        // Draw() failed → native TAA runs this frame (advances the game's own history);
                        // re-arm force_reset_sr so SR recovery doesn't blend across the gap → ghosting.
                        device_data.force_reset_sr = true;
#if DEVELOPMENT || TEST
                        if (!gd.logged_draw_fail)
                        {
                           gd.logged_draw_fail = true;
                           reshade::log::message(reshade::log::level::warning,
                              "MEA: SR Draw FAIL — native TAA this frame, force_reset_sr re-armed; history resets on SR recovery.");
                        }
#endif
                     }
                  }
               }
            }
         }
#endif // ENABLE_SR
         if (native_ran)
            return DrawOrDispatchOverrideType::Replaced; // native already issued manually — don't dispatch twice
         // Fall through (SR off / not ready): let the native TAA run.
      }

      return DrawOrDispatchOverrideType::None;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& gd = GetGameDeviceData(device_data);

      // Texture mip LOD bias for the active AA mode: DLAA wants a negative bias (=-1 at native via GetMipLODBias)
      // to recover detail under jitter; FXAA/SMAA wants 0. Skipped while the user overrides it. Core (re)creates
      // the biased sampler lazily on the next descriptor bind — no manual pre-warm.
      if (enable_samplers_upgrade && !custom_texture_mip_lod_bias_offset)
      {
         // Hold s_mutex_samplers EXCLUSIVELY while writing the offset (core reads it as a std::map key under
         // shared_lock on Frostbite worker threads and writes it under unique_lock; a shared_lock here would
         // race those). Also skip the DLSS bias while SR is suppressed (loading/menus) so we don't over-sharpen
         // those frames.
         std::unique_lock lock_samplers(s_mutex_samplers);
         const bool dlaa_ran = gd.dlss_ran_this_frame && !device_data.sr_suppressed; // precise: DLSS actually drew this frame
         device_data.texture_mip_lod_bias_offset = dlaa_ran
                                                      ? SR::GetMipLODBias(device_data.render_resolution.y, device_data.output_resolution.y) // -1 at native, more negative when upscaling
                                                      : (gd.taa_hits_this_frame > 0 ? -1.f : 0.f);                                          // game TAA w/o DLSS -> -1; FXAA/SMAA -> 0
      }

#if DEVELOPMENT || TEST
      // One situational state line — re-printed only when something the user can toggle changes.
      {
         MassEffectAndromedaGameDeviceData::LogState cur;
         cur.aa_mode = gd.taa_hits_this_frame > 0 ? 0 : (gd.fxaa_seen_this_frame > 0 ? 1 : -1);
         cur.dlaa = gd.dlss_ran_this_frame ? 1 : 0;
         cur.smaa_replaced = gd.fxaa_hits_this_frame > 0 ? 1 : 0;
         cur.sharpen = (g_smaa_sharpness > 0.f) ? 1 : 0;
         cur.sr_supp = device_data.sr_suppressed ? 1 : 0;
         cur.sr_type = (int)device_data.sr_type;
         cur.smaa_en = g_smaa_enable ? 1 : 0;
         cur.cam_valid = gd.cam_valid_this_frame ? 1 : 0;
         cur.bias = device_data.texture_mip_lod_bias_offset;

         if (cur.DiffersFrom(gd.log_state))
         {
            gd.log_state = cur;

            const char* aa = cur.aa_mode == 0 ? "TAA" : (cur.aa_mode == 1 ? "FXAA" : "none(Off/cutscene)");
            // Why DLAA isn't running (only meaningful in TAA mode; in FXAA/none mode there's no TAA to hook):
            const char* reason =
               cur.aa_mode != 0 ? "no-TAA-pass" : cur.sr_type == (int)SR::Type::None ? "sr=None"
                                               : cur.sr_supp                         ? "sr-suppressed"
                                               : !cur.dlaa                           ? "capture/draw-fail"
                                                                                     : "ok";
            char b[256];
            snprintf(b, sizeof(b),
               "MEA[f=%llu] aa=%s dlaa_active=%d smaa_replaced=%d sharpen=%d | sr=%d supp=%d smaa_en=%d camValid=%d bias=%.2f reason=%s",
               (unsigned long long)cb_luma_global_settings.FrameIndex, aa, cur.dlaa, cur.smaa_replaced, cur.sharpen,
               cur.sr_type, cur.sr_supp, cur.smaa_en, cur.cam_valid, cur.bias, reason);
            reshade::log::message(reshade::log::level::info, b);
         }
      }
      // AF verification (one-shot): dump the descriptors of the custom samplers core created from
      // samplers_upgrade_mode=4 (it upgrades the game's ANISOTROPIC samplers → MaxAnisotropy=16 + our LOD bias).
      // Expect filter=85 (D3D11_FILTER_ANISOTROPIC), maxAniso=16. Empty map = no anisotropic samplers got upgraded.
      if (!gd.logged_samplers)
      {
         std::shared_lock sl(s_mutex_samplers);
         if (!device_data.custom_sampler_by_original_sampler.empty())
         {
            gd.logged_samplers = true;
            int total = 0, shown = 0;
            for (const auto& by_orig : device_data.custom_sampler_by_original_sampler)
               for (const auto& by_bias : by_orig.second)
               {
                  ++total;
                  if (by_bias.second && shown < 4)
                  {
                     ++shown;
                     D3D11_SAMPLER_DESC d = {};
                     by_bias.second->GetDesc(&d);
                     char sb[176];
                     snprintf(sb, sizeof(sb), "MEA AF: custom sampler filter=%d maxAniso=%u mipBias=%.2f minLOD=%.1f (offset key=%.2f)",
                        (int)d.Filter, d.MaxAnisotropy, d.MipLODBias, d.MinLOD, by_bias.first);
                     reshade::log::message(reshade::log::level::info, sb);
                  }
               }
            char hb[96];
            snprintf(hb, sizeof(hb), "MEA AF: %d custom (upgraded) sampler(s) active — filter=85=ANISOTROPIC, maxAniso=16 means AF working", total);
            reshade::log::message(reshade::log::level::info, hb);
         }
      }
#endif // DEVELOPMENT || TEST

      gd.taa_hits_this_frame = 0;
      gd.cam_valid_this_frame = false;
      gd.fxaa_hits_this_frame = 0;
      gd.fxaa_seen_this_frame = 0;
      gd.dlss_ran_this_frame = false;
      // has_drawn_sr is per-frame (core snapshots it before this). taa_detected is LATCHED (never reset here):
      // the overlay reads it directly, and the ✓ is gated by the per-frame has_drawn_sr_imgui anyway.
#if ENABLE_SR
      device_data.has_drawn_sr = false;
#endif
      {
         std::lock_guard<std::mutex> lock(gd.map_recs_mutex);
         gd.map_rec_count = 0;
      } // re-capture ring ptr next frame; guarded vs worker-thread maps
   }

   // Persist the user settings across launches (ReShade config section NAME="Luma"). Called on boot.
   void LoadConfigs() override
   {
      reshade::get_config_value(nullptr, NAME, "SMAAEnable", g_smaa_enable);
      reshade::get_config_value(nullptr, NAME, "SMAASharpness", g_smaa_sharpness);
   }

   // User-facing settings (all configs). Minimal + tooltips + persistence. The DLSS quality preset is selectable
   // in core's "Super Resolution" section above.
   void DrawImGuiSettings(DeviceData& device_data) override
   {
      ImGui::SeparatorText("Anti-Aliasing");
      if (ImGui::Checkbox("SMAA Enable", &g_smaa_enable))
         reshade::set_config_value(nullptr, NAME, "SMAAEnable", g_smaa_enable);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Replaces the game's FXAA with SMAA (only active when in-game Anti-Aliasing is set to FXAA).");
      ImGui::BeginDisabled(!g_smaa_enable);
      ImGui::SliderFloat("RCAS Sharpness", &g_smaa_sharpness, 0.f, 1.f); // updates live; persist on release
      if (ImGui::IsItemDeactivatedAfterEdit())
         reshade::set_config_value(nullptr, NAME, "SMAASharpness", g_smaa_sharpness);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         ImGui::SetTooltip("Sharpening applied on top of SMAA (0 = off).");
      ImGui::EndDisabled();
   }

#if DEVELOPMENT || TEST
   // Runtime diagnostics (Info tab). Read-only per-frame state.
   void PrintImGuiInfo(const DeviceData& device_data) override
   {
      auto& d = *static_cast<MassEffectAndromedaGameDeviceData*>(device_data.game);
      const char* aa = d.taa_hits_this_frame > 0 ? "TAA" : (d.fxaa_seen_this_frame > 0 ? "FXAA" : "none (Off/cutscene)");
      ImGui::Text("AA pass this frame: %s", aa);
      ImGui::Text("SR (DLSS/FSR) active: %d | SMAA replaced FXAA: %d", (int)d.dlss_ran_this_frame, (int)(d.fxaa_hits_this_frame > 0));
      ImGui::Text("Render: %.0fx%.0f  Output: %.0fx%.0f", device_data.render_resolution.x, device_data.render_resolution.y, device_data.output_resolution.x, device_data.output_resolution.y);
      ImGui::Text("Camera: near=%.4f jitterClip=%.6f,%.6f valid=%d", d.cam_near, d.cam_jitter_clip_x, d.cam_jitter_clip_y, (int)d.cam_valid_this_frame);
      ImGui::Text("TAA hits: frame=%u total=%llu | FXAA->SMAA hits/frame=%u", d.taa_hits_this_frame, (unsigned long long)d.taa_hits_total, d.fxaa_hits_this_frame);
   }
#endif

#if DEVELOPMENT
   // Dev-only tuning knobs (no persistence — transient). Diagnostics moved to PrintImGuiInfo; user toggles to DrawImGuiSettings.
   void DrawImGuiDevSettings(DeviceData& device_data) override
   {
      auto& d = GetGameDeviceData(device_data);
      ImGui::SeparatorText("Motion vectors");
      ImGui::Checkbox("MV flip X", &d.mvs_flip_x);
      ImGui::Checkbox("MV flip Y", &d.mvs_flip_y);
      ImGui::SliderFloat("MV scale mult", &d.mvs_scale_mult, 0.25f, 4.0f);
      ImGui::Checkbox("MVs already jittered", &d.mvs_jittered);

      ImGui::SeparatorText("Jitter");
      ImGui::Checkbox("Jitter flip X", &d.jitter_flip_x);
      ImGui::Checkbox("Jitter flip Y", &d.jitter_flip_y);

      ImGui::SeparatorText("DLSS");
      ImGui::SliderFloat("Sharpness (inert: DoSharpening flag not set)", &d.sharpness, -1.0f, 1.0f);
      ImGui::Checkbox("Auto exposure", &d.auto_exposure);
   }
#endif // DEVELOPMENT

   void PrintImGuiAbout() override
   {
      ImGui::PushTextWrapPos(0.f);
      ImGui::Text(
         "Luma for \"Mass Effect: Andromeda\" is developed by DristoforColumb and is open source and free.\n"
         "It replaces the game's TAA with DLSS or FSR (Native AA) and the game's FXAA with SMAA, plus 16x "
         "anisotropic filtering.\n"
         "Set in-game Anti-Aliasing to TAA for DLSS/FSR, or to FXAA for SMAA.\n"
         "Thanks to the Luma team and contributors.");
      ImGui::PopTextWrapPos();

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
                  "\nSMAA (Iryoku)"
                  "\nAMD FidelityFX (RCAS + FSR Native AA)"
                  "\nNVIDIA NGX (DLSS)",
         "");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Mass Effect: Andromeda - Luma DLAA mod");
      Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::Finished;
      Globals::VERSION = 2;

      // DLAA-only: leave HDR / swapchain / texture-format handling alone.
      swapchain_format_upgrade_type = TextureFormatUpgradesType::None;
      swapchain_upgrade_type = SwapchainUpgradeType::None;
      texture_format_upgrades_type = TextureFormatUpgradesType::None;

      // DLAA-only must NOT touch the display: core's per-present display-composition gamma pass otherwise darkens
      // the backbuffer even with no upgrades.
      force_disable_display_composition = true;

      // Sharper textures: AF16x + negative mip LOD bias (mode 4 = AF16x + additive bias). The bias value is set
      // per-frame by the active AA mode in OnPresent.
      enable_samplers_upgrade = true; // boot-time only
      samplers_upgrade_mode = 4;

      // Default DLSS preset: J (Transformer v1, FP16, log-domain internals). On current DLLs "Default"
      // resolves to L/M — Transformer v2, FP8, inferring directly in linear space — which hue-crushes
      // MEA's 40k+ single-channel emissive peaks into green garbage frames (J/K render them clean; L is
      // meant for Ultra Performance upscaling anyway, not DLAA). Only a boot default: the user's saved
      // "DLSSRenderPreset" config is loaded after this and overrides it.
      dlss_render_preset = 10; // NVSDK_NGX_DLSS_Hint_Render_Preset_J

      game = new MassEffectAndromeda();
   }
   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
      // We registered these in OnInit; unregister so a reload doesn't double-register / dangle.
      reshade::unregister_event<reshade::addon_event::map_buffer_region>(MassEffectAndromeda::OnMapBufferRegion);
      reshade::unregister_event<reshade::addon_event::destroy_resource>(MassEffectAndromeda::OnDestroyResource);
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}
