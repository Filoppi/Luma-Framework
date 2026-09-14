#define GAME_PERSONA_5_ROYAL 1

#define ALLOW_SHADERS_DUMPING 0

#include "..\..\Core\core.hpp"
#define XXH_STATIC_LINKING_ONLY
#define XXH_IMPLEMENTATION
#include "xxhash.h"

enum class FramePhase
{
   SHADOW_MAP,
   REFLECTION, // planar reflections are rarely used, one place is in Madarames Palace just outside the central garden save room
   GBUFFER,
   LIGHTING,
   DEFERRED,
   POSTPROCESSING_AND_UI,
   UI_ONLY
};

struct ReplacementTexture
{
   ComPtr<ID3D11Texture2D> texture;
   ComPtr<ID3D11ShaderResourceView> srv;
   ComPtr<ID3D11RenderTargetView> rtv;
   D3D11_TEXTURE2D_DESC desc;
   bool in_use = false;
};

struct GFD_VSCONST_VIEWPROJ
{
   float4x4 mtxViewProj;
   float4x4 mtxView;
   float3 eyePosition;
   float _reserved_b2;
   float4x4 mtxPrevViewProj;
};

namespace
{
   uint32_t g_shadow_map_size_override = 0;

   float2 projection_jitters = {0, 0};
   ShaderHashesList shader_hashes_light;
   ShaderHashesList shader_hashes_bloom_select;
   ShaderHashesList shader_hashes_bloom_filter;
   ShaderHashesList shader_hashes_copy;
   ShaderHashesList shader_hashes_blur;
   ShaderHashesList shader_hashes_fxaa;
   ShaderHashesList shader_hashes_smaa_edge_detection;
   ShaderHashesList shader_hashes_smaa_weight_calculation;
   ShaderHashesList shader_hashes_smaa_blending;
   ShaderHashesList shader_hashes_ui;
   uint64_t hash_identity = 0;

   uint8_t* jump_memory = nullptr;

   bool PatchSamplerStates()
   {
      constexpr size_t stolenLen = 18; // Length of bytes we are overwriting

      const HMODULE hModule = GetModuleHandleA(nullptr);
      const uintptr_t baseAddr = (uintptr_t)hModule;

      auto dosHeader = (PIMAGE_DOS_HEADER)baseAddr;
      auto ntHeaders = (PIMAGE_NT_HEADERS)baseAddr + dosHeader->e_lfanew;

      // only search in the first 25 MB that's where all the code is thanks to Denuvo the exe is unnecessarily large
      std::size_t sectionSize = min(ntHeaders->OptionalHeader.SizeOfImage, 25U * 1024U * 1024U);

      // movss xmm2, [rcx+2CCh], jnz 5, mov xmm2, ... only occurs where we need it in every version released on steam so far
      std::vector<std::byte> pattern = {std::byte{0xf3}, std::byte{0x0f}, std::byte{0x10}, std::byte{0x91},
         std::byte{0xcc}, std::byte{0x02}, std::byte{0x00}, std::byte{0x00},
         std::byte{0x75}, std::byte{0x0a}, std::byte{0xf3}, std::byte{0x0f},
         std::byte{0x10}, std::byte{0x15}};
      std::vector<std::byte*> patternAddr = System::ScanMemoryForPattern((std::byte*)baseAddr, sectionSize, pattern);

      if (patternAddr.empty())
      {
         return false;
      }

      uintptr_t patchAddr = reinterpret_cast<uintptr_t>(patternAddr[0]);

      uint32_t mipBiasOffset;
      memcpy(&mipBiasOffset, (void*)(patchAddr + 14), sizeof(mipBiasOffset));
      uintptr_t mipBiasAddr = patchAddr + 18 + mipBiasOffset;

      uint8_t earlyOutOffset;
      memcpy(&earlyOutOffset, (void*)(patchAddr + 9), sizeof(earlyOutOffset));
      uintptr_t earlyOutAddr = patchAddr + 10 + earlyOutOffset;

      uint8_t returnOffset;
      memcpy(&returnOffset, (void*)(patchAddr + 19), sizeof(returnOffset));
      uintptr_t returnAddr = patchAddr + 20 + returnOffset;

      // when the resolution dependent mip bias is zero we add FLT_MIN so we can detect it and not
      // upgrade the sampler state
      std::vector<uint8_t> shellcode = {
         0xf3, 0x0f, 0x10, 0x91, 0xcc, 0x02, 0x00, 0x00,             // movss xmm2, dword ptr [rcx+2CCh]
         0x75, 0x30,                                                 // jne early out
         0x48, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // mov rax, [mip bias addr]
         0xf3, 0x0f, 0x10, 0x10,                                     // movss xmm2, rax
         0x0f, 0x57, 0xc9,                                           // xorps xmm1, xmm1
         0x0f, 0x2e, 0xd1,                                           // ucomiss xmm2, xmm1
         0x75, 0x0D,                                                 // jne return
         0xb8, 0x00, 0x00, 0x80, 0x00,                               // mov eax, 0x800000
         0x66, 0x0f, 0x6e, 0xc8,                                     // movd xmm1, eax
         0xf3, 0x0f, 0x58, 0xd1,                                     // addss xmm2, xmm1
         0x49, 0xba, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // return: mov r10, [return address 1]
         0x41, 0xff, 0xe2,                                           // jmp r10
         0x49, 0xba, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // early out: mov r10, [return address 2]
         0x41, 0xff, 0xe2                                            // jmp r10
      };

      size_t allocSize = shellcode.size() + 16; // Add 16 for extra safety
      jump_memory = (uint8_t*)VirtualAlloc(nullptr, allocSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
      if (!jump_memory)
         return false;

      uintptr_t codeAddr = (uintptr_t)jump_memory;

      memcpy(&shellcode[12], &mipBiasAddr, sizeof(void*));
      memcpy(&shellcode[47], &returnAddr, sizeof(void*));
      memcpy(&shellcode[60], &earlyOutAddr, sizeof(void*));
      memcpy((void*)codeAddr, shellcode.data(), shellcode.size());

      FlushInstructionCache(GetCurrentProcess(), (void*)codeAddr, shellcode.size());

      DWORD oldProtect;
      BOOL success = VirtualProtect((void*)patchAddr, stolenLen, PAGE_EXECUTE_READWRITE, &oldProtect);
      if (success)
      {
         // Build jump to shellcode
         uint8_t jmpToShellcode[13] = {0x49, 0xba, 0, 0, 0, 0, 0, 0, 0, 0, 0x41, 0xff, 0xe2}; // mov r10, [addr]; jmp r10
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
} // namespace

struct GameDeviceDataPersona5Royal final : public GameDeviceData
{
#if ENABLE_SR
   // SR
   std::atomic<bool> has_drawn_upscaling = false;

   // resources used to identify the deferred context used for scene drawing
   ComPtr<ID3D11CommandList> remainder_command_list;
   std::atomic<ID3D11DeviceContext*> draw_device_context = nullptr;

   // textures we got from the game
   ComPtr<ID3D11Texture2D> source_color;
   ComPtr<ID3D11Resource> depth_texture;
   ComPtr<ID3D11Texture2D> motion_vectors;

   // the command list we split to interject dlss
   ComPtr<ID3D11CommandList> partial_command_list;

   // resources used to apply sr
   ComPtr<ID3D11Texture2D> decoded_motion_vectors;
   ComPtr<ID3D11UnorderedAccessView> decoded_motion_vectors_uav;
   ComPtr<ID3D11Texture2D> resolve_texture;
   ComPtr<ID3D11Texture2D> merged_texture;
   ComPtr<ID3D11UnorderedAccessView> merged_texture_uav;
   ComPtr<ID3D11ShaderResourceView> merged_texture_srv;
   ComPtr<ID3D11RenderTargetView> merged_texture_rtv;

   // pool for replacement textures
   std::vector<ReplacementTexture> replacement_textures;
   // active replacements for the current frame
   std::unordered_map<ID3D11Resource*, uint32_t> current_replacements;
   // the game uses this to draw geometry for the UI this is the only resource that gets mapped
   // after the bloom effect, as constant buffers are updated with UpdateSubresource
   ComPtr<ID3D11Buffer> modifiable_index_vertex_buffer;
   uint2 render_resolution = {};
   uint2 upscale_resolution = {};
   uint2 target_resolution = {};
   uint2 last_viewport_size = {};
   float fov = 0.0f;

   // variables used to fix motion vectors on non-skinned moving objects
   std::unordered_map<uint64_t, float4x4> prev_local_to_view_lookup;
   std::unordered_map<uint64_t, float4x4> local_to_view_lookup;
   std::unordered_map<ID3D11Buffer*, std::array<uint8_t, 7168>> cbuffer_cache;
   std::atomic<ID3D11Buffer*> cb_transform = nullptr;
#endif // ENABLE_SR
   ComPtr<ID3D11Buffer> scratch_constant_buffer;
   ComPtr<ID3D11UnorderedAccessView> scratch_constant_buffer_uav;

   FramePhase frame_phase = FramePhase::SHADOW_MAP;
   bool render_target_changed = false;
};

class Persona5Royal final : public Game
{
   static GameDeviceDataPersona5Royal& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceDataPersona5Royal*>(device_data.game);
   }

   static bool SrActive(const DeviceData& device_data)
   {
      return device_data.sr_type != SR::Type::None && !device_data.sr_suppressed;
   }

public:
   void OnInit(bool async) override
   {
      native_shaders_definitions.emplace(CompileTimeStringHash("Update Shadow Constants"), ShaderDefinition{"Luma_UpdateShadowConstants", reshade::api::pipeline_subobject_type::compute_shader});
      native_shaders_definitions.emplace(CompileTimeStringHash("Decode Motion Vector"), ShaderDefinition{"Luma_DecodeMotionVector", reshade::api::pipeline_subobject_type::compute_shader});
      native_shaders_definitions.emplace(CompileTimeStringHash("Merge"), ShaderDefinition{"Luma_CopyDsrResult", reshade::api::pipeline_subobject_type::compute_shader});
      native_shaders_definitions.emplace(CompileTimeStringHash("Copy RGB 1 A"), ShaderDefinition{"Luma_Copy_RGB", reshade::api::pipeline_subobject_type::pixel_shader});

      reshade::register_event<reshade::addon_event::execute_secondary_command_list>(Persona5Royal::OnExecuteSecondaryCommandList);
      reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(Persona5Royal::OnBindRenderTargetsAndDepthStencil);
      reshade::register_event<reshade::addon_event::map_buffer_region>(Persona5Royal::OnMapBufferRegion);
      reshade::register_event<reshade::addon_event::update_buffer_region_command>(Persona5Royal::OnUpdateBufferRegionCommand);
      reshade::register_event<reshade::addon_event::create_resource>(Persona5Royal::OnCreateResource);
      reshade::register_event<reshade::addon_event::bind_viewports>(Persona5Royal::OnBindViewports);

      float4x4 identity = {};
      identity.m00 = 1.0f;
      identity.m11 = 1.0f;
      identity.m22 = 1.0f;
      identity.m33 = 1.0f;
      hash_identity = XXH3_64bits((const uint8_t*)&identity, sizeof(identity));
   }

   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;
      if (!reshade::get_config_value(runtime, NAME, "ShadowMapSizeOverride", g_shadow_map_size_override))
      {
         // older versions were saving the setting under the wrong key
         reshade::get_config_value(runtime, NAME, "shadow_map_size_override", g_shadow_map_size_override);
      }
   }

   void OnInitSwapchain(reshade::api::swapchain* swapchain) override
   {
      auto& device_data = *swapchain->get_device()->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);
      game_device_data.target_resolution.x = device_data.output_resolution.x;
      game_device_data.target_resolution.y = device_data.output_resolution.y;

      // unless the ultra wide screen mod is installed output resolution is alway 16:9
      if ((float)game_device_data.target_resolution.x / (float)game_device_data.target_resolution.y < 16.0f / 9.0f)
      {
         game_device_data.target_resolution.y = (game_device_data.target_resolution.x / 16) * 9;
      }
      else if ((float)game_device_data.target_resolution.x / (float)game_device_data.target_resolution.y > 16.0f / 9.0f)
      {
         game_device_data.target_resolution.x = (game_device_data.target_resolution.y / 9) * 16;
      }
   }

   void OnInitDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      {
         D3D11_BUFFER_DESC bd;
         bd.ByteWidth = 80;
         bd.Usage = D3D11_USAGE_DEFAULT;
         bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
         bd.CPUAccessFlags = 0;
         bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
         bd.StructureByteStride = 80;
         native_device->CreateBuffer(&bd, nullptr, game_device_data.scratch_constant_buffer.put());
      }

      {
         D3D11_UNORDERED_ACCESS_VIEW_DESC uavd;
         uavd.Format = DXGI_FORMAT_UNKNOWN;
         uavd.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
         uavd.Buffer.FirstElement = 0;
         uavd.Buffer.Flags = 0;
         uavd.Buffer.NumElements = 1;
         native_device->CreateUnorderedAccessView(game_device_data.scratch_constant_buffer.get(), &uavd, game_device_data.scratch_constant_buffer_uav.put());
      }

      // no taa but needed for DLSS indicator in UI
      device_data.taa_detected = true;
   }

   void SetupSr(ID3D11DeviceContext* native_device_context, GameDeviceDataPersona5Royal& game_device_data, DeviceData& device_data)
   {
      ComPtr<ID3D11Device> device;
      native_device_context->GetDevice(device.put());

      D3D11_TEXTURE2D_DESC target_desc;
      game_device_data.source_color->GetDesc(&target_desc);

      uint32_t width = target_desc.Width;
      uint32_t height = target_desc.Height;

      uint32_t output_width;
      uint32_t output_height;

      if (game_device_data.target_resolution.x > width &&
          game_device_data.target_resolution.y > height)
      {
         output_width = game_device_data.target_resolution.x;
         output_height = game_device_data.target_resolution.y;
      }
      else
      {
         output_width = width;
         output_height = height;
      }

      if (game_device_data.upscale_resolution.x != output_width ||
          game_device_data.upscale_resolution.y != output_height ||
          game_device_data.render_resolution.x != width ||
          game_device_data.render_resolution.y != height)
      {
         cb_luma_global_settings.GameSettings.RenderRes = {(float)width, (float)height};
         cb_luma_global_settings.GameSettings.InvRenderRes = {1.0f / (float)width, 1.0f / (float)height};
         cb_luma_global_settings.GameSettings.OutputRes = {(float)output_width, (float)output_height};
         cb_luma_global_settings.GameSettings.InvOutputRes = {1.0f / (float)output_width, 1.0f / (float)output_height};
         cb_luma_global_settings.GameSettings.RenderScale = (float)width / (float)output_width;
         cb_luma_global_settings.GameSettings.InvRenderScale = 1.0f / cb_luma_global_settings.GameSettings.RenderScale;
         device_data.cb_luma_global_settings_dirty = true;
         {
            D3D11_TEXTURE2D_DESC motion_vector_desc;
            motion_vector_desc.Width = width;
            motion_vector_desc.Height = height;
            motion_vector_desc.Usage = D3D11_USAGE_DEFAULT;
            motion_vector_desc.ArraySize = 1;
            motion_vector_desc.Format = DXGI_FORMAT_R16G16_FLOAT;
            motion_vector_desc.SampleDesc.Count = 1;
            motion_vector_desc.SampleDesc.Quality = 0;
            motion_vector_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            motion_vector_desc.CPUAccessFlags = 0;
            motion_vector_desc.MiscFlags = 0;
            motion_vector_desc.MipLevels = 1;

            device->CreateTexture2D(&motion_vector_desc,
               nullptr,
               game_device_data.decoded_motion_vectors.put());
         }
         {
            D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc;
            uav_desc.Format = DXGI_FORMAT_R16G16_FLOAT;
            uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
            uav_desc.Texture2D.MipSlice = 0;

            device->CreateUnorderedAccessView(game_device_data.decoded_motion_vectors.get(),
               &uav_desc,
               game_device_data.decoded_motion_vectors_uav.put());
         }
         {
            D3D11_TEXTURE2D_DESC desc;
            desc.Width = output_width;
            desc.Height = output_height;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.ArraySize = 1;
            desc.Format = target_desc.Format;
            desc.SampleDesc.Count = 1;
            desc.SampleDesc.Quality = 0;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            desc.CPUAccessFlags = 0;
            desc.MiscFlags = 0;
            desc.MipLevels = 1;

            device->CreateTexture2D(&desc,
               nullptr,
               game_device_data.resolve_texture.put());
         }
         {
            D3D11_TEXTURE2D_DESC desc;
            desc.Width = output_width;
            desc.Height = output_height;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.ArraySize = 1;
            desc.Format = target_desc.Format;
            desc.SampleDesc.Count = 1;
            desc.SampleDesc.Quality = 0;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS;
            desc.CPUAccessFlags = 0;
            desc.MiscFlags = 0;
            desc.MipLevels = 1;

            device->CreateTexture2D(&desc,
               nullptr,
               game_device_data.merged_texture.put());
         }
         {
            D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
            srv_desc.Format = target_desc.Format;
            srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srv_desc.Texture2D.MostDetailedMip = 0;
            srv_desc.Texture2D.MipLevels = 1;

            device->CreateShaderResourceView(game_device_data.merged_texture.get(),
               &srv_desc,
               game_device_data.merged_texture_srv.put());
         }
         {
            D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
            rtv_desc.Format = target_desc.Format;
            rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            rtv_desc.Texture2D.MipSlice = 0;

            device->CreateRenderTargetView(game_device_data.merged_texture.get(),
               &rtv_desc,
               game_device_data.merged_texture_rtv.put());
         }
         {
            D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc;
            uavDesc.Format = target_desc.Format;
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
            uavDesc.Texture2D.MipSlice = 0;

            device->CreateUnorderedAccessView(game_device_data.merged_texture.get(),
               &uavDesc,
               game_device_data.merged_texture_uav.put());
         }

         game_device_data.replacement_textures.clear();
         game_device_data.current_replacements.clear();

         float clear[] = {0.0f, 0.0f, 0.0f, 0.0f};
         native_device_context->ClearUnorderedAccessViewFloat(game_device_data.decoded_motion_vectors_uav.get(), clear);

         game_device_data.render_resolution.x = width;
         game_device_data.render_resolution.y = height;
         game_device_data.upscale_resolution.x = output_width;
         game_device_data.upscale_resolution.y = output_height;
      }
   }

   ID3D11RenderTargetView* GetPostProcessRtv(const D3D11_TEXTURE2D_DESC& texture_desc, ID3D11Resource* resource, GameDeviceDataPersona5Royal& game_device_data)
   {
      for (size_t i = 0; i < game_device_data.replacement_textures.size(); ++i)
      {
         if (game_device_data.replacement_textures[i].in_use)
         {
            continue;
         }
         if (memcmp(&texture_desc, &game_device_data.replacement_textures[i].desc, sizeof(texture_desc)) == 0)
         {
            game_device_data.current_replacements[resource] = i;
            game_device_data.replacement_textures[i].in_use = true;
            return game_device_data.replacement_textures[i].rtv.get();
         }
      }

      ComPtr<ID3D11Device> device;
      resource->GetDevice(device.put());

      ReplacementTexture replacement_texture;
      device->CreateTexture2D(&texture_desc,
         nullptr,
         replacement_texture.texture.put());

      DXGI_FORMAT format = texture_desc.Format;
      if (format == DXGI_FORMAT_R8G8B8A8_TYPELESS)
      {
         format = DXGI_FORMAT_R8G8B8A8_UNORM;
      }
      else if (format == DXGI_FORMAT_R16G16B16A16_TYPELESS) // compatibility with render targets upgraded by RenoDX
      {
         format = DXGI_FORMAT_R16G16B16A16_UNORM;
      }

      D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
      srv_desc.Format = format;
      srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
      srv_desc.Texture2D.MostDetailedMip = 0;
      srv_desc.Texture2D.MipLevels = 1;
      device->CreateShaderResourceView(replacement_texture.texture.get(),
         &srv_desc,
         replacement_texture.srv.put());

      D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
      rtv_desc.Format = format;
      rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
      rtv_desc.Texture2D.MipSlice = 0;
      device->CreateRenderTargetView(replacement_texture.texture.get(),
         &rtv_desc,
         replacement_texture.rtv.put());

      replacement_texture.desc = texture_desc;
      replacement_texture.in_use = true;
      game_device_data.replacement_textures.push_back(replacement_texture);
      game_device_data.current_replacements[resource] = game_device_data.replacement_textures.size() - 1;

      return replacement_texture.rtv.get();
   }

   ID3D11RenderTargetView* GetPostProcessRtvOutputRes(ID3D11RenderTargetView* rtv, GameDeviceDataPersona5Royal& game_device_data, uint2& resolution)
   {
      ComPtr<ID3D11Resource> resource;
      rtv->GetResource(resource.put());

      if (game_device_data.current_replacements.contains(resource.get()))
      {
         auto& replacement = game_device_data.replacement_textures[game_device_data.current_replacements[resource.get()]];
         // should always be game_device_data.upscale_resolution but if a hash for a bloom shader is missing and an rtv
         // is reused we still might end up with a smaller render target here
         resolution = {replacement.desc.Width, replacement.desc.Height};
         return game_device_data.replacement_textures[game_device_data.current_replacements[resource.get()]].rtv.get();
      }

      if (resource.get() == (ID3D11Texture2D*)game_device_data.source_color.get())
      {
         resolution = {game_device_data.upscale_resolution.x, game_device_data.upscale_resolution.y};
         return game_device_data.merged_texture_rtv.get();
      }

      ComPtr<ID3D11Texture2D> texture;
      resource->QueryInterface(texture.put());

      D3D11_TEXTURE2D_DESC texture_desc;
      texture->GetDesc(&texture_desc);
      if (texture_desc.Width != game_device_data.render_resolution.x || texture_desc.Height != game_device_data.render_resolution.y)
      {
         resolution = {texture_desc.Width, texture_desc.Height};
         return rtv;
      }
      resolution.x = texture_desc.Width = game_device_data.upscale_resolution.x;
      resolution.y = texture_desc.Height = game_device_data.upscale_resolution.y;

      return GetPostProcessRtv(texture_desc, resource.get(), game_device_data);
   }

   ID3D11RenderTargetView* GetPostProcessRtvScaled(ID3D11RenderTargetView* rtv, GameDeviceDataPersona5Royal& game_device_data, uint2& resolution)
   {
      ComPtr<ID3D11Resource> resource;
      rtv->GetResource(resource.put());

      if (game_device_data.current_replacements.contains(resource.get()))
      {
         auto& replacement = game_device_data.replacement_textures[game_device_data.current_replacements[resource.get()]];
         resolution = {replacement.desc.Width, replacement.desc.Height};
         return replacement.rtv.get();
      }

      if (resource.get() == (ID3D11Texture2D*)game_device_data.source_color.get())
      {
         resolution = {game_device_data.upscale_resolution.x, game_device_data.upscale_resolution.y};
         return game_device_data.merged_texture_rtv.get();
      }

      ComPtr<ID3D11Texture2D> texture;
      resource->QueryInterface(texture.put());

      D3D11_TEXTURE2D_DESC texture_desc;
      texture->GetDesc(&texture_desc);
      if (texture_desc.Width >= game_device_data.upscale_resolution.x || texture_desc.Height >= game_device_data.upscale_resolution.y)
      {
         resolution = {texture_desc.Width, texture_desc.Height};
         return rtv;
      }
      resolution.x = texture_desc.Width = (uint32_t)((float)texture_desc.Width * cb_luma_global_settings.GameSettings.InvRenderScale);
      resolution.y = texture_desc.Height = (uint32_t)((float)texture_desc.Height * cb_luma_global_settings.GameSettings.InvRenderScale);

      return GetPostProcessRtv(texture_desc, resource.get(), game_device_data);
   }

   ID3D11ShaderResourceView* GetPostProcessSrv(ID3D11ShaderResourceView* srv, GameDeviceDataPersona5Royal& game_device_data)
   {
      ComPtr<ID3D11Resource> resource;
      srv->GetResource(resource.put());

      if (game_device_data.current_replacements.contains(resource.get()))
      {
         return game_device_data.replacement_textures[game_device_data.current_replacements[resource.get()]].srv.get();
      }

      if (resource.get() == (ID3D11Texture2D*)game_device_data.source_color.get())
      {
         return game_device_data.merged_texture_srv.get();
      }

      return srv;
   }

   static bool HandleTransformUpdate(ID3D11Buffer* buffer, const void* data, ID3D11DeviceContext* native_device_context, GameDeviceDataPersona5Royal& game_device_data, DeviceData& device_data)
   {
      // the constant buffer GFD_VSCONST_TRANSFORM contains float4x4 mtxLocalToWorld, float4x4x mtxPrevLocalToWorld
      // though at least for objects attached to bones mtxPrevLocalToWorld actually contains a transform matrix for
      // the next frame instead of the previous, so we need to build a lookup for the actual previous transforms here
      // and also apply the values we collected in the previous frame
      uint64_t hash_current = XXH3_64bits((const uint8_t*)data, sizeof(float4x4));

      // skinned mesh vertex positions are already in view space
      if (hash_current == hash_identity)
      {
         return false;
      }

      uint64_t hash_prev = XXH3_64bits((const uint8_t*)data + sizeof(float4x4), sizeof(float4x4));

      game_device_data.local_to_view_lookup[hash_prev] = ((float4x4*)data)[0];

      auto it = game_device_data.prev_local_to_view_lookup.find(hash_current);
      if (hash_current == hash_prev || it == game_device_data.prev_local_to_view_lookup.cend())
      {
         return false;
      }

      thread_local static float4x4 transforms[448];
      transforms[0] = ((float4x4*)data)[0];
      transforms[1] = it->second;
      native_device_context->UpdateSubresource(buffer, 0, nullptr, &transforms[0], 0, 0);

      return true;
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      if ((stages & reshade::api::shader_stage::vertex) == 0)
      {
         return DrawOrDispatchOverrideType::None;
      }
      auto& game_device_data = GetGameDeviceData(device_data);

      auto CheckAndHandleRenderPassTransition = [native_device_context, &cmd_list_data, &device_data, &game_device_data](ID3D11RenderTargetView* render_target_view_1, ID3D11DepthStencilView* depth_stencil_view)
      {
         ComPtr<ID3D11Resource> resource;
         render_target_view_1->GetResource(resource.put());
         if (!resource)
         {
            return DrawOrDispatchOverrideType::None;
         }
         ComPtr<ID3D11Texture2D> tex;
         resource->QueryInterface(tex.put());
         if (!tex)
         {
            return DrawOrDispatchOverrideType::None;
         }
         D3D11_TEXTURE2D_DESC tex_desc;
         tex->GetDesc(&tex_desc);

         // the normal gbuffer is DXGI_FORMAT_R10G10B10A2_UNORM (or DXGI_FORMAT_R16G16B16A16_FLOAT when upgraded by renodx)
         // for planar reflections render target 1 is DXGI_FORMAT_R8G8B8A8_UNORM
         if (tex_desc.Format != DXGI_FORMAT_R10G10B10A2_UNORM &&
             tex_desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT)
         {
            game_device_data.frame_phase = FramePhase::REFLECTION;
         }
         else
         {
            game_device_data.frame_phase = FramePhase::GBUFFER;

            if (SrActive(device_data))
            {
               depth_stencil_view->GetResource(game_device_data.depth_texture.put());

               ComPtr<ID3D11Resource> renderTargetResource;
               render_target_view_1->GetResource(renderTargetResource.put());

               renderTargetResource->QueryInterface(game_device_data.motion_vectors.put());

               D3D11_TEXTURE2D_DESC target_desc;
               game_device_data.motion_vectors->GetDesc(&target_desc);

               ComPtr<ID3D11Buffer> constant_buffers[2];
               {
                  ID3D11Buffer* constant_buffers_raw[2];
                  native_device_context->VSGetConstantBuffers(1, 2, &constant_buffers_raw[0]);
                  constant_buffers[0].attach(constant_buffers_raw[0]);
                  constant_buffers[1].attach(constant_buffers_raw[1]);
               }

               if (constant_buffers[0])
               {
                  game_device_data.cb_transform = constant_buffers[0].get();

                  auto it = game_device_data.cbuffer_cache.find(constant_buffers[0].get());
                  if (it != game_device_data.cbuffer_cache.cend())
                  {
                     HandleTransformUpdate(constant_buffers[0].get(), it->second.data(), native_device_context, game_device_data, device_data);
                  }
               }

               if (constant_buffers[1])
               {
                  auto it = game_device_data.cbuffer_cache.find(constant_buffers[1].get());
                  if (it != game_device_data.cbuffer_cache.cend())
                  {
                     GFD_VSCONST_VIEWPROJ* view_proj_data = (GFD_VSCONST_VIEWPROJ*)it->second.data();
                     float4x4 inv_view = view_proj_data->mtxView.GetTransposed().GetInverted();
                     float4x4 proj = inv_view * view_proj_data->mtxViewProj.GetTransposed();
                     float4x4 inv_proj = proj.GetInverted();
                     // assume that projection doesn't change between frames
                     float4x4 prev_view = view_proj_data->mtxPrevViewProj.GetTransposed() * inv_proj;

                     proj.m20 -= 2.0f * projection_jitters.x / (float)target_desc.Width;
                     proj.m21 += 2.0f * projection_jitters.y / (float)target_desc.Height;

                     view_proj_data->mtxViewProj = (view_proj_data->mtxView.GetTransposed() * proj).GetTransposed();
                     view_proj_data->mtxPrevViewProj = (prev_view * proj).GetTransposed();

                     native_device_context->UpdateSubresource(constant_buffers[1].get(), 0, nullptr, it->second.data(), 0, 0);

                     game_device_data.fov = 2.0f * atan(1.0f / proj.m11);
                  }
               }
            }
         }
         return DrawOrDispatchOverrideType::None;
      };

      if (game_device_data.frame_phase == FramePhase::SHADOW_MAP)
      {
         if (original_shader_hashes.Contains(shader_hashes_ui))
         {
            game_device_data.frame_phase = FramePhase::UI_ONLY;
            return DrawOrDispatchOverrideType::None;
         }
         if (!game_device_data.render_target_changed)
         {
            return DrawOrDispatchOverrideType::None;
         }

         game_device_data.render_target_changed = false;

         ComPtr<ID3D11DepthStencilView> depth_stencil_view;
         ComPtr<ID3D11RenderTargetView> render_target_views[2];
         {
            ID3D11RenderTargetView* render_target_views_raw[2];
            native_device_context->OMGetRenderTargets(2, &render_target_views_raw[0], depth_stencil_view.put());
            render_target_views[0].attach(render_target_views_raw[0]);
            render_target_views[1].attach(render_target_views_raw[1]);
         }

         if (!depth_stencil_view)
         {
            return DrawOrDispatchOverrideType::None;
         }

         if (render_target_views[0] &&
             render_target_views[1])
         {
            return CheckAndHandleRenderPassTransition(render_target_views[1].get(), depth_stencil_view.get());
         }

         ComPtr<ID3D11Resource> depth_stencil_resource;
         depth_stencil_view->GetResource(depth_stencil_resource.put());
         if (!depth_stencil_resource)
         {
            return DrawOrDispatchOverrideType::None;
         }
         ComPtr<ID3D11Texture2D> depth_stencil_texture;
         depth_stencil_resource->QueryInterface(depth_stencil_texture.put());
         if (!depth_stencil_texture)
         {
            return DrawOrDispatchOverrideType::None;
         }
         D3D11_TEXTURE2D_DESC tex_desc;
         depth_stencil_texture->GetDesc(&tex_desc);
         cb_luma_global_settings.GameSettings.ShadowRes = tex_desc.Width;
         cb_luma_global_settings.GameSettings.InvShadowRes = 1.0f / cb_luma_global_settings.GameSettings.ShadowRes;
         device_data.cb_luma_global_settings_dirty = true;

         UINT viewport_count = 1;
         D3D11_VIEWPORT viewport;
         native_device_context->RSGetViewports(&viewport_count, &viewport);
         if (viewport_count > 0 &&
             viewport.Width == 2048 &&
             viewport.Height == 2048)
         {
            viewport.Width = viewport.Height = cb_luma_global_settings.GameSettings.ShadowRes;
            native_device_context->RSSetViewports(1, &viewport);
         }
         UINT rect_count = 1;
         D3D11_RECT scissor_rect;
         native_device_context->RSGetScissorRects(&rect_count, &scissor_rect);
         if (rect_count > 0 &&
             scissor_rect.right == 2048 &&
             scissor_rect.bottom == 2048)
         {
            scissor_rect.right = scissor_rect.bottom = cb_luma_global_settings.GameSettings.ShadowRes;

            native_device_context->RSSetScissorRects(1, &scissor_rect);
         }
      }
      else if (game_device_data.frame_phase == FramePhase::REFLECTION)
      {
         if (!game_device_data.render_target_changed)
         {
            return DrawOrDispatchOverrideType::None;
         }
         game_device_data.render_target_changed = false;

         ComPtr<ID3D11DepthStencilView> depth_stencil_view;
         ComPtr<ID3D11RenderTargetView> render_target_views[2];
         {
            ID3D11RenderTargetView* render_target_views_raw[2];
            native_device_context->OMGetRenderTargets(2, &render_target_views_raw[0], depth_stencil_view.put());
            render_target_views[0].attach(render_target_views_raw[0]);
            render_target_views[1].attach(render_target_views_raw[1]);
         }

         if (!render_target_views[0] ||
             !render_target_views[1] ||
             !depth_stencil_view)
         {
            return DrawOrDispatchOverrideType::None;
         }

         return CheckAndHandleRenderPassTransition(render_target_views[1].get(), depth_stencil_view.get());
      }
      else if (original_shader_hashes.Contains(shader_hashes_light))
      {
         game_device_data.frame_phase = FramePhase::LIGHTING;
      }
      else if (game_device_data.frame_phase == FramePhase::LIGHTING &&
               !original_shader_hashes.Contains(shader_hashes_light))
      {
         game_device_data.frame_phase = FramePhase::DEFERRED;

         ComPtr<ID3D11Buffer> cbShadow;
         native_device_context->PSGetConstantBuffers(6, 1, cbShadow.put());

         if (cbShadow)
         {
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::compute, LumaConstantBufferType::LumaSettings);

            ID3D11Buffer* cbs[] = {cbShadow.get()};
            ID3D11UnorderedAccessView* uavs[] = {game_device_data.scratch_constant_buffer_uav.get()};

            native_device_context->CSSetShader(device_data.native_compute_shaders[CompileTimeStringHash("Update Shadow Constants")].get(), nullptr, 0);
            native_device_context->CSSetConstantBuffers(0, 1, cbs);
            native_device_context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
            native_device_context->Dispatch(1, 1, 1);

            native_device_context->CopySubresourceRegion(cbShadow.get(), 0, 0, 0, 0, game_device_data.scratch_constant_buffer.get(), 0, nullptr);
         }
      }
      else if (original_shader_hashes.Contains(shader_hashes_bloom_select))
      {
         // only apply sr when we have the necessary input resources
         if (SrActive(device_data) &&
             game_device_data.depth_texture &&
             game_device_data.motion_vectors)
         {
            game_device_data.frame_phase = FramePhase::POSTPROCESSING_AND_UI;
            ComPtr<ID3D11ShaderResourceView> color_srv;
            native_device_context->PSGetShaderResources(0, 1, color_srv.put());

            ComPtr<ID3D11Resource> color_resource;
            color_srv->GetResource(color_resource.put());
            color_resource->QueryInterface(game_device_data.source_color.put());

            SetupSr(native_device_context, game_device_data, device_data);

            // split the command list since DLSS must be executed on an immediate context
            native_device_context->FinishCommandList(TRUE, game_device_data.partial_command_list.put());
            if (game_device_data.modifiable_index_vertex_buffer)
            {
               D3D11_MAPPED_SUBRESOURCE mapped_buffer;
               // When starting a new command list first map has to be D3D11_MAP_WRITE_DISCARD
               native_device_context->Map(game_device_data.modifiable_index_vertex_buffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer);
               native_device_context->Unmap(game_device_data.modifiable_index_vertex_buffer.get(), 0);
            }

            game_device_data.draw_device_context = native_device_context;
            device_data.has_drawn_main_post_processing = true;
         }
      }
      // fallthrough replace rtv on bloom select as well
      if (game_device_data.frame_phase == FramePhase::POSTPROCESSING_AND_UI &&
          SrActive(device_data) &&
          (game_device_data.render_resolution.x != game_device_data.upscale_resolution.x ||
             game_device_data.render_resolution.y != game_device_data.upscale_resolution.y))
      {
         ComPtr<ID3D11ShaderResourceView> srvs[4];
         {
            ID3D11ShaderResourceView* srvs_raw[4];
            native_device_context->PSGetShaderResources(0, 4, &srvs_raw[0]);
            for (uint32_t i = 0; i < 4; ++i)
            {
               srvs[i].attach(srvs_raw[i]);
            }
         }
         bool srv_replaced = false;
         for (uint32_t i = 0; i < 4; ++i)
         {
            if (srvs[i])
            {
               ID3D11ShaderResourceView* replacement_srv = GetPostProcessSrv(srvs[i].get(), game_device_data);
               if (replacement_srv != srvs[i].get())
               {
                  srvs[i] = replacement_srv;
                  srv_replaced = true;
               }
            }
         }
         if (srv_replaced)
         {
            native_device_context->PSSetShaderResources(0, 4, &srvs[0]);
         }

         ComPtr<ID3D11DepthStencilView> depth_stencil_view;
         ComPtr<ID3D11RenderTargetView> render_target_view;
         native_device_context->OMGetRenderTargets(1, render_target_view.put(), depth_stencil_view.put());
         if (!original_shader_hashes.Contains(shader_hashes_copy) && render_target_view)
         {
            ID3D11RenderTargetView* replacement_rtv;
            uint2 replacement_resolution;
            if (original_shader_hashes.Contains(shader_hashes_bloom_select) ||
                original_shader_hashes.Contains(shader_hashes_bloom_filter))
            {
               replacement_rtv = GetPostProcessRtvScaled(render_target_view.get(), game_device_data, replacement_resolution);
            }
            else
            {
               replacement_rtv = GetPostProcessRtvOutputRes(render_target_view.get(), game_device_data, replacement_resolution);
            }
            if (replacement_rtv != render_target_view.get())
            {
               native_device_context->OMSetRenderTargets(1, &replacement_rtv, nullptr);

               D3D11_RECT scissor_rect;
               scissor_rect.left = 0;
               scissor_rect.top = 0;
               scissor_rect.right = replacement_resolution.x;
               scissor_rect.bottom = replacement_resolution.y;
               native_device_context->RSSetScissorRects(1, &scissor_rect);
               D3D11_VIEWPORT viewport;
               viewport.Width = replacement_resolution.x;
               viewport.Height = replacement_resolution.y;
               viewport.MinDepth = 0.0f;
               viewport.MaxDepth = 1.0f;
               viewport.TopLeftX = 0.0f;
               viewport.TopLeftY = 0.0f;
               native_device_context->RSSetViewports(1, &viewport);
            }
         }
      }

      if (SrActive(device_data) &&
          (original_shader_hashes.Contains(shader_hashes_fxaa) ||
             original_shader_hashes.Contains(shader_hashes_smaa_blending)))
      {
         native_device_context->PSSetShader(device_data.native_pixel_shaders[CompileTimeStringHash("Copy RGB 1 A")].get(), nullptr, 0);
      }
      else if (SrActive(device_data) &&
               (original_shader_hashes.Contains(shader_hashes_smaa_edge_detection) ||
                  original_shader_hashes.Contains(shader_hashes_smaa_weight_calculation)))
      {
         return DrawOrDispatchOverrideType::Skip;
      }
      else if (original_shader_hashes.Contains(shader_hashes_blur))
      {
         // the game has different stages that it combines for the blur effect when running
         // this is a step that sometimes replaces the content of render target and
         // sometimes is alpha blended
         // in the school hallway it is blended on a version of the scene texture that hasn't
         // been color graded yet leading to the scene noticably shifting color
         // as far as I can tell the render target and source texture have basiscally the same
         // content whenever this is used so copying it over before fixes the hallway and should
         // be safe for everything else
         ComPtr<ID3D11ShaderResourceView> srv;
         native_device_context->PSGetShaderResources(0, 1, srv.put());
         ComPtr<ID3D11RenderTargetView> rtv;
         native_device_context->OMGetRenderTargets(1, rtv.put(), nullptr);

         ComPtr<ID3D11Resource> srv_resource;
         srv->GetResource(srv_resource.put());

         ComPtr<ID3D11Resource> rtv_resource;
         rtv->GetResource(rtv_resource.put());

         native_device_context->CopySubresourceRegion(rtv_resource.get(), 0, 0, 0, 0, srv_resource.get(), 0, nullptr);
      }

      return DrawOrDispatchOverrideType::None;
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceDataPersona5Royal;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      for (size_t i = 0; i < game_device_data.replacement_textures.size(); ++i)
      {
         game_device_data.replacement_textures[i].in_use = false;
      }
      game_device_data.current_replacements.clear();

      device_data.force_reset_sr = !game_device_data.has_drawn_upscaling;
      game_device_data.has_drawn_upscaling = false;

      // Update TAA jitters:
      int phases = SR::GetDefaultJitterPhases();
      if (device_data.sr_type != SR::Type::None)
      {
         auto* sr_instance_data = device_data.GetSRInstanceData();
         phases = sr_implementations[device_data.sr_type]->GetJitterPhases(sr_instance_data);
      }
      int temporal_frame = cb_luma_global_settings.FrameIndex % phases;
      projection_jitters.x = SR::HaltonSequence(temporal_frame, 2);
      projection_jitters.y = SR::HaltonSequence(temporal_frame, 3);

      if (!custom_texture_mip_lod_bias_offset)
      {
         std::shared_lock shared_lock_samplers(s_mutex_samplers);
         if (SrActive(device_data) &&
             game_device_data.render_resolution.y > 0.0f &&
             game_device_data.upscale_resolution.y > 0.0f)
         {
            device_data.texture_mip_lod_bias_offset = SR::GetMipLODBias(game_device_data.render_resolution.y, game_device_data.upscale_resolution.y); // This results in -1 at output res
         }
         else
         {
            device_data.texture_mip_lod_bias_offset = 0.f;
         }
      }

      game_device_data.frame_phase = FramePhase::SHADOW_MAP;
      game_device_data.render_target_changed = false;

      // release all resources from the game we got this frame
      game_device_data.remainder_command_list.reset();
      game_device_data.draw_device_context = nullptr;
      game_device_data.source_color.reset();
      game_device_data.depth_texture.reset();
      game_device_data.motion_vectors.reset();
      game_device_data.cb_transform = nullptr;

      std::swap(game_device_data.prev_local_to_view_lookup, game_device_data.local_to_view_lookup);
      game_device_data.local_to_view_lookup.clear();
      game_device_data.cbuffer_cache.clear();

      if (game_device_data.last_viewport_size.x != 0 &&
          game_device_data.last_viewport_size.y != 0)
      {
         game_device_data.target_resolution = game_device_data.last_viewport_size;
         game_device_data.last_viewport_size = uint2(0, 0);
      }

      device_data.has_drawn_sr = false;
      device_data.has_drawn_main_post_processing = false;
   }

   static void OnExecuteSecondaryCommandList(reshade::api::command_list* cmd_list, reshade::api::command_list* secondary_cmd_list)
   {
      ComPtr<ID3D11DeviceContext> native_device_context;
      ID3D11DeviceChild* device_child = (ID3D11DeviceChild*)(cmd_list->get_native());
      HRESULT hr = device_child->QueryInterface(native_device_context.put());

      auto& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);

      if (native_device_context)
      {
         ComPtr<ID3D11CommandList> native_command_list;
         ID3D11DeviceChild* device_child = (ID3D11DeviceChild*)(secondary_cmd_list->get_native());
         HRESULT hr = device_child->QueryInterface(native_command_list.put());
         if (native_command_list == game_device_data.remainder_command_list && game_device_data.partial_command_list)
         {
            native_device_context->ExecuteCommandList(game_device_data.partial_command_list.get(), FALSE);
            game_device_data.partial_command_list.reset();

            if (!game_device_data.source_color || !game_device_data.depth_texture || device_data.sr_type == SR::Type::None)
            {
               return;
            }

            CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
            SetLumaConstantBuffers(native_device_context.get(), cmd_list_data, device_data, reshade::api::shader_stage::compute, LumaConstantBufferType::LumaSettings);

            D3D11_TEXTURE2D_DESC target_desc;
            game_device_data.source_color->GetDesc(&target_desc);

            auto* sr_instance_data = device_data.GetSRInstanceData();
            {
               SR::SettingsData settings_data;
               settings_data.output_width = game_device_data.upscale_resolution.x;
               settings_data.output_height = game_device_data.upscale_resolution.y;
               settings_data.render_width = game_device_data.render_resolution.x;
               settings_data.render_height = game_device_data.render_resolution.y;
               settings_data.dynamic_resolution = false;
               settings_data.hdr = true;
               settings_data.inverted_depth = false;
               settings_data.mvs_jittered = false;
               settings_data.render_preset = dlss_render_preset;
               sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context.get(), settings_data);
            }

            {
               ComPtr<ID3D11Device> device;
               native_device_context->GetDevice(device.put());

               D3D11_TEXTURE2D_DESC motion_vectors_desc;
               game_device_data.motion_vectors->GetDesc(&motion_vectors_desc);

               ComPtr<ID3D11ShaderResourceView> motion_vectors_srv;
               {
                  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
                  srv_desc.Format = motion_vectors_desc.Format;
                  srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                  srv_desc.Texture2D.MostDetailedMip = 0;
                  srv_desc.Texture2D.MipLevels = 1;
                  device->CreateShaderResourceView(game_device_data.motion_vectors.get(),
                     &srv_desc,
                     motion_vectors_srv.put());
               }

               native_device_context->CSSetShader(device_data.native_compute_shaders[CompileTimeStringHash("Decode Motion Vector")].get(), 0, 0);
               native_device_context->CSSetShaderResources(0, 1, motion_vectors_srv.get_addressof());
               native_device_context->CSSetUnorderedAccessViews(0, 1, game_device_data.decoded_motion_vectors_uav.get_addressof(), nullptr);
               native_device_context->Dispatch((game_device_data.render_resolution.x + 7) / 8, (game_device_data.render_resolution.y + 7) / 8, 1);
            }

            {
               SR::SuperResolutionImpl::DrawData draw_data;
               draw_data.source_color = game_device_data.source_color.get();
               draw_data.output_color = game_device_data.resolve_texture.get();
               draw_data.motion_vectors = game_device_data.decoded_motion_vectors.get();
               draw_data.depth_buffer = game_device_data.depth_texture.get();
               draw_data.render_width = game_device_data.render_resolution.x;
               draw_data.render_height = game_device_data.render_resolution.y;
               draw_data.pre_exposure = 0.0f;
               draw_data.jitter_x = projection_jitters.x;
               draw_data.jitter_y = projection_jitters.y;
               draw_data.vert_fov = game_device_data.fov;
               draw_data.reset = device_data.force_reset_sr;

               bool dlss_succeeded = sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context.get(), draw_data);
               game_device_data.has_drawn_upscaling = dlss_succeeded;
               device_data.has_drawn_sr = dlss_succeeded;
            }
            {
               ComPtr<ID3D11Device> device;
               native_device_context->GetDevice(device.put());
               ComPtr<ID3D11ShaderResourceView> resolve_texture_srv;
               ComPtr<ID3D11ShaderResourceView> color_srv;

               {
                  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
                  srv_desc.Format = target_desc.Format;
                  srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                  srv_desc.Texture2D.MostDetailedMip = 0;
                  srv_desc.Texture2D.MipLevels = 1;
                  device->CreateShaderResourceView(game_device_data.resolve_texture.get(),
                     &srv_desc,
                     resolve_texture_srv.put());
               }
               {
                  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
                  srv_desc.Format = target_desc.Format;
                  srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                  srv_desc.Texture2D.MostDetailedMip = 0;
                  srv_desc.Texture2D.MipLevels = 1;
                  device->CreateShaderResourceView(game_device_data.source_color.get(),
                     &srv_desc,
                     color_srv.put());
               }

               // some sr methods don't retain the alpha channel - combine sr result with the alpha from the original color texture
               {
                  ID3D11ShaderResourceView* srvs[] = {resolve_texture_srv.get(), color_srv.get()};
                  ID3D11SamplerState* samplers[] = {device_data.sampler_state_linear.get()};
                  native_device_context->CSSetShader(device_data.native_compute_shaders[CompileTimeStringHash("Merge")].get(), 0, 0);
                  native_device_context->CSSetShaderResources(0, 2, srvs);
                  native_device_context->CSSetUnorderedAccessViews(0, 1, game_device_data.merged_texture_uav.get_addressof(), nullptr);
                  native_device_context->CSSetSamplers(0, 1, samplers);
                  native_device_context->Dispatch((game_device_data.upscale_resolution.x + 7) / 8, (game_device_data.upscale_resolution.y + 7) / 8, 1);
               }

               if (game_device_data.render_resolution == game_device_data.upscale_resolution)
               {
                  native_device_context->CopySubresourceRegion(game_device_data.source_color.get(), 0, 0, 0, 0, game_device_data.merged_texture.get(), 0, nullptr);
               }
            }
         }
      }

      ComPtr<ID3D11CommandList> native_command_list;
      hr = device_child->QueryInterface(native_command_list.put());
      if (native_command_list)
      {
         ID3D11DeviceChild* device_child = (ID3D11DeviceChild*)(secondary_cmd_list->get_native());
         hr = device_child->QueryInterface(native_device_context.put());
         if (native_device_context.get() == game_device_data.draw_device_context)
         {
            game_device_data.remainder_command_list = native_command_list.get();
         }
      }
   }

   static void OnBindRenderTargetsAndDepthStencil(reshade::api::command_list* cmd_list, uint32_t count, const reshade::api::resource_view* rtvs, reshade::api::resource_view dsv)
   {
      auto& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);

      game_device_data.render_target_changed = true;
   }

   static void OnMapBufferRegion(reshade::api::device* device, reshade::api::resource resource, uint64_t offset, uint64_t size, reshade::api::map_access access, void** data)
   {
      if (access != reshade::api::map_access::write_only)
      {
         return;
      }
      D3D11_BUFFER_DESC bd;
      ((ID3D11Buffer*)resource.handle)->GetDesc(&bd);
      if (bd.BindFlags == (D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_INDEX_BUFFER))
      {
         auto& device_data = *device->get_private_data<DeviceData>();
         auto& game_device_data = GetGameDeviceData(device_data);

         game_device_data.modifiable_index_vertex_buffer = (ID3D11Buffer*)resource.handle;
      }
   }

   static bool OnUpdateBufferRegionCommand(reshade::api::command_list* cmd_list, const void* data, reshade::api::resource dest, uint64_t dest_offset, uint64_t size)
   {
      auto& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);

      if (!SrActive(device_data))
      {
         return false;
      }

      // early we don't need any cbuffer values after gbuffers are finished
      if (game_device_data.frame_phase != FramePhase::SHADOW_MAP &&
          game_device_data.frame_phase != FramePhase::REFLECTION &&
          game_device_data.frame_phase != FramePhase::GBUFFER)
      {
         return false;
      }

      // store values so we can find first change to the transform cbuffer
      if (game_device_data.frame_phase == FramePhase::SHADOW_MAP ||
          game_device_data.frame_phase == FramePhase::REFLECTION)
      {
         if (game_device_data.render_target_changed)
         {
            ID3D11Buffer* buffer = (ID3D11Buffer*)dest.handle;
            D3D11_BUFFER_DESC bd;
            ((ID3D11Buffer*)dest.handle)->GetDesc(&bd);
            // constant buffers used on draw thread are exclusively 7168 bytes in size
            // the deferred context that handles the update of skinned meshes uses
            // constant buffers sized at different powers of two
            if (bd.ByteWidth != 7168)
            {
               return false;
            }

            memcpy(game_device_data.cbuffer_cache[buffer].data(), data, 7168);
         }

         return false;
      }

      // game_device_data.frame_phase == FramePhase::GBUFFER
      if ((ID3D11Buffer*)dest.handle == game_device_data.cb_transform)
      {
         ComPtr<ID3D11DeviceContext> native_device_context;
         ID3D11DeviceChild* device_child = (ID3D11DeviceChild*)(cmd_list->get_native());
         HRESULT hr = device_child->QueryInterface(native_device_context.put());
         return HandleTransformUpdate((ID3D11Buffer*)dest.handle, data, native_device_context.get(), game_device_data, device_data);
      }

      return false;
   }

   static void OnBindViewports(reshade::api::command_list* cmd_list, uint32_t first, uint32_t count, const reshade::api::viewport* viewports)
   {
      auto& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);

      if (!game_device_data.draw_device_context)
      {
         return;
      }

      ComPtr<ID3D11DeviceContext> native_device_context;
      ID3D11DeviceChild* device_child = (ID3D11DeviceChild*)(cmd_list->get_native());
      device_child->QueryInterface(native_device_context.put());
      if (native_device_context.get() == game_device_data.draw_device_context)
      {
         game_device_data.last_viewport_size = uint2((uint32_t)viewports->width, (uint32_t)viewports->height);
      }
   }

   static bool OnCreateResource(reshade::api::device* device, reshade::api::resource_desc& desc, reshade::api::subresource_data* initial_data, reshade::api::resource_usage initial_state)
   {
      // after starting the game or some scene transitions the selected shadow quality is not applied anymore
      // and the middle setting is used instead which is 2048 so we just override that
      uint32_t shadow_map_size_override = g_shadow_map_size_override;
      if (shadow_map_size_override > 0 &&
          desc.type == reshade::api::resource_type::texture_2d &&
          (desc.usage & reshade::api::resource_usage::depth_stencil) == reshade::api::resource_usage::depth_stencil &&
          desc.texture.format == reshade::api::format::r32_typeless &&
          desc.texture.width == 2048 &&
          desc.texture.height == 2048)
      {
         desc.texture.height = desc.texture.width = shadow_map_size_override;
         return true;
      }
      return false;
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;

      ImGui::NewLine();

      const char* previewString;
      char buffer[32];
      if (g_shadow_map_size_override == 512)
      {
         previewString = "Very low - 512";
      }
      else if (g_shadow_map_size_override == 1024)
      {
         previewString = "Low - 1024";
      }
      else if (g_shadow_map_size_override == 2048)
      {
         previewString = "Middle - 2048";
      }
      else if (g_shadow_map_size_override == 4096)
      {
         previewString = "High - 4096";
      }
      else if (g_shadow_map_size_override == 8192)
      {
         previewString = "Very high - 8192";
      }
      else if (g_shadow_map_size_override > 0)
      {
         sprintf_s(buffer, 32, "%d", g_shadow_map_size_override);
         previewString = buffer;
      }
      else
      {
         previewString = "None";
      }
      if (ImGui::BeginCombo("Shadow map size override", previewString))
      {
         auto AddComboItem = [&](const char* name, uint32_t size, bool enabled)
         {
            const bool selected = g_shadow_map_size_override == size;
            if (ImGui::Selectable(name, selected))
            {
               g_shadow_map_size_override = size;
               reshade::set_config_value(runtime, NAME, "ShadowMapSizeOverride", g_shadow_map_size_override);
            }
            if (selected)
            {
               ImGui::SetItemDefaultFocus();
            }
         };

         AddComboItem("None", 0, true);
         AddComboItem("Very low - 512", 512, true);
         AddComboItem("Low - 1024", 1024, true);
         AddComboItem("Middle - 2048", 2048, true);
         AddComboItem("High - 4096", 4096, true);
         AddComboItem("Very high - 8192", 8192, true);
         ImGui::EndCombo();
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
         ImGui::SetTooltip("Set ingame Shadow Quality to Middle (The shadow quality setting is broken and Middle is what ends up being used anyway, but just to make sure). Requires restart/resetting Shadow Quality in settings.");
      }
   }

   void PrintImGuiAbout() override
   {
      ImGui::Text("Persona 5 Royal Luma mod - about and credits section", "");
      ImGui::Text("xxHash Library\n"
                  "Copyright (c) 2012-2021 Yann Collet\n"
                  "All rights reserved.\n"
                  "\n"
                  "BSD 2-Clause License (https://www.opensource.org/licenses/bsd-license.php)\n"
                  "\n"
                  "Redistribution and use in source and binary forms, with or without modification,\n"
                  "are permitted provided that the following conditions are met:\n"
                  "\n"
                  "* Redistributions of source code must retain the above copyright notice, this\n"
                  "  list of conditions and the following disclaimer.\n"
                  "\n"
                  "* Redistributions in binary form must reproduce the above copyright notice, this\n"
                  "  list of conditions and the following disclaimer in the documentation and/or\n"
                  "  other materials provided with the distribution.\n"
                  "\n"
                  "THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS \"AS IS\" AND\n"
                  "ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED\n"
                  "WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE\n"
                  "DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR\n"
                  "ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES\n"
                  "(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;\n"
                  "LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON\n"
                  "ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT\n"
                  "(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS\n"
                  "SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.\n");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Persona 5 Royal Luma mod");
      Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::Finished;
      Globals::VERSION = 1;

      // need to patch the code that adds the resolution dependent mip bias to sampler states
      // otherwise mip chain based effects break when the render resolution is 3840x2160
      enable_samplers_upgrade = PatchSamplerStates();
      samplers_upgrade_mode = 3;

      shader_hashes_light.pixel_shaders.emplace(std::stoul("D434C03A", nullptr, 16));
      shader_hashes_light.pixel_shaders.emplace(std::stoul("5C4DD977", nullptr, 16));

      shader_hashes_bloom_select.pixel_shaders.emplace(std::stoul("D51D54EF", nullptr, 16));
      shader_hashes_bloom_select.pixel_shaders.emplace(std::stoul("CD84F54A", nullptr, 16));

      shader_hashes_bloom_filter.pixel_shaders.emplace(std::stoul("9E6F2CA4", nullptr, 16));
      shader_hashes_bloom_filter.pixel_shaders.emplace(std::stoul("994E9696", nullptr, 16));
      shader_hashes_bloom_filter.pixel_shaders.emplace(std::stoul("9F5D846E", nullptr, 16));
      shader_hashes_bloom_filter.pixel_shaders.emplace(std::stoul("182FC62F", nullptr, 16));
      shader_hashes_bloom_filter.pixel_shaders.emplace(std::stoul("329EB6C6", nullptr, 16));
      shader_hashes_bloom_filter.pixel_shaders.emplace(std::stoul("67F7FD3B", nullptr, 16));
      shader_hashes_bloom_filter.pixel_shaders.emplace(std::stoul("526CA67C", nullptr, 16));

      shader_hashes_copy.pixel_shaders.emplace(std::stoul("B6E26AC7", nullptr, 16));

      shader_hashes_blur.pixel_shaders.emplace(std::stoul("1601D274", nullptr, 16));

      shader_hashes_fxaa.pixel_shaders.emplace(std::stoul("9EE7A272", nullptr, 16));

      shader_hashes_smaa_edge_detection.pixel_shaders.emplace(std::stoul("BB722F0A", nullptr, 16));
      shader_hashes_smaa_weight_calculation.pixel_shaders.emplace(std::stoul("4016ED43", nullptr, 16));
      shader_hashes_smaa_blending.pixel_shaders.emplace(std::stoul("960502CC", nullptr, 16));

      // not exhaustive but the first shader used in frames during which only the UI is active
      shader_hashes_ui.pixel_shaders.emplace(std::stoul("5E008C96", nullptr, 16));

      // cbuffer slots are fairly spread out for compute shaders any slot from 2 upwards is free,
      // for pixel shaders 7 seem unused, for vertex shaders no slots are unused
      luma_settings_cbuffer_index = 7;
      swapchain_upgrade_type = SwapchainUpgradeType::None;
      force_disable_display_composition = true;

      game = new Persona5Royal();
   }
   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
      reshade::unregister_event<reshade::addon_event::execute_secondary_command_list>(Persona5Royal::OnExecuteSecondaryCommandList);
      reshade::unregister_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(Persona5Royal::OnBindRenderTargetsAndDepthStencil);
      reshade::unregister_event<reshade::addon_event::map_buffer_region>(Persona5Royal::OnMapBufferRegion);
      reshade::unregister_event<reshade::addon_event::update_buffer_region_command>(Persona5Royal::OnUpdateBufferRegionCommand);
      reshade::unregister_event<reshade::addon_event::create_resource>(Persona5Royal::OnCreateResource);
      reshade::unregister_event<reshade::addon_event::bind_viewports>(Persona5Royal::OnBindViewports);
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}