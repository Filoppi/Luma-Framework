Luma is modding framework that facilitates improving graphics in DirectX 11 games.
It leverages the ReShade Addon system to add or modify rendering passes (and replace shaders (e.g. post processing)) through DirectX hooks.
While most of the generic shaders code is focused on HDR output support, there's a lot more to it already (like DLSS support), and no limit to what it can do.
Multiple games are already in the code, including a template project, and adding a new one is relatively easy.
Luma also acts as a graphics analyzer, having deep debugging capabilities (capturing all draw/dispatch commands and state changes in DX (e.g. SRVs/RTVs/UAVs/DSV/CBs etc etc)), download the graphics analyzer to just use that in any DX11 game.
<img width="854" height="848" alt="Mafia3DefinitiveEdition_awDHUAZNqF" src="https://github.com/user-attachments/assets/d8a27757-9f97-47c7-a896-08a99f62f597" />

# Development requirements
- Windows 11 (Windows 10 probably works fine too)
- Visual Studio 2026 (2022 works, older versions might work too)
- Windows 11 SDK 10.0.26100.0 (older versions work, but don't support HDR as well)

# Instructions
- (Prey only, optional otherwise) Set "VCPKG_ROOT" environment variable to your vcpkg installation folder if it wasn't already (download it from here "https://github.com/microsoft/vcpkg", the version integrated with Visual Studio doesn't seem to be as reliable).
- Install the latest VC++ redist before running the code (https://aka.ms/vs/17/release/vc_redist.x64.exe), we enforced users to update to the latest versions, but "_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR" could be defined to avoid that.
- Open "Luma.sln" and build it. Note that "Edit and Continue" build settings (\ZI) should not be used as they break the code patches generation (at least for projects that use DKUtil, like Prey).
- The code hot spots are in core.hpp and each game's main.cpp files.
- Luma uses the game project for developing and shipping mods. Simply toggle between DEVELOPMENT, TEST and PUBLISHING configurations to enable their respective features. They automatically spread to shaders on the next load/compile. Both Development-Release and Development-Debug builds should have debug symbols (and thus allow break points), however "Debug" builds have optimizations disabled and more logs, so they are exponentially slower and should only be used if debugging is required (VS might default to them, so change it manually).

# Adding a new game mod
- Add the template file from the ".\Templates\VisualStudio" folder to (e.g.) "%USERPROFILE%\Documents\Visual Studio 18\Templates\ProjectTemplates\Visual C++" (or "Visual Studio 2022" for VS 2022).
- Add a new project to the solution and select the Luma Template project, add it under ".\Source\Games". You can name it with the full game name, including spaces etc. You can manually check out the Template project that is already in the Luma solution for more information.
- Check the newly created main.cpp file and replace what you need to replace, everything is explained there. Check out other game's mods for further inspiration.
- Each mod's version is stored in "Globals::VERSION" and can be increased there.
- Win32 (x86/x32) games are compatible too, "BioShock 2" is an example of them, make sure to select the platform for it.
- Add an environment variable called "LUMA_GAME_NAME_BIN_PATH" (e.g. "LUMA_BIOSHOCK_2_BIN_PATH" for Bioshock 2), and make it point to the game's executable folder (where ReShade goes). Go to the project settings post-build event page and set it to copy the binaries in that folder (it will already be there as "LUMA_TEMPLATE_BIN_PATH").
- Go to the project settings debugging page and set the Command to the game executable path (e.g. "$(LUMA_PREY_BIN_PATH)\Prey.exe", without the "), so it's run and attached to when debugging (this often doesn't work on games with DRM).
- Install ReShade 6.6.0+ (or usually the current latest version) in the game's directory.
- Build the project and run it for debugging, it should automatically run the game with the mod loaded.

# Shaders development
- The mod automatically dumps the game's shaders in development builds.
- Luma shaders can be found in ".\Shaders\GameName" in development builds (starting from the repository root). Dumped shaders will go there, and hand created ones should also go there (unless they are generic, then they should go in the generic folder).
- Shader are saved and replaced by (cso/binary) hash.
- VSCode is suggested for editing them.
- In publishing and test builds, shaders will be loaded from the ".\Luma\GameName" folder, starting from the game binary folder (where the addon is).

# Releasing
Github actions automatically build all game projects and package them with their respective shaders (the Luma folder).
Once your mod is ready, make a PR to the original repository.
Make sure to test the mod in "Publishing" mode before wrapping it up, given that both the shaders and c++ code can change depending on the configuration.

# Comparison with RenoDX
Luma is similar to RenoDX (https://github.com/clshortfuse/renodx), where it got some inspiration from, but Luma is more focused on modding games deep down, like for example adding and replacing entire rendering techniques, adding DLSS or Ultrawide support etc. Porting simple mods between the two is relatively easy.

# Relationship with Starfield and Kingdom Come Deliverance Luma mods
The Luma Framework was born out of the modding code I originally wrote for Prey (Luma). I soon after realize that I could make it all generic and re-use many of its features on other games.
Starfield and Kingdom Come Deliverance Luma mods are not based on the Luma (generic) Framework and thus should not be confused with it. They do share some of the authors, and some of the code features (e.g. HDR stuff), but they are separate entities.

# Why ReShade?
It'd be possible to achieve the same without ReShade and game specific code hooks, by only using generic DirectX hooks, but it'd be exponentially more complicated (even if more performant) (some engines re-use render target textures for different purposes, so we couldn't easily tell which ones to upgrade, and ReShade offers settings serialization and a bunch of other features).
