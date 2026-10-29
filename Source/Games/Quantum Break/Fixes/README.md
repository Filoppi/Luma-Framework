# Quantum Break native fixes

Adds the existing Mouse Fix features to the Quantum Break Luma add-on. There is no second add-on registration, second overlay, or private MinHook build. Existing Luma shaders, HDR conversion, grading, SR inputs/backends, jitter generation, and ultrawide patches are unchanged.

## Controls

- Fix Mouse Input: event-driven raw mouse counts with existing in-game sensitivity, five buttons, and both wheel axes. No smoothing or fixed polling gate.
- Disable Motion Blur: native vector blur flag.
- Disable Bloom: native bloom flag.
- Disable Flares: separate native flare flag; this is independent of Luma's existing Flare/Glare Compensation grading slider.
- Disable Depth of Field: native DOF flag, including cutscenes.
- Unlock Cutscene FPS: rendered cinematics, including camera/spotlight timing and terminal-camera release correction. Encoded videos are unaffected.
- High Resolution Particles: the engine's existing full-resolution transparent particle path, including compatible airborne blood. This does not supersample or replace textures.

Preferences retain the existing [MouseFix] configuration keys in ReShade's configuration. Missing choices default on; explicit saved choices are preserved. An absent separate flare choice inherits the old combined bloom preference once. No separate INI or log is created.

## Coexistence and lifecycle

Initialization runs only from Luma's asynchronous OnInit path, outside the loader lock, after its existing native SR hook installation. The fixes reuse the project's pinned MinHook sources. They never uninitialize the shared hook library. Luma's existing shaders and native jitter/view capture hooks are not patched by these fixes.

The cutscene camera-cut hook forwards the native invalidation call. Luma continues to decide whether its own scene history is valid. Changing a visual control through Luma's menu requests a reset through Luma's existing force_reset_sr mechanism. No artificial camera-history-valid flag is introduced.

Remove the old standalone MouseFix.addon64 before testing this build. Known standalone filenames are rejected before initialization; native byte guards also reject incompatible camera/cutscene patches. Renaming an old add-on does not make coexistence supported. Native hooks and the input worker remain process-lifetime, as in the source implementation; live DLL unloading is not supported.

Unsupported raw-input initialization rolls back its partial installation before starting optional cutscene/particle hooks. The menu displays the failure status. Native code and particle parameter layout guards remain specific to the tested Quantum Break DX11 build 1.0.126.0307.

## Validation

Run tests/run.ps1 -ZigPath <path-to-zig.exe> on Windows x64 with Zig 0.15.2. It builds the integrated source against Luma's pinned MinHook/ReShade/ImGui dependencies, runs saved-settings, native input ownership, all-button/wheel forwarding, native effect restoration, particle guards/restoration, and cinematic lifecycle/timing checks. It also compiles the actual Luma menu/config adapter.

These focused checks do not replace building the complete Quantum Break Luma project in Publishing-Release and testing inside the game with HDR and SR enabled. That complete build and gameplay validation remain required before marking the contribution ready for merge. No installed game files have been changed while preparing this integration.

No reflection, fog, texture-streaming, poster, or particle-supersampling experiments are included.
