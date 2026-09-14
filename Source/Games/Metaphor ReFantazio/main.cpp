#define GAME_METAPHOR_REFANTAZIO 1

#define ALLOW_SHADERS_DUMPING 0
#define ENABLE_DRAW_DISPATCH_DATA_CACHE 1

#include "..\..\Core\core.hpp"

#include "ShaderPatches\ShaderPatches.h"
#include "blue_noise.h"
#include "stretchy_buffer.h"
#include "temporal_aa_depth.h"
#define XXH_STATIC_LINKING_ONLY
#define XXH_IMPLEMENTATION
#include "xxhash.h"

static constexpr GUID GUID_DeferredContextTag = {0x4ea43a31, 0xee75, 0x4194, {0xb2, 0x8a, 0x39, 0x11, 0xd3, 0xe9, 0x14, 0x2f}};

struct GFD_VSCONST_TRANSFORM
{
   float4x4 mtxLocalToWorld;
   float4x4 mtxLocalToWorldViewProj;
   float4x4 mtxLocalToWorldViewProjPrev;
   float4x4 mtxModelToLocal;
};

struct GFD_VSCONST_VIEWPROJ
{
   float4x4 mtxViewProj;
   float4x4 mtxView;
   float4x4 mtxInvView;
   float3 eyePosition;
   float fovy;
};

struct GFD_PSCONST_SYSTEM
{
   float2 resolution;
   float2 resolutionRev;
   float4x4 mtxView;
   float4x4 mtxInvView;
   float4x4 mtxProj;
   float4x4 mtxInvProj;
   float4 invProjParams;
};

struct GFD_VSCONST_SKIN_CACHE
{
   uint offset;
   uint stride;
};

struct GFD_VSCONST_OUTLINE_PREV_DATA
{
   float4x4 mtxLocalToWorldPrev;
   float4x4 mtxViewProjPrev;
   float3 eyePositionPrev;
   uint skinned_mesh;
};

struct CB_PREPARE_OCEAN
{
   float4x4 mtxLocalToWorldPrev;
   float4x4 mtxViewProjPrev;
   bool useCurrentTexShift;
   uint TexShiftOffset;
};

struct TransformCacheEntry
{
   uint64_t transform_hash;
   float4x4 mtxLocalToWorldViewProj;
   float4x4 mtxLocalToWorld;
};

struct TransformCacheGroup
{
   std::vector<TransformCacheEntry> transforms[3];
};

struct OceanCacheEntry
{
   float4x4 mtxLocalToWorld;
   uint32_t TexShiftOffset;
};

struct SkinCacheItem
{
   ComPtr<ID3D11Buffer> buffer;
   uint32_t size;
   uint32_t stride;
};

struct SkinCacheEntry
{
   uint32_t offset;
   uint32_t stride;
};

struct BoundingBox
{
   float3 min;
   float3 max;
};

struct Plane
{
   float3 normal;
   float d;
};

enum class UpscalingMode : uint32_t
{
   Auto = 0,
   SuperResolution = 1,
   Game = 2
};

enum class ContextTag : uint32_t
{
   Unknown = 0,
   Skin_0 = 1,
   Skin_1 = 2,
   Skin_2 = 3,
   ShadowMap_0 = 4,
   ShadowMap_1 = 5,
   ShadowMap_2 = 6,
   Reflection_0 = 7,
   Reflection_1 = 8,
   Reflection_2 = 9,
   Draw_0 = 10,
   Draw_1 = 11,
   Draw_2 = 12,
};

enum class ContextType : uint32_t
{
   Other = 0,
   Skin,
   ShadowMap,
   Reflection,
   Draw
};

// vertex buffers have usually either a stride of 28 or 40 bytes
// we don't know which at creation time so we store the bounding boxes for both options
struct BoundingBoxCollection
{
   BoundingBox box28;
   BoundingBox box40;
};

class FrameProgress
{
public:
   enum Events
   {
      FrameStarted,
      OpaqueRenderingStarted,
      BackgroundTonemapped,
      AddedParticles,
      LutApplied,
      DepthTemporalAADrawn,
      SceneUiDrawStarted,
      SceneUiDrawFinished,
      Count
   };

   FrameProgress()
   {
      Reset();
   }

   void Reset()
   {
      for (uint32_t i = 0; i < Count; ++i)
      {
         markers[i] = false;
      }
   }

   bool Reached(Events event)
   {
      return markers[event];
   }

   void SetReached(Events event)
   {
      markers[event] = true;
   }

private:
   bool markers[Events::Count];
};

M_INLINE float3 TransformPoint(const float4x4 m, const float3& b)
{
   float3 v;
   v.x = m.m00 * b.x + m.m01 * b.y + m.m02 * b.z + m.m03;
   v.y = m.m10 * b.x + m.m11 * b.y + m.m12 * b.z + m.m13;
   v.z = m.m20 * b.x + m.m21 * b.y + m.m22 * b.z + m.m23;
   return v;
}

M_INLINE bool IsOutsideFrustum(const float4x4& worldViewProj, const BoundingBox& box)
{
   // Plane planes[4];

   //// left
   // planes[0].normal.x = worldViewProj.m30 + worldViewProj.m00;
   // planes[0].normal.y = worldViewProj.m31 + worldViewProj.m01;
   // planes[0].normal.z = worldViewProj.m32 + worldViewProj.m02;
   // planes[0].d = worldViewProj.m33 + worldViewProj.m03;

   //// right
   // planes[1].normal.x = worldViewProj.m30 - worldViewProj.m00;
   // planes[1].normal.y = worldViewProj.m31 - worldViewProj.m01;
   // planes[1].normal.z = worldViewProj.m32 - worldViewProj.m02;
   // planes[1].d = worldViewProj.m33 - worldViewProj.m03;

   //// bottom
   // planes[2].normal.x = worldViewProj.m30 + worldViewProj.m10;
   // planes[2].normal.y = worldViewProj.m31 + worldViewProj.m11;
   // planes[2].normal.z = worldViewProj.m32 + worldViewProj.m12;
   // planes[2].d = worldViewProj.m33 + worldViewProj.m13;

   //// top
   // planes[3].normal.x = worldViewProj.m30 - worldViewProj.m10;
   // planes[3].normal.y = worldViewProj.m31 - worldViewProj.m11;
   // planes[3].normal.z = worldViewProj.m32 - worldViewProj.m12;
   // planes[3].d = worldViewProj.m33 - worldViewProj.m13;

   // for (uint32_t i = 0; i < 4; ++i)
   //{
   //    float positive = (planes[i].normal.x >= 0 ? box.max.x : box.min.x) * planes[i].normal.x;
   //    positive += (planes[i].normal.y >= 0 ? box.max.y : box.min.y) * planes[i].normal.y;
   //    positive += (planes[i].normal.z >= 0 ? box.max.z : box.min.z) * planes[i].normal.z;

   //    if (positive + planes[i].d < 0)
   //    {
   //        return true;
   //    }
   //}
   // return false;

   __m128 row0 = _mm_load_ps(&worldViewProj.m00);
   __m128 row1 = _mm_load_ps(&worldViewProj.m10);
   __m128 row3 = _mm_load_ps(&worldViewProj.m30);

   __m128 left = _mm_add_ps(row3, row0);
   __m128 right = _mm_sub_ps(row3, row0);
   __m128 bottom = _mm_add_ps(row3, row1);
   __m128 top = _mm_sub_ps(row3, row1);

   __m128 normal_x, normal_y, normal_z, d;
   // basically _MM_TRANSPOSE4_PS
   {
      __m128 _Tmp0 = _mm_shuffle_ps((left), (right), 0x44);
      __m128 _Tmp2 = _mm_shuffle_ps((left), (right), 0xEE);
      __m128 _Tmp1 = _mm_shuffle_ps((bottom), (top), 0x44);
      __m128 _Tmp3 = _mm_shuffle_ps((bottom), (top), 0xEE);

      (normal_x) = _mm_shuffle_ps(_Tmp0, _Tmp1, 0x88);
      (normal_y) = _mm_shuffle_ps(_Tmp0, _Tmp1, 0xDD);
      (normal_z) = _mm_shuffle_ps(_Tmp2, _Tmp3, 0x88);
      (d) = _mm_shuffle_ps(_Tmp2, _Tmp3, 0xDD);
   }

   __m128 zero = _mm_setzero_ps();

   __m128 box_min_x = _mm_set_ps1(box.min.x);
   __m128 box_max_x = _mm_set_ps1(box.max.x);

   __m128 x_negative = _mm_cmplt_ps(normal_x, zero);
   __m128 distance = _mm_mul_ps(_mm_or_ps(_mm_andnot_ps(x_negative, box_max_x), _mm_and_ps(x_negative, box_min_x)), normal_x);

   __m128 box_min_y = _mm_set_ps1(box.min.y);
   __m128 box_max_y = _mm_set_ps1(box.max.y);

   __m128 y_negative = _mm_cmplt_ps(normal_y, zero);
   distance = _mm_add_ps(distance, _mm_mul_ps(_mm_or_ps(_mm_andnot_ps(y_negative, box_max_y), _mm_and_ps(y_negative, box_min_y)), normal_y));

   __m128 box_min_z = _mm_set_ps1(box.min.z);
   __m128 box_max_z = _mm_set_ps1(box.max.z);

   __m128 z_negative = _mm_cmplt_ps(normal_z, zero);
   distance = _mm_add_ps(distance, _mm_mul_ps(_mm_or_ps(_mm_andnot_ps(z_negative, box_max_z), _mm_and_ps(z_negative, box_min_z)), normal_z));

   __m128 test = _mm_cmplt_ps(_mm_add_ps(distance, d), zero);

   return _mm_movemask_ps(test) != 0;
}

struct DrawContextData
{
   GFD_VSCONST_TRANSFORM vsconst_transform_data;
   bool vsconst_transform_data_changed = false;

   FrameProgress frame_progress;

   float2 projection_jitters = {0, 0};

   // resources used to identify the deferred context used for scene drawing
   ComPtr<ID3D11CommandList> remainder_command_list;

   // textures we got from the game
   ComPtr<ID3D11Texture2D> source_color;
   ComPtr<ID3D11Texture2D> dest_color;
   ComPtr<ID3D11Texture2D> depth_texture;
   ComPtr<ID3D11Texture2D> particle_texture;

   // resources needed for sr
   ComPtr<ID3D11Texture2D> motion_vectors;
   ComPtr<ID3D11RenderTargetView> motion_vectors_rtv;
   ComPtr<ID3D11ShaderResourceView> motion_vectors_srv;

   // the command list we split to interject dlss
   std::vector<ComPtr<ID3D11CommandList>> partial_command_lists;

   // resources related to MSAA rendering of the UI elements rendered in the 3D scene
   ComPtr<ID3D11RasterizerState> original_scene_raterizer_state;
   ComPtr<ID3D11BlendState> original_scene_blend_state;
   ComPtr<ID3D11RenderTargetView> original_scene_texture_rtv;
   ComPtr<ID3D11DepthStencilView> original_scene_dsv;
   ComPtr<ID3D11Texture2D> scene_ui_texture;
   ComPtr<ID3D11RenderTargetView> scene_ui_texture_rtv;
   ComPtr<ID3D11Texture2D> scene_ui_depth_texture;
   ComPtr<ID3D11DepthStencilView> scene_ui_depth_texture_dsv;
   ComPtr<ID3D11Texture2D> resolved_scene_ui_texture;
   ComPtr<ID3D11ShaderResourceView> resolved_scene_ui_texture_srv;
   uint32_t scene_ui_resource_width = 0;
   uint32_t scene_ui_resource_height = 0;
   uint32_t scene_ui_resource_msaa_samples = 0;

   // bloom intermediate render target
   ComPtr<ID3D11Texture2D> bloom_texture;
   ComPtr<ID3D11RenderTargetView> bloom_texture_rtv;
   ComPtr<ID3D11ShaderResourceView> bloom_texture_srv;

   // values extracted from ps system cbuffer
   float4x4 inv_proj;
   float4x4 proj;
   float4x4 proj_with_jitter;
   float4x4 view;
   float3 eye_pos = {};
   float fov = 0.0f;

   // cached values
   float4x4 prev_inv_proj;
   float4x4 prev_proj_with_current_jitter;
   float4x4 prev_view_proj;
   float3 prev_eye_pos = {};

   // cached skinning data
   std::unordered_map<ID3D11Buffer*, SkinCacheItem> pending_skin_cache;
   std::unique_ptr<StretchyBuffer> skin_buffer;
   std::unordered_map<ID3D11Buffer*, SkinCacheEntry> skin_lookup;

   // cache ocean data
   std::unique_ptr<StretchyBuffer> ocean_buffer;
   std::vector<OceanCacheEntry> ocean_lookup;

   // used to store cbuffer data when it's not clear yet which ones we want to watch
   std::unordered_map<ID3D11Buffer*, std::array<uint8_t, 288>> cbuffer_cache;
};

struct ShadowContextData
{
   float4x4 world_view_proj;
   bool world_view_proj_valid = false;
};

namespace
{
   bool first_boot = true; // Automatic setting
   bool enable_hdr = false;
   bool next_enable_hdr = enable_hdr; // The value we serialize, that will be ignored until reboot
   UpscalingMode upscaling_mode = UpscalingMode::Auto;

#if DEVELOPMENT || TEST
   uint32_t shadow_draw_calls = 0;
   uint32_t shadow_draw_calls_culled = 0;
   uint32_t draw_calls_culled = 0;
#endif

   uint32_t g_scene_ui_msaa_samples = 8;
   ShaderHashesList shader_hashes_tonemap;
   ShaderHashesList shader_hashes_merge_particles;
   ShaderHashesList shader_hashes_fxaa;
   ShaderHashesList shader_hashes_smaa_edge_detection;
   ShaderHashesList shader_hashes_smaa_weight_calculation;
   ShaderHashesList shader_hashes_smaa_blending;
   ShaderHashesList shader_hashes_dof_prepare;
   ShaderHashesList shader_hashes_lut;
   ShaderHashesList shader_hashes_outline;
   ShaderHashesList shader_hashes_ocean;
   ShaderHashesList shader_hashes_material;
} // namespace

struct GameDeviceDataMetaphorReFantazio final : public GameDeviceData
{
#if ENABLE_SR
   // SR
   std::atomic<bool> has_drawn_upscaling = false;

   // resources used to apply sr
   ComPtr<ID3D11Texture2D> scaled_motion_vectors;
   ComPtr<ID3D11UnorderedAccessView> scaled_motion_vectors_uav;
   ComPtr<ID3D11Texture2D> bias_mask;
   ComPtr<ID3D11UnorderedAccessView> bias_mask_uav;
   ComPtr<ID3D11Texture2D> resolve_texture;
   ComPtr<ID3D11Texture2D> merged_texture;
   ComPtr<ID3D11UnorderedAccessView> merged_texture_uav;
   ComPtr<ID3D11ShaderResourceView> merged_texture_srv;
   ComPtr<ID3D11RenderTargetView> merged_texture_rtv;

   // constant buffers
   ComPtr<ID3D11Buffer> cbuffer_prev_data;
   ComPtr<ID3D11Buffer> cbuffer_prepare_ocean_data;
   ComPtr<ID3D11Buffer> cbuffer_skin_cache;
   ComPtr<ID3D11Buffer> cbuffer_motion_vector;

   // the constant buffer we watch for transform updates
   std::atomic<ID3D11Buffer*> cb_viewproj = nullptr;
   std::atomic<ID3D11Buffer*> cb_transform = nullptr;
   std::atomic<ID3D11Buffer*> cb_system = nullptr;

   // cached transform
   std::mutex transform_mutex;
   std::unordered_map<uint64_t, TransformCacheGroup> transform_lookup;

   bool upscaling = false;

   std::mutex temporal_depth_pass_mutex;
   TemporalAADepth::TemporalAADepthPass temporal_depth_pass;
#endif // ENABLE_SR
   DrawContextData draw_contexts[3];
   ShadowContextData shadow_contexts[3];

   uint32_t command_list_count = 0;

   // std::vector<ComPtr<ID3D11Texture2D>> bayer_matrix_textures;
   // std::vector<ComPtr<ID3D11ShaderResourceView>> bayer_matrix_texture_srvs;

   ComPtr<ID3D10Multithread> multithread;

   ComPtr<ID3D11Texture2D> noise_texture;
   ComPtr<ID3D11ShaderResourceView> noise_texture_srv;

   ComPtr<ID3D11Buffer> scratch_constant_buffer;
   ComPtr<ID3D11UnorderedAccessView> scratch_constant_buffer_uav;

   // resources related to MSAA rendering of the UI elements rendered in the 3D scene
   ComPtr<ID3D11RasterizerState> scene_ui_rasterizer_state;
   ComPtr<ID3D11BlendState> scene_ui_blend_state;
   ComPtr<ID3D11BlendState> scene_ui_merge_blend_state;

   // resources related to frustum culling
   ID3D11Buffer* cb_shadow_transform = nullptr;
   std::shared_mutex bounding_box_mutex;
   std::unordered_map<ID3D11Buffer*, BoundingBoxCollection> bounding_boxes;

   std::unordered_map<uint32_t, std::array<uint32_t, 2>> vertex_shader_ndc_coord_indices;
   std::unordered_map<uint32_t, ComPtr<ID3D11VertexShader>> original_vertex_shaders;
   std::unordered_map<uint32_t, ComPtr<ID3D11VertexShader>> modified_vertex_shaders;
   std::unordered_map<uint32_t, std::vector<std::byte>> pixel_shader_code;
   std::shared_mutex pixel_shader_mutex;
   std::unordered_map<uint32_t, ComPtr<ID3D11PixelShader>> modified_pixel_shaders;
};

class MetaphorReFantazio final : public Game
{
   static GameDeviceDataMetaphorReFantazio& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceDataMetaphorReFantazio*>(device_data.game);
   }

   static bool SrActive(const DeviceData& device_data)
   {
      return device_data.sr_type != SR::Type::None && !device_data.sr_suppressed;
   }

   static bool UseSRForUpscaling(const DeviceData& device_data)
   {
      return (upscaling_mode == UpscalingMode::SuperResolution || (upscaling_mode == UpscalingMode::Auto && device_data.sr_type == SR::Type::DLSS)) && !device_data.sr_suppressed;
   }

   static ContextTag GetContextTag(ID3D11DeviceContext* device_context)
   {
      uint32_t tag_size = sizeof(ContextTag);
      ContextTag tag;
      device_context->GetPrivateData(GUID_DeferredContextTag, &tag_size, &tag);
      return tag;
   }

   static ContextType GetContextType(ContextTag tag)
   {
      ContextType type;
      switch (tag)
      {
      case ContextTag::Skin_0:
      case ContextTag::Skin_1:
      case ContextTag::Skin_2:
         type = ContextType::Skin;
         break;
      case ContextTag::ShadowMap_0:
      case ContextTag::ShadowMap_1:
      case ContextTag::ShadowMap_2:
         type = ContextType::ShadowMap;
         break;
      case ContextTag::Reflection_0:
      case ContextTag::Reflection_1:
      case ContextTag::Reflection_2:
         type = ContextType::Reflection;
         break;
      case ContextTag::Draw_0:
      case ContextTag::Draw_1:
      case ContextTag::Draw_2:
         type = ContextType::Draw;
         break;
      default:
         type = ContextType::Other;
         break;
      }
      return type;
   }

   static uint32_t GetContextIndex(ContextTag tag)
   {
      uint32_t index;
      switch (tag)
      {
      case ContextTag::Skin_0:
      case ContextTag::ShadowMap_0:
      case ContextTag::Reflection_0:
      case ContextTag::Draw_0:
         index = 0;
         break;
      case ContextTag::Skin_1:
      case ContextTag::ShadowMap_1:
      case ContextTag::Reflection_1:
      case ContextTag::Draw_1:
         index = 1;
         break;
      case ContextTag::Skin_2:
      case ContextTag::ShadowMap_2:
      case ContextTag::Reflection_2:
      case ContextTag::Draw_2:
         index = 2;
         break;
      default:
         index = 0;
         break;
      }

      return index;
   }

   static uint32_t GetPreviousContextIndex(uint32_t index)
   {
      return (index + 2) % 3;
   }

public:
   void OnInit(bool async) override
   {
      std::vector<ShaderDefineData> game_shader_defines_data = {
         {"ENABLE_HDR_BOOST", '1', true, false, "Enable a \"Fake\" HDR boosting effect.", 1},
      };
      shader_defines_data.append_range(game_shader_defines_data);

      GetShaderDefineData(POST_PROCESS_SPACE_TYPE_HASH).SetDefaultValue('1');

      native_shaders_definitions.emplace(CompileTimeStringHash("Copy Depth"), ShaderDefinition{"Luma_CopyDepth", reshade::api::pipeline_subobject_type::pixel_shader});
      native_shaders_definitions.emplace(CompileTimeStringHash("Gaussian Blur Horizontal"),
         ShaderDefinition{"Luma_GaussianBlur", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr, {{"HORIZONTAL", "1"}}});
      native_shaders_definitions.emplace(CompileTimeStringHash("Gaussian Blur Vertical"), ShaderDefinition{"Luma_GaussianBlur", reshade::api::pipeline_subobject_type::pixel_shader});
      native_shaders_definitions.emplace(CompileTimeStringHash("Prepare Motion Vector"), ShaderDefinition{"Luma_PrepareMotionVector", reshade::api::pipeline_subobject_type::compute_shader});
      native_shaders_definitions.emplace(CompileTimeStringHash("Prepare Ocean Data"), ShaderDefinition{"Luma_PrepareOceanData", reshade::api::pipeline_subobject_type::compute_shader});
      native_shaders_definitions.emplace(CompileTimeStringHash("Create Bias Mask"), ShaderDefinition{"Luma_CreateBiasMask", reshade::api::pipeline_subobject_type::compute_shader});
      native_shaders_definitions.emplace(CompileTimeStringHash("Merge"), ShaderDefinition{"Luma_CopyDsrResult", reshade::api::pipeline_subobject_type::compute_shader});
      native_shaders_definitions.emplace(CompileTimeStringHash("Temporal AA Depth With History"),
         ShaderDefinition{"Luma_TemporalAADepth", reshade::api::pipeline_subobject_type::compute_shader, nullptr, nullptr, {{"HAS_PREVIOUS_FRAME", "1"}}});
      native_shaders_definitions.emplace(CompileTimeStringHash("Temporal AA Depth Without History"),
         ShaderDefinition{"Luma_TemporalAADepth", reshade::api::pipeline_subobject_type::compute_shader, nullptr, nullptr, {}});

      reshade::register_event<reshade::addon_event::execute_secondary_command_list>(MetaphorReFantazio::OnExecuteSecondaryCommandList);
      reshade::register_event<reshade::addon_event::update_buffer_region_command>(MetaphorReFantazio::OnUpdateBufferRegionCommand);
      reshade::register_event<reshade::addon_event::create_pipeline>(MetaphorReFantazio::OnCreatePipeline);
      reshade::register_event<reshade::addon_event::init_resource>(MetaphorReFantazio::OnInitResource);
      reshade::register_event<reshade::addon_event::destroy_resource>(MetaphorReFantazio::OnDestroyResource);
      reshade::register_event<reshade::addon_event::init_command_list>(MetaphorReFantazio::OnInitCommandList);
   }

   void LoadConfigs() override
   {
      reshade::api::effect_runtime* runtime = nullptr;
      reshade::get_config_value(runtime, NAME, "SceneUiMsaaSamples", g_scene_ui_msaa_samples);
      if (g_scene_ui_msaa_samples != 1 &&
          g_scene_ui_msaa_samples != 2 &&
          g_scene_ui_msaa_samples != 4 &&
          g_scene_ui_msaa_samples != 8)
      {
         g_scene_ui_msaa_samples = 8;
      }
      uint32_t upscaling_mode_uint = (uint32_t)UpscalingMode::Auto;
      if (!reshade::get_config_value(runtime, NAME, "UpscalingMode", upscaling_mode_uint))
      {
         bool use_sr_for_upscaling;
         if (reshade::get_config_value(runtime, NAME, "UseSRForUpscaling", use_sr_for_upscaling))
         {
            upscaling_mode_uint = (uint32_t)(use_sr_for_upscaling ? UpscalingMode::SuperResolution : UpscalingMode::Auto);
         }
      }
      if (upscaling_mode_uint > (uint32_t)UpscalingMode::Game)
      {
         upscaling_mode = UpscalingMode::Auto;
      }
      else
      {
         upscaling_mode = (UpscalingMode)upscaling_mode_uint;
      }
   }

   void OnInitSwapchain(reshade::api::swapchain* swapchain) override
   {
      auto& device_data = *swapchain->get_device()->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);
      if (!enable_hdr)
      {
         cb_luma_global_settings.DisplayMode = DisplayModeType::SDR;
         cb_luma_global_settings.ScenePeakWhite = srgb_white_level;
         cb_luma_global_settings.ScenePaperWhite = srgb_white_level;
         cb_luma_global_settings.UIPaperWhite = srgb_white_level;
      }
   }

   void OnInitDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      {
         D3D11_BUFFER_DESC bd = {};
         bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
         bd.ByteWidth = 144;
         bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
         bd.MiscFlags = 0;
         bd.StructureByteStride = 0;
         bd.Usage = D3D11_USAGE_DYNAMIC;
         native_device->CreateBuffer(&bd, nullptr, game_device_data.cbuffer_prev_data.put());
      }

      {
         D3D11_BUFFER_DESC bd = {};
         bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
         bd.ByteWidth = 144;
         bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
         bd.MiscFlags = 0;
         bd.StructureByteStride = 0;
         bd.Usage = D3D11_USAGE_DYNAMIC;
         native_device->CreateBuffer(&bd, nullptr, game_device_data.cbuffer_prepare_ocean_data.put());
      }

      {
         D3D11_BUFFER_DESC bd = {};
         bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
         bd.ByteWidth = 16;
         bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
         bd.MiscFlags = 0;
         bd.StructureByteStride = 0;
         bd.Usage = D3D11_USAGE_DYNAMIC;
         native_device->CreateBuffer(&bd, nullptr, game_device_data.cbuffer_skin_cache.put());
      }

      {
         D3D11_BUFFER_DESC bd = {};
         bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
         bd.ByteWidth = 64;
         bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
         bd.MiscFlags = 0;
         bd.StructureByteStride = 0;
         bd.Usage = D3D11_USAGE_DYNAMIC;
         native_device->CreateBuffer(&bd, nullptr, game_device_data.cbuffer_motion_vector.put());
      }

      {
         D3D11_BUFFER_DESC bd = {};
         bd.ByteWidth = 144;
         bd.Usage = D3D11_USAGE_DEFAULT;
         bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
         bd.CPUAccessFlags = 0;
         bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
         bd.StructureByteStride = 144;
         native_device->CreateBuffer(&bd, nullptr, game_device_data.scratch_constant_buffer.put());
      }

      {
         D3D11_UNORDERED_ACCESS_VIEW_DESC uavd = {};
         uavd.Format = DXGI_FORMAT_UNKNOWN;
         uavd.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
         uavd.Buffer.FirstElement = 0;
         uavd.Buffer.Flags = 0;
         uavd.Buffer.NumElements = 1;
         native_device->CreateUnorderedAccessView(game_device_data.scratch_constant_buffer.get(), &uavd, game_device_data.scratch_constant_buffer_uav.put());
      }

      {
         D3D11_RASTERIZER_DESC rd = {};
         rd.FillMode = D3D11_FILL_SOLID;
         rd.CullMode = D3D11_CULL_NONE;
         rd.FrontCounterClockwise = TRUE;
         rd.DepthBias = 0;
         rd.DepthBiasClamp = 0.0f;
         rd.SlopeScaledDepthBias = 0.0f;
         rd.DepthClipEnable = TRUE;
         rd.ScissorEnable = FALSE;
         rd.MultisampleEnable = TRUE;
         rd.AntialiasedLineEnable = TRUE;

         native_device->CreateRasterizerState(&rd, game_device_data.scene_ui_rasterizer_state.put());
      }

      {
         D3D11_BLEND_DESC bd = {};
         bd.AlphaToCoverageEnable = FALSE;
         bd.IndependentBlendEnable = FALSE;
         bd.RenderTarget[0].BlendEnable = TRUE;
         bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
         bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
         bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
         bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
         bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
         bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
         bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

         native_device->CreateBlendState(&bd, game_device_data.scene_ui_blend_state.put());
      }

      {
         D3D11_BLEND_DESC bd = {};
         bd.AlphaToCoverageEnable = FALSE;
         bd.IndependentBlendEnable = FALSE;
         bd.RenderTarget[0].BlendEnable = TRUE;
         bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
         bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
         bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
         bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
         bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
         bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
         bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

         native_device->CreateBlendState(&bd, game_device_data.scene_ui_merge_blend_state.put());
      }

      // game_device_data.bayer_matrix_textures.resize(16);
      // game_device_data.bayer_matrix_texture_srvs.resize(16);

      // for (uint32_t i = 0; i < 16; ++i)
      //{
      //    {
      //       D3D11_TEXTURE2D_DESC desc = {};
      //       desc.Width = 4;
      //       desc.Height = 4;
      //       desc.Usage = D3D11_USAGE_DEFAULT;
      //       desc.ArraySize = 1;
      //       desc.Format = DXGI_FORMAT_R8_UNORM;
      //       desc.SampleDesc.Count = 1;
      //       desc.SampleDesc.Quality = 0;
      //       desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
      //       desc.CPUAccessFlags = 0;
      //       desc.MiscFlags = 0;
      //       desc.MipLevels = 1;

      //      uint32_t offset_x = i % 4;
      //      uint32_t offset_y = i / 4;

      //      uint8_t bayer[4][4] = {{15, 135, 45, 165},
      //         {195, 75, 225, 105},
      //         {60, 180, 30, 150},
      //         {240, 120, 210, 90}};

      //      uint8_t data[16];

      //      for (uint32_t x = 0; x < 4; ++x)
      //      {
      //         for (uint32_t y = 0; y < 4; ++y)
      //         {
      //            data[y * 4 + x] = bayer[(x + offset_x) & 3][(y + offset_y) & 3];
      //         }
      //      }

      //      D3D11_SUBRESOURCE_DATA subresource_data;
      //      subresource_data.pSysMem = data;
      //      subresource_data.SysMemPitch = 4;
      //      subresource_data.SysMemSlicePitch = 16;

      //      native_device->CreateTexture2D(&desc,
      //         &subresource_data,
      //         game_device_data.bayer_matrix_textures[i].put());
      //   }
      //   {
      //      native_device->CreateShaderResourceView(game_device_data.bayer_matrix_textures[i].get(),
      //         nullptr,
      //         game_device_data.bayer_matrix_texture_srvs[i].put());
      //   }
      //}

      {
         D3D11_TEXTURE2D_DESC desc = {};
         desc.Width = 32;
         desc.Height = 32;
         desc.Usage = D3D11_USAGE_DEFAULT;
         desc.ArraySize = 1;
         desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
         desc.SampleDesc.Count = 1;
         desc.SampleDesc.Quality = 0;
         desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
         desc.CPUAccessFlags = 0;
         desc.MiscFlags = 0;
         desc.MipLevels = 1;

         D3D11_SUBRESOURCE_DATA subresource_data;
         subresource_data.pSysMem = blue_noise_data;
         subresource_data.SysMemPitch = 4 * blue_noise_width;
         subresource_data.SysMemSlicePitch = blue_noise_height * subresource_data.SysMemPitch;

         native_device->CreateTexture2D(&desc,
            &subresource_data,
            game_device_data.noise_texture.put());
      }
      {
         native_device->CreateShaderResourceView(game_device_data.noise_texture.get(),
            nullptr,
            game_device_data.noise_texture_srv.put());
      }

      ComPtr<ID3D11RasterizerState> scene_ui_rasterizer_state;
      ComPtr<ID3D11BlendState> scene_ui_blend_state;
      ComPtr<ID3D11BlendState> scene_ui_merge_blend_state;

      ComPtr<ID3D11DeviceContext> context;
      native_device->GetImmediateContext(context.put());
      // walking around Grand Trad 28 MB seems to be the max used
      for (uint32_t i = 0; i < 3; ++i)
      {
         game_device_data.draw_contexts[i].skin_buffer = std::make_unique<StretchyBuffer>(native_device, context.get(), 32 * 1024 * 1024);

         game_device_data.draw_contexts[i].ocean_buffer = std::make_unique<StretchyBuffer>(native_device, context.get(), 32);
      }

      native_device->QueryInterface(game_device_data.multithread.put());

      // no taa but needed for DLSS indicator in UI
      device_data.taa_detected = true;
   }

   void SetupMotionVectorTexture(ID3D11Device* device, DrawContextData& context_data, uint32_t width, uint32_t height)
   {
      if (width == 0 ||
          height == 0)
      {
         return;
      }
      if (context_data.motion_vectors)
      {
         D3D11_TEXTURE2D_DESC mv_desc = {};
         context_data.motion_vectors->GetDesc(&mv_desc);
         if (mv_desc.Width == width &&
             mv_desc.Height == height)
         {
            return;
         }
      }
      {
         D3D11_TEXTURE2D_DESC motion_vector_desc = {};
         motion_vector_desc.Width = width;
         motion_vector_desc.Height = height;
         motion_vector_desc.Usage = D3D11_USAGE_DEFAULT;
         motion_vector_desc.ArraySize = 1;
         motion_vector_desc.Format = DXGI_FORMAT_R32G32_FLOAT;
         motion_vector_desc.SampleDesc.Count = 1;
         motion_vector_desc.SampleDesc.Quality = 0;
         motion_vector_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
         motion_vector_desc.CPUAccessFlags = 0;
         motion_vector_desc.MiscFlags = 0;
         motion_vector_desc.MipLevels = 1;

         device->CreateTexture2D(&motion_vector_desc,
            nullptr,
            context_data.motion_vectors.put());
      }
      {
         D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
         rtv_desc.Format = DXGI_FORMAT_R32G32_FLOAT;
         rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
         rtv_desc.Texture2D.MipSlice = 0;

         device->CreateRenderTargetView(context_data.motion_vectors.get(),
            &rtv_desc,
            context_data.motion_vectors_rtv.put());
      }
      {
         D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
         srv_desc.Format = DXGI_FORMAT_R32G32_FLOAT;
         srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
         srv_desc.Texture2D.MostDetailedMip = 0;
         srv_desc.Texture2D.MipLevels = 1;

         device->CreateShaderResourceView(context_data.motion_vectors.get(),
            &srv_desc,
            context_data.motion_vectors_srv.put());
      }
   }

   void CommitSkinCache(ID3D11DeviceContext* native_device_context, DrawContextData& context_data)
   {
      if (context_data.pending_skin_cache.empty())
      {
         return;
      }
      context_data.skin_buffer->Reset();
      context_data.skin_lookup.clear();
      for (const auto& it : context_data.pending_skin_cache)
      {
         const SkinCacheItem& pending_item = it.second;

         SkinCacheEntry cache_entry = {};
         cache_entry.offset = context_data.skin_buffer->size;
         cache_entry.stride = pending_item.stride;

         context_data.skin_buffer->CopyFromBuffer(native_device_context, pending_item.buffer.get(), 0, pending_item.size);

         context_data.skin_lookup[it.first] = cache_entry;
      }
      context_data.pending_skin_cache.clear();
   }

   static void HandleTransformUpdate(ID3D11Buffer* buffer, const void* data, DrawContextData& context_data)
   {
      context_data.vsconst_transform_data = *(GFD_VSCONST_TRANSFORM*)data;
      context_data.vsconst_transform_data_changed = true;
   }

   static void UpdatePreviousTransformAndCache(bool has_pixel_shader, bool is_outline_pass, bool is_skinned_mesh, ID3D11Buffer* vertex_buffer, ID3D11DeviceContext* native_device_context, ContextTag context_tag, GameDeviceDataMetaphorReFantazio& game_device_data, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes)
   {
      DrawContextData& context_data = game_device_data.draw_contexts[GetContextIndex(context_tag)];
      GFD_VSCONST_TRANSFORM vs_consts = context_data.vsconst_transform_data;

      if (has_pixel_shader)
      {
         auto hash_transform = [](float4x4 transform)
         {
            return XXH3_64bits((const uint8_t*)&transform.m30, 4 * sizeof(float));
         };

         auto hash_draw_call = [](uint64_t pixel_shader, ID3D11Buffer* vertex_buffer, uint32_t vertex_count)
         {
            uint8_t buffer[sizeof(pixel_shader) + sizeof(vertex_buffer) + sizeof(vertex_count)] = {};
            memcpy(&buffer[0], &pixel_shader, sizeof(pixel_shader));
            memcpy(&buffer[sizeof(pixel_shader)], &vertex_buffer, sizeof(vertex_buffer));
            memcpy(&buffer[sizeof(pixel_shader) + sizeof(vertex_buffer)], &vertex_count, sizeof(vertex_count));
            return XXH3_64bits(buffer, sizeof(buffer));
         };

         uint64_t draw_call_hash = hash_draw_call(original_shader_hashes.pixel_shaders[0], vertex_buffer, max(last_draw_dispatch_data.index_count, last_draw_dispatch_data.vertex_count));
         uint64_t transform_hash = hash_transform(vs_consts.mtxLocalToWorldViewProj);

         {
            std::unique_lock lock(game_device_data.transform_mutex);

            auto& stored_transforms = game_device_data.transform_lookup[draw_call_hash];
            std::vector<TransformCacheEntry>& current_transforms = stored_transforms.transforms[GetContextIndex(context_tag)];

            bool found = false;
            for (uint32_t i = 0; i < current_transforms.size(); ++i)
            {
               if (current_transforms[i].transform_hash == transform_hash)
               {
                  found = true;
                  break;
               }
            }
            if (!found)
            {
               current_transforms.push_back({transform_hash, vs_consts.mtxLocalToWorldViewProj, vs_consts.mtxLocalToWorld});
            }

            const std::vector<TransformCacheEntry>& previous_transforms = stored_transforms.transforms[GetPreviousContextIndex(GetContextIndex(context_tag))];
            if (previous_transforms.size() > 0)
            {
               uint64_t prev_transform_hash = hash_transform(vs_consts.mtxLocalToWorldViewProjPrev);

               const TransformCacheEntry* cache_data = nullptr;
               for (uint32_t i = 0; i < previous_transforms.size(); ++i)
               {
                  if (previous_transforms[i].transform_hash == prev_transform_hash)
                  {
                     cache_data = &previous_transforms[i];
                     break;
                  }
               }
               if (!cache_data)
               {
                  float shortest_distance = FLT_MAX;
                  float3 a = TransformPoint(vs_consts.mtxLocalToWorldViewProjPrev, float3(1.0f, 1.0f, 1.0f));
                  for (uint32_t i = 0; i < previous_transforms.size(); ++i)
                  {
                     float3 b = TransformPoint(previous_transforms[i].mtxLocalToWorldViewProj, float3(1.0f, 1.0f, 1.0f));
                     float dist = (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z);
                     if (dist < shortest_distance)
                     {
                        cache_data = &previous_transforms[i];
                        shortest_distance = dist;
                     }
                  }
               }

               vs_consts.mtxLocalToWorldViewProjPrev = cache_data->mtxLocalToWorldViewProj;

               if (is_outline_pass)
               {
                  D3D11_MAPPED_SUBRESOURCE mapped_cbuffer;
                  native_device_context->Map(game_device_data.cbuffer_prev_data.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_cbuffer);
                  GFD_VSCONST_OUTLINE_PREV_DATA* vs_outline_prev_data = (GFD_VSCONST_OUTLINE_PREV_DATA*)mapped_cbuffer.pData;
                  vs_outline_prev_data->mtxLocalToWorldPrev = cache_data->mtxLocalToWorld;
                  vs_outline_prev_data->mtxViewProjPrev = context_data.prev_view_proj;
                  vs_outline_prev_data->eyePositionPrev = context_data.prev_eye_pos;
                  vs_outline_prev_data->skinned_mesh = is_skinned_mesh ? 1 : 0;
                  native_device_context->Unmap(game_device_data.cbuffer_prev_data.get(), 0);
               }
            }
            else if (is_outline_pass)
            {
               D3D11_MAPPED_SUBRESOURCE mapped_cbuffer;
               native_device_context->Map(game_device_data.cbuffer_prev_data.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_cbuffer);
               GFD_VSCONST_OUTLINE_PREV_DATA* vs_outline_prev_data = (GFD_VSCONST_OUTLINE_PREV_DATA*)mapped_cbuffer.pData;
               vs_outline_prev_data->mtxLocalToWorldPrev = vs_consts.mtxLocalToWorld;
               vs_outline_prev_data->mtxViewProjPrev = context_data.prev_view_proj;
               vs_outline_prev_data->eyePositionPrev = context_data.prev_eye_pos;
               vs_outline_prev_data->skinned_mesh = is_skinned_mesh ? 1 : 0;
               native_device_context->Unmap(game_device_data.cbuffer_prev_data.get(), 0);
            }
         }
      }

      vs_consts.mtxLocalToWorldViewProj = context_data.proj_with_jitter * context_data.inv_proj * vs_consts.mtxLocalToWorldViewProj;
      vs_consts.mtxLocalToWorldViewProjPrev = context_data.prev_proj_with_current_jitter * context_data.prev_inv_proj * vs_consts.mtxLocalToWorldViewProjPrev;

      if (game_device_data.cb_transform)
      {
         native_device_context->UpdateSubresource(game_device_data.cb_transform, 0, nullptr, &vs_consts, 0, 0);
      }

      game_device_data.draw_contexts[GetContextIndex(context_tag)].vsconst_transform_data_changed = false;
   }

   static ID3D11PixelShader* GetMotionVectorPixelShader(uint32_t vertex_shader_hash, uint32_t pixel_shader_hash, ID3D11Device* native_device, GameDeviceDataMetaphorReFantazio& game_device_data)
   {
      game_device_data.pixel_shader_mutex.lock_shared();
      const auto pixel_shader_it = game_device_data.modified_pixel_shaders.find(pixel_shader_hash);
      if (pixel_shader_it == game_device_data.modified_pixel_shaders.cend())
      {
         game_device_data.pixel_shader_mutex.unlock_shared();
         // if another thread has added the shader in the meantime we just patch it twice
         std::unique_lock lock(game_device_data.pixel_shader_mutex);

         const auto coord_index_it = game_device_data.vertex_shader_ndc_coord_indices.find(vertex_shader_hash);
         if (coord_index_it == game_device_data.vertex_shader_ndc_coord_indices.cend())
         {
            return nullptr;
         }
         const auto shader_code_it = game_device_data.pixel_shader_code.find(pixel_shader_hash);
         if (shader_code_it == game_device_data.pixel_shader_code.cend())
         {
            return nullptr;
         }

         std::vector<std::byte> shader_code = shader_code_it->second;

         uint32_t coord_input_register = coord_index_it->second[0];
         uint32_t prev_coord_input_register = coord_index_it->second[1];

         PatchPixelShader(shader_code, coord_input_register, prev_coord_input_register);

         ID3D11PixelShader* shader;
         HRESULT hr = native_device->CreatePixelShader(shader_code.data(), shader_code.size(), nullptr, &shader);
         game_device_data.modified_pixel_shaders[pixel_shader_hash] = shader;

         return shader;
      }
      else
      {
         game_device_data.pixel_shader_mutex.unlock_shared();
         return pixel_shader_it->second.get();
      }
   }

   static void BindMotionVectorRenderTarget(ID3D11DeviceContext* native_device_context, DrawContextData& context_data)
   {
      ComPtr<ID3D11DepthStencilView> depth_stencil_view;
      ComPtr<ID3D11RenderTargetView> render_target_views[6];
      {
         ID3D11RenderTargetView* render_target_views_raw[6];
         native_device_context->OMGetRenderTargets(6, &render_target_views_raw[0], depth_stencil_view.put());
         for (uint32_t i = 0; i < 6; ++i)
         {
            render_target_views[i].attach(render_target_views_raw[i]);
         }
      }
      if (render_target_views[5] != context_data.motion_vectors_rtv)
      {
         ID3D11RenderTargetView* updated_render_target_views[] = {render_target_views[0].get(),
            render_target_views[1].get(),
            render_target_views[2].get(),
            render_target_views[3].get(),
            render_target_views[4].get(),
            context_data.motion_vectors_rtv.get()};
         native_device_context->OMSetRenderTargets(6, updated_render_target_views, depth_stencil_view.get());
      }
   }

   static void ResolveSceneUI(ID3D11DeviceContext* native_device_context, ContextTag tag, GameDeviceDataMetaphorReFantazio& game_device_data, DeviceData& device_data)
   {
      DrawContextData& context_data = game_device_data.draw_contexts[GetContextIndex(tag)];

      D3D11_TEXTURE2D_DESC render_target_desc = {};
      context_data.resolved_scene_ui_texture->GetDesc(&render_target_desc);
      native_device_context->ResolveSubresource(context_data.resolved_scene_ui_texture.get(), 0, context_data.scene_ui_texture.get(), 0, render_target_desc.Format);

      DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack;
      draw_state_stack.Cache(native_device_context, device_data.uav_max_count);

      native_device_context->OMSetBlendState(game_device_data.scene_ui_blend_state.get(), nullptr, 0xFFFFFFFF);
      native_device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
      native_device_context->OMSetDepthStencilState(nullptr, 0);
      native_device_context->OMSetRenderTargets(1, context_data.original_scene_texture_rtv.get_addressof(), nullptr);
      ID3D11VertexShader* vs = device_data.native_vertex_shaders[CompileTimeStringHash("Copy VS")].get();
      native_device_context->VSSetShader(vs, nullptr, 0);
      ID3D11PixelShader* ps = device_data.native_pixel_shaders[CompileTimeStringHash("Copy PS")].get();
      native_device_context->PSSetShader(ps, nullptr, 0);
      native_device_context->PSSetShaderResources(0, 1, context_data.resolved_scene_ui_texture_srv.get_addressof());
      native_device_context->IASetInputLayout(nullptr);
      native_device_context->RSSetState(nullptr);
      native_device_context->Draw(4, 0);

      draw_state_stack.Restore(native_device_context);

      ComPtr<ID3D11RenderTargetView> render_target_view;
      native_device_context->OMGetRenderTargets(1, render_target_view.put(), nullptr);
      if (render_target_view == context_data.scene_ui_texture_rtv)
      {
         native_device_context->OMSetRenderTargets(1, context_data.original_scene_texture_rtv.get_addressof(),
            context_data.original_scene_dsv.get());
      }

      ComPtr<ID3D11RasterizerState> rasterizer_state;
      native_device_context->RSGetState(rasterizer_state.put());
      if (rasterizer_state == game_device_data.scene_ui_rasterizer_state)
      {
         native_device_context->RSSetState(context_data.original_scene_raterizer_state.get());
      }
      ComPtr<ID3D11BlendState> blend_state;
      native_device_context->OMGetBlendState(blend_state.put(), nullptr, nullptr);
      if (blend_state == game_device_data.scene_ui_blend_state)
      {
         native_device_context->OMSetBlendState(context_data.original_scene_blend_state.get(), nullptr, 0xFFFFFFFF);
      }

      context_data.frame_progress.SetReached(FrameProgress::SceneUiDrawFinished);
   }

   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      ContextTag context_tag = GetContextTag(native_device_context);

      // cull shadow map draw calls
      if (GetContextType(context_tag) == ContextType::ShadowMap)
      {
#if DEVELOPMENT || TEST
         shadow_draw_calls++;
#endif
         if (!game_device_data.cb_shadow_transform)
         {
            ComPtr<ID3D11Buffer> transform_constant_buffer;
            native_device_context->VSGetConstantBuffers(1, 1, transform_constant_buffer.put());

            game_device_data.cb_shadow_transform = transform_constant_buffer.get();
         }
#if DEVELOPMENT
         else
         {
            ComPtr<ID3D11Buffer> transform_constant_buffer;
            native_device_context->VSGetConstantBuffers(1, 1, transform_constant_buffer.put());

            ASSERT_ONCE(game_device_data.cb_shadow_transform == transform_constant_buffer.get());
         }
#endif
         ShadowContextData& contex_data = game_device_data.shadow_contexts[GetContextIndex(context_tag)];

         if (contex_data.world_view_proj_valid)
         {
            ComPtr<ID3D11Buffer> vertex_buffer;
            uint32_t stride;
            native_device_context->IAGetVertexBuffers(0, 1, vertex_buffer.put(), &stride, nullptr);
            if ((stride == 28 || stride == 40))
            {
               const std::shared_lock shared_lock_bounding_boxes(game_device_data.bounding_box_mutex);
               auto it = game_device_data.bounding_boxes.find(vertex_buffer.get());

               if (it != game_device_data.bounding_boxes.cend())
               {
                  float4x4 worldViewProj = contex_data.world_view_proj;

                  if (IsOutsideFrustum(worldViewProj, stride == 28 ? it->second.box28 : it->second.box40))
                  {
#if DEVELOPMENT || TEST
                     shadow_draw_calls_culled++;
#endif
                     return DrawOrDispatchOverrideType::Skip;
                  }
               }
            }
         }
      }

      if (GetContextType(context_tag) != ContextType::Draw)
      {
         return DrawOrDispatchOverrideType::None;
      }
      DrawContextData& context_data = game_device_data.draw_contexts[GetContextIndex(context_tag)];

      if ((stages & reshade::api::shader_stage::compute) != 0 &&
          original_shader_hashes.compute_shaders[0] == 0xF2DB8A9B)
      {
         ComPtr<ID3D11ShaderResourceView> srv;
         native_device_context->CSGetShaderResources(0, 1, srv.put());

         if (context_data.frame_progress.Reached(FrameProgress::SceneUiDrawStarted))
         {
            ID3D11ShaderResourceView* null_srv = nullptr;
            native_device_context->CSSetShaderResources(0, 1, &null_srv);
            ResolveSceneUI(native_device_context, context_tag, game_device_data, device_data);
            native_device_context->CSSetShaderResources(0, 1, srv.get_addressof());
         }

         ComPtr<ID3D11UnorderedAccessView> uav;
         native_device_context->CSGetUnorderedAccessViews(0, 1, uav.put());

         ComPtr<ID3D11Resource> srv_resource;
         srv->GetResource(srv_resource.put());

         ComPtr<ID3D11Texture2D> srv_texture;
         srv_resource->QueryInterface(srv_texture.put());

         D3D11_TEXTURE2D_DESC srv_desc;
         srv_texture->GetDesc(&srv_desc);

         ComPtr<ID3D11Resource> uav_resource;
         uav->GetResource(uav_resource.put());

         ComPtr<ID3D11Texture2D> uav_texture;
         uav_resource->QueryInterface(uav_texture.put());

         D3D11_TEXTURE2D_DESC uav_desc;
         uav_texture->GetDesc(&uav_desc);

         if (SrActive(device_data) &&
             UseSRForUpscaling(device_data) &&
             (srv_desc.Width < uav_desc.Width && srv_desc.Height < uav_desc.Height))
         {
            context_data.source_color = srv_texture;
            context_data.dest_color = uav_texture;

            // split the command list since DLSS must be executed on an immediate context
            ComPtr<ID3D11CommandList> command_list;
            native_device_context->FinishCommandList(TRUE, command_list.put());
            context_data.partial_command_lists.push_back(command_list);

            return DrawOrDispatchOverrideType::Replaced;
         }

         return DrawOrDispatchOverrideType::None;
      }
      else if ((stages & reshade::api::shader_stage::vertex) == 0)
      {
         return DrawOrDispatchOverrideType::None;
      }

      DrawOrDispatchOverrideType overrideType = DrawOrDispatchOverrideType::None;
      if (!context_data.frame_progress.Reached(FrameProgress::FrameStarted))
      {
         context_data.ocean_lookup.clear();
         context_data.ocean_buffer->Reset();

         {
            std::unique_lock lock(game_device_data.transform_mutex);

            uint32_t content_idx = GetContextIndex(context_tag);
            for (auto& it : game_device_data.transform_lookup)
            {
               it.second.transforms[content_idx].clear();
            }
         }

         // Update TAA jitters:
         int phases = 8; // A good default
         if (device_data.sr_type != SR::Type::None)
         {
            auto* sr_instance_data = device_data.GetSRInstanceData();
            phases = sr_implementations[device_data.sr_type]->GetJitterPhases(sr_instance_data);
         }
         cb_luma_global_settings.GameSettings.FrameIndex++;
         device_data.cb_luma_global_settings_dirty = true;
         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::all, LumaConstantBufferType::LumaSettings);

         int temporal_frame = cb_luma_global_settings.GameSettings.FrameIndex % phases;
         context_data.projection_jitters.x = SR::HaltonSequence(temporal_frame, 2);
         context_data.projection_jitters.y = SR::HaltonSequence(temporal_frame, 3);

         context_data.frame_progress.SetReached(FrameProgress::FrameStarted);
      }

      if (!context_data.frame_progress.Reached(FrameProgress::OpaqueRenderingStarted))
      {
         ComPtr<ID3D11RenderTargetView> render_target_views[4];
         ComPtr<ID3D11DepthStencilView> depth_stencil_view;
         {
            ID3D11RenderTargetView* render_target_views_raw[4] = {};
            native_device_context->OMGetRenderTargets(4, &render_target_views_raw[0], depth_stencil_view.put());
            for (uint32_t i = 0; i < 4; ++i)
            {
               render_target_views[i].attach(render_target_views_raw[i]);
            }
         }

         if (!depth_stencil_view)
         {
            return DrawOrDispatchOverrideType::None;
         }

         if (render_target_views[0] &&
             render_target_views[1] &&
             render_target_views[2] &&
             render_target_views[3])
         {
            context_data.frame_progress.SetReached(FrameProgress::OpaqueRenderingStarted);

            if (SrActive(device_data))
            {
               {
                  ID3D11Buffer* cb = game_device_data.cbuffer_prev_data.get();
                  native_device_context->VSSetConstantBuffers(5, 1, &cb);
                  cb = game_device_data.cbuffer_skin_cache.get();
                  native_device_context->VSSetConstantBuffers(9, 1, &cb);
               }
               {
                  ID3D11ShaderResourceView* srv = game_device_data.draw_contexts[GetPreviousContextIndex(GetContextIndex(context_tag))].skin_buffer->srv.get();
                  native_device_context->VSSetShaderResources(1, 1, &srv);
               }

               ComPtr<ID3D11Resource> depthResource;
               depth_stencil_view->GetResource(depthResource.put());

               depthResource->QueryInterface(context_data.depth_texture.put());

               ComPtr<ID3D11Resource> render_target_resource;
               render_target_views[0]->GetResource(render_target_resource.put());

               ComPtr<ID3D11Texture2D> texture;
               render_target_resource->QueryInterface(texture.put());

               D3D11_TEXTURE2D_DESC target_desc = {};
               texture->GetDesc(&target_desc);

               ComPtr<ID3D11Device> device;
               native_device_context->GetDevice(device.put());
               SetupMotionVectorTexture(device.get(), context_data, target_desc.Width, target_desc.Height);

               ComPtr<ID3D11Buffer> transform_constant_buffer;
               native_device_context->VSGetConstantBuffers(1, 1, transform_constant_buffer.put());

               ComPtr<ID3D11Buffer> view_proj_constant_buffer;
               native_device_context->VSGetConstantBuffers(2, 1, view_proj_constant_buffer.put());

               ComPtr<ID3D11Buffer> ps_system_constant_buffer;
               native_device_context->PSGetConstantBuffers(0, 1, ps_system_constant_buffer.put());

               if (transform_constant_buffer && view_proj_constant_buffer && ps_system_constant_buffer)
               {
                  const DrawContextData& previous_context_data = game_device_data.draw_contexts[GetPreviousContextIndex(GetContextIndex(context_tag))];

                  context_data.prev_inv_proj = previous_context_data.inv_proj;
                  context_data.prev_proj_with_current_jitter = previous_context_data.proj;
                  context_data.prev_proj_with_current_jitter.m02 -= 2.0f * context_data.projection_jitters.x / (float)target_desc.Width;
                  context_data.prev_proj_with_current_jitter.m12 += 2.0f * context_data.projection_jitters.y / (float)target_desc.Height;
                  context_data.prev_view_proj = context_data.prev_proj_with_current_jitter * previous_context_data.view;

                  context_data.prev_eye_pos = previous_context_data.eye_pos;

                  D3D11_BUFFER_DESC ps_system_constant_buffer_desc = {};
                  ps_system_constant_buffer->GetDesc(&ps_system_constant_buffer_desc);

#ifdef DEVELOPMENT
                  ASSERT_ONCE(!game_device_data.cb_system || game_device_data.cb_system == ps_system_constant_buffer.get());
#endif
                  if (ps_system_constant_buffer_desc.ByteWidth == 288)
                  {
                     game_device_data.cb_system = ps_system_constant_buffer.get();
                     auto it = context_data.cbuffer_cache.find(ps_system_constant_buffer.get());
                     if (it != context_data.cbuffer_cache.cend())
                     {
                        const GFD_PSCONST_SYSTEM* ps_const_system = (GFD_PSCONST_SYSTEM*)it->second.data();
                        context_data.inv_proj = ps_const_system->mtxInvProj;
                        context_data.proj = ps_const_system->mtxProj;
                        context_data.proj_with_jitter = context_data.proj;

                        context_data.proj_with_jitter.m02 -= 2.0f * context_data.projection_jitters.x / (float)target_desc.Width;
                        context_data.proj_with_jitter.m12 += 2.0f * context_data.projection_jitters.y / (float)target_desc.Height;

                        context_data.view = ps_const_system->mtxView;
                     }
                  }

                  D3D11_BUFFER_DESC transform_constant_buffer_desc;
                  transform_constant_buffer->GetDesc(&transform_constant_buffer_desc);
                  if (transform_constant_buffer_desc.ByteWidth == 256)
                  {
                     game_device_data.cb_transform = transform_constant_buffer.get();
                     {
                        auto it = context_data.cbuffer_cache.find(transform_constant_buffer.get());
                        if (it != context_data.cbuffer_cache.cend())
                        {
                           HandleTransformUpdate(transform_constant_buffer.get(), it->second.data(), context_data);
                        }
                     }
                  }

                  D3D11_BUFFER_DESC view_proj_constant_buffer_desc = {};
                  view_proj_constant_buffer->GetDesc(&view_proj_constant_buffer_desc);

#ifdef DEVELOPMENT
                  ASSERT_ONCE(!game_device_data.cb_viewproj || game_device_data.cb_viewproj == view_proj_constant_buffer.get());
#endif
                  if (view_proj_constant_buffer_desc.ByteWidth == 208)
                  {
                     game_device_data.cb_viewproj = view_proj_constant_buffer.get();
                     auto it = context_data.cbuffer_cache.find(view_proj_constant_buffer.get());
                     if (it != context_data.cbuffer_cache.cend())
                     {
                        GFD_VSCONST_VIEWPROJ vs_const_viewproj = *((GFD_VSCONST_VIEWPROJ*)it->second.data());
                        context_data.eye_pos = vs_const_viewproj.eyePosition;
                        context_data.fov = vs_const_viewproj.fovy;

                        vs_const_viewproj.mtxViewProj = context_data.proj_with_jitter * context_data.view;
                        native_device_context->UpdateSubresource(view_proj_constant_buffer.get(), 0, nullptr, &vs_const_viewproj, 0, 0);
                     }
                  }
               }

               if (context_data.motion_vectors_rtv)
               {
                  float clear_value[] = {0.0f, 0.0f, 0.0f, 0.0f};
                  native_device_context->ClearRenderTargetView(context_data.motion_vectors_rtv.get(), clear_value);
               }
            }
            // dithered objects look nicer but makes scene UI elements look too busy
            // if (SrActive(device_data))
            //{
            //   auto* sr_instance_data = device_data.GetSRInstanceData();
            //   int phases = sr_implementations[device_data.sr_type]->GetJitterPhases(sr_instance_data);
            //   int index = cb_luma_global_settings.FrameIndex % min(game_device_data.bayer_matrix_texture_srvs.size(), phases);
            //   native_device_context->PSSetShaderResources(15, 1, game_device_data.bayer_matrix_texture_srvs[index].get_addressof());
            //}
         }
         else if (depth_stencil_view)
         {
            // apply jitter to depth pre-pass
            ComPtr<ID3D11Buffer> transform_constant_buffer;
            native_device_context->VSGetConstantBuffers(1, 1, transform_constant_buffer.put());

            ComPtr<ID3D11Buffer> ps_system_constant_buffer;
            native_device_context->PSGetConstantBuffers(0, 1, ps_system_constant_buffer.put());

            ComPtr<ID3D11Resource> depth_stencil_resource;
            depth_stencil_view->GetResource(depth_stencil_resource.put());

            ComPtr<ID3D11Texture2D> texture;
            depth_stencil_resource->QueryInterface(texture.put());

            D3D11_TEXTURE2D_DESC target_desc = {};
            texture->GetDesc(&target_desc);

            if (transform_constant_buffer && ps_system_constant_buffer)
            {
               float4x4 inv_proj;
               float4x4 proj;
               float4x4 proj_with_jitter;

               D3D11_BUFFER_DESC ps_system_constant_buffer_desc = {};
               ps_system_constant_buffer->GetDesc(&ps_system_constant_buffer_desc);
               if (ps_system_constant_buffer_desc.ByteWidth == 288)
               {
                  auto it = context_data.cbuffer_cache.find(ps_system_constant_buffer.get());
                  if (it != context_data.cbuffer_cache.cend())
                  {
                     const GFD_PSCONST_SYSTEM* ps_const_system = (GFD_PSCONST_SYSTEM*)it->second.data();
                     inv_proj = ps_const_system->mtxInvProj;
                     proj = ps_const_system->mtxProj;
                     proj_with_jitter = proj;

                     proj_with_jitter.m02 -= 2.0f * context_data.projection_jitters.x / (float)target_desc.Width;
                     proj_with_jitter.m12 += 2.0f * context_data.projection_jitters.y / (float)target_desc.Height;
                  }
                  else
                  {
                     return DrawOrDispatchOverrideType::None;
                  }
               }
               else
               {
                  return DrawOrDispatchOverrideType::None;
               }

               if (!game_device_data.cb_transform)
               {
                  D3D11_BUFFER_DESC transform_constant_buffer_desc = {};
                  transform_constant_buffer->GetDesc(&transform_constant_buffer_desc);
                  if (transform_constant_buffer_desc.ByteWidth == 256)
                  {
                     {
                        auto it = context_data.cbuffer_cache.find(transform_constant_buffer.get());
                        if (it != context_data.cbuffer_cache.cend())
                        {
                           // not used for motion vectors so no point in updating mtxLocalToWorldViewProjPrev
                           GFD_VSCONST_TRANSFORM vs_consts = *(GFD_VSCONST_TRANSFORM*)it->second.data();
                           vs_consts.mtxLocalToWorldViewProj = proj_with_jitter * inv_proj * vs_consts.mtxLocalToWorldViewProj;

                           if (transform_constant_buffer)
                           {
                              native_device_context->UpdateSubresource(transform_constant_buffer.get(), 0, nullptr, &vs_consts, 0, 0);
                           }
                        }
                     }
                  }
               }
               else
               {
                  GFD_VSCONST_TRANSFORM vs_consts = context_data.vsconst_transform_data;
                  vs_consts.mtxLocalToWorldViewProj = proj_with_jitter * inv_proj * vs_consts.mtxLocalToWorldViewProj;

                  if (game_device_data.cb_transform)
                  {
                     native_device_context->UpdateSubresource(game_device_data.cb_transform, 0, nullptr, &vs_consts, 0, 0);
                  }
               }
            }

            return DrawOrDispatchOverrideType::None;
         }
         else
         {
            return DrawOrDispatchOverrideType::None;
         }
      }

      if (original_shader_hashes.pixel_shaders.size() > 0 &&
          original_shader_hashes.pixel_shaders.front() == 0x1A75C9AE) // AO
      {
         native_device_context->PSSetShaderResources(2, 1, game_device_data.noise_texture_srv.get_addressof());
      }
      else if (original_shader_hashes.pixel_shaders.size() > 0 &&
               original_shader_hashes.pixel_shaders.front() == 0x2054ae6a) // 13-sample blur
      {
         ComPtr<ID3D11RenderTargetView> render_target_view;
         native_device_context->OMGetRenderTargets(1, render_target_view.put(), nullptr);

         ComPtr<ID3D11Resource> render_target_resource;
         render_target_view->GetResource(render_target_resource.put());
         ComPtr<ID3D11Texture2D> render_target_texture;
         render_target_resource->QueryInterface(render_target_texture.put());

         D3D11_TEXTURE2D_DESC render_target_desc;
         render_target_texture->GetDesc(&render_target_desc);

         bool needs_recreate = false;
         if (context_data.bloom_texture)
         {
            D3D11_TEXTURE2D_DESC bloom_texture_desc;
            context_data.bloom_texture->GetDesc(&bloom_texture_desc);
            if (bloom_texture_desc.Width != render_target_desc.Width ||
                bloom_texture_desc.Height != render_target_desc.Height)
            {
               needs_recreate = true;
            }
         }

         if (!context_data.bloom_texture ||
             needs_recreate)
         {
            D3D11_TEXTURE2D_DESC bloom_desc = render_target_desc;

            native_device->CreateTexture2D(&bloom_desc,
               nullptr,
               context_data.bloom_texture.put());
            native_device->CreateShaderResourceView(context_data.bloom_texture.get(),
               nullptr,
               context_data.bloom_texture_srv.put());
            native_device->CreateRenderTargetView(context_data.bloom_texture.get(),
               nullptr,
               context_data.bloom_texture_rtv.put());
         }

         ID3D11PixelShader* ps = device_data.native_pixel_shaders[CompileTimeStringHash("Gaussian Blur Horizontal")].get();
         native_device_context->PSSetShader(ps, nullptr, 0);
         native_device_context->OMSetRenderTargets(1, context_data.bloom_texture_rtv.get_addressof(), nullptr);
         native_device_context->Draw(4, 0);

         ps = device_data.native_pixel_shaders[CompileTimeStringHash("Gaussian Blur Vertical")].get();
         native_device_context->PSSetShader(ps, nullptr, 0);
         native_device_context->OMSetRenderTargets(1, render_target_view.get_addressof(), nullptr);
         native_device_context->PSSetShaderResources(0, 1, context_data.bloom_texture_srv.get_addressof());
         native_device_context->Draw(4, 0);

         overrideType = DrawOrDispatchOverrideType::Replaced;
      }
      else if (SrActive(device_data) &&
          original_shader_hashes.pixel_shaders.size() > 0 &&
          (original_shader_hashes.pixel_shaders.front() == 0xA7108284 || // tonemap background
             original_shader_hashes.pixel_shaders.front() == 0xC1787BC6))
      {
         CommitSkinCache(native_device_context, context_data);

         native_device_context->Draw(4, 0);

         ComPtr<ID3D11RenderTargetView> render_target_view;
         native_device_context->OMGetRenderTargets(1, render_target_view.put(), nullptr);

         ComPtr<ID3D11Resource> color_resource;
         render_target_view->GetResource(color_resource.put());
         color_resource->QueryInterface(context_data.source_color.put());
         context_data.dest_color = context_data.source_color;

         // split the command list since DLSS must be executed on an immediate context
         ComPtr<ID3D11CommandList> command_list;
         native_device_context->FinishCommandList(TRUE, command_list.put());
         context_data.partial_command_lists.push_back(command_list);

         context_data.frame_progress.SetReached(FrameProgress::BackgroundTonemapped);
         device_data.has_drawn_main_post_processing = true;
         overrideType = DrawOrDispatchOverrideType::Replaced;
      }
      else if (original_shader_hashes.pixel_shaders.size() > 0 &&
               (original_shader_hashes.pixel_shaders.front() == 0xD8196629)) // apply lut
      {
         context_data.frame_progress.SetReached(FrameProgress::LutApplied);

         // if there's no bloom or particle we use this as a second chance to inject super resolution
         if (SrActive(device_data) &&
             !context_data.frame_progress.Reached(FrameProgress::AddedParticles) &&
             !context_data.frame_progress.Reached(FrameProgress::LutApplied) &&
             context_data.depth_texture)
         {
            CommitSkinCache(native_device_context, context_data);

            ComPtr<ID3D11ShaderResourceView> srv;
            native_device_context->PSGetShaderResources(0, 1, srv.put());

            ComPtr<ID3D11Resource> color_resource;
            srv->GetResource(color_resource.put());
            color_resource->QueryInterface(context_data.source_color.put());
            context_data.dest_color = context_data.source_color;

            // split the command list since DLSS must be executed on an immediate context
            ComPtr<ID3D11CommandList> command_list;
            native_device_context->FinishCommandList(TRUE, command_list.put());
            context_data.partial_command_lists.push_back(command_list);
         }
         ID3D11SamplerState* sampler = device_data.sampler_state_linear.get();
         native_device_context->PSSetSamplers(0, 1, &sampler);
      }
      else if (!context_data.frame_progress.Reached(FrameProgress::AddedParticles) &&
               original_shader_hashes.pixel_shaders.size() > 0 &&
               (original_shader_hashes.pixel_shaders.front() == 0xAC103037)) // add particles
      {
         context_data.frame_progress.SetReached(FrameProgress::AddedParticles);

         // only apply sr when we have the necessary input resources
         if (SrActive(device_data) &&
             context_data.depth_texture)
         {
            CommitSkinCache(native_device_context, context_data);

            native_device_context->Draw(4, 0);

            ComPtr<ID3D11RenderTargetView> render_target_view;
            native_device_context->OMGetRenderTargets(1, render_target_view.put(), nullptr);

            ComPtr<ID3D11Resource> color_resource;
            render_target_view->GetResource(color_resource.put());
            color_resource->QueryInterface(context_data.source_color.put());
            context_data.dest_color = context_data.source_color;

            ComPtr<ID3D11ShaderResourceView> particle_srv;
            native_device_context->PSGetShaderResources(2, 1, particle_srv.put());

            ComPtr<ID3D11Resource> particle_resource;
            particle_srv->GetResource(particle_resource.put());

            particle_resource->QueryInterface(context_data.particle_texture.put());

            // split the command list since DLSS must be executed on an immediate context
            ComPtr<ID3D11CommandList> command_list;
            native_device_context->FinishCommandList(TRUE, command_list.put());
            context_data.partial_command_lists.push_back(command_list);

            overrideType = DrawOrDispatchOverrideType::Replaced;
         }
      }
      else if (context_data.frame_progress.Reached(FrameProgress::OpaqueRenderingStarted) &&
          !context_data.frame_progress.Reached(FrameProgress::BackgroundTonemapped) &&
          !context_data.frame_progress.Reached(FrameProgress::LutApplied) &&
          !context_data.frame_progress.Reached(FrameProgress::AddedParticles))
      {
         if (!SrActive(device_data) ||
             original_shader_hashes.vertex_shaders.empty() ||
             original_shader_hashes.pixel_shaders.empty())
         {
            return DrawOrDispatchOverrideType::None;
         }

         ComPtr<ID3D11Buffer> vertex_buffer;
         uint32_t stride;
         native_device_context->IAGetVertexBuffers(0, 1, vertex_buffer.put(), &stride, nullptr);

         D3D11_BUFFER_DESC bd = {};
         vertex_buffer->GetDesc(&bd);
         bool is_skinned_mesh = ((bd.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0);
         bool is_outline_pass = shader_hashes_outline.Contains(original_shader_hashes);

         if (is_skinned_mesh)
         {
            if (context_data.pending_skin_cache.find(vertex_buffer.get()) == context_data.pending_skin_cache.cend())
            {
               // only queue vertex buffer copies here and execute them in a single batch in CommitSkinCache
               // inter-mixing draw and copy too much negatively affects performance in scenes with lots of skinned objects
               // e.g. on some parts of Virga Island I saw a difference of up to 2ms on a RTX 4080
               SkinCacheItem cache_item = {};
               cache_item.buffer = vertex_buffer;
               cache_item.size = bd.ByteWidth;
               cache_item.stride = stride;
               context_data.pending_skin_cache[vertex_buffer.get()] = cache_item;
            }
         }

         auto restoreOriginalShader = [&game_device_data, is_outline_pass, native_device_context, &original_shader_hashes]()
         {
            if (!is_outline_pass)
            {
               auto shader_it = game_device_data.original_vertex_shaders.find(original_shader_hashes.vertex_shaders[0]);
               if (shader_it != game_device_data.original_vertex_shaders.cend())
               {
                  native_device_context->VSSetShader(shader_it->second.get(), nullptr, 0);
               }
            }
         };

         if (original_shader_hashes.Contains(shader_hashes_ocean))
         {
            const DrawContextData& previous_context_data = game_device_data.draw_contexts[GetPreviousContextIndex(GetContextIndex(context_tag))];
            GFD_VSCONST_TRANSFORM vs_consts = context_data.vsconst_transform_data;

            if (previous_context_data.ocean_lookup.size())
            {
               const OceanCacheEntry* cache_data = nullptr;
               float shortest_distance = FLT_MAX;
               float3 a = TransformPoint(vs_consts.mtxLocalToWorld, float3(1.0f, 1.0f, 1.0f));
               for (uint32_t i = 0; i < previous_context_data.ocean_lookup.size(); ++i)
               {
                  float3 b = TransformPoint(previous_context_data.ocean_lookup[i].mtxLocalToWorld, float3(1.0f, 1.0f, 1.0f));
                  float dist = (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z);
                  if (dist < shortest_distance)
                  {
                     cache_data = &previous_context_data.ocean_lookup[i];
                     shortest_distance = dist;
                  }
               }

               D3D11_MAPPED_SUBRESOURCE mapped_cbuffer;
               native_device_context->Map(game_device_data.cbuffer_prepare_ocean_data.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_cbuffer);
               CB_PREPARE_OCEAN* cb_prepare_ocean_data = (CB_PREPARE_OCEAN*)mapped_cbuffer.pData;
               cb_prepare_ocean_data->mtxLocalToWorldPrev = cache_data->mtxLocalToWorld;
               cb_prepare_ocean_data->mtxViewProjPrev = context_data.prev_view_proj;
               cb_prepare_ocean_data->useCurrentTexShift = false;
               cb_prepare_ocean_data->TexShiftOffset = cache_data->TexShiftOffset;
               native_device_context->Unmap(game_device_data.cbuffer_prepare_ocean_data.get(), 0);
            }
            else
            {
               D3D11_MAPPED_SUBRESOURCE mapped_cbuffer;
               native_device_context->Map(game_device_data.cbuffer_prepare_ocean_data.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_cbuffer);
               CB_PREPARE_OCEAN* cb_prepare_ocean_data = (CB_PREPARE_OCEAN*)mapped_cbuffer.pData;
               cb_prepare_ocean_data->mtxLocalToWorldPrev = vs_consts.mtxLocalToWorld;
               cb_prepare_ocean_data->mtxViewProjPrev = context_data.prev_view_proj;
               cb_prepare_ocean_data->useCurrentTexShift = true;
               native_device_context->Unmap(game_device_data.cbuffer_prepare_ocean_data.get(), 0);
            }

            ComPtr<ID3D11Buffer> ocean_constant_buffer;
            native_device_context->VSGetConstantBuffers(7, 1, ocean_constant_buffer.put());

            {
               ID3D11Buffer* cbs[] = {game_device_data.cbuffer_prepare_ocean_data.get(), ocean_constant_buffer.get()};
               ID3D11ShaderResourceView* srvs[] = {previous_context_data.ocean_buffer->srv.get()};
               ID3D11UnorderedAccessView* uavs[] = {game_device_data.scratch_constant_buffer_uav.get()};

               native_device_context->CSSetShader(device_data.native_compute_shaders[CompileTimeStringHash("Prepare Ocean Data")].get(), 0, 0);
               native_device_context->CSSetConstantBuffers(0, 2, cbs);
               native_device_context->CSSetShaderResources(0, 1, srvs);
               native_device_context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
               native_device_context->Dispatch(1, 1, 1);
            }
            native_device_context->CopySubresourceRegion(game_device_data.cbuffer_prev_data.get(), 0, 0, 0, 0, game_device_data.scratch_constant_buffer.get(), 0, nullptr);

            BindMotionVectorRenderTarget(native_device_context, context_data);

            vs_consts.mtxLocalToWorldViewProj = context_data.proj_with_jitter * context_data.inv_proj * vs_consts.mtxLocalToWorldViewProj;
            vs_consts.mtxLocalToWorldViewProjPrev = context_data.prev_proj_with_current_jitter * context_data.prev_inv_proj * vs_consts.mtxLocalToWorldViewProjPrev;

            if (game_device_data.cb_transform)
            {
               native_device_context->UpdateSubresource(game_device_data.cb_transform, 0, nullptr, &vs_consts, 0, 0);
            }

            {
               bool addToCache = true;
               for (uint32_t i = 0; i < context_data.ocean_lookup.size(); ++i)
               {
                  if (memcmp(&context_data.ocean_lookup[i].mtxLocalToWorld, &vs_consts.mtxLocalToWorld, sizeof(vs_consts.mtxLocalToWorld)) == 0)
                  {
                     addToCache = false;
                     break;
                  }
               }

               if (addToCache)
               {
                  OceanCacheEntry cache_entry = {};
                  cache_entry.mtxLocalToWorld = vs_consts.mtxLocalToWorld;
                  cache_entry.TexShiftOffset = context_data.ocean_buffer->size;

                  context_data.ocean_buffer->CopyFromBuffer(native_device_context, ocean_constant_buffer.get(), 64, 16);

                  context_data.ocean_lookup.push_back(cache_entry);
               }
            }

            return DrawOrDispatchOverrideType::None;
         }

         bool previous_skin_set = false;
         if (is_skinned_mesh)
         {
            const DrawContextData& previous_context_data = game_device_data.draw_contexts[GetPreviousContextIndex(GetContextIndex(context_tag))];
            auto cache_it = previous_context_data.skin_lookup.find(vertex_buffer.get());

            if (cache_it != previous_context_data.skin_lookup.cend())
            {
               D3D11_MAPPED_SUBRESOURCE mapped_cbuffer;
               native_device_context->Map(game_device_data.cbuffer_skin_cache.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_cbuffer);
               GFD_VSCONST_SKIN_CACHE* vs_consts_skin = (GFD_VSCONST_SKIN_CACHE*)mapped_cbuffer.pData;
               vs_consts_skin->offset = cache_it->second.offset;
               vs_consts_skin->stride = cache_it->second.stride;
               native_device_context->Unmap(game_device_data.cbuffer_skin_cache.get(), 0);

               previous_skin_set = true;

               // outline shaders are manually overriden
               if (!is_outline_pass)
               {
                  auto shader_it = game_device_data.modified_vertex_shaders.find(original_shader_hashes.vertex_shaders[0]);
                  if (shader_it != game_device_data.modified_vertex_shaders.cend())
                  {
                     ComPtr<ID3D11VertexShader> vertex_shader;
                     native_device_context->VSGetShader(vertex_shader.put(), nullptr, nullptr);

                     if (vertex_shader != shader_it->second.get())
                     {
                        game_device_data.original_vertex_shaders[original_shader_hashes.vertex_shaders[0]] = vertex_shader;
                        native_device_context->VSSetShader(shader_it->second.get(), nullptr, 0);
                     }
                  }
                  else
                  {
                     restoreOriginalShader();
                  }
               }
            }
            else
            {
               restoreOriginalShader();
            }
         }
         else
         {
            restoreOriginalShader();
         }

         // only culls < 100 draw calls a frame, not worth the effort
         //         if (!is_outline_pass && (stride == 28 || stride == 40))
         //         {
         //            const std::shared_lock shared_lock_bounding_boxes(game_device_data.bounding_box_mutex);
         //            auto it = game_device_data.bounding_boxes.find(vertex_buffer.get());
         //
         //            if (it != game_device_data.bounding_boxes.cend())
         //            {
         //               float4x4 worldViewProj = game_device_data.vsconst_transform_data.mtxLocalToWorldViewProj;
         //
         //               if (IsOutsideFrustum(worldViewProj, stride == 28 ? it->second.box28 : it->second.box40))
         //               {
         // #if DEVELOPMENT || TEST
         //                  draw_calls_culled++;
         // #endif
         //                  return DrawOrDispatchOverrideType::Skip;
         //               }
         //            }
         //         }

         if (context_data.vsconst_transform_data_changed ||
             is_outline_pass)
         {
            UpdatePreviousTransformAndCache((stages & reshade::api::shader_stage::pixel) != 0, is_outline_pass, previous_skin_set, vertex_buffer.get(), native_device_context,
               context_tag, game_device_data, original_shader_hashes);
         }

         ID3D11PixelShader* shader = GetMotionVectorPixelShader(original_shader_hashes.vertex_shaders.front(), original_shader_hashes.pixel_shaders.front(), native_device, game_device_data);
         if (!shader)
         {
            return DrawOrDispatchOverrideType::None;
         }
         native_device_context->PSSetShader(shader, nullptr, 0);

         BindMotionVectorRenderTarget(native_device_context, context_data);
      }
      else if (SrActive(device_data) &&
               original_shader_hashes.Contains(shader_hashes_fxaa))
      {
         ComPtr<ID3D11ShaderResourceView> srv;
         native_device_context->PSGetShaderResources(0, 1, srv.put());
         ComPtr<ID3D11RenderTargetView> rtv;
         native_device_context->OMGetRenderTargets(1, rtv.put(), nullptr);

         ComPtr<ID3D11Resource> srv_resource;
         srv->GetResource(srv_resource.put());

         ComPtr<ID3D11Resource> rtv_resource;
         rtv->GetResource(rtv_resource.put());

         native_device_context->CopySubresourceRegion(rtv_resource.get(), 0, 0, 0, 0, srv_resource.get(), 0, nullptr);

         overrideType = DrawOrDispatchOverrideType::Skip;
      }
      else if (SrActive(device_data) &&
               original_shader_hashes.Contains(shader_hashes_smaa_blending))
      {
         ComPtr<ID3D11BlendState> blend_state;
         native_device_context->OMGetBlendState(blend_state.put(), nullptr, nullptr);
         D3D11_BLEND_DESC blend_desc = {};
         blend_state->GetDesc(&blend_desc);

         // menus that overlay 3D models onto the scene always apply SMAA
         // so we don't skip when alpha blending is enabled
         // that's also why we don't skip SMAA edge detection and weight calculation
         if (!blend_desc.RenderTarget[0].BlendEnable ||
             blend_desc.RenderTarget[0].SrcBlend != D3D11_BLEND_SRC_ALPHA)
         {
            ComPtr<ID3D11ShaderResourceView> srv;
            native_device_context->PSGetShaderResources(0, 1, srv.put());
            ComPtr<ID3D11RenderTargetView> rtv;
            native_device_context->OMGetRenderTargets(1, rtv.put(), nullptr);

            ComPtr<ID3D11Resource> srv_resource;
            srv->GetResource(srv_resource.put());

            ComPtr<ID3D11Resource> rtv_resource;
            rtv->GetResource(rtv_resource.put());

            native_device_context->CopySubresourceRegion(rtv_resource.get(), 0, 0, 0, 0, srv_resource.get(), 0, nullptr);

            overrideType = DrawOrDispatchOverrideType::Skip;
         }
      }
      else if (original_shader_hashes.Contains(shader_hashes_dof_prepare) &&
               SrActive(device_data) &&
               !game_device_data.upscaling)
      {
         std::lock_guard<std::mutex> lock(game_device_data.temporal_depth_pass_mutex);
         if (!context_data.frame_progress.Reached(FrameProgress::DepthTemporalAADrawn))
         {
            ComPtr<ID3D11ShaderResourceView> depth_srv;
            native_device_context->PSGetShaderResources(1, 1, depth_srv.put());
            ComPtr<ID3D11Resource> depth_resource;
            depth_srv->GetResource(depth_resource.put());
            ComPtr<ID3D11Texture2D> depth_texture;
            depth_resource->QueryInterface(depth_texture.put());

            D3D11_TEXTURE2D_DESC depth_desc;
            depth_texture->GetDesc(&depth_desc);

            TemporalAADepth::DrawData draw_data;
            draw_data.width = depth_desc.Width;
            draw_data.height = depth_desc.Height;
            draw_data.input_depth_srv = depth_srv.get();
            draw_data.input_mv_srv = context_data.motion_vectors_srv.get();
            draw_data.use_variance_clip = true;
            draw_data.variance_scale = 1.0f;
            draw_data.velocity_scale.x = -1.0f;
            draw_data.velocity_scale.y = -1.0f;
            draw_data.has_history = !device_data.force_reset_sr;

            ComPtr<ID3D11Device> native_device;
            native_device_context->GetDevice(native_device.put());

            DrawStateStack<DrawStateStackType::Compute> draw_state_stack;
            draw_state_stack.Cache(native_device_context, device_data.uav_max_count);
            game_device_data.temporal_depth_pass.Draw(native_device.get(), native_device_context, device_data, draw_data);
            draw_state_stack.Restore(native_device_context);

            context_data.frame_progress.SetReached(FrameProgress::DepthTemporalAADrawn);
         }
         ID3D11ShaderResourceView* temporal_depth_srv = game_device_data.temporal_depth_pass.resources[TemporalAADepth::Texture::DepthHistoryRead].srv.get();
         native_device_context->PSSetShaderResources(1, 1, &temporal_depth_srv);
      }
      else if (context_data.frame_progress.Reached(FrameProgress::AddedParticles) &&
               !context_data.frame_progress.Reached(FrameProgress::SceneUiDrawFinished) &&
               !game_device_data.upscaling &&
               g_scene_ui_msaa_samples > 1)
      {
         ComPtr<ID3D11DepthStencilState> depth_stencil_state;
         native_device_context->OMGetDepthStencilState(depth_stencil_state.put(), nullptr);

         ComPtr<ID3D11BlendState> blend_state;
         native_device_context->OMGetBlendState(blend_state.put(), nullptr, nullptr);
         if (depth_stencil_state && blend_state)
         {
            D3D11_DEPTH_STENCIL_DESC dsd = {};
            depth_stencil_state->GetDesc(&dsd);

            D3D11_BLEND_DESC bd = {};
            blend_state->GetDesc(&bd);
            if (dsd.DepthEnable && bd.RenderTarget[0].BlendOp != D3D11_BLEND_OP_REV_SUBTRACT && !context_data.frame_progress.Reached(FrameProgress::SceneUiDrawStarted) &&
                !shader_hashes_material.Contains(original_shader_hashes))
            {
               native_device_context->OMGetRenderTargets(1, context_data.original_scene_texture_rtv.put(), context_data.original_scene_dsv.put());

               ComPtr<ID3D11Resource> render_target_resource;
               context_data.original_scene_texture_rtv->GetResource(render_target_resource.put());

               ComPtr<ID3D11Texture2D> render_target_texture;
               render_target_resource->QueryInterface(render_target_texture.put());

               D3D11_TEXTURE2D_DESC render_target_desc = {};
               render_target_texture->GetDesc(&render_target_desc);

               ComPtr<ID3D11Resource> depth_stencil_resource;
               context_data.original_scene_dsv->GetResource(depth_stencil_resource.put());

               ComPtr<ID3D11Texture2D> depth_stencil_texture;
               depth_stencil_resource->QueryInterface(depth_stencil_texture.put());

               D3D11_TEXTURE2D_DESC depth_stencil_desc = {};
               depth_stencil_texture->GetDesc(&depth_stencil_desc);

               if (!context_data.scene_ui_texture ||
                   !context_data.scene_ui_depth_texture ||
                   context_data.scene_ui_resource_width != render_target_desc.Width ||
                   context_data.scene_ui_resource_height != render_target_desc.Height ||
                   context_data.scene_ui_resource_msaa_samples != g_scene_ui_msaa_samples)
               {
                  native_device->CreateTexture2D(&render_target_desc, nullptr, context_data.resolved_scene_ui_texture.put());

                  {
                     D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
                     srv_desc.Format = render_target_desc.Format;
                     srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                     srv_desc.Texture2D.MipLevels = 0;
                     srv_desc.Texture2D.MipLevels = 1;

                     native_device->CreateShaderResourceView(context_data.resolved_scene_ui_texture.get(),
                        &srv_desc,
                        context_data.resolved_scene_ui_texture_srv.put());
                  }
                  {
                     render_target_desc.SampleDesc.Count = g_scene_ui_msaa_samples;

                     native_device->CreateTexture2D(&render_target_desc, nullptr, context_data.scene_ui_texture.put());
                  }
                  {
                     D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
                     rtv_desc.Format = render_target_desc.Format;
                     rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;

                     native_device->CreateRenderTargetView(context_data.scene_ui_texture.get(),
                        &rtv_desc,
                        context_data.scene_ui_texture_rtv.put());
                  }
                  {
                     depth_stencil_desc.Format = DXGI_FORMAT_D32_FLOAT;
                     depth_stencil_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
                     depth_stencil_desc.SampleDesc.Count = g_scene_ui_msaa_samples;

                     native_device->CreateTexture2D(&depth_stencil_desc, nullptr, context_data.scene_ui_depth_texture.put());
                  }
                  {
                     D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
                     dsv_desc.Format = depth_stencil_desc.Format;
                     dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS;

                     native_device->CreateDepthStencilView(context_data.scene_ui_depth_texture.get(),
                        &dsv_desc,
                        context_data.scene_ui_depth_texture_dsv.put());
                  }

                  context_data.scene_ui_resource_width = render_target_desc.Width;
                  context_data.scene_ui_resource_height = render_target_desc.Height;
                  context_data.scene_ui_resource_msaa_samples = g_scene_ui_msaa_samples;
               }

               ComPtr<ID3D11ShaderResourceView> depth_stencil_resource_view;
               {
                  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
                  srv_desc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
                  srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                  srv_desc.Texture2D.MostDetailedMip = 0;
                  srv_desc.Texture2D.MipLevels = 1;
                  native_device->CreateShaderResourceView(depth_stencil_texture.get(),
                     &srv_desc,
                     depth_stencil_resource_view.put());
               }
               {
                  constexpr FLOAT clear_color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                  native_device_context->ClearRenderTargetView(context_data.scene_ui_texture_rtv.get(), clear_color);
               }
               native_device_context->ClearDepthStencilView(context_data.scene_ui_depth_texture_dsv.get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

               native_device_context->OMSetRenderTargets(1, context_data.scene_ui_texture_rtv.get_addressof(), context_data.scene_ui_depth_texture_dsv.get());

               ComPtr<ID3D11ShaderResourceView> depth_source = depth_stencil_resource_view.get();
               if (SrActive(device_data))
               {
                  std::lock_guard<std::mutex> lock(game_device_data.temporal_depth_pass_mutex);
                  if (!context_data.frame_progress.Reached(FrameProgress::DepthTemporalAADrawn))
                  {
                     TemporalAADepth::DrawData draw_data;
                     draw_data.width = render_target_desc.Width;
                     draw_data.height = render_target_desc.Height;
                     draw_data.input_depth_srv = depth_stencil_resource_view.get();
                     draw_data.input_mv_srv = context_data.motion_vectors_srv.get();
                     draw_data.use_variance_clip = true;
                     draw_data.variance_scale = 1.0f;
                     draw_data.velocity_scale.x = -1.0f;
                     draw_data.velocity_scale.y = -1.0f;
                     draw_data.has_history = !device_data.force_reset_sr;

                     ComPtr<ID3D11Device> native_device;
                     native_device_context->GetDevice(native_device.put());

                     DrawStateStack<DrawStateStackType::Compute> draw_state_stack;
                     draw_state_stack.Cache(native_device_context, device_data.uav_max_count);
                     game_device_data.temporal_depth_pass.Draw(native_device.get(), native_device_context, device_data, draw_data);
                     draw_state_stack.Restore(native_device_context);

                     context_data.frame_progress.SetReached(FrameProgress::DepthTemporalAADrawn);
                  }

                  depth_source = game_device_data.temporal_depth_pass.resources[TemporalAADepth::Texture::DepthHistoryRead].srv.get();
               }

               DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack;
               draw_state_stack.Cache(native_device_context, device_data.uav_max_count);

               native_device_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
               native_device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
               native_device_context->OMSetDepthStencilState(nullptr, 0);
               native_device_context->OMSetRenderTargets(0, nullptr, context_data.scene_ui_depth_texture_dsv.get());
               ID3D11VertexShader* vs = device_data.native_vertex_shaders[CompileTimeStringHash("Copy VS")].get();
               native_device_context->VSSetShader(vs, nullptr, 0);
               ID3D11PixelShader* ps = device_data.native_pixel_shaders[CompileTimeStringHash("Copy Depth")].get();
               native_device_context->PSSetShader(ps, nullptr, 0);
               ID3D11SamplerState* sampler = device_data.sampler_state_linear.get();
               native_device_context->PSGetSamplers(0, 1, &sampler);
               native_device_context->PSSetShaderResources(0, 1, &depth_source);
               native_device_context->IASetInputLayout(nullptr);
               native_device_context->RSSetState(nullptr);
               native_device_context->Draw(4, 0);

               draw_state_stack.Restore(native_device_context);

               native_device_context->RSGetState(context_data.original_scene_raterizer_state.put());
               native_device_context->RSSetState(game_device_data.scene_ui_rasterizer_state.get());

               native_device_context->OMGetBlendState(context_data.original_scene_blend_state.put(), nullptr, nullptr);
               native_device_context->OMSetBlendState(game_device_data.scene_ui_blend_state.get(), nullptr, 0xFFFFFFFF);

               context_data.frame_progress.SetReached(FrameProgress::SceneUiDrawStarted);
            }
            else if (dsd.DepthEnable && bd.RenderTarget[0].BlendOp != D3D11_BLEND_OP_REV_SUBTRACT && context_data.frame_progress.Reached(FrameProgress::SceneUiDrawStarted) &&
                     !shader_hashes_material.Contains(original_shader_hashes))
            {
               native_device_context->RSGetState(context_data.original_scene_raterizer_state.put());
               native_device_context->RSSetState(game_device_data.scene_ui_rasterizer_state.get());

               native_device_context->OMGetBlendState(context_data.original_scene_blend_state.put(), nullptr, nullptr);
               native_device_context->OMSetBlendState(game_device_data.scene_ui_blend_state.get(), nullptr, 0xFFFFFFFF);
            }
            else if (!dsd.DepthEnable && context_data.frame_progress.Reached(FrameProgress::SceneUiDrawStarted))
            {
               ResolveSceneUI(native_device_context, context_tag, game_device_data, device_data);
            }
         }
      }
      else if (context_data.frame_progress.Reached(FrameProgress::AddedParticles) &&
               game_device_data.upscaling)
      {
         if (!SrActive(device_data) ||
             original_shader_hashes.vertex_shaders.empty() ||
             original_shader_hashes.pixel_shaders.empty())
         {
            return DrawOrDispatchOverrideType::None;
         }

         ComPtr<ID3D11DepthStencilState> depth_stencil_state;
         native_device_context->OMGetDepthStencilState(depth_stencil_state.put(), nullptr);

         ComPtr<ID3D11BlendState> blend_state;
         native_device_context->OMGetBlendState(blend_state.put(), nullptr, nullptr);
         if (depth_stencil_state && blend_state)
         {
            D3D11_DEPTH_STENCIL_DESC dsd = {};
            depth_stencil_state->GetDesc(&dsd);

            D3D11_BLEND_DESC bd = {};
            blend_state->GetDesc(&bd);
            if (dsd.DepthEnable && bd.RenderTarget[0].BlendOp != D3D11_BLEND_OP_REV_SUBTRACT && !shader_hashes_material.Contains(original_shader_hashes))
            {
               if (context_data.vsconst_transform_data_changed)
               {
                  ComPtr<ID3D11Buffer> vertex_buffer;
                  uint32_t stride;
                  native_device_context->IAGetVertexBuffers(0, 1, vertex_buffer.put(), &stride, nullptr);
                  UpdatePreviousTransformAndCache((stages & reshade::api::shader_stage::pixel) != 0, false, false, vertex_buffer.get(), native_device_context,
                     context_tag, game_device_data, original_shader_hashes);
               }
               if (original_shader_hashes.pixel_shaders.front() != 0xE7F75BFE) // don't replace shader for sprites like gallica's speechbubble
               {
                  ID3D11PixelShader* shader = GetMotionVectorPixelShader(original_shader_hashes.vertex_shaders.front(), original_shader_hashes.pixel_shaders.front(), native_device, game_device_data);
                  if (!shader)
                  {
                     return DrawOrDispatchOverrideType::None;
                  }
                  native_device_context->PSSetShader(shader, nullptr, 0);
               }

               BindMotionVectorRenderTarget(native_device_context, context_data);
            }
         }
      }
      else if (context_data.vsconst_transform_data_changed)
      {
         GFD_VSCONST_TRANSFORM vs_consts = context_data.vsconst_transform_data;
         vs_consts.mtxLocalToWorldViewProj = context_data.proj_with_jitter * context_data.inv_proj * vs_consts.mtxLocalToWorldViewProj;
         vs_consts.mtxLocalToWorldViewProjPrev = context_data.prev_proj_with_current_jitter * context_data.prev_inv_proj * vs_consts.mtxLocalToWorldViewProjPrev;

         if (game_device_data.cb_transform)
         {
            native_device_context->UpdateSubresource(game_device_data.cb_transform, 0, nullptr, &vs_consts, 0, 0);
         }

         context_data.vsconst_transform_data_changed = false;
      }

      return overrideType;
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceDataMetaphorReFantazio;
   }

   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);

      device_data.force_reset_sr = !game_device_data.has_drawn_upscaling;
      game_device_data.has_drawn_upscaling = false;
      device_data.has_drawn_sr = false;
      device_data.has_drawn_main_post_processing = false;

      if (!custom_texture_mip_lod_bias_offset)
      {
         std::shared_lock shared_lock_samplers(s_mutex_samplers);
         if (SrActive(device_data) &&
             device_data.render_resolution.y > 0.0f &&
             device_data.output_resolution.y > 0.0f)
         {
            device_data.texture_mip_lod_bias_offset = std::log2(device_data.render_resolution.y / device_data.output_resolution.y) - 1.f; // This results in -1 at output res
         }
         else
         {
            device_data.texture_mip_lod_bias_offset = 0.f;
         }
      }

      cb_luma_global_settings.SRType = SrActive(device_data) ? (uint(device_data.sr_type) + 1) : 0;
      device_data.cb_luma_global_settings_dirty = true;
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

         uint32_t context_index = UINT32_MAX;
         for (uint32_t i = 0; i < 3; ++i)
         {
            if (native_command_list == game_device_data.draw_contexts[i].remainder_command_list && game_device_data.draw_contexts[i].partial_command_lists.size())
            {
               context_index = i;
            }
         }
         if (context_index == UINT32_MAX)
         {
            return;
         }

         DrawContextData& context_data = game_device_data.draw_contexts[context_index];

         context_data.remainder_command_list.reset();
         for (uint32_t i = 0; i < context_data.partial_command_lists.size(); ++i)
         {
            native_device_context->ExecuteCommandList(context_data.partial_command_lists[i].get(), FALSE);
            context_data.partial_command_lists[i].reset();
         }
         context_data.partial_command_lists.clear();

         if (!context_data.source_color || !context_data.dest_color || !context_data.depth_texture || device_data.sr_type == SR::Type::None)
         {
            return;
         }

         ComPtr<ID3D11Device> device;
         native_device_context->GetDevice(device.put());

         D3D11_TEXTURE2D_DESC target_desc = {};
         context_data.dest_color->GetDesc(&target_desc);

         {
            D3D11_TEXTURE2D_DESC src_desc = {};
            context_data.source_color->GetDesc(&src_desc);

            uint32_t width = src_desc.Width;
            uint32_t height = src_desc.Height;

            uint32_t output_width = target_desc.Width;
            uint32_t output_height = target_desc.Height;

            if (device_data.output_resolution.x != output_width ||
                device_data.output_resolution.y != output_height ||
                device_data.render_resolution.x != width ||
                device_data.render_resolution.y != height ||
                !game_device_data.scaled_motion_vectors) // check if resources were previously created
            {
               cb_luma_global_settings.GameSettings.RenderRes = {(float)width, (float)height};
               cb_luma_global_settings.GameSettings.InvRenderRes = {1.0f / (float)width, 1.0f / (float)height};
               cb_luma_global_settings.GameSettings.OutputRes = {(float)output_width, (float)output_height};
               cb_luma_global_settings.GameSettings.InvOutputRes = {1.0f / (float)output_width, 1.0f / (float)output_height};
               cb_luma_global_settings.GameSettings.RenderScale = (float)width / (float)output_width;
               cb_luma_global_settings.GameSettings.InvRenderScale = 1.0f / cb_luma_global_settings.GameSettings.RenderScale;
               device_data.cb_luma_global_settings_dirty = true;

               {
                  D3D11_TEXTURE2D_DESC motion_vector_desc = {};
                  motion_vector_desc.Width = width;
                  motion_vector_desc.Height = height;
                  motion_vector_desc.Usage = D3D11_USAGE_DEFAULT;
                  motion_vector_desc.ArraySize = 1;
                  motion_vector_desc.Format = DXGI_FORMAT_R32G32_FLOAT;
                  motion_vector_desc.SampleDesc.Count = 1;
                  motion_vector_desc.SampleDesc.Quality = 0;
                  motion_vector_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
                  motion_vector_desc.CPUAccessFlags = 0;
                  motion_vector_desc.MiscFlags = 0;
                  motion_vector_desc.MipLevels = 1;

                  device->CreateTexture2D(&motion_vector_desc,
                     nullptr,
                     game_device_data.scaled_motion_vectors.put());
               }
               {
                  D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
                  uav_desc.Format = DXGI_FORMAT_R32G32_FLOAT;
                  uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
                  uav_desc.Texture2D.MipSlice = 0;

                  device->CreateUnorderedAccessView(game_device_data.scaled_motion_vectors.get(),
                     &uav_desc,
                     game_device_data.scaled_motion_vectors_uav.put());
               }
               {
                  D3D11_TEXTURE2D_DESC bias_mask_desc = {};
                  bias_mask_desc.Width = width;
                  bias_mask_desc.Height = height;
                  bias_mask_desc.Usage = D3D11_USAGE_DEFAULT;
                  bias_mask_desc.ArraySize = 1;
                  bias_mask_desc.Format = DXGI_FORMAT_R16_FLOAT;
                  bias_mask_desc.SampleDesc.Count = 1;
                  bias_mask_desc.SampleDesc.Quality = 0;
                  bias_mask_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
                  bias_mask_desc.CPUAccessFlags = 0;
                  bias_mask_desc.MiscFlags = 0;
                  bias_mask_desc.MipLevels = 1;

                  device->CreateTexture2D(&bias_mask_desc,
                     nullptr,
                     game_device_data.bias_mask.put());
               }
               {
                  D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
                  uav_desc.Format = DXGI_FORMAT_R16_FLOAT;
                  uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
                  uav_desc.Texture2D.MipSlice = 0;

                  device->CreateUnorderedAccessView(game_device_data.bias_mask.get(),
                     &uav_desc,
                     game_device_data.bias_mask_uav.put());
               }

               float clear[] = {0.0f, 0.0f, 0.0f, 0.0f};
               native_device_context->ClearUnorderedAccessViewFloat(game_device_data.scaled_motion_vectors_uav.get(), clear);

               {
                  D3D11_TEXTURE2D_DESC desc = {};
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
                  D3D11_TEXTURE2D_DESC desc = {};
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
                  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
                  srv_desc.Format = target_desc.Format;
                  srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                  srv_desc.Texture2D.MostDetailedMip = 0;
                  srv_desc.Texture2D.MipLevels = 1;

                  device->CreateShaderResourceView(game_device_data.merged_texture.get(),
                     &srv_desc,
                     game_device_data.merged_texture_srv.put());
               }
               {
                  D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
                  rtv_desc.Format = target_desc.Format;
                  rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
                  rtv_desc.Texture2D.MipSlice = 0;

                  device->CreateRenderTargetView(game_device_data.merged_texture.get(),
                     &rtv_desc,
                     game_device_data.merged_texture_rtv.put());
               }
               {
                  D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
                  uavDesc.Format = target_desc.Format;
                  uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
                  uavDesc.Texture2D.MipSlice = 0;

                  device->CreateUnorderedAccessView(game_device_data.merged_texture.get(),
                     &uavDesc,
                     game_device_data.merged_texture_uav.put());
               }

               device_data.render_resolution.x = width;
               device_data.render_resolution.y = height;
               device_data.output_resolution.x = output_width;
               device_data.output_resolution.y = output_height;

               game_device_data.upscaling = device_data.render_resolution.x != device_data.output_resolution.x &&
                                            device_data.render_resolution.y != device_data.output_resolution.y;
            }
         }

         CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
         SetLumaConstantBuffers(native_device_context.get(), cmd_list_data, device_data, reshade::api::shader_stage::compute, LumaConstantBufferType::LumaSettings);

         auto* sr_instance_data = device_data.GetSRInstanceData();
         {
            SR::SettingsData settings_data;
            settings_data.output_width = device_data.output_resolution.x;
            settings_data.output_height = device_data.output_resolution.y;
            settings_data.render_width = device_data.render_resolution.x;
            settings_data.render_height = device_data.render_resolution.y;
            settings_data.dynamic_resolution = false;
            settings_data.hdr = false;
            settings_data.inverted_depth = false;
            settings_data.mvs_jittered = false;
            settings_data.render_preset = dlss_render_preset;
            sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context.get(), settings_data);
         }

         {
            ComPtr<ID3D11ShaderResourceView> depth_texture_srv;
            {
               D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
               srv_desc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
               srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
               srv_desc.Texture2D.MostDetailedMip = 0;
               srv_desc.Texture2D.MipLevels = 1;
               device->CreateShaderResourceView(context_data.depth_texture.get(),
                  &srv_desc,
                  depth_texture_srv.put());
            }

            {
               D3D11_MAPPED_SUBRESOURCE mapped_cbuffer;
               native_device_context->Map(game_device_data.cbuffer_motion_vector.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_cbuffer);
               float4x4* reprojection_matrix = (float4x4*)mapped_cbuffer.pData;
               *reprojection_matrix = context_data.prev_view_proj * context_data.view.GetTransposed().GetInverted().GetTransposed() * context_data.proj_with_jitter.GetTransposed().GetInverted().GetTransposed();
               native_device_context->Unmap(game_device_data.cbuffer_motion_vector.get(), 0);
            }

            ID3D11Buffer* cbs[] = {game_device_data.cbuffer_motion_vector.get()};
            ID3D11ShaderResourceView* srvs[] = {context_data.motion_vectors_srv.get(), depth_texture_srv.get()};
            ID3D11UnorderedAccessView* uavs[] = {game_device_data.scaled_motion_vectors_uav.get()};

            native_device_context->CSSetShader(device_data.native_compute_shaders[CompileTimeStringHash("Prepare Motion Vector")].get(), 0, 0);
            native_device_context->CSSetConstantBuffers(0, 1, cbs);
            native_device_context->CSSetShaderResources(0, 2, srvs);
            native_device_context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
            native_device_context->Dispatch((device_data.render_resolution.x + 7) / 8, (device_data.render_resolution.y + 7) / 8, 1);
         }

         if (context_data.particle_texture)
         {
            ComPtr<ID3D11ShaderResourceView> particle_texture_srv;
            {
               D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
               srv_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
               srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
               srv_desc.Texture2D.MostDetailedMip = 0;
               srv_desc.Texture2D.MipLevels = 1;
               device->CreateShaderResourceView(context_data.particle_texture.get(),
                  &srv_desc,
                  particle_texture_srv.put());
            }

            ID3D11ShaderResourceView* srvs[] = {particle_texture_srv.get()};
            ID3D11UnorderedAccessView* uavs[] = {game_device_data.bias_mask_uav.get()};

            native_device_context->CSSetShader(device_data.native_compute_shaders[CompileTimeStringHash("Create Bias Mask")].get(), 0, 0);
            native_device_context->CSSetShaderResources(0, 1, srvs);
            native_device_context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
            native_device_context->Dispatch((device_data.render_resolution.x + 7) / 8, (device_data.render_resolution.y + 7) / 8, 1);
         }

         {
            SR::SuperResolutionImpl::DrawData draw_data;
            draw_data.source_color = context_data.source_color.get();
            draw_data.output_color = game_device_data.resolve_texture.get();
            draw_data.motion_vectors = game_device_data.scaled_motion_vectors.get();
            draw_data.depth_buffer = context_data.depth_texture.get();
            draw_data.render_width = device_data.render_resolution.x;
            draw_data.render_height = device_data.render_resolution.y;
            draw_data.bias_mask = context_data.particle_texture ? game_device_data.bias_mask.get() : nullptr;
            draw_data.pre_exposure = 0.0f;
            draw_data.jitter_x = context_data.projection_jitters.x;
            draw_data.jitter_y = context_data.projection_jitters.y;
            draw_data.vert_fov = context_data.fov;
            draw_data.reset = device_data.force_reset_sr;

            bool multithread_protected = game_device_data.multithread ? game_device_data.multithread->GetMultithreadProtected() == TRUE : false;

            bool locked = false;
            if (game_device_data.multithread && multithread_protected)
            {
               game_device_data.multithread->Enter();
               locked = true;
            }

            bool dlss_succeeded = sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context.get(), draw_data);
            game_device_data.has_drawn_upscaling = dlss_succeeded;
            device_data.has_drawn_sr = dlss_succeeded;

            if (locked)
            {
               game_device_data.multithread->Leave();
            }
         }
         {
            ComPtr<ID3D11Device> device;
            native_device_context->GetDevice(device.put());
            ComPtr<ID3D11ShaderResourceView> resolve_texture_srv;
            ComPtr<ID3D11ShaderResourceView> color_srv;

            {
               D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
               srv_desc.Format = target_desc.Format;
               srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
               srv_desc.Texture2D.MostDetailedMip = 0;
               srv_desc.Texture2D.MipLevels = 1;
               device->CreateShaderResourceView(game_device_data.resolve_texture.get(),
                  &srv_desc,
                  resolve_texture_srv.put());
            }
            {
               D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
               srv_desc.Format = target_desc.Format;
               srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
               srv_desc.Texture2D.MostDetailedMip = 0;
               srv_desc.Texture2D.MipLevels = 1;
               device->CreateShaderResourceView(context_data.source_color.get(),
                  &srv_desc,
                  color_srv.put());
            }

            // some sr methods don't retain the alpha channel - combine sr result with the alpha from the original color texture
            {
               ID3D11ShaderResourceView* srvs[] = {resolve_texture_srv.get(), color_srv.get()};
               ID3D11UnorderedAccessView* uavs[] = {game_device_data.merged_texture_uav.get()};
               ID3D11SamplerState* samplers[] = {device_data.sampler_state_linear.get()};
               native_device_context->CSSetShader(device_data.native_compute_shaders[CompileTimeStringHash("Merge")].get(), 0, 0);
               native_device_context->CSSetShaderResources(0, 2, srvs);
               native_device_context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
               native_device_context->CSSetSamplers(0, 1, samplers);
               native_device_context->Dispatch((device_data.output_resolution.x + 7) / 8, (device_data.output_resolution.y + 7) / 8, 1);
            }

            native_device_context->CopySubresourceRegion(context_data.dest_color.get(), 0, 0, 0, 0, game_device_data.merged_texture.get(), 0, nullptr);
         }

         context_data.source_color.reset();
         context_data.dest_color.reset();
         context_data.depth_texture.reset();
         context_data.particle_texture.reset();
         // release all resources from the game we got this frame
         context_data.remainder_command_list.reset();
      }

      ComPtr<ID3D11CommandList> native_command_list;
      hr = device_child->QueryInterface(native_command_list.put());
      if (native_command_list)
      {
         ID3D11DeviceChild* device_child = (ID3D11DeviceChild*)(secondary_cmd_list->get_native());
         hr = device_child->QueryInterface(native_device_context.put());

         ContextTag context_tag = GetContextTag(native_device_context.get());
         if (GetContextType(context_tag) == ContextType::Draw)
         {
            DrawContextData& context_data = game_device_data.draw_contexts[GetContextIndex(context_tag)];

            context_data.original_scene_raterizer_state.reset();
            context_data.original_scene_blend_state.reset();
            context_data.original_scene_texture_rtv.reset();
            context_data.original_scene_dsv.reset();

            {
               std::unique_lock lock(game_device_data.transform_mutex);

               for (auto it = game_device_data.transform_lookup.begin(); it != game_device_data.transform_lookup.end();)
               {
                  if (it->second.transforms[0].empty() &&
                      it->second.transforms[1].empty() &&
                      it->second.transforms[2].empty())
                  {
                     it = game_device_data.transform_lookup.erase(it);
                  }
                  else
                  {
                     it++;
                  }
               }
            }

            context_data.frame_progress.Reset();

            context_data.cbuffer_cache.clear();

            if (context_data.partial_command_lists.size())
            {
               context_data.remainder_command_list = native_command_list.get();
            }
         }
      }
   }

   static bool OnUpdateBufferRegionCommand(reshade::api::command_list* cmd_list, const void* data, reshade::api::resource dest, uint64_t dest_offset, uint64_t size)
   {
      auto& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);

      ID3D11DeviceContext* native_device_context = (ID3D11DeviceContext*)(cmd_list->get_native());
      ContextTag context_tag = GetContextTag(native_device_context);

      if (GetContextType(context_tag) == ContextType::ShadowMap &&
          (ID3D11Buffer*)dest.handle == game_device_data.cb_shadow_transform)
      {
         uint32_t context_idx = GetContextIndex(context_tag);
         game_device_data.shadow_contexts[context_idx].world_view_proj = ((GFD_VSCONST_TRANSFORM*)data)->mtxLocalToWorldViewProj;
         game_device_data.shadow_contexts[context_idx].world_view_proj_valid = true;
      }

      if (!SrActive(device_data) ||
          GetContextType(context_tag) != ContextType::Draw)
      {
         return false;
      }

      DrawContextData& context_data = game_device_data.draw_contexts[GetContextIndex(context_tag)];

      // store values so we can find changes for the constant buffers we are interested in
      if (game_device_data.cb_viewproj == nullptr ||
          game_device_data.cb_viewproj == (ID3D11Buffer*)dest.handle ||
          game_device_data.cb_system == nullptr ||
          game_device_data.cb_system == (ID3D11Buffer*)dest.handle ||
          game_device_data.cb_transform == nullptr)
      {
         ID3D11Buffer* buffer = (ID3D11Buffer*)dest.handle;
         D3D11_BUFFER_DESC bd = {};
         ((ID3D11Buffer*)dest.handle)->GetDesc(&bd);
         if (bd.ByteWidth != 208 && // GFD_VSCONST_VIEWPROJ
             bd.ByteWidth != 256 && // GFD_VSCONST_TRANSFORM
             bd.ByteWidth != 288)   // GFD_PSCONST_SYSTEM
         {
            return false;
         }

         memcpy(context_data.cbuffer_cache[buffer].data(), data, bd.ByteWidth);

         return false;
      }

      // early out we don't need any cbuffer values after rendering finished
      if ((context_data.frame_progress.Reached(FrameProgress::AddedParticles) ||
             context_data.frame_progress.Reached(FrameProgress::SceneUiDrawStarted)) &&
          !game_device_data.upscaling)
      {
         return false;
      }

      // game_device_data.frame_phase == FramePhase::GBUFFER
      if ((ID3D11Buffer*)dest.handle == game_device_data.cb_transform)
      {
         ComPtr<ID3D11DeviceContext> native_device_context;
         ID3D11DeviceChild* device_child = (ID3D11DeviceChild*)(cmd_list->get_native());
         HRESULT hr = device_child->QueryInterface(native_device_context.put());
         HandleTransformUpdate((ID3D11Buffer*)dest.handle, data, context_data);
         return context_data.frame_progress.Reached(FrameProgress::OpaqueRenderingStarted) && !context_data.frame_progress.Reached(FrameProgress::AddedParticles);
      }

      return false;
   }

   static bool OnCreatePipeline(reshade::api::device* device, reshade::api::pipeline_layout layout, uint32_t subobject_count, const reshade::api::pipeline_subobject* subobjects)
   {
      auto& device_data = *device->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);
      for (uint32_t i = 0; i < subobject_count; ++i)
      {
         const auto& subobject = subobjects[i];
         for (uint32_t j = 0; j < subobject.count; ++j)
         {
            if (subobject.type == reshade::api::pipeline_subobject_type::vertex_shader)
            {
               const auto* original_shader_desc = static_cast<reshade::api::shader_desc*>(subobjects[i].data);
               if (System::ScanMemoryForPattern((const std::byte*)original_shader_desc->code, original_shader_desc->code_size, (std::byte*)"mtxLocalToWorldViewProjPrev", 27, true).size() == 0)
               {
                  continue;
               }
               std::vector<std::byte> shader_code((const std::byte*)original_shader_desc->code, ((const std::byte*)original_shader_desc->code) + original_shader_desc->code_size);

               uint32_t prev_coord_output_register;
               PatchVertexShader(shader_code, prev_coord_output_register);
               if (prev_coord_output_register != 0xFFFFFFFF)
               {
                  uint32_t hash = Shader::BinToHash((const uint8_t*)original_shader_desc->code, original_shader_desc->code_size);
                  game_device_data.vertex_shader_ndc_coord_indices[hash] = {prev_coord_output_register - 1, prev_coord_output_register};

                  ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
                  ComPtr<ID3D11VertexShader> patched_shader;
                  native_device->CreateVertexShader(shader_code.data(), shader_code.size(), nullptr, patched_shader.put());

                  game_device_data.modified_vertex_shaders[hash] = patched_shader;
               }
            }
            else if (subobject.type == reshade::api::pipeline_subobject_type::pixel_shader)
            {
               const auto* original_shader_desc = static_cast<reshade::api::shader_desc*>(subobjects[i].data);
               uint32_t hash = Shader::BinToHash((const uint8_t*)original_shader_desc->code, original_shader_desc->code_size);
               std::vector<std::byte> code;
               code.resize(original_shader_desc->code_size);
               memcpy(&code[0], original_shader_desc->code, original_shader_desc->code_size);
               game_device_data.pixel_shader_code[hash] = std::move(code);

               if (System::ScanMemoryForPattern((const std::byte*)original_shader_desc->code, original_shader_desc->code_size, (std::byte*)"GFD_PSCONST_SYSTEM", 18, true).size() > 0)
               {
                  shader_hashes_material.pixel_shaders.emplace(hash);
               }
            }
            else if (subobject.type == reshade::api::pipeline_subobject_type::blend_state)
            {
               auto* blend_desc = static_cast<reshade::api::blend_desc*>(subobjects[i].data);
               blend_desc->blend_enable[5] = true;
               blend_desc->source_color_blend_factor[5] = reshade::api::blend_factor::one;
               blend_desc->dest_color_blend_factor[5] = reshade::api::blend_factor::zero;
               blend_desc->color_blend_op[5] = reshade::api::blend_op::add;
               blend_desc->render_target_write_mask[5] = 15;
               return true;
            }
         }
      }
      return false;
   }

   static void OnInitResource(reshade::api::device* device, const reshade::api::resource_desc& desc, const reshade::api::subresource_data* initial_data, reshade::api::resource_usage initial_state, reshade::api::resource resource)
   {
      if (desc.type != reshade::api::resource_type::buffer ||
          (desc.usage & reshade::api::resource_usage::vertex_buffer) == 0 ||
          (desc.flags & reshade::api::resource_flags::immutable) == 0 ||
          desc.buffer.size < 2000)
      {
         return;
      }

      auto& device_data = *device->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);

      ID3D11Buffer* buffer = (ID3D11Buffer*)resource.handle;

      {
         const std::shared_lock shared_lock_bounding_boxes(game_device_data.bounding_box_mutex);

         if (game_device_data.bounding_boxes.contains(buffer))
         {
            return;
         }
      }

      BoundingBox box28;
      {
         BoundingBox box;
         box.min = {FLT_MAX, FLT_MAX, FLT_MAX};
         box.max = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

         int64_t remaining_bytes = (int64_t)desc.buffer.size;
         uint32_t stride = 28;

         const byte* data = (const byte*)initial_data->data;
         while (remaining_bytes > 0)
         {
            float3 pos = *(float3*)data;

            box.min.x = min(box.min.x, pos.x);
            box.min.y = min(box.min.y, pos.y);
            box.min.z = min(box.min.z, pos.z);

            box.max.x = max(box.max.x, pos.x);
            box.max.y = max(box.max.y, pos.y);
            box.max.z = max(box.max.z, pos.z);

            data += stride;
            remaining_bytes -= stride;
         }

         box28 = box;
      }

      BoundingBox box40;
      {
         BoundingBox box;
         box.min = {FLT_MAX, FLT_MAX, FLT_MAX};
         box.max = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

         int64_t remaining_bytes = (int64_t)desc.buffer.size;
         uint32_t stride = 40;

         const byte* data = (const byte*)initial_data->data;
         while (remaining_bytes > 0)
         {
            float3 pos = *(float3*)data;

            box.min.x = min(box.min.x, pos.x);
            box.min.y = min(box.min.y, pos.y);
            box.min.z = min(box.min.z, pos.z);

            box.max.x = max(box.max.x, pos.x);
            box.max.y = max(box.max.y, pos.y);
            box.max.z = max(box.max.z, pos.z);

            data += stride;
            remaining_bytes -= stride;
         }

         box40 = box;
      }

      const std::unique_lock lock_bounding_boxes(game_device_data.bounding_box_mutex);
      game_device_data.bounding_boxes[buffer] = {box28, box40};
   }

   static void OnDestroyResource(reshade::api::device* device, reshade::api::resource resource)
   {
      auto& device_data = *device->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);

      const std::unique_lock lock_bounding_boxes(game_device_data.bounding_box_mutex);
      game_device_data.bounding_boxes.erase((ID3D11Buffer*)resource.handle);
   }

   static void OnInitCommandList(reshade::api::command_list* cmd_list)
   {
      ComPtr<ID3D11DeviceContext> native_device_context;
      ID3D11DeviceChild* device_child = (ID3D11DeviceChild*)(cmd_list->get_native());
      HRESULT hr = device_child->QueryInterface(native_device_context.put());
      if (native_device_context)
      {
         auto& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
         auto& game_device_data = GetGameDeviceData(device_data);

         // The game creates 13 deferred contexts in total - one of unknown use
         //  and 4 groups of 3 performing specific operations and cycled through every frame
         //  0 - doesn't seem to be used
         // 1,2,3 - Compute(skinning)
         // 4,5,6 - Shadow map
         // 7,8,9 - Planar reflections
         // 10,11,12 - main draw

#if DEVELOPMENT
         ASSERT_ONCE(game_device_data.command_list_count < 13);
#endif
         ContextTag tag = ContextTag(game_device_data.command_list_count);

         native_device_context->SetPrivateData(GUID_DeferredContextTag, sizeof(tag), &tag);
         game_device_data.command_list_count++;
      }
   }

   void DrawImGuiSettings(DeviceData& device_data) override
   {
      reshade::api::effect_runtime* runtime = nullptr;

      ImGui::NewLine();

      if (ImGui::Checkbox("Enable HDR", &next_enable_hdr))
      {
         reshade::set_config_value(runtime, NAME, "EnableHDR", next_enable_hdr);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
         ImGui::SetTooltip("Requires restart.");
      }

      const char* upscaling_mode_names[] = {
         "Auto",
         "Yes",
         "No"};
      if (ImGui::BeginCombo("Use Super Resolution for upscaling", upscaling_mode_names[(uint32_t)upscaling_mode]))
      {
         auto AddComboItem = [&](const char* name, uint32_t size, bool enabled)
         {
            const bool selected = upscaling_mode == (UpscalingMode)size;
            if (ImGui::Selectable(name, selected))
            {
               upscaling_mode = (UpscalingMode)size;
               reshade::set_config_value(runtime, NAME, "UpscalingMode", size);
            }
            if (selected)
            {
               ImGui::SetItemDefaultFocus();
            }
         };

         AddComboItem(upscaling_mode_names[(uint32_t)UpscalingMode::Auto], (uint32_t)UpscalingMode::Auto, true);
         AddComboItem(upscaling_mode_names[(uint32_t)UpscalingMode::SuperResolution], (uint32_t)UpscalingMode::SuperResolution, true);
         AddComboItem(upscaling_mode_names[(uint32_t)UpscalingMode::Game], (uint32_t)UpscalingMode::Game, true);
         ImGui::EndCombo();
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
         ImGui::SetTooltip("When enabled setting Rendering Scale to 50%% or 75%% will use Super Resolution (DLSS or FSR) to scale the image to the output resolution.\n"
                           "Otherwise DLAA or FSR AA and game internal upscaler is used.\n"
                           "Especially with FSR this might not look better and will degrade visual quality of the floating icons and the blur when sprinting.\n"
                           "Auto mode chooses the best setting for the current Super Resolution technique.\n");
      }

      const char* previewString;
      char buffer[32];
      if (g_scene_ui_msaa_samples == 1)
      {
         previewString = "Off";
      }
      else if (g_scene_ui_msaa_samples == 2)
      {
         previewString = "2x";
      }
      else if (g_scene_ui_msaa_samples == 4)
      {
         previewString = "4x";
      }
      else if (g_scene_ui_msaa_samples == 8)
      {
         previewString = "8x";
      }
      ImGui::BeginDisabled(UseSRForUpscaling(device_data) && device_data.render_resolution != device_data.output_resolution);
      if (ImGui::BeginCombo("3D UI MSAA Sample Count", previewString))
      {
         auto AddComboItem = [&](const char* name, uint32_t size, bool enabled)
         {
            const bool selected = g_scene_ui_msaa_samples == size;
            if (ImGui::Selectable(name, selected))
            {
               g_scene_ui_msaa_samples = size;
               reshade::set_config_value(runtime, NAME, "SceneUiMsaaSamples", g_scene_ui_msaa_samples);
            }
            if (selected)
            {
               ImGui::SetItemDefaultFocus();
            }
         };

         AddComboItem("Off", 1, true);
         AddComboItem("2x", 2, true);
         AddComboItem("4x", 4, true);
         AddComboItem("8x", 8, true);
         ImGui::EndCombo();
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      {
         ImGui::SetTooltip("Applies MSAA to the floating icons. Only active when the image isn't upscaled with DLSS/FSR.");
      }
      ImGui::EndDisabled();
   }

#if DEVELOPMENT || TEST
   void PrintImGuiInfo(const DeviceData& device_data) override
   {
      char buffer[1024];
      sprintf(buffer, "\nDraw calls culled: %d\n", draw_calls_culled);
      ImGui::Text(buffer);
      draw_calls_culled = 0;

      sprintf(buffer, "\nShadow draw calls total: %d\n", shadow_draw_calls);
      ImGui::Text(buffer);
      sprintf(buffer, "\nShadow draw calls culled: %d\n", shadow_draw_calls_culled);
      ImGui::Text(buffer);

      shadow_draw_calls_culled = 0;
      shadow_draw_calls = 0;
   }
#endif

   void PrintImGuiAbout() override
   {
      ImGui::Text("Metaphor: ReFantazio Luma mod - about and credits section", "");
      ImGui::Text("Credits:\n"
                  "Idarion\n"
                  "Luma Framework: Pumbo\n"
                  "HDR implementation based on RenoDX mod by: Ritsu\n");
      ImGui::Text("\nrenodx\n"
                  "MIT License\n"
                  "\n"
                  "Copyright (c) 2025 Carlos Lopez Jr.\n"
                  "\n"
                  "Permission is hereby granted, free of charge, to any person obtaining a copy\n"
                  "of this software and associated documentation files (the \"Software\"), to deal\n"
                  "in the Software without restriction, including without limitation the rights\n"
                  "to use, copy, modify, merge, publish, distribute, sublicense, and/or sell\n"
                  "copies of the Software, and to permit persons to whom the Software is\n"
                  "furnished to do so, subject to the following conditions:\n"
                  "\n"
                  "The above copyright notice and this permission notice shall be included in all\n"
                  "copies or substantial portions of the Software.\n"
                  "\n"
                  "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n"
                  "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n"
                  "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE\n"
                  "AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\n"
                  "LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,\n"
                  "OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE\n"
                  "SOFTWARE.\n");
      ImGui::Text("\nxxHash Library\n"
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
      Globals::SetGlobals(PROJECT_NAME, "Metaphor: ReFantazio Luma mod");
      Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::Playable;
      Globals::VERSION = 1;

      enable_samplers_upgrade = true;

      shader_hashes_tonemap.pixel_shaders.emplace(std::stoul("A7108284", nullptr, 16));
      shader_hashes_tonemap.pixel_shaders.emplace(std::stoul("C1787BC6", nullptr, 16));

      shader_hashes_merge_particles.pixel_shaders.emplace(std::stoul("AC103037", nullptr, 16));

      shader_hashes_fxaa.pixel_shaders.emplace(std::stoul("94D1203C", nullptr, 16));

      shader_hashes_smaa_edge_detection.pixel_shaders.emplace(std::stoul("8C9E5C72", nullptr, 16));
      shader_hashes_smaa_weight_calculation.pixel_shaders.emplace(std::stoul("CA15EAC0", nullptr, 16));
      shader_hashes_smaa_blending.pixel_shaders.emplace(std::stoul("5732C405", nullptr, 16));

      shader_hashes_dof_prepare.pixel_shaders.emplace(std::stoul("19B152A6", nullptr, 16));

      shader_hashes_lut.pixel_shaders.emplace(std::stoul("D8196629", nullptr, 16));

      shader_hashes_outline.vertex_shaders.emplace(0xBF5FF106);
      shader_hashes_outline.vertex_shaders.emplace(0x155F917A);
      shader_hashes_outline.vertex_shaders.emplace(0xAC0C30DA);
      shader_hashes_outline.vertex_shaders.emplace(0xAA5FA872);
      shader_hashes_outline.vertex_shaders.emplace(0x4BA795B0);
      shader_hashes_outline.vertex_shaders.emplace(0x06974A0D);
      shader_hashes_outline.vertex_shaders.emplace(0xEF234E0D);
      shader_hashes_outline.vertex_shaders.emplace(0x13CA235D);
      shader_hashes_outline.vertex_shaders.emplace(0x4556846C);
      shader_hashes_outline.vertex_shaders.emplace(0xF9DBB0A3);
      shader_hashes_outline.vertex_shaders.emplace(0x4570848A);
      shader_hashes_outline.vertex_shaders.emplace(0x4738BD67);
      shader_hashes_outline.vertex_shaders.emplace(0x791D21BB);
      shader_hashes_outline.vertex_shaders.emplace(0xE74080FF);
      shader_hashes_outline.vertex_shaders.emplace(0x596A7EF4);
      shader_hashes_outline.vertex_shaders.emplace(0x4F8411AC);
      shader_hashes_outline.vertex_shaders.emplace(0x942BB234);
      shader_hashes_outline.vertex_shaders.emplace(0x60258879);
      shader_hashes_outline.vertex_shaders.emplace(0x40C3609D);
      shader_hashes_outline.vertex_shaders.emplace(0x0626E62A);
      shader_hashes_outline.vertex_shaders.emplace(0xCA9890B9);
      shader_hashes_outline.vertex_shaders.emplace(0xF1C91A88);
      shader_hashes_outline.vertex_shaders.emplace(0x32EA4F16);
      shader_hashes_outline.vertex_shaders.emplace(0x0457B469);
      shader_hashes_outline.vertex_shaders.emplace(0xEAC4051F);
      shader_hashes_outline.vertex_shaders.emplace(0x3BEAEE64);
      shader_hashes_outline.vertex_shaders.emplace(0xBC1CB334);
      shader_hashes_outline.vertex_shaders.emplace(0x5AF9F60E);
      shader_hashes_outline.vertex_shaders.emplace(0xF428E7C9);
      shader_hashes_outline.vertex_shaders.emplace(0x83600C7A);
      shader_hashes_outline.vertex_shaders.emplace(0x8074A956);
      shader_hashes_outline.vertex_shaders.emplace(0xBC35C89E);
      shader_hashes_outline.vertex_shaders.emplace(0x698E18C4);
      shader_hashes_outline.vertex_shaders.emplace(0x4444523E);
      shader_hashes_outline.vertex_shaders.emplace(0x1A870BC3);
      shader_hashes_outline.vertex_shaders.emplace(0x6B4BFF34);
      shader_hashes_outline.vertex_shaders.emplace(0x48B83083);
      shader_hashes_outline.vertex_shaders.emplace(0xD4E02B75);
      shader_hashes_outline.vertex_shaders.emplace(0xFD1E6280);
      shader_hashes_outline.vertex_shaders.emplace(0xCC0E1722);
      shader_hashes_outline.vertex_shaders.emplace(0xB8A229B4);
      shader_hashes_outline.vertex_shaders.emplace(0xD24AA7C9);
      shader_hashes_outline.vertex_shaders.emplace(0xEA628D0C);
      shader_hashes_outline.vertex_shaders.emplace(0xA98201C3);
      shader_hashes_outline.vertex_shaders.emplace(0xE52D3977);
      shader_hashes_outline.vertex_shaders.emplace(0x59100809);
      shader_hashes_outline.vertex_shaders.emplace(0x19F7088B);
      shader_hashes_outline.vertex_shaders.emplace(0x74C65285);
      shader_hashes_outline.vertex_shaders.emplace(0x74CE22E3);
      shader_hashes_outline.vertex_shaders.emplace(0xF48CCAC1);
      shader_hashes_outline.vertex_shaders.emplace(0x42C641AC);
      shader_hashes_outline.vertex_shaders.emplace(0x0D23069A);
      shader_hashes_outline.vertex_shaders.emplace(0x29F2DD5F);
      shader_hashes_outline.vertex_shaders.emplace(0xA7A61EEF);
      shader_hashes_outline.vertex_shaders.emplace(0xAC5361B3);
      shader_hashes_outline.vertex_shaders.emplace(0xB60A4BD4);
      shader_hashes_outline.vertex_shaders.emplace(0x66D1D047);
      shader_hashes_outline.vertex_shaders.emplace(0x8FC43E56);
      shader_hashes_outline.vertex_shaders.emplace(0x8E41F8DB);
      shader_hashes_outline.vertex_shaders.emplace(0xF02623E0);
      shader_hashes_outline.vertex_shaders.emplace(0x5263AEB4);
      shader_hashes_outline.vertex_shaders.emplace(0xE30ADBA6);
      shader_hashes_outline.vertex_shaders.emplace(0x5BD11C4A);
      shader_hashes_outline.vertex_shaders.emplace(0x83245268);

      shader_hashes_ocean.vertex_shaders.emplace(0x43A6029F);
      shader_hashes_ocean.vertex_shaders.emplace(0x90D867D7);
      shader_hashes_ocean.vertex_shaders.emplace(0x10401BC4);
      shader_hashes_ocean.vertex_shaders.emplace(0xA74FE1E5);
      shader_hashes_ocean.vertex_shaders.emplace(0xB2510239);

      // unused cbuffer slots by type
      // VS - 5, 8, 9, 13(not used by shader but set by the game)
      // PS - none but 8 is only use in 2 alpha test shaders
      // CS - 2, 3 , 5, 6, 7, 8, 9, 10, 11, 12, 13
      luma_settings_cbuffer_index = 8;
      luma_data_cbuffer_index = 9;

      game = new MetaphorReFantazio();
   }
   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
      reshade::unregister_event<reshade::addon_event::execute_secondary_command_list>(MetaphorReFantazio::OnExecuteSecondaryCommandList);
      reshade::unregister_event<reshade::addon_event::update_buffer_region_command>(MetaphorReFantazio::OnUpdateBufferRegionCommand);
      reshade::unregister_event<reshade::addon_event::create_pipeline>(MetaphorReFantazio::OnCreatePipeline);
      reshade::unregister_event<reshade::addon_event::init_resource>(MetaphorReFantazio::OnInitResource);
      reshade::unregister_event<reshade::addon_event::destroy_resource>(MetaphorReFantazio::OnDestroyResource);
      reshade::unregister_event<reshade::addon_event::init_command_list>(MetaphorReFantazio::OnInitCommandList);
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      reshade::get_config_value(nullptr, NAME, "FirstBoot", first_boot);
      if (first_boot)
      {
         reshade::set_config_value(nullptr, NAME, "FirstBoot", false);

         // Automatically enable HDR in the mod if it's supported on the primary display on first boot
         bool hdr_supported_display;
         bool hdr_enabled_display;
         Display::IsHDRSupportedAndEnabled(0, hdr_supported_display, hdr_enabled_display);
         enable_hdr = hdr_supported_display;

         reshade::set_config_value(nullptr, NAME, "EnableHDR", enable_hdr);
      }
      else
      {
         reshade::get_config_value(nullptr, NAME, "EnableHDR", enable_hdr);
      }
      next_enable_hdr = enable_hdr;

      if (enable_hdr)
      {
         swapchain_format_upgrade_type = TextureFormatUpgradesType::AllowedEnabled;
         swapchain_upgrade_type = SwapchainUpgradeType::scRGB;
      }
      else
      {
         swapchain_upgrade_type = SwapchainUpgradeType::None;
         force_disable_display_composition = true;
      }
   }

   return TRUE;
}