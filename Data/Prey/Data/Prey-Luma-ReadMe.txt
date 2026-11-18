Luma is a Prey (2017) + Mooncrash DLC mod that re-writes the game's late rendering and post processing phases to improve the game's look without drifting from the artistic vision (believe me!).
The highlight feature is adding HDR support, DLSS and new Ambient Occlusion, making it akin to a smallish Remastered.
The mod works by hooking into the game's code and replacing shaders.
Luma for Prey was created by Pumbo (graphics) and Ersh (reverse engineering).
Since then Luma grew into a generic DirectX 11 games modding framework (https://github.com/Filoppi/Luma-Framework).

# List of features:
- Added HDR output (scRGB 16bit) (improved tonemapping, reworked all post processing effects)
- Increased buffers quality in SDR and HDR, reducing banding all around
- Improved the quality of dynamic shadow, especially from up close (they had broken filtering that causes them to be blocky) (the maximum shadow render distance is also increased through configs)
- Added a more modern Ambient Occlusion solution (GTAO) (the original AO is also improved in quality)
- Improved Screen Space Reflections (they are not cropped close to the camera anymore, they now get progressively more diffuse with distance, they blend in and out of view more nicely etc etc, their math in general has been refactored for much better looking and more "physically accurate" results)
- Added DLAA+DLSS Super Resolution (on Nvidia GPUs) (OptiScaler can be used to inject FSR 3+) (this looks drastically better than the native TAA and has no noticeable ghosting)
- Added RCAS sharpening after TAA (replacing the original basic sharpening implementation, making it look a lot more natural)
- Added Perspective Correction (optional) (a modern type of "lens distortion" that makes the rendering projection look natural)
- Improved all of the native Anti Aliasing implementations (e.g. SMAA/TAA)
- Improved Anisotropic Filtering (it was not set to 16x on all textures that would benefit from it)
- Improved quality and look of the Sun, Sun Shaft effects and Lens "Optics" effects (e.g. lens flare)
- Improved Motion Blur quality and fixes multiple issues with its motion vectors
- Improved Bloom quality and temporal stability and fixes multiple issues with its generation (e.g. it was trailing at the edge of the screen)
- Improved Ultrawide Aspect Ratio and High FOV support (Bloom, AO, SSR, Sun Shafts, Lens Optics, Lens Distortion etc did not scale properly with either aspect ratio or FOV, e.g. causing the sun to be huge in UW or causing bloom to be stretched, chromatic aberration was stretched in UW) (the game now also exposes the vertical FOV instead of the horizontal one, which was limited to 120 and not ultrawide friendly)
- Improved High Resolution support (the game was mostly developed for 1080p resolution, a multitude of effects did not scale properly to 4k, like the objects highlights overlay, or stars/sun sprites)
- Improved Dynamic Resolution Scaling support (Film Grain, Bloom, TAA, AO, SSR, Sun Shafts, Lens Optics, Particles and many other effects did not scale properly with it, causing visible changes in the image when the resolution changed, and its upscaling implementation just did not look very nice)
- Improved High Frame Rate support by unlocking the frame rate beyond 144 (you can change the limit in the menu now) (with DRS you can easily reach 240FPS now, thanks to tweaked settings that made it more stable)
- Improve Auto Exposure logic, by making it focus more on what's on the center of the screen and not at the edges
- Improved Swapchain flip model (more responsive for VRR)
- More (e.g. added optional HDR post process filter on pre-rendered video, added settings to turn off Vignette or Camera Motion Blur)!

# How to install:
- Drop all the files into the game installation folder (including "autoexec.cfg" and "system.cfg") (the root folder, not the one with the executable). Override all files (you can make a backup, but Luma just changes a couple configs in the game packages, these changes simply increase the rendering quality and can persist without Luma). If you are updating the mod, delete the "Prey-Luma" folder in the game binaries folder before applying the new one. These files will automatically load the mod for the Mooncrash DLC too, in case you had it.
- If you are on GOG or Epic Games Store, move the files in ".\Binaries\Danielle\x64\" respectively to ".\Binaries\Danielle\x64-GOG\" or ".\Binaries\Danielle\x64-Epic\".
- Install the latest VC++ redist before using (https://aka.ms/vs/17/release/vc_redist.x64.exe).
- On Linux/Proton, install it in the game's prefix: run "protontricks 480490 win11" (the installer refuses older Windows versions), download the latest installer (https://aka.ms/vc14/vc_redist.x64.exe), then run "protontricks-launch --appid 480490 <path to>/vc_redist.x64.exe /install /quiet /norestart". To check for a newer one, compare the "File version" of "steamapps/compatdata/480490/pfx/drive_c/windows/system32/msvcp140.dll" with the latest version listed at https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist.
- Install ReShade 6.4.1+ (with Addons support, for DX11, preferably as dxgi.dll) (you can disable the "Generic Depth" and "Effects Runtime Sync" Addons for performance gains).
- Unless you are on Linux/Proton, delete the "d3dcompiler_47.dll" from the main binary folder, it's an outdated shader compiler bundled with the game for "no reason" (Windows will fall back on the latest version of it this way, but Proton doesn't distribute the file so leave it in).

# Information:
- You can press Home to show ReShade's interface, and there you will find a new window with Luma's settings.
- The performance cost on modern GPUs is negligeable, especially when using DLSS SR + Resolution Scaling (in fact, performance might drastically increase in that case) (GTAO is the only addition that has a perceivable performance cost, but it can be disabled or lowered in quality in the advanced settings section).
- The mod is best used with all the graphics settings maxed out in the game, but any setting combination is supported too.
- The game's HDR uses the HDR calibration data from Windows 11 and display's EDID.
- The in game brightness slider is best left at default value.
- Before updating the mod, make sure to delete all its previous files. To uninstall, delete all the files and restore the original version of the overwritten ones (or not, they simply change a couple of quality configs, they can stay without Luma).
- The game runs in HDR mode even when targeting SDR. Most ReShade shaders/effects still don't properly support HDR yet, so avoid using them.
- The changes to the game's Engine.pak file are completely optional and not destructive. They simply increase the quality of shadow and a couple of other things for the highest quality profile, and make sure all graphic settings default to "Very High" (4), given that the game is now relatively old and cheap to run. Some of the settings are inspired from "Real Lights plus Ultra Graphics Mod", but they've been carefully polished (I've also kept a couple random files that that mod added to the engine pak, to make sure it's 100% compatible with it when Luma is installed after). You can run Luma without changing these files from their vanilla version.
- If you want to force DLSS at a specific resolution scale (and thus quality mode), simply set the dynamic resolution scale to (e.g.) 0.75 in the settings menu, and then set its target frame rate very high, so that the game will never reach it and you will constantly run at the target minimum resolution.

# Issues and limitations:
- The weapon FoV will reset to a (vertical) value of 55° (the default) every time a new level is loaded.
- The Microsoft Store version is not supported (the game data is the same across all game releases, so one could theoretically force use the executable from Steam, GOG or Epic Store).
- The UI will look a bit different from Vanilla due to Luma using HDR/linear blending modes by default. Set "POST_PROCESS_SPACE_TYPE" to 0 or 2 in the advanced settings to make it behave like Vanilla.
- MSAA or Super-sampling are not supported with Luma.
- Changing the resolution after starting the game is not suggested, as some effects get initialized for the original resolution without being resized (vanilla issue).
- FXAA and no AA are not suggested as they lack object highlights and have other bugs (e.g. FXAA can break the game when close to an enemy and looking at the sun) (vanilla issue) (Luma hides FXAA from the settings).
- Sun shafts can disappear while they are still visible if the sun center gets occluded (this is a bug with the original game, it's slightly more noticeable with LUMA because sun shafts are stronger).
- Some objects in some levels disappear at certain camera angles (vanilla issue, lowering object details to high or below fixes it) (it can be fixed with the "Shuttle Bay Fix" mod, see below).
- Glass and shadow can flicker heavily, especially when there's multiple layers of it (vanilla issue), this is seemingly often caused by the "Real Lights plus Ultra Graphics Mod" mod if you have it.
- Due to Windows limitations, the game cursor will follow the OS SDR White Level (SDR content brightness) instead of the game's UI paper white. Set the Windows SDR content brightness setting to 31 (out of 100) to make it match ~203 nits, as Luma is set to by default.
- DLSS Preset J and K, as in many other games, have too much ghosting in Prey, due to reflections and volumetric effects.

# Compatibility:
This mod should work with any other mod for Prey, just be careful of what you install, because some of the most popular mods change very random stuff with the game, or its graphics config (they will still be compatible with Luma).
Replace their files with the Luma version if necessary, none of the game's mods rely on config changes, so the Luma version of the configs will work with them too, and Luma only changes what's strictly necessary and with careful research behind it.

# Suggested mods:
- Real Lights plus Ultra Graphics Mod - https://www.nexusmods.com/prey2017/mods/22
  This mod is seemingly great overall, though it changes some arguable stuff, like light placements, fire sprites (swapping them to a different color, which breaks their gameplay design) and adds a strong darkening effect when in combat (which does not look good in HDR).
  It seems to make glasses flicker in and out of view in certain levels from certain angles.
  Luma overrides the graphics menu changes from this mod, give that it exposes a lot of random and redundant stuff for user control.
  If you use it, do not apply the "autoexec.cfg" and "system.cfg" files from it, because they contain a myriad of random and unsafe changes (use the Luma version of the same files, which is compatible with this mod too).
  Also make sure to use the version without extra ReShade filters, as these are SDR only and are not great with Luma either way.
- 2023 - PREY - Quality of Life Enhancement Mod - https://www.nexusmods.com/prey2017/mods/99
  This shares some features with the mod above, but generally improves on them.
- Shuttle Bay Fix - https://www.nexusmods.com/prey2017/mods/140
  Fixes objects disappearing in the "Shuttle Bay" level if the Objects Quality setting was set to Very High.
- Touch-Up Mod - https://www.nexusmods.com/prey2017/mods/102
- No-Intro (Skip Startup - Splash Videos) - https://www.nexusmods.com/prey2017/mods/115
- Chairloader - The Prey Modding Framework - https://www.nexusmods.com/prey2017/mods/103
  Not for general usage but it's great for messing around.
- Sensitivity Sprint Scale - https://www.nexusmods.com/prey2017/mods/117

# References:
Join our discord: https://discord.gg/DNGfMZgH3f
Source Code: https://github.com/Filoppi/Prey-Luma

# Donations:
- https://www.buymeacoffee.com/realfiloppi (Pumbo)
- https://www.paypal.com/donate?hosted_button_id=BFT6XUJPRL6YC (Pumbo)
- https://ko-fi.com/ershin (Ersh)

# Thanks:
ShortFuse (support), Lilium (support), KoKlusz (testing), Musa (testing), crosire (support), FreshCloth (support), Regevitamins (support), MartysMods (support), Kaldaien (support), nd4spd (testing)

# Third party:
ReShade, ImGui, RenoDX, n3Dmigoto, DKUtil, Nvidia (DLSS), Fubaxiusz (Perfect Perspective), Oklab, Intel (Xe)GTAO, Darktable UCS, AMD RCAS, DICE (HDR tonemapper), Crytek (CryEngine) and Arkane (Prey)