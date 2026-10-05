# Mafia: Definitive Edition — Luma mod report

This is the engineering report for the Luma game mod in this folder. It is also the user facing
document: read §1–§3 to install and configure it, and §4 onward for the reverse engineering it is
built on and for the measurements behind its defaults.

Everything below was measured on the machine this mod was developed on:

- Windows 11, RTX 5090, 3840x2160, DLAA at native resolution
- ReShade 6.8.0.2155, loaded from `d3d11.dll`
- `nvngx_dlss.dll` **310.9.1.0**
- Game runs in borderless windowed; exclusive fullscreen is prevented (see §3.5)

---

## 1. What this is

The game's only anti-aliasing is a temporal resolve that is always on and cannot be turned off, and
it is soft and ghosty. This mod replaces that resolve with **DLAA** (DLSS at native resolution)
using Luma-Framework's super resolution abstraction, the same way Luma's Mafia III mod does it.

It does **not** upscale. It swaps one temporal AA for a better one, and then fixes the handful of
things the game does around that resolve which are wrong for anything but its own TAA.

What it specifically fixes, beyond running DLAA at all:

| Problem in the game | What the mod does |
| --- | --- |
| The resolve runs **twice per frame**; the second call re-resolves the character/skin/transparency layer on top of DLSS's output, so faces and hands got no DLAA at all | Cancels the second resolve (`cancel_second_taa`, on by default) |
| Motion vectors are in **UV space** but read as pixel space | Scales them by the render resolution |
| The main depth is not bound to the resolve | Captures it from the position-buffer fill pass instead |
| Depth is **reversed-Z** | Sets the matching DLSS flag (verified, not assumed) |

It does not touch the game's rendering, its tonemapper, or its colour pipeline.

---

## 2. Requirements and installation

**Required**

- The game, plus **ReShade 6.8 or newer** — put ReShade's DLL (`d3d11.dll` in this configuration)
  next to `mafiadefinitiveedition.exe` and install ReShade's standard shaders as usual.
- **`nvngx_dlss.dll`** in the game folder (any recent version; 310.9.1.0 is what was developed
  against). Without it DLSS cannot initialise and the mod silently falls back to the game's own AA -
  the panel tells you which of the two is running.
- An RTX GPU.

**Install**

1. Copy `Luma-Mafia Definitive Edition.addon` next to `mafiadefinitiveedition.exe`.
2. Copy the `Luma` **shader** folder (from Luma-Framework's `Shaders/`) next to the executable too.
   This mod uses Luma's global shaders (`Luma/Luma_DisplayComposition`, `Luma/Luma_Scale_VS/PS`,
   `Luma/Luma_Copy_PS`, ...) as well as its own `Luma_MafiaDE_Sharpen`.
3. Start the game and press **Home** to open the panel. The mod's section is at the bottom of
   Luma's panel, and it reports whether DLSS came up ("`DLSS: active`").
4. **Set the game's Anti-Aliasing setting to Low.** It is the only one of the three levels that runs
   the temporal resolve this mod replaces; at Medium and High the game runs no temporal resolve at
   all, so there is nothing for DLAA to stand in for. The panel says so directly if it detects that
   the world is being rendered with no resolve running (§3.5, §6.5).

**The mod ships with every development diagnostic compiled out** (the `Publishing-Release`
configuration defines no `DEVELOPMENT`), so a release build only contains the settings described in §3.

---

## 3. Settings that matter

Everything below is set in the panel and persisted to `ReShade.ini`.

### 3.1 Super Resolution

Luma's own **Super Resolution** combo picks the type (DLSS/DLAA here). There is no second on/off
control in this mod, on purpose: the combo already owns that state, and a duplicate control that
writes to a variable nothing reads is exactly the kind of thing that makes a panel look functional
while doing nothing.

### 3.2 DLSS render preset — pin it, do not leave it on Default

**Use `M`.** This is not a taste call: it is the only preset measured to reproduce the game's
emissive and transparent layers correctly. See §6.1 for the full table.

The mod pins **`M`** automatically, but **only when the preset is still `Default`**. A preset you
picked yourself is never overwritten.

Why it matters, in one line: with `K` and `J` the neon behind a rain-covered window is far too dim
and loses its glow entirely; with `E` and `F` the chroma is right but the glow is still missing. Only
`M` matches the game's own TAA, which is the ground truth here because it involves no DLSS at all.

### 3.3 MVs jittered — default OFF, and it is fine to flip

The default is **OFF**, which contradicts what the data says, and the code says so at the
declaration. Read that comment before changing it back; in short:

- The game's motion vectors **do** already contain the per-frame jitter delta (the resolve shader is
  `prevUV = curUV + MV` and never reads the jitter floats), so the *correct* declaration is ON.
- With DLSS 310.9.1.0, ON measures **softer and noisier**; OFF measures sharper with less noise at
  the cost of more aliasing on high contrast edges.
- OFF cannot smear: the offset it introduces is bounded by the jitter amplitude (≤0.5 px per axis),
  so DLSS's history clamp absorbs it as reduced temporal accumulation rather than as a reprojection
  onto the wrong content. Ghosting needs the filter to trust its history *more*.

Either choice is defensible and the switch is in the panel.

### 3.4 RCAS sharpen — off, available

Off by default. It is the only working sharpening control this mod has: negative values blur instead
of sharpen, which is a cheap way to trade the noise for softness. See §4.5 for the two sharpening
controls that *look* available and are not.

### 3.5 AA quality must be **Low** — the other two levels cannot use DLAA

The game's anti-aliasing setting has Low/Medium/High, and **only Low runs the temporal resolve this
mod replaces**. At Medium and High the game does not run a temporal anti-aliasing pass at all, so
there is nothing for DLSS to stand in for. §6.5 has the measurements.

This is not a defect of the mod, and there is nothing to fix on this side: set the game's AA to
**Low** and let DLAA provide the anti-aliasing. The slider then stops describing anti-aliasing
quality, which is the point of the mod in the first place.

### 3.6 Fullscreen

Exclusive fullscreen is prevented (`prevent_fullscreen_state`). The game deadlocks on an exclusive
fullscreen transition with a hooked swapchain, and nothing here needs it.

---

## 4. How it works

Passes are identified by **disassembly fingerprint × live binding**, not by hash alone.

| Pass | Hash | Role |
| --- | --- | --- |
| Temporal resolve | `0x37B05605` / `0x7B9B914F` | **Replaced by DLSS.** 3 textures: t0 = jittered HDR colour (R11G11B10_FLOAT), t1 = TAA history, t2 = UV space motion vectors (R16G16B16A16_FLOAT). 3x3 Karis variance clamp. Runs at every quality level. |
| FXAA on R32 | `0x2B974E48` | **Deliberately not hooked.** All four of its FXAA taps read t0, which is a *single channel* R32_FLOAT auxiliary buffer, not the colour. Feeding it to DLSS as the colour source is what produced the original "heavy noise" report. |
| Position fill | `0x121A4A96` | Geometry pass, depth test and write enabled → the main depth is captured from here, because the resolve itself runs with depth testing disabled and has no DSV bound. |
| Tonemapper | `0x916B1D65` | Reference point, **not** replaced. Same hash as RenoDX's, so independently confirmed. See §6.3. |
| Depth of field | `0xA6CCD59B` | Reference point. |
| Bloom bright | `0x5C8BCE6A` | 5-tap mean + `max(., 0)`. Left alone. |
| Post-tonemap sharpen | `0x747C6210` | **Never runs during gameplay** in practice - see §4.5. Never cancel this draw. |

### 4.1 Motion vectors

This game has **no standalone raw motion vector buffer**. The resolve's own t2 is the motion vector,
confirmed by sniffing ~5.1M draws for any full-resolution two-channel float texture that a pixel
shader binds. It is stored in **UV space**, so it must be scaled by the render resolution before it
is handed to DLSS (Mafia III's mod does the same).

### 4.2 Jitter

Read from the resolve's pixel shader constant buffer slot 0, floats 32 and 33. They are already in
pixel space (-0.5..0.5, quantised to an 8 phase Halton sequence), and are handed to DLSS as-is: no
NDC conversion and no sign flip. They were located by diffing constant buffer dumps across frames -
they are the only values that change every frame and sit in that range.

Measured: changing this value (one frame late, sign flipped, or both) makes **no visible difference**
with DLSS 310.9.1.0, so it is not worth chasing if something looks wrong. See §6.2.

### 4.3 Depth

Reversed-Z, and **verified rather than assumed**: the main geometry pass runs with
`DepthFunc = D3D11_COMPARISON_GREATER_EQUAL`, i.e. far plane at 0. An in-game A/B of the DLSS flag
shows no visible difference at all, which is exactly why it needed a hard judge like that probe -
getting it wrong only shows up as wrong disocclusion at occluded edges.

The trap this guards against: the position-buffer fill pass is a bare `mov o0.xyzw, v0.xyzw` with
zero resources, so the engine also reuses it for depth-only draws whose DSV is a **shadow cascade
atlas** (2048x2048 R16_TYPELESS, two array slices). Taking whatever the last matching draw bound
made the main depth flip-flop between the real one and that atlas within a single session. The
discriminator is size and slice count: the main depth is at least (almost) the render resolution and
a single slice, and the format is deliberately not whitelisted.

### 4.4 The second resolve

The resolve runs **twice per frame** for different parts of the image (same shader hash both times;
Mafia III's mod documents the same thing with "this happens 3 times"). The second call covers the
character / skin / transparency layer. If it is allowed to run, the game's own TAA re-resolves that
layer on top of the DLSS output, which is why faces and hands showed no DLAA and no noise reduction
effect while the rest of the frame did. `cancel_second_taa` cancels it. DLSS writes the whole frame.

### 4.5 Sharpening: two controls that look real and are not

- **The game's own post-tonemap sharpener (`0x747C6210`) never runs during gameplay.** The probe log
  contains zero `[sharpen]` lines across long sessions, and that pass is instrumented to print on its
  first four draws. RenoDX labels the very same hash "loading screen". So scaling its `cb0[0].z` - 
  which the mod can do - does nothing, and the slider for it was removed from the shipping panel.
  **Never cancel that draw either**: its `mul r0, r0, v1` sits *outside* its own
  `if (strength > 0)` block, so cancelling removes a per-pixel multiply and turns the screen black.
- **DLSS's own sharpness parameter is not reachable through Luma.** `DLSS.cpp` declares
  `float sharpness = 0.f; // Unused` and only ever passes it out of `NGX_DLSS_GET_OPTIMAL_SETTINGS`;
  it never reaches the eval parameters. Only FSR honours `draw_data.user_sharpness`.

That leaves the mod's own RCAS pass (§3.4) as the only real sharpening control.

---

## 5. Facts that took the longest to establish

Do not re-litigate these; each cost a session.

1. **There is no raw MV buffer** (§4.1) and the resolve's t2 is the motion vector.
2. **The MVs must be scaled by the render resolution**, not read as pixel space. Reading UV space
   vectors as pixel space shrinks the perceived motion by a factor of 3840, which makes DLSS see
   essentially random motion - that was the original "heavy noise".
3. **The MVs already contain the jitter delta** (§3.3). The declaration is a fact about the data.
4. **Depth is reversed-Z, and it must be captured from the position fill pass**, with the shadow
   atlas trap handled (§4.3).
5. **Jitter and depth flags show no visible difference in an A/B**, so they need a hard judge
   (constant buffer diffing, a depth function probe) rather than eyeballing.
6. **The game has no user-facing image quality controls at all** (§6.3): the tonemapper's curve
   parameters are hardcoded engine constants, and no post-process pass owns a "sharpen" or
   "quality" value. Graphics presets change nothing in the post chain - an early measurement that
   looked like they did was actually the film grain's rotation matrix animating, plus texel sizes
   changing with resolution.
7. **Only AA=Low runs a temporal resolve, and cutscenes run no 3D at all** (§6.5, §6.6). Both looked
   like mod bugs for a while - "DLSS silently stops working at AA=Medium/High" and "the cutscene has
   no anti-aliasing". Neither is something this mod can fix, and what settled both was recording the
   set of pixel shaders drawn on every frame and diffing it across the transitions, rather than
   reasoning about which pass ought to own which setting.

---

## 6. Measurements

### 6.1 DLSS render presets

Measured on this title over a rainy night scene (neon behind a wet window, puddles, dark bar floor,
sofa) and sunlit ground at noon, DLAA at native resolution.

| preset | sharpness | power draw | noise flicker | reflective / transparent correctness |
| --- | --- | --- | --- | --- |
| `E` (CNN) | lowest | lowest | lowest | chroma close to the game's TAA, **edge glow missing** |
| `F` (CNN) | lowest | lowest | lowest | same as `E` |
| `J` | high | low | lowest | **wrong** |
| `K` | highest | low | lowest | **wrong** - neon far too dim, chroma wrong, no glow |
| **`M`** | medium | very high | highest | **correct, matches the game's own TAA** |
| `L` | slightly above `M` | highest | highest | not measured (eliminated on cost) |
| `Default` | unknown | - | - | resolves to a model that mishandles reflective content |

Notes:

- **Reflective correctness is a separate axis from noise**, and it is the one that decides this. It
  was measured against **`none`** mode (the game's own TAA, no DLSS involved) as ground truth.
  An earlier version of this table wrongly merged the neon's *brightness* with three *noise*
  readings into one score and concluded the opposite; that is corrected here.
- The core's own tooltip for `J`/`K` ("issues with reflections and transparent effects /
  volumetrics") is **correct** and is exactly what was measured.
- `L` and `M` are the newest models and are meant for upscaling from a low resolution - this mod
  runs DLAA at native resolution, which is the one case they are not tuned for, and they cost an
  order of magnitude more power on top.
- **`M` is therefore the shipping default**, despite being the noisiest and second most expensive:
  losing an emissive layer is a content error that no post-process can undo, while `M`'s softness
  is the kind of problem a sharpening pass can address.

### 6.2 `mvs_jittered` and jitter timing

| configuration | result |
| --- | --- |
| `ON` (correct declaration) | softer, more noise on bright sky / sunlit ground |
| `OFF` | sharper, less noise, more aliasing on high contrast edges; reflections unaffected |
| jitter one frame late / sign flipped / both | **no visible difference** at all |
| `auto_exposure` on/off | no visible difference |
| `hdr` input off | breaks contrast, so HDR input must stay **on** |
| MV scale set to 1.0 (wrong) | dramatically worse - and the fastest way to see what real ghosting looks like here |

### 6.3 The tonemapper (`0x916B1D65`)

Reverse engineered to settle whether it owns any usable control. The full SM5 disassembly this is
based on is bundled as **`docs/disasm-916B1D65.txt`** (and the frame tail's other candidate,
`docs/disasm-DFE68AFF.txt`, which turns out to be a deferred lighting pass rather than a post
process). The essentials:

- It is a full post chain in one shader: chromatic aberration, bloom, an additive detail layer, a
  hue-preserving contrast curve, a **3D colour grading LUT** (16³, bound at t9, blended at 0.8), a
  saturation trim, a rotating film-grain overlay, a vignette, and a dithered 8-bit output.
- **It contains no sharpening at all** - no neighbourhood filter, no unsharp mask, no CAS/RCAS. The
  frame tail's other six shaders are a deferred lighting pass, three single-sample blits and two
  zero-sample ROP passes. **So there is no game-side sharpening constant to expose anywhere in this
  title.** That is what closed the original "find the sharpening" investigation.
- Its operator is a single enum in `cb0[3].x`: `0` and `1` clamp to 1 (SDR, what the game uses),
  while **`-2` is literally `max(exposure * colour, 0)` with no curve and no clamp**, i.e. linear
  unclamped HDR, and `-1`, `-3`, `-4` are also unclamped.
- **Exposure and white point are not in a constant buffer.** They live in a **1x1 texture** (t4,
  R16G16B16A16_FLOAT): measured exposure 0.0019, white point 64.0. This is why DLSS's
  `pre_exposure` was previously "ambiguous" - it was never in the CB.
- The Uncharted2 parameters (`cb1[4]` = 0.22/0.30/0.10, `cb1[5]` = 0.20/0.01/0.30) are hardcoded
  engine constants and never change.

### 6.4 HDR output

**Verdict: this mod ships without a native HDR container, but this is *not* a limitation of the
game, and it is an open problem rather than a closed one.**

What happens with Luma's own swapchain upgrade (`swapchain_format_upgrade_type = AllowedEnabled`),
in all three configurations tried:

| container | format | present model | result |
| --- | --- | --- | --- |
| scRGB | `R16G16B16A16_FLOAT` | flip forced | 8 draws/frame, black screen |
| HDR10 | `R10G10B10A2_UNORM` | flip forced | 8 draws/frame, black screen |
| scRGB | `R16G16B16A16_FLOAT` | `DISABLE_SWAPCHAIN_FLIP_MODEL 1` | 8 draws/frame, black screen |

In every case the upgraded swapchain really is created and Luma logs no error at all, but the TAA
resolve never runs and DLSS never draws: the renderer goes idle while frames keep being presented.

**The same game runs fine at those formats through RenoDX.** RenoDX's mafiade add-on drives it at
`r10g10b10a2_unorm` (HDR10, `hdr10_st2084`) and at `r16g16b16a16_float` (scRGB) with clean gradients
and zero errors over 30-60 s runs. The difference is the mechanism: RenoDX **proxies** the swapchain
(its own proxy vertex/pixel shaders plus resource cloning, via `renodx::mods::swapchain`), while Luma
changes the game's own swapchain **in place** and additionally forces `DXGI_SWAP_EFFECT_FLIP_DISCARD`
and, when `prevent_fullscreen_state` is set, `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT`.
One of those is what this game rejects. That is the open question, and it is a tractable one because
a working reference exists.

The content side is already free should anyone pick this up: switching the tonemapper's operator to
`-2` (see §6.3) makes the game emit linear unclamped HDR, which is one constant buffer float, and
Luma's display composition parameters are already wired for it.

The mod forces the container off and ignores the ini key, because a published build must not ship a
switch that produces a black screen.

**One caveat on those three measurements.** They were taken with RenoDX's HDR10 channel and
OptiScaler's XeFG/NR both live on the same swapchain: `ReShade.log` from that session shows both
add-ons loading and RenoDX upgrading resources `b8g8r8a8 -> r16g16b16a16`. So "this game rejects
Luma's in-place upgrade" was never fully separated from "three tools reached for one swapchain at
once". A single-variable retest with RenoDX and OptiScaler disabled would settle it, and the
`NativeHDRContainer` ini key is deliberately left in the code as the switch for exactly that test -
the key is read, validated and logged, it just is not honoured. Do not record the black screen as a
property of the game until that retest has been run.

### 6.5 AA quality: only Low runs the resolve

The animation setting has three levels and **only Low runs a temporal resolve**. This was measured
with a per-frame pixel shader set diagnostic over one session that walked Low -> Medium -> High ->
Low and then watched a cutscene.

| segment | frames | distinct pixel shaders | contains `37B05605` / `7B9B914F` (the resolves) |
| --- | --- | --- | --- |
| AA = Low (step 1) | 4676-11900 | 109-110 | **yes** |
| AA = Medium | 11943-13743 | 147-176 | **no**, in none of 15 snapshots |
| AA = High | 14629-16253 | 147-177 | **no**, in none of 15 snapshots |
| AA = Low again | 17085-18902 | 101-150 | **yes** |
| back at the same spot | 53484-53664 | 107-110 | **yes**, identical to step 1 hash for hash |

Supporting evidence:

- The `resolve hooked` counter is completely static across both non-Low segments.
- Medium and High differ by exactly one hash - they are the same render path.
- All 67 shaders that appear at Medium/High but not at Low were disassembled: every one of them has
  vertex input, MRT output and a light array (`CB1[254]`), i.e. they are geometry passes, not
  post-processing.
- Screening all 147 shaders of the Medium/High set for neighbourhood sampling (`sample >= 9`) found
  no temporal resolve either.

**So at Medium and High the game is not doing temporal anti-aliasing at all.** That is why replacing
the resolve with DLAA does nothing there - there is no draw to replace. It also means the AA slider
stops describing anti-aliasing quality once DLAA is in play, which is why §3.5 says to leave it on
Low.

### 6.6 Cutscenes: what actually runs on screen

Cutscenes (and the main menu and the loading screens) are **pre-rendered video**, not a 3D render
with anti-aliasing turned off. The same per-frame shader set shows it directly:

```
[set] frame 23001: 7 pixel shaders: 00D96EAE 48A2BAAB 61A36F70 6B824D1B A7799306 BBFBB5A1 D870A196
[set] frame 49615: 6 pixel shaders: 00D96EAE 48A2BAAB 61A36F70 6B824D1B A7799306 BBFBB5A1
```

- 6-8 shaders against 100-190 while the world is being drawn, with **no resolve and no tonemapper**.
- Every 3D-to-flat transition in the session was clean: 17 of them, with no intermediate state.
- `0x00D96EAE` is the video/letterbox pass, and the mod already counts its draws for exactly this
  reason - see `video_pass_draws`, which exists so a cutscene can be told apart from a menu by the
  log rather than by asking.

Consequences, none of which are fixable from this side:

- **There is nothing to anti-alias.** A video has no geometry edges; the content is already fully
  resolved pixels, so "turn AA on during cutscenes" is not a meaningful goal.
- **DLSS SR, RR and NR all need guides** (depth, motion vectors, and for RR the G-buffer). A video
  frame has none of them, so running them there means synthesising them: estimated optical flow plus
  a constant depth. That is what Magpie's colour-only path does and it works, but it is a different
  pipeline rather than a flag in this mod.
- **Frame generation over video** is subject to the same missing-guide problem.

The mod's own contribution to that problem is the detection signal, and it is reliable: a frame with
no tonemapper is a frame that is not rendering the world. The transition is a single frame wide, so
the signal is available at the exact frame the cutscene starts.

---

## 7. Known limitations

1. **DLSS render preset must be `M`** for correct reflective and transparent content (§6.1).
2. **`M` is the noisiest and second most expensive preset.** That is the price of correctness, and
   the only knob against it is the mod's RCAS, which trades sharpness for noise.
3. **No native HDR output through this mod** (§6.4). Not a game limitation; the mechanism is unsolved.
4. **No game-side sharpness control exists to expose** (§6.3).
5. The mod replaces the temporal resolve only. It does not upscale, so there is no performance win -
   DLAA at native resolution costs more than the game's TAA.
6. **The game's AA must be set to Low.** Medium and High run no temporal resolve, so DLAA cannot
   engage there (§6.5). Nothing on this side can change that.
7. **Cutscenes are pre-rendered video.** They cannot be anti-aliased, and they cannot be fed to
   DLSS SR/RR/NR or to frame generation by this mod, because a video frame carries none of the
   guides those features need (§6.6).
8. **The probe log is a single file** (`LumaMafiaDE-probe.log`, overwritten each run), with a
   50000-line cap that writes "[line cap reached, further output suppressed]" into the file when it
   is hit. Nothing is ever renamed or deleted. A release build emits roughly one line per five
   seconds plus a few one-off events, so the cap is many hours away; it exists because the older
   behaviour of returning silently from the logger is what made a session's tail disappear without
   anyone noticing.

---

## 8. Credits and licence

- Built on **Luma-Framework** by Filippo Tarpini (`Filoppi/Luma-Framework`) - the super resolution
  abstraction, the display composition, the resource upgrade machinery and the add-on loader.
  Licence: Custom MIT, see the repository root.
- The DLSS preset measurements, the tonemapper anatomy and the HDR container verdict were measured
  on this machine and are recorded with their raw logs; they are not theory.
- `docs/disasm-916B1D65.txt` and `docs/disasm-DFE68AFF.txt` are SM5 disassemblies of the game's own
  shaders, produced from Luma's own shader dumps. The tool that produced them is a small
  `D3DDisassemble` wrapper; it lives outside this mod's folder in the development tree.
