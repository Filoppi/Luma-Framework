# Quantum Break Super Resolution

Luma inserts DLSS/FSR at the game's temporal resolve, using GPU textures and live native
camera/jitter values without GPU-to-CPU constant-buffer readback. If SR cannot run safely,
the game keeps its TAA. The in-game AA option controls MSAA, not TAA.

## Rendering Flow

1. Observe depth linearization and history reprojection to retain the current frame's depth/MVs.
2. At temporal resolve, require the D3D11 immediate context and the actual custom temporal
   pixel shader to be bound; knowing the original shader hash is not sufficient.
3. Select matching textures and validate their identities against the live native scene.
4. `Luma_QB_PreSRDecode` converts gamma-space color to linear HDR; DLSS/FSR upscales it.
5. Bind the SR result at PS SRV2 and set Luma's SRType. The temporal replacement encodes
   it back to gamma for the remaining post-processing instead of performing the normal TAA.

The original resolve draw executes once, except for the unsafe transition case below.
Graphics/compute state, PS SRV2, and Luma CB12 (data)/CB13 (settings) bindings are restored.

## Inputs

| Input | Source |
|---|---|
| Color | Temporal resolve PS `0x99274617`, SRV2 |
| Preferred depth | Depth linearization PS `0xA43343D6`, SRV0 **before** linearization |
| Alternate depth | Temporal resolve PS SRV0 (`g_tClipDepth`) |
| Motion vectors | History reprojection CS `0xE8337D48`, SRV0 |
| Output size | Temporal resolve OM RTV0 |
| Near plane / vertical FOV | Current native main-view and SSAA path 0 projection/inverse |
| Jitter | Current native SSAA path 0 projection |

Keep the largest depth-linearization input per frame; its linear-depth RTV is not an SR input.
Color, selected depth, and MVs must have equal nonzero dimensions. Select depth once, then
validate and pass the same owned depth/MV resources to the backend. Output dimensions may differ.

## Native Camera And Units

`NativeHooks.cpp` fingerprints the supported renderer layout before installing MinHook hooks.
Samples exist only inside their owning scene/temporal calls. Eligibility requires the same
thread, generation token, main view, accepted renderer scene, and observed depth/MV identities;
main and temporal near/FOV must agree. Toggles/cleanup invalidate tokens. Never substitute
previous-frame or merely last-observed inputs; debug snapshots are display-only.

Preserve native double-to-float conversion before jitter scaling: UV jitter is
`float(projection[8]) * 0.5f`, `float(projection[9]) * -0.5f`.
SR pixel jitter is `uv_x * render_width`, `-uv_y * render_height`.
Near/FOV decoding preserves the renderer's float operation order and rejects invalid matrices.
The game's four-phase jitter is unchanged; no Halton override is implemented.

Backend settings: linear HDR, pre-exposure 1, auto exposure off, normal depth, non-jittered MVs,
MV scales equal render width/height, dynamic resolution off, far plane 1000, FSR sharpness 0.

## Failure And History Policy

CB0/CB1 checks use only binding flags and buffer sizes (minimum 1944/200 bytes), never contents.
For an SR-requested resolve with scene color/depth/output bindings:

| Condition | Action |
|---|---|
| Verified shader/native inputs/textures and both CB layouts valid | Run SR after warmup |
| Rejected inputs or either CB layout invalid, with at least one valid layout | Reset SR; run game TAA |
| Neither CB layout valid | Reset SR; skip the known unsafe transition draw |

The skip guard protects against a loading-transition device hang. Native rejection alone is
not permission to skip; the renderer's rejection byte cannot replace this binding guard.
Rejected inputs must not reach the backend or bind stale SR output.

DLSS waits for 120 valid scene resolves after selection/cleanup and when rearmed by rejected
inputs or no-scene frames. During warmup, game TAA runs and DLSS history stays reset.
Texture/output changes, failures, and explicit requests reset history; requests are consumed
with atomic exchange so concurrent resets are not lost. Pause detection accounts for queued frames.

## Code Map And Maintenance

- `main.cpp`: pass routing, original draw/CB restoration, lifecycle, settings, pause markers.
- `Upscaling.hpp` / `.cpp`: pass descriptors, resources, SR execution, history and diagnostics.
- `NativeHooks.hpp` / `.cpp`: renderer layout/ABI, hook installation, live capture scopes.
- `NativeInputs.hpp`: pure decoding, tokens/scopes, eligibility, resource/action/reset policies.

**Keep `Upscaling.cpp` included exactly once by `main.cpp` and excluded from separate compilation.**
Core has translation-unit-local state. `NativeHooks.cpp` compiles independently without Core.
`UseLumaNGX` / `UseLumaFSR` control features; both off excludes native hooks and MinHook.
Install hooks outside `DllMain`, pin modules before activation, and retain trampolines until exit;
detach only invalidates capture. Do not weaken these lifetimes or draw/state restoration ordering.

Development/Test expose bounded `QB native inputs:` logs and display-only snapshots.
Changes to renderer ABI, transitions, or shader reloads require in-game validation.