#define GAME_WATCH_DOGS_2 1

#define ENABLE_NGX 1
// Hooking a debugger is forbidden
#define DISABLE_AUTO_DEBUGGER 1
#define DEBUG_LOG 0
#define HDR_ENABLED 0

// This crashes the game now for some reason
//#define ENABLE_ORIGINAL_SHADERS_MEMORY_EDITS 1

#include "..\..\Core\core.hpp"
#include "..\..\Core\includes\shader_patching.h"
#include "includes\hooks.hpp"
#include "includes\safetyhook.hpp"
#include "includes\hooks.cpp"
#include "includes\stretchy_buffer.hpp"

struct PreviousSkinCache
{
   uint offset;
   uint stride;
};

struct SkinCacheEntry
{
   uint32_t offset;
   uint32_t stride;
};

namespace
{
   union word_t
   {
      float f;
      int32_t i;
      uint32_t u;
      std::byte b[4];
   };

   float2 projection_jitters = {0, 0};
   
   static float2 jit[2] = {
      {0, 0}, {0, 0}
   };
   
   void JitterUpdate(bool sr_active)
   {
      if (sr_active)
      {
         auto index = cb_luma_global_settings.FrameIndex % 8;
         jit[0].x = SR::HaltonSequence(index+1, 2);
         jit[0].y = SR::HaltonSequence(index+1, 3);
         jit[1].x = jit[0].x;
         jit[1].y = jit[0].y;
      }
      else
      {
         jit[0].x = 0.25f;
         jit[0].y = 0.25f;
         jit[1].x = -0.25f;
         jit[1].y = -0.25f;
      }
      std::memcpy((void*)JitterTableOffset, &jit, sizeof(jit));
   }
   
   DirectX::XMMATRIX ComputeCameraSpaceToPreviousProjectedSpaceMatrix()
   {
      using namespace DirectX;
	   
      XMMATRIX inv_view_matrix_current = XMLoadFloat4x4(
         reinterpret_cast<const XMFLOAT4X4*>(&m_viewportPrivateData->m_motionBlur.m_lastCurrentCamera.m_camera.m_cameraMatrix.m_viewMatrixInverse));
      XMMATRIX inv_view_matrix_prev = XMLoadFloat4x4(
         reinterpret_cast<const XMFLOAT4X4*>(&m_viewportPrivateData->m_motionBlur.m_lastPreviousCamera.m_camera.m_cameraMatrix.m_viewMatrixInverse));
      XMMATRIX proj_prev = XMLoadFloat4x4(
         reinterpret_cast<const XMFLOAT4X4*>(&m_viewportPrivateData->m_motionBlur.m_lastPreviousCamera.m_camera.m_cameraMatrix.m_projectionMatrix));
		   
      proj_prev = XMMatrixTranspose(proj_prev);
      
      float4 position_delta = {0.0,0.0,0.0,1.0};
      position_delta.x = inv_view_matrix_current.r[3].m128_f32[0] - inv_view_matrix_prev.r[3].m128_f32[0];
      position_delta.y = inv_view_matrix_current.r[3].m128_f32[1] - inv_view_matrix_prev.r[3].m128_f32[1];
      position_delta.z = inv_view_matrix_current.r[3].m128_f32[2] - inv_view_matrix_prev.r[3].m128_f32[2];
      
      inv_view_matrix_current = XMMatrixTranspose(inv_view_matrix_current);
      inv_view_matrix_prev = XMMatrixTranspose(inv_view_matrix_prev);
      
      XMMATRIX view_rotation_prev = inv_view_matrix_prev;
      view_rotation_prev.r[0].m128_f32[3] = 0.0;
      view_rotation_prev.r[1].m128_f32[3] = 0.0;
      view_rotation_prev.r[2].m128_f32[3] = 0.0;
      view_rotation_prev.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
      
      {
         XMVECTOR c0 = view_rotation_prev.r[0];
         XMVECTOR c1 = view_rotation_prev.r[1];
         XMVECTOR c2 = view_rotation_prev.r[2];

         view_rotation_prev.r[0] = XMVectorSet(XMVectorGetX(c0), XMVectorGetX(c1), XMVectorGetX(c2), 0.0f);
         view_rotation_prev.r[1] = XMVectorSet(XMVectorGetY(c0), XMVectorGetY(c1), XMVectorGetY(c2), 0.0f);
         view_rotation_prev.r[2] = XMVectorSet(XMVectorGetZ(c0), XMVectorGetZ(c1), XMVectorGetZ(c2), 0.0f);
         view_rotation_prev.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
      }
      
      XMMATRIX inv_view_delta_matrix_current = inv_view_matrix_current;
      inv_view_delta_matrix_current.r[0].m128_f32[3] = position_delta.x;
      inv_view_delta_matrix_current.r[1].m128_f32[3] = position_delta.y;
      inv_view_delta_matrix_current.r[2].m128_f32[3] = position_delta.z;
      
      XMMATRIX temp = XMMatrixMultiply(proj_prev, view_rotation_prev);
      return XMMatrixMultiply(temp, inv_view_delta_matrix_current);
   }
   
   DirectX::XMMATRIX ComputePreviousViewRotProjectionMatrix()
   {
      using namespace DirectX;
      
      XMMATRIX view_matrix_prev = XMLoadFloat4x4(
         reinterpret_cast<const XMFLOAT4X4*>(&m_viewportPrivateData->m_motionBlur.m_lastPreviousCamera.m_camera.m_cameraMatrix.m_viewMatrix));
      XMMATRIX proj_prev = XMLoadFloat4x4(
         reinterpret_cast<const XMFLOAT4X4*>(&m_viewportPrivateData->m_motionBlur.m_lastPreviousCamera.m_camera.m_cameraMatrix.m_projectionMatrix));

      proj_prev = XMMatrixTranspose(proj_prev);
      view_matrix_prev.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
      view_matrix_prev = XMMatrixTranspose(view_matrix_prev);

      return XMMatrixMultiply(proj_prev, view_matrix_prev);
   }
   
   DirectX::XMMATRIX ComputeViewRotProjectionPureMatrix()
   {
      using namespace DirectX;
      
      XMMATRIX view_matrix = XMLoadFloat4x4(
         reinterpret_cast<const XMFLOAT4X4*>(&m_viewportPrivateData->m_motionBlur.m_lastCurrentCamera.m_camera.m_cameraMatrix.m_viewMatrix));
      XMMATRIX proj = XMLoadFloat4x4(
         reinterpret_cast<const XMFLOAT4X4*>(&m_viewportPrivateData->m_motionBlur.m_lastCurrentCamera.m_camera.m_cameraMatrix.m_projectionMatrix));

      proj = XMMatrixTranspose(proj);
      proj.r[0].m128_f32[2] = 0.0f;
      proj.r[1].m128_f32[2] = 0.0f;
      view_matrix.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
      view_matrix = XMMatrixTranspose(view_matrix);

      return XMMatrixMultiply(proj, view_matrix);
   }
   
   ShaderHashesList shader_hashes_SMAA_Reprojection;
   ShaderHashesList shader_hashes_SMAA_EdgeDectction;
   ShaderHashesList shader_hashes_TemporalResolve;
   ShaderHashesList shader_hashes_TemporalAA;
   ShaderHashesList shader_hashes_WaterGridVectorMap;
   ShaderHashesList shader_hashes_Materials;
   ShaderHashesList shader_hashes_ClothPreTransformed;
   ShaderHashesList shader_hashes_PostFXMask;
   ShaderHashesList shader_hashes_TemporalFiltering;
#if HDR_ENABLED
   ShaderHashesList shader_hashes_ColorGradingLUT;
#endif
   
   std::shared_mutex materials_mutex;
   
   std::unordered_set<ID3D11DeviceContext*> motion_vector_contexts;
}

struct GameDeviceDataWatchDogs2 final : public GameDeviceData
{
   // resources used to identify the deferred context used for scene drawing
   std::atomic<ID3D11CommandList*> remainder_command_list;
   std::atomic<ID3D11DeviceContext*> draw_device_context = nullptr;

   // textures we got from the game
   ComPtr<ID3D11Texture2D> source_color;
   ComPtr<ID3D11ShaderResourceView> source_color_srv;
   ComPtr<ID3D11Buffer> viewport_cbv;
   ComPtr<ID3D11ShaderResourceView> sr_output_color_srv;
   
   ComPtr<ID3D11Texture2D> decoded_motion_vectors;
   ComPtr<ID3D11ShaderResourceView> decoded_motion_vectors_srv;
   ComPtr<ID3D11UnorderedAccessView> decoded_motion_vectors_uav;
   
   ComPtr<ID3D11Texture2D> unjittered_depth;
   ComPtr<ID3D11ShaderResourceView> unjittered_depth_srv;
   ComPtr<ID3D11UnorderedAccessView> unjittered_depth_uav;
   
   ComPtr<ID3D11Texture2D> unjittered_postfx_mask;
   ComPtr<ID3D11ShaderResourceView> unjittered_postfx_mask_srv;
   ComPtr<ID3D11UnorderedAccessView> unjittered_postfx_mask_uav;
   
   ComPtr<ID3D11RenderTargetView> postfx_mask_rtv;
   
   ComPtr<ID3D11SamplerState> depth_sampler;
   ComPtr<ID3D11DepthStencilState> depth_stencil_state;

   // the command list we split to interject dlss
   ComPtr<ID3D11CommandList> partial_command_list;

   ComPtr<ID3D11Buffer> modifiable_index_vertex_buffer;
   
   ComPtr<ID3D11Buffer> cbuffer_skin_cache;
   std::unique_ptr<StretchyBuffer> prev_skin_buffer;
   std::unique_ptr<StretchyBuffer> skin_buffer;
   std::unordered_map<ID3D11Buffer*, SkinCacheEntry> prev_skin_lookup;
   std::unordered_map<ID3D11Buffer*, SkinCacheEntry> skin_lookup;
   
   std::mutex game_device_data_mutex;

   DirectX::XMMATRIX CameraSpaceToPreviousProjectedSpace;
   DirectX::XMMATRIX PreviousViewRotProjectionMatrix;
   
   float2 render_resolution;
   
   void CleanMVResources()
   {
      decoded_motion_vectors.reset();
      decoded_motion_vectors_srv.reset();
      decoded_motion_vectors_uav.reset();
      
      unjittered_depth.reset();
      unjittered_depth_srv.reset();
      unjittered_depth_uav.reset();
      
      unjittered_postfx_mask.reset();
      unjittered_postfx_mask_srv.reset();
      unjittered_postfx_mask_uav.reset();
   }
};

class WatchDogs2 final : public Game
{
   static GameDeviceDataWatchDogs2& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceDataWatchDogs2*>(device_data.game);
   }

   static const GameDeviceDataWatchDogs2& GetGameDeviceData(const DeviceData& device_data)
   {
      return *static_cast<const GameDeviceDataWatchDogs2*>(device_data.game);
   }

public:
   void OnInit(bool async) override
   {
      HMODULE engine_module = nullptr;
      while (!engine_module)
      {
         engine_module = GetModuleHandleA("Disrupt_64.dll");
         Sleep(100);
      }
      auto base_addr = (uintptr_t)engine_module;
      auto dos_header = reinterpret_cast<PIMAGE_DOS_HEADER>(engine_module);
      auto nt_headers = reinterpret_cast<PIMAGE_NT_HEADERS>(reinterpret_cast<std::byte*>(engine_module) + dos_header->e_lfanew);
      std::size_t section_size = nt_headers->OptionalHeader.SizeOfImage;
      
      JitterTableOffset = base_addr + 0x3E3B5C8;

      auto WILDCARD = System::BytePattern(System::BytePattern::WildcardType::Wildcard);

      std::vector<System::BytePattern> pattern = {
         0x48, 0x89, 0x05,
         WILDCARD, WILDCARD, WILDCARD, WILDCARD,
         0x48, 0x8B, 0x87
      };

      auto results = System::ScanMemoryForPattern(
         reinterpret_cast<std::byte*>(engine_module),
         section_size,
         pattern
         );

      if (!results.empty())
      {
         AAOptionBase = ResolveRipRelative<uintptr_t>(results[0], 3, 7);
      }
      
      pattern = {
         0x48, 0x89, 0x5C, 0x24, WILDCARD,
         0x48, 0x89, 0x74, 0x24, WILDCARD,
         0x89, 0x54, 0x24, WILDCARD,
         0x57,
         0x48, 0x83, 0xEC, WILDCARD,
         0x48, 0x89, 0xCE,
         0x48, 0x81, 0xC1, WILDCARD, WILDCARD, WILDCARD, WILDCARD,
         0xE8,
      };

      results = System::ScanMemoryForPattern(
         reinterpret_cast<std::byte*>(engine_module),
         section_size,
         pattern
         );

      if (!results.empty())
      {
         GetExistingSharedTexture = reinterpret_cast<fnGetExistingSharedTexture>(results[0]);
         reshade::log::message(reshade::log::level::info, "Found GetExistingSharedTexture()");
      }

      pattern = {
         0x49, 0x89, 0xE3, 0x55, 0x56, 0x57, 0x41, 0x56, 0x48, 0x8D, 0x6C, 0x24
      };

      results = System::ScanMemoryForPattern(
         reinterpret_cast<std::byte*>(engine_module),
         section_size,
         pattern
         );
#if DEBUG_LOG
      for (auto addr : results)
      {
         std::stringstream s;
         s << "Candidate: 0x" << std::hex << (uintptr_t)addr;
         reshade::log::message(reshade::log::level::info, s.str().c_str());
      }
#endif
      if (!results.empty() && !g_deferred_fx_antialias_renderer_hook)
      {
         void* fn = reinterpret_cast<void*>(results[0]);

         g_deferred_fx_antialias_renderer_hook = safetyhook::create_inline(
            fn,
            Hooked_CDeferredFxAntialiasRendererPrepare
            );

         if (g_deferred_fx_antialias_renderer_hook)
         {
            reshade::log::message(reshade::log::level::info, "Hook installed successfully");
         }
         else
         {
            reshade::log::message(reshade::log::level::error, "Failed to create inline hook");
         }
      }
      
      pattern = {
         0x4C, 0x89, 0x4C, 0x24, WILDCARD,
         0x4C, 0x89, 0x44, 0x24, WILDCARD,
         0x55,
         0x53,
         0x56,
         0x57,
         0x41, 0x54,
         0x41, 0x55,
         0x41, 0x56,
         0x41, 0x57,
         0x48, 0x8D, 0x6C, 0x24, WILDCARD,
         0x48, 0x81, 0xEC, WILDCARD, WILDCARD, WILDCARD, WILDCARD,
         0x48, 0x8B, 0xB5,
      };
      
      
      results = System::ScanMemoryForPattern(
         reinterpret_cast<std::byte*>(engine_module),
         section_size,
         pattern
         );

      if (!results.empty() && !g_net_hacking_renderer_hook)
      {
         void* fn = reinterpret_cast<void*>(results[0]);

         g_net_hacking_renderer_hook = safetyhook::create_inline(
            fn,
            Hooked_CNetHackingRendererPrepare
            );

         if (g_net_hacking_renderer_hook)
         {
            reshade::log::message(reshade::log::level::info, "Hook installed successfully");
         }
         else
         {
            reshade::log::message(reshade::log::level::error, "Failed to create inline hook");
         }
      }
      
      native_shaders_definitions.emplace(CompileTimeStringHash("Unjitter Depth"), ShaderDefinition{"Luma_CopyDepth", reshade::api::pipeline_subobject_type::pixel_shader});
      native_shaders_definitions.emplace(CompileTimeStringHash("Decode Motion Vector"), ShaderDefinition{"Luma_DecodeMotionVector", reshade::api::pipeline_subobject_type::compute_shader});
      native_shaders_definitions.emplace(CompileTimeStringHash("Copy Luminance"), ShaderDefinition{"Luma_CopyLuminance", reshade::api::pipeline_subobject_type::compute_shader});

      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"ENABLE_DITHER", '0', true, false, "Allows disabling the game's 8 bit dithering effect (luma disables it by default as it's all HDR)"},
      };
      shader_defines_data.append_range(game_shader_defines_data);
      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('1'); // Game was all linear, rendering is R16G16B16A16_FLOAT and post processing + UI is R8G8B8A8_UNORM_SRGB or B8G8R8A8_UNORM_SRGB.
      GetShaderDefineData(GAMMA_CORRECTION_TYPE_HASH).SetDefaultValue('0'); // Game seemengly looks better (less crush, less unnatural shadow) in sRGB than 2.2
      GetShaderDefineData(UI_DRAW_TYPE_HASH).SetDefaultValue('2');
   }

   void OnLoad(std::filesystem::path& file_path, bool failed) override
   {
      if (!failed)
      {
         reshade::register_event<reshade::addon_event::execute_secondary_command_list>(WatchDogs2::OnExecuteSecondaryCommandList);
      }
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceDataWatchDogs2;
   }
   
   void OnInitDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      {
         D3D11_BUFFER_DESC bd;
         bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
         bd.ByteWidth = 16;
         bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
         bd.MiscFlags = 0;
         bd.StructureByteStride = 0;
         bd.Usage = D3D11_USAGE_DYNAMIC;
         native_device->CreateBuffer(&bd, nullptr, game_device_data.cbuffer_skin_cache.put());
      }

      com_ptr<ID3D11DeviceContext> context;
      native_device->GetImmediateContext(&context);
      // 32MB is untested
      game_device_data.skin_buffer = std::make_unique<StretchyBuffer>(native_device, context.get(), 32 * 1024 * 1024);
      game_device_data.prev_skin_buffer = std::make_unique<StretchyBuffer>(native_device, context.get(), 32 * 1024 * 1024);
      
      CreateSamplers(native_device, game_device_data);
   }
   
   std::unique_ptr<std::byte[]> ModifyShaderByteCode(const std::byte* code, size_t& size, reshade::api::pipeline_subobject_type type, uint64_t shader_hash, const std::byte* shader_object, size_t shader_object_size) override
   {
      if (type == reshade::api::pipeline_subobject_type::vertex_shader)
      {
         std::unique_ptr<std::byte[]> new_code = nullptr;
         
         using namespace System;
         static const std::vector<System::BytePattern> pattern = {
            0x36, 0x00, 0x00, 0x05, 0x82, 0x00, 0x10, 0x00, ANY, ANY, ANY, ANY, 0x01, 0x40, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3F,  //mov r1.w, l(1.000000)
            0x11, 0x00, 0x00, 0x08, ANY, ANY, ANY, ANY, ANY, ANY, ANY, ANY, 0x46, 0x0E, 0x10, 0x00, ANY, ANY, ANY, ANY, 0x46, 0x8E, 0x20, 0x00, ANY, ANY, ANY, ANY, 0x14, 0x00, 0x00, 0x00, //dp4 r0.x, r1.xyzw, cb0[20].xyzw
            0x11, 0x00, 0x00, 0x08, ANY, ANY, ANY, ANY, ANY, ANY, ANY, ANY, 0x46, 0x0E, 0x10, 0x00, ANY, ANY, ANY, ANY, 0x46, 0x8E, 0x20, 0x00, ANY, ANY, ANY, ANY, 0x15, 0x00, 0x00, 0x00, //dp4 r0.y, r1.xyzw, cb0[21].xyzw
            0x11, 0x00, 0x00, 0x08, ANY, ANY, ANY, ANY, ANY, ANY, ANY, ANY, 0x46, 0x0E, 0x10, 0x00, ANY, ANY, ANY, ANY, 0x46, 0x8E, 0x20, 0x00, ANY, ANY, ANY, ANY, 0x17, 0x00, 0x00, 0x00  //dp4 o4.z, r1.xyzw, cb0[23].xyzw
         };
         
         static const std::vector<System::BytePattern> dcl_cb_pattern = {
            0x59, 0x00, 0x00, 0x04, 0x46, 0x8E, 0x20, 0x00, ANY, ANY, ANY, ANY, ANY, ANY, ANY, ANY, //dcl_constantbuffer cb0[181], immediateIndexed
         };
         
         word_t instruction_operand_register; // we don't know register so use wildcard byte pattern
         
         std::vector<uint8_t> appended_patch = {
            0x00, 0x00, 0x00, 0x09, // length(9)
            0x72, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, //add r0.xyz
            0x46, 0x02, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, //r0.xyzx
            0x46, 0x82, 0x20, 0x80, 0x41, 0x00, 0x00, 0x00, //-cb__index__.xyzx
            0x0B, 0x00, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, //11[11]
         };
         
         std::vector<uint8_t> appended_patch_cb = {
            0x59, 0x00, 0x00, 0x04, 0x46, 0x8E, 0x20, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, //dcl_constantbuffer cb11[16], immediateIndexed
         };
         
         std::vector<std::byte*> matches;
         matches = ScanMemoryForPattern(reinterpret_cast<const std::byte*>(code), size, pattern , true);
         
         if (!matches.empty())
         {
            {
               const std::unique_lock lock(materials_mutex);
               shader_hashes_Materials.vertex_shaders.emplace(uint32_t(shader_hash));
            }
            
            reshade::log::message(reshade::log::level::info, "Found motion vector shader.");
            std::byte* match_address = matches[0];
            instruction_operand_register.u = *reinterpret_cast<uint32_t*>(match_address+8);
            for (int i = 0; i < 4; i++)
            {
               appended_patch[8 + i]  = static_cast<uint8_t>(instruction_operand_register.b[i]);
               appended_patch[16 + i] = static_cast<uint8_t>(instruction_operand_register.b[i]);
            }
            
            new_code = std::make_unique<std::byte[]>(size + appended_patch.size() + appended_patch_cb.size());
            
         
            std::vector<std::byte*> matches_cb;
            matches_cb = ScanMemoryForPattern(reinterpret_cast<const std::byte*>(code), size, dcl_cb_pattern , true);
            
            if (!matches_cb.empty())
            {
               size_t insert_pos_cb = matches_cb[0] - code;
               // Copy everything before pattern
               std::memcpy(new_code.get(), code, insert_pos_cb);
               // Insert the patch
               std::memcpy(new_code.get() + insert_pos_cb, appended_patch_cb.data(), appended_patch_cb.size());
               
               size_t new_base = appended_patch_cb.size() + insert_pos_cb;
               
               size_t insert_pos = match_address - code;
               // Copy everything from pattern cb to pattern
               std::memcpy(new_code.get() + new_base, code + insert_pos_cb, insert_pos - insert_pos_cb);
               // Insert the patch
               std::memcpy(new_code.get() + appended_patch_cb.size() + insert_pos, appended_patch.data(), appended_patch.size());
               // Copy the rest (including the return instruction)
               std::memcpy(new_code.get() + appended_patch_cb.size() + insert_pos + appended_patch.size(), code + insert_pos, size - insert_pos);
               
               static const uint8_t cb_07_bytes[8] = {
                  0x0B, 0x00, 0x00, 0x00,   // cb11
                  0x07, 0x00, 0x00, 0x00    // [7]
               };
               static const uint8_t cb_08_bytes[8] = {
                  0x0B, 0x00, 0x00, 0x00,   // cb11
                  0x08, 0x00, 0x00, 0x00    // [8]
               };
               static const uint8_t cb_10_bytes[8] = {
                  0x0B, 0x00, 0x00, 0x00,   // cb11
                  0x0A, 0x00, 0x00, 0x00    // [10]
               };
               
               std::memcpy(new_code.get() + appended_patch_cb.size() + insert_pos + appended_patch.size() + 44, cb_07_bytes, 8);
               std::memcpy(new_code.get() + appended_patch_cb.size() + insert_pos + appended_patch.size() + 76, cb_08_bytes, 8);
               std::memcpy(new_code.get() + appended_patch_cb.size() + insert_pos + appended_patch.size() + 108, cb_10_bytes, 8);
               
               size += appended_patch.size() + appended_patch_cb.size();
            }
         }
         
         return new_code;
      }
      
      {
         if (type != reshade::api::pipeline_subobject_type::compute_shader)
            return nullptr;

         std::unique_ptr<std::byte[]> new_code = nullptr;

         // This compute shader was unsafe, it was reading and writing to the same coordinates of the same resources, from different threads at the same time, hence it needs some barriers to be added
         // Credits to Nukem, Blisto, doitsujin and pendingchaos for helping figure it out.
         if (shader_hash != 0x28BA3808)
         {
            return new_code;
         }

         std::vector<uint8_t> appended_patch;
         std::vector<const std::byte*> appended_patches_addresses;

         // Matches "AllMemoryBarrierWithGroupSync()" ("sync_uglobal_g_t" in asm)
         constexpr uint32_t flags =
            D3D11_SB_SYNC_THREADS_IN_GROUP |
            D3D11_SB_SYNC_THREAD_GROUP_SHARED_MEMORY |
            D3D11_SB_SYNC_UNORDERED_ACCESS_VIEW_MEMORY_GROUP |
            D3D11_SB_SYNC_UNORDERED_ACCESS_VIEW_MEMORY_GLOBAL;
         uint32_t opcode_token =
            ENCODE_D3D10_SB_OPCODE_TYPE(D3D11_SB_OPCODE_SYNC) |
            ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(1) |
            ENCODE_D3D11_SB_SYNC_FLAGS(flags);
#if 1 // TODOFT: test... we got "sync_sat_uglobal_g_t" otherwise?
         // make 100% sure SAT is off (paranoia, but harmless)
         opcode_token &= ~D3D10_SB_INSTRUCTION_SATURATE_MASK;
#endif
         std::vector<uint32_t> opcode_token_patch = std::vector<uint32_t>{opcode_token};

         appended_patch.insert(appended_patch.end(), reinterpret_cast<uint8_t*>(opcode_token_patch.data()), reinterpret_cast<uint8_t*>(opcode_token_patch.data()) + opcode_token_patch.size() * sizeof(uint32_t));

         size_t size_u32 = size / sizeof(uint32_t);
         const uint32_t* code_u32 = reinterpret_cast<const uint32_t*>(code);
         size_t i = 0;
         while (i < size_u32)
         {
            uint32_t opcode_token = code_u32[i];
            D3D10_SB_OPCODE_TYPE opcode_type = DECODE_D3D10_SB_OPCODE_TYPE(opcode_token);
            size_t instruction_size = opcode_type == D3D10_SB_OPCODE_CUSTOMDATA ? code_u32[i + 1] : DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(opcode_token); // Includes itself

            if (opcode_type == D3D10_SB_OPCODE_IF)
            {
               // Add the patch before every single branch value.
               // Shift it by how much the data would have been shifted by prior patches we already added.
               size_t i_add = appended_patches_addresses.size() * appended_patch.size() / sizeof(uint32_t); // Patches should always be a multiple of DWORD
               appended_patches_addresses.emplace_back(reinterpret_cast<const std::byte*>(&code_u32[i + i_add]));
            }

            i += instruction_size;
            if (instruction_size == 0)
               break;
         }

         // Insert the patch for each address
         if (!appended_patches_addresses.empty())
         {
            new_code = std::make_unique<std::byte[]>(size + appended_patch.size() * appended_patches_addresses.size());

            std::memcpy(new_code.get(), code, size);

            size_t valid_size = size;

            std::unique_ptr<std::byte[]> scratch_buffer = std::make_unique<std::byte[]>(size + appended_patch.size() * appended_patches_addresses.size());

            for (const auto appended_patches_address : appended_patches_addresses)
            {
               size_t insert_pos = appended_patches_address - code; // These are already shifted to account for the previously inserted patches

               // Copy from the address we'll insert the patch at, until the end, into a temporary buffer
               std::memcpy(scratch_buffer.get(), new_code.get() + insert_pos, valid_size - insert_pos);
               // Insert the patch
               std::memcpy(new_code.get() + insert_pos, appended_patch.data(), appended_patch.size());
               // Fill back the previous data, shifted
               std::memcpy(new_code.get() + insert_pos + appended_patch.size(), scratch_buffer.get(), valid_size - insert_pos);

               valid_size += appended_patch.size();
            }

            size = valid_size;
         }
         
         return new_code;
      }
   }
   
   static bool CreateSamplers(ID3D11Device* native_device, GameDeviceDataWatchDogs2& game_device_data)
   {
      // Return early if resources already exist with correct dimensions
      if (game_device_data.depth_sampler.get())
      {
         return true;
      }
      
      HRESULT hr;
      D3D11_SAMPLER_DESC sampler_desc = {};

      // linear for fake depth
      sampler_desc.Filter = D3D11_FILTER::D3D11_FILTER_MIN_MAG_MIP_LINEAR;
      sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_MODE::D3D11_TEXTURE_ADDRESS_CLAMP;
      sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_MODE::D3D11_TEXTURE_ADDRESS_CLAMP;
      sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_MODE::D3D11_TEXTURE_ADDRESS_CLAMP;
      sampler_desc.MipLODBias = 0.0f;
      sampler_desc.MaxAnisotropy = 0;
      sampler_desc.ComparisonFunc = D3D11_COMPARISON_FUNC::D3D11_COMPARISON_NEVER;
      sampler_desc.BorderColor[0] = 0.0f;
      sampler_desc.BorderColor[1] = 0.0f;
      sampler_desc.BorderColor[2] = 0.0f;
      sampler_desc.BorderColor[3] = 0.0f;
      sampler_desc.MinLOD = 0.0f;
      sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
      
      hr = native_device->CreateSamplerState(&sampler_desc, game_device_data.depth_sampler.put());
      if (FAILED(hr))
      {
         std::stringstream s;
         s << "Depth sampler: Creation Failed";
         reshade::log::message(reshade::log::level::info, s.str().c_str());
         return false;
      }
      
      if (!game_device_data.depth_stencil_state.get())
      {
         CD3D11_DEPTH_STENCIL_DESC ds_desc(D3D11_DEFAULT);
         ds_desc.DepthEnable = TRUE;
         ds_desc.DepthFunc = D3D11_COMPARISON_ALWAYS;

         HRESULT hr = native_device->CreateDepthStencilState(&ds_desc, game_device_data.depth_stencil_state.put());
         if (FAILED(hr))
         {
            std::stringstream s;
            s << "DS: DS State Creation Failed";
            reshade::log::message(reshade::log::level::info, s.str().c_str());
            return false;
         }
      }
      return true;
   }
   
   static bool CreateMVResources(ID3D11Device* native_device, GameDeviceDataWatchDogs2& game_device_data)
   {
      if (game_device_data.decoded_motion_vectors.get())
      {
         D3D11_TEXTURE2D_DESC desc;
         game_device_data.decoded_motion_vectors.get()->GetDesc(&desc);
         // Return early if resources already exist with correct dimensions
         bool output_changed = desc.Width != m_viewportPrivateData->m_viewportSize[0]
         || desc.Height != m_viewportPrivateData->m_viewportSize[1];
         if (!output_changed)
         {
            return true;
         }
         game_device_data.CleanMVResources();
      }
      
      HRESULT hr;
      
      D3D11_TEXTURE2D_DESC tex_desc;
      tex_desc.Width = m_viewportPrivateData->m_viewportSize[0];
      tex_desc.Height = m_viewportPrivateData->m_viewportSize[1];
      tex_desc.Usage = D3D11_USAGE_DEFAULT;
      tex_desc.ArraySize = 1;
      tex_desc.Format = DXGI_FORMAT_R16G16_FLOAT;
      tex_desc.SampleDesc.Count = 1;
      tex_desc.SampleDesc.Quality = 0;
      tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
      tex_desc.CPUAccessFlags = 0;
      tex_desc.MiscFlags = 0;
      tex_desc.MipLevels = 1;
      
      hr = native_device->CreateTexture2D(&tex_desc, nullptr, game_device_data.decoded_motion_vectors.put());
      if (FAILED(hr))
      {
         std::stringstream s;
         s << "MV: Texture Creation Failed";
         reshade::log::message(reshade::log::level::info, s.str().c_str());
         return false;
      }
      
      tex_desc.Format = DXGI_FORMAT_R32_TYPELESS;
      hr = native_device->CreateTexture2D(&tex_desc, nullptr, game_device_data.unjittered_depth.put());
      if (FAILED(hr))
      {
         std::stringstream s;
         s << "Depth: Texture Creation Failed";
         reshade::log::message(reshade::log::level::info, s.str().c_str());
         return false;
      }
      
      tex_desc.Format = DXGI_FORMAT_B8G8R8A8_TYPELESS;
      hr = native_device->CreateTexture2D(&tex_desc, nullptr, game_device_data.unjittered_postfx_mask.put());
      if (FAILED(hr))
      {
         std::stringstream s;
         s << "Post FX Mask: Texture Creation Failed";
         reshade::log::message(reshade::log::level::info, s.str().c_str());
         return false;
      }
      
      D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
      uav_desc.Format = DXGI_FORMAT_R16G16_FLOAT;
      uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
      uav_desc.Texture2D.MipSlice = 0;
      hr = native_device->CreateUnorderedAccessView(game_device_data.decoded_motion_vectors.get(), &uav_desc, game_device_data.decoded_motion_vectors_uav.put());
      if (FAILED(hr))
      {
         std::stringstream s;
         s << "MV: UAV Creation Failed";
         reshade::log::message(reshade::log::level::info, s.str().c_str());
         return false;
      }
      
      uav_desc.Format = DXGI_FORMAT_R32_FLOAT;
      hr = native_device->CreateUnorderedAccessView(game_device_data.unjittered_depth.get(), &uav_desc, game_device_data.unjittered_depth_uav.put());
      if (FAILED(hr))
      {
         std::stringstream s;
         s << "Depth: UAV Creation Failed";
         reshade::log::message(reshade::log::level::info, s.str().c_str());
         return false;
      }
      
      uav_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
      hr = native_device->CreateUnorderedAccessView(game_device_data.unjittered_postfx_mask.get(), &uav_desc, game_device_data.unjittered_postfx_mask_uav.put());
      if (FAILED(hr))
      {
         std::stringstream s;
         s << "Post FX Mask: UAV Creation Failed";
         reshade::log::message(reshade::log::level::info, s.str().c_str());
         return false;
      }
      
      D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
      srv_desc.Format = DXGI_FORMAT_R16G16_FLOAT;
      srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
      srv_desc.Texture2D.MostDetailedMip = 0;
      srv_desc.Texture2D.MipLevels = 1;
      hr = native_device->CreateShaderResourceView(game_device_data.decoded_motion_vectors.get(), &srv_desc, game_device_data.decoded_motion_vectors_srv.put());
      if (FAILED(hr))
      {
         std::stringstream s;
         s << "MV: SRV Creation Failed";
         reshade::log::message(reshade::log::level::info, s.str().c_str());
         return false;
      }
      
      srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
      hr = native_device->CreateShaderResourceView(game_device_data.unjittered_depth.get(), &srv_desc, game_device_data.unjittered_depth_srv.put());
      if (FAILED(hr))
      {
         std::stringstream s;
         s << "Depth: SRV Creation Failed";
         reshade::log::message(reshade::log::level::info, s.str().c_str());
         return false;
      }
      
      srv_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
      hr = native_device->CreateShaderResourceView(game_device_data.unjittered_postfx_mask.get(), &srv_desc, game_device_data.unjittered_postfx_mask_srv.put());
      if (FAILED(hr))
      {
         std::stringstream s;
         s << "Post FX Mask: SRV Creation Failed";
         reshade::log::message(reshade::log::level::info, s.str().c_str());
         return false;
      }

      return true;
   }
   
   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
#if HDR_ENABLED
      // Make sure the swapchain copy shader always and only targets the swapchain RT, otherwise we'd need to branch in it!
      if (is_custom_pass && (stages & reshade::api::shader_stage::compute) != 0 && original_shader_hashes.Contains(shader_hashes_ColorGradingLUT))
      {
         // We need access to a linear sampler in the customized version of this CS, so add it (and make sure it's not overlapping with any other used slot, so we don't pollute the state)
         ID3D11SamplerState* const sampler_state_linear = device_data.sampler_state_linear.get();
         native_device_context->CSSetSamplers(D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - 1, 1, &sampler_state_linear);

         return DrawOrDispatchOverrideType::None;
      }
#endif
      if ((stages & reshade::api::shader_stage::compute) != 0 && original_shader_hashes.Contains(shader_hashes_TemporalFiltering))
      {
         static bool has_sent_tf_warning = false;
         if (!has_sent_tf_warning && MessageBoxA(NULL, "Temporal Filtering is broken in Watch Dogs 2 depending on your GPU, Luma suggests against using it.", "Temporal Filtering detected", MB_OK | MB_SETFOREGROUND) == IDOK)
         {
            has_sent_tf_warning = true;
         }
         return DrawOrDispatchOverrideType::None;
      }
      
      auto& game_device_data = GetGameDeviceData(device_data);

      const std::shared_lock lock(materials_mutex);
      if (original_shader_hashes.Contains(shader_hashes_Materials))
      {
         if (!motion_vector_contexts.contains(native_device_context))
         {
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::vertex, LumaConstantBufferType::LumaData);
            motion_vector_contexts.emplace(native_device_context);
         }
         return DrawOrDispatchOverrideType::None;
      }
      
      if (original_shader_hashes.Contains(shader_hashes_ClothPreTransformed))
      {
         if (!motion_vector_contexts.contains(native_device_context))
         {
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::vertex, LumaConstantBufferType::LumaData);
            motion_vector_contexts.emplace(native_device_context);
         }
            
         ID3D11ShaderResourceView* srv = game_device_data.prev_skin_buffer->srv.get();
         native_device_context->VSSetConstantBuffers(9, 1, &game_device_data.cbuffer_skin_cache);
         native_device_context->VSSetShaderResources(1, 1, &srv);
            
         ComPtr<ID3D11Buffer> vertex_buffer;
         uint32_t stride;
         uint32_t offset;
         // positions are stored buffer 1 instead of 0
         native_device_context->IAGetVertexBuffers(1, 1, vertex_buffer.put(), &stride, &offset);

         D3D11_BUFFER_DESC bd;
         vertex_buffer->GetDesc(&bd);
            
         if (game_device_data.skin_lookup.find(vertex_buffer.get()) == game_device_data.skin_lookup.cend())
         {
            SkinCacheEntry cache_entry = {};
            cache_entry.offset = game_device_data.skin_buffer->size + offset;
            cache_entry.stride = stride;

            game_device_data.skin_buffer->CopyFromBuffer(native_device_context, vertex_buffer.get(), bd.ByteWidth);

            game_device_data.skin_lookup[vertex_buffer.get()] = cache_entry;
         }
            
         bool previous_skin_set = false;
            
         auto cache_it = game_device_data.prev_skin_lookup.find(vertex_buffer.get());
            
         if (cache_it != game_device_data.prev_skin_lookup.cend())
         {
            D3D11_MAPPED_SUBRESOURCE mapped_cbuffer;
            native_device_context->Map(game_device_data.cbuffer_skin_cache.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_cbuffer);
            PreviousSkinCache* vs_consts_skin = (PreviousSkinCache*)mapped_cbuffer.pData;
            vs_consts_skin->offset = cache_it->second.offset;
            vs_consts_skin->stride = cache_it->second.stride;
            native_device_context->Unmap(game_device_data.cbuffer_skin_cache.get(), 0);

            previous_skin_set = true;
         }
         //has_set_luma_cb_material = false;
         return DrawOrDispatchOverrideType::None;
      }
      
      if (m_viewportPrivateData && CDeferredFxAntialiasRenderer && !bIsNetHackingRendering && GetAAOption() == OPTION_SMAA_T2X)
      {
         if (original_shader_hashes.Contains(shader_hashes_WaterGridVectorMap))
         {
            if (device_data.sr_type != SR::Type::None)
            {
               native_device_context->Draw(4, 0);
               // split the command list since DLSS must be executed on an immediate context
               native_device_context->FinishCommandList(TRUE, game_device_data.partial_command_list.put());
               game_device_data.draw_device_context = native_device_context;
               return DrawOrDispatchOverrideType::Replaced;
            }
         }
         
         // This doesnt work with unjittered matrices because depth will reject
         if (original_shader_hashes.Contains(shader_hashes_PostFXMask))
         {
            if (!game_device_data.postfx_mask_rtv.get())
            {
               native_device_context->OMGetRenderTargets(1, game_device_data.postfx_mask_rtv.put(), nullptr);
            }
            return DrawOrDispatchOverrideType::None;
         }

         if (original_shader_hashes.Contains(shader_hashes_TemporalAA))
         {
            if (device_data.sr_type != SR::Type::None)
            {
               ComPtr<ID3D11ShaderResourceView> srv;
               ComPtr<ID3D11Buffer> cbv;
               native_device_context->PSGetShaderResources(1, 1, srv.put());
               native_device_context->PSGetConstantBuffers(0, 1, cbv.put());
                  
               {
                  game_device_data.source_color_srv = srv;
                  game_device_data.viewport_cbv = cbv;
               }
               return DrawOrDispatchOverrideType::Skip;
            }
         }
         
         if (original_shader_hashes.Contains(shader_hashes_TemporalResolve))
         {
            if (device_data.sr_type != SR::Type::None)
            {
               return DrawOrDispatchOverrideType::Skip;
            }
         }
      }

      return DrawOrDispatchOverrideType::None;
   }
   
   static void OnExecuteSecondaryCommandList(reshade::api::command_list* cmd_list, reshade::api::command_list* secondary_cmd_list)
   {
      SKIP_UNSUPPORTED_DEVICE_API(device->get_api());
      
      ID3D11DeviceChild* primary_child = reinterpret_cast<ID3D11DeviceChild*>(cmd_list->get_native());
      ID3D11DeviceChild* secondary_child = reinterpret_cast<ID3D11DeviceChild*>(secondary_cmd_list->get_native());
      if (!primary_child || !secondary_child)
         return;

      auto& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);

      ComPtr<ID3D11DeviceContext> deferred_ctx;
      ComPtr<ID3D11CommandList> finished_cmd_list;
      if (SUCCEEDED(secondary_child->QueryInterface(deferred_ctx.put())) && deferred_ctx)
      {
         // This is a FinishCommandList call - capture the remainder command list
         primary_child->QueryInterface(finished_cmd_list.put());
         if (finished_cmd_list && deferred_ctx.get() == game_device_data.draw_device_context)
         {
#if DEBUG_LOG
            reshade::log::message(reshade::log::level::info, "Partial SMAA command list finished (deferred ctx)");
#endif
            game_device_data.remainder_command_list.store(finished_cmd_list.get(), std::memory_order_release);
            game_device_data.draw_device_context = nullptr;
         }
         return;
      }

      ComPtr<ID3D11DeviceContext> immediate_ctx;
      ComPtr<ID3D11CommandList> exec_cmd_list;
      if (SUCCEEDED(primary_child->QueryInterface(immediate_ctx.put())) && immediate_ctx)
      {
         secondary_child->QueryInterface(exec_cmd_list.put());
         if (exec_cmd_list.get() == game_device_data.remainder_command_list && game_device_data.partial_command_list.get() != nullptr)
         {
            immediate_ctx->ExecuteCommandList(game_device_data.partial_command_list.get(), FALSE);
#if DEBUG_LOG
            reshade::log::message(reshade::log::level::info, "Partial SMAA command list executed (immediate ctx)");
#endif

            game_device_data.partial_command_list.reset();
            game_device_data.remainder_command_list.store(nullptr, std::memory_order_relaxed);
            
            const bool dlss_inputs_valid = 
               game_device_data.source_color_srv.get() != nullptr
            && m_deferredFXRendererContextTextures.m_motionVectors != nullptr
            && m_deferredFXRendererContextTextures.m_motionVectors->m_texture != nullptr
            && m_deferredFXRendererContextTextures.m_motionVectors->m_texture->m_shaderResourceView != nullptr
            && m_deferredFXRendererContextTextures.m_motionVectors->m_texture->m_renderTargetView != nullptr
            && m_deferredFXRendererContextTextures.m_linearDepthTexture != nullptr
            && m_deferredFXRendererContextTextures.m_linearDepthTexture->m_texture != nullptr
            && m_deferredFXRendererContextTextures.m_linearDepthTexture->m_texture->m_shaderResourceView != nullptr
            && m_deferredFXRendererContextTextures.m_linearDepthTexture->m_texture->m_depthStencilViews != nullptr;

            if (!dlss_inputs_valid)
            {
#if DEBUG_LOG
               reshade::log::message(reshade::log::level::info, "No source color resource");
#endif
               return;
            }
            
            DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack;
            DrawStateStack<DrawStateStackType::Compute> compute_state_stack;
            draw_state_stack.Cache(immediate_ctx.get(), device_data.uav_max_count);
            compute_state_stack.Cache(immediate_ctx.get(), device_data.uav_max_count);
            
            {
               ComPtr<ID3D11Resource> color_resource;
               game_device_data.source_color_srv->GetResource(color_resource.put());
               if (color_resource)
               {
                  color_resource->QueryInterface(game_device_data.source_color.put());
               }
            }

            if (device_data.sr_type != SR::Type::None && !device_data.has_drawn_sr)
            {
               game_device_data.CameraSpaceToPreviousProjectedSpace = ComputeCameraSpaceToPreviousProjectedSpaceMatrix();
               game_device_data.PreviousViewRotProjectionMatrix = ComputePreviousViewRotProjectionMatrix();
               
               CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
               SetLumaConstantBuffers(immediate_ctx.get(), cmd_list_data, device_data, reshade::api::shader_stage::compute, LumaConstantBufferType::LumaSettings);
               
               auto* sr_instance_data = device_data.GetSRInstanceData();
               {
#if DEBUG_LOG
                  reshade::log::message(reshade::log::level::info, "Setting SR");
#endif
                  SR::SettingsData settings_data;
                  settings_data.output_width = m_viewportPrivateData->m_viewportSize[0];
                  settings_data.output_height = m_viewportPrivateData->m_viewportSize[1];
                  settings_data.render_width = m_viewportPrivateData->m_viewportSize[0];
                  settings_data.render_height = m_viewportPrivateData->m_viewportSize[1];
                  settings_data.dynamic_resolution = false;
                  settings_data.hdr = true;
                  settings_data.inverted_depth = true;
                  settings_data.mvs_jittered = false;
                  settings_data.auto_exposure = device_data.sr_type != SR::Type::FSR;
                  settings_data.mvs_x_scale = -(float)m_viewportPrivateData->m_viewportSize[0];
                  settings_data.mvs_y_scale = -(float)m_viewportPrivateData->m_viewportSize[1];
                  settings_data.render_preset = dlss_render_preset;
                  sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, immediate_ctx.get(), settings_data);
               }

               {
                  //device_data.force_reset_sr = false;
                  bool is_camera_cut;
                  float camera_dot = m_viewportPrivateData->m_motionBlur.m_lastPreviousCamera.m_camera.m_position[0] * m_viewportPrivateData->m_motionBlur.m_lastCurrentCamera.m_camera.m_position[0] +
                     m_viewportPrivateData->m_motionBlur.m_lastPreviousCamera.m_camera.m_position[1] * m_viewportPrivateData->m_motionBlur.m_lastCurrentCamera.m_camera.m_position[1] +
                     m_viewportPrivateData->m_motionBlur.m_lastPreviousCamera.m_camera.m_position[2] * m_viewportPrivateData->m_motionBlur.m_lastCurrentCamera.m_camera.m_position[2];
                  is_camera_cut = camera_dot < 0.8;
                  
                  device_data.force_reset_sr = device_data.force_reset_sr || is_camera_cut;
                  
                  projection_jitters.x = m_viewportPrivateData->m_motionBlur.m_lastCurrentCamera.m_jitter[0] * (float)m_viewportPrivateData->m_viewportSize[0];
                  projection_jitters.y = m_viewportPrivateData->m_motionBlur.m_lastCurrentCamera.m_jitter[1] * (float)m_viewportPrivateData->m_viewportSize[1];
               }

               D3D11_TEXTURE2D_DESC taa_output_texture_desc;
               game_device_data.source_color->GetDesc(&taa_output_texture_desc);

               bool skip_dlss = taa_output_texture_desc.Width < sr_instance_data->min_resolution || taa_output_texture_desc.Height < sr_instance_data->min_resolution;
               bool dlss_output_changed = false;

               {
                  D3D11_TEXTURE2D_DESC dlss_output_texture_desc = taa_output_texture_desc;
                  dlss_output_texture_desc.Width = m_viewportPrivateData->m_viewportSize[0];
                  dlss_output_texture_desc.Height = m_viewportPrivateData->m_viewportSize[1];
                  dlss_output_texture_desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

                  ComPtr<ID3D11Device> native_device;
                  immediate_ctx->GetDevice(native_device.put());

                  if (device_data.sr_output_color.get())
                  {
                     D3D11_TEXTURE2D_DESC prev_dlss_output_texture_desc;
                     device_data.sr_output_color->GetDesc(&prev_dlss_output_texture_desc);
                     dlss_output_changed = prev_dlss_output_texture_desc.Width != dlss_output_texture_desc.Width || prev_dlss_output_texture_desc.Height != dlss_output_texture_desc.Height || prev_dlss_output_texture_desc.Format != dlss_output_texture_desc.Format;
                  }

                  if (!device_data.sr_output_color.get() || dlss_output_changed)
                  {
                     device_data.sr_output_color = nullptr; // Make sure we discard the previous one
                     HRESULT hr = native_device->CreateTexture2D(&dlss_output_texture_desc, nullptr, &device_data.sr_output_color);
                     ASSERT_ONCE(SUCCEEDED(hr));
                  }
                  
                  if (!device_data.sr_output_color.get())
                  {
                     skip_dlss = true;
                  }
                  
                  if (CreateMVResources(native_device.get(), game_device_data))
                  {
#if DEBUG_LOG
                     reshade::log::message(reshade::log::level::info, "Decoding MV");
#endif
                     SetLumaConstantBuffers(immediate_ctx.get(), cmd_list_data, device_data, reshade::api::shader_stage::compute, LumaConstantBufferType::LumaData);
                     ID3D11ShaderResourceView* srvs[] = {m_deferredFXRendererContextTextures.m_motionVectors->m_texture->m_shaderResourceView, m_deferredFXRendererContextTextures.m_linearDepthTexture->m_texture->m_shaderResourceView};
                     ID3D11UnorderedAccessView* uavs[] = {game_device_data.decoded_motion_vectors_uav.get(), game_device_data.unjittered_depth_uav.get()};
                     ID3D11Buffer* buffers[] = {game_device_data.viewport_cbv.get()};
                     ID3D11SamplerState* samplers[] = {game_device_data.depth_sampler.get()};
                     immediate_ctx->CSSetShader(device_data.native_compute_shaders[CompileTimeStringHash("Decode Motion Vector")].get(), nullptr, 0);
                     immediate_ctx->CSSetShaderResources(0, 2, srvs);
                     immediate_ctx->CSSetConstantBuffers(0, 1, buffers);
                     immediate_ctx->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
                     immediate_ctx->CSSetSamplers(0, 1, samplers);
               
                     immediate_ctx->Dispatch((m_viewportPrivateData->m_viewportSize[0] + 7) / 8, (m_viewportPrivateData->m_viewportSize[1] + 7) / 8, 1);
                     
                     uavs[0] = nullptr;
                     uavs[1] = nullptr;
                     srvs[0] = nullptr;
                     srvs[1] = nullptr;
                     buffers[0] = nullptr;
                     immediate_ctx->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
                     immediate_ctx->CSSetShaderResources(0, 2, srvs);
                     immediate_ctx->CSSetConstantBuffers(0, 1, buffers);
                  }
                  else
                  {
                     skip_dlss = true;
                  }
               }
               
               if (!skip_dlss)
               {
#if DEBUG_LOG
                  reshade::log::message(reshade::log::level::info, "Drawing SR");
#endif
                  SR::SuperResolutionImpl::DrawData draw_data;
                  draw_data.source_color = game_device_data.source_color.get();
                  draw_data.output_color = device_data.sr_output_color.get();
                  draw_data.motion_vectors = game_device_data.decoded_motion_vectors.get();
                  draw_data.depth_buffer = m_deferredFXRendererContextTextures.m_linearDepthTexture->m_texture->m_nativeTexture;//game_device_data.depth_texture.get();
                  draw_data.pre_exposure = 0.0f;
                  draw_data.render_width = m_viewportPrivateData->m_viewportSize[0];
                  draw_data.render_height = m_viewportPrivateData->m_viewportSize[1];
                  draw_data.jitter_x = projection_jitters.x;
                  draw_data.jitter_y = -projection_jitters.y;
                  draw_data.vert_fov = m_viewportPrivateData->m_motionBlur.m_lastCurrentCamera.m_camera.m_FOV;
                  draw_data.far_plane = m_viewportPrivateData->m_motionBlur.m_lastCurrentCamera.m_camera.m_farClipDistance;
                  draw_data.near_plane = m_viewportPrivateData->m_motionBlur.m_lastCurrentCamera.m_camera.m_nearClipDistance;
                  draw_data.reset = device_data.force_reset_sr;
                  draw_data.frame_index = m_viewportPrivateData->m_renderCounter;
                  draw_data.time_delta = m_viewportPrivateData->m_motionBlur.m_lastGameDeltaTime;

                  bool dlss_succeeded = sr_implementations[device_data.sr_type]->Draw(sr_instance_data, immediate_ctx.get(), draw_data);

                  if (dlss_succeeded)
                  {
                     device_data.has_drawn_sr = true;
                     device_data.force_reset_sr = false;
                  }
                  else
                  {
                     device_data.has_drawn_sr = false;
                     device_data.force_reset_sr = true;
                  }
               }

               if (device_data.has_drawn_sr)
               {
#if DEBUG_LOG
                  reshade::log::message(reshade::log::level::info, "Resolving Post SR");
#endif
                  if (game_device_data.postfx_mask_rtv.get())
                  {
                     ID3D11Resource* mask_resource;
                     game_device_data.postfx_mask_rtv->GetResource(&mask_resource);
                     immediate_ctx->CopyResource(game_device_data.unjittered_postfx_mask.get(), mask_resource);
                  }
                  SetLumaConstantBuffers(immediate_ctx.get(), cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaData);

                  immediate_ctx->IAGetVertexBuffers(0,1,0,0,0);
                  constexpr FLOAT blend_factor_alpha[4] = { 1.f, 1.f, 1.f, 1.f };
                  constexpr FLOAT blend_factor[4] = { 1.f, 1.f, 1.f, 0.f }; // TODO: this makes no sense as the blend state is unlikely to use it, use write mask instead
                  immediate_ctx->OMSetBlendState(nullptr, false ? blend_factor_alpha : blend_factor, 0xFFFFFFFF);
                  immediate_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
                  immediate_ctx->RSSetScissorRects(0, nullptr);
                  D3D11_VIEWPORT viewport;
                  viewport.TopLeftX = 0;
                  viewport.TopLeftY = 0;
                  viewport.Width = static_cast<float>(m_viewportPrivateData->m_viewportSize[0]);
                  viewport.Height = static_cast<float>(m_viewportPrivateData->m_viewportSize[1]);
                  viewport.MinDepth = 0;
                  viewport.MaxDepth = 1;
                  immediate_ctx->RSSetViewports(1, &viewport); // Viewport is always needed
                  ID3D11ShaderResourceView* srvs[] = {game_device_data.unjittered_depth_srv.get(), game_device_data.decoded_motion_vectors_srv.get(), game_device_data.unjittered_postfx_mask_srv.get()};
                  ID3D11RenderTargetView* rtvs[] = {m_deferredFXRendererContextTextures.m_motionVectors->m_texture->m_renderTargetView, game_device_data.postfx_mask_rtv.get()};
                  ID3D11Buffer* buffers[] = {game_device_data.viewport_cbv.get()};
                  ID3D11SamplerState* samplers[] = {game_device_data.depth_sampler.get()};
                  immediate_ctx->PSSetConstantBuffers(0, 1, buffers);
                  immediate_ctx->PSSetShaderResources(0, 3, srvs);
                  immediate_ctx->PSSetSamplers(0, 1, samplers);
                  immediate_ctx->OMSetDepthStencilState(game_device_data.depth_stencil_state.get(), 0);
                  immediate_ctx->OMSetRenderTargets(2, rtvs, m_deferredFXRendererContextTextures.m_linearDepthTexture->m_texture->m_depthStencilViews[0][0].get());

                  auto vs = device_data.native_vertex_shaders[CompileTimeStringHash("Copy VS")].get();
                  auto ps = device_data.native_pixel_shaders[CompileTimeStringHash("Unjitter Depth")].get();
                  immediate_ctx->VSSetShader(vs, nullptr, 0);
                  immediate_ctx->PSSetShader(ps, nullptr, 0);
                  immediate_ctx->IASetInputLayout(nullptr);
                  immediate_ctx->RSSetState(nullptr);
                  
                  // Finally draw:
                  immediate_ctx->Draw(4, 0);
                  
                  //TODO: write luminance to alpha as required for exposure + post effects
                  ComPtr<ID3D11UnorderedAccessView> color_uav;
                  {
                     ComPtr<ID3D11Device> native_device;
                     immediate_ctx->GetDevice(native_device.put());
                     
                     D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc;
                     uav_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                     uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
                     uav_desc.Texture2D.MipSlice = 0;
                     native_device->CreateUnorderedAccessView(device_data.sr_output_color.get(),
                        &uav_desc,
                        color_uav.put());
                  }
                  
                  if (color_uav.get() != nullptr)
                  {
                     ID3D11ShaderResourceView* srvs[] = {game_device_data.source_color_srv.get()};
                     ID3D11UnorderedAccessView* uavs[] = {color_uav.get()};
                     ID3D11Buffer* buffers[] = {game_device_data.viewport_cbv.get()};
                     immediate_ctx->CSSetShader(device_data.native_compute_shaders[CompileTimeStringHash("Copy Luminance")].get(), nullptr, 0);
                     immediate_ctx->CSSetConstantBuffers(0, 1, buffers);
                     immediate_ctx->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
                     immediate_ctx->CSSetShaderResources(0, 1, srvs);
               
                     immediate_ctx->Dispatch((m_viewportPrivateData->m_viewportSize[0] + 7) / 8, (m_viewportPrivateData->m_viewportSize[1] + 7) / 8, 1);
                  }
                  
                  immediate_ctx->CopyResource(game_device_data.source_color.get(), device_data.sr_output_color.get());
               }
               draw_state_stack.Restore(immediate_ctx.get());
               compute_state_stack.Restore(immediate_ctx.get());
            }
            else
            {
               draw_state_stack.Restore(immediate_ctx.get());
               compute_state_stack.Restore(immediate_ctx.get());
            }
         }
      }
   }
   
   void UpdateLumaInstanceDataCB(CB::LumaInstanceDataPadded& data, CommandListData& cmd_list_data, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      
      memcpy(&data.GameData.CameraSpaceToPreviousProjectedSpace, &game_device_data.CameraSpaceToPreviousProjectedSpace, sizeof(game_device_data.CameraSpaceToPreviousProjectedSpace));
      
      if (m_viewportPrivateData != nullptr)
      {
         DirectX::XMMATRIX prev_view_rot_proj = ComputePreviousViewRotProjectionMatrix();
         memcpy(&data.GameData.PreviousViewRotProjectionMatrix, &prev_view_rot_proj, sizeof(game_device_data.PreviousViewRotProjectionMatrix));
      }
      
      if (m_viewportPrivateData != nullptr)
      {
         data.GameData.PreviousCameraPosition = {
            m_viewportPrivateData->m_motionBlur.m_lastPreviousCamera.m_camera.m_position[0],
            m_viewportPrivateData->m_motionBlur.m_lastPreviousCamera.m_camera.m_position[1],
            m_viewportPrivateData->m_motionBlur.m_lastPreviousCamera.m_camera.m_position[2],
            1.0,
         };
         
         float2 jitter = {0.0, 0.0};
         jitter.x = m_viewportPrivateData->m_motionBlur.m_lastCurrentCamera.m_jitter[0] * (float)m_viewportPrivateData->m_viewportSize[0];
         jitter.y = m_viewportPrivateData->m_motionBlur.m_lastCurrentCamera.m_jitter[1] * (float)m_viewportPrivateData->m_viewportSize[1];
         data.GameData.CurrJitters = jitter;
         
         jitter.x = m_viewportPrivateData->m_motionBlur.m_lastPreviousCamera.m_jitter[0] * (float)m_viewportPrivateData->m_viewportSize[0];
         jitter.y = m_viewportPrivateData->m_motionBlur.m_lastPreviousCamera.m_jitter[1] * (float)m_viewportPrivateData->m_viewportSize[1];
         data.GameData.PrevJitters = jitter;
      }
      
   }
   
   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      
      if (device_data.sr_type != SR::Type::None)
      {
         if (GetAAOption() != OPTION_SMAA_T2X)
         {
            static bool has_sent_smaa_warning = false;
            if (!has_sent_smaa_warning && MessageBoxA(NULL, "DLSS requires SMAA T2x with MSAA off.", "DLSS Requirement", MB_OK | MB_SETFOREGROUND) == IDOK)
            {
               has_sent_smaa_warning = true;
            }
         }
      }

      JitterUpdate(device_data.sr_type != SR::Type::None);
      
      if (!custom_texture_mip_lod_bias_offset && CDeferredFxAntialiasRenderer)
      {
         std::shared_lock shared_lock_samplers(s_mutex_samplers);
         if (device_data.sr_type != SR::Type::None && !device_data.sr_suppressed)
         {
            device_data.texture_mip_lod_bias_offset = SR::GetMipLODBias((float)m_viewportPrivateData->m_viewportSize[1], (float)m_viewportPrivateData->m_viewportSize[1]); // This results in -1 at output res
         }
         else
         {
            device_data.texture_mip_lod_bias_offset = 0.0f;
         }
      }

      // release all resources from the game we got this frame
      game_device_data.partial_command_list.reset();
      game_device_data.remainder_command_list.store(nullptr, std::memory_order_relaxed);
      game_device_data.draw_device_context = nullptr;
      
      {
         //std::lock_guard<std::mutex> lock(game_device_data.game_device_data_mutex);
         game_device_data.source_color.reset();
         game_device_data.source_color_srv.reset();
         game_device_data.viewport_cbv.reset();
         game_device_data.postfx_mask_rtv.reset();
         
         CDeferredFxAntialiasRenderer = 0;
         bIsNetHackingRendering = false;
         m_deferredFXRendererContext = nullptr;
         m_viewportPrivateData = nullptr;
         m_viewportParamProvider = nullptr;
         m_deferredFxAntialiasRenderer = nullptr;
         m_currDeferredFXAntialiasFrameTexture = nullptr;
         m_deferredFXRendererContextTextures.m_accumBuffer = nullptr;
         m_deferredFXRendererContextTextures.m_linearDepthTexture = nullptr;
         m_deferredFXRendererContextTextures.m_smallDepthColorTexture = nullptr;
         m_deferredFXRendererContextTextures.m_depthStencilSurface = nullptr;
         m_deferredFXRendererContextTextures.m_motionVectors = nullptr;
         m_deferredFXRendererContextTextures.m_normalsTexture = nullptr;
         m_deferredFXRendererContextTextures.m_gBufferAOTexture = nullptr;
         m_deferredFXRendererContextTextures.m_fullAOTexture = nullptr;
         
         motion_vector_contexts.clear();
         
         std::swap(game_device_data.prev_skin_lookup, game_device_data.skin_lookup);
         game_device_data.skin_lookup.clear();
         std::swap(game_device_data.prev_skin_buffer, game_device_data.skin_buffer);
         game_device_data.skin_buffer->Reset();
      }

      if (!device_data.has_drawn_sr)
      {
         device_data.force_reset_sr = true;
      }
      
      device_data.has_drawn_sr = false;
      
      device_data.cb_luma_global_settings_dirty = true;
      int32_t sr_type = static_cast<int32_t>(device_data.sr_type);
      cb_luma_global_settings.SRType = static_cast<uint32_t>(sr_type + 1);
   }
   
   void PrintImGuiAbout() override
   {
      ImGui::Text("Luma for \"Watch Dogs 2\" is developed by Pumbo and is open source and free.\nIf you enjoy it, consider donating.", "");

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

         "\n\nThird Party:"
         "\nReShade"
         "\nDICE (HDR tonemapper)"
         , "");
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Watch Dogs 2 Luma mod");
      Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::WorkInProgress;
      Globals::VERSION = 1;

      luma_settings_cbuffer_index = 12; // 13 is used
      luma_data_cbuffer_index = 11;
      
      enable_samplers_upgrade = true;

#if HDR_ENABLED
      swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
      swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      texture_format_upgrades_type = TextureFormatUpgradesType::AllowedEnabled;
      texture_upgrade_formats = {
#if 0 // TODO: needed?
            reshade::api::format::r8g8b8a8_unorm,
            reshade::api::format::r8g8b8a8_unorm_srgb,
            reshade::api::format::r8g8b8a8_typeless,
#endif
#if 0 // These are probably not needed (unused) but shouldn't hurt (actually they are!!!)
            reshade::api::format::r8g8b8x8_unorm,
            reshade::api::format::r8g8b8x8_unorm_srgb,
            reshade::api::format::b8g8r8a8_unorm,
            reshade::api::format::b8g8r8a8_unorm_srgb,
            reshade::api::format::b8g8r8a8_typeless,
            reshade::api::format::b8g8r8x8_unorm,
            reshade::api::format::b8g8r8x8_unorm_srgb,
            reshade::api::format::b8g8r8x8_typeless,
#else
            reshade::api::format::r8g8b8a8_typeless,
            reshade::api::format::b8g8r8a8_typeless,
#endif
#if 1
            reshade::api::format::r11g11b10_float,
#endif
      };
      //texture_format_upgrades_2d_size_filters = 0 | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainResolution | (uint32_t)TextureFormatUpgrades2DSizeFilters::SwapchainAspectRatio;
      texture_format_upgrades_2d_size_filters = (uint32_t)TextureFormatUpgrades2DSizeFilters::None;
      //enable_indirect_texture_format_upgrades = true; // Makes the game crash when we copy textures on the CPU in "OnMapTextureRegion" // TODOFT
      enable_chain_indirect_texture_format_upgrades = ChainTextureFormatUpgradesType::DirectDependencies;

#if 1
      // Upgrade post process textures only, the rest is not needed, it damages performance and potentially causes artifacts
      auto_texture_format_upgrade_shader_hashes = {
         // Tonemappers:
         {0x0A7D2AB7, {{0}, {}}},
         {0xAD6E5AAF, {{0}, {}}},
         {0x10FA30C8, {{0}, {}}},
         {0x2D2FB973, {{0}, {}}},
         {0x691AE5AF, {{0}, {}}},
         {0xEA7FA4E5, {{0}, {}}},
         {0x67A672D7, {{0}, {}}},
         {0xC8651827, {{0}, {}}},
         
         {0x35B62AAF, {{0}, {}}}, // Upscale (Temporal Filtering)
         {0x6C8FE673, {{0}, {}}}, // FXAA
         {0x5554278D, {{0}, {}}}, // SMAA
         {0x8ADB0AAD, {{0}, {}}}, // PostFX
         {0x65D9186F, {{0}, {}}}, // PostFX
         {0x5EA57AF3, {{0}, {}}}, // PostFX
         {0xF584A327, {{0}, {}}}, // PostFX
         {0x84DB2096, {{0}, {}}}, // Blur UI
      };
#endif
      texture_format_upgrades_lut_size = 32;
      texture_format_upgrades_lut_dimensions = LUTDimensions::_3D;
      
      shader_hashes_ColorGradingLUT.compute_shaders = {
         0xAC50585B,
         0xEED9A3FF,
         0x2B8472D5,
         0x8B8BEC2A,
         0x919F1537,
         0x56F305BB,
         0x2033D7C9,
         0x0A696247,
         0x28816DF0,
         0x7336D9BE,
         0x6F668AD1,
         0x60118D8B,
         0x8F54485D,
         0xB054D156,
         0x21774BE1,
         0x69D3F6E7,
      };
      redirected_shader_hashes["ColorGradingLUT"] =
      {
         "AC50585B",
         "EED9A3FF",
         "2B8472D5",
         "8B8BEC2A",
         "919F1537",
         "56F305BB",
         "2033D7C9",
         "0A696247",
         "28816DF0",
         "7336D9BE",
         "6F668AD1",
         "60118D8B",
         "8F54485D",
         "B054D156",
         "21774BE1",
         "69D3F6E7",
      };
      // TODO: edge cases are still missing
      redirected_shader_hashes["Tonemap"] =
      {
         "0A7D2AB7",
         "AD6E5AAF",
         "10FA30C8",
         "2D2FB973",
         "691AE5AF",
         "EA7FA4E5",
         "67A672D7",
         "C8651827",
      };
#else
      swapchain_format_upgrade_type = TextureFormatUpgradesType::None;
      swapchain_upgrade_type = SwapchainUpgradeType::None;
      texture_format_upgrades_type = TextureFormatUpgradesType::None;
      texture_upgrade_formats = {
      };
#endif
      
      shader_hashes_TemporalFiltering.compute_shaders = {
         0x45FD59AC,
         0x14AA8AC5,
      };
      shader_hashes_SMAA_Reprojection.compute_shaders = {
         0x1445F2D0,
      };
      shader_hashes_SMAA_EdgeDectction.pixel_shaders = {
         0xE82D1C86,
      };
      shader_hashes_TemporalResolve.pixel_shaders = {
         0x4053E8B2,
      };
      shader_hashes_TemporalAA.pixel_shaders = {
         0x29C5D2F6,
         0xCB7D9EAE,
         0x3BF6F7A1,
         0xBE95E1B0,
      };
      shader_hashes_WaterGridVectorMap.pixel_shaders = {
         0xC8873B8F,
      };
      shader_hashes_ClothPreTransformed.vertex_shaders = {
         0xD298ACCF,
      };
      shader_hashes_PostFXMask.vertex_shaders = {
         0x39F7EEDF,
         0x99D12381,
         0x297B5CB6,
         0x686B8EF6,
         0x0696DFD6,
         0x1149EBDB,
         0x4179C60A,
         0x08724038,
         0x56152901,
         0xA4928DD1,
         0xB10DDE47,
         0xBA36576C,
         0xC1B9F2FD,
         0xC22FB3D8,
         0xC34C5FE0,
         0xE0A1C257,
         0xF323E6FC,
         0xFF5713A0,
         0x2C3F4493,
         0x33C5E45E
      };

#if DEVELOPMENT
      forced_shader_names.emplace(Shader::Hash_StrToNum("74F79E89"), "Clean to Black");
      forced_shader_names.emplace(Shader::Hash_StrToNum("4B06125F"), "Clean to Black");
      forced_shader_names.emplace(Shader::Hash_StrToNum("765C1510"), "UPlay Overlay");
      forced_shader_names.emplace(Shader::Hash_StrToNum("C941F7C4"), "Copy Depth");
      forced_shader_names.emplace(Shader::Hash_StrToNum("E82D1C86"), "SMAA Edges Detection");
      forced_shader_names.emplace(Shader::Hash_StrToNum("B9DD88BE"), "SMAA Weights Detection");
      forced_shader_names.emplace(Shader::Hash_StrToNum("1445F2D0"), "SMAA Weights Detection + Temporal Reprojection");
      forced_shader_names.emplace(Shader::Hash_StrToNum("5554278D"), "SMAA");
      forced_shader_names.emplace(Shader::Hash_StrToNum("29C5D2F6"), "Temporal Accumulation");
      forced_shader_names.emplace(Shader::Hash_StrToNum("4053E8B2"), "Temporal Accumulation Resolve");
#endif

      game = new WatchDogs2();
   }
   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
      g_deferred_fx_antialias_renderer_hook.reset();
      g_net_hacking_renderer_hook.reset();
      reshade::unregister_event<reshade::addon_event::execute_secondary_command_list>(WatchDogs2::OnExecuteSecondaryCommandList);
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}