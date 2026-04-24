# Quantum Break Upscaling Notes

## Scope

These notes describe the current Quantum Break SR/DLSS integration in Luma.

Relevant files:

- `Source/Games/Quantum Break/main.cpp`
- `Shaders/Quantum Break/Luma_QB_PreSRDecode.hlsl`
- `Shaders/Quantum Break/Luma_QB_PostSREncode.hlsl`
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

## Hook Points

Shader hashes:

```cpp
shader_hashes_history_reprojection.compute_shaders.emplace(std::stoul("E8337D48", nullptr, 16));
shader_hashes_temporal_resolve.pixel_shaders.emplace(std::stoul("99274617", nullptr, 16));
```

History reprojection pass:

- Compute shader hash `E8337D48`.
- Marks TAA detected.
- Captures motion vectors from `CS SRV slot 0`.
- Stores the resource in `game_device_data.sr_motion_vectors`.

Temporal resolve pass:

- Pixel shader hash `99274617`.
- Main SR insertion point.
- Captures depth from `PS SRV slot 0`.
- Captures source color from `PS SRV slot 2`.
- Captures final resolve RTV from `OM RTV slot 0`.
- Reads only the SR-required values from `cb_update_1` and the temporal resolve `ssaa` cbuffer through staging readback.
- Runs SR before executing the original temporal resolve draw.

## Resolution Source

SR render/input resolution is taken from actual texture dimensions, not cbuffer resolution fields:

```cpp
const uint32_t max_input_width = min(source_desc.Width, depth_desc.Width, motion_vectors_desc.Width);
const uint32_t max_input_height = min(source_desc.Height, depth_desc.Height, motion_vectors_desc.Height);
const uint32_t render_width = max_input_width;
const uint32_t render_height = max_input_height;
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
-> Luma_QB_PostSREncode.hlsl
-> QB gamma SR color
-> original temporal resolve shader
```

Pre-SR decode:

- Shader: `Luma_QB_PreSRDecode.hlsl`
- Input: temporal resolve color from `PS SRV slot 2`.
- Operation: `gamma_sRGB_to_linear(..., GCT_POSITIVE)`.
- Output: `game_device_data.sr_linear_input_color`.

Post-SR encode:

- Shader: `Luma_QB_PostSREncode.hlsl`
- Input: `device_data.sr_output_color`.
- Operation: `linear_to_sRGB_gamma(..., GCT_POSITIVE)`.
- Output: `game_device_data.sr_gamma_output_color`.

The original temporal resolve draw receives `sr_gamma_output_color_srv` in `PS SRV slot 2`, so the shader contract remains gamma-space.

## Current SR Settings

Current `SR::SettingsData`:

```cpp
settings_data.dynamic_resolution = false;
settings_data.hdr = true;
settings_data.auto_exposure = false;
settings_data.inverted_depth = false;
settings_data.mvs_jittered = false;
settings_data.mvs_x_scale = static_cast<float>(render_width) * Settings::SuperResolution::mv_scale;
settings_data.mvs_y_scale = static_cast<float>(render_height) * Settings::SuperResolution::mv_scale;
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
- `depth_buffer`: temporal resolve `PS SRV slot 0`
- `pre_exposure`: `1.f`
- `jitter_x/y`: SSAA jitter if available, otherwise `cb_update_1` jitter
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

The temporal resolve shader samples current color with `g_vSSAAJitterOffset[0]`, so this remains the most authoritative jitter source until proven otherwise.

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

## Debug UI

In development/test builds, collapsible SR debug sections show:

- history reprojection pass seen
- temporal resolve pass seen
- motion vectors captured
- UI-only hold frames
- last SR render size
- last SR output size
- MV scale multiplier
- jitter scale multiplier

User tuning controls:

- `Reset SR History`

FSR sharpness is fixed at `0.f`.
MV scale and jitter scale are fixed at `1.f`.

## Current Open Questions

Main remaining validation work:

- Confirm MV scale and sign. Current fixed scale is `render_width/height`.
- Confirm jitter sign and units. Current fixed scale is `1.f`.
- Confirm whether FSR behaves best with the same linear/HDR path used for DLSS.
- Derive far plane if a reliable source is found.
- Validate color stability with the gamma->linear->SR->gamma path.
- Validate render/output sizes in the debug UI across gameplay, menus, cutscenes, and resolution changes.

Most useful runtime checks:

1. Confirm `Last SR Render Width/Height` matches the source/depth/MV texture resolution.
2. Confirm `Last SR Output Width/Height` matches the actual final output resolution.
3. Test camera cuts, pause/menu transitions, loading, and resolution changes.
