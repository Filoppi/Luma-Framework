#define GAME_INSIDE 1

#define LUMA_PATCH_BYTECODE_SYNC 1

#include "..\..\Core\core.hpp"

#include "..\..\Core\includes\shader_patching.h"

namespace
{
   ShaderHashesList shader_hashes_LightingBuffer;
   ShaderHashesList shader_hashes_LightingBufferEnd; // First shader that always runs after
   ShaderHashesList shader_hashes_Materials;
   ShaderHashesList shader_hashes_MaterialsEnd;
   ShaderHashesList shader_hashes_TAA; // Always runs if the scene rendered
   ShaderHashesList shader_hashes_SwapchainCopy;

   std::vector<std::byte*> aspect_ratio_pattern_addresses;
   constexpr float default_aspect_ratio = 16.f / 9.f;
   float max_aspect_ratio = -1.f;
   bool disable_dither = true; // Probably not necessary with 16bit rendering and output?

   void PatchAspectRatio(float target_aspect_ratio = default_aspect_ratio)
   {
      if (target_aspect_ratio <= 0.0) target_aspect_ratio = default_aspect_ratio;

      for (std::byte* pattern_address : aspect_ratio_pattern_addresses)
      {
         DWORD old_protect;
         BOOL success = VirtualProtect(pattern_address, 1, PAGE_EXECUTE_READWRITE, &old_protect);
         if (success)
         {
            std::memcpy(pattern_address, &target_aspect_ratio, sizeof(float));
            DWORD temp_protect;
            VirtualProtect(pattern_address, 1, old_protect, &temp_protect);

            FlushInstructionCache(GetCurrentProcess(), pattern_address, 1);
         }
      }
   }

   void SetAspectRatioUpgrades(DeviceData& device_data, float target_aspect_ratio)
   {
      const std::unique_lock lock(device_data.resource_upgrades.mutex);
      device_data.resource_upgrades.texture_format_upgrades_2d_custom_aspect_ratios = { 16.f / 9.f, target_aspect_ratio };
   }
}

struct GameDeviceDataINSIDE final : public GameDeviceData
{
   com_ptr<ID3D11RenderTargetView> lighting_buffer_rtv; // Taken from the game
   com_ptr<ID3D11Texture2D> lighting_buffer_texture; // Luma clone
   com_ptr<ID3D11ShaderResourceView> lighting_buffer_srv; // Luma created
   UINT lighting_buffer_width = 0;
   UINT lighting_buffer_height = 0;

   com_ptr<ID3D11RenderTargetView> scene_color_rtv;

   SanitizeNaNsData sanitize_nans_data;

#if 0
   com_ptr<ID3D11BlendState> custom_blend_state;
#endif

   bool is_drawing_materials = false;
};

class INSIDE final : public Game
{
public:
   static const GameDeviceDataINSIDE& GetGameDeviceData(const DeviceData& device_data)
   {
      return *static_cast<const GameDeviceDataINSIDE*>(device_data.game);
   }
   static GameDeviceDataINSIDE& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceDataINSIDE*>(device_data.game);
   }

   void OnInit(bool async) override
   {
      shader_hashes_TAA.pixel_shaders.emplace(Shader::Hash_StrToNum("A141EA3E"));
      shader_hashes_TAA.pixel_shaders.emplace(Shader::Hash_StrToNum("DEBF1AC4"));

      shader_hashes_SwapchainCopy.pixel_shaders.emplace(Shader::Hash_StrToNum("8674BE1F"));

      shader_hashes_LightingBuffer.pixel_shaders =
      {
         std::stoul("00667169", nullptr, 16),
         std::stoul("D25D922C", nullptr, 16),
         std::stoul("A2D0F112", nullptr, 16),
         std::stoul("1D132483", nullptr, 16),
         std::stoul("FAD79F26", nullptr, 16),
         std::stoul("8CF70F59", nullptr, 16),
         std::stoul("3E3BC12E", nullptr, 16),
         //std::stoul("B6762984", nullptr, 16), // Ambiguous as it's also used for other purposes
         std::stoul("8DE93667", nullptr, 16),
         std::stoul("51D367BC", nullptr, 16),
         std::stoul("CE21BEA0", nullptr, 16),
         std::stoul("F31113C3", nullptr, 16),
         std::stoul("25DB2618", nullptr, 16),
         std::stoul("DD83BF95", nullptr, 16),
         std::stoul("6B79C71C", nullptr, 16),
         std::stoul("3072F024", nullptr, 16)
      };
      shader_hashes_LightingBufferEnd.pixel_shaders.emplace(Shader::Hash_StrToNum("B2662B89"));
      // The most common materials (e.g. common geometry and boy shaders)
      shader_hashes_Materials.pixel_shaders =
      {
         std::stoul("3F826D79", nullptr, 16),
         std::stoul("D2E14BDB", nullptr, 16)
      };
      shader_hashes_MaterialsEnd.pixel_shaders.emplace(Shader::Hash_StrToNum("BBC7E546"));

#if DEVELOPMENT
      forced_shader_names.emplace(Shader::Hash_StrToNum("B8090FB7"), "Clear");
      forced_shader_names.emplace(Shader::Hash_StrToNum("B6762984"), "Draw Black");
      forced_shader_names.emplace(Shader::Hash_StrToNum("BBC7E546"), "Draw White");

      // Not inclusive list
      forced_shader_names.emplace(Shader::Hash_StrToNum("82528FBE"), "Generate Reflections");
      forced_shader_names.emplace(Shader::Hash_StrToNum("63EB1954"), "Generate Reflections");
      forced_shader_names.emplace(Shader::Hash_StrToNum("5947A3FE"), "Generate Reflections");
      forced_shader_names.emplace(Shader::Hash_StrToNum("82528FBE"), "Generate Reflections (Transparent)");
      forced_shader_names.emplace(Shader::Hash_StrToNum("21047A13"), "Generate Reflections (Transparent)");
      forced_shader_names.emplace(Shader::Hash_StrToNum("1F271954"), "Generate Reflections (Transparent)");
      forced_shader_names.emplace(Shader::Hash_StrToNum("53E60842"), "Generate Reflections (Transparent)");

      forced_shader_names.emplace(Shader::Hash_StrToNum("0AAF0B02"), "Draw Motion Vectors");
      forced_shader_names.emplace(Shader::Hash_StrToNum("A6B71745"), "Downscale 1/2");

      forced_shader_names.emplace(Shader::Hash_StrToNum("2C49DEA4"), "Generate Bloom and Emissive");
      forced_shader_names.emplace(Shader::Hash_StrToNum("E34B6A4A"), "Downscale Bloom");
      forced_shader_names.emplace(Shader::Hash_StrToNum("45D205FB"), "Downscale Emissive");
      forced_shader_names.emplace(Shader::Hash_StrToNum("C41ACF9B"), "Downscale Emissive");
      forced_shader_names.emplace(Shader::Hash_StrToNum("4EEFB466"), "Upscale Emissive");
      forced_shader_names.emplace(Shader::Hash_StrToNum("10F78033"), "Mix Bloom and Emissive");

      forced_shader_names.emplace(Shader::Hash_StrToNum("7980933D"), "Generate Shadow Map (depth)");
      forced_shader_names.emplace(Shader::Hash_StrToNum("6C37B1D6"), "Generate Shadow Map (depth)");

      forced_shader_names.emplace(Shader::Hash_StrToNum("B2662B89"), "Linearize and Downscale Depth");
      forced_shader_names.emplace(Shader::Hash_StrToNum("A77CBE7A"), "Linearize Depth");

      //forced_shader_names.emplace(Shader::Hash_StrToNum("BBC7E546"), "Generate Light Shafts Mask"); // Same as "Draw White" above
      forced_shader_names.emplace(Shader::Hash_StrToNum("907C01ED"), "Generate Light Shafts");
      forced_shader_names.emplace(Shader::Hash_StrToNum("E267173D"), "Generate Light Shafts");
      forced_shader_names.emplace(Shader::Hash_StrToNum("B196A894"), "Generate Light Shafts");
      forced_shader_names.emplace(Shader::Hash_StrToNum("2BA9556B"), "Generate Light Shafts");
      forced_shader_names.emplace(Shader::Hash_StrToNum("424CD1EB"), "Generate Light Shafts");
      forced_shader_names.emplace(Shader::Hash_StrToNum("7BA896D6"), "Compose Light Shafts");

      forced_shader_names.emplace(Shader::Hash_StrToNum("F4CB7914"), "Draw Floor Screen Space Reflections");

      // Not inclusive list
      forced_shader_names.emplace(Shader::Hash_StrToNum("00667169"), "Draw Lighting Buffer");
      forced_shader_names.emplace(Shader::Hash_StrToNum("D25D922C"), "Draw Lighting Buffer");
      forced_shader_names.emplace(Shader::Hash_StrToNum("A2D0F112"), "Draw Lighting Buffer");
      forced_shader_names.emplace(Shader::Hash_StrToNum("1D132483"), "Draw Lighting Buffer");
      forced_shader_names.emplace(Shader::Hash_StrToNum("FAD79F26"), "Draw Lighting Buffer");
      forced_shader_names.emplace(Shader::Hash_StrToNum("8CF70F59"), "Draw Lighting Buffer with Shadow Map");
      forced_shader_names.emplace(Shader::Hash_StrToNum("3E3BC12E"), "Draw Lighting Buffer Phase 2");
      //forced_shader_names.emplace(Shader::Hash_StrToNum("B6762984"), "Draw Lighting Buffer Phase 2"); // Same as "Draw Black" above (it uses the alpha channel of the original R10G10B10A2 RT as stencil, it seems)
      forced_shader_names.emplace(Shader::Hash_StrToNum("8DE93667"), "Draw Lighting Buffer Phase 2");
      forced_shader_names.emplace(Shader::Hash_StrToNum("51D367BC"), "Draw Lighting Buffer Phase 2");
      forced_shader_names.emplace(Shader::Hash_StrToNum("CE21BEA0"), "Draw Lighting Buffer Phase 2");
      forced_shader_names.emplace(Shader::Hash_StrToNum("F31113C3"), "Draw Lighting Buffer Phase 2");
      forced_shader_names.emplace(Shader::Hash_StrToNum("25DB2618"), "Draw Lighting Buffer Phase 2");
      forced_shader_names.emplace(Shader::Hash_StrToNum("DD83BF95"), "Draw Lighting Buffer Phase 2");
      forced_shader_names.emplace(Shader::Hash_StrToNum("6B79C71C"), "Draw Lighting Buffer Phase 2");
      forced_shader_names.emplace(Shader::Hash_StrToNum("3072F024"), "Draw Lighting Buffer Phase 2");

      forced_shader_names.emplace(Shader::Hash_StrToNum("017E3BDB"), "Draw Baked Shadow?");

      // Materials color draws (albedo * lighting)
      forced_shader_names.emplace(Shader::Hash_StrToNum("6EC3069D"), "Geometry Emissive");
      forced_shader_names.emplace(Shader::Hash_StrToNum("0C957010"), "Geometry Emissive");

      forced_shader_names.emplace(Shader::Hash_StrToNum("A6DD1D4A"), "Geometry Transparent");
      forced_shader_names.emplace(Shader::Hash_StrToNum("6DF38EAF"), "Geometry Transparent");
      forced_shader_names.emplace(Shader::Hash_StrToNum("887768C1"), "Geometry Transparent");
      forced_shader_names.emplace(Shader::Hash_StrToNum("499E61A0"), "Geometry Transparent");
      forced_shader_names.emplace(Shader::Hash_StrToNum("F530F57E"), "Geometry Transparent");
      forced_shader_names.emplace(Shader::Hash_StrToNum("BF126A0F"), "Geometry Transparent");
      forced_shader_names.emplace(Shader::Hash_StrToNum("BAF71D9B"), "Geometry Transparent");
      forced_shader_names.emplace(Shader::Hash_StrToNum("DBDF5BE0"), "Geometry Transparent");
      forced_shader_names.emplace(Shader::Hash_StrToNum("FC49CA3E"), "Geometry Transparent");
      forced_shader_names.emplace(Shader::Hash_StrToNum("5DBD05FA"), "Geometry Transparent");
      forced_shader_names.emplace(Shader::Hash_StrToNum("187EA450"), "Geometry Transparent");
      forced_shader_names.emplace(Shader::Hash_StrToNum("418C1E59"), "Geometry Transparent");
      forced_shader_names.emplace(Shader::Hash_StrToNum("C4B728C1"), "Geometry Transparent");
      forced_shader_names.emplace(Shader::Hash_StrToNum("CA7517FF"), "Geometry Transparent");
      forced_shader_names.emplace(Shader::Hash_StrToNum("63BB6A97"), "Geometry Transparent");

      forced_shader_names.emplace(Shader::Hash_StrToNum("918A45F7"), "Geometry Masked");
      forced_shader_names.emplace(Shader::Hash_StrToNum("885B3C34"), "Geometry Masked");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42ABE850"), "Geometry Masked?");

      forced_shader_names.emplace(Shader::Hash_StrToNum("65DF6B49"), "Water");
      forced_shader_names.emplace(Shader::Hash_StrToNum("EC3A9A46"), "Water");
      forced_shader_names.emplace(Shader::Hash_StrToNum("FC028A8B"), "Water");
      forced_shader_names.emplace(Shader::Hash_StrToNum("3B5689CF"), "Water");
      forced_shader_names.emplace(Shader::Hash_StrToNum("26918C4A"), "Water");

      forced_shader_names.emplace(Shader::Hash_StrToNum("0406BFD1"), "Water Foam");

      forced_shader_names.emplace(Shader::Hash_StrToNum("0F462D3A"), "Water Surface");

      forced_shader_names.emplace(Shader::Hash_StrToNum("C6751363"), "Fog");

      forced_shader_names.emplace(Shader::Hash_StrToNum("101D8193"), "Decal");
      forced_shader_names.emplace(Shader::Hash_StrToNum("7D95CF84"), "Decal");
      forced_shader_names.emplace(Shader::Hash_StrToNum("C66DE2DD"), "Decal");

      forced_shader_names.emplace(Shader::Hash_StrToNum("C6F8102C"), "Baked Shadow or Decal?");

      forced_shader_names.emplace(Shader::Hash_StrToNum("B341FEEC"), "Particles");
      forced_shader_names.emplace(Shader::Hash_StrToNum("4D830665"), "Particles");
      forced_shader_names.emplace(Shader::Hash_StrToNum("E49EDA53"), "Particles");
      forced_shader_names.emplace(Shader::Hash_StrToNum("11D02B9C"), "Particles");
      forced_shader_names.emplace(Shader::Hash_StrToNum("43C9CF87"), "Particles");

      forced_shader_names.emplace(Shader::Hash_StrToNum("CF09813F"), "Screen Space Light");

      forced_shader_names.emplace(Shader::Hash_StrToNum("31DECB17"), "Generate DoF Phase 1");
      forced_shader_names.emplace(Shader::Hash_StrToNum("AD7B753B"), "Generate DoF Phase 2");

      forced_shader_names.emplace(Shader::Hash_StrToNum("8674BE1F"), "Swapchain Copy"); // Skipped if not needed (if the render and output resolution match)
#endif

      HMODULE module_handle = GetModuleHandle(nullptr); // Handle to the current executable
      auto dos_header = reinterpret_cast<PIMAGE_DOS_HEADER>(module_handle);
      auto nt_headers = reinterpret_cast<PIMAGE_NT_HEADERS>(reinterpret_cast<std::byte*>(module_handle) + dos_header->e_lfanew);

      std::byte* base = reinterpret_cast<std::byte*>(module_handle);
      std::size_t section_size = nt_headers->OptionalHeader.SizeOfImage;

      const std::vector<std::byte> pattern = { std::byte{0x39}, std::byte{0x8E}, std::byte{0xE3}, std::byte{0x3F} };

      aspect_ratio_pattern_addresses = System::ScanMemoryForPattern(base, section_size, pattern);

      // A bit weird to hardcode this, given that we might be windowed etc, but it's good to do it as early as possible
      int screen_width = GetSystemMetrics(SM_CXSCREEN);
      int screen_height = GetSystemMetrics(SM_CYSCREEN);
      float screen_aspect_ratio = static_cast<float>(screen_width) / static_cast<float>(screen_height);
      PatchAspectRatio(screen_aspect_ratio);

      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"ENABLE_LUMA", '1', true, false, "Enables all Luma's post processing modifications, to improve the image and output HDR", 1},
         {"ENABLE_FILM_GRAIN", '1', true, false, "Allows disabling the game's film grain effect", 1},
         {"ENABLE_LENS_DISTORTION", '1', true, false, "Allows disabling the game's lens distortion effect", 1},
         {"ENABLE_CHROMATIC_ABERRATION", '1', true, false, "Allows disabling the game's chromatic aberration effect", 1},
         {"ENABLE_BLACK_FLOOR_TWEAKS_TYPE", '1', true, false, "Allows customizing how the game handles the black floor. Set to 0 for the vanilla look. Set to 3 for increased visibility.", 3},
#if DEVELOPMENT || TEST
         {"ENABLE_FAKE_HDR", '0', true, false, "Enable a \"Fake\" HDR boosting effect (not usually necessary as the game's tonemapper can already extract highlights)", 1},
         {"ENABLE_DITHER", disable_dither ? '0' : '1', true, false, "", 1},
#endif
      };
      shader_defines_data.append_range(game_shader_defines_data);

      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0');
      // No gamma mismatch baked in the textures as the game never applied gamma, it was gamma from the beginning (likely as an extreme optimization)!
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('1');
      GetShaderDefineData(VANILLA_ENCODING_TYPE_HASH).SetDefaultValue('1');
      // Unity games almost always have a clear last shader, so we can pre-scale by the inverse of the UI brightness, so the UI can draw at a custom brightness.
      // The UI usually draws in linear space too, though that's an engine setting.
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('2');
      GetShaderDefineData(TEST_SDR_HDR_SPLIT_VIEW_MODE_NATIVE_IMPL_HASH).SetDefaultValue('1');

      native_shaders_definitions.emplace(CompileTimeStringHash("Sanitize Lighting"), ShaderDefinition{ "Luma_SanitizeLighting", reshade::api::pipeline_subobject_type::pixel_shader });
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceDataINSIDE;
   }

   void OnInitSwapchain(reshade::api::swapchain* swapchain) override
   {
      auto& device_data = *swapchain->get_device()->get_private_data<DeviceData>();
      if (device_data.game)
      {
         auto& game_device_data = *static_cast<GameDeviceDataINSIDE*>(device_data.game);

         game_device_data.sanitize_nans_data = {};

         int screen_width = GetSystemMetrics(SM_CXSCREEN);
         int screen_height = GetSystemMetrics(SM_CYSCREEN);
         float screen_aspect_ratio = static_cast<float>(screen_width) / static_cast<float>(screen_height);
         SetAspectRatioUpgrades(device_data, screen_aspect_ratio);
      }
   }

   std::unique_ptr<std::byte[]> PatchShaderBytecodeSync(const std::byte* code, size_t& size, reshade::api::pipeline_subobject_type type, uint64_t shader_hash, const std::byte* shader_object, size_t shader_object_size) override
   {
      using namespace System;

      if (type != reshade::api::pipeline_subobject_type::pixel_shader) return nullptr;

      std::unique_ptr<std::byte[]> new_code = nullptr;

      // float4(95.4307022, 97.5901031, 93.8368988, 91.6931)
      // This pattern is always present when dithering is present. Maybe not all but 99% of them, the rest shouldn't matter or even be detrimental (e.g. blue noise).
      // Used as (e.g.):
      // float4 ditheredColor = color.rgba + (frac(float4(95.4307022, 97.5901031, 93.8368988, 91.6931) * ditherSourceValue.x) * 0.00392156886);
      // Optionally followed by the second pattern below, applied to another variable.
      // One of the "scaling" patterns is always (almost always?) present too.
      // Only the first 3 elements matter, the 4th float is only used sometimes (the same applied to the other patterns below).
      const std::vector<uint8_t> pattern_dither_rand_1 = { 0x85, 0xDC, 0xBE, 0x42, 0x22, 0x2E, 0xC3, 0x42, 0x7E, 0xAC, 0xBB, 0x42, 0xDE, 0x62, 0xB7, 0x42 };
      // float4(75.0490875, 75.0495682, 75.0496063, 75.0496674)
      const std::vector<uint8_t> pattern_dither_rand_2 = { 0x22, 0x19, 0x96, 0x42, 0x61, 0x19, 0x96, 0x42, 0x66, 0x19, 0x96, 0x42, 0x6E, 0x19, 0x96, 0x42 };
      // float(0.00392156886)
      // Dither scales (mul) can be float(1), float3 or float4 (essentially repeated 1x, 3x or 4x times in a row). This is all we need to patch to disable dither.
      // The first 4 bytes are to highlights it's a literal single value.
      const std::vector<uint8_t> pattern_dither_scale_1_1c = { 0x01, 0x40, 0x00, 0x00, 0x81, 0x80, 0x80, 0x3B };
      // The first 4 bytes are to highlights it's a literal vector value.
      const std::vector<uint8_t> pattern_dither_scale_1_3c = { 0x02, 0x40, 0x00, 0x00, 0x81, 0x80, 0x80, 0x3B, 0x81, 0x80, 0x80, 0x3B, 0x81, 0x80, 0x80, 0x3B };
      const std::vector<uint8_t> pattern_dither_scale_1_4c = { 0x02, 0x40, 0x00, 0x00, 0x81, 0x80, 0x80, 0x3B, 0x81, 0x80, 0x80, 0x3B, 0x81, 0x80, 0x80, 0x3B, 0x81, 0x80, 0x80, 0x3B };
      const std::vector<uint8_t> pattern_dither_scale_1_4c_alt = { 0x02, 0x40, 0x00, 0x00, 0x81, 0x80, 0x80, 0x3B, 0x81, 0x80, 0x80, 0x3B, 0x81, 0x80, 0x80, 0x3B, 0x89, 0x88, 0x88, 0x3D }; // 4th literal is float(0.0666666701)
      // float(0.00196078443)
      const std::vector<uint8_t> pattern_dither_scale_2_3c = { 0x02, 0x40, 0x00, 0x00, 0x81, 0x80, 0x00, 0x3B, 0x81, 0x80, 0x00, 0x3B, 0x81, 0x80, 0x00, 0x3B };
      const std::vector<uint8_t> pattern_dither_scale_2_4c = { 0x02, 0x40, 0x00, 0x00, 0x81, 0x80, 0x00, 0x3B, 0x81, 0x80, 0x00, 0x3B, 0x81, 0x80, 0x00, 0x3B, 0x81, 0x80, 0x00, 0x3B };
      // float(0.000977517106)
      const std::vector<uint8_t> pattern_dither_scale_3_3c = { 0x02, 0x40, 0x00, 0x00, 0x08, 0x20, 0x80, 0x3A, 0x08, 0x20, 0x80, 0x3A, 0x08, 0x20, 0x80, 0x3A };
      const std::vector<uint8_t> pattern_dither_scale_3_4c = { 0x02, 0x40, 0x00, 0x00, 0x08, 0x20, 0x80, 0x3A, 0x08, 0x20, 0x80, 0x3A, 0x08, 0x20, 0x80, 0x3A, 0x08, 0x20, 0x80, 0x3A };
      // float(0.0666666701)
      const std::vector<uint8_t> pattern_dither_scale_4_3c = { 0x02, 0x40, 0x00, 0x00, 0x89, 0x88, 0x88, 0x3D, 0x89, 0x88, 0x88, 0x3D, 0x89, 0x88, 0x88, 0x3D };
      const std::vector<uint8_t> pattern_dither_scale_4_3c_alt = { 0x02, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x89, 0x88, 0x88, 0x3D, 0x89, 0x88, 0x88, 0x3D, 0x89, 0x88, 0x88, 0x3D };
      constexpr size_t float_pattern_channels = 3; // Scan for float3, not float4

      // Character material shaders don't have dither but instead have some other clearly recognizable geometry pattern (the registers change but the literals stay):
      // mad r2.x, r1.w, l(0.020835), l(-0.085133) // Hex: 01 40 00 00 5F AE AA 3C 01 40 00 00 36 5A AE BD
      // mad r2.x, r1.w, r2.x, l(0.180141) // Hex: 01 40 00 00 E2 76 38 3E
      // mad r2.x, r1.w, r2.x, l(-0.330299) // Hex: 01 40 00 00 04 1D A9 BE
      // mad r1.w, r1.w, r2.x, l(0.999866) // Hex: 01 40 00 00 38 F7 7F 3F
      // mul r2.x, r1.w, r1.z
      // mad r2.x, r2.x, l(-2.000000), l(1.570796) // 01 40 00 00 00 00 00 C0 01 40 00 00 DB 0F C9 3F
      // lt r2.y, |r1.y|, |r1.x|
      // and r2.x, r2.y, r2.x
      // mad r1.z, r1.z, r1.w, r2.x
      // lt r1.w, r1.y, -r1.y
      // and r1.w, r1.w, l(0xc0490fdb) // 01 40 00 00 DB 0F 49 C0
      // e.g. shader hashes D2E14BDB, 1AE7664E, FFF4B712 etc
      // This huge wildcard pattern is a bit unnecessary but whatever, it's very reliable as the compiler is deterministic and this was likely a function.
      static const std::vector<System::BytePattern> pattern_character_geometry = {
         0x01, 0x40, 0x00, 0x00, 0x5F, 0xAE, 0xAA, 0x3C, 0x01, 0x40, 0x00, 0x00, 0x36, 0x5A, 0xAE, 0xBD,
         // D2E14BDB: 32 00 00 09 12 00 10 00 02 00 00 00 3A 00 10 00 01 00 00 00 0A 00 10 00 02 00 00 00
         // 1AE7664E: 32 00 00 09 22 00 10 00 01 00 00 00 0A 00 10 00 01 00 00 00 1A 00 10 00 01 00 00 00
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         0x01, 0x40, 0x00, 0x00, 0xE2, 0x76, 0x38, 0x3E,
         // D2E14BDB: 32 00 00 09 12 00 10 00 02 00 00 00 3A 00 10 00 01 00 00 00 0A 00 10 00 02 00 00 00
         // 1AE7664E: 32 00 00 09 22 00 10 00 01 00 00 00 0A 00 10 00 01 00 00 00 1A 00 10 00 01 00 00 00
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         0x01, 0x40, 0x00, 0x00, 0x04, 0x1D, 0xA9, 0xBE,
         // D2E14BDB: 32 00 00 09 82 00 10 00 01 00 00 00 3A 00 10 00 01 00 00 00 0A 00 10 00 02 00 00 00
         // 1AE7664E: 32 00 00 09 12 00 10 00 01 00 00 00 0A 00 10 00 01 00 00 00 1A 00 10 00 01 00 00 00
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         0x01, 0x40, 0x00, 0x00, 0x38, 0xF7, 0x7F, 0x3F,
         // D2E14BDB: 38 00 00 07 12 00 10 00 02 00 00 00 3A 00 10 00 01 00 00 00 2A 00 10 00 01 00 00 00 32 00 00 09 12 00 10 00 02 00 00 00 0A 00 10 00 02 00 00 00
         // 1AE7664E: 38 00 00 07 22 00 10 00 01 00 00 00 3A 00 10 00 00 00 00 00 0A 00 10 00 01 00 00 00 32 00 00 09 22 00 10 00 01 00 00 00 1A 00 10 00 01 00 00 00
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         0x01, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0,
         0x01, 0x40, 0x00, 0x00, 0xDB, 0x0F, 0xC9, 0x3F,
         // D2E14BDB: 31 00 00 09 22 00 10 00 02 00 00 00 1A 00 10 80 81 00 00 00 01 00 00 00 0A 00 10 80 81 00 00 00 01 00 00 00 01 00 00 07 12 00 10 00 02 00 00 00 1A 00 10 00 02 00 00 00 0A 00 10 00 02 00 00 00 32 00 00 09 42 00 10 00 01 00 00 00 2A 00 10 00 01 00 00 00 3A 00 10 00 01 00 00 00 0A 00 10 00 02 00 00 00 31 00 00 08 82 00 10 00 01 00 00 00 1A 00 10 00 01 00 00 00 1A 00 10 80 41 00 00 00 01 00 00 00 01 00 00 07 82 00 10 00 01 00 00 00 3A 00 10 00 01 00 00 00
         // 1AE7664E: 31 00 00 09 42 00 10 00 01 00 00 00 2A 00 10 80 81 00 00 00 00 00 00 00 1A 00 10 80 81 00 00 00 00 00 00 00 01 00 00 07 22 00 10 00 01 00 00 00 2A 00 10 00 01 00 00 00 1A 00 10 00 01 00 00 00 32 00 00 09 82 00 10 00 00 00 00 00 3A 00 10 00 00 00 00 00 0A 00 10 00 01 00 00 00 1A 00 10 00 01 00 00 00 31 00 00 08 12 00 10 00 01 00 00 00 2A 00 10 00 00 00 00 00 2A 00 10 80 41 00 00 00 00 00 00 00 01 00 00 07 12 00 10 00 01 00 00 00 0A 00 10 00 01 00 00 00
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,

         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,

         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,

         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         ANY, ANY, ANY, ANY,
         0x01, 0x40, 0x00, 0x00, 0xDB, 0x0F, 0x49, 0xC0
      };

      // Almost all material and lights shaders have dithering, so we can use this pattern to add a saturate on alpha and max(0, rgb) on color. The ones that don't are manually patched (at least the relevant ones we caught that showed issues)
      std::vector<std::byte*> matches_dither_rand_1 = System::ScanMemoryForPattern(reinterpret_cast<const std::byte*>(code), size, reinterpret_cast<const std::byte*>(pattern_dither_rand_1.data()), pattern_dither_rand_1.size() / 4 * float_pattern_channels, true);

      std::vector<std::byte*> matches_characters_geometry_1;
      // No need to bother if we already found other matches, we won't do anything with these matches
      if (matches_dither_rand_1.empty())
      {
         matches_characters_geometry_1 = System::ScanMemoryForPattern(reinterpret_cast<const std::byte*>(code), size, pattern_character_geometry, true);
      }

      if (!matches_dither_rand_1.empty() || !matches_characters_geometry_1.empty())
      {
         std::vector<uint8_t> appended_patch;

         constexpr bool enable_unorm_emulation = true;
         if (enable_unorm_emulation)
         {
            std::vector<uint32_t> mov_sat_o0w_o0w = ShaderPatching::GetMovInstruction(D3D10_SB_OPERAND_TYPE_OUTPUT, 0, D3D10_SB_OPERAND_TYPE_OUTPUT, 0, true);
            appended_patch.insert(appended_patch.end(), reinterpret_cast<uint8_t*>(mov_sat_o0w_o0w.data()), reinterpret_cast<uint8_t*>(mov_sat_o0w_o0w.data()) + mov_sat_o0w_o0w.size() * sizeof(uint32_t));
            std::vector<uint32_t> max_o0xyz_o0xyz_0 = ShaderPatching::GetMaxInstruction(D3D10_SB_OPERAND_TYPE_OUTPUT, 0, D3D10_SB_OPERAND_TYPE_OUTPUT, 0);
            appended_patch.insert(appended_patch.end(), reinterpret_cast<uint8_t*>(max_o0xyz_o0xyz_0.data()), reinterpret_cast<uint8_t*>(max_o0xyz_o0xyz_0.data()) + max_o0xyz_o0xyz_0.size() * sizeof(uint32_t));
         }

         // Allocate new buffer and copy original shader code, then append the new code to fix UNORM to FLOAT texture upgrades
         // This emulates UNORM render target behaviour on FLOAT render targets (from texture upgrades), without limiting the rgb color range.
         // o0.rgb = max(o0.rgb, 0); // max is 0x34
         // o0.w = saturate(o0.w); // mov is 0x36
         new_code = std::make_unique<std::byte[]>(size + appended_patch.size());

         // Pattern to search for: 3E 00 00 01 (the last byte is the size, and the minimum is 1 (the unit is 4 bytes), given it also counts for the opcode and it's own size byte
         const std::vector<std::byte> return_pattern = { std::byte{0x3E}, std::byte{0x00}, std::byte{0x00}, std::byte{0x01} };

         // Append before the ret instruction if there's one at the end (there might not be?)
         // Our patch shouldn't pre-include a ret value (though it'd probably work anyway)!
         // Note that we could also just remove the return instruction and the shader would compile fine anyway? Unless the shader had any branches (if we added one, we should force add return!)
         if (!appended_patch.empty() && code[size - return_pattern.size()] == return_pattern[0] && code[size - return_pattern.size() + 1] == return_pattern[1] && code[size - return_pattern.size() + 2] == return_pattern[2] && code[size - return_pattern.size() + 3] == return_pattern[3])
         {
            size_t insert_pos = size - return_pattern.size();
            // Copy everything before pattern
            std::memcpy(new_code.get(), code, insert_pos);
            // Insert the patch
            std::memcpy(new_code.get() + insert_pos, appended_patch.data(), appended_patch.size());
            // Copy the rest (including the return instruction)
            std::memcpy(new_code.get() + insert_pos + appended_patch.size(), code + insert_pos, size - insert_pos);
         }
         // Append patch at the end
         else
         {
            std::memcpy(new_code.get(), code, size);
            std::memcpy(new_code.get() + size, appended_patch.data(), appended_patch.size());
         }

         if (disable_dither && !matches_dither_rand_1.empty())
         {
            auto PatchDitherScale = [&](std::byte* start, size_t max_size, const std::vector<uint8_t>& pattern) -> bool {
               auto matches = System::ScanMemoryForPattern(start, max_size, reinterpret_cast<const std::byte*>(pattern.data()), pattern.size());
               for (std::byte* match : matches) {
                  size_t offset = match - code;
                  // Zero out all bytes after the first 4 bytes
                  std::memset(new_code.get() + offset + 4, 0, pattern.size() - 4);
               }
               return !matches.empty();
               };

            bool pattern_dither_scale_found = false;
            const size_t size_offset = matches_dither_rand_1[0] - code; // They are always after that pattern
            pattern_dither_scale_found |= PatchDitherScale(matches_dither_rand_1[0], size - size_offset, pattern_dither_scale_1_1c);
            pattern_dither_scale_found |= PatchDitherScale(matches_dither_rand_1[0], size - size_offset, pattern_dither_scale_1_4c); // Replace the larger channel patters before otherwise it could prevent follow up channel patterns ones from being found
            pattern_dither_scale_found |= PatchDitherScale(matches_dither_rand_1[0], size - size_offset, pattern_dither_scale_1_4c_alt);
            pattern_dither_scale_found |= PatchDitherScale(matches_dither_rand_1[0], size - size_offset, pattern_dither_scale_1_3c);

            // The other patterns are usually mutually exclusive
            if (!pattern_dither_scale_found)
            {
               pattern_dither_scale_found |= PatchDitherScale(matches_dither_rand_1[0], size - size_offset, pattern_dither_scale_2_4c);
               pattern_dither_scale_found |= PatchDitherScale(matches_dither_rand_1[0], size - size_offset, pattern_dither_scale_2_3c);
               if (!pattern_dither_scale_found)
               {
                  pattern_dither_scale_found |= PatchDitherScale(matches_dither_rand_1[0], size - size_offset, pattern_dither_scale_3_4c);
                  pattern_dither_scale_found |= PatchDitherScale(matches_dither_rand_1[0], size - size_offset, pattern_dither_scale_3_3c);
                  if (!pattern_dither_scale_found)
                  {
                     pattern_dither_scale_found |= PatchDitherScale(matches_dither_rand_1[0], size - size_offset, pattern_dither_scale_4_3c_alt);
                     pattern_dither_scale_found |= PatchDitherScale(matches_dither_rand_1[0], size - size_offset, pattern_dither_scale_4_3c);
                  }
               }
            }
         }

         size += appended_patch.size();
      }

      return new_code;
   }

   // The entire game rendering pipeline was SDR
   // It goes like this:
   // -Render flipped world reflections with simple geometry (when there's a water body in view eg)
   // -Render normal maps
   //  Render some kind of low res depth map
   // -Render lighting (R10G10B10A2) (flipped, 1 is full shadow, without manual clamping shadow can
   // go beyond 1 if render targets are float) -Render color scene (material albedo * lighting,
   // pretty much) -Render additive lights -Draw motion vectors for dynamic objects (rest is
   // calculated from the camera I think) -TAA -Bloom and emissive color -Tonemap -Swapchain output
   // (possibly draws black bars)
   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& game_device_data = *static_cast<GameDeviceDataINSIDE*>(device_data.game);

      if (original_shader_hashes.Contains(shader_hashes_LightingBuffer))
      {
         com_ptr<ID3D11RenderTargetView> rtv;
         native_device_context->OMGetRenderTargets(1, &rtv, nullptr);
         if (rtv.get())
         {
            if (game_device_data.lighting_buffer_rtv != rtv)
            {
               game_device_data.lighting_buffer_rtv = rtv; // We don't clean this usually, to make sure we always find it in a new frame

               game_device_data.lighting_buffer_srv = nullptr;
               game_device_data.lighting_buffer_texture = nullptr;
               game_device_data.lighting_buffer_width = 0;
               game_device_data.lighting_buffer_height = 0;
            }
         }
      }

      if (original_shader_hashes.Contains(shader_hashes_LightingBufferEnd))
      {
         ASSERT_ONCE(game_device_data.lighting_buffer_rtv.get()); // Can happen on boot when first loading
         game_device_data.is_drawing_materials = true;

         // Sanitize the lighting buffer, if it had values beyond 1 (which happens), it'd mean lighting would go subtractive/negative (it was stored with flipped values)
         if (device_data.native_pixel_shaders[CompileTimeStringHash("Sanitize Lighting")].get() && game_device_data.lighting_buffer_rtv.get())
         {
            if (!game_device_data.lighting_buffer_srv.get())
            {
               com_ptr<ID3D11Resource> resource;
               game_device_data.lighting_buffer_rtv->GetResource(&resource);
               if (resource)
               {
                  game_device_data.lighting_buffer_texture = CloneTexture<ID3D11Texture2D>(native_device, resource.get(), DXGI_FORMAT_UNKNOWN, D3D11_BIND_SHADER_RESOURCE, D3D11_BIND_RENDER_TARGET, false, false, native_device_context);

                  if (game_device_data.lighting_buffer_texture)
                  {
                     HRESULT hr = native_device->CreateShaderResourceView(game_device_data.lighting_buffer_texture.get(), nullptr, &game_device_data.lighting_buffer_srv);
                     ASSERT_ONCE(SUCCEEDED(hr));

                     com_ptr<ID3D11Texture2D> texture_2d;
                     resource->QueryInterface(&texture_2d);
                     if (texture_2d)
                     {
                        D3D11_TEXTURE2D_DESC texture_2d_desc;
                        texture_2d->GetDesc(&texture_2d_desc);
                        game_device_data.lighting_buffer_width = texture_2d_desc.Width;
                        game_device_data.lighting_buffer_height = texture_2d_desc.Height;
                     }
                  }
               }
            }

            if (game_device_data.lighting_buffer_srv.get() && game_device_data.lighting_buffer_width != 0)
            {
               DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack; // Use full mode because setting the RTV here might unbind the same resource being bound as SRV
               draw_state_stack.Cache(native_device_context, device_data.uav_max_count);

               com_ptr<ID3D11Resource> rtv_resource;
               game_device_data.lighting_buffer_rtv->GetResource(&rtv_resource);
               native_device_context->CopyResource(game_device_data.lighting_buffer_texture.get(), rtv_resource.get());

               // Note: this is possibly not needed anymore given we patch some shaders that draw on the lighting buffer to avoid nans (we could patch even more).
               if (test_index != 19)
                  DrawCustomPixelShader(native_device_context,
                     device_data.default_depth_stencil_state.get(),
                     device_data.default_blend_state.get(),
                     nullptr,
                     device_data.native_vertex_shaders[CompileTimeStringHash("Copy VS")].get(),
                     device_data.native_pixel_shaders[CompileTimeStringHash("Sanitize Lighting")].get(),
                     game_device_data.lighting_buffer_srv.get(),
                     game_device_data.lighting_buffer_rtv.get(),
                     game_device_data.lighting_buffer_width, game_device_data.lighting_buffer_height);

#if DEVELOPMENT
               const std::shared_lock lock_trace(s_mutex_trace);
               if (trace_running)
               {
                  const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
                  TraceDrawCallData trace_draw_call_data;
                  trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
                  trace_draw_call_data.command_list = native_device_context;
                  trace_draw_call_data.custom_name = "Sanitize Lighting Buffer";
                  cmd_list_data.trace_draw_calls_data.insert(cmd_list_data.trace_draw_calls_data.end() - 1, trace_draw_call_data);

                  trace_draw_call_data.custom_name = "Start Drawing Scene Color";
                  cmd_list_data.trace_draw_calls_data.push_back(trace_draw_call_data);
               }
#endif

               draw_state_stack.Restore(native_device_context);
            }
         }
#if DEVELOPMENT // Make sure to clear the texture in case shaders failed to re-compile, to avoid keeping unnecessary resources references
         else
         {
            game_device_data.lighting_buffer_srv = nullptr;
            game_device_data.lighting_buffer_texture = nullptr;
            game_device_data.lighting_buffer_width = 0;
            game_device_data.lighting_buffer_height = 0;
         }
#endif
      }
      else if (original_shader_hashes.Contains(shader_hashes_MaterialsEnd) && game_device_data.is_drawing_materials)
      {
         ASSERT_ONCE(game_device_data.scene_color_rtv.get());
         game_device_data.is_drawing_materials = false;

         DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack; // Use full mode because setting the RTV here might unbind the same resource being bound as SRV
         DrawStateStack<DrawStateStackType::Compute> compute_state_stack;
         draw_state_stack.Cache(native_device_context, device_data.uav_max_count);
         compute_state_stack.Cache(native_device_context, device_data.uav_max_count);

         // Avoids nans spreading over the transparency phase and TAA etc (character shaders occasionally spit out some)
         // Note: this is likely not needed anymore given we patch (almost) all shaders that draw on the scene buffer to avoid nans.
         if (test_index != 18)
            SanitizeNaNs(native_device, native_device_context, game_device_data.scene_color_rtv.get(), device_data, game_device_data.sanitize_nans_data, true);

#if DEVELOPMENT
         const std::shared_lock lock_trace(s_mutex_trace);
         if (trace_running)
         {
            const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
            TraceDrawCallData trace_draw_call_data;
            trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
            trace_draw_call_data.command_list = native_device_context;

            trace_draw_call_data.custom_name = "Stop Drawing Scene Color";
            cmd_list_data.trace_draw_calls_data.push_back(trace_draw_call_data);

            trace_draw_call_data.custom_name = "Start Drawing Transparency"; // TODO: add stop event for these (when post process ends)
            cmd_list_data.trace_draw_calls_data.push_back(trace_draw_call_data);

            trace_draw_call_data.custom_name = "Sanitize Scene Color NaNs";
            // Re-use the RTV data for simplicity
            GetResourceInfo(game_device_data.scene_color_rtv.get(), trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);
            cmd_list_data.trace_draw_calls_data.insert(cmd_list_data.trace_draw_calls_data.end() - 3, trace_draw_call_data); // Add this one before any other
         }
#endif

         // Restore the compute state first, given it might be considered as output and take over SR bindings of the same resource
         compute_state_stack.Restore(native_device_context);
         draw_state_stack.Restore(native_device_context);
      }
      else if (game_device_data.is_drawing_materials)
      {
         if (original_shader_hashes.Contains(shader_hashes_Materials))
         {
            com_ptr<ID3D11RenderTargetView> rtv;
            native_device_context->OMGetRenderTargets(1, &rtv, nullptr);
            if (rtv.get())
            {
               ASSERT_ONCE(!game_device_data.scene_color_rtv.get() || game_device_data.scene_color_rtv == rtv); // We don't expect this to change within the materials rendering
               game_device_data.scene_color_rtv = rtv; // We don't clean this usually, to make sure we always find it in a new frame
            }
         }

#if 0 // This doesn't work won't prevent NaNs in the dest texture from persisting, there's no blend mode to force the source to override the dest value without having the dest spread/keep its nans // TODO: delete
         com_ptr<ID3D11BlendState> blend_state;
         FLOAT blend_factor[4] = { 1.f, 1.f, 1.f, 1.f };
         UINT blend_sample_mask;
         native_device_context->OMGetBlendState(&blend_state, blend_factor, &blend_sample_mask);
         if (blend_state && test_index != 17)
         {
            D3D11_BLEND_DESC blend_desc;
            blend_state->GetDesc(&blend_desc);

            // Change the blend state to skip the destination values, so we don't carry around NaNs from the dest if we are simply overwriting it
            // (this blend combination was identical to blending disabled, but still allowed NaNs to persist, which would only happen if we upgraded textures from UNORM to FLOAT)
            if (blend_desc.RenderTarget[0].BlendEnable
               && blend_desc.RenderTarget[0].SrcBlend == D3D11_BLEND_ONE
               && blend_desc.RenderTarget[0].DestBlend == D3D11_BLEND_ZERO
               && blend_desc.RenderTarget[0].BlendOp == D3D11_BLEND_OP_ADD
               && blend_desc.RenderTarget[0].SrcBlendAlpha == D3D11_BLEND_ZERO
               && blend_desc.RenderTarget[0].DestBlendAlpha == D3D11_BLEND_ZERO
               && blend_desc.RenderTarget[0].BlendOpAlpha == D3D11_BLEND_OP_ADD
               && blend_desc.RenderTarget[0].RenderTargetWriteMask == D3D11_COLOR_WRITE_ENABLE_ALL)
            {
               if (!game_device_data.custom_blend_state) // TODO: move initialziation
               {
                  ASSERT_ONCE(!blend_desc.IndependentBlendEnable || !blend_desc.RenderTarget[1].BlendEnable);

                  com_ptr<ID3D11RenderTargetView> rtvs[2];
                  native_device_context->OMGetRenderTargets(2, &rtvs[0], nullptr);
                  ASSERT_ONCE(!rtvs[1]);

                  blend_desc.RenderTarget->DestBlend = D3D11_BLEND_BLEND_FACTOR;
                  blend_desc.RenderTarget->DestBlendAlpha = D3D11_BLEND_BLEND_FACTOR;

                  native_device->CreateBlendState(&blend_desc, &game_device_data.custom_blend_state);
               }

               if (test_index != 18)
               {
                  blend_factor[0] = 0.f;
                  blend_factor[1] = 0.f;
                  blend_factor[2] = 0.f;
                  blend_factor[3] = 0.f;
                  native_device_context->OMSetBlendState(game_device_data.custom_blend_state.get(), blend_factor, blend_sample_mask);
               }
               // Test disable blending completely
               else
               {
                  native_device_context->OMSetBlendState(nullptr, blend_factor, blend_sample_mask);
               }
            }
         }
#endif
      }

      if (original_shader_hashes.Contains(shader_hashes_TAA))
      {
         ASSERT_ONCE(!game_device_data.is_drawing_materials); // Can happen on boot when first loading
         game_device_data.is_drawing_materials = false;
         device_data.has_drawn_main_post_processing = true;

         return DrawOrDispatchOverrideType::None;
      }

      // Update the game's render resolution and texture upgrade aspect ratio filter based on the final swapchain shader, which converts from the render resolution to the output one
      if (original_shader_hashes.Contains(shader_hashes_SwapchainCopy))
      {
         com_ptr<ID3D11ShaderResourceView> srv;
         native_device_context->PSGetShaderResources(0, 1, &srv);
         if (srv.get())
         {
            com_ptr<ID3D11Resource> resource;
            srv->GetResource(&resource);

            com_ptr<ID3D11Texture2D> texture_2d;
            HRESULT hr = resource->QueryInterface(&texture_2d);
            if (SUCCEEDED(hr) && texture_2d)
            {
               D3D11_TEXTURE2D_DESC texture_2d_desc;
               texture_2d->GetDesc(&texture_2d_desc);

               device_data.render_resolution.x = texture_2d_desc.Width;
               device_data.render_resolution.y = texture_2d_desc.Height;
            }
         }

         return DrawOrDispatchOverrideType::None;
      }

      return DrawOrDispatchOverrideType::None;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      ASSERT_ONCE(!game_device_data.is_drawing_materials);
      game_device_data.is_drawing_materials = false;

      if (!device_data.has_drawn_main_post_processing)
      {
         game_device_data.lighting_buffer_rtv.reset();
         game_device_data.scene_color_rtv.reset();
      }
      device_data.has_drawn_main_post_processing = false;
   }

   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;

      reshade::get_config_value(runtime, NAME, "HDRIntensity", cb_luma_global_settings.GameSettings.HDRIntensity);
      reshade::get_config_value(runtime, NAME, "HighlightsDesaturation", cb_luma_global_settings.GameSettings.HighlightsDesaturation);
      // "device_data.cb_luma_global_settings_dirty" should already be true at this point

      reshade::get_config_value(runtime, NAME, "CustomAspectRatio", max_aspect_ratio);
      // Don't re-patch if the custom AR was disabled, we'll let the mod default to the output AR
      if (max_aspect_ratio > 0.f)
      {
         PatchAspectRatio(max_aspect_ratio);
      }

      reshade::get_config_value(runtime, NAME, "DisableDither", disable_dither);
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;

      ImGui::NewLine();

      ImGui::SetNextItemOpen(true, ImGuiCond_Once);
      if (ImGui::TreeNode("Advanced Settings"))
      {
         if (cb_luma_global_settings.DisplayMode == DisplayModeType::HDR)
         {
            if (ImGui::SliderFloat("HDR Intensity", &cb_luma_global_settings.GameSettings.HDRIntensity, 0.f, 2.f))
            {
               reshade::set_config_value(runtime, NAME, "HDRIntensity", cb_luma_global_settings.GameSettings.HDRIntensity);
            }
            DrawResetButton(cb_luma_global_settings.GameSettings.HDRIntensity, 1.f, "HDRIntensity", runtime);

            if (ImGui::SliderFloat("Highlights Desaturation", &cb_luma_global_settings.GameSettings.HighlightsDesaturation, 0.f, 1.f))
            {
               reshade::set_config_value(runtime, NAME, "HighlightsDesaturation", cb_luma_global_settings.GameSettings.HighlightsDesaturation);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
               ImGui::SetTooltip("The higher the value, the closer we are to the vanilla look (results might still be different due to HDR tonemapping)");
            }
            DrawResetButton(cb_luma_global_settings.GameSettings.HighlightsDesaturation, 0.4f, "HighlightsDesaturation", runtime);

            ImGui::NewLine();
         }

         bool custom_aspect_ratio_enabled = max_aspect_ratio > 0.f;
         const float output_aspect_ratio = device_data.output_resolution.x / device_data.output_resolution.y;
         if (ImGui::Checkbox("Custom Aspect Ratio", &custom_aspect_ratio_enabled))
         {
            if (custom_aspect_ratio_enabled)
               max_aspect_ratio = output_aspect_ratio; // Start from the output AR (it's probably already patched in)
            else
               max_aspect_ratio = -1.f;
            PatchAspectRatio(output_aspect_ratio);
            SetAspectRatioUpgrades(device_data, output_aspect_ratio);
            reshade::set_config_value(runtime, NAME, "CustomAspectRatio", max_aspect_ratio);
         }
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         {
            ImGui::SetTooltip("Luma automatically unlocks the aspect ratio for the game from 16:9 on boot,"
               "\nhowever the game was designed for 16:9, and some objects might be missing from the edges on wider aspect ratios, other might end up being visible before they were meant to,"
               " this allows you to customize it.\nFor the change to apply, it's necessary to change the resolution or fullscreen state in the graphics settings menu.\nIf HDR seems disabled after changing the aspect ratio, restart the game"); // TODO: needs "enable_indirect_texture_format_upgrades" as RTs are resized before the swapchain when we change resolution (aspect ratio) (maybe)
         }

         if (custom_aspect_ratio_enabled)
         {
            // Going beyond the window aspect ratio won't work,
            // nor going below 16:9 (the game never allows rendering below that).
            if (ImGui::SliderFloat("Custom Aspect Ratio", &max_aspect_ratio, default_aspect_ratio, output_aspect_ratio))
            {
               reshade::set_config_value(runtime, NAME, "CustomAspectRatio", max_aspect_ratio);
               PatchAspectRatio(max_aspect_ratio);
               SetAspectRatioUpgrades(device_data, max_aspect_ratio);
            }
            if (DrawResetButton(max_aspect_ratio, output_aspect_ratio, "CustomAspectRatio", runtime))
            {
               reshade::set_config_value(runtime, NAME, "CustomAspectRatio", max_aspect_ratio);
               PatchAspectRatio(max_aspect_ratio);
               SetAspectRatioUpgrades(device_data, max_aspect_ratio);
            }
         }

         ImGui::NewLine();
         if (ImGui::Checkbox("Disable Dithering", &disable_dither))
         {
            reshade::set_config_value(runtime, NAME, "DisableDither", disable_dither);

            const std::shared_lock lock(s_mutex_shader_defines);
            GetShaderDefineData(char_ptr_crc32("ENABLE_DITHER")).SetDefaultValue(disable_dither ? '0' : '1');
            GetShaderDefineData(char_ptr_crc32("ENABLE_DITHER")).SetValue(disable_dither ? '0' : '1');
            defines_need_recompilation = true;
            ShaderDefineData::Save(shader_defines_data, NAME_ADVANCED_SETTINGS);
         }
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         {
            ImGui::SetTooltip("Disables dithering on almost all shaders of this game. Might introduce banding and break the TAA reconstruction. Requires restart");
         }

         ImGui::TreePop();
      }
   }

#if DEVELOPMENT
   void DrawImGuiDevSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;
   }
#endif

   void PrintImGuiAbout() override
   {
      ImGui::Text("Luma for \"INSIDE\" is developed by Pumbo and is open source and free.\n"
         "It adds HDR rendering and output, improved SDR output, improved tonemapping,\n"
         "and additionally it adds ultrawide compatibility.\n"
         "If you enjoy it, consider donating.", "");

      const auto button_color = ImGui::GetStyleColorVec4(ImGuiCol_Button);
      const auto button_hovered_color = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
      const auto button_active_color = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
      ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(70, 134, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(70 + 9, 134 + 9, 0, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(70 + 18, 134 + 18, 0, 255));
      static const std::string donation_link_pumbo = std::string("Buy Pumbo a Coffee on buymeacoffee ") + std::string(ICON_FK_OK);
      if (ImGui::Button(donation_link_pumbo.c_str()))
      {
         system("start https://buymeacoffee.com/realfiloppi");
      }
      static const std::string donation_link_pumbo_2 = std::string("Buy Pumbo a Coffee on ko-fi ") + std::string(ICON_FK_OK);
      if (ImGui::Button(donation_link_pumbo_2.c_str()))
      {
         system("start https://ko-fi.com/realpumbo");
      }
      ImGui::PopStyleColor(3);

      ImGui::NewLine();
      // Restore the previous color, otherwise the state we set would persist even if we popped it
      ImGui::PushStyleColor(ImGuiCol_Button, button_color);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, button_hovered_color);
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, button_active_color);
      static const std::string social_link = std::string("Join our \"HDR Den\" Discord ") + std::string(ICON_FK_SEARCH);
      if (ImGui::Button(social_link.c_str()))
      {
         // Unique link for Luma by Pumbo (to track the origin of people joining), do not share for other purposes
         static const std::string obfuscated_link = std::string("start https://discord.gg/J9fM") + std::string("3EVuEZ");
         system(obfuscated_link.c_str());
      }
      static const std::string contributing_link = std::string("Contribute on Github ") + std::string(ICON_FK_FILE_CODE);
      if (ImGui::Button(contributing_link.c_str()))
      {
         system("start https://github.com/Filoppi/Luma-Framework");
      }
      ImGui::PopStyleColor(3);

      ImGui::NewLine();
      ImGui::Text("Credits:"
         "\n\nMain:"
         "\nPumbo"

         "\n\nThird Party:"
         "\nReShade"
         "\nImGui"
         "\nRenoDX"
         "\n3Dmigoto"
         "\nOklab"
         "\nDICE (HDR tonemapper)"
         , "");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "INSIDE Luma mod");
      Globals::VERSION = 2;

      // Unity apparently never uses these
      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;

      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::CustomAspectRatio | (uint32_t)TextureFormatUpgrades2DSizeFilters::RenderAspectRatio;
      texture_upgrade_formats = {
            reshade::api::format::r8g8b8a8_unorm,
            reshade::api::format::r8g8b8a8_unorm_srgb,
            reshade::api::format::r8g8b8a8_typeless,
            // Likely unnecessary but won't hurt
            reshade::api::format::r11g11b10_float,

            // These are used by lighting, which wouldn't really need to be upgraded as it 10bit in log space are already plenty, and possibly even better than FP16,
            // however, they are also used by bloom so we need to upgrade them (until we find a way to selectively upgrade textures)
            reshade::api::format::r10g10b10a2_typeless,
            reshade::api::format::r10g10b10a2_unorm,
      };

      // There's many...
      redirected_shader_hashes["Tonemap"] =
      {
         "02C7E7CB",
         "06132AC1",
         "07FE2DEC",
         "0AE21975",
         "0EF00A11",
         "10E5A1DF",
         "18010638",
         "1926C2D3",
         "21B3A200",
         "2337502D",
         "2D6B78F6",
         "2FE2C060",
         "30074255",
         "343CD73C",
         "3796FF82",
         "38A7430E",
         "3A63AE73",
         "3E3A55F7",
         "4030784C",
         "48E38F85",
         "493DA507",
         "49BDA2EC",
         "50873049",
         "515B88D8",
         "519DF6E7",
         "51DF35B3",
         "59C674F6",
         "6787B520",
         "6792E8D3",
         "6956455B",
         "7003995F",
         "746E571C",
         "82A02335",
         "84F1D7F4",
         "8589AC8E",
         "87E4E17A",
         "8847F08D",
         "8DEE69CB",
         "90337E76",
         "91970ABF",
         "9B7D1702",
         "9D055A64",
         "9D414A70",
         "A51DAE54",
         "A5777313",
         "A97B7480",
         "B0398871",
         "B5908835",
         "BA96FA20",
         "BEC46939",
         "BF930E1F",
         "BFAB5215",
         "C4065BE1",
         "C5DABDD4",
         "C71FE0A4",
         "C753F2E4",
         "CF97AAD6",
         "D4C38351",
         "D6763B69",
         "D86D3CA9",
         "D8EE0CED",
         "ED517C58",
         "F0503978",
         "FF2021BF",
      };
#if DEVELOPMENT // Unity flips Y coordinates on all textures until the final swapchain draws
      debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::FlipY;
#endif

      // Defaults are hardcoded in ImGUI too
      cb_luma_global_settings.GameSettings.HDRIntensity = 1.f;
      cb_luma_global_settings.GameSettings.HighlightsDesaturation = 0.4f;
      // TODO: use "default_luma_global_game_settings"

      game = new INSIDE();
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}