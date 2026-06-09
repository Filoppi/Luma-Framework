# Quantum Break Upscaling Notes

## Scope

These notes describe the current Quantum Break SR/DLSS integration in Luma.

Relevant files:

- `Source/Games/Quantum Break/main.cpp`
- `Source/Games/Quantum Break/Upscaling.hpp`
- `Source/Games/Quantum Break/Quantum Break.vcxproj`
- `Shaders/Quantum Break/Luma_QB_PreSRDecode.hlsl`
- `Shaders/Quantum Break/temporal_resolve_0x99274617.ps_5_0.hlsl`
- `Shaders/Quantum Break/unused/history_reprojection_clamp_0xE8337D48.cs_5_0.hlsl`
- `Shaders/Quantum Break/Includes/CBuffer_cb_update_1.hlsli`
- `Shaders/Quantum Break/Includes/quantum_break_common.hlsli`

Quantum Break enables:

```cpp
#define ENABLE_NGX 1
#define ENABLE_FIDELITY_SK 1
#define ENABLE_POST_DRAW_DISPATCH_CALLBACK 1
```

The post draw/dispatch callback is required because Luma temporarily replaces the temporal resolve color SRV with the SR result, executes the game's original resolve draw, then restores the original binding.

The Visual Studio project copies the built addon and, for symbol-producing configurations, the matching PDB to `LUMA_QUANTUM_BREAK_BIN_PATH`. That keeps the deployed addon debuggable when Quantum Break prompts for manual debugger attach.

## Hook Points

Shader hashes:

```cpp
DepthLinearizationHashes().pixel_shaders.emplace(hash_depth_linearization);
HistoryReprojectionHashes().compute_shaders.emplace(hash_history_reprojection);
TemporalResolveHashes().pixel_shaders.emplace(hash_temporal_resolve);
```

Development builds also force shader names into Luma's debugger:

- `0xA43343D6`: `QB Depth Linearization (Clip Depth -> Linear Depth)`
- `0xE8337D48`: `QB History Reprojection (Motion Vectors)`
- `0x99274617`: `QB Temporal Resolve / TAA`
- `0x037FCE62`: `QB MSAA GBuffer Reconstruction Mask`
- `0x84DD3C0C`: `QB MSAA GBuffer Depth Resolve`

Depth linearization pass:

- Pixel shader hash `A43343D6`.
- Suggested name: `QB Depth Linearization (Clip Depth -> Linear Depth)`.
- Captures clip/device depth from `PS SRV slot 0`.
- The shader writes linear depth to `OM RTV slot 0`, but Luma deliberately does **not** use that output for SR.
- The pass can run at full, half, and quarter resolution. Luma keeps the largest captured `PS SRV 0` resource each frame so downscaled linear-depth passes cannot replace main scene depth.

History reprojection pass:

- Compute shader hash `E8337D48`.
- Suggested name: `QB History Reprojection (Motion Vectors)`.
- Marks TAA detected.
- Captures motion vectors from `CS SRV slot 0`.
- Stores the resource in `game_device_data.upscaling.sr_motion_vectors`.

Temporal resolve pass:

- Pixel shader hash `99274617`.
- Suggested name: `QB Temporal Resolve / TAA`.
- Main SR insertion point.
- Uses cached clip/device depth from the `A43343D6` depth-linearization input when it matches source color and motion-vector size.
- Falls back to temporal resolve `PS SRV slot 0`, named `g_tClipDepth` in the shader dump, if the explicit depth cache is missing or mismatched.
- Captures source color from `PS SRV slot 2`.
- Captures final resolve RTV from `OM RTV slot 0`.
- Reads only the SR-required values from `cb_update_1` and the temporal resolve `ssaa` cbuffer through staging readback.
- Runs SR before executing the original temporal resolve draw.

MSAA-specific depth path:

- `0x037FCE62`: MSAA GBuffer/depth reconstruction mask, reads `Texture2DMS<float> g_tDepthMS`.
- `0x84DD3C0C`: MSAA GBuffer/depth resolve, reads `Texture2DMS<float> g_tDepthMS` and writes `SV_Depth`.
- These disappear when the in-game AA option disables MSAA.
- They are not used as SR depth inputs because they are the MSAA path, not the stable single-sample clip-depth resource used by temporal resolve.

## SR Input Map

Every SR input is taken from a specific QB pass/binding:

| SR input | Source pass | Binding | Resource meaning |
|---|---|---|---|
| Source color | `0x99274617` Temporal Resolve | `PS SRV2` / `g_tColor` | Current temporal color at render resolution, still in QB gamma-space before Luma decodes it. |
| Depth | `0xA43343D6` Depth Linearization | `PS SRV0` | Pre-linearized clip/device depth, viewed as `r32_float` on an `r32_typeless` resource. This is the preferred SR depth. |
| Depth fallback | `0x99274617` Temporal Resolve | `PS SRV0` / `g_tClipDepth` | Same kind of clip-depth resource, used only if the explicit `0xA43343D6` cache is missing or size-mismatched. |
| Motion vectors | `0xE8337D48` History Reprojection | `CS SRV0` / `g_tGeometryVelocityTexture` | Geometry velocity texture, observed as `r16g16_float` at render resolution. |
| Output color | `0x99274617` Temporal Resolve | `OM RTV0` | Final temporal resolve target, normally output/upscaled resolution. |
| Main camera constants | `0x99274617` Temporal Resolve | `PS CB0` / `cb_update_1` | Near plane, projection scale/FOV fallback, and cbuffer jitter fallback. |
| Temporal jitter | `0x99274617` Temporal Resolve | `PS CB1` / `ssaa` | `g_vSSAAJitterOffset[0]`, the jitter used by the temporal resolve when sampling current color. |

Depth chain summary:

```text
0xA43343D6 PS SRV0   clip/device depth      -> Luma SR depth cache
0xA43343D6 RTV0      linear r16 depth       -> not used for SR
0x99274617 PS SRV0   g_tClipDepth fallback  -> used only if cache is unavailable
```

## Resolution Source

SR render/input resolution is taken from actual texture dimensions, not cbuffer resolution fields:

```cpp
const uint32_t input_width = min(source_desc.Width, depth_desc.Width, motion_vectors_desc.Width);
const uint32_t input_height = min(source_desc.Height, depth_desc.Height, motion_vectors_desc.Height);
const uint32_t render_width = input_width;
const uint32_t render_height = input_height;
```

SR output resolution is taken from the temporal resolve RTV:

```cpp
const uint32_t output_width = output_desc.Width;
const uint32_t output_height = output_desc.Height;
```

Reason:

- `g_vScreenRes` is post-upscale/final resolution.
- `g_vOutputRes` is generally pre-upscale/render resolution, but it has been observed fluctuating between render and quarter of render size.
- `g_vTAASourceRes` currently reads invalid values like `6, 0`.
- Therefore cbuffer resolution values are not captured or used for SR sizing.

Expected runtime state for 1440p -> 4K:

```text
Last SR Render Width/Height: 2560 1440
Last SR Output Width/Height: 3840 2160
```

## Color Space Flow

Quantum Break's temporal resolve path operates in gamma-space post-processing values before final display mapping and film grain. DLSS is now fed linear color:

```text
QB gamma source color
-> Luma_QB_PreSRDecode.hlsl
-> linear SR input texture
-> DLSS/FSR
-> linear SR output texture
-> original temporal resolve shader (encodes to gamma in SR branch)
```

Pre-SR decode:

- Shader: `Luma_QB_PreSRDecode.hlsl`
- Input: temporal resolve color from `PS SRV slot 2`.
- Operation: `gamma_sRGB_to_linear(..., GCT_POSITIVE)`.
- Output: `game_device_data.upscaling.sr_linear_input_color`.

Post-SR encode is now in `temporal_resolve_0x99274617.ps_5_0.hlsl` when `LumaSettings.SRType > 0`:

- Input: `device_data.sr_output_color` (bound as `PS SRV slot 2`).
- Operation: `linear_to_sRGB_gamma(..., GCT_POSITIVE)` in the SR branch.
- Output: gamma-space color that continues through the normal resolve/display-map path.

## Current SR Settings

Current `SR::SettingsData`:

```cpp
settings_data.dynamic_resolution = false;
settings_data.hdr = true;
settings_data.auto_exposure = false;
settings_data.inverted_depth = false;
settings_data.mvs_jittered = false;
settings_data.mvs_x_scale = static_cast<float>(render_width) * Settings::mv_scale;
settings_data.mvs_y_scale = static_cast<float>(render_height) * Settings::mv_scale;
settings_data.render_preset = dlss_render_preset;
```

Rationale:

- `hdr = true` because the pre-SR pass decodes color to linear.
- `auto_exposure = false` because this is after QB has already applied exposure/post-processing.
- `pre_exposure = 1.f` in draw data, the neutral exposure value.
- `inverted_depth = false`; a game developer confirmed depth is normal, not reversed.
- `mvs_jittered = false`; a game developer confirmed motion vectors are not jittered.

Current `SR::SuperResolutionImpl::DrawData`:

- `source_color`: `sr_linear_input_color`
- `output_color`: `device_data.sr_output_color`
- `motion_vectors`: captured history reprojection motion-vector resource
- `depth_buffer`: cached `A43343D6` depth-linearization `PS SRV slot 0` resource when it matches source color and motion-vector resolution; otherwise temporal resolve `PS SRV slot 0` fallback
- `pre_exposure`: `1.f`
- `jitter_x/y`: chosen raw QB jitter converted to SR input/render pixel space
- `vert_fov`: derived from projection scale, fallback `0.775934f`
- `near_plane`: derived from `g_fInvNear`
- `far_plane`: fixed `1000.f`
- `reset`: true on forced reset, texture shape changes, output changes, or render-size changes

## CBuffer Data

`cb_update_1` is read for:

- jitter fallback
- `g_fInvNear`, used to derive near plane
- `g_mViewToClip`, used to derive vertical FOV
- tessellation projection scale fallback for vertical FOV

Important resolution naming quirk:

- `g_vScreenRes`: post-upscale/final resolution.
- `g_vOutputRes`: generally pre-upscale/render resolution, but not stable enough to drive SR settings.

`ssaa` cbuffer is read for:

- `g_vSSAAJitterOffset[0]`, preferred DLSS jitter source.

The temporal resolve shader samples current color/depth with `g_vSSAAJitterOffset[0]` added directly to UVs, so this remains the most authoritative jitter source until proven otherwise.

## Jitter Units

Raw jitter source priority:

1. `ssaa` `PS CB1` at `g_vSSAAJitterOffset[0]`.
2. `cb_update_1` `PS CB0` at `g_vJitterOffset` as fallback.

The raw QB jitter is kept in `cb_luma_global_settings.GameSettings.JitterOffset` for any QB/Luma shader code that needs to reason in the game's original units.

The SR backend receives render-pixel jitter, because DLSS/FSR expect offsets in input pixel space rather than the raw QB cbuffer units:

```cpp
if (using_ssaa_jitter)
{
	// g_vSSAAJitterOffset[0] is already a normalized UV offset.
	sr_jitter_x = raw_ssaa_jitter_x * render_width;
	sr_jitter_y = -raw_ssaa_jitter_y * render_height;
}
else
{
	// cb_update_1.g_vJitterOffset is less authoritative and appears pixel-space in other QB shaders.
	sr_jitter_x = raw_cb_jitter_x;
	sr_jitter_y = -raw_cb_jitter_y;
}
```

The Y sign is flipped to match the SR convention used by the other Luma integrations.

Before this change, DLSS received QB's tiny raw cbuffer jitter values directly. This change converts the authoritative SSAA jitter to render-pixel units and fixes the Y sign. `g_vSSAAJitterOffset[0]` is a UV offset, so it must use full render-size scaling; a projection/NDC-style `* 0.5f` conversion would make the actual game pattern half-strength.

Captured scene logs show `cb_update_1.g_vJitterOffset` as zero on relevant frames. Other QB shaders add `g_vJitterOffset` to `SV_Position`, which means the fallback should not be multiplied by render size if it ever becomes non-zero. The meaningful temporal resolve input is `ssaa.g_vSSAAJitterOffset[0]`, which repeats this 4-sample UV sequence:

```text
( 0.00014648, -0.00008681 )
( -0.00014648,  0.00008681 )
( 0.00004883,  0.00026042 )
( -0.00004883, -0.00026042 )
```

At a 2560x1440 render size, which is the observed internal render resolution when QB's own upscaling option is enabled for 4K output, that maps to this SR pixel-space sequence after the Y flip:

```text
( 0.375,  0.125 )
( -0.375, -0.125 )
( 0.125, -0.375 )
( -0.125,  0.375 )
```

With an incorrect projection-style `* 0.5f` conversion, DLSS would receive only:

```text
( 0.1875,  0.0625 )
( -0.1875, -0.0625 )
( 0.0625, -0.1875 )
( -0.0625,  0.1875 )
```

The current implementation only fixes units/sign/amplitude for the jitter already produced by QB. Replacing or overriding QB's hardcoded 4-sample pattern with a longer Halton sequence should be a separate follow-up improvement so regressions can be bisected cleanly.

## FOV And Camera Data

Previously observed projection values:

```text
g_mViewToClip proj XY: 1.376382 2.446901
Vertical FOV derived: 0.775934 radians
```

That is about `44.46 degrees`:

```text
vertical_fov = 2 * atan(1 / 2.446901)
```

The code derives FOV from `g_mViewToClip.y` first, then `g_mViewToClip.x`, then the tessellation projection fallback.

Current fallback:

```cpp
constexpr float sr_vertical_fov_fallback = 0.775934f; // ~44.46 degrees
```

Near plane:

- Derived from `g_fInvNear`.
- Defaults to `0.1f`.

Far plane:

- Currently fixed at `1000.f`.
- Still worth deriving if a stable cbuffer source is identified.

## Reset And Pause Handling

DLSS history resets on:

- explicit user reset
- SR output texture size/format change
- source color texture shape change
- depth texture shape change
- motion-vector texture shape change
- render size change
- SR requested but no SR output drawn during a frame

Pause/menu detection:

- Tracks whether scene temporal resolve was seen this frame.
- Holds pause state across the swapchain queue to avoid treating UI-only frames as active gameplay.

Loading/transition guards:

- Temporal-resolve draws that bind the main color/depth/output resources but expose neither `cb_update_1` nor `ssaa` are treated as non-scene transition variants.
- For those transition variants, SR is reset, `LumaSettings.SRType` is cleared, `TemporalResolveResult::stop_processing` is set, and `main.cpp` skips the original draw. This prevents the draw from being replayed through the post-SR resolve path.
- DLSS is warmed up after switching to DLSS, rebuilding SR resources, or observing a no-scene/title/loading frame.
- The warmup is counted in valid scene temporal-resolve draws, not wall-clock seconds. `dlss_scene_warmup_resolve_count` is currently `120`.
- During the warmup window, valid scene temporal resolves run QB's normal temporal resolve and DLSS is skipped with `force_reset_sr` left set. This keeps DLSS history empty until the scene pipeline is stable.
- This avoids the AA-off save-load device hang seen when DLSS started on the first unstable gameplay resolves after title/loading.

Observed failure that motivated the guard:

- None and FSR could load saves.
- DLSS selected after loading a save worked.
- DLSS selected before loading a save could hang the D3D device when Quantum Break's in-game Anti Aliasing option was off.
- The failing path reached a temporal resolve with scene-looking resources but missing both scene constant buffers. The later `CreateRenderTargetView` assert in Luma core was secondary to `DXGI_ERROR_DEVICE_REMOVED` / `DXGI_ERROR_DEVICE_HUNG`, not the root cause.

## Debug UI

In development/test builds, collapsible SR debug sections show:

- history reprojection pass seen
- temporal resolve pass seen
- clip depth captured from the depth-linearization pass
- motion vectors captured
- whether cached clip depth was used for the last SR draw
- UI-only hold frames
- last SR render size
- last SR output size
- active jitter source
- raw SSAA jitter
- raw `cb_update_1` jitter
- SR render-pixel jitter sent to DLSS/FSR
- MV scale multiplier
- jitter scale multiplier

User tuning controls:

- `Reset SR History`

FSR sharpness is fixed at `0.f`.
MV scale and jitter scale are fixed at `1.f`.

## Validation Status And Follow-Ups

Validated in this work:

- SR depth is sourced from the pre-linearized depth-linearization input instead of the linearized depth output.
- Raw QB jitter is converted to SR render-pixel units before being sent to DLSS/FSR; the preferred SSAA jitter path uses full UV-to-pixel scaling, not raw cbuffer values or half-amplitude projection conversion.
- DLSS selected before loading a save no longer hangs on the AA-off path after the transition guard and DLSS warmup.
- The Luma core `CreateRenderTargetView` assert observed during debugging was a secondary symptom of device removal, so no core workaround is kept.

Remaining validation/follow-up work:

- Confirm MV scale and sign over more camera motion. Current fixed scale is `render_width/height`.
- Consider a separate Halton/game jitter-table patch after the unit/sign/amplitude fix is stable enough to bisect separately.
- Confirm whether FSR behaves best with the same linear/HDR path used for DLSS.
- Derive far plane if a reliable source is found.
- Validate color stability with the gamma->linear->SR->gamma path across gameplay, menus, cutscenes, and resolution changes.
- Replace the conservative `120`-resolve DLSS warmup with a tighter state-based stability check if a reliable signal is found.

Most useful runtime checks:

1. Confirm `Last SR Render Width/Height` matches the source/depth/MV texture resolution.
2. Confirm `Last SR Output Width/Height` matches the actual final output resolution.
3. Confirm the cached clip-depth path stays selected with MSAA off; MSAA reconstruction/resolve hashes should not appear in that mode.
4. Test DLSS selected before save load with Anti Aliasing off.
5. Test camera cuts, pause/menu transitions, loading, and resolution changes.
