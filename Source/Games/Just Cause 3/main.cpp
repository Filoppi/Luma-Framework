#define GAME_JUST_CAUSE_3 1

// TODO: move SR to run before the final screen space stuff starts happening (e.g. heat distortion, bloom, blur, tonemap, etc)! Alternatively, dejitter the image before calculating bloom and dof etc?
#define ENABLE_NGX 1
// FSR is disabled in publishing builds, JC3 has no proper motion vectors on vegetation nor skinned meshes, hence it looks terrible on them (DLSS looks fine with them!).
#define ENABLE_FIDELITY_SK ((DEVELOPMENT || TEST) ? 1 : 0)
#define AUTO_ENABLE_SR 1

#define ENABLE_ORIGINAL_SHADERS_MEMORY_EDITS 1

// Hooking a debugger crashes the game (possibly due to Steam DRM, but there's only that version, and Steamless doesn't do anything)
#define DISABLE_AUTO_DEBUGGER 1

#include "..\..\Core\core.hpp"

#include "..\..\Core\includes\shader_patching.h"

#include <chrono>

#define READ_BACK_JITTERS 0

namespace
{
   ShaderHashesList shader_hashes_Tonemapper;
   ShaderHashesList shader_hashes_DownscaleBlur;
   ShaderHashesList shader_hashes_Blur;
   ShaderHashesList shader_hashes_SwapchainCopy;
   ShaderHashesList shader_hashes_PauseBackgroundCopy;
   ShaderHashesList shader_hashes_EarlyAutoExposure;
   ShaderHashesList shader_hashes_LateAutoExposure;
   ShaderHashesList shader_hashes_SMAA_EdgeDetection;
   ShaderHashesList shader_hashes_SMAA;
   ShaderHashesList shader_hashes_SMAA_2TX;
   ShaderHashesList shader_hashes_GenMotionVectors;
   ShaderHashesList shader_hashes_GenMotionVectors_TAA;
   ShaderHashesList shader_hashes_TerrainMaterials;
   ShaderHashesList shader_hashes_Materials;

   std::shared_mutex materials_mutex;

   constexpr uint32_t FORCE_VANILLA_AUTO_EXPOSURE_TYPE_HASH = char_ptr_crc32("FORCE_VANILLA_AUTO_EXPOSURE_TYPE");

   // TODO: make game device data... and force reset them when swapchain res changes, though this game only ever creates a device once
   com_ptr<ID3D11Texture2D> auto_exposure_mip_chain_texture;
   com_ptr<ID3D11ShaderResourceView> auto_exposure_mip_chain_srv;
   com_ptr<ID3D11ShaderResourceView> auto_exposure_mip_last_srv;

   com_ptr<ID3D11Resource> secondary_bloom_texture;
   com_ptr<ID3D11ShaderResourceView> secondary_bloom_srv;

   com_ptr<ID3D11SamplerState> af_sampler;

   com_ptr<ID3D11Resource> depth;

   bool has_downscaled_bloom = false;
   bool has_drawn_taa = false;
   bool has_done_swapchain_copy = false;

   uint32_t bloom_blur_passes = 0;
#if DEVELOPMENT // TODO: delete, it seems fine now, but play some more with the checks enabled!
   uint32_t blur_passes = 0;
#endif

   float frame_rate = 60.f;
   std::chrono::high_resolution_clock::time_point last_frame_time;

   // The last set (or read back) value
   float2 frame_jitters = float2(0.f, 0.f);
   float2 prev_frame_jitters = frame_jitters;

   uint8_t* jump_memory = nullptr;
#if READ_BACK_JITTERS
   // Jitters sequence (e.g. Halton). x and y for every row.
   // Can be any multiple of 2. Should be at least 8x phases for proper super resolution.
   constexpr float jittersPattern[] = {
       0.0f,        -0.16666666f,
      -0.25f,        0.16666667f,
       0.25f,       -0.38888889f,
      -0.375f,      -0.05555555f,
       0.125f,       0.27777778f,
      -0.125f,      -0.27777778f,
       0.375f,       0.05555556f,
      -0.4375f,      0.38888889f
   };
#else
   float jittersPattern[] = {
      0.0f, 0.0f
   };
   
   void SetCurrentJitters(float2 jitters)
   {
      if (!jump_memory)
         return;
      memcpy(jittersPattern, &jitters, sizeof(jitters));
      memcpy(jump_memory, &jitters, sizeof(jitters));
   }
#endif
   constexpr uint8_t jittersPhases = ARRAYSIZE(jittersPattern) / 2;

   uint32_t GetGameFrameIndex()
   {
      constexpr auto readAddressOffset = 0x2D3A6B0; // Hardcoded for end of 2025 Steam version

      const uintptr_t baseAddr = (uintptr_t)GetModuleHandleA(NULL);
      uint32_t currentFrame = *(uint32_t*)(baseAddr + readAddressOffset); // a static variable with the frame index, updated at the beginning of the frame
      return currentFrame;
   }

   // Read this after the frame begun to be sure the frame index updated
   float2 GetCurrentJitter(int32_t frame_offset = 0)
   {
#if READ_BACK_JITTERS
      uint32_t phase = (int32_t(GetGameFrameIndex()) - frame_offset) % jittersPhases;
#else
      uint32_t phase = 0;
#endif
      float curJitX = jittersPattern[phase * 2];
      float curJitY = jittersPattern[phase * 2 + 1];
      return float2{curJitX, curJitY};
   }

   bool PatchJitters()
   {
      constexpr auto patchAddressOffset = 0xC7725; // Hardcoded for end of 2025 Steam version // TODO: add some checks to make sure version is good, otherwise disable custom jitters
      constexpr size_t stolenLen = 20; // Length of bytes we are overwriting

      const uintptr_t baseAddr = (uintptr_t)GetModuleHandleA(NULL);
      uintptr_t patchAddr = baseAddr + patchAddressOffset;

      uint8_t phasesMinusOne = jittersPhases - 1;
      std::vector<uint8_t> shellcode = {
         0x48, 0xB9, 0, 0, 0, 0, 0, 0, 0, 0,          // mov rcx, [jitters table address]
         0x83, 0xE0, phasesMinusOne,                  // and eax, (e.g.) 7 [EAX = current frame index]
         0x41, 0xBA, 0x00, 0x00, 0x00, 0x40,          // mov r10d, 0x40000000 (stolen bytes)
         0x66, 0x41, 0x0F, 0x6E, 0xD2,                // movd xmm2, r10d
         0x49, 0xBA, 0, 0, 0, 0, 0, 0, 0, 0,          // mov r10, [return address]
         0x41, 0xFF, 0xE2                             // jmp r10
      };

      size_t allocSize = sizeof(jittersPattern) + shellcode.size() + 16; // Add 16 for extra safety
      jump_memory = (uint8_t*)VirtualAlloc(NULL, allocSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
      if (!jump_memory)
         return false;

      uintptr_t jittersTableAddr = (uintptr_t)jump_memory;
      uintptr_t codeAddr = (uintptr_t)jump_memory + sizeof(jittersPattern);
      uintptr_t returnAddr = patchAddr + stolenLen;

      // Put the jitters right before the byte code
      memcpy(jump_memory, jittersPattern, sizeof(jittersPattern));

      memcpy(&shellcode[2], &jittersTableAddr, sizeof(void*));
      memcpy(&shellcode[26], &returnAddr, sizeof(void*));
      memcpy((void*)codeAddr, shellcode.data(), shellcode.size());

      FlushInstructionCache(GetCurrentProcess(), (void*)codeAddr, shellcode.size());

      DWORD oldProtect;
      BOOL success = VirtualProtect((void*)patchAddr, stolenLen, PAGE_EXECUTE_READWRITE, &oldProtect);
      if (success)
      {
         // Build jump to shellcode
         uint8_t jmpToShellcode[13] = {0x49, 0xBA, 0, 0, 0, 0, 0, 0, 0, 0, 0x41, 0xFF, 0xE2}; // mov r10, addr; jmp r10
         memcpy(&jmpToShellcode[2], &codeAddr, sizeof(void*));

         memset((void*)patchAddr, 0x90, stolenLen);    // NOP original bytes
         memcpy((void*)patchAddr, jmpToShellcode, 13); // Write the jump

         VirtualProtect((void*)patchAddr, stolenLen, oldProtect, &oldProtect);

         FlushInstructionCache(GetCurrentProcess(), (void*)patchAddr, stolenLen);

         return true;
      }

      if (jump_memory)
         VirtualFree(jump_memory, 0, MEM_RELEASE);

      return false;
   }
}

class JustCause3 final : public Game
{
public:
   void OnInit(bool async) override
   {
      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"FORCE_VANILLA_FOG", '1', true, false, "In HDR, the fog texture might get upgraded and thus unclipped, causing the fog to be stronger.\nEnable this to re-clamp it to vanilla levels.", 1},
         {"FORCE_VANILLA_AUTO_EXPOSURE_TYPE", '0', true, false, "The game auto exposure was calculated after tonemapping, hence HDR will affect it.\nThe higher the value of this, the closer the exposure level will be to the vanilla SDR, however, it doesn't actually seem to look better in HDR.", 2},
         {"DISABLE_AUTO_EXPOSURE", '0', true, false, "Disables the game's strong auto exposure adjustments.", 1},
         {"ENABLE_ANAMORPHIC_BLOOM", '0', true, false, "The game bloom was stretched horizontally to emulate the look of film. It doesn't seem to fit with the rest of the game, so Luma disables that by default.", 1},
         {"ENABLE_HDR_BOOST", '1', true, false, "Enable a \"Fake\" HDR boosting effect (applies to videos too).", 1},
         {"HIGH_QUALITY_LUT", '0', true, false, "Enables a higher quality analysis for the Color Grading + Tonemapping SDR LUT, for better HDR extrapolation from it.\nNote that this isn't necessarily better.", 1},
         {"ENABLE_SHIMMER_FILTER", '0', true, false, "Enables an anti shimmering/fireflies filter, that either clips small highlights or spreads them around, in favour of image stability.\nThis does not do anything if Luma's Super Resolution is enabled.", 1},
         {"FIX_MOTION_BLUR_SHUTTER_SPEED", '1', true, false, "The game's motion blur was barely noticeable beyond 60 fps, this makes the intensity match how it looked at 60 fps, independently of the frame rate.", 1},
      };
      shader_defines_data.append_range(game_shader_defines_data);

#if ENABLE_SR
      sr_game_tooltip = "Select \"SMAA T2X\" in the game's AA settings for Super Resolution (DLSS/DLAA or FSR) to engage.\nNote that characters miss motion vectors from their animations so they might end up a bit smudged.\n";
#endif

      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('0');
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('2');
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('0'); // The game looks best in sRGB<->sRGB. Doing any kind of 2.2 conversion, whether per channel or by luminance, crushes blacks and makes the game look unnatural (especially doing the night). Though just in case, "3" looks second best here. Ideally we'd expose it as a slider, or dynamically pick it based on the LUT and time of day. // TODO: try again on OLED
      GetShaderDefineData(TEST_SDR_HDR_SPLIT_VIEW_MODE_NATIVE_IMPL_HASH).SetDefaultValue('1');

      last_frame_time = std::chrono::high_resolution_clock::now();
   }
   
   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      D3D11_TEXTURE2D_DESC exposure_texture_desc; // DLSS fails if we pass in a 1D texture so we have to make a 2D one
      exposure_texture_desc.Width = 1;
      exposure_texture_desc.Height = 1;
      exposure_texture_desc.MipLevels = 1;
      exposure_texture_desc.ArraySize = 1;
      exposure_texture_desc.Format = DXGI_FORMAT::DXGI_FORMAT_R32_FLOAT; // FP32 just so it's easier to initialize data for it
      exposure_texture_desc.SampleDesc.Count = 1;
      exposure_texture_desc.SampleDesc.Quality = 0;
      exposure_texture_desc.Usage = D3D11_USAGE_IMMUTABLE;
      exposure_texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
      exposure_texture_desc.CPUAccessFlags = 0;
      exposure_texture_desc.MiscFlags = 0;
      
      // It's best to force an exposure of 1 given that DLSS runs after the auto exposure is applied (in tonemapping).
      // Theoretically knowing the average exposure of the frame would still be beneficial to it (somehow) so maybe we could simply let the auto exposure in,
      D3D11_SUBRESOURCE_DATA exposure_texture_data;
      exposure_texture_data.pSysMem = &device_data.sr_exposure_texture_value; // This needs to be "static" data in case the texture initialization was somehow delayed and read the data after the stack destroyed it
      exposure_texture_data.SysMemPitch = 32;
      exposure_texture_data.SysMemSlicePitch = 32;
      
      device_data.sr_exposure = nullptr; // Make sure we discard the previous one
      HRESULT hr = native_device->CreateTexture2D(&exposure_texture_desc, &exposure_texture_data, &device_data.sr_exposure);
      assert(SUCCEEDED(hr));
   }

   // Add a saturate on materials/gbuffers
   std::unique_ptr<std::byte[]> ModifyShaderByteCode(const std::byte* code, size_t& size, reshade::api::pipeline_subobject_type type, uint64_t shader_hash, const std::byte* shader_object, size_t shader_object_size) override
   {
      if (type != reshade::api::pipeline_subobject_type::pixel_shader)
         return nullptr;

      std::unique_ptr<std::byte[]> new_code = nullptr;

      bool gbuffers_pattern_found = false;
      bool gbuffers_diffuse_pattern_found = false;
      bool lighting_pattern_found = false;

      // The game first renders to 4 UNORM GBuffers (which we might upgrade to FLOAT RTs hence we need to add saturate() on their output, to prevent values beyond 0-1 and NaNs)
      const char str_to_find_gbuffers_1[] = "DepthMap";
      const char str_to_find_gbuffers_2[] = "DiffuseMap";
      const char str_to_find_gbuffers_3[] = "DiffuseAlpha";
      const char str_to_find_gbuffers_4[] = "NormalMap";
      const char str_to_find_gbuffers_5[] = "MaterialConsts"; // Sometimes it's "cbMaterialConsts" too
      const char str_to_find_gbuffers_6[] = "DecalAlbedoMap"; // Sometimes it's "cbMaterialConsts" too
      const char str_to_find_gbuffers_7[] = "SamplerRegular"; // This is always at slot 0

      // The game then composes the gbuffers and renders "lighting" on top (lights, fog, transparency (glass), particles, decals, ...)
      const char str_to_find_lighting[] = "LightingFrameConsts";

      std::vector<std::byte> pattern_safety_check;
      bool pattern_found;
      pattern_safety_check = std::vector<std::byte>(reinterpret_cast<const std::byte*>(str_to_find_gbuffers_1), reinterpret_cast<const std::byte*>(str_to_find_gbuffers_1) + strlen(str_to_find_gbuffers_1));
      gbuffers_pattern_found |= !System::ScanMemoryForPattern(reinterpret_cast<const std::byte*>(shader_object), shader_object_size, pattern_safety_check, true).empty();
      pattern_safety_check = std::vector<std::byte>(reinterpret_cast<const std::byte*>(str_to_find_gbuffers_2), reinterpret_cast<const std::byte*>(str_to_find_gbuffers_2) + strlen(str_to_find_gbuffers_2));
      pattern_found = !System::ScanMemoryForPattern(reinterpret_cast<const std::byte*>(shader_object), shader_object_size, pattern_safety_check, true).empty();
      gbuffers_pattern_found |= pattern_found;
      gbuffers_diffuse_pattern_found |= pattern_found;
      pattern_safety_check = std::vector<std::byte>(reinterpret_cast<const std::byte*>(str_to_find_gbuffers_3), reinterpret_cast<const std::byte*>(str_to_find_gbuffers_3) + strlen(str_to_find_gbuffers_3));
      gbuffers_pattern_found |= !System::ScanMemoryForPattern(reinterpret_cast<const std::byte*>(shader_object), shader_object_size, pattern_safety_check, true).empty();
      pattern_safety_check = std::vector<std::byte>(reinterpret_cast<const std::byte*>(str_to_find_gbuffers_4), reinterpret_cast<const std::byte*>(str_to_find_gbuffers_4) + strlen(str_to_find_gbuffers_4));
      gbuffers_pattern_found |= !System::ScanMemoryForPattern(reinterpret_cast<const std::byte*>(shader_object), shader_object_size, pattern_safety_check, true).empty();
      pattern_safety_check = std::vector<std::byte>(reinterpret_cast<const std::byte*>(str_to_find_gbuffers_5), reinterpret_cast<const std::byte*>(str_to_find_gbuffers_5) + strlen(str_to_find_gbuffers_5));
      gbuffers_pattern_found |= !System::ScanMemoryForPattern(reinterpret_cast<const std::byte*>(shader_object), shader_object_size, pattern_safety_check, true).empty();
      pattern_safety_check = std::vector<std::byte>(reinterpret_cast<const std::byte*>(str_to_find_gbuffers_6), reinterpret_cast<const std::byte*>(str_to_find_gbuffers_6) + strlen(str_to_find_gbuffers_6));
      gbuffers_pattern_found |= !System::ScanMemoryForPattern(reinterpret_cast<const std::byte*>(shader_object), shader_object_size, pattern_safety_check, true).empty();
      pattern_safety_check = std::vector<std::byte>(reinterpret_cast<const std::byte*>(str_to_find_gbuffers_7), reinterpret_cast<const std::byte*>(str_to_find_gbuffers_7) + strlen(str_to_find_gbuffers_7));
      gbuffers_diffuse_pattern_found |= !System::ScanMemoryForPattern(reinterpret_cast<const std::byte*>(shader_object), shader_object_size, pattern_safety_check, true).empty();
      // TODO: why does "0xDFBD067F" fail this check?

      pattern_safety_check = std::vector<std::byte>(reinterpret_cast<const std::byte*>(str_to_find_lighting), reinterpret_cast<const std::byte*>(str_to_find_lighting) + strlen(str_to_find_lighting));
      lighting_pattern_found |= !System::ScanMemoryForPattern(reinterpret_cast<const std::byte*>(shader_object), shader_object_size, pattern_safety_check, true).empty();

      if (!gbuffers_pattern_found && !lighting_pattern_found)
         return new_code;

      uint8_t found_dcl_outputs = 0;

      size_t size_u32 = size / sizeof(uint32_t);
      const uint32_t* code_u32 = reinterpret_cast<const uint32_t*>(code);
      size_t i = 0;
      bool found_first_dcl = false;
      while (i < size_u32)
      {
         uint32_t opcode_token = code_u32[i];
         D3D10_SB_OPCODE_TYPE opcode_type = DECODE_D3D10_SB_OPCODE_TYPE(opcode_token);

         // Stop scanning the DCL opcodes, they are all declared at the beginning
         if (ShaderPatching::opcodes_dcl.contains(opcode_type))
            found_first_dcl = true;
         else if (found_first_dcl) // Extra safety in case there were instructions before DCL ones
            break;

         size_t instruction_size = opcode_type == D3D10_SB_OPCODE_CUSTOMDATA ? code_u32[i + 1] : DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(opcode_token); // Includes itself

         if (opcode_type == D3D10_SB_OPCODE_DCL_OUTPUT)
         {
            uint32_t operand_token = code_u32[i + 1];
            bool four_channels = true;
            uint32_t test_operand_token =
               ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT) |
               ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE) |
               ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(D3D10_SB_OPERAND_4_COMPONENT_MASK_X) |
               ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(four_channels ? D3D10_SB_OPERAND_4_COMPONENT_MASK_Y : D3D10_SB_OPERAND_4_COMPONENT_MASK_X) |
               ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(four_channels ? D3D10_SB_OPERAND_4_COMPONENT_MASK_Z : D3D10_SB_OPERAND_4_COMPONENT_MASK_X) |
               ENCODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(four_channels ? D3D10_SB_OPERAND_4_COMPONENT_MASK_W : D3D10_SB_OPERAND_4_COMPONENT_MASK_X) |
               ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_OUTPUT) |
               ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D) |
               ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(found_dcl_outputs, D3D10_SB_OPERAND_INDEX_IMMEDIATE32);
            if (operand_token == test_operand_token)
            {
               found_dcl_outputs++;
            }
         }

         i += instruction_size;
         if (instruction_size == 0)
            break;
      }

      // Make sure they have 4 or 1 RTs
      if (gbuffers_pattern_found)
      {
         gbuffers_pattern_found &= found_dcl_outputs == 4;
      }
      if (lighting_pattern_found)
      {
         lighting_pattern_found &= found_dcl_outputs == 1;
      }

      if (gbuffers_pattern_found || lighting_pattern_found)
      {
         if (gbuffers_pattern_found && gbuffers_diffuse_pattern_found)
         {
            const std::unique_lock lock(materials_mutex); // This is all single threaded anyway (verified)
            shader_hashes_Materials.pixel_shaders.emplace(uint32_t(shader_hash));
         }

         std::vector<uint8_t> appended_patch;

         constexpr bool enable_unorm_emulation = false; // TODO: this breaks everything... I don't know why. Probably because of some false positive that writes on float or snorm.
         constexpr bool enable_r11g11b10float_emulation = true;
         if (gbuffers_pattern_found && enable_unorm_emulation)
         {
            // Saturate all 4 outputs
            std::vector<uint32_t> mov_sat_onxyzw_onxyzw;
            mov_sat_onxyzw_onxyzw = ShaderPatching::GetSatInstruction(D3D10_SB_OPERAND_TYPE_OUTPUT, 0);
            appended_patch.insert(appended_patch.end(), reinterpret_cast<uint8_t*>(mov_sat_onxyzw_onxyzw.data()), reinterpret_cast<uint8_t*>(mov_sat_onxyzw_onxyzw.data()) + mov_sat_onxyzw_onxyzw.size() * sizeof(uint32_t));
            mov_sat_onxyzw_onxyzw = ShaderPatching::GetSatInstruction(D3D10_SB_OPERAND_TYPE_OUTPUT, 1);
            appended_patch.insert(appended_patch.end(), reinterpret_cast<uint8_t*>(mov_sat_onxyzw_onxyzw.data()), reinterpret_cast<uint8_t*>(mov_sat_onxyzw_onxyzw.data()) + mov_sat_onxyzw_onxyzw.size() * sizeof(uint32_t));
            mov_sat_onxyzw_onxyzw = ShaderPatching::GetSatInstruction(D3D10_SB_OPERAND_TYPE_OUTPUT, 2);
            appended_patch.insert(appended_patch.end(), reinterpret_cast<uint8_t*>(mov_sat_onxyzw_onxyzw.data()), reinterpret_cast<uint8_t*>(mov_sat_onxyzw_onxyzw.data()) + mov_sat_onxyzw_onxyzw.size() * sizeof(uint32_t));
            mov_sat_onxyzw_onxyzw = ShaderPatching::GetSatInstruction(D3D10_SB_OPERAND_TYPE_OUTPUT, 3);
            appended_patch.insert(appended_patch.end(), reinterpret_cast<uint8_t*>(mov_sat_onxyzw_onxyzw.data()), reinterpret_cast<uint8_t*>(mov_sat_onxyzw_onxyzw.data()) + mov_sat_onxyzw_onxyzw.size() * sizeof(uint32_t));
         }
         else if (lighting_pattern_found && enable_r11g11b10float_emulation)
         {
            // >= 0 (fixes negative values and NaNs)
            // Note: theoretically we should saturate alpha, but it doesn't seem to ever be a problem
            std::vector<uint32_t> max_o0xyz_o0xyz_0 = ShaderPatching::GetMaxInstruction(D3D10_SB_OPERAND_TYPE_OUTPUT, 0, D3D10_SB_OPERAND_TYPE_OUTPUT, 0);
            appended_patch.insert(appended_patch.end(), reinterpret_cast<uint8_t*>(max_o0xyz_o0xyz_0.data()), reinterpret_cast<uint8_t*>(max_o0xyz_o0xyz_0.data()) + max_o0xyz_o0xyz_0.size() * sizeof(uint32_t));
         }

         new_code = std::make_unique<std::byte[]>(size + appended_patch.size());

         // Pattern to search for: 3E 00 00 01 (the last byte is the size, and the minimum is 1 (the unit is 4 bytes), given it also counts for the opcode and it's own size byte
         const std::vector<std::byte> return_pattern = {std::byte{0x3E}, std::byte{0x00}, std::byte{0x00}, std::byte{0x01}};

         // Append before the ret instruction if there's one at the end (there should always be)
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

         size += appended_patch.size();
      }

      return new_code;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      const std::shared_lock lock(materials_mutex); // Note: this could be "optimized" a bit, given that most shaders will compile on boot and on the same thread, but not all (I checked). Either way it shouldn't be too slow.
      if (enable_samplers_upgrade && original_shader_hashes.Contains(shader_hashes_TerrainMaterials) && test_index != 2)
      {
         // Replace sampler 3 and 4 with the one in slot 5 (AF)
         //SamplerState DetailColorState_s : register(s3)
         //SamplerState DetailBumpState_s : register(s4)
         //SamplerState SamplerStateDiffuseAnisotropic_s : register(s5)

         // TODO: it'd probably be enough to do this once when this shader is bound, for performance reasons, but anyway we could skip second calls here
         com_ptr<ID3D11SamplerState> sampler_states[2];
         if (!af_sampler)
         {
            native_device_context->PSGetSamplers(5, 1, &sampler_states[0]);
            sampler_states[1] = sampler_states[0]; // Duplicate, so we can do a single set below
            af_sampler = sampler_states[0];        // Cache it aside!
         }
         else
         {
            sampler_states[0] = af_sampler;
            sampler_states[1] = af_sampler;
         }
         ID3D11SamplerState* const* sampler_states_const = (ID3D11SamplerState**)std::addressof(sampler_states[0]);
         native_device_context->PSSetSamplers(3, 2, sampler_states_const); // TODO: actually this doesn't do anything?
      }
      else if (enable_samplers_upgrade && original_shader_hashes.Contains(shader_hashes_Materials) && af_sampler && test_index != 2)
      {
         // TODO: replace these too, in all detected materials (through shader patching), then store a map of material hashes and which samplers do they need replaced.
         // All of these were "linear" in the engine while they should have been AF.
         // A few of these would already be AF. Target state: "D3D11_FILTER_ANISOTROPIC" "D3D11_TEXTURE_ADDRESS_WRAP" "D3D11_COMPARISON_NEVER" 16x.
         // SamplerState DiffuseMap_s : register(s0);
         // SamplerState NormalMap_s : register(s1);
         // SamplerState PropertiesMap_s : register(s2); // ?
         // SamplerState PropertyMap_s : register(s2); // ?
         // SamplerState DetailDiffuseMap_s : register(s3);
         // SamplerState DetailNormalMap_s : register(s4);
         // SamplerState NormalDetailMap_s : register(s4);
         // SamplerState EmissiveMap_s : register(s5);
         // SamplerState FeatureMap_s : register(s5); // ?
         // SamplerState WrinkleMap_s : register(s6); // ?
         // SamplerState MetallicMap_s : register(s9);
         //
         // SamplerState SamplerRegular_s : register(s0);
         // SamplerState SamplerNormalMap_s : register(s1);
         // 
         // Transparency or Alpha Masked:
         // SamplerState DiffuseBase_s : register(s0);

         ID3D11SamplerState* sampler_states[3];
         sampler_states[0] = af_sampler.get();
         sampler_states[1] = af_sampler.get();
         sampler_states[2] = af_sampler.get(); // For simpler materials, the ones with "SamplerRegular", this isn't set/used but it should be fine anyway
         ID3D11SamplerState* const* sampler_states_const = (ID3D11SamplerState**)std::addressof(sampler_states[0]);
         native_device_context->PSSetSamplers(0, 3, sampler_states_const);
      }
      // Make sure the swapchain copy shader always and only targets the swapchain RT, otherwise we'd need to branch in it!
      else if (is_custom_pass && original_shader_hashes.Contains(shader_hashes_SwapchainCopy))
      {
         com_ptr<ID3D11RenderTargetView> rtv;
         native_device_context->OMGetRenderTargets(1, &rtv, nullptr);
         bool is_rt_swapchain = false;
         if (rtv)
         {
            com_ptr<ID3D11Resource> rt_resource;
            rtv->GetResource(&rt_resource);

            const std::shared_lock lock(device_data.mutex);
            is_rt_swapchain = device_data.back_buffers.contains((uint64_t)rt_resource.get());
         }

         // Needed to branch on gamma conversions in the shaders
         uint32_t custom_data_1 = is_rt_swapchain ? 1 : 0;
         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData, custom_data_1);
         updated_cbuffers = true;

         if (is_rt_swapchain)
            has_done_swapchain_copy = true;
      }
      else if (is_custom_pass && original_shader_hashes.Contains(shader_hashes_PauseBackgroundCopy))
      {
         uint32_t custom_data_1 = has_done_swapchain_copy ? 1 : 0;
         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData, custom_data_1);
         updated_cbuffers = true;
      }
      else if (original_shader_hashes.Contains(shader_hashes_DownscaleBlur))
      {
         // This always runs before tm
         has_downscaled_bloom = true;
      }
      else if (original_shader_hashes.Contains(shader_hashes_Blur))
      {
         if (has_downscaled_bloom)
         {
            bloom_blur_passes++;
            // The 6th bloom pass was done and allowed bloom to be "symmetrical" (like a small dot blooming to a circle) but somehow they forgot to use the last
            if (bloom_blur_passes == 6)
            {
               com_ptr<ID3D11RenderTargetView> render_target_views[1];
               native_device_context->OMGetRenderTargets(1, &render_target_views[0], nullptr);
               com_ptr<ID3D11Resource> current_secondary_bloom_texture;
               if (render_target_views[0])
                  render_target_views[0]->GetResource(&current_secondary_bloom_texture);
               if (current_secondary_bloom_texture != secondary_bloom_texture)
               {
                  secondary_bloom_texture = nullptr;
                  secondary_bloom_srv = nullptr;

                  if (current_secondary_bloom_texture)
                  {
                     secondary_bloom_texture = current_secondary_bloom_texture;
                     HRESULT hr = native_device->CreateShaderResourceView(secondary_bloom_texture.get(), nullptr, &secondary_bloom_srv);
                     ASSERT_ONCE(SUCCEEDED(hr));
                  }
               }
            }
         }
#if DEVELOPMENT
         blur_passes++;
#endif
      }
      else if (has_downscaled_bloom && original_shader_hashes.Contains(shader_hashes_Tonemapper))
      {
         device_data.has_drawn_main_post_processing = true;

         if (secondary_bloom_srv)
         {
            ID3D11ShaderResourceView* const secondary_bloom_srv_const = secondary_bloom_srv.get();
            native_device_context->PSSetShaderResources(4, 1, &secondary_bloom_srv_const);
         }
      }
      // Replace the auto exposure approximate pass with a full 1x1 mip downscale.
      // Exposure was sampling some random texels of the scene onto a fixed ~300x100 texture, which causes the game to flicker when small strong lights were on screen, due to the exposure constantly changing.
      // This has a performance cost but it fixes relevant issues with the game.
      // TODO: fix occasional light flickers (from secondary or primary bloom) that happen at night, especially when DLSS is enabled?
      else if (is_custom_pass && (original_shader_hashes.Contains(shader_hashes_EarlyAutoExposure) || original_shader_hashes.Contains(shader_hashes_LateAutoExposure)) && GetShaderDefineCompiledNumericalValue(FORCE_VANILLA_AUTO_EXPOSURE_TYPE_HASH) <= 1 && test_index != 1)
      {
         com_ptr<ID3D11ShaderResourceView> srv;
         native_device_context->PSGetShaderResources(0, 1, &srv);
         if (srv.get())
         {
            com_ptr<ID3D11Resource> sr;
            srv->GetResource(&sr);

            uint4 size_a, size_b;
            DXGI_FORMAT format_a, format_b;
            GetResourceInfo(auto_exposure_mip_chain_texture.get(), size_a, format_a);
            GetResourceInfo(sr.get(), size_b, format_b);
            // Note: we would have upgraded both the SRV of "shader_hashes_EarlyAutoExposure" and "shader_hashes_LateAutoExposure" so the format check doesn't trigger twice per frame!
            if (size_a != size_b || format_a != format_b)
            {
               UINT mips = GetTextureMaxMipLevels(size_b.x, size_b.y, size_b.z); // Up to 1x1
#if 1
               mips = GetOptimalTextureMipLevelsForTargetSize(size_b.x, size_b.y, 320, 180);
#endif
               auto_exposure_mip_chain_texture = CloneTexture<ID3D11Texture2D>(native_device, sr.get(), DXGI_FORMAT_UNKNOWN, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, D3D11_BIND_UNORDERED_ACCESS, false, false, native_device_context, mips);
               auto_exposure_mip_chain_srv = nullptr;
               auto_exposure_mip_last_srv = nullptr;
               if (auto_exposure_mip_chain_texture)
               {
                  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
                  srv->GetDesc(&srv_desc);
                  srv_desc.Texture2D.MipLevels = 1;
                  srv_desc.Texture2D.MostDetailedMip = mips - 1; // Only give access to the 1x1 resource
                  HRESULT hr = native_device->CreateShaderResourceView(auto_exposure_mip_chain_texture.get(), &srv_desc, &auto_exposure_mip_last_srv);
                  ASSERT_ONCE(SUCCEEDED(hr));
                  hr = native_device->CreateShaderResourceView(auto_exposure_mip_chain_texture.get(), nullptr, &auto_exposure_mip_chain_srv);
                  ASSERT_ONCE(SUCCEEDED(hr));
               }
            }

            native_device_context->CopySubresourceRegion(auto_exposure_mip_chain_texture.get(), 0, 0, 0, 0, sr.get(), 0, nullptr);

            native_device_context->GenerateMips(auto_exposure_mip_chain_srv.get());

#if DEVELOPMENT
            const std::shared_lock lock_trace(s_mutex_trace);
            if (trace_running)
            {
               const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
               TraceDrawCallData trace_draw_call_data;
               trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
               trace_draw_call_data.command_list = native_device_context;
               trace_draw_call_data.custom_name = "Generate Auto Exposure Mips";
               cmd_list_data.trace_draw_calls_data.insert(cmd_list_data.trace_draw_calls_data.end() - 1, trace_draw_call_data);
            }
#endif
            ID3D11ShaderResourceView* const auto_exposure_mip_last_srv_const = auto_exposure_mip_last_srv.get();
            native_device_context->PSSetShaderResources(0, 1, &auto_exposure_mip_last_srv_const);

            // Add a linear sampler to aid in conservative downscaling, otherwise auto exposure flickers, because it was using a point sampler to downscale a huge image on a tiny one.
            // The target image is always 320x180, and misses a part of the top of the image for some reason...
            // It seems like this is read back in c++ and it takes the min and max to calculate the exposure, or some sample random points on it, because if we manually pre-average the color,
            // the exposure changes, so we need to preserve the min and max, but also make the image smooth and conservative.
            ID3D11SamplerState* const sampler_state_linear = device_data.sampler_state_linear.get();
            native_device_context->PSSetSamplers(D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - 1, 1, &sampler_state_linear);
         }
      }
      else if (original_shader_hashes.Contains(shader_hashes_GenMotionVectors))
      {
         depth.reset();

#if 0 // Depth is bound as SRV not as DSV
         com_ptr<ID3D11RenderTargetView> render_target_views[1];
         com_ptr<ID3D11DepthStencilView> depth_stencil_view;
         native_device_context->OMGetRenderTargets(1, &render_target_views[0], &depth_stencil_view);
         if (depth_stencil_view)
            depth_stencil_view->GetResource(&depth);
#else
         com_ptr<ID3D11ShaderResourceView> srv[1];
         native_device_context->PSGetShaderResources(1, ARRAYSIZE(srv), &srv[0]);
         if (srv[0])
            srv[0]->GetResource(&depth);
#endif

#if DEVELOPMENT && READ_BACK_JITTERS // TODO: delete after testing it (it doesn't matter if it fails around pause)
         // This always runs when SMAA 2TX
         if (original_shader_hashes.Contains(shader_hashes_GenMotionVectors_TAA))
         {
            const bool sr_active = device_data.sr_type != SR::Type::None && !device_data.sr_suppressed;
            if (sr_active)
            {
               // Verify the jitters we predicted is correct!
               ASSERT_ONCE(frame_jitters == GetCurrentJitter());
            }
         }
#endif

         if (is_custom_pass)
         {
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, stages, LumaConstantBufferType::LumaData, 0, 0, max(frame_rate, 10.f)); // Clamp the frame rate (a bit random). Ideally we'd retrieve it from the game cbuffers but I couldn't find it
            updated_cbuffers = true;
         }
      }
      else if (original_shader_hashes.Contains(shader_hashes_SMAA_EdgeDetection))
      {
         // We can skip these if Super Resolution is on, as they won't be used!
         if (device_data.sr_type != SR::Type::None && !device_data.sr_suppressed && device_data.taa_detected)
         {
            return DrawOrDispatchOverrideType::Skip;
         }
      }
      else if (original_shader_hashes.Contains(shader_hashes_SMAA_2TX))
      {
         has_drawn_taa = true;
         device_data.taa_detected = true;
#if ENABLE_SR
         if (device_data.sr_type != SR::Type::None && !device_data.sr_suppressed)
         {
            assert(device_data.sr_type != SR::Type::None && !device_data.sr_suppressed && !device_data.has_drawn_sr);

            // 0 Source Color (post SMAA, pre TAA)
            // 1 Previous Color (post SMAA, pre TAA)
            // 2 Raw Motion Vectors (directly in UV space)
            com_ptr<ID3D11ShaderResourceView> ps_shader_resources[3];
            native_device_context->PSGetShaderResources(0, ARRAYSIZE(ps_shader_resources), &ps_shader_resources[0]);

            com_ptr<ID3D11RenderTargetView> render_target_views[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]; // There should only be 1
            com_ptr<ID3D11DepthStencilView> depth_stencil_view;
            native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &render_target_views[0], &depth_stencil_view);
            const bool dlss_inputs_valid = ps_shader_resources[0].get() && ps_shader_resources[2].get() && render_target_views[0].get() && depth.get();
            ASSERT_ONCE(dlss_inputs_valid);

            if (dlss_inputs_valid)
            {
               DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack;
               DrawStateStack<DrawStateStackType::Compute> compute_state_stack;
               draw_state_stack.Cache(native_device_context, device_data.uav_max_count);
               compute_state_stack.Cache(native_device_context, device_data.uav_max_count);

               auto* sr_instance_data = device_data.GetSRInstanceData();
               ASSERT_ONCE(sr_instance_data);

               com_ptr<ID3D11Resource> output_color_resource;
               render_target_views[0]->GetResource(&output_color_resource);
               com_ptr<ID3D11Texture2D> output_color;
               HRESULT hr = output_color_resource->QueryInterface(&output_color);
               ASSERT_ONCE(SUCCEEDED(hr));

               D3D11_TEXTURE2D_DESC taa_output_texture_desc;
               output_color->GetDesc(&taa_output_texture_desc);

               SR::SettingsData settings_data;
               settings_data.output_width = unsigned int(device_data.output_resolution.x + 0.5);
               settings_data.output_height = unsigned int(device_data.output_resolution.y + 0.5);
               settings_data.render_width = unsigned int(device_data.render_resolution.x + 0.5);
               settings_data.render_height = unsigned int(device_data.render_resolution.y + 0.5);
               settings_data.hdr = true; // At this point we are linear and "HDR" though the image is partially tonemapped if we are after SMAA
               settings_data.inverted_depth = true;
               settings_data.mvs_jittered = false; // See shader 0xA1037803, they were partially jittered (with the current frame jitter but not the previous one, so we fixed that up and completely removed jitters, so it also makes motion blur independent from them)
               settings_data.auto_exposure = device_data.sr_type != SR::Type::FSR; // Exp is ~1 given it's all after post processing (and the game's auto exposure). FSR breaks with auto exposure in this game (it heavily clips highlights). DLSS looks fine with it.
               // MVs in UV space, so we need to scale by the render resolution to transform to pixel space
               settings_data.mvs_x_scale = -device_data.render_resolution.x;
               settings_data.mvs_y_scale = -device_data.render_resolution.y;
               settings_data.render_preset = dlss_render_preset;
               sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context, settings_data);

               bool skip_dlss = taa_output_texture_desc.Width < sr_instance_data->min_resolution || taa_output_texture_desc.Height < sr_instance_data->min_resolution;
               bool dlss_output_changed = false;

               constexpr bool dlss_use_native_uav = true;
               bool dlss_output_supports_uav = dlss_use_native_uav && (taa_output_texture_desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0;
               // Create a copy that supports Unordered Access if it wasn't already supported
               if (!dlss_output_supports_uav)
               {
                  D3D11_TEXTURE2D_DESC dlss_output_texture_desc = taa_output_texture_desc;
                  dlss_output_texture_desc.Width = std::lrintf(device_data.output_resolution.x);
                  dlss_output_texture_desc.Height = std::lrintf(device_data.output_resolution.y);
                  dlss_output_texture_desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

                  if (device_data.sr_output_color.get())
                  {
                     D3D11_TEXTURE2D_DESC prev_dlss_output_texture_desc;
                     device_data.sr_output_color->GetDesc(&prev_dlss_output_texture_desc);
                     dlss_output_changed = prev_dlss_output_texture_desc.Width != dlss_output_texture_desc.Width || prev_dlss_output_texture_desc.Height != dlss_output_texture_desc.Height || prev_dlss_output_texture_desc.Format != dlss_output_texture_desc.Format;
                  }
                  if (!device_data.sr_output_color.get() || dlss_output_changed)
                  {
                     device_data.sr_output_color = nullptr; // Make sure we discard the previous one
                     hr = native_device->CreateTexture2D(&dlss_output_texture_desc, nullptr, &device_data.sr_output_color);
                     ASSERT_ONCE(SUCCEEDED(hr));
                  }
                  // Texture creation failed, we can't proceed with DLSS
                  if (!device_data.sr_output_color.get())
                  {
                     skip_dlss = true;
                  }
               }
               else
               {
                  ASSERT_ONCE(device_data.sr_output_color == nullptr);
                  device_data.sr_output_color = output_color;
               }

               if (!skip_dlss)
               {
                  com_ptr<ID3D11Resource> sr_source_color;
                  ps_shader_resources[0]->GetResource(&sr_source_color);
                  com_ptr<ID3D11Resource> motion_vectors;
                  ps_shader_resources[2]->GetResource(&motion_vectors);

                  ASSERT_ONCE(motion_vectors.get() && sr_source_color.get() && depth.get());

                  bool reset_dlss = device_data.force_reset_sr || dlss_output_changed;
                  device_data.force_reset_sr = false;

                  float dlss_pre_exposure = 0.f;

                  SR::SuperResolutionImpl::DrawData draw_data;
                  draw_data.source_color = sr_source_color.get();
                  draw_data.output_color = device_data.sr_output_color.get();
                  draw_data.motion_vectors = motion_vectors.get();
                  draw_data.depth_buffer = depth.get();
                  draw_data.pre_exposure = dlss_pre_exposure;
                  draw_data.jitter_x = frame_jitters.x; // Not 100% sure these shouldn't be scaled by 0.5, but probably not! (I tried, couldn't tell the difference, but logic points towards not doing it)
                  draw_data.jitter_y = frame_jitters.y;
                  draw_data.reset = reset_dlss; // TODO: implement camera cuts too... I don't think the game has them exposed though. Possibly reset DLSS when we pause the game or go into a loading screen.

#if 1 // Extracted from proj matrix on the CPU (not 100% if this or the ones below are right)
                  draw_data.near_plane = 0.1; // 10cm
                  draw_data.far_plane = 40000.0; // 40km
#else // Extracted from "A1037803" PS cbuffers. Supposedly they are fixed throughout the game. "7BE70E91" might also have them.
                  draw_data.near_plane = 0.025; // 2.5cm
                  draw_data.far_plane = 10000.0; // 10km
#endif
                  draw_data.vert_fov = 0.60894538; // Would be "atan(1.f / projection_matrix.m11) * 2.0", however we don't have the proj matrix in any cbuffer in this game, it's only in the CPU. No current SR implementation uses this anyway. Seems like the default is 34.89 degs.
                  draw_data.frame_index = cb_luma_global_settings.FrameIndex;
                  draw_data.time_delta = 1.0 / 60.0;
                  if (!settings_data.auto_exposure)
                     draw_data.exposure = device_data.sr_exposure.get();

                  bool dlss_succeeded = sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context, draw_data);
                  if (dlss_succeeded)
                  {
                     device_data.has_drawn_sr = true;
                  }

                  draw_state_stack.Restore(native_device_context, device_data.uav_max_count);
                  compute_state_stack.Restore(native_device_context, device_data.uav_max_count);

                  if (device_data.has_drawn_sr)
                  {
#if DEVELOPMENT
                     const std::shared_lock lock_trace(s_mutex_trace);
                     if (trace_running)
                     {
                        const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
                        TraceDrawCallData trace_draw_call_data;
                        trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
                        trace_draw_call_data.command_list = native_device_context;
                        trace_draw_call_data.custom_name = "Super Resolution";
                        // Re-use the RTV data for simplicity
                        GetResourceInfo(device_data.sr_output_color.get(), trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);
                        cmd_list_data.trace_draw_calls_data.insert(cmd_list_data.trace_draw_calls_data.end() - 1, trace_draw_call_data);
                     }
#endif

                     if (!dlss_output_supports_uav)
                     {
                        native_device_context->CopyResource(output_color.get(), device_data.sr_output_color.get()); // DX11 doesn't need barriers
                     }
                     else
                     {
                        device_data.sr_output_color = nullptr;
                     }

                     return DrawOrDispatchOverrideType::Replaced;
                  }
                  else
                  {
                     cb_luma_global_settings.SRType = 0;
                     device_data.cb_luma_global_settings_dirty = true;

                     device_data.sr_suppressed = true;
                     device_data.force_reset_sr = true;
                  }
               }
               if (dlss_output_supports_uav)
               {
                  device_data.sr_output_color = nullptr;
               }
            }
            return DrawOrDispatchOverrideType::None;
         }
#endif // ENABLE_SR
      }

      return DrawOrDispatchOverrideType::None; // Don't cancel the original draw call
   }

   void UpdateLumaInstanceDataCB(CB::LumaInstanceDataPadded& data, CommandListData& cmd_list_data, DeviceData& device_data) override
   {
      float2 jitters = frame_jitters;
      jitters.x /= device_data.render_resolution.x;
      jitters.y /= device_data.render_resolution.y;
      memcpy(&data.GameData.CurrJitters, &jitters, sizeof(jitters));
      jitters = prev_frame_jitters;
      jitters.x /= device_data.render_resolution.x;
      jitters.y /= device_data.render_resolution.y;
      memcpy(&data.GameData.PrevJitters, &jitters, sizeof(jitters));
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      if (!has_drawn_taa)
      {
#if ENABLE_SR
         device_data.force_reset_sr = true; // If the frame didn't draw the scene, SR needs to reset to prevent the old history from blending with the new scene
#endif
         device_data.taa_detected = false;

         // Theoretically we turn this flag off one frame late (or well, at the end of the frame),
         // but then again, if no scene rendered, this flag wouldn't have been used for anything.
         if (cb_luma_global_settings.SRType > 0)
         {
            cb_luma_global_settings.SRType = 0; // No need for "s_mutex_reshade" here, given that they are generally only also changed by the user manually changing the settings in ImGUI, which runs at the very end of the frame
            device_data.cb_luma_global_settings_dirty = true;
         }

         device_data.sr_suppressed = false;
      }

      bool drew_sr = cb_luma_global_settings.SRType > 0;                                                                                                                        // If this was true, SR would have been enabled and probably drew
      cb_luma_global_settings.SRType = (device_data.sr_type != SR::Type::None && !device_data.sr_suppressed && device_data.taa_detected) ? (uint(device_data.sr_type) + 1) : 0; // No need for "s_mutex_reshade" here, given that they are generally only also changed by the user manually changing the settings in ImGUI, which runs at the very end of the frame
      if (cb_luma_global_settings.SRType > 0 && !drew_sr)
      {
         device_data.cb_luma_global_settings_dirty = true;
         // Reset SR history when we toggle SR on and off manually, or when the user in the game changes the AA mode,
         // otherwise the history from the last time SR was active will be kept (SR implementations don't know time passes since it was last used).
         // We could also clear SR resources here when we know it's unused for a while, but it would possibly lead to stutters.
         device_data.force_reset_sr = true;
      }

      depth = nullptr;

      device_data.has_drawn_main_post_processing = false;
      device_data.has_drawn_sr = false;
      has_drawn_taa = false;
      has_downscaled_bloom = false;
      has_done_swapchain_copy = false;

      if (!custom_texture_mip_lod_bias_offset)
      {
         std::shared_lock shared_lock_samplers(s_mutex_samplers);
         if (device_data.sr_type != SR::Type::None && !device_data.sr_suppressed)
         {
            device_data.texture_mip_lod_bias_offset = SR::GetMipLODBias(device_data.render_resolution.y, device_data.output_resolution.y); // This results in -1 at output res
         }
         else
         {
            device_data.texture_mip_lod_bias_offset = 0.f;
         }
      }

      bloom_blur_passes = 0;
#if DEVELOPMENT
      // Make sure this shader isn't re-used for anything else other than motion blur, given it might
      ASSERT_ONCE(blur_passes == 6 || blur_passes == 0);
      blur_passes = 0;
#endif

      auto now = std::chrono::high_resolution_clock::now();
      std::chrono::duration<float> delta = now - last_frame_time;
      frame_rate = 1.0f / delta.count();
      last_frame_time = now;

      const bool sr_active = device_data.sr_type != SR::Type::None && !device_data.sr_suppressed;
#if !READ_BACK_JITTERS
      // Force write the jitters in the game's memory every frame
      float2 jitters;
      if (sr_active)
      {
         int next_frame_index = (cb_luma_global_settings.FrameIndex + 1) % 8; // Pre-increment, as it will be incremented after this. Period of 8 for native resolution.
         jitters.x = SR::HaltonSequence(next_frame_index, 2);
         jitters.y = SR::HaltonSequence(next_frame_index, 3);
      }
      else
      {
         bool is_even_frame = (GetGameFrameIndex() + 1) % 2; // Next frame, it hasn't been increased yet
         // The game jitter matrix is -0.25 0.25, 0.25 -0.25. Respectively x and y.
         // Just two frames, the standard SMAA T2X jitter pattern.
         // We emulate it here, to restore the vanilla behaviour without our custom jitters code.
         if (is_even_frame)
         {
            jitters = float2(-0.25f, 0.25f);
         }
         else
         {
            jitters = float2(0.25f, -0.25f);
         }
      }
      SetCurrentJitters(jitters);
#endif

      prev_frame_jitters = frame_jitters;
      if (device_data.taa_detected)
      {
         // "frame_jitters" is in UV space, and shouldn't go beyond -0.5 and 0.5.
         if (sr_active) // Use the jitters we write
         {
            // TODO: make sure Fog, AO, sun shadow etc aren't affected by jitters (the sun shadow clearly are, in a bad way)
            frame_jitters = GetCurrentJitter(1); // Next frame (only matters with "READ_BACK_JITTERS")
         }
         else // Fall back on original game jitters, without changing them
         {
            bool is_even_frame = (GetGameFrameIndex() + 1) % 2;
            // The game jitter matrix is -0.25 0.25, 0.25 -0.25. Respectively x and y.
            // Just two frames, the standard SMAA T2X jitter pattern.
            if (is_even_frame)
            {
               frame_jitters = float2(-0.25f, 0.25f);
            }
            else
            {
               frame_jitters = float2(0.25f, -0.25f);
            }
         }
      }
      else
      {
         frame_jitters = float2(0.f, 0.f);
         prev_frame_jitters = frame_jitters;
      }
   }

   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;
      reshade::get_config_value(runtime, NAME, "ColorGradingIntensity", cb_luma_global_settings.GameSettings.ColorGradingIntensity);
      reshade::get_config_value(runtime, NAME, "HDRBoostSaturationAmount", cb_luma_global_settings.GameSettings.HDRBoostSaturationAmount);
      reshade::get_config_value(runtime, NAME, "BloomIntensity", cb_luma_global_settings.GameSettings.BloomIntensity);
      // "device_data.cb_luma_global_settings_dirty" should already be true at this point
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;

      ImGui::NewLine();

      if (cb_luma_global_settings.DisplayMode == DisplayModeType::HDR)
      {
         if (ImGui::SliderFloat("Color Grading Intensity", &cb_luma_global_settings.GameSettings.ColorGradingIntensity, 0.f, 1.f))
         {
            reshade::set_config_value(runtime, NAME, "ColorGradingIntensity", cb_luma_global_settings.GameSettings.ColorGradingIntensity);
         }
         DrawResetButton(cb_luma_global_settings.GameSettings.ColorGradingIntensity, 0.8f, "ColorGradingIntensity", runtime);

         if (GetShaderDefineCompiledNumericalValue(char_ptr_crc32("ENABLE_HDR_BOOST")) > 0)
         {
            if (ImGui::SliderFloat("HDR Saturation Boost", &cb_luma_global_settings.GameSettings.HDRBoostSaturationAmount, 0.f, 1.f))
            {
               reshade::set_config_value(runtime, NAME, "HDRBoostSaturationAmount", cb_luma_global_settings.GameSettings.HDRBoostSaturationAmount);
            }
            DrawResetButton(cb_luma_global_settings.GameSettings.HDRBoostSaturationAmount, 0.2f, "HDRBoostSaturationAmount", runtime);
         }
      }

      if (ImGui::SliderFloat("Bloom Intensity", &cb_luma_global_settings.GameSettings.BloomIntensity, 0.f, 1.f))
      {
         reshade::set_config_value(runtime, NAME, "BloomIntensity", cb_luma_global_settings.GameSettings.BloomIntensity);
      }
      DrawResetButton(cb_luma_global_settings.GameSettings.BloomIntensity, 1.0f, "BloomIntensity", runtime);
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("Luma for \"Just Cause 3\" is developed by Pumbo and is open source and free.\nIf you enjoy it, consider donating");

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
#if 0
      static const std::string mod_link = std::string("Nexus Mods Page ") + std::string(ICON_FK_SEARCH);
      if (ImGui::Button(mod_link.c_str()))
      {
         system("start https://www.nexusmods.com/prey2017/mods/149");
      }
#endif
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
         
                  "\n\nContributors:"
                  "\nz1rp"

                  "\n\nThird Party:"
                  "\nReShade"
                  "\nImGui"
                  "");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Just Cause 3 Luma mod");
      Globals::VERSION = 1;

      bool patch_successful = PatchJitters();
      ASSERT(patch_successful);

      swapchain_format_upgrade_type  = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type         = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type   = TextureFormatUpgradesType::AllowedEnabled;
      texture_upgrade_formats = {
         reshade::api::format::r8g8b8a8_unorm,
         reshade::api::format::r8g8b8a8_unorm_srgb,
         reshade::api::format::r8g8b8a8_typeless,
         reshade::api::format::r8g8b8x8_unorm,
         reshade::api::format::r8g8b8x8_unorm_srgb,
         reshade::api::format::b8g8r8a8_unorm,
         reshade::api::format::b8g8r8a8_unorm_srgb,
         reshade::api::format::b8g8r8a8_typeless,
         reshade::api::format::b8g8r8x8_unorm,
         reshade::api::format::b8g8r8x8_unorm_srgb,
         reshade::api::format::b8g8r8x8_typeless,

         reshade::api::format::r11g11b10_float,
      };
      texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;
      enable_indirect_texture_format_upgrades = true;
      enable_chain_indirect_texture_format_upgrades = ChainTextureFormatUpgradesType::DirectDependencies;

      // The game has x16 AA but it doesn't seem to apply to many textures (mainly because it literally used linear samplers instead of AF for many...)
      enable_samplers_upgrade = true;

      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 11; // 12 is used

      // There's many...
      redirected_shader_hashes["Tonemap"] =
         {
            "01F41F2D",
            "0BAC4255",
            "0C35F299",
            "148CD952",
            "15BC0ABC",
            "1C087BA1",
            "2319D5A4",
            "2607E7C0",
            "288B16D3",
            "2FB48E77",
            "371AD4D5",
            "3753CA0A",
            "38ABE9E7",
            "3EC5DBB9",
            "4030BF6E",
            "49704266",
            "4A9BFEC5",
            "6A1C711F",
            "6C0BCB6B",
            "6F6BFEDA",
            "75190444",
            "79193F1D",
            "7CF7827A",
            "7F138E1C",
            "83CC89FB",
            "8610E7F5",
            "87F34BAA",
            "8D59471A",
            "8D8F7072",
            "92550B56",
            "96DA986B",
            "9C62A6F9",
            "9D857B42",
            "A274F081",
            "A91CF149",
            "A91F8AB9",
            "A9CEF67D",
            "ADAFB4CD",
            "BCF2BA69",
            "BF1F1C29",
            "C16B4E6B",
            "D0F9B11B",
            "D4B1C6E9",
            "DC0FE377",
            "DED46AD7",
            "E1ECF661",
            "F21C9CBA",
            "F4E80E62",
            "FA0676EF",
            "FA796E93",
            "FDBDB73F",
         };

      shader_hashes_Tonemapper.pixel_shaders = {
         0x01F41F2D,
         0x0BAC4255,
         0x0C35F299,
         0x148CD952,
         0x15BC0ABC,
         0x1C087BA1,
         0x2319D5A4,
         0x2607E7C0,
         0x288B16D3,
         0x2FB48E77,
         0x371AD4D5,
         0x3753CA0A,
         0x38ABE9E7,
         0x3EC5DBB9,
         0x4030BF6E,
         0x49704266,
         0x4A9BFEC5,
         0x6A1C711F,
         0x6C0BCB6B,
         0x6F6BFEDA,
         0x75190444,
         0x79193F1D,
         0x7CF7827A,
         0x7F138E1C,
         0x83CC89FB,
         0x8610E7F5,
         0x87F34BAA,
         0x8D59471A,
         0x8D8F7072,
         0x92550B56,
         0x96DA986B,
         0x9C62A6F9,
         0x9D857B42,
         0xA274F081,
         0xA91CF149,
         0xA91F8AB9,
         0xA9CEF67D,
         0xADAFB4CD,
         0xBCF2BA69,
         0xBF1F1C29,
         0xC16B4E6B,
         0xD0F9B11B,
         0xD4B1C6E9,
         0xDC0FE377,
         0xDED46AD7,
         0xE1ECF661,
         0xF21C9CBA,
         0xF4E80E62,
         0xFA0676EF,
         0xFA796E93,
         0xFDBDB73F,
      };
      shader_hashes_DownscaleBlur.pixel_shaders = { 0x8FE1772E };
      shader_hashes_Blur.pixel_shaders = { 0xF31D7D22 };
      shader_hashes_SwapchainCopy.pixel_shaders = { 0x3DA3DB98 };
      shader_hashes_PauseBackgroundCopy.pixel_shaders = { 0x018C83B7 };
      shader_hashes_EarlyAutoExposure.pixel_shaders = { 0x71D05CE4 };
      shader_hashes_LateAutoExposure.pixel_shaders = { 0x23B61EDE };
      shader_hashes_SMAA_EdgeDetection.pixel_shaders = { 0x60EB1F22, 0x5040BB59 };
      shader_hashes_SMAA.pixel_shaders = { 0x8A824E55 };
      shader_hashes_SMAA_2TX.pixel_shaders = { 0xF7078237 };
      shader_hashes_GenMotionVectors.pixel_shaders = { 0xA1037803, 0x2E0AC461 };
      shader_hashes_GenMotionVectors_TAA.pixel_shaders = { 0xA1037803 };
      shader_hashes_TerrainMaterials.pixel_shaders = { 0x87E0C350, 0xE2313E10, 0x95A485D4 };

      // Defaults are hardcoded in ImGUI too
      cb_luma_global_settings.GameSettings.ColorGradingIntensity = 0.8f; // Don't default to 1 (vanilla) because it's too saturated and hue shifted
      cb_luma_global_settings.GameSettings.HDRBoostSaturationAmount = 0.2f;
      cb_luma_global_settings.GameSettings.BloomIntensity = 1.0f;
      // TODO: use "default_luma_global_game_settings"

#if DEVELOPMENT
      forced_shader_names.emplace(Shader::Hash_StrToNum("60EB1F22"), "SMAA Edge Detection 1");
      forced_shader_names.emplace(Shader::Hash_StrToNum("5DB69E08"), "SMAA Edge Detection 2");
      forced_shader_names.emplace(Shader::Hash_StrToNum("8A824E55"), "SMAA");

      forced_shader_names.emplace(Shader::Hash_StrToNum("D8C32AC0"), "Sky/Stars");
      forced_shader_names.emplace(Shader::Hash_StrToNum("Clouds"), "Heat Distortion");

      forced_shader_names.emplace(Shader::Hash_StrToNum("47827156"), "Heat Distortion");

      forced_shader_names.emplace(Shader::Hash_StrToNum("87E0C350"), "Close Terrain");
      forced_shader_names.emplace(Shader::Hash_StrToNum("E2313E10"), "Mid Terrain");
      forced_shader_names.emplace(Shader::Hash_StrToNum("95A485D4"), "Far Terrain");

      forced_shader_names.emplace(Shader::Hash_StrToNum("592575B0"), "Clear 4 Textures to Black");

      forced_shader_names.emplace(Shader::Hash_StrToNum("CA7DFA32"), "GBuffers composition"); // One of the many?
#endif

      game = new JustCause3();
   }
   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
#if 0 // TODO: restore the original memory
      // Disabled as we'd need to restore the previous values. It's not really needed in this game.
      if (jump_memory)
         VirtualFree(jump_memory, 0, MEM_RELEASE);
#endif
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}
