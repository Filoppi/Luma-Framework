#define GAME_BLUE_REFLECTION_SECOND_LIGHT 1

#define DISABLE_AUTO_DEBUGGER 1
#define DISABLE_FOCUS_LOSS_SUPPRESSION 1
#define CHECK_GRAPHICS_API_COMPATIBILITY 1
#define AVOID_INPUT_LOSS 1
#define DISABLE_SWAPCHAIN_FLIP_MODEL 1
#define ENABLE_DRAW_DISPATCH_DATA_CACHE 1
#define ENABLE_POST_DRAW_DISPATCH_CALLBACK 1

// FSR is enabled to force feature level 11.1
#define DEBUG_LOG 0

//#define LUMA_PATCH_BYTECODE_SYNC 1

#include "..\..\Core\core.hpp"

#include "includes\shader_patches.h"
#include "includes\hooks.hpp"
#include "includes\safetyhook.hpp"
#include "includes\hooks.cpp"
#include "includes\stretchy_buffer.h"
#define XXH_STATIC_LINKING_ONLY
#define XXH_IMPLEMENTATION
#include "includes\xxhash.h"

enum class FramePhase
{
   NONE,
   GBUFFER,
   FORWARD,
   POSTPROCESSING_AND_UI,
   UNDEFINED,
};

struct SkinCacheEntry
{
   uint32_t offset;
   uint32_t stride;
};

struct CBufferEntry
{
   uint32_t offset;
   uint32_t size;
   float3 world_position;
};

struct PendingCBufferData
{
   std::vector<uint8_t> cpu_data;
   void* data_ptr = nullptr;
   bool dirty = false;
   uint32_t size;
   uint32_t last_dirty_offset = 0;
};

struct CameraMatrices
{
   DirectX::XMMATRIX view_matrix;
   DirectX::XMMATRIX projection_matrix;
   DirectX::XMMATRIX inv_view_matrix;
   DirectX::XMMATRIX inv_projection_matrix;
   float4 position;
};

struct SearchModeFilterRenderState
{
   bool dirty = false;
   ComPtr<ID3D11VertexShader> vertex_shader;
   //ComPtr<ID3D11PixelShader> pixel_shader;
   ComPtr<ID3D11InputLayout> input_layout;
   ComPtr<ID3D11Buffer> vertex_buffer;
   ComPtr<ID3D11Buffer> index_buffer;
   ComPtr<ID3D11Buffer> vs_constant_buffer;
   ComPtr<ID3D11Buffer> ps_constant_buffer;
   std::array<ComPtr<ID3D11SamplerState>, 2> ps_samplers;
   ComPtr<ID3D11ShaderResourceView> ps_depth_srv;
   ComPtr<ID3D11DepthStencilView> depth_stencil_view;
   ComPtr<ID3D11DepthStencilState> depth_stencil_state;
   ComPtr<ID3D11RasterizerState> rasterizer_state;
   UINT vertex_buffer_stride = 0;
   UINT vertex_buffer_offset = 0;
   UINT index_buffer_offset = 0;
   UINT stencil_ref = 0;
   DXGI_FORMAT index_buffer_format = DXGI_FORMAT_UNKNOWN;
   
   void Reset()
   {
      dirty = false;
      vertex_shader = nullptr;
      //pixel_shader = nullptr;
      input_layout = nullptr;
      vertex_buffer = nullptr;
      index_buffer = nullptr;
      vs_constant_buffer = nullptr;
      ps_constant_buffer = nullptr;
      ps_samplers[0] = nullptr;
      ps_samplers[1] = nullptr;
      ps_depth_srv = nullptr;
      depth_stencil_view = nullptr;
      depth_stencil_state = nullptr;
      rasterizer_state = nullptr;
      
      vertex_buffer_stride = 0;
      vertex_buffer_offset = 0;
      index_buffer_offset = 0;
      stencil_ref = 0;
      index_buffer_format = DXGI_FORMAT_UNKNOWN;
   }
};

namespace MotionBlur
{
   template <typename T, typename Enum>
   struct EnumArray
   {
      std::array<T, static_cast<size_t>(Enum::Count)> data;

      T& operator[](Enum e)
      {
         return data[static_cast<size_t>(e)];
      }

      const T& operator[](Enum e) const
      {
         return data[static_cast<size_t>(e)];
      }
   };
   
   enum class Texture
   {
      VelocityBuffer,
      Tile2,
      Tile4,
      Tile8,
      TileMax,
      TileNeighborMax,
      Count,
   };
   
   struct DrawData
   {
      ID3D11ShaderResourceView* input_color_srv = nullptr;
      ID3D11ShaderResourceView* input_mv_srv = nullptr;
      ID3D11ShaderResourceView* input_depth_srv = nullptr;
      ID3D11RenderTargetView* output_rtv = nullptr;
      int width = 0;
      int height = 0;
      int velocity_tex_width = 0;
      int velocity_tex_height = 0;
      float z_far;
      float z_near;
      bool z_reversed = false;
      float shutter_angle = 270.f;
      int sample_count = 10;
   };
   
   struct alignas(16) CBufferData
   {
      float VelocityScale;
      float MaxBlurRadius;
      float RcpMaxBlurRadius;
      float LoopCount;
	  
      float2 TileMaxOffs;
      float TileMaxLoop;
      float pad = 0.0;
	  
      float4 MainTex_TexelSize = {0.0, 0.0, 0.0, 0.0};
      float4 CameraMotionVectorsTexture_TexelSize = {0.0, 0.0, 0.0, 0.0};
      float2 VelocityTex_TexelSize = {0.0, 0.0};
      float2 NeighborMaxTex_TexelSize = {0.0, 0.0};
	  
      float4 DepthParams = {0.0, 0.0, 0.0, 1.0};
   };
   
   struct Resource
   {
      ComPtr<ID3D11Texture2D> tex;
      ComPtr<ID3D11ShaderResourceView> srv;
      ComPtr<ID3D11RenderTargetView> rtv;
      float2 dimension = {0.0, 0.0};
   };
   
   class MotionBlurPass
   {
   public:
      void Draw(ID3D11Device* device, ID3D11DeviceContext* device_context, const DeviceData& device_data, const DrawData& data)
      {
         // Init CBuffer
         CBufferData cb_data;
         InitCBuffer(data, cb_data);
         
         // Create viewport
         D3D11_VIEWPORT viewport = {};
         viewport.TopLeftX = 0;
         viewport.TopLeftY = 0;
         viewport.MinDepth = 0;
         viewport.MaxDepth = 1;
         
         // Create CB
         if (!cbuffer.get())
         {
            D3D11_BUFFER_DESC bd;
            bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            bd.ByteWidth = sizeof(CBufferData);
            bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            bd.MiscFlags = 0;
            bd.StructureByteStride = 0;
            bd.Usage = D3D11_USAGE_DYNAMIC;
            device->CreateBuffer(&bd, nullptr, cbuffer.put());
         }
         
         // Create Samplers
         if (!linear_sampler.get())
         {
            D3D11_SAMPLER_DESC sampler_desc = {};

            sampler_desc.Filter = D3D11_FILTER::D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
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
            
            device->CreateSamplerState(&sampler_desc, linear_sampler.put());
         }
         if (!point_sampler.get())
         {
            D3D11_SAMPLER_DESC sampler_desc = {};

            sampler_desc.Filter = D3D11_FILTER::D3D11_FILTER_MIN_MAG_MIP_POINT;
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
            
            device->CreateSamplerState(&sampler_desc, point_sampler.put());
         }
         
         // Create Textures
         SetupTextures(device, data.velocity_tex_width, data.velocity_tex_height);
         
         // Set states to default
         //device_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
         //device_context->OMSetDepthStencilState(nullptr, 0);
         //device_context->RSSetState(nullptr);
         //device_context->RSSetScissorRects(0, nullptr);
         device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
         //device_context->IASetInputLayout(nullptr);
         
         // VelocityBuffer
         ID3D11SamplerState* ps_samplers[2] = {linear_sampler.get(), point_sampler.get()};
         ID3D11ShaderResourceView* srvs[3] = {data.input_depth_srv, data.input_mv_srv, nullptr};
         ID3D11RenderTargetView* rtvs[1] = {resources[Texture::VelocityBuffer].rtv.get()};
         ID3D11Buffer* cbvs[] = {cbuffer.get()};
         
         cb_data.MainTex_TexelSize.x = static_cast<float>(data.velocity_tex_width);
         cb_data.MainTex_TexelSize.y = static_cast<float>(data.velocity_tex_height);
         cb_data.MainTex_TexelSize.z = 1.f / cb_data.MainTex_TexelSize.x;
         cb_data.MainTex_TexelSize.w = 1.f / cb_data.MainTex_TexelSize.y;
         viewport.Width = static_cast<float>(data.velocity_tex_width);
         viewport.Height = static_cast<float>(data.velocity_tex_height);
         D3D11_MAPPED_SUBRESOURCE mapped_buffer;
         device_context->Map(cbuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer);
         memcpy(mapped_buffer.pData, &cb_data, sizeof(cb_data));
         device_context->Unmap(cbuffer.get(), 0);
         
         auto vs = device_data.native_vertex_shaders.at(Math::CompileTimeStringHash("Motion Blur Vertex")).get();
         auto ps = device_data.native_pixel_shaders.at(Math::CompileTimeStringHash("Motion Blur Velocity Setup")).get();
         device_context->VSSetShader(vs, nullptr, 0);
         device_context->PSSetShader(ps, nullptr, 0);
         device_context->PSSetSamplers(7, 2, &ps_samplers[0]);
         device_context->PSSetConstantBuffers(0, 1, &cbvs[0]);
         device_context->OMSetRenderTargets(1, &rtvs[0], nullptr);
         device_context->PSSetShaderResources(0, 2, &srvs[0]);
         device_context->RSSetViewports(1, &viewport);
         device_context->Draw(3, 0);
         
         // Tile2
         cb_data.MainTex_TexelSize.x = resources[Texture::VelocityBuffer].dimension.x;
         cb_data.MainTex_TexelSize.y = resources[Texture::VelocityBuffer].dimension.y;
         cb_data.MainTex_TexelSize.z = 1.f / cb_data.MainTex_TexelSize.x;
         cb_data.MainTex_TexelSize.w = 1.f / cb_data.MainTex_TexelSize.y;
         viewport.Width = resources[Texture::Tile2].dimension.x;
         viewport.Height = resources[Texture::Tile2].dimension.y;
         rtvs[0] = resources[Texture::Tile2].rtv.get();
         srvs[0] = resources[Texture::VelocityBuffer].srv.get();
         device_context->Map(cbuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer);
         memcpy(mapped_buffer.pData, &cb_data, sizeof(cb_data));
         device_context->Unmap(cbuffer.get(), 0);
         ps = device_data.native_pixel_shaders.at(Math::CompileTimeStringHash("Motion Blur TileMax1")).get();
         device_context->PSSetShader(ps, nullptr, 0);
         device_context->OMSetRenderTargets(1, &rtvs[0], nullptr);
         device_context->PSSetShaderResources(0, 1, &srvs[0]);
         device_context->RSSetViewports(1, &viewport);
         device_context->Draw(3, 0);
         
         // Tile4
         cb_data.MainTex_TexelSize.x = resources[Texture::Tile2].dimension.x;
         cb_data.MainTex_TexelSize.y = resources[Texture::Tile2].dimension.y;
         cb_data.MainTex_TexelSize.z = 1.f / cb_data.MainTex_TexelSize.x;
         cb_data.MainTex_TexelSize.w = 1.f / cb_data.MainTex_TexelSize.y;
         viewport.Width = resources[Texture::Tile4].dimension.x;
         viewport.Height = resources[Texture::Tile4].dimension.y;
         rtvs[0] = resources[Texture::Tile4].rtv.get();
         srvs[0] = resources[Texture::Tile2].srv.get();
         device_context->Map(cbuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer);
         memcpy(mapped_buffer.pData, &cb_data, sizeof(cb_data));
         device_context->Unmap(cbuffer.get(), 0);
         ps = device_data.native_pixel_shaders.at(Math::CompileTimeStringHash("Motion Blur TileMax2")).get();
         device_context->PSSetShader(ps, nullptr, 0);
         device_context->OMSetRenderTargets(1, &rtvs[0], nullptr);
         device_context->PSSetShaderResources(0, 1, &srvs[0]);
         device_context->RSSetViewports(1, &viewport);
         device_context->Draw(3, 0);
         
         // Tile8
         cb_data.MainTex_TexelSize.x = resources[Texture::Tile4].dimension.x;
         cb_data.MainTex_TexelSize.y = resources[Texture::Tile4].dimension.y;
         cb_data.MainTex_TexelSize.z = 1.f / cb_data.MainTex_TexelSize.x;
         cb_data.MainTex_TexelSize.w = 1.f / cb_data.MainTex_TexelSize.y;
         viewport.Width = resources[Texture::Tile8].dimension.x;
         viewport.Height = resources[Texture::Tile8].dimension.y;
         rtvs[0] = resources[Texture::Tile8].rtv.get();
         srvs[0] = resources[Texture::Tile4].srv.get();
         device_context->Map(cbuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer);
         memcpy(mapped_buffer.pData, &cb_data, sizeof(cb_data));
         device_context->Unmap(cbuffer.get(), 0);
         ps = device_data.native_pixel_shaders.at(Math::CompileTimeStringHash("Motion Blur TileMax2")).get();
         device_context->PSSetShader(ps, nullptr, 0);
         device_context->OMSetRenderTargets(1, &rtvs[0], nullptr);
         device_context->PSSetShaderResources(0, 1, &srvs[0]);
         device_context->RSSetViewports(1, &viewport);
         device_context->Draw(3, 0);
         
         // TileMax
         cb_data.MainTex_TexelSize.x = resources[Texture::Tile8].dimension.x;
         cb_data.MainTex_TexelSize.y = resources[Texture::Tile8].dimension.y;
         cb_data.MainTex_TexelSize.z = 1.f / cb_data.MainTex_TexelSize.x;
         cb_data.MainTex_TexelSize.w = 1.f / cb_data.MainTex_TexelSize.y;
         viewport.Width = resources[Texture::TileMax].dimension.x;
         viewport.Height = resources[Texture::TileMax].dimension.y;
         rtvs[0] = resources[Texture::TileMax].rtv.get();
         srvs[0] = resources[Texture::Tile8].srv.get();
         device_context->Map(cbuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer);
         memcpy(mapped_buffer.pData, &cb_data, sizeof(cb_data));
         device_context->Unmap(cbuffer.get(), 0);
         ps = device_data.native_pixel_shaders.at(Math::CompileTimeStringHash("Motion Blur TileMaxV")).get();
         device_context->PSSetShader(ps, nullptr, 0);
         device_context->OMSetRenderTargets(1, &rtvs[0], nullptr);
         device_context->PSSetShaderResources(0, 1, &srvs[0]);
         device_context->RSSetViewports(1, &viewport);
         device_context->Draw(3, 0);
         
         // TileNeighborMax
         cb_data.MainTex_TexelSize.x = resources[Texture::TileMax].dimension.x;
         cb_data.MainTex_TexelSize.y = resources[Texture::TileMax].dimension.y;
         cb_data.MainTex_TexelSize.z = 1.f / cb_data.MainTex_TexelSize.x;
         cb_data.MainTex_TexelSize.w = 1.f / cb_data.MainTex_TexelSize.y;
         viewport.Width = resources[Texture::TileNeighborMax].dimension.x;
         viewport.Height = resources[Texture::TileNeighborMax].dimension.y;
         rtvs[0] = resources[Texture::TileNeighborMax].rtv.get();
         srvs[0] = resources[Texture::TileMax].srv.get();
         device_context->Map(cbuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer);
         memcpy(mapped_buffer.pData, &cb_data, sizeof(cb_data));
         device_context->Unmap(cbuffer.get(), 0);
         ps = device_data.native_pixel_shaders.at(Math::CompileTimeStringHash("Motion Blur NeighborMax")).get();
         device_context->PSSetShader(ps, nullptr, 0);
         device_context->OMSetRenderTargets(1, &rtvs[0], nullptr);
         device_context->PSSetShaderResources(0, 1, &srvs[0]);
         device_context->RSSetViewports(1, &viewport);
         device_context->Draw(3, 0);
         
         // Reconstruction
         cb_data.MainTex_TexelSize.x = static_cast<float>(data.width);
         cb_data.MainTex_TexelSize.y = static_cast<float>(data.height);
         cb_data.MainTex_TexelSize.z = 1.f / cb_data.MainTex_TexelSize.x;
         cb_data.MainTex_TexelSize.w = 1.f / cb_data.MainTex_TexelSize.y;
         rtvs[0] = data.output_rtv;
         srvs[0] = data.input_color_srv;
         srvs[1] = resources[Texture::TileNeighborMax].srv.get();
         srvs[2] = resources[Texture::VelocityBuffer].srv.get();
         viewport.Width = static_cast<float>(data.width);
         viewport.Height = static_cast<float>(data.height);
         device_context->Map(cbuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer);
         memcpy(mapped_buffer.pData, &cb_data, sizeof(cb_data));
         device_context->Unmap(cbuffer.get(), 0);
         ps = device_data.native_pixel_shaders.at(Math::CompileTimeStringHash("Motion Blur Reconstruction")).get();
         device_context->PSSetShader(ps, nullptr, 0);
         device_context->OMSetRenderTargets(1, &rtvs[0], nullptr);
         device_context->PSSetShaderResources(0, 3, &srvs[0]);
         device_context->RSSetViewports(1, &viewport);
         device_context->Draw(3, 0);
      }

   private:
      void InitCBuffer(const DrawData& data, CBufferData& cbuffer)
      {
         const float2 resolution = {static_cast<float>(data.width), static_cast<float>(data.height)};
         const float2 v_resolution = {static_cast<float>(data.velocity_tex_width), static_cast<float>(data.velocity_tex_height)};
         const float kMaxBlurRadius = 5.f;
         int maxBlurPixels = static_cast<int>(kMaxBlurRadius * resolution.y / 100.f);
         int tileSize = ((maxBlurPixels - 1) / 8 + 1) * 8;
      
         cbuffer.VelocityScale = data.shutter_angle / 360.f;
         cbuffer.MaxBlurRadius = static_cast<float>(tileSize);
         cbuffer.RcpMaxBlurRadius = 1.f / static_cast<float>(tileSize);
         cbuffer.LoopCount = static_cast<float>(std::clamp(data.sample_count / 2, 1, 64));
      
         cbuffer.TileMaxOffs.x = (static_cast<float>(tileSize) / 8.f - 1.f) * -0.5f;
         cbuffer.TileMaxOffs.y = cbuffer.TileMaxOffs.x;
         cbuffer.TileMaxLoop = static_cast<float>(tileSize / 8);
      
         cbuffer.CameraMotionVectorsTexture_TexelSize.x = v_resolution.x;
         cbuffer.CameraMotionVectorsTexture_TexelSize.y = v_resolution.y;
         cbuffer.CameraMotionVectorsTexture_TexelSize.z = 1.f / v_resolution.x;
         cbuffer.CameraMotionVectorsTexture_TexelSize.w = 1.f / v_resolution.y;
      
         cbuffer.NeighborMaxTex_TexelSize.x = 1.f / static_cast<float>((data.velocity_tex_width + tileSize - 1) / tileSize);
         cbuffer.NeighborMaxTex_TexelSize.y = 1.f / static_cast<float>((data.velocity_tex_height + tileSize - 1) / tileSize);
         cbuffer.VelocityTex_TexelSize.x = 1.f / v_resolution.x;
         cbuffer.VelocityTex_TexelSize.y = 1.f / v_resolution.y;
      
         if (!data.z_reversed)
         {
            cbuffer.DepthParams.x = 1.0f / data.z_far - 1.0f / data.z_near;
            cbuffer.DepthParams.y = 1.0f / data.z_near;
            cbuffer.DepthParams.z = data.z_far - data.z_near;
         }
         else
         {
            cbuffer.DepthParams.x = 1.0f / data.z_near - 1.0f / data.z_far;
            cbuffer.DepthParams.y = 1.0f / data.z_far;
            cbuffer.DepthParams.z = data.z_far - data.z_near;
         }
      }
      
      void SetupTextures(ID3D11Device* device, uint32_t width, uint32_t height)
      {
         if (width == 0 || height == 0)
            return;

         if (resources[Texture::VelocityBuffer].tex.get())
         {
            if (cached_width == width && cached_height == height)
               return;
         }
         
         //reshade::log::message(reshade::log::level::info, "Motion Blur: creating textures.");
         
         {
            auto CreateResource = [&](Texture t, const D3D11_TEXTURE2D_DESC& desc)
            {
               auto& r = resources[t];

               D3D11_RENDER_TARGET_VIEW_DESC rtv_desc{};
               rtv_desc.Format = desc.Format;
               rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
               rtv_desc.Texture2D.MipSlice = 0;

               D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
               srv_desc.Format = desc.Format;
               srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
               srv_desc.Texture2D.MostDetailedMip = 0;
               srv_desc.Texture2D.MipLevels = 1;

               device->CreateTexture2D(&desc, nullptr, r.tex.put());
               device->CreateRenderTargetView(r.tex.get(), &rtv_desc, r.rtv.put());
               device->CreateShaderResourceView(r.tex.get(), &srv_desc, r.srv.put());
               r.dimension.x = static_cast<float>(desc.Width);
               r.dimension.y = static_cast<float>(desc.Height);
            };
            
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = width;
            desc.Height = height;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
            desc.SampleDesc.Count = 1;
            desc.SampleDesc.Quality = 0;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            desc.CPUAccessFlags = 0;
            desc.MiscFlags = 0;
            desc.MipLevels = 1;
            
            CreateResource(Texture::VelocityBuffer, desc);
            
            desc.Width = (width + 1) / 2;
            desc.Height = (height + 1) / 2;
            desc.Format = DXGI_FORMAT_R16G16_FLOAT;
            CreateResource(Texture::Tile2, desc);
            
            desc.Width = (width + 3) / 4;
            desc.Height = (height + 3) / 4;
            CreateResource(Texture::Tile4, desc);
            
            desc.Width = (width + 7) / 8;
            desc.Height = (height + 7) / 8;
            CreateResource(Texture::Tile8, desc);
            
            const float kMaxBlurRadius = 5.f;
            int maxBlurPixels = static_cast<int>(kMaxBlurRadius * static_cast<float>(height) / 100.f);
            int tileSize = ((maxBlurPixels - 1) / 8 + 1) * 8;
            desc.Width = (width + tileSize - 1) / tileSize;
            desc.Height = (height + tileSize - 1) / tileSize;
            CreateResource(Texture::TileMax, desc);
            CreateResource(Texture::TileNeighborMax, desc);
            
            cached_width = width;
            cached_height = height;
         }
      }
      
      EnumArray<Resource, Texture> resources;

      ComPtr<ID3D11Buffer> cbuffer;
      ComPtr<ID3D11SamplerState> linear_sampler;
      ComPtr<ID3D11SamplerState> point_sampler;

      uint32_t cached_width = 0;
      uint32_t cached_height = 0;
   };
}

namespace TemporalAADepth
{
   template <typename T, typename Enum>
   struct EnumArray
   {
      std::array<T, static_cast<size_t>(Enum::Count)> data;

      T& operator[](Enum e)
      {
         return data[static_cast<size_t>(e)];
      }

      const T& operator[](Enum e) const
      {
         return data[static_cast<size_t>(e)];
      }
   };
   
   enum class Texture
   {
      DepthHistoryRead,
      DepthHistoryWrite,
      Count,
   };
   
   struct DrawData
   {
      ID3D11ShaderResourceView* input_mv_srv = nullptr;
      ID3D11ShaderResourceView* input_depth_srv = nullptr;
      int width = 0;
      int height = 0;
      bool use_variance_clip = true;
      float variance_scale = 1.0f;
      float2 velocity_scale = {1.0f, 1.0f};
      bool has_history = false;
   };
   
   struct alignas(16) CBufferData
   {
      float4 ScreenInfo = {0.0, 0.0, 0.0, 0.0};
      int UseVarianceClipping;
      float VarianceScale;
      float2 VelocityScale = {0.0, 0.0};
   };
   
   struct Resource
   {
      ComPtr<ID3D11Texture2D> tex;
      ComPtr<ID3D11UnorderedAccessView> uav;
      ComPtr<ID3D11ShaderResourceView> srv;
      ComPtr<ID3D11RenderTargetView> rtv;
      float2 dimension = {0.0, 0.0};
   };
   
   class TemporalAADepthPass
   {
   public:
      void Draw(ID3D11Device* device, ID3D11DeviceContext* device_context, const DeviceData& device_data, const DrawData& data)
      {
         // Init CBuffer
         CBufferData cb_data;
         InitCBuffer(data, cb_data);
         
         // Create CB
         if (!cbuffer.get())
         {
            D3D11_BUFFER_DESC bd;
            bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            bd.ByteWidth = sizeof(CBufferData);
            bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            bd.MiscFlags = 0;
            bd.StructureByteStride = 0;
            bd.Usage = D3D11_USAGE_DYNAMIC;
            device->CreateBuffer(&bd, nullptr, cbuffer.put());
         }
         
         // Create Samplers
         if (!linear_sampler.get())
         {
            D3D11_SAMPLER_DESC sampler_desc = {};

            sampler_desc.Filter = D3D11_FILTER::D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
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
            
            device->CreateSamplerState(&sampler_desc, linear_sampler.put());
         }
         if (!point_sampler.get())
         {
            D3D11_SAMPLER_DESC sampler_desc = {};

            sampler_desc.Filter = D3D11_FILTER::D3D11_FILTER_MIN_MAG_MIP_POINT;
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
            
            device->CreateSamplerState(&sampler_desc, point_sampler.put());
         }
         
         // Create Textures
         SetupTextures(device, data.width, data.height);
         
         ID3D11SamplerState* cs_samplers[2] = {linear_sampler.get(), point_sampler.get()};
         ID3D11ShaderResourceView* srvs[3] = {data.input_depth_srv, resources[Texture::DepthHistoryRead].srv.get(), data.input_mv_srv};
         ID3D11UnorderedAccessView* uavs[1] = {resources[Texture::DepthHistoryWrite].uav.get()};
         ID3D11Buffer* cbvs[] = {cbuffer.get()};
         const auto shader_hash = data.has_history ? Math::CompileTimeStringHash("Temporal AA Depth With History") : Math::CompileTimeStringHash("Temporal AA Depth Without History");
         ID3D11ComputeShader* cs = device_data.native_compute_shaders.at(shader_hash).get();
         
         D3D11_MAPPED_SUBRESOURCE mapped_buffer;
         device_context->Map(cbuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer);
         memcpy(mapped_buffer.pData, &cb_data, sizeof(cb_data));
         device_context->Unmap(cbuffer.get(), 0);
         
         device_context->CSSetShader(cs,nullptr,0);
         device_context->CSSetSamplers(7, 2, &cs_samplers[0]);
         device_context->CSSetConstantBuffers(0, 1, &cbvs[0]);
         device_context->CSSetShaderResources(0, 3, srvs);
         device_context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
         device_context->Dispatch((data.width + 7) / 8, (data.height + 7) / 8, 1);
         
         //device_context->CopyResource(resources[Texture::DepthHistoryRead].tex.get(), resources[Texture::DepthHistoryWrite].tex.get());
         std::swap(resources[Texture::DepthHistoryRead], resources[Texture::DepthHistoryWrite]);
      }

      EnumArray<Resource, Texture> resources;

   private:
      void InitCBuffer(const DrawData& data, CBufferData& cbuffer)
      {
         const float2 resolution = {static_cast<float>(data.width), static_cast<float>(data.height)};
      
         cbuffer.ScreenInfo.x = resolution.x;
         cbuffer.ScreenInfo.y = resolution.y;
         cbuffer.ScreenInfo.z = 1.f / resolution.x;
         cbuffer.ScreenInfo.w = 1.f / resolution.y;
         cbuffer.UseVarianceClipping = data.use_variance_clip ? 1 : 0;
         cbuffer.VarianceScale = data.variance_scale;
         cbuffer.VelocityScale = data.velocity_scale;
      }
      
      void SetupTextures(ID3D11Device* device, uint32_t width, uint32_t height)
      {
         if (width == 0 || height == 0)
            return;

         if (resources[Texture::DepthHistoryWrite].tex.get())
         {
            if (cached_width == width && cached_height == height)
               return;
         }
         
         {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = width;
            desc.Height = height;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R32_TYPELESS;
            desc.SampleDesc.Count = 1;
            desc.SampleDesc.Quality = 0;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            desc.CPUAccessFlags = 0;
            desc.MiscFlags = 0;
            desc.MipLevels = 1;
            
            {
               auto& r = resources[Texture::DepthHistoryWrite];

               D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
               srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
               srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
               srv_desc.Texture2D.MostDetailedMip = 0;
               srv_desc.Texture2D.MipLevels = 1;
               
               D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
               uav_desc.Format = DXGI_FORMAT_R32_FLOAT;
               uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
               uav_desc.Texture2D.MipSlice = 0;

               device->CreateTexture2D(&desc, nullptr, r.tex.put());
               device->CreateShaderResourceView(r.tex.get(), &srv_desc, r.srv.put());
               device->CreateUnorderedAccessView(r.tex.get(), &uav_desc, r.uav.put());
               r.dimension.x = static_cast<float>(desc.Width);
               r.dimension.y = static_cast<float>(desc.Height);
            }
            
            {
               auto& r = resources[Texture::DepthHistoryRead];

               D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
               srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
               srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
               srv_desc.Texture2D.MostDetailedMip = 0;
               srv_desc.Texture2D.MipLevels = 1;
               
               D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
               uav_desc.Format = DXGI_FORMAT_R32_FLOAT;
               uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
               uav_desc.Texture2D.MipSlice = 0;

               device->CreateTexture2D(&desc, nullptr, r.tex.put());
               device->CreateShaderResourceView(r.tex.get(), &srv_desc, r.srv.put());
               device->CreateUnorderedAccessView(r.tex.get(), &uav_desc, r.uav.put());
               r.dimension.x = static_cast<float>(desc.Width);
               r.dimension.y = static_cast<float>(desc.Height);
            }

            cached_width = width;
            cached_height = height;
         }
      }

      ComPtr<ID3D11Buffer> cbuffer;
      ComPtr<ID3D11SamplerState> linear_sampler;
      ComPtr<ID3D11SamplerState> point_sampler;

      uint32_t cached_width = 0;
      uint32_t cached_height = 0;
   };
}

M_INLINE float3 TransformPoint(const float4 m[3], const float3& b)
{
   float3 v;
   v.x = m[0].x * b.x + m[0].y * b.y + m[0].z * b.z + m[0].w;
   v.y = m[1].x * b.x + m[1].y * b.y + m[1].z * b.z + m[1].w;
   v.z = m[2].x * b.x + m[2].y * b.y + m[2].z * b.z + m[2].w;
   return v;
}

float2 projection_jitters = {0, 0};
float2 prev_projection_jitters = {0, 0};
float2 output_resolution = {0, 0};
int counter = 0;
bool is_search_mode_on = false;
bool is_in_battle_mode = false;
bool is_hatching_on = false;

namespace
{
   ShaderHashesList shader_hashes_fog;
   ShaderHashesList shader_hashes_copy;
   ShaderHashesList shader_hashes_effect_merge;
   ShaderHashesList shader_hashes_vertex;
   ShaderHashesList shader_hashes_copy_red;
   ShaderHashesList shader_hashes_search_mode;
   ShaderHashesList shader_hashes_hatching_gbuffer;
   ShaderHashesList shader_hashes_hatching_forward;
   ShaderHashesList shader_hashes_motion_blur;
   ShaderHashesList shader_hashes_dof_merge;
   ShaderHashesList shader_hashes_postfx_composite;
   ShaderHashesList shader_hashes_postfx_composite_with_depth;
   
   ShaderHashesList shader_hashes_L2P_vertex;
   
   CameraMatrices camera_matrices_current;
   CameraMatrices camera_matrices_previous;
   
   DirectX::XMMATRIX ComputeCameraSpaceToPreviousProjectedSpaceMatrix()
   {
      auto& current = camera_matrices_current;
      auto& previous = camera_matrices_previous;
      
      float4 position_delta = {0.0,0.0,0.0,1.0};
      position_delta.x = current.inv_view_matrix.r[3].m128_f32[0] - previous.inv_view_matrix.r[3].m128_f32[0];
      position_delta.y = current.inv_view_matrix.r[3].m128_f32[1] - previous.inv_view_matrix.r[3].m128_f32[1];
      position_delta.z = current.inv_view_matrix.r[3].m128_f32[2] - previous.inv_view_matrix.r[3].m128_f32[2];
      
      DirectX::XMMATRIX previous_rotation_matrix = previous.inv_view_matrix;
      previous_rotation_matrix.r[0].m128_f32[3] = 0.0;
      previous_rotation_matrix.r[1].m128_f32[3] = 0.0;
      previous_rotation_matrix.r[2].m128_f32[3] = 0.0;
      previous_rotation_matrix.r[3] = DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
      previous_rotation_matrix = DirectX::XMMatrixTranspose(previous_rotation_matrix);
      
      DirectX::XMMATRIX current_rotation_matrix = current.inv_view_matrix;
      current_rotation_matrix.r[0].m128_f32[3] = 0.0;
      current_rotation_matrix.r[1].m128_f32[3] = 0.0;
      current_rotation_matrix.r[2].m128_f32[3] = 0.0;
      current_rotation_matrix.r[3] = DirectX::XMVectorSet(position_delta.x, position_delta.y, position_delta.z, 1.0f);
      
      DirectX::XMMATRIX temp = DirectX::XMMatrixMultiply(previous_rotation_matrix, previous.projection_matrix);
      DirectX::XMMATRIX CameraSpaceToPreviousProjectedSpace = XMMatrixMultiply(current_rotation_matrix, temp);
      
      /*
      LogXMMatrix("ProjectionMatrix", current.projection_matrix);
      LogXMMatrix("CurrentInvViewMatrix", current.inv_view_matrix);
      LogXMMatrix("PreviousInvViewMatrix", previous.inv_view_matrix);
      LogXMMatrix("CurrentInvViewMatrix_CurrWithDelta", current_rotation_matrix);
      LogXMMatrix("PreviousInvViewMatrix_PrevRotationOnly", previous_rotation_matrix);
      LogXMMatrix("CameraSpaceToPreviousProjectedSpace", CameraSpaceToPreviousProjectedSpace);
      */
      
      return CameraSpaceToPreviousProjectedSpace;
   }
   
#include "includes/shader_local_to_world_offset_map.h"
}

struct GameDeviceDataBlueReflectionSecondLight final : public GameDeviceData
{
   // resources used to identify the deferred context used for scene drawing
   std::atomic<ID3D11CommandList*> remainder_command_list;
   std::atomic<ID3D11DeviceContext*> draw_device_context = nullptr;

   // textures we got from the game
   ComPtr<ID3D11Texture2D> source_color;
   ComPtr<ID3D11ShaderResourceView> source_color_srv;
   ComPtr<ID3D11RenderTargetView> source_color_rtv;
   
   ComPtr<ID3D11Texture2D> postfx_source_color;
   ComPtr<ID3D11ShaderResourceView> postfx_source_color_srv;
   
   ComPtr<ID3D11Texture2D> motion_vectors;
   ComPtr<ID3D11RenderTargetView> motion_vectors_rtv;
   ComPtr<ID3D11ShaderResourceView> motion_vectors_srv;
   
   ComPtr<ID3D11Texture2D> decoded_motion_vectors;
   ComPtr<ID3D11RenderTargetView> decoded_motion_vectors_rtv;
   ComPtr<ID3D11ShaderResourceView> decoded_motion_vectors_srv;
   
   ComPtr<ID3D11Texture2D> depth_texture;
   ComPtr<ID3D11ShaderResourceView> depth_texture_srv;
   
   ComPtr<ID3D11Texture2D> resolve_texture;
   
   ComPtr<ID3D11Texture2D> frame_color_texture;
   ComPtr<ID3D11ShaderResourceView> frame_color_texture_srv;

   // the command list we split to interject dlss
   ComPtr<ID3D11CommandList> partial_command_list;
   ComPtr<ID3D11CommandList> pre_search_mode_command_list;
   ComPtr<ID3D11CommandList> post_search_mode_command_list;
   
   SearchModeFilterRenderState search_mode_filter_render_state;

   /*
   // The game uses cbuffer skinning matrices instead of pre transform vertices
   // These might not be needed
   ComPtr<ID3D11Buffer> skin_cache;
   std::unique_ptr<StretchyBuffer> prev_skin_buffer;
   std::unique_ptr<StretchyBuffer> skin_buffer;
   std::unordered_map<ID3D11Buffer*, SkinCacheEntry> prev_skin_lookup;
   std::unordered_map<ID3D11Buffer*, SkinCacheEntry> skin_lookup;
   */
   
   StretchyCpuBuffer g_shadow_memory;
   //StretchyCpuBuffer g_prev_shadow_memory;
   std::unordered_map<uint64_t, std::vector<CBufferEntry>> prev_global_cbuffer_lookup;
   std::unordered_map<uint64_t, std::vector<CBufferEntry>> global_cbuffer_lookup;
   // spatial index: for each draw_call_hash, map quantized world-position key -> index into prev_global_cbuffer_lookup vector
   std::unordered_map<uint64_t, std::unordered_map<uint64_t, uint32_t>> prev_global_cbuffer_spatial_index;
   std::unordered_map<ID3D11DeviceContext*, ComPtr<ID3D11DeviceContext1>> ctx1_cache;
   std::unordered_map<ID3D11Buffer*, PendingCBufferData> g_pending_cb_data;
   StretchyGpuBuffer g_cbuffer;
   
   ComPtr<ID3D11Buffer> modifiable_vertex_buffer;
   ComPtr<ID3D11Buffer> modifiable_inedx_buffer;
   
   std::unordered_map<uint32_t, com_ptr<ID3D11VertexShader>> modified_vertex_shaders;
   std::unordered_map<uint32_t, com_ptr<ID3D11PixelShader>> modified_pixel_shaders;
   std::unordered_map<uint32_t, com_ptr<ID3D11VertexShader>> original_vertex_shaders;
   std::unordered_map<uint32_t, com_ptr<ID3D11PixelShader>> original_pixel_shaders;
   
   float2 render_resolution;
   FramePhase frame_phase = FramePhase::NONE;
   
   DirectX::XMMATRIX inv_view_matrix;
   DirectX::XMMATRIX inv_projection_matrix;
   DirectX::XMMATRIX inv_view_projection_matrix;
   DirectX::XMMATRIX prev_view_projection_matrix;
   DirectX::XMMATRIX reprojection_matrix;
   
   bool has_drawn_fog;
   bool has_copied_fog_to_main_rt;
   bool has_drawn_search_mode;
   bool has_drawn_upscaling;
   bool has_replaced_motion_blur;
   
   MotionBlur::MotionBlurPass motion_blur_pass;
   TemporalAADepth::TemporalAADepthPass temporal_depth_pass;
   
   std::array<ID3D11RenderTargetView*, 8> current_rtvs = {};
   ID3D11DepthStencilView* current_dsv = nullptr;
   
   void CleanMVResources()
   {
      motion_vectors.reset();
      motion_vectors_rtv.reset();
      motion_vectors_srv.reset();
      
      decoded_motion_vectors.reset();
      decoded_motion_vectors_rtv.reset();
      decoded_motion_vectors_srv.reset();
      
      resolve_texture.reset();
      
      frame_color_texture.reset();
      frame_color_texture_srv.reset();
   }
   

   void Destroy(DeviceData& device_data)
   {
      // Atomics holding raw COM pointers
      if (auto* cmd = remainder_command_list.exchange(nullptr))
         cmd->Release();

      if (auto* ctx = draw_device_context.exchange(nullptr))
         ctx->Release();

      CleanMVResources();

      source_color.reset();
      source_color_srv.reset();
      source_color_rtv.reset();

      postfx_source_color.reset();
      postfx_source_color_srv.reset();

      depth_texture.reset();
      depth_texture_srv.reset();

      partial_command_list.reset();
      pre_search_mode_command_list.reset();
      post_search_mode_command_list.reset();

      modifiable_vertex_buffer.reset();
      modifiable_inedx_buffer.reset();

      // Manually release upgraded samplers to avoid hang
      if (enable_samplers_upgrade)
      {
         decltype(device_data.custom_sampler_by_original_sampler) samplers;

         {
            std::lock_guard lock(s_mutex_samplers);
            samplers = std::move(device_data.custom_sampler_by_original_sampler);
            device_data.custom_sampler_by_original_sampler.clear();
         }
      }
   }
};

class BlueReflectionSecondLight final : public Game
{
   static GameDeviceDataBlueReflectionSecondLight& GetGameDeviceData(DeviceData& device_data)
   {
      return *static_cast<GameDeviceDataBlueReflectionSecondLight*>(device_data.game);
   }

   static const GameDeviceDataBlueReflectionSecondLight& GetGameDeviceData(const DeviceData& device_data)
   {
      return *static_cast<const GameDeviceDataBlueReflectionSecondLight*>(device_data.game);
   }

public:
   void OnInit(bool async) override
   {
      HMODULE engine_module = nullptr;
      while (!engine_module)
      {
         engine_module = GetModuleHandleA("BLUE REFLECTION Second Light.exe");
         Sleep(100);
      }
      auto base_addr = (uintptr_t)engine_module;
      auto dos_header = reinterpret_cast<PIMAGE_DOS_HEADER>(engine_module);
      auto nt_headers = reinterpret_cast<PIMAGE_NT_HEADERS>(reinterpret_cast<std::byte*>(engine_module) + dos_header->e_lfanew);
      std::size_t section_size = nt_headers->OptionalHeader.SizeOfImage;
      
      RenderResolution = base_addr + 0x18C28E0;
      
      //BLUE REFLECTION Second Light.exe + 0x19C16C8
      //Pointer to a resource container that holds Owner|RefCount|ID3D11Texture|ID3D11ShaderResource

      auto WILDCARD = System::BytePattern(System::BytePattern::WildcardType::Wildcard);

      std::vector<System::BytePattern> pattern = {
         0x40, 0x53, 0x48, 0x83, 0xEC,
         WILDCARD,
         0xF3, 0x0F, 0x59, 0x0D,
         WILDCARD, WILDCARD, WILDCARD, WILDCARD,
         0x48, 0x8B, 0xD9, 0x0F, 0x29, 0x74, 0x24
      };

      auto results = System::ScanMemoryForPattern(
         reinterpret_cast<std::byte*>(engine_module),
         section_size,
         pattern
         );

      if (!results.empty() && !g_compute_projection_matrix_hook)
      {
         void* fn = reinterpret_cast<void*>(results[0]);

         g_compute_projection_matrix_hook = safetyhook::create_inline(
            fn,
            Hooked_ComputeProjectionMatrix
            );
      }

      if (!g_camera_fieldmap_view_proj_matrix_hook)
      {
         void* fn = reinterpret_cast<void*>(base_addr + 0x4E2580);

         g_camera_fieldmap_view_proj_matrix_hook = safetyhook::create_inline(
            fn,
            Hooked_CameraUpdateFieldMap3DHUD
            );
      }

      if (!g_battle_camera_hook)
      {
         void* fn = reinterpret_cast<void*>(base_addr + 0x641600);

         g_battle_camera_hook = safetyhook::create_inline(
            fn,
            Hooked_CameraUpdateFunctionSceneAnd3DHUD
            );
      }
      
      if (!g_active_camera_hook)
      {
         void* fn = reinterpret_cast<void*>(base_addr + 0x69B780);

         g_active_camera_hook = safetyhook::create_inline(
            fn,
            Hooked_UpdateActiveCameraAddress
            );
      }
      
      if (!g_search_mode_hook)
      {
         void* fn = reinterpret_cast<void*>(base_addr + 0x259FB0);

         g_search_mode_hook = safetyhook::create_inline(
            fn,
            PostEffectSearchModeRendererPrepare
            );
      }
      
      if (!g_postfx_hatching_hook)
      {
         void* fn = reinterpret_cast<void*>(base_addr + 0x259A30);

         g_postfx_hatching_hook = safetyhook::create_inline(
            fn,
            PostEffectHatchingRendererPrepare
            );
      }
      
      if (!g_battle_mode_hook)
      {
         void* fn = reinterpret_cast<void*>(base_addr + 0x2EDBC0);

         g_battle_mode_hook = safetyhook::create_inline(
            fn,
            Hooked_BattleMainLoopIterate
            );
      }
      
      CombineViewProjectionMatrices = reinterpret_cast<fnCombineViewProjectionMatrices>(base_addr + 0x46DAE0);
      GetSettingValue = reinterpret_cast<fnGetSettingValue>(base_addr + 0x29850);   // index 15 is motion blur
      
      //native_shaders_definitions.emplace(CompileTimeStringHash("River Clear Velocity"), ShaderDefinition{"SRIVER-GBufferNormalMap", reshade::api::pipeline_subobject_type::pixel_shader});
      native_shaders_definitions.emplace(CompileTimeStringHash("Effect Merge Test"), ShaderDefinition{"Luma_EffectMerge_OutputVelocity", reshade::api::pipeline_subobject_type::pixel_shader});
      native_shaders_definitions.emplace(CompileTimeStringHash("Search Mode Filter"), ShaderDefinition{"Luma_PostEffectSearchMode", reshade::api::pipeline_subobject_type::pixel_shader});
      native_shaders_definitions.emplace(CompileTimeStringHash("Motion Blur Passthrough"), ShaderDefinition{"Luma_PostEffectMotionBlurWithDT", reshade::api::pipeline_subobject_type::pixel_shader});
      //native_shaders_definitions.emplace(CompileTimeStringHash("Decode Motion Vector"), ShaderDefinition{"Luma_DecodeMotionVector", reshade::api::pipeline_subobject_type::compute_shader});
      
      native_shaders_definitions.emplace(CompileTimeStringHash("Motion Blur Vertex"), 
         ShaderDefinition{"Luma_PostEffectMotionBlur", reshade::api::pipeline_subobject_type::vertex_shader, nullptr, nullptr, 
            {{"VERTEXSHADER", "1"}}});
      
      native_shaders_definitions.emplace(CompileTimeStringHash("Motion Blur Velocity Setup"), 
         ShaderDefinition{"Luma_PostEffectMotionBlur", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr, 
            {{"VELOCITY_SETUP", "1"}}});
      
      native_shaders_definitions.emplace(CompileTimeStringHash("Motion Blur TileMax1"), 
         ShaderDefinition{"Luma_PostEffectMotionBlur", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr, 
            {{"TILEMAX_1", "1"}}});
      
      native_shaders_definitions.emplace(CompileTimeStringHash("Motion Blur TileMax2"), 
         ShaderDefinition{"Luma_PostEffectMotionBlur", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr, 
            {{"TILEMAX_2", "1"}}});
      
      native_shaders_definitions.emplace(CompileTimeStringHash("Motion Blur TileMaxV"), 
         ShaderDefinition{"Luma_PostEffectMotionBlur", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr, 
            {{"TILEMAX_V", "1"}}});
      
      native_shaders_definitions.emplace(CompileTimeStringHash("Motion Blur NeighborMax"), 
         ShaderDefinition{"Luma_PostEffectMotionBlur", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr, 
            {{"NEIGHBOR_MAX", "1"}}});
      
      native_shaders_definitions.emplace(CompileTimeStringHash("Motion Blur Reconstruction"), 
         ShaderDefinition{"Luma_PostEffectMotionBlur", reshade::api::pipeline_subobject_type::pixel_shader, nullptr, nullptr, 
            {{"RECONSTRUCTION", "1"}}});
      
      native_shaders_definitions.emplace(CompileTimeStringHash("Temporal AA Depth With History"), 
         ShaderDefinition{"Luma_TemporalAADepth", reshade::api::pipeline_subobject_type::compute_shader, nullptr, nullptr, 
            {{"HAS_PREVIOUS_FRAME", "1"}}});
      
      native_shaders_definitions.emplace(CompileTimeStringHash("Temporal AA Depth Without History"), 
         ShaderDefinition{"Luma_TemporalAADepth", reshade::api::pipeline_subobject_type::compute_shader, nullptr, nullptr, 
            {}});
      
      reshade::register_event<reshade::addon_event::bind_render_targets_and_depth_stencil >(BlueReflectionSecondLight::OnBindRenderTargetsAndDepthStencil);
      reshade::register_event<reshade::addon_event::clear_render_target_view>(BlueReflectionSecondLight::OnClearRenderTargetView);
      reshade::register_event<reshade::addon_event::copy_texture_region>(BlueReflectionSecondLight::OnCopyTextureRegion);
      reshade::register_event<reshade::addon_event::unmap_buffer_region>(BlueReflectionSecondLight::OnUnmapBufferRegion);
      reshade::register_event<reshade::addon_event::map_buffer_region>(BlueReflectionSecondLight::OnMapBufferRegion);
      reshade::register_event<reshade::addon_event::create_pipeline>(BlueReflectionSecondLight::OnCreatePipeline);
      reshade::register_event<reshade::addon_event::execute_secondary_command_list>(BlueReflectionSecondLight::OnExecuteSecondaryCommandList);
      reshade::register_event<reshade::addon_event::destroy_resource >(BlueReflectionSecondLight::OnDestroyResource);
   }

   void OnLoad(std::filesystem::path& file_path, bool failed) override
   {
      if (!failed)
      {
      }
   }

   void OnCreateDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      device_data.game = new GameDeviceDataBlueReflectionSecondLight;
   }
   
   void OnInitDevice(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      
      /*
      ComPtr<ID3D11DeviceContext> context;
      native_device->GetImmediateContext(context.put());
      */
      game_device_data.g_shadow_memory.Resize(32 * 1024 * 1024);
      //game_device_data.g_prev_shadow_memory.Resize(32 * 1024 * 1024);
      game_device_data.g_cbuffer.Init(native_device, 32 * 1024 * 1024);
   }
   
   static void SetupPostFXTexture(ID3D11Device* device, GameDeviceDataBlueReflectionSecondLight& game_device_data, uint32_t width, uint32_t height)
   {
      if (width == 0 ||
          height == 0)
      {
         return;
      }
      if (game_device_data.postfx_source_color.get() != nullptr)
      {
         D3D11_TEXTURE2D_DESC desc;
         game_device_data.postfx_source_color->GetDesc(&desc);
         if (desc.Width == width &&
             desc.Height == height)
         {
            return;
         }
      }
      {
         D3D11_TEXTURE2D_DESC desc;
         desc.Width = width;
         desc.Height = height;
         desc.Usage = D3D11_USAGE_DEFAULT;
         desc.ArraySize = 1;
         desc.Format = DXGI_FORMAT_B8G8R8A8_TYPELESS;
         desc.SampleDesc.Count = 1;
         desc.SampleDesc.Quality = 0;
         desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
         desc.CPUAccessFlags = 0;
         desc.MiscFlags = 0;
         desc.MipLevels = 1;

         device->CreateTexture2D(&desc,
            nullptr,
            game_device_data.postfx_source_color.put());
      }
      {
         D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
         srv_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
         srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
         srv_desc.Texture2D.MostDetailedMip = 0;
         srv_desc.Texture2D.MipLevels = 1;

         device->CreateShaderResourceView(game_device_data.postfx_source_color.get(),
            &srv_desc,
            game_device_data.postfx_source_color_srv.put());
      }
   }
   
   static void SetupMotionVectorTexture(ID3D11Device* device, GameDeviceDataBlueReflectionSecondLight& game_device_data, uint32_t width, uint32_t height)
   {
      if (width == 0 ||
          height == 0)
      {
         return;
      }
      if (game_device_data.motion_vectors.get() != nullptr)
      {
         D3D11_TEXTURE2D_DESC mv_desc;
         game_device_data.motion_vectors->GetDesc(&mv_desc);
         if (mv_desc.Width == width &&
             mv_desc.Height == height)
         {
            return;
         }
      }
      {
         D3D11_TEXTURE2D_DESC motion_vector_desc;
         motion_vector_desc.Width = width;
         motion_vector_desc.Height = height;
         motion_vector_desc.Usage = D3D11_USAGE_DEFAULT;
         motion_vector_desc.ArraySize = 1;
         motion_vector_desc.Format = DXGI_FORMAT_R16G16_FLOAT;
         motion_vector_desc.SampleDesc.Count = 1;
         motion_vector_desc.SampleDesc.Quality = 0;
         motion_vector_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
         motion_vector_desc.CPUAccessFlags = 0;
         motion_vector_desc.MiscFlags = 0;
         motion_vector_desc.MipLevels = 1;

         device->CreateTexture2D(&motion_vector_desc,
            nullptr,
            game_device_data.motion_vectors.put());
         
         motion_vector_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS;
         
         device->CreateTexture2D(&motion_vector_desc,
            nullptr,
            game_device_data.decoded_motion_vectors.put());
      }
      {
         D3D11_TEXTURE2D_DESC desc;
         desc.Width = width;
         desc.Height = height;
         desc.Usage = D3D11_USAGE_DEFAULT;
         desc.ArraySize = 1;
         desc.Format = DXGI_FORMAT_R16G16B16A16_TYPELESS;
         desc.SampleDesc.Count = 1;
         desc.SampleDesc.Quality = 0;
         desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
         desc.CPUAccessFlags = 0;
         desc.MiscFlags = 0;
         desc.MipLevels = 1;

         device->CreateTexture2D(&desc,
            nullptr,
            game_device_data.resolve_texture.put());
         
         device->CreateTexture2D(&desc,
            nullptr,
            game_device_data.frame_color_texture.put());
      }
      {
         D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
         rtv_desc.Format = DXGI_FORMAT_R16G16_FLOAT;
         rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
         rtv_desc.Texture2D.MipSlice = 0;

         device->CreateRenderTargetView(game_device_data.motion_vectors.get(),
            &rtv_desc,
            game_device_data.motion_vectors_rtv.put());
         device->CreateRenderTargetView(game_device_data.decoded_motion_vectors.get(),
            &rtv_desc,
            game_device_data.decoded_motion_vectors_rtv.put());
      }
      {
         D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
         srv_desc.Format = DXGI_FORMAT_R16G16_FLOAT;
         srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
         srv_desc.Texture2D.MostDetailedMip = 0;
         srv_desc.Texture2D.MipLevels = 1;

         device->CreateShaderResourceView(game_device_data.motion_vectors.get(),
            &srv_desc,
            game_device_data.motion_vectors_srv.put());
         device->CreateShaderResourceView(game_device_data.decoded_motion_vectors.get(),
            &srv_desc,
            game_device_data.decoded_motion_vectors_srv.put());
         
         srv_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
         device->CreateShaderResourceView(game_device_data.frame_color_texture.get(),
            &srv_desc,
            game_device_data.frame_color_texture_srv.put());
      }
   }
   
   static uint64_t HashDrawCall(uint64_t pixel_shader, ID3D11Buffer* vertex_buffer, uint32_t vertex_count)
   {
      uint8_t buffer[sizeof(pixel_shader) + sizeof(vertex_buffer) + sizeof(vertex_count)] = {};
      memcpy(&buffer[0], &pixel_shader, sizeof(pixel_shader));
      memcpy(&buffer[sizeof(pixel_shader)], &vertex_buffer, sizeof(vertex_buffer));
      memcpy(&buffer[sizeof(pixel_shader) + sizeof(vertex_buffer)], &vertex_count, sizeof(vertex_count));
      return XXH3_64bits(buffer, sizeof(buffer));
   };
   
   DrawOrDispatchOverrideType OnDrawOrDispatch(ID3D11Device* native_device, ID3D11DeviceContext* native_device_context, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::shader_stage stages, const ShaderHashesList<OneShaderPerPipeline>& original_shader_hashes, bool is_custom_pass, bool& updated_cbuffers, std::function<void()>* original_draw_dispatch_func) override
   {
      if (device_data.sr_type == SR::Type::None)
         return DrawOrDispatchOverrideType::None;
      
      if ((stages & reshade::api::shader_stage::pixel) == 0)
         return DrawOrDispatchOverrideType::None;
      
      auto& game_device_data = GetGameDeviceData(device_data);
      
      if (original_shader_hashes.Contains(shader_hashes_effect_merge))
      {
         if (!game_device_data.has_copied_fog_to_main_rt)
            return DrawOrDispatchOverrideType::None;
         
         if (game_device_data.has_drawn_upscaling)
            return DrawOrDispatchOverrideType::None;
         
         ComPtr<ID3D11RenderTargetView> render_target_views;
         native_device_context->OMGetRenderTargets(1, render_target_views.put(), nullptr);
         ComPtr<ID3D11Resource> resource;
         render_target_views->GetResource(resource.put());
         HRESULT hr = resource->QueryInterface(game_device_data.source_color.put());
         if (FAILED(hr))
         {
            game_device_data.source_color = nullptr;
            reshade::log::message(reshade::log::level::error, "Failed getting color texture");
         }
         
         game_device_data.depth_texture_srv->GetResource(resource.put());
         hr = resource->QueryInterface(game_device_data.depth_texture.put());
         if (FAILED(hr))
         {
            reshade::log::message(reshade::log::level::error, "Failed getting depth texture");
         }
         
         game_device_data.source_color_rtv = render_target_views;
         
         (*original_draw_dispatch_func)();
         
         {
            SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaData);
            ID3D11ShaderResourceView* srvs[] = {game_device_data.motion_vectors_srv.get(), game_device_data.depth_texture_srv.get()};
            native_device_context->PSSetShaderResources(0, 2, srvs);
            auto ps = device_data.native_pixel_shaders[CompileTimeStringHash("Effect Merge Test")].get();
            native_device_context->PSSetShader(ps, nullptr, 0);
            
            auto rtv = game_device_data.decoded_motion_vectors_rtv.get();
#if DEVELOPMENT
            if (test_index == 15 && device_data.sr_type == SR::Type::None)
            {
               native_device_context->OMSetRenderTargets(1, &rtv, nullptr);
               (*original_draw_dispatch_func)();
               rtv = render_target_views.get();
               native_device_context->OMSetRenderTargets(1, &rtv, nullptr);
               srvs[0] = game_device_data.decoded_motion_vectors_srv.get();
               native_device_context->PSSetShaderResources(0, 2, srvs);
               (*original_draw_dispatch_func)();
            }
            else
#endif
            {
               native_device_context->OMSetRenderTargets(1, &rtv, nullptr);
               (*original_draw_dispatch_func)();
               rtv = render_target_views.get();
               native_device_context->OMSetRenderTargets(1, &rtv, nullptr);
            }

            {
               // split the command list since DLSS must be executed on an immediate context
               native_device_context->FinishCommandList(TRUE, game_device_data.partial_command_list.put());
               
               if (game_device_data.modifiable_inedx_buffer)
               {
                  D3D11_MAPPED_SUBRESOURCE mapped_buffer;
                  // When starting a new command list first map has to be D3D11_MAP_WRITE_DISCARD
                  native_device_context->Map(game_device_data.modifiable_inedx_buffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer);
                  native_device_context->Unmap(game_device_data.modifiable_inedx_buffer.get(), 0);
               }
               if (game_device_data.modifiable_vertex_buffer)
               {
                  D3D11_MAPPED_SUBRESOURCE mapped_buffer;
                  // When starting a new command list first map has to be D3D11_MAP_WRITE_DISCARD
                  native_device_context->Map(game_device_data.modifiable_vertex_buffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer);
                  native_device_context->Unmap(game_device_data.modifiable_vertex_buffer.get(), 0);
               }
               if (!is_search_mode_on)
               {
                  game_device_data.draw_device_context = native_device_context;
               }
            }
         }
         return DrawOrDispatchOverrideType::Replaced;
      }
      
      if (original_shader_hashes.Contains(shader_hashes_fog))
      {
         game_device_data.has_drawn_fog = true;
         return DrawOrDispatchOverrideType::None;
      }
      
      if (is_search_mode_on)
      {
         {
            if (original_shader_hashes.Contains(shader_hashes_search_mode))
            {
               game_device_data.has_drawn_search_mode = true;
               
               // Skip drawing it, we will replay it in immediate context
               // native_device_context->Draw(4, 0);
               {
                  auto& rs = game_device_data.search_mode_filter_render_state;
                  native_device_context->VSGetShader(rs.vertex_shader.put(), nullptr, 0);
                  native_device_context->IAGetInputLayout(rs.input_layout.put());
                  native_device_context->IAGetVertexBuffers(0, 1, rs.vertex_buffer.put(), &rs.vertex_buffer_stride, &rs.vertex_buffer_offset);
                  native_device_context->IAGetIndexBuffer(rs.index_buffer.put(), &rs.index_buffer_format, &rs.index_buffer_offset);
                  native_device_context->VSGetConstantBuffers(0,1,rs.vs_constant_buffer.put());
                  native_device_context->PSGetConstantBuffers(0,1,rs.ps_constant_buffer.put());
                  ID3D11SamplerState* ps_samplers[2] = {};
                  native_device_context->PSGetSamplers(0,2,&ps_samplers[0]);
                  for (UINT i = 0; i < 2; ++i)
                  {
                     rs.ps_samplers[i] = ps_samplers[i];
                  }
                  native_device_context->PSGetShaderResources(1,1,rs.ps_depth_srv.put());
                  native_device_context->OMGetRenderTargets(0,nullptr,rs.depth_stencil_view.put());
                  native_device_context->OMGetDepthStencilState(rs.depth_stencil_state.put(), &rs.stencil_ref);
                  native_device_context->RSGetState(rs.rasterizer_state.put());
                  rs.dirty = true;
               }
               
               // split the command list since DLSS must be executed on an immediate context
               native_device_context->FinishCommandList(TRUE, game_device_data.pre_search_mode_command_list.put());
               
               if (game_device_data.modifiable_inedx_buffer)
               {
                  D3D11_MAPPED_SUBRESOURCE mapped_buffer;
                  // When starting a new command list first map has to be D3D11_MAP_WRITE_DISCARD
                  native_device_context->Map(game_device_data.modifiable_inedx_buffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer);
                  native_device_context->Unmap(game_device_data.modifiable_inedx_buffer.get(), 0);
               }
               if (game_device_data.modifiable_vertex_buffer)
               {
                  D3D11_MAPPED_SUBRESOURCE mapped_buffer;
                  // When starting a new command list first map has to be D3D11_MAP_WRITE_DISCARD
                  native_device_context->Map(game_device_data.modifiable_vertex_buffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer);
                  native_device_context->Unmap(game_device_data.modifiable_vertex_buffer.get(), 0);
               }
               //game_device_data.draw_device_context = native_device_context;

               return DrawOrDispatchOverrideType::Replaced;
            }
            
            if (game_device_data.has_drawn_search_mode && original_shader_hashes.Contains(shader_hashes_copy))
            {
               auto rtv = game_device_data.source_color_rtv.get();
               ComPtr<ID3D11DepthStencilView> dsv;
               native_device_context->OMGetRenderTargets(0, nullptr, dsv.put());
               native_device_context->OMSetRenderTargets(1, &rtv, dsv.get());
               (*original_draw_dispatch_func)();
               // split the command list since DLSS must be executed on an immediate context
               native_device_context->FinishCommandList(TRUE, game_device_data.post_search_mode_command_list.put());
               
               if (game_device_data.modifiable_inedx_buffer)
               {
                  D3D11_MAPPED_SUBRESOURCE mapped_buffer;
                  // When starting a new command list first map has to be D3D11_MAP_WRITE_DISCARD
                  native_device_context->Map(game_device_data.modifiable_inedx_buffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer);
                  native_device_context->Unmap(game_device_data.modifiable_inedx_buffer.get(), 0);
               }
               if (game_device_data.modifiable_vertex_buffer)
               {
                  D3D11_MAPPED_SUBRESOURCE mapped_buffer;
                  // When starting a new command list first map has to be D3D11_MAP_WRITE_DISCARD
                  native_device_context->Map(game_device_data.modifiable_vertex_buffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer);
                  native_device_context->Unmap(game_device_data.modifiable_vertex_buffer.get(), 0);
               }
               
               game_device_data.draw_device_context = native_device_context;
               
               game_device_data.has_drawn_search_mode = false;
               
               return DrawOrDispatchOverrideType::Replaced;
            }
         }
      }

      if (original_shader_hashes.Contains(shader_hashes_motion_blur))
      {
         /*
         SetLumaConstantBuffers(native_device_context, cmd_list_data, device_data, reshade::api::shader_stage::pixel, LumaConstantBufferType::LumaData);
         auto ps = device_data.native_pixel_shaders[CompileTimeStringHash("Motion Blur")].get();
         ID3D11ShaderResourceView* srv = { game_device_data.decoded_motion_vectors_srv.get() };
         native_device_context->PSSetShader(ps, nullptr, 0);
         native_device_context->PSSetShaderResources(3, 1, &srv);
         
         DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack;
         draw_state_stack.Cache(native_device_context, device_data.uav_max_count);
         
         ID3D11ShaderResourceView* srvs[2];
         ID3D11RenderTargetView* rtvs[1];
         native_device_context->PSGetShaderResources(0, 2, &srvs[0]);
         native_device_context->OMGetRenderTargets(1, &rtvs[0], nullptr);
         MotionBlur::DrawData draw_data;
         draw_data.input_color_srv = srvs[1];
         draw_data.input_depth_srv = game_device_data.temporal_depth_pass.resources[TemporalAADepth::Texture::DepthHistoryRead].srv.get();
         draw_data.input_mv_srv = game_device_data.decoded_motion_vectors_srv.get();
         draw_data.output_rtv = rtvs[0];
         draw_data.width = *(int*)(RenderResolution + 0x00);
         draw_data.height = *(int*)(RenderResolution + 0x04);
         draw_data.velocity_tex_width = *(int*)(RenderResolution + 0x00);
         draw_data.velocity_tex_height = *(int*)(RenderResolution + 0x04);
         draw_data.z_far = CameraData->camera_distances.y;
         draw_data.z_near = CameraData->camera_distances.x;
         draw_data.z_reversed = false;
         draw_data.shutter_angle = -270.f;
         draw_data.sample_count = 10;
         
         game_device_data.motion_blur_pass.Draw(native_device, native_device_context, device_data, draw_data);
         
         draw_state_stack.Restore(native_device_context);
         return DrawOrDispatchOverrideType::Replaced;
         */
         if (game_device_data.has_replaced_motion_blur)
         {
            auto ps = device_data.native_pixel_shaders[CompileTimeStringHash("Motion Blur Passthrough")].get();
            native_device_context->PSSetShader(ps, nullptr, 0);
            return DrawOrDispatchOverrideType::None;
         }
         
         DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack;
         draw_state_stack.Cache(native_device_context, device_data.uav_max_count);
         
         ID3D11ShaderResourceView* srvs[2];
         ID3D11RenderTargetView* rtvs[1];
         native_device_context->PSGetShaderResources(0, 2, &srvs[0]);
         native_device_context->OMGetRenderTargets(1, &rtvs[0], nullptr);
         MotionBlur::DrawData draw_data;
         draw_data.input_color_srv = srvs[1];
         draw_data.input_depth_srv = game_device_data.temporal_depth_pass.resources[TemporalAADepth::Texture::DepthHistoryRead].srv.get();
         draw_data.input_mv_srv = game_device_data.decoded_motion_vectors_srv.get();
         draw_data.output_rtv = rtvs[0];
         draw_data.width = *(int*)(RenderResolution + 0x00);
         draw_data.height = *(int*)(RenderResolution + 0x04);
         draw_data.velocity_tex_width = *(int*)(RenderResolution + 0x00);
         draw_data.velocity_tex_height = *(int*)(RenderResolution + 0x04);
         draw_data.z_far = CameraData->camera_distances.y;
         draw_data.z_near = CameraData->camera_distances.x;
         draw_data.z_reversed = false;
         draw_data.shutter_angle = -270.f;
         draw_data.sample_count = 10;
         
         game_device_data.motion_blur_pass.Draw(native_device, native_device_context, device_data, draw_data);
         
         draw_state_stack.Restore(native_device_context);
         return DrawOrDispatchOverrideType::Replaced;
      }
      
      if (original_shader_hashes.Contains(shader_hashes_dof_merge))
      {
         UINT slot = 1;
         if (original_shader_hashes.pixel_shaders[0] == 0xFB128186)
            slot = 0;
         
         native_device_context->PSSetShaderResources(slot, 1, game_device_data.temporal_depth_pass.resources[TemporalAADepth::Texture::DepthHistoryRead].srv.get_addressof());
         return DrawOrDispatchOverrideType::None;
      }
      
      if (original_shader_hashes.Contains(shader_hashes_postfx_composite))
      {
         if (original_shader_hashes.Contains(shader_hashes_postfx_composite_with_depth))
         {
            native_device_context->PSSetShaderResources(2, 1, game_device_data.temporal_depth_pass.resources[TemporalAADepth::Texture::DepthHistoryRead].srv.get_addressof());
         }
         
         if (!is_in_battle_mode || GetSettingValue(15) == 0)
            return DrawOrDispatchOverrideType::None;
         
         (*original_draw_dispatch_func)();

         int width  = *(int*)(RenderResolution + 0x00);
         int height = *(int*)(RenderResolution + 0x04);
         SetupPostFXTexture(native_device, game_device_data, width, height);
         
         ComPtr<ID3D11RenderTargetView> render_target_views;
         native_device_context->OMGetRenderTargets(1, render_target_views.put(), nullptr);
         ComPtr<ID3D11Resource> resource;
         render_target_views->GetResource(resource.put());
         native_device_context->CopyResource(game_device_data.postfx_source_color.get(), resource.get());
         
         DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack;
         draw_state_stack.Cache(native_device_context, device_data.uav_max_count);
         
         MotionBlur::DrawData draw_data;
         draw_data.input_color_srv = game_device_data.postfx_source_color_srv.get();
         draw_data.input_depth_srv = game_device_data.temporal_depth_pass.resources[TemporalAADepth::Texture::DepthHistoryRead].srv.get();;
         draw_data.input_mv_srv = game_device_data.decoded_motion_vectors_srv.get();
         draw_data.output_rtv = render_target_views.get();
         draw_data.width = *(int*)(RenderResolution + 0x00);
         draw_data.height = *(int*)(RenderResolution + 0x04);
         draw_data.velocity_tex_width = *(int*)(RenderResolution + 0x00);
         draw_data.velocity_tex_height = *(int*)(RenderResolution + 0x04);
         draw_data.z_far = CameraData->camera_distances.y;
         draw_data.z_near = CameraData->camera_distances.x;
         draw_data.z_reversed = false;
         draw_data.shutter_angle = -270.f;
         draw_data.sample_count = 10;
         
         game_device_data.motion_blur_pass.Draw(native_device, native_device_context, device_data, draw_data);
         game_device_data.has_replaced_motion_blur = true;
         
         draw_state_stack.Restore(native_device_context);
         return DrawOrDispatchOverrideType::Replaced;
      }
      
      // The game will copy to main RT after fog has drawn
      if (game_device_data.has_drawn_fog && !game_device_data.has_copied_fog_to_main_rt)
      {
         if (original_shader_hashes.Contains(shader_hashes_copy))
         {
            game_device_data.has_copied_fog_to_main_rt = true;
            // this is very loose because draws after this could be other types of draw
            game_device_data.frame_phase = FramePhase::FORWARD;
            counter++;
            //reshade::log::message(reshade::log::level::info, std::format("Frame phase: Forward, count: {}.", counter).c_str());
#if DEVELOPMENT
            const std::shared_lock lock_trace(s_mutex_trace);
            if (trace_running)
            {
               const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
               TraceDrawCallData trace_draw_call_data;
               trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
               trace_draw_call_data.command_list = native_device_context;
               trace_draw_call_data.custom_name = "Forward Start";
               cmd_list_data.trace_draw_calls_data.insert(cmd_list_data.trace_draw_calls_data.end() - 1, trace_draw_call_data);
            }
#endif
         }
         return DrawOrDispatchOverrideType::None;
      }
      
      if (game_device_data.frame_phase == FramePhase::FORWARD)
      {
         if (original_shader_hashes.Contains(shader_hashes_copy_red))
         {
            native_device_context->PSGetShaderResources(0, 1, game_device_data.depth_texture_srv.put());
            return DrawOrDispatchOverrideType::None;
         }
      }

      if (game_device_data.frame_phase == FramePhase::GBUFFER || game_device_data.frame_phase == FramePhase::FORWARD)
      {
         //reshade::log::message(reshade::log::level::info, std::format("Current PS: 0x{:X}", original_shader_hashes.pixel_shaders[0]).c_str());
         
         if (original_shader_hashes.pixel_shaders.empty())
            return DrawOrDispatchOverrideType::None;
         
         if (!original_shader_hashes.Contains(shader_hashes_vertex))
            return DrawOrDispatchOverrideType::None;
         
#if DEVELOPMENT
         if (original_shader_hashes.Contains(shader_hashes_L2P_vertex))
         {
            Luma::OverlayLog::AddWarning(std::format("L2P Shader Drawing: 0x{:X}", original_shader_hashes.vertex_shaders[0]).c_str());
         }
#endif
         
         {
            ComPtr<ID3D11Buffer> vertex_cb;
            native_device_context->VSGetConstantBuffers(0, 1, vertex_cb.put());
            
            if (!vertex_cb.get())
               return DrawOrDispatchOverrideType::None;
            /*
            auto hash_draw_call = [](uint64_t pixel_shader, ID3D11Buffer* vertex_buffer, uint32_t vertex_count)
            {
               uint8_t buffer[sizeof(pixel_shader) + sizeof(vertex_buffer) + sizeof(vertex_count)] = {};
               memcpy(&buffer[0], &pixel_shader, sizeof(pixel_shader));
               memcpy(&buffer[sizeof(pixel_shader)], &vertex_buffer, sizeof(vertex_buffer));
               memcpy(&buffer[sizeof(pixel_shader) + sizeof(vertex_buffer)], &vertex_count, sizeof(vertex_count));
               return XXH3_64bits(buffer, sizeof(buffer));
            };
            */
            // Generally the slot 0, instance data at slot 8, sometimes slot 1
            ComPtr<ID3D11Buffer> vertex_buffer;
            //uint32_t stride;
            native_device_context->IAGetVertexBuffers(0, 1, vertex_buffer.put(), nullptr, nullptr);
            /*
            D3D11_BUFFER_DESC bd;
            vertex_cb->GetDesc(&bd);
            */
            auto& cb_data = game_device_data.g_pending_cb_data[vertex_cb.get()];
            
            auto local_to_world_offset_it = g_local_to_world_offset.find(original_shader_hashes.vertex_shaders[0]);
            float3 world_position = {0.0f, 0.0f, 0.0f};
            if (local_to_world_offset_it != g_local_to_world_offset.end())
            {
               if (local_to_world_offset_it->second <= cb_data.cpu_data.size() - 16)
               {
                  auto* floats = reinterpret_cast<const float4*>(cb_data.cpu_data.data() + local_to_world_offset_it->second);
                  world_position = {floats[0].w, floats[1].w, floats[2].w};
               }
            }
            
            bool is_cb_dirty = cb_data.dirty;
            uint64_t draw_call_hash = HashDrawCall(original_shader_hashes.vertex_shaders[0], vertex_buffer.get(), max(last_draw_dispatch_data.index_count, last_draw_dispatch_data.vertex_count));
            
            
            // Cache all regardless due to potential hash collision
            // if (game_device_data.global_cbuffer_lookup.find(draw_call_hash) == game_device_data.global_cbuffer_lookup.cend())
            {
               CBufferEntry cache_entry = {};
               uint32_t Align256 = (cb_data.size + 255) & ~255;
               cache_entry.size = Align256;//bd.ByteWidth;
               if (is_cb_dirty)
               {
                  cache_entry.offset = game_device_data.g_shadow_memory.size;
                  game_device_data.g_shadow_memory.CopyFromMemory(cb_data.cpu_data.data(), cb_data.size, Align256);
                  cb_data.last_dirty_offset = cache_entry.offset;
                  cb_data.dirty = false;
               }
               else
               {
                  cache_entry.offset = cb_data.last_dirty_offset;
               }
               cache_entry.world_position = world_position;

               game_device_data.global_cbuffer_lookup[draw_call_hash].push_back(cache_entry);
            }
            
            //ID3D11Buffer* cb = game_device_data.g_cbuffer.get();
            //native_device_context->VSSetConstantBuffers(5, 1, &cb);
            
            bool has_previous_input = false;
            
            auto prev_it = game_device_data.prev_global_cbuffer_lookup.find(draw_call_hash);
            
            if (prev_it != game_device_data.prev_global_cbuffer_lookup.cend())
            {
               //if (is_cb_dirty)
               {
                  ID3D11Buffer* cb = game_device_data.g_cbuffer.buffer.get();
                  auto& prev_cb = prev_it->second;
                  uint32_t first_constant;
                  uint32_t num_constants;
                  
                  if (prev_cb.size() == 1)
                  {
                     first_constant = prev_cb[0].offset / 16;
                     num_constants = prev_cb[0].size / 16;
                  }
                  else
                  {
                     // Try a spatial-hash lookup first to avoid scanning all previous entries.
                     CBufferEntry* cache_data = nullptr;
                     float shortest_distance = FLT_MAX;
                     uint32_t best_index = 0;

                     // quantize world position into a spatial key
                     const float kBucketSize = 0.5f; // 0.5 world units per bucket (tunable)
                     auto quantize = [](float v, float bucket) -> int32_t { return static_cast<int32_t>(floorf(v / bucket)); };
                     uint64_t qx = static_cast<uint64_t>(static_cast<uint32_t>(quantize(world_position.x, kBucketSize)) & 0x1FFFFF);
                     uint64_t qy = static_cast<uint64_t>(static_cast<uint32_t>(quantize(world_position.y, kBucketSize)) & 0x1FFFFF);
                     uint64_t qz = static_cast<uint64_t>(static_cast<uint32_t>(quantize(world_position.z, kBucketSize)) & 0x1FFFFF);
                     uint64_t spatial_key = qx | (qy << 21) | (qz << 42);

                     auto& spatial_index_map = game_device_data.prev_global_cbuffer_spatial_index[draw_call_hash];
                     if (spatial_index_map.empty())
                     {
                        // build spatial index for this draw hash
                        for (uint32_t i = 0; i < prev_cb.size(); ++i)
                        {
                           const float3& p = prev_cb[i].world_position;
                           uint64_t px = static_cast<uint64_t>(static_cast<uint32_t>(quantize(p.x, kBucketSize)) & 0x1FFFFF);
                           uint64_t py = static_cast<uint64_t>(static_cast<uint32_t>(quantize(p.y, kBucketSize)) & 0x1FFFFF);
                           uint64_t pz = static_cast<uint64_t>(static_cast<uint32_t>(quantize(p.z, kBucketSize)) & 0x1FFFFF);
                           uint64_t pk = px | (py << 21) | (pz << 42);
                           // Only keep the first / representative index for this bucket
                           if (spatial_index_map.find(pk) == spatial_index_map.end())
                              spatial_index_map[pk] = i;
                        }
                     }

                     auto found = spatial_index_map.find(spatial_key);
                     if (found != spatial_index_map.end())
                     {
                        best_index = found->second;
                        cache_data = &prev_cb[best_index];
                     }
                     else
                     {
                        // fallback to full scan (kept but cheaper in most cases)
                        for (uint32_t i = 0; i < prev_cb.size(); ++i)
                        {
                           float3 prev_world_position = prev_cb[i].world_position;
                           float dist =   (world_position.x - prev_world_position.x) * (world_position.x - prev_world_position.x) +
                                          (world_position.y - prev_world_position.y) * (world_position.y - prev_world_position.y) +
                                          (world_position.z - prev_world_position.z) * (world_position.z - prev_world_position.z);
                           if (dist == 0.0f)
                           {
                              cache_data = &prev_cb[i];
                              best_index = i;
                              break;
                           }
                           if (dist < shortest_distance)
                           {
                              cache_data = &prev_cb[i];
                              shortest_distance = dist;
                              best_index = i;
                           }
                        }
                        // update spatial index with this representative
                        spatial_index_map[spatial_key] = best_index;
                     }
                     first_constant = cache_data->offset / 16;
                     num_constants = cache_data->size / 16;
                  }
                  
                  ComPtr<ID3D11DeviceContext1> ctx1;
                  auto it_ctx = game_device_data.ctx1_cache.find(native_device_context);
                  if (it_ctx != game_device_data.ctx1_cache.end())
                  {
                     ctx1 = it_ctx->second;
                  }
                  else
                  {
                     if (SUCCEEDED(native_device_context->QueryInterface(ctx1.put())))
                     {
                        game_device_data.ctx1_cache[native_device_context] = ctx1;
                     }
                     else
                     {
                        //reshade::log::message(reshade::log::level::error, "context1 not supported.");
                        Luma::OverlayLog::AddError("context1 not supported.");
                        return DrawOrDispatchOverrideType::None;
                     }
                  }
                  //reshade::log::message(reshade::log::level::info, std::format("First Constant: {}, Num Constants: {}", first_constant, num_constants).c_str());
                  ctx1->VSSetConstantBuffers1(5, 1, &cb, &first_constant, &num_constants);
               }
               // TODO: make a list of "static" vertex shader to check if they're wrong from last frame
               //if (original_shader_hashes.vertex_shaders[0] != 0xC51AD711)
               {
                  has_previous_input = true;
               }
            }
            
            bool is_gbuffer = game_device_data.frame_phase == FramePhase::GBUFFER;
            
            if (!is_gbuffer)
            {
               ComPtr<ID3D11Buffer> pixel_cb;
               native_device_context->PSGetConstantBuffers(0, 1, pixel_cb.put());
                  
               auto at_color_it = g_at_color_offset.find(original_shader_hashes.pixel_shaders[0]);
                  
               if (pixel_cb.get() != nullptr && at_color_it != g_at_color_offset.end())
               {
                  cb_data = game_device_data.g_pending_cb_data[pixel_cb.get()];
                  auto* floats = reinterpret_cast<const float4*>(cb_data.cpu_data.data() + at_color_it->second);
                  float alpha = floats[0].w;
                  if (alpha != 1.0f)
                  {
                     has_previous_input = false;
                  }
               }
            }
            
            auto shader_it = game_device_data.modified_vertex_shaders.find(original_shader_hashes.vertex_shaders[0]);
            if (shader_it != game_device_data.modified_vertex_shaders.cend())
            {
               ComPtr<ID3D11VertexShader> vertex_shader;
               native_device_context->VSGetShader(vertex_shader.put(), nullptr, nullptr);

               if (vertex_shader != shader_it->second.get())
               {
                  game_device_data.original_vertex_shaders[original_shader_hashes.vertex_shaders[0]] = vertex_shader.get();
                  if (has_previous_input)
                  {
                     native_device_context->VSSetShader(shader_it->second.get(), nullptr, 0);
                  }
               }
               else if (!has_previous_input)
               {
                  native_device_context->VSSetShader(game_device_data.original_vertex_shaders[original_shader_hashes.vertex_shaders[0]].get(), nullptr, 0);
                  return DrawOrDispatchOverrideType::None;
               }

               auto shader_pixel_it = game_device_data.modified_pixel_shaders.find(original_shader_hashes.pixel_shaders[0]);
               if (shader_pixel_it != game_device_data.modified_pixel_shaders.cend())
               {
                  ComPtr<ID3D11PixelShader> pixel_shader;
                  native_device_context->PSGetShader(pixel_shader.put(), nullptr, nullptr);
                  
                  if (pixel_shader != shader_pixel_it->second.get())
                  {
                     game_device_data.original_pixel_shaders[original_shader_hashes.pixel_shaders[0]] = pixel_shader.get();
                     if (has_previous_input)
                     {
                        native_device_context->PSSetShader(shader_pixel_it->second.get(), nullptr, 0);
                     }
                  }
                  else if (!has_previous_input)
                  {
                     native_device_context->PSSetShader(game_device_data.original_pixel_shaders[original_shader_hashes.pixel_shaders[0]].get(), nullptr, 0);
                     return DrawOrDispatchOverrideType::None;
                  }
               }
            }
            
            //bool replace = false;
            bool is_hatching = is_hatching_on;
            
            if (is_gbuffer)
            {
               is_hatching = is_hatching && original_shader_hashes.Contains(shader_hashes_hatching_gbuffer);
               //if (1)
               {
                  if (!is_hatching)
                  {
                     if (game_device_data.current_rtvs[5] != game_device_data.motion_vectors_rtv.get())
                     {
                        ID3D11RenderTargetView* updated_render_target_views[] = {game_device_data.current_rtvs[0],
                           game_device_data.current_rtvs[1],
                           game_device_data.current_rtvs[2],
                           game_device_data.current_rtvs[3],
                           game_device_data.current_rtvs[4],
                           game_device_data.motion_vectors_rtv.get()};
                        native_device_context->OMSetRenderTargets(6, updated_render_target_views, game_device_data.current_dsv);
                        game_device_data.current_rtvs[5] = game_device_data.motion_vectors_rtv.get();
#if DEVELOPMENT
                        const std::shared_lock lock_trace(s_mutex_trace);
                        if (trace_running)
                        {
                           const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
                           TraceDrawCallData trace_draw_call_data;
                           trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
                           trace_draw_call_data.command_list = native_device_context;
                           trace_draw_call_data.custom_name = "GBuffer Override RT 5";
                           cmd_list_data.trace_draw_calls_data.insert(cmd_list_data.trace_draw_calls_data.end() - 1, trace_draw_call_data);
                        }
#endif
                     }
                  }
                  else
                  {
                     if (game_device_data.current_rtvs[6] != game_device_data.motion_vectors_rtv.get())
                     {
                        ID3D11RenderTargetView* updated_render_target_views[] = {game_device_data.current_rtvs[0],
                           game_device_data.current_rtvs[1],
                           game_device_data.current_rtvs[2],
                           game_device_data.current_rtvs[3],
                           game_device_data.current_rtvs[4],
                           game_device_data.current_rtvs[5],
                           game_device_data.motion_vectors_rtv.get()};
                        native_device_context->OMSetRenderTargets(7, updated_render_target_views, game_device_data.current_dsv);
                        game_device_data.current_rtvs[6] = game_device_data.motion_vectors_rtv.get();
                     }
                  }
               }
            }
            else
            {
               
               is_hatching = is_hatching && original_shader_hashes.Contains(shader_hashes_hatching_forward);
               
               //if (1)
               {
                  if (!is_hatching)
                  {
                     if (game_device_data.current_rtvs[0] == game_device_data.source_color_rtv.get())
                     {
                        if (game_device_data.current_rtvs[1] != game_device_data.motion_vectors_rtv.get())
                        {
                           ID3D11RenderTargetView* updated_render_target_views[] = {game_device_data.current_rtvs[0],
                              game_device_data.motion_vectors_rtv.get()};
                           native_device_context->OMSetRenderTargets(2, updated_render_target_views, game_device_data.current_dsv);
                           game_device_data.current_rtvs[1] = game_device_data.motion_vectors_rtv.get();
#if DEVELOPMENT
                           const std::shared_lock lock_trace(s_mutex_trace);
                           if (trace_running)
                           {
                              const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
                              TraceDrawCallData trace_draw_call_data;
                              trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
                              trace_draw_call_data.command_list = native_device_context;
                              trace_draw_call_data.custom_name = "Forward Override RT 1";
                              cmd_list_data.trace_draw_calls_data.insert(cmd_list_data.trace_draw_calls_data.end() - 1, trace_draw_call_data);
                           }
#endif
                        }
                     }
                  }
                  else
                  {
                     if (game_device_data.current_rtvs[0] == game_device_data.source_color_rtv.get())
                     {
                        if (game_device_data.current_rtvs[2] != game_device_data.motion_vectors_rtv.get())
                        {
                           ID3D11RenderTargetView* updated_render_target_views[] = {game_device_data.current_rtvs[0],
                              game_device_data.current_rtvs[1],
                              game_device_data.motion_vectors_rtv.get()};
                           native_device_context->OMSetRenderTargets(3, updated_render_target_views, game_device_data.current_dsv);
                           game_device_data.current_rtvs[2] = game_device_data.motion_vectors_rtv.get();
                        }
                     }
                  }
               }
            }
         }
      }

      return DrawOrDispatchOverrideType::None;
   }
   
   static void OnExecuteSecondaryCommandList(reshade::api::command_list* cmd_list, reshade::api::command_list* secondary_cmd_list)
   {
      com_ptr<ID3D11DeviceContext> native_device_context;
      ID3D11DeviceChild* device_child = (ID3D11DeviceChild*)(cmd_list->get_native());
      HRESULT hr = device_child->QueryInterface(&native_device_context);

      auto& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);

      if (native_device_context)
      {
         com_ptr<ID3D11CommandList> native_command_list;
         ID3D11DeviceChild* device_child = (ID3D11DeviceChild*)(secondary_cmd_list->get_native());
         HRESULT hr = device_child->QueryInterface(&native_command_list);
         if (native_command_list.get() == game_device_data.remainder_command_list && game_device_data.partial_command_list)
         {
            DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack;
            DrawStateStack<DrawStateStackType::Compute> compute_state_stack;
            draw_state_stack.Cache(native_device_context.get(), device_data.uav_max_count);
            compute_state_stack.Cache(native_device_context.get(), device_data.uav_max_count);
            
            if (!is_search_mode_on)
            {
               native_device_context->ExecuteCommandList(game_device_data.partial_command_list.get(), FALSE);
               game_device_data.partial_command_list.reset();
               game_device_data.remainder_command_list.store(nullptr, std::memory_order_relaxed);
            }
            else
            {
               native_device_context->ExecuteCommandList(game_device_data.partial_command_list.get(), FALSE);
               game_device_data.partial_command_list.reset();
               
               auto& rs = game_device_data.search_mode_filter_render_state;
               if (rs.dirty)
               {
                  int width  = *(int*)(RenderResolution + 0x00);
                  int height = *(int*)(RenderResolution + 0x04);
                  ID3D11Buffer* vertex_buffer = rs.vertex_buffer.get();
                  ID3D11Buffer* index_buffer = rs.index_buffer.get();
                  ID3D11RenderTargetView* rtv = game_device_data.source_color_rtv.get();
                  ID3D11Buffer* vs_cb = rs.vs_constant_buffer.get();
                  ID3D11Buffer* ps_cb = rs.ps_constant_buffer.get();
                  ID3D11ShaderResourceView* srvs[2] = {};
                  srvs[0] = game_device_data.frame_color_texture_srv.get();
                  srvs[1] = rs.ps_depth_srv.get();
                  
                  ID3D11SamplerState* ps_samplers[2] = {};
                  for (UINT i = 0; i < 2; ++i)
                  {
                     ps_samplers[i] = rs.ps_samplers[i].get();
                  }
                  
                  auto ps = device_data.native_pixel_shaders[CompileTimeStringHash("Search Mode Filter")].get();
                  
                  D3D11_VIEWPORT viewport;
                  viewport.TopLeftX = 0;
                  viewport.TopLeftY = 0;
                  viewport.Width = static_cast<float>(width);
                  viewport.Height = static_cast<float>(height);
                  viewport.MinDepth = 0;
                  viewport.MaxDepth = 1;
                  
                  native_device_context->CopyResource(game_device_data.frame_color_texture.get(), game_device_data.source_color.get());
                  
                  native_device_context->IASetVertexBuffers(0,1, &vertex_buffer,&rs.vertex_buffer_stride,&rs.vertex_buffer_offset);
                  native_device_context->IASetIndexBuffer(index_buffer, rs.index_buffer_format, rs.index_buffer_offset);
                  native_device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
                  native_device_context->IASetInputLayout(rs.input_layout.get());
                  
                  native_device_context->VSSetConstantBuffers(0,1, &vs_cb);
                  native_device_context->VSSetShader(rs.vertex_shader.get(), nullptr, 0);
                  
                  native_device_context->PSSetConstantBuffers(0,1, &ps_cb);
                  native_device_context->PSSetSamplers(0,2, &ps_samplers[0]);
                  native_device_context->PSSetShaderResources(0, 2, &srvs[0]);
                  native_device_context->PSSetShader(ps, nullptr, 0);
                  
                  native_device_context->OMSetRenderTargets(1, &rtv, rs.depth_stencil_view.get());
                  native_device_context->OMSetDepthStencilState(rs.depth_stencil_state.get(), rs.stencil_ref);
                  native_device_context->RSSetViewports(1, &viewport);
                  native_device_context->RSSetScissorRects(0, nullptr);
                  native_device_context->RSSetState(rs.rasterizer_state.get());
                  
                  native_device_context->Draw(4, 0);
               }
               
               native_device_context->ExecuteCommandList(game_device_data.post_search_mode_command_list.get(), FALSE);
               game_device_data.post_search_mode_command_list.reset();
               
               game_device_data.remainder_command_list.store(nullptr, std::memory_order_relaxed);
            }

            if (!game_device_data.source_color.get() || !game_device_data.depth_texture.get() || device_data.sr_type == SR::Type::None)// || test_index == 5)
            {
               if (is_search_mode_on)
               {
                  native_device_context->ExecuteCommandList(game_device_data.pre_search_mode_command_list.get(), FALSE);
                  game_device_data.pre_search_mode_command_list.reset();
               }
               
               draw_state_stack.Restore(native_device_context.get());
               compute_state_stack.Restore(native_device_context.get());
               
               return;
            }

            //CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
            
            int width  = *(int*)(RenderResolution + 0x00);
            int height = *(int*)(RenderResolution + 0x04);
            
            auto* sr_instance_data = device_data.GetSRInstanceData();
            {
               SR::SettingsData settings_data;
               settings_data.output_width = width;
               settings_data.output_height = height;
               settings_data.render_width = width;
               settings_data.render_height = height;
               settings_data.dynamic_resolution = false;
               settings_data.hdr = true;
               settings_data.inverted_depth = false;
               settings_data.mvs_jittered = false;
               settings_data.mvs_x_scale = -static_cast<float>(width);
               settings_data.mvs_y_scale = -static_cast<float>(height);
               settings_data.auto_exposure = true;
               settings_data.render_preset = dlss_render_preset;
               sr_implementations[device_data.sr_type]->UpdateSettings(sr_instance_data, native_device_context.get(), settings_data);
            }
            /*
            {
               SetLumaConstantBuffers(native_device_context.get(), cmd_list_data, device_data, reshade::api::shader_stage::compute, LumaConstantBufferType::LumaData);
               ID3D11ShaderResourceView* srvs[] = {game_device_data.motion_vectors_srv.get(), game_device_data.depth_texture_srv.get()};
               native_device_context->CSSetShaderResources(0, 2, srvs);
               auto cs = device_data.native_compute_shaders[CompileTimeStringHash("Decode Motion Vector")].get();
               native_device_context->CSSetShader(cs,nullptr,0);
               native_device_context->CSSetUnorderedAccessViews(0, 1, game_device_data.decoded_motion_vectors_uav.put(), nullptr);
               native_device_context->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
            }
            */
            
            {
               // Unbind views to prevent resource hazard crash?
               ID3D11ShaderResourceView* nullSRVs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
               ID3D11RenderTargetView* nullRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
               native_device_context->PSSetShaderResources(0, device_data.uav_max_count, nullSRVs);
               native_device_context->CSSetShaderResources(0, device_data.uav_max_count, nullSRVs);
               native_device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, nullRTVs, nullptr);
            }
            
            {
               SR::SuperResolutionImpl::DrawData draw_data;
               draw_data.source_color = game_device_data.source_color.get();
               draw_data.output_color = game_device_data.resolve_texture.get();
               draw_data.motion_vectors = game_device_data.decoded_motion_vectors.get();
               draw_data.depth_buffer = game_device_data.depth_texture.get();
               draw_data.render_width = width;
               draw_data.render_height = height;
               draw_data.pre_exposure = 0.0f;
               draw_data.jitter_x = -projection_jitters.x;
               draw_data.jitter_y = -projection_jitters.y;
               draw_data.vert_fov = CameraData->camera_fov.y;
               draw_data.near_plane = CameraData->camera_distances.x;
               draw_data.far_plane = CameraData->camera_distances.y;
               draw_data.reset = device_data.force_reset_sr;

               bool dlss_succeeded = sr_implementations[device_data.sr_type]->Draw(sr_instance_data, native_device_context.get(), draw_data);
               game_device_data.has_drawn_upscaling = true;
               
               if (dlss_succeeded)
                  native_device_context->CopyResource(game_device_data.source_color.get(), game_device_data.resolve_texture.get());
            }
            
            if (is_search_mode_on)
            {
               native_device_context->ExecuteCommandList(game_device_data.pre_search_mode_command_list.get(), FALSE);
               game_device_data.pre_search_mode_command_list.reset();
            }
            
            {
               TemporalAADepth::DrawData draw_data;
               draw_data.width = width;
               draw_data.height = height;
               draw_data.input_depth_srv = game_device_data.depth_texture_srv.get();
               draw_data.input_mv_srv = game_device_data.decoded_motion_vectors_srv.get();
               draw_data.use_variance_clip = true;
               draw_data.variance_scale = 1.0f;
               draw_data.velocity_scale.x = -1.0f;
               draw_data.velocity_scale.y = -1.0f;
               draw_data.has_history = !device_data.force_reset_sr;
               
               ComPtr<ID3D11Device> native_device;
               native_device_context->GetDevice(native_device.put());
         
               game_device_data.temporal_depth_pass.Draw(native_device.get(), native_device_context.get(), device_data, draw_data);
            }
            
            draw_state_stack.Restore(native_device_context.get());
            compute_state_stack.Restore(native_device_context.get());
         }
      }

      com_ptr<ID3D11CommandList> native_command_list;
      hr = device_child->QueryInterface(&native_command_list);
      if (native_command_list)
      {
         ID3D11DeviceChild* device_child = (ID3D11DeviceChild*)(secondary_cmd_list->get_native());
         hr = device_child->QueryInterface(&native_device_context);
         if (native_device_context == game_device_data.draw_device_context)
         {
            game_device_data.remainder_command_list = native_command_list.get();
            game_device_data.draw_device_context = nullptr;
         }
      }
   }
   
   void UpdateLumaInstanceDataCB(CB::LumaInstanceDataPadded& data, CommandListData& cmd_list_data, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      
      int width  = *(int*)(RenderResolution + 0x00);
      int height = *(int*)(RenderResolution + 0x04);
      
      data.GameData.CurrJitters.x = projection_jitters.x / static_cast<float>(width);
      data.GameData.CurrJitters.y = projection_jitters.y / static_cast<float>(height);
      data.GameData.PrevJitters.x = prev_projection_jitters.x / static_cast<float>(width);
      data.GameData.PrevJitters.y = prev_projection_jitters.y / static_cast<float>(height);
      data.GameData.ViewportSize.x = static_cast<float>(width);
      data.GameData.ViewportSize.y = static_cast<float>(height);
      data.GameData.ViewportSize.z = 1.0 / static_cast<float>(width);
      data.GameData.ViewportSize.w = 1.0 / static_cast<float>(height);
      memcpy(&data.GameData.CurrentViewInverseMatrix, &game_device_data.inv_view_matrix, sizeof(DirectX::XMMATRIX));
      memcpy(&data.GameData.CurrentProjectionInverseMatrix, &game_device_data.inv_projection_matrix, sizeof(DirectX::XMMATRIX));
      memcpy(&data.GameData.CurrentViewProjectionInverseMatrix, &game_device_data.inv_view_projection_matrix, sizeof(DirectX::XMMATRIX));
      memcpy(&data.GameData.PreviousViewProjectionMatrix, &game_device_data.prev_view_projection_matrix, sizeof(DirectX::XMMATRIX));
      memcpy(&data.GameData.ReprojectionMatrix, &game_device_data.reprojection_matrix, sizeof(DirectX::XMMATRIX));
   }
   
   static void OnBindRenderTargetsAndDepthStencil(reshade::api::command_list* cmd_list, uint32_t count, const reshade::api::resource_view *rtvs, reshade::api::resource_view dsv)
   {
      ComPtr<ID3D11DeviceContext> native_device_context;
      ID3D11DeviceChild* device_child = (ID3D11DeviceChild*)(cmd_list->get_native());
      HRESULT hr = device_child->QueryInterface(native_device_context.put());

      auto& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);
      
      if (device_data.sr_type == SR::Type::None)
         return;
      
      for (uint32_t i = 0; i < count; i++)
      {
         ID3D11RenderTargetView* native_rtv = reinterpret_cast<ID3D11RenderTargetView*>(rtvs[i].handle);
         game_device_data.current_rtvs[i] = native_rtv;
      }
      
      for (uint32_t i = count; i < game_device_data.current_rtvs.size(); ++i)
      {
         game_device_data.current_rtvs[i] = nullptr;
      }
      
      game_device_data.current_dsv = reinterpret_cast<ID3D11DepthStencilView*>(dsv.handle);
      
      if (count > 4 && dsv.handle != 0)
      {
         // In title screen this happens for 3 times
         // 1. actual gbuffer binding
         // 2. water will draw after lighting pass
         // 3. beginning of the next deferred context but immediately overwritten by forward render target
         game_device_data.frame_phase = FramePhase::GBUFFER;
         counter++;
         //reshade::log::message(reshade::log::level::info, std::format("Frame phase: GBuffer, count: {}.", counter).c_str());
#if DEVELOPMENT
         const std::shared_lock lock_trace(s_mutex_trace);
         if (trace_running)
         {
            CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
            const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
            TraceDrawCallData trace_draw_call_data;
            trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
            trace_draw_call_data.command_list = native_device_context.get();
            trace_draw_call_data.custom_name = "GBuffer Start";
            cmd_list_data.trace_draw_calls_data.insert(cmd_list_data.trace_draw_calls_data.end() - 1, trace_draw_call_data);
         }
#endif
      }
   }
   
   static bool OnClearRenderTargetView(reshade::api::command_list* cmd_list, reshade::api::resource_view rtv, const float color[4], uint32_t rect_count, const reshade::api::rect* rects)
   {
      ComPtr<ID3D11DeviceContext> native_device_context;
      ID3D11DeviceChild* device_child = (ID3D11DeviceChild*)(cmd_list->get_native());
      HRESULT hr = device_child->QueryInterface(native_device_context.put());

      auto& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);
      
      if (device_data.sr_type == SR::Type::None)
         return false;
      
      if (game_device_data.frame_phase == FramePhase::NONE && game_device_data.source_color_rtv.get() == nullptr)
      {
         ID3D11RenderTargetView* native_rtv = reinterpret_cast<ID3D11RenderTargetView*>(rtv.handle);
         D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
         native_rtv->GetDesc(&rtv_desc);
         if (rtv_desc.Format == DXGI_FORMAT_R11G11B10_FLOAT)
         {
            game_device_data.source_color_rtv = native_rtv;
            //reshade::log::message(reshade::log::level::info, "Found source color RTV.");
            
            if (CameraData != nullptr)
            {
               DirectX::XMVECTOR det;
               game_device_data.inv_projection_matrix = DirectX::XMMatrixInverse(&det, CameraData->projection_matrix);
               game_device_data.inv_view_matrix = DirectX::XMMatrixInverse(&det, CameraData->view_matrix);
               game_device_data.inv_view_projection_matrix = DirectX::XMMatrixInverse(&det, CameraData->view_projection_matrix);
               
               camera_matrices_current.view_matrix = CameraData->view_matrix;
               camera_matrices_current.projection_matrix = CameraData->projection_matrix;
               camera_matrices_current.inv_view_matrix = game_device_data.inv_view_matrix;
               camera_matrices_current.inv_projection_matrix = game_device_data.inv_projection_matrix;
               camera_matrices_current.position = CameraData->camera_position;
               
               game_device_data.reprojection_matrix = ComputeCameraSpaceToPreviousProjectedSpaceMatrix();
            }
            
            ComPtr<ID3D11Device> device;
            native_device_context->GetDevice(device.put());
            
            int width  = *(int*)(RenderResolution + 0x00);
            int height = *(int*)(RenderResolution + 0x04);
            SetupMotionVectorTexture(device.get(), game_device_data, width, height);
            if (game_device_data.motion_vectors_rtv)
            {
               float clear_value[] = {0.0f, -1.0f, 0.0f, 0.0f};
               native_device_context->ClearRenderTargetView(game_device_data.motion_vectors_rtv.get(), clear_value);
            }
         }
      }
      // This is when the game clears the particle/effect rendertarget, after forward objects have drawn
      else if (game_device_data.frame_phase == FramePhase::FORWARD)
      {
         ID3D11RenderTargetView* native_rtv = reinterpret_cast<ID3D11RenderTargetView*>(rtv.handle);
         D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
         native_rtv->GetDesc(&rtv_desc);
         if (rtv_desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT)
         {
            game_device_data.frame_phase = FramePhase::UNDEFINED;
            //reshade::log::message(reshade::log::level::info, "Frame phase: Undefined.");
#if DEVELOPMENT
            const std::shared_lock lock_trace(s_mutex_trace);
            if (trace_running)
            {
               CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
               const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
               TraceDrawCallData trace_draw_call_data;
               trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
               trace_draw_call_data.command_list = native_device_context.get();
               trace_draw_call_data.custom_name = "Forward End";
               cmd_list_data.trace_draw_calls_data.insert(cmd_list_data.trace_draw_calls_data.end() - 1, trace_draw_call_data);
            }
#endif
         }
      }

      return false;
   }
   
   static bool OnCopyTextureRegion(reshade::api::command_list* cmd_list, reshade::api::resource source, uint32_t source_subresource, const reshade::api::subresource_box *source_box, reshade::api::resource dest, uint32_t dest_subresource, const reshade::api::subresource_box *dest_box, reshade::api::filter_mode filter)
   {
      ComPtr<ID3D11DeviceContext> native_device_context;
      ID3D11DeviceChild* device_child = (ID3D11DeviceChild*)(cmd_list->get_native());
      HRESULT hr = device_child->QueryInterface(native_device_context.put());

      auto& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);
      
      if (device_data.sr_type == SR::Type::None)
         return false;
      
      // This call "CopySubresourceRegion" seems to be used for every phase changes
      // It copies the depth to another resource
      if (game_device_data.frame_phase == FramePhase::GBUFFER)
      {
         game_device_data.frame_phase = FramePhase::UNDEFINED;
         //reshade::log::message(reshade::log::level::info, "Frame phase: Undefined.");
#if DEVELOPMENT
         const std::shared_lock lock_trace(s_mutex_trace);
         if (trace_running)
         {
            CommandListData& cmd_list_data = *cmd_list->get_private_data<CommandListData>();
            const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
            TraceDrawCallData trace_draw_call_data;
            trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
            trace_draw_call_data.command_list = native_device_context.get();
            trace_draw_call_data.custom_name = "GBuffer End";
            cmd_list_data.trace_draw_calls_data.insert(cmd_list_data.trace_draw_calls_data.end() - 1, trace_draw_call_data);
         }
#endif
      }
      return false;
   }
   
   static void OnMapBufferRegion(reshade::api::device* device, reshade::api::resource resource, uint64_t offset, uint64_t size, reshade::api::map_access access, void** data)
   {
      ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
      ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);
      
      if (device_data.sr_type == SR::Type::None)
         return;
      
      if (access == reshade::api::map_access::write_discard)
      {
         if (game_device_data.frame_phase == FramePhase::GBUFFER || game_device_data.frame_phase == FramePhase::FORWARD)
         {
            D3D11_BUFFER_DESC buffer_desc;
            buffer->GetDesc(&buffer_desc);
            if (buffer_desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER)
            {
               auto& pending = game_device_data.g_pending_cb_data[buffer];
               pending.size = buffer_desc.ByteWidth;

               // Resize once if needed
               if (pending.cpu_data.size() != buffer_desc.ByteWidth)
               {
                  pending.cpu_data.resize(buffer_desc.ByteWidth);
               }
               pending.data_ptr = *data;
               *data = pending.cpu_data.data(); // The game writes data into our allocated buffer
            }
         }
      }
      else if (access == reshade::api::map_access::write_only)
      {
         D3D11_BUFFER_DESC bd;
         ((ID3D11Buffer*)resource.handle)->GetDesc(&bd);
         if (bd.BindFlags == D3D11_BIND_VERTEX_BUFFER)
         {
            auto& device_data = *device->get_private_data<DeviceData>();
            auto& game_device_data = GetGameDeviceData(device_data);

            game_device_data.modifiable_vertex_buffer = (ID3D11Buffer*)resource.handle;
         }
         else if (bd.BindFlags == D3D11_BIND_INDEX_BUFFER)
         {
            auto& device_data = *device->get_private_data<DeviceData>();
            auto& game_device_data = GetGameDeviceData(device_data);

            game_device_data.modifiable_inedx_buffer = (ID3D11Buffer*)resource.handle;
         }
      }
   }
   
   static void OnUnmapBufferRegion(reshade::api::device* device, reshade::api::resource resource)
   {
      ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
      ID3D11Buffer* buffer = reinterpret_cast<ID3D11Buffer*>(resource.handle);
      DeviceData& device_data = *device->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);
      
      if (device_data.sr_type == SR::Type::None)
         return;

      if (game_device_data.frame_phase == FramePhase::GBUFFER || game_device_data.frame_phase == FramePhase::FORWARD)
      {
         auto it = game_device_data.g_pending_cb_data.find(buffer);
         if (it != game_device_data.g_pending_cb_data.cend())
         {
            if (it->second.data_ptr != nullptr)
            {
               memcpy(it->second.data_ptr, it->second.cpu_data.data(), it->second.cpu_data.size()); // Write our allocated buffer to d3d11 allocated memory
               it->second.dirty = true;
               it->second.data_ptr = nullptr;
            }
         }
      }
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
               std::vector<std::byte> shader_code((const std::byte*)original_shader_desc->code, ((const std::byte*)original_shader_desc->code) + original_shader_desc->code_size);
               uint32_t hash = Shader::BinToHash((const uint8_t*)original_shader_desc->code, original_shader_desc->code_size);
               
               if (!shader_hashes_vertex.Contains(hash, reshade::api::shader_stage::vertex))
                  return false;
               
               PatchVertexShader(shader_code);

               ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
               com_ptr<ID3D11VertexShader> patched_shader;
               native_device->CreateVertexShader(shader_code.data(), shader_code.size(), nullptr, &patched_shader);

               game_device_data.modified_vertex_shaders[hash] = patched_shader;
            }
            else if (subobject.type == reshade::api::pipeline_subobject_type::pixel_shader)
            {
               const auto* original_shader_desc = static_cast<reshade::api::shader_desc*>(subobjects[i].data);
               std::vector<std::byte> shader_code((const std::byte*)original_shader_desc->code, ((const std::byte*)original_shader_desc->code) + original_shader_desc->code_size);

               uint32_t hash = Shader::BinToHash((const uint8_t*)original_shader_desc->code, original_shader_desc->code_size);
               /*
               if (!shader_hashes_pixel.Contains(hash, reshade::api::shader_stage::pixel))
                  return false;
               */
               PatchPixelShader(shader_code);

               ID3D11Device* native_device = (ID3D11Device*)(device->get_native());
               com_ptr<ID3D11PixelShader> patched_shader;
               native_device->CreatePixelShader(shader_code.data(), shader_code.size(), nullptr, &patched_shader);

               game_device_data.modified_pixel_shaders[hash] = patched_shader;
            }
            else if (subobject.type == reshade::api::pipeline_subobject_type::blend_state)
            {
               auto* blend_desc = static_cast<reshade::api::blend_desc*>(subobjects[i].data);
               blend_desc->blend_enable[5] = true;
               blend_desc->render_target_write_mask[5] = 15;
               return true;
            }
         }
      }
      return false;
   }
   
   static void OnDestroyResource(reshade::api::device *device, reshade::api::resource resource)
   {
      auto& device_data = *device->get_private_data<DeviceData>();
      auto& game_device_data = GetGameDeviceData(device_data);
      
      if (device_data.sr_type == SR::Type::None)
         return;
#if 0      
      if (device_data.shutting_down.load(std::memory_order_acquire))
         return;
#endif      
      ID3D11Buffer* buffer = nullptr;
      ID3D11Resource* native_resource = reinterpret_cast<ID3D11Resource*>(resource.handle);

      HRESULT hr = native_resource->QueryInterface(&buffer);

      if (SUCCEEDED(hr))
      {
         if (game_device_data.g_pending_cb_data.find(buffer) != game_device_data.g_pending_cb_data.end())
         {
            game_device_data.g_pending_cb_data.erase(buffer);
         }
         buffer->Release();
      }
   }
   
   void OnPresent(ID3D11Device* native_device, DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      
      prev_projection_jitters = projection_jitters;
      
      if (device_data.sr_type == SR::Type::None)
      {
         projection_jitters.x = 0.0;
         projection_jitters.y = 0.0;
      }
      else
      {
         auto index = cb_luma_global_settings.FrameIndex % 8;
         projection_jitters.x = SR::HaltonSequence(index+1, 2);
         projection_jitters.y = SR::HaltonSequence(index+1, 3);
      }
      
      int width  = *(int*)(RenderResolution + 0x00);
      int height = *(int*)(RenderResolution + 0x04);
      device_data.render_resolution.x = static_cast<float>(width);
      device_data.render_resolution.y = static_cast<float>(height);
      
      if (CameraData != nullptr)
      {
         game_device_data.prev_view_projection_matrix = CameraData->view_projection_matrix;
         
         //camera_matrices_previous = camera_matrices_current;
         memcpy(&camera_matrices_previous, &camera_matrices_current, sizeof(CameraMatrices));
      }
      
      ComPtr<ID3D11DeviceContext> context;
      native_device->GetImmediateContext(context.put());
      game_device_data.g_cbuffer.Upload(context.get(), game_device_data.g_shadow_memory.Data(), game_device_data.g_shadow_memory.size);
      
      //(reshade::log::level::info, std::format("Cached cbuffer count: {}", game_device_data.g_pending_cb_data.size()).c_str());
      
      if (!custom_texture_mip_lod_bias_offset)
      {
         std::shared_lock shared_lock_samplers(s_mutex_samplers);
         if (device_data.sr_type != SR::Type::None && !device_data.sr_suppressed)
         {
            device_data.texture_mip_lod_bias_offset = SR::GetMipLODBias((float)height, (float)height); // This results in -1 at output res
         }
         else
         {
            device_data.texture_mip_lod_bias_offset = 0.0f;
         }
      }
      
      {
         game_device_data.remainder_command_list.store(nullptr, std::memory_order_relaxed);
         game_device_data.draw_device_context = nullptr;
         
         game_device_data.source_color.reset();
         game_device_data.source_color_srv.reset();
         game_device_data.source_color_rtv.reset();
         
         game_device_data.depth_texture.reset();
         game_device_data.depth_texture_srv.reset();
         
         game_device_data.frame_phase = FramePhase::NONE;
         counter = 0;
         
         // TODO: Keep alive for buffers that are not destroyed
         // Erase them OnDestroyResource
         // game_device_data.g_pending_cb_data.clear();
         //std::swap(game_device_data.g_prev_shadow_memory, game_device_data.g_shadow_memory);
         std::swap(game_device_data.prev_global_cbuffer_lookup, game_device_data.global_cbuffer_lookup);
         game_device_data.g_shadow_memory.Reset();
         game_device_data.global_cbuffer_lookup.clear();
         game_device_data.prev_global_cbuffer_spatial_index.clear();
         game_device_data.ctx1_cache.clear();
         
         device_data.force_reset_sr = !game_device_data.has_drawn_upscaling;
         game_device_data.has_drawn_fog = false;
         game_device_data.has_copied_fog_to_main_rt = false;
         game_device_data.has_drawn_search_mode = false;
         game_device_data.has_drawn_upscaling = false;
         game_device_data.has_replaced_motion_blur = false;
         device_data.sr_suppressed = false;
         is_search_mode_on = false;
         is_in_battle_mode = false;
         is_hatching_on = false;
         
         if (game_device_data.search_mode_filter_render_state.dirty)
         {
            game_device_data.search_mode_filter_render_state.Reset();
         }
      }
   }
   
   void OnDestroyDeviceData(DeviceData& device_data) override
   {
      auto& game_device_data = GetGameDeviceData(device_data);
      
      game_device_data.Destroy(device_data);
      
      Game::OnDestroyDeviceData(device_data);
   }
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
   if (ul_reason_for_call == DLL_PROCESS_ATTACH)
   {
      Globals::SetGlobals(PROJECT_NAME, "Blue Reflection Second Light Luma mod");
      Globals::DEVELOPMENT_STATE = Globals::ModDevelopmentState::Playable;
      Globals::VERSION = 1;

      luma_settings_cbuffer_index = 13;
      luma_data_cbuffer_index = 12;
      
      // Will cause game to hang upon exit
      enable_samplers_upgrade = true;
#if !DEVELOPMENT
      force_disable_display_composition = true;
#endif
      // Force game to use borderless due to Luma causing game in/out of focus in FSE
      // which leads to game recalculating with jittery physics and stuttering
      // DOESN'T FIX STILL
      prevent_fullscreen_state = false;
      //force_borderless = true;

      swapchain_format_upgrade_type = TextureFormatUpgradesType::None;
      swapchain_upgrade_type = SwapchainUpgradeType::None;
      texture_format_upgrades_type = TextureFormatUpgradesType::None;
      texture_upgrade_formats = {
      };
      
      shader_hashes_copy.pixel_shaders = {
         0x987DC89C,
      };
      shader_hashes_fog.pixel_shaders = {
         0x4763D376,
         0xC95E9F2D,
      };
		// VS
      // TODO: investigate SRIVER shader errors
		shader_hashes_vertex.vertex_shaders = {
			0x0356F78E, //A_Shader_to_draw_outline_model.
			0x04A1EA4C, //A_Shader_to_draw_outline_model.
			0x293B2E0B, //A_Shader_to_draw_outline_model.
			0x4440DE88, //A_Shader_to_draw_outline_model.
			0x4C466497, //A_Shader_to_draw_outline_model.
			0x611E488A, //A_Shader_to_draw_outline_model.
			0x645829EA, //A_Shader_to_draw_outline_model.
			0x78210FD8, //A_Shader_to_draw_outline_model.
			0x82C24BE3, //A_Shader_to_draw_outline_model.
			0x8495D5F6, //A_Shader_to_draw_outline_model.
			0x8E38B29A, //A_Shader_to_draw_outline_model.
			0x8E851138, //A_Shader_to_draw_outline_model.
			0x92DBF940, //A_Shader_to_draw_outline_model.
			0xB34C8293, //A_Shader_to_draw_outline_model.
			0xD2ECB593, //A_Shader_to_draw_outline_model.
			0xD909A60A, //A_Shader_to_draw_outline_model.
			0xEB7A0C61, //A_Shader_to_draw_outline_model.
			0xEDB78642, //A_Shader_to_draw_outline_model.
			0x70F00C4A, //Calm_Water_Shader
			0xEDDCA1AE, //Calm_Water_Shader
			0xEA1841C3, //Calm_Water_Shader_2
			0xFA582144, //Calm_Water_Shader_2
			// 0xA7E3FF90, //Cloud_Height_Shader
			0xA81299B1, //Cloud_Particle_Shader
			// 0x89971680, //Cloud_Plane_Shader
			// 0xC186B098, //Cluster_Decal_Count_Visualize_Shader
			// C186B098, //Cluster_Light_Count_Visualize_Shader
			// 0x3CEB8596, //Contrast_Shader
			0x1EFC0357, //DBPL
			0x7719FD48, //DBSL
			// 0xD3102C0D, //DepthSilhouetteShader
			// D3102C0D, //DitherColorShader
			// D3102C0D, //DitherMonochromeShader
			0x04B1C137, //e3d_scroll
			0x1C631C71, //e3d_scroll
			0x398DD324, //e3d_scroll
			0x52418053, //e3d_scroll
			0xF385A5BE, //e3d_scroll
			// D3102C0D, //Effect2DAfterToneMapAndGammaShader
			// D3102C0D, //Effect2DPreInvToneMapAndGammar
			// D3102C0D, //GaussianBlurXShader
			// D3102C0D, //GaussianBlurYShader
			0x668F8B18, //Grass_Shader
			// 0x238BCC5A, //Grass_ShadowMap_Shader
			// 0x474383BF, //Grass_ShadowMap_Shader
			// 0x5B824617, //Grass_ShadowMap_Shader
			// 0x70BB6E14, //Grass_ShadowMap_Shader
			// 0xDE757C14, //Grass_ShadowMap_Shader
			// 0xE25E5B55, //Grass_ShadowMap_Shader
			// D3102C0D, //HeatHazeShader
			// 0x86343451, //HeightFog_Shader
			// 0x04A76E78, //Horizon-Based_Ambient_Occlusion_Shader
			// D3102C0D, //Horizon-Based_Ambient_Occlusion_Shader
			// 0x38BF9BB6, //IPU_Shader
			// 0xF8AE5526, //IPU_Shading_Shader
			// 0x06AFB822, //KTGL_Effect_Shader
			// 0x0C092D5B, //KTGL_Effect_Shader
			// 0x0C78FB1D, //KTGL_Effect_Shader
			// 0x11FEEAE9, //KTGL_Effect_Shader
			// 0x15C0DAB6, //KTGL_Effect_Shader
			// 0x15D22FEF, //KTGL_Effect_Shader
			// 0x19FBAD84, //KTGL_Effect_Shader
			// 0x1A7A3EC4, //KTGL_Effect_Shader
			// 0x1E3A8C91, //KTGL_Effect_Shader
			// 0x21F36BC3, //KTGL_Effect_Shader
			// 0x22D495AA, //KTGL_Effect_Shader
			// 0x24706F3E, //KTGL_Effect_Shader
			// 0x25525F47, //KTGL_Effect_Shader
			// 0x2AAAB8FC, //KTGL_Effect_Shader
			// 0x2AC6DF40, //KTGL_Effect_Shader
			// 0x2E8CC37E, //KTGL_Effect_Shader
			// 0x3279C909, //KTGL_Effect_Shader
			// 0x40E57017, //KTGL_Effect_Shader
			// 0x47BB7C1A, //KTGL_Effect_Shader
			// 0x59DF9181, //KTGL_Effect_Shader
			// 0x5EE35CE5, //KTGL_Effect_Shader
			// 0x6439CD7F, //KTGL_Effect_Shader
			// 0x6A0026A2, //KTGL_Effect_Shader
			// 0x6A9CEB21, //KTGL_Effect_Shader
			// 0x6DDB0429, //KTGL_Effect_Shader
			// 0x7E36F3AB, //KTGL_Effect_Shader
			// 0x7E42AC39, //KTGL_Effect_Shader
			// 0x813D9049, //KTGL_Effect_Shader
			// 0x84C1842E, //KTGL_Effect_Shader
			// 0x86698BF8, //KTGL_Effect_Shader
			// 0x8FC66936, //KTGL_Effect_Shader
			// 0x91C3293E, //KTGL_Effect_Shader
			// 0x956AC8DF, //KTGL_Effect_Shader
			// 0x95D4F8CC, //KTGL_Effect_Shader
			// 0x9683C6DF, //KTGL_Effect_Shader
			// 0x975CBC90, //KTGL_Effect_Shader
			// 0x9AA88DE7, //KTGL_Effect_Shader
			// 0x9E76A0CA, //KTGL_Effect_Shader
			// 0xA0C9870C, //KTGL_Effect_Shader
			// 0xA109B3AB, //KTGL_Effect_Shader
			// 0xA1D6A582, //KTGL_Effect_Shader
			// 0xA80D17E4, //KTGL_Effect_Shader
			// 0xA8956E71, //KTGL_Effect_Shader
			// 0xAB9D33C4, //KTGL_Effect_Shader
			// 0xB7DBB8B2, //KTGL_Effect_Shader
			// 0xBC82144D, //KTGL_Effect_Shader
			// 0xBCE011EB, //KTGL_Effect_Shader
			// 0xBEA1B547, //KTGL_Effect_Shader
			// 0xC03219ED, //KTGL_Effect_Shader
			// 0xD1C471A9, //KTGL_Effect_Shader
			// 0xD4623B80, //KTGL_Effect_Shader
			// 0xD4CD0454, //KTGL_Effect_Shader
			// 0xD8427254, //KTGL_Effect_Shader
			// 0xD8A0BB31, //KTGL_Effect_Shader
			// 0xDD62F48D, //KTGL_Effect_Shader
			// 0xDE2C1EDA, //KTGL_Effect_Shader
			// 0xDEAABB97, //KTGL_Effect_Shader
			// 0xE0A6D47D, //KTGL_Effect_Shader
			// 0xE9DEC362, //KTGL_Effect_Shader
			// 0xF1C0FC4D, //KTGL_Effect_Shader
			// 0xF1E34C88, //KTGL_Effect_Shader
			// 0xF2B37262, //KTGL_Effect_Shader
			// 0xF33D46EA, //KTGL_Effect_Shader
			// 0xF3820B0B, //KTGL_Effect_Shader
			// 0xF69D4BB7, //KTGL_Effect_Shader
			// 0x48413E93, //LPV_Injection_Shader
			// 0x872CD310, //LPV_Injection_Shader
			// 0x6F8EF239, //Merge_Real-Time_Local_Reflection_Shader
			// 0x3997C1C0, //Noise_Shader
			// 0x23EB397E, //Offscreen_Glitch_Shader
			// 0x12B3E01C, //PBKTF-GbL2wRtt2
			// 0x237AC43E, //PBKTF-GbL2wRtt2
			// 0x46BA3379, //PBKTF-GbL2wRtt2
			// 0x73517008, //PBKTF-GbL2wRtt2
			// 0x8AB6B6A1, //PBKTF-GbL2wRtt2
			// 0xACABAB36, //PBKTF-GbL2wRtt2
			// 0xB527EC97, //PBKTF-GbL2wRtt2
			// 0xCDBAC663, //PBKTF-GbL2wRtt2
			// 0xF57609A6, //PBKTF-GbL2wRtt2
			0x254B32AB, //Physically-Based_Mixed_Metal_Non-Metal_Tree2_Shader
			0x7232FFB9, //Physically-Based_Mixed_Metal_Non-Metal_Tree2_Shader
			0x749149DC, //Physically-Based_Mixed_Metal_Non-Metal_Tree2_Shader
			0xF02A37E5, //Physically-Based_Mixed_Metal_Non-Metal_Tree2_Shader
			0x06F4EC5C, //Physically-Based_Standard_Tree_Branch_Shader
			0x1540FFFE, //Physically-Based_Standard_Tree_Branch_Shader
			0x2D6AA3A4, //Physically-Based_Standard_Tree_Branch_Shader
			0x502FD233, //Physically-Based_Standard_Tree_Branch_Shader
			0x605F5CA1, //Physically-Based_Standard_Tree_Branch_Shader
			0x666D52AF, //Physically-Based_Standard_Tree_Branch_Shader
			0x6AFAF3E3, //Physically-Based_Standard_Tree_Branch_Shader
			0x71544EF8, //Physically-Based_Standard_Tree_Branch_Shader
			0xD1FADED5, //Physically-Based_Standard_Tree_Branch_Shader
			0xE0819B17, //Physically-Based_Standard_Tree_Branch_Shader
			0xE2E374EB, //Physically-Based_Standard_Tree_Branch_Shader
			0xF157920A, //Physically-Based_Standard_Tree_Branch_Shader
			0xF7E638B3, //Physically-Based_Standard_Tree_Branch_Shader
			// 0xEFC5515F, //Physically_Based_After_Rain_Shader
			// EFC5515F, //Physically_Based_Deferred_Decal_Shader
			// 6F8EF239, //Physically_Based_Deferred_Shading_Shader
			0x010F34CF, //Physically_Based_Standard_Both_Shader
			0x04187F5A, //Physically_Based_Standard_Both_Shader
			0x043C2F23, //Physically_Based_Standard_Both_Shader
			0x045F38B8, //Physically_Based_Standard_Both_Shader
			0x046F96A7, //Physically_Based_Standard_Both_Shader
			0x05348533, //Physically_Based_Standard_Both_Shader
			0x058D0BC7, //Physically_Based_Standard_Both_Shader
			0x06A470E0, //Physically_Based_Standard_Both_Shader
			0x06D13970, //Physically_Based_Standard_Both_Shader
			0x0772F590, //Physically_Based_Standard_Both_Shader
			0x07CAEEC4, //Physically_Based_Standard_Both_Shader
			0x08AA4669, //Physically_Based_Standard_Both_Shader
			0x08D5F9D7, //Physically_Based_Standard_Both_Shader
			0x0951450F, //Physically_Based_Standard_Both_Shader
			0x0992E0C5, //Physically_Based_Standard_Both_Shader
			0x09BBD8AD, //Physically_Based_Standard_Both_Shader
			0x09D8466E, //Physically_Based_Standard_Both_Shader
			0x0A308B77, //Physically_Based_Standard_Both_Shader
			0x0A5D9762, //Physically_Based_Standard_Both_Shader
			0x0A760BA6, //Physically_Based_Standard_Both_Shader
			0x0C56C93E, //Physically_Based_Standard_Both_Shader
			0x0CEA0817, //Physically_Based_Standard_Both_Shader
			0x0ECBDF79, //Physically_Based_Standard_Both_Shader
			0x0FA0E581, //Physically_Based_Standard_Both_Shader
			0x0FC98679, //Physically_Based_Standard_Both_Shader
			0x0FEE645B, //Physically_Based_Standard_Both_Shader
			0x0FEE74F2, //Physically_Based_Standard_Both_Shader
			0x0FF19F20, //Physically_Based_Standard_Both_Shader
			0x10F55BF3, //Physically_Based_Standard_Both_Shader
			0x1113491C, //Physically_Based_Standard_Both_Shader
			0x1203AC9F, //Physically_Based_Standard_Both_Shader
			0x12075375, //Physically_Based_Standard_Both_Shader
			0x12649DC5, //Physically_Based_Standard_Both_Shader
			0x12D53B44, //Physically_Based_Standard_Both_Shader
			0x130A07DD, //Physically_Based_Standard_Both_Shader
			0x131CBAA1, //Physically_Based_Standard_Both_Shader
			0x132DE147, //Physically_Based_Standard_Both_Shader
			0x13C1ABAA, //Physically_Based_Standard_Both_Shader
			0x15137C6F, //Physically_Based_Standard_Both_Shader
			0x15B712EE, //Physically_Based_Standard_Both_Shader
			0x1612D129, //Physically_Based_Standard_Both_Shader
			0x1680E2C9, //Physically_Based_Standard_Both_Shader
			0x173282FE, //Physically_Based_Standard_Both_Shader
			0x1794FF8F, //Physically_Based_Standard_Both_Shader
			0x18A59EE7, //Physically_Based_Standard_Both_Shader
			0x19016D6C, //Physically_Based_Standard_Both_Shader
			0x19709E29, //Physically_Based_Standard_Both_Shader
			0x1A428594, //Physically_Based_Standard_Both_Shader
			0x1A4F6E69, //Physically_Based_Standard_Both_Shader
			0x1B86412F, //Physically_Based_Standard_Both_Shader
			0x1C1B0ACB, //Physically_Based_Standard_Both_Shader
			0x1C4AD91D, //Physically_Based_Standard_Both_Shader
			0x1C9EC128, //Physically_Based_Standard_Both_Shader
			0x1D844421, //Physically_Based_Standard_Both_Shader
			0x1E87DFFD, //Physically_Based_Standard_Both_Shader
			0x1F22D15B, //Physically_Based_Standard_Both_Shader
			0x200C9AD4, //Physically_Based_Standard_Both_Shader
			0x203266D7, //Physically_Based_Standard_Both_Shader
			0x20D5D741, //Physically_Based_Standard_Both_Shader
			0x2158E72E, //Physically_Based_Standard_Both_Shader
			0x218A506B, //Physically_Based_Standard_Both_Shader
			0x21CD6D47, //Physically_Based_Standard_Both_Shader
			0x23017892, //Physically_Based_Standard_Both_Shader
			0x24677A9B, //Physically_Based_Standard_Both_Shader
			0x254E0303, //Physically_Based_Standard_Both_Shader
			0x25A8A16D, //Physically_Based_Standard_Both_Shader
			0x26EB8C02, //Physically_Based_Standard_Both_Shader
			0x272A9D11, //Physically_Based_Standard_Both_Shader
			0x298D7188, //Physically_Based_Standard_Both_Shader
			0x2B899114, //Physically_Based_Standard_Both_Shader
			0x2BC0E4B5, //Physically_Based_Standard_Both_Shader
			0x2CDD74FE, //Physically_Based_Standard_Both_Shader
			0x2EA8C591, //Physically_Based_Standard_Both_Shader
			0x2F78EC1E, //Physically_Based_Standard_Both_Shader
			0x2F80F598, //Physically_Based_Standard_Both_Shader
			0x2F8FE069, //Physically_Based_Standard_Both_Shader
			0x310F464C, //Physically_Based_Standard_Both_Shader
			0x319296C8, //Physically_Based_Standard_Both_Shader
			0x3214319D, //Physically_Based_Standard_Both_Shader
			0x322A335B, //Physically_Based_Standard_Both_Shader
			0x32E4BB1A, //Physically_Based_Standard_Both_Shader
			0x343D5E12, //Physically_Based_Standard_Both_Shader
			0x36143A1D, //Physically_Based_Standard_Both_Shader
			0x38DAA23F, //Physically_Based_Standard_Both_Shader
			0x394D4CDE, //Physically_Based_Standard_Both_Shader
			0x3B300010, //Physically_Based_Standard_Both_Shader
			0x3B404DD1, //Physically_Based_Standard_Both_Shader
			0x3BE93107, //Physically_Based_Standard_Both_Shader
			0x3C071463, //Physically_Based_Standard_Both_Shader
			0x3C719B18, //Physically_Based_Standard_Both_Shader
			0x3D609E5D, //Physically_Based_Standard_Both_Shader
			0x3DD7EBB1, //Physically_Based_Standard_Both_Shader
			0x3E59C011, //Physically_Based_Standard_Both_Shader
			0x3E82721A, //Physically_Based_Standard_Both_Shader
			0x3E949842, //Physically_Based_Standard_Both_Shader
			0x3F016156, //Physically_Based_Standard_Both_Shader
			0x3F8D6A66, //Physically_Based_Standard_Both_Shader
			0x3FBB5126, //Physically_Based_Standard_Both_Shader
			0x3FEC1BFA, //Physically_Based_Standard_Both_Shader
			0x40D7991E, //Physically_Based_Standard_Both_Shader
			0x41E44886, //Physically_Based_Standard_Both_Shader
			0x430A8D59, //Physically_Based_Standard_Both_Shader
			0x44E4A66F, //Physically_Based_Standard_Both_Shader
			0x46E476BD, //Physically_Based_Standard_Both_Shader
			0x47E214D4, //Physically_Based_Standard_Both_Shader
			0x4843381E, //Physically_Based_Standard_Both_Shader
			0x48624F1E, //Physically_Based_Standard_Both_Shader
			0x48743E85, //Physically_Based_Standard_Both_Shader
			0x4A4F0607, //Physically_Based_Standard_Both_Shader
			0x4C4BEB5F, //Physically_Based_Standard_Both_Shader
			0x4CDD0A92, //Physically_Based_Standard_Both_Shader
			0x4D686CF9, //Physically_Based_Standard_Both_Shader
			0x4ED959F8, //Physically_Based_Standard_Both_Shader
			0x509DEB46, //Physically_Based_Standard_Both_Shader
			0x51D11DE0, //Physically_Based_Standard_Both_Shader
			0x53A1F4AA, //Physically_Based_Standard_Both_Shader
			0x55A2D5FE, //Physically_Based_Standard_Both_Shader
			0x573CEE7D, //Physically_Based_Standard_Both_Shader
			0x57680896, //Physically_Based_Standard_Both_Shader
			0x57725D24, //Physically_Based_Standard_Both_Shader
			0x57857AD2, //Physically_Based_Standard_Both_Shader
			0x5786D973, //Physically_Based_Standard_Both_Shader
			0x57F4D135, //Physically_Based_Standard_Both_Shader
			0x590191F8, //Physically_Based_Standard_Both_Shader
			0x598C2F47, //Physically_Based_Standard_Both_Shader
			0x5A10ECD3, //Physically_Based_Standard_Both_Shader
			0x5AB22C71, //Physically_Based_Standard_Both_Shader
			0x5B48E663, //Physically_Based_Standard_Both_Shader
			0x5BAEA1A5, //Physically_Based_Standard_Both_Shader
			0x5BC7DE13, //Physically_Based_Standard_Both_Shader
			0x5BF7B53B, //Physically_Based_Standard_Both_Shader
			0x5C9FF5E6, //Physically_Based_Standard_Both_Shader
			0x5CE31F63, //Physically_Based_Standard_Both_Shader
			0x5CE6B8D5, //Physically_Based_Standard_Both_Shader
			0x5DA649B7, //Physically_Based_Standard_Both_Shader
			0x5EAD3F8F, //Physically_Based_Standard_Both_Shader
			0x60433B8C, //Physically_Based_Standard_Both_Shader
			0x609B79A3, //Physically_Based_Standard_Both_Shader
			0x60ECD382, //Physically_Based_Standard_Both_Shader
			0x610A24E6, //Physically_Based_Standard_Both_Shader
			0x61BC7DAC, //Physically_Based_Standard_Both_Shader
			0x61E3D2FE, //Physically_Based_Standard_Both_Shader
			0x62213C22, //Physically_Based_Standard_Both_Shader
			0x634B606C, //Physically_Based_Standard_Both_Shader
			0x635E8B59, //Physically_Based_Standard_Both_Shader
			0x636F71BD, //Physically_Based_Standard_Both_Shader
			0x68B78F7A, //Physically_Based_Standard_Both_Shader
			0x690AB5D7, //Physically_Based_Standard_Both_Shader
			0x69140051, //Physically_Based_Standard_Both_Shader
			0x6C658942, //Physically_Based_Standard_Both_Shader
			0x6C70F00D, //Physically_Based_Standard_Both_Shader
			0x6D0D9438, //Physically_Based_Standard_Both_Shader
			0x6D184A7E, //Physically_Based_Standard_Both_Shader
			0x6D2FFCD6, //Physically_Based_Standard_Both_Shader
			0x6DF34034, //Physically_Based_Standard_Both_Shader
			0x716EB73A, //Physically_Based_Standard_Both_Shader
			0x72D0AD0F, //Physically_Based_Standard_Both_Shader
			0x7397CA8B, //Physically_Based_Standard_Both_Shader
			0x747E1CA9, //Physically_Based_Standard_Both_Shader
			0x74C54B11, //Physically_Based_Standard_Both_Shader
			0x76629643, //Physically_Based_Standard_Both_Shader
			0x77292CFA, //Physically_Based_Standard_Both_Shader
			0x778A910D, //Physically_Based_Standard_Both_Shader
			0x782354FE, //Physically_Based_Standard_Both_Shader
			0x79E26415, //Physically_Based_Standard_Both_Shader
			0x79F69077, //Physically_Based_Standard_Both_Shader
			0x7B7103CF, //Physically_Based_Standard_Both_Shader
			0x7E22CECC, //Physically_Based_Standard_Both_Shader
			0x7F328100, //Physically_Based_Standard_Both_Shader
			0x80A4AF6B, //Physically_Based_Standard_Both_Shader
			0x812E4B53, //Physically_Based_Standard_Both_Shader
			0x81AA32B5, //Physically_Based_Standard_Both_Shader
			0x822D71C0, //Physically_Based_Standard_Both_Shader
			0x82FA67AB, //Physically_Based_Standard_Both_Shader
			0x83ADE925, //Physically_Based_Standard_Both_Shader
			0x83EF1126, //Physically_Based_Standard_Both_Shader
			0x8451C64F, //Physically_Based_Standard_Both_Shader
			0x8516E767, //Physically_Based_Standard_Both_Shader
			0x85DE0AED, //Physically_Based_Standard_Both_Shader
			0x863B36C3, //Physically_Based_Standard_Both_Shader
			0x86C2ADFE, //Physically_Based_Standard_Both_Shader
			0x883D5101, //Physically_Based_Standard_Both_Shader
			0x886456C5, //Physically_Based_Standard_Both_Shader
			0x88FD0794, //Physically_Based_Standard_Both_Shader
			0x896F6AE8, //Physically_Based_Standard_Both_Shader
			0x89C81C51, //Physically_Based_Standard_Both_Shader
			0x8AB2C54E, //Physically_Based_Standard_Both_Shader
			0x8B6FBC2D, //Physically_Based_Standard_Both_Shader
			0x8BC43BBF, //Physically_Based_Standard_Both_Shader
			0x8C45EF74, //Physically_Based_Standard_Both_Shader
			0x8CD6C035, //Physically_Based_Standard_Both_Shader
			0x8D63F9AE, //Physically_Based_Standard_Both_Shader
			0x8FA8BCE5, //Physically_Based_Standard_Both_Shader
			0x90BE3AA2, //Physically_Based_Standard_Both_Shader
			0x919A05CA, //Physically_Based_Standard_Both_Shader
			0x924C6051, //Physically_Based_Standard_Both_Shader
			0x93BD9931, //Physically_Based_Standard_Both_Shader
			0x93F63F7A, //Physically_Based_Standard_Both_Shader
			0x94EB98FD, //Physically_Based_Standard_Both_Shader
			0x95625FBD, //Physically_Based_Standard_Both_Shader
			0x958765AE, //Physically_Based_Standard_Both_Shader
			0x985B19CA, //Physically_Based_Standard_Both_Shader
			0x9A2FD35B, //Physically_Based_Standard_Both_Shader
			0x9AA48399, //Physically_Based_Standard_Both_Shader
			0x9B315C6E, //Physically_Based_Standard_Both_Shader
			0x9B389161, //Physically_Based_Standard_Both_Shader
			0x9B466828, //Physically_Based_Standard_Both_Shader
			0x9B6D98BD, //Physically_Based_Standard_Both_Shader
			0x9B929F29, //Physically_Based_Standard_Both_Shader
			0x9BAFF123, //Physically_Based_Standard_Both_Shader
			0x9BF62C76, //Physically_Based_Standard_Both_Shader
			0x9CBFB107, //Physically_Based_Standard_Both_Shader
			0x9CDD6F2D, //Physically_Based_Standard_Both_Shader
			0x9E5AC71B, //Physically_Based_Standard_Both_Shader
			0x9EC065AE, //Physically_Based_Standard_Both_Shader
			0x9F384924, //Physically_Based_Standard_Both_Shader
			0x9FF79B84, //Physically_Based_Standard_Both_Shader
			0xA0B29C32, //Physically_Based_Standard_Both_Shader
			0xA1C53534, //Physically_Based_Standard_Both_Shader
			0xA1E48BC6, //Physically_Based_Standard_Both_Shader
			0xA311DE48, //Physically_Based_Standard_Both_Shader
			0xA31DCA39, //Physically_Based_Standard_Both_Shader
			0xA45072BD, //Physically_Based_Standard_Both_Shader
			0xA513919A, //Physically_Based_Standard_Both_Shader
			0xA58488E2, //Physically_Based_Standard_Both_Shader
			0xA5EE5A8F, //Physically_Based_Standard_Both_Shader
			0xA72BF812, //Physically_Based_Standard_Both_Shader
			0xA7F7B83D, //Physically_Based_Standard_Both_Shader
			0xA89ABE68, //Physically_Based_Standard_Both_Shader
			0xA9B08BAE, //Physically_Based_Standard_Both_Shader
			0xAA78773A, //Physically_Based_Standard_Both_Shader
			0xAA7F11F0, //Physically_Based_Standard_Both_Shader
			0xAAEAEFCB, //Physically_Based_Standard_Both_Shader
			0xAD4A5127, //Physically_Based_Standard_Both_Shader
			0xADD53C42, //Physically_Based_Standard_Both_Shader
			0xAE377F33, //Physically_Based_Standard_Both_Shader
			0xAE8CEC3D, //Physically_Based_Standard_Both_Shader
			0xAEC60C4C, //Physically_Based_Standard_Both_Shader
			0xB1354D71, //Physically_Based_Standard_Both_Shader
			0xB16C058A, //Physically_Based_Standard_Both_Shader
			0xB2C579FA, //Physically_Based_Standard_Both_Shader
			0xB672D51D, //Physically_Based_Standard_Both_Shader
			0xB6AFFC2F, //Physically_Based_Standard_Both_Shader
			0xB6D2BE06, //Physically_Based_Standard_Both_Shader
			0xB77A1B5D, //Physically_Based_Standard_Both_Shader
			0xB84D6C74, //Physically_Based_Standard_Both_Shader
			0xB8AE4747, //Physically_Based_Standard_Both_Shader
			0xB9F6E206, //Physically_Based_Standard_Both_Shader
			0xBB575237, //Physically_Based_Standard_Both_Shader
			0xBBA81CD7, //Physically_Based_Standard_Both_Shader
			0xBCC8000A, //Physically_Based_Standard_Both_Shader
			0xBD3A7FDA, //Physically_Based_Standard_Both_Shader
			0xBF37889C, //Physically_Based_Standard_Both_Shader
			0xBFFAE4DA, //Physically_Based_Standard_Both_Shader
			0xC0F0B147, //Physically_Based_Standard_Both_Shader
			0xC207F742, //Physically_Based_Standard_Both_Shader
			0xC3EB32BE, //Physically_Based_Standard_Both_Shader
			0xC4097456, //Physically_Based_Standard_Both_Shader
			0xC4C0A0B5, //Physically_Based_Standard_Both_Shader
			0xC51AD711, //Physically_Based_Standard_Both_Shader
			0xC5909EA7, //Physically_Based_Standard_Both_Shader
			0xC64E65F8, //Physically_Based_Standard_Both_Shader
			0xC6B51E16, //Physically_Based_Standard_Both_Shader
			0xC71577E0, //Physically_Based_Standard_Both_Shader
			0xC7C6E4FC, //Physically_Based_Standard_Both_Shader
			0xC88DF8A2, //Physically_Based_Standard_Both_Shader
			0xC8DDB3D6, //Physically_Based_Standard_Both_Shader
			0xC9C9E31C, //Physically_Based_Standard_Both_Shader
			0xC9FCF617, //Physically_Based_Standard_Both_Shader
			0xCA4F9D3A, //Physically_Based_Standard_Both_Shader
			0xCAA332EE, //Physically_Based_Standard_Both_Shader
			0xCB52337B, //Physically_Based_Standard_Both_Shader
			0xCB93470F, //Physically_Based_Standard_Both_Shader
			0xCBA4A49A, //Physically_Based_Standard_Both_Shader
			0xCBA9EB86, //Physically_Based_Standard_Both_Shader
			0xCBF985DC, //Physically_Based_Standard_Both_Shader
			0xCD16E4CA, //Physically_Based_Standard_Both_Shader
			0xCD8B25DB, //Physically_Based_Standard_Both_Shader
			0xCD99F333, //Physically_Based_Standard_Both_Shader
			0xCF2062FC, //Physically_Based_Standard_Both_Shader
			0xCF8A7A5D, //Physically_Based_Standard_Both_Shader
			0xD07E9815, //Physically_Based_Standard_Both_Shader
			0xD1A75E70, //Physically_Based_Standard_Both_Shader
			0xD23A05C9, //Physically_Based_Standard_Both_Shader
			0xD3565A52, //Physically_Based_Standard_Both_Shader
			0xD3C491B9, //Physically_Based_Standard_Both_Shader
			0xD58C95B8, //Physically_Based_Standard_Both_Shader
			0xD6B28C5B, //Physically_Based_Standard_Both_Shader
			0xD76BF7C7, //Physically_Based_Standard_Both_Shader
			0xD7DBC6C7, //Physically_Based_Standard_Both_Shader
			0xD7F1A482, //Physically_Based_Standard_Both_Shader
			0xD8737B56, //Physically_Based_Standard_Both_Shader
			0xD9F1136D, //Physically_Based_Standard_Both_Shader
			0xDA02C0E2, //Physically_Based_Standard_Both_Shader
			0xDA6ABA42, //Physically_Based_Standard_Both_Shader
			0xDAE6C516, //Physically_Based_Standard_Both_Shader
			0xDBE8853B, //Physically_Based_Standard_Both_Shader
			0xDC951FDE, //Physically_Based_Standard_Both_Shader
			0xDCB930B2, //Physically_Based_Standard_Both_Shader
			0xDE9A1A37, //Physically_Based_Standard_Both_Shader
			0xDF4DD2D6, //Physically_Based_Standard_Both_Shader
			0xDF5BAC56, //Physically_Based_Standard_Both_Shader
			0xDFD6A64A, //Physically_Based_Standard_Both_Shader
			0xE0B6F506, //Physically_Based_Standard_Both_Shader
			0xE146094B, //Physically_Based_Standard_Both_Shader
			0xE2E809F8, //Physically_Based_Standard_Both_Shader
			0xE3166DE9, //Physically_Based_Standard_Both_Shader
			0xE33B7E2E, //Physically_Based_Standard_Both_Shader
			0xE3E52D3F, //Physically_Based_Standard_Both_Shader
			0xE47FDB43, //Physically_Based_Standard_Both_Shader
			0xE6088AE6, //Physically_Based_Standard_Both_Shader
			0xE71371CB, //Physically_Based_Standard_Both_Shader
			0xE72E8E30, //Physically_Based_Standard_Both_Shader
			0xE8125174, //Physically_Based_Standard_Both_Shader
			0xE8384FC4, //Physically_Based_Standard_Both_Shader
			0xE9662D50, //Physically_Based_Standard_Both_Shader
			0xEA5A3CD1, //Physically_Based_Standard_Both_Shader
			0xEAD54366, //Physically_Based_Standard_Both_Shader
			0xEB78334B, //Physically_Based_Standard_Both_Shader
			0xEB9C77DC, //Physically_Based_Standard_Both_Shader
			0xEBD38238, //Physically_Based_Standard_Both_Shader
			0xEBF3C70F, //Physically_Based_Standard_Both_Shader
			0xECFDFE24, //Physically_Based_Standard_Both_Shader
			0xED551978, //Physically_Based_Standard_Both_Shader
			0xED7BD18A, //Physically_Based_Standard_Both_Shader
			0xED8678EF, //Physically_Based_Standard_Both_Shader
			0xEDBDF144, //Physically_Based_Standard_Both_Shader
			0xEDD9E255, //Physically_Based_Standard_Both_Shader
			0xEE8984D9, //Physically_Based_Standard_Both_Shader
			0xEF9AD76A, //Physically_Based_Standard_Both_Shader
			0xF0C80079, //Physically_Based_Standard_Both_Shader
			0xF0EEFB00, //Physically_Based_Standard_Both_Shader
			0xF18168A3, //Physically_Based_Standard_Both_Shader
			0xF2399900, //Physically_Based_Standard_Both_Shader
			0xF3750CA8, //Physically_Based_Standard_Both_Shader
			0xF38AA098, //Physically_Based_Standard_Both_Shader
			0xF38AD66D, //Physically_Based_Standard_Both_Shader
			0xF59C98FE, //Physically_Based_Standard_Both_Shader
			0xF60FD467, //Physically_Based_Standard_Both_Shader
			0xF67B7F8B, //Physically_Based_Standard_Both_Shader
			0xF87792F2, //Physically_Based_Standard_Both_Shader
			0xF8AC509C, //Physically_Based_Standard_Both_Shader
			0xF9B3C845, //Physically_Based_Standard_Both_Shader
			0xF9B9BEE2, //Physically_Based_Standard_Both_Shader
			0xFBC0BFA5, //Physically_Based_Standard_Both_Shader
			0xFC0CB704, //Physically_Based_Standard_Both_Shader
			0xFCE4C8DB, //Physically_Based_Standard_Both_Shader
			0xFCEEFC28, //Physically_Based_Standard_Both_Shader
			0xFDF5C69E, //Physically_Based_Standard_Both_Shader
			0xFE2F2549, //Physically_Based_Standard_Both_Shader
			0xFF672EA2, //Physically_Based_Standard_Both_Shader
			0x091F0E7B, //Physically_Based_Standard_Shader
			// 0951450F, //Physically_Based_Standard_Shader
			0x0C4608F3, //Physically_Based_Standard_Shader
			0x1048BD17, //Physically_Based_Standard_Shader
			0x186218A6, //Physically_Based_Standard_Shader
			0x196A10D5, //Physically_Based_Standard_Shader
			// 1E87DFFD, //Physically_Based_Standard_Shader
			0x1F7B1860, //Physically_Based_Standard_Shader
			0x29058484, //Physically_Based_Standard_Shader
			0x33F07D79, //Physically_Based_Standard_Shader
			0x39E5525A, //Physically_Based_Standard_Shader
			0x4F3AA0BA, //Physically_Based_Standard_Shader
			0x785D5A94, //Physically_Based_Standard_Shader
			0x7871F71F, //Physically_Based_Standard_Shader
			// 79E26415, //Physically_Based_Standard_Shader
			// 8D63F9AE, //Physically_Based_Standard_Shader
			0x8F682F27, //Physically_Based_Standard_Shader
			// 9FF79B84, //Physically_Based_Standard_Shader
			0xAA32484C, //Physically_Based_Standard_Shader
			0xBE91AD09, //Physically_Based_Standard_Shader
			0xBFC86E30, //Physically_Based_Standard_Shader
			0xC0FCD713, //Physically_Based_Standard_Shader
			0xC3ACC4F7, //Physically_Based_Standard_Shader
			0xC413E857, //Physically_Based_Standard_Shader
			0xC4ACB720, //Physically_Based_Standard_Shader
			// C51AD711, //Physically_Based_Standard_Shader
			// D6B28C5B, //Physically_Based_Standard_Shader
			0xE661B585, //Physically_Based_Standard_Shader
			0xEE5E7066, //Physically_Based_Standard_Shader
			0xEFAB627D, //Physically_Based_Standard_Shader
			// F8AC509C, //Physically_Based_Standard_Shader
			// F9B3C845, //Physically_Based_Standard_Shader
			0xFBA553E2, //Physically_Based_Standard_Shader
			// FBC0BFA5, //Physically_Based_Standard_Shader
			0xFDC71808, //Physically_Based_Standard_Shader
			// 0x90BE487E, //Physically_G-buffer_Edit_Shader
			// 04A76E78, //Real-Time_Local_Reflection_Shader
			// 0xB993F0CB, //Real-Time_Local_Reflection_Shader
			// 0xC2E6A10F, //Real-Time_Local_Reflection_Shader
			// D3102C0D, //SaturationLightnessGammaShader
			// D3102C0D, //SaturationScaleExShader
			// 0x06C81328, //Shadow_Map_Shader
			// 0x0EEFF4C1, //Shadow_Map_Shader
			// 0x1905A21D, //Shadow_Map_Shader
			// 0x199044BA, //Shadow_Map_Shader
			// 0x1A6B8DEC, //Shadow_Map_Shader
			// 0x1F9922CF, //Shadow_Map_Shader
			// 0x21FEC165, //Shadow_Map_Shader
			// 0x25FFF2EB, //Shadow_Map_Shader
			// 0x34C5F2BE, //Shadow_Map_Shader
			// 0x366009EE, //Shadow_Map_Shader
			// 0x382270B9, //Shadow_Map_Shader
			// 0x38902057, //Shadow_Map_Shader
			// 0x4DB220A3, //Shadow_Map_Shader
			// 0x50C061E5, //Shadow_Map_Shader
			// 0x5BD96E5A, //Shadow_Map_Shader
			// 0x687086B0, //Shadow_Map_Shader
			// 0x69A4B9B7, //Shadow_Map_Shader
			// 0x6C1BD948, //Shadow_Map_Shader
			// 0x7C3AA369, //Shadow_Map_Shader
			// 0x7E8DE1F0, //Shadow_Map_Shader
			// 0x813B6FC2, //Shadow_Map_Shader
			// 0x8148DD0C, //Shadow_Map_Shader
			// 0x8738331E, //Shadow_Map_Shader
			// 0x897EB5ED, //Shadow_Map_Shader
			// 0x936F42ED, //Shadow_Map_Shader
			// 0x9B7A6FF1, //Shadow_Map_Shader
			// 0x9CF36CD8, //Shadow_Map_Shader
			// 0xA0F8B9A6, //Shadow_Map_Shader
			// 0xB09163D8, //Shadow_Map_Shader
			// 0xB894F049, //Shadow_Map_Shader
			// 0xC809FBAB, //Shadow_Map_Shader
			// 0xDB078281, //Shadow_Map_Shader
			// 0xE1471752, //Shadow_Map_Shader
			// 0xE5233626, //Shadow_Map_Shader
			// 0xE7ECA6A0, //Shadow_Map_Shader
			// 0xEFBE5DD6, //Shadow_Map_Shader
			// 0xF227E080, //Shadow_Map_Shader
			// 0xF3A74B7B, //Shadow_Map_Shader
			// 0xFCABA574, //Shadow_Map_Shader
			// 0x19A4186D, //Sky_Plane_Shader
			// 0xD73B73D9, //SRIVER-GbNRr
			// 0x78CBA6FB, //SRIVER-NRr
			// 0x0D6D3F8C, //SSSS_Shader
			// 0xE5D3F2C6, //SSSS_Shader
			0xDD2C4C7C, //Standard_Shader
			0x8BDF88C4, //Star_Shader
			0x2B77F22D, //Stream_Water_Shader
			// 0x530156B6, //Terrain_Shadow_Map_Shader
			// 0x033AA3C7, //Tree2_ShadowMap_Shader
			// 0x2298CA17, //Tree2_ShadowMap_Shader
			// 0x931FF8A3, //Tree2_ShadowMap_Shader
			// 0xEAC80764, //Tree2_ShadowMap_Shader
			0x1F24F663, //TwinkleEyeTranslucence_Shader
			0x1FF525BD, //TwinkleEyeTranslucence_Shader
			0x98E64C00, //TwinkleEyeTranslucence_Shader
			0xA46DBCA8, //TwinkleEyeTranslucence_Shader
			0xB57D3A34, //TwinkleEyeTranslucence_Shader
			0xCB72CD80, //TwinkleEyeTranslucence_Shader
			0x7F7BFEAA, //Uniform_Color_Only_Shader
			0xB14329A5, //Vertex_Color_Only_2d_Shader
			// 38BF9BB6, //Vertex_Color_Only_Shader
			// B14329A5, //Vertex_Color_Only_Shader
			// 0x7E4C1A2B, //Voxelize_Shader
			// 0xAD4E28F9, //Voxelize_Shader
			// 90BE487E, //Write_Indirect_Diffuse_and_fShadow_Value_Shader
		};
      shader_hashes_L2P_vertex.vertex_shaders = {
         0x70F00C4A,
         0xEDDCA1AE,
         0xEA1841C3,
         0xFA582144,
         0x7F7BFEAA,
         0xB14329A5,
      };
      shader_hashes_copy_red.pixel_shaders = {
         0X5897B918,
      };
      shader_hashes_effect_merge.pixel_shaders = {
         0x5182EC11,
      };
      shader_hashes_search_mode.pixel_shaders = {
         0xE05124D1,
      };
      shader_hashes_motion_blur.pixel_shaders = {
         0x27EA459B,
      };
      shader_hashes_hatching_gbuffer.pixel_shaders = {
         0x038CC484,
         0x14785515,
         0x15B5D1E8,
         0x1FB13110,
         0x349C3F34,
         0x40B28571,
         0x44E1A687,
         0x45B12C6C,
         0x671B762C,
         0x7BA222DF,
         0x7C7CD49F,
         0x8663DEE7,
         0x891F3C50,
         0x90CC69DC,
         0x9848CE7A,
         0xA0C9A336,
         0xA2B6102A,
         0xABF97D79,
         0xC33ABF26,
         0xE935A929,
         0xFD27123F,
         0x1A4EE633,
         0xD329C1AA,
         0x03BB3534,
         0x182AD830,
         0x2514D20B,
         0x5309FBAA,
         0x5DC90920,
         0x78AF012D,
         0x97C553BD,
         0xF6E9CE54,
         0x0049CB11,
         0x03C90B38,
         0x06BC2AC8,
         0x0E62BE43,
         0x0FEC3963,
         0x15AB6DB2,
         0x15D16B08,
         0x1919B30C,
         0x1A25D3EA,
         0x29D1AF59,
         0x3586E62D,
         0x386C82C5,
         0x395B8B4C,
         0x3BC4CCFA,
         0x3EE4F252,
         0x408F3CE0,
         0x440ABC6D,
         0x540F99B8,
         0x55FD1783,
         0x567F6526,
         0x5AE729DE,
         0x5CB14A1C,
         0x5E777D8F,
         0x5EF14863,
         0x6257D3A3,
         0x62B32AFC,
         0x638B5827,
         0x71216ECF,
         0x7989C557,
         0x7BCA36AD,
         0x81D6F538,
         0x82680082,
         0x840DD118,
         0x84FF1E64,
         0x8AC982A1,
         0x9495D16B,
         0x969BF403,
         0x99ADFF9F,
         0x9CB2C1C9,
         0x9F70E00A,
         0xA0110FB8,
         0xA246E57B,
         0xA47CC9A6,
         0xA89F9395,
         0xA9C66D43,
         0xAA95F561,
         0xAC690F6F,
         0xAD8835C6,
         0xB559B362,
         0xB6B234D9,
         0xB8766CBA,
         0xBE37E6F3,
         0xBFB571F5,
         0xC0DA8743,
         0xC9B5536F,
         0xD239AC05,
         0xD4D03DDC,
         0xD9AC569C,
         0xD9B57AA5,
         0xDD182C8C,
         0xDD4A1A73,
         0xDF096096,
         0xE0E6DD54,
         0xE76B0D0D,
         0xE7C6CA76,
         0xEC0648BF,
         0xEFD7287B,
         0xF3FAA03A,
         0xF4EC1343,
         0xF6393CB3,
         0xF69410F3,
         0x18B88D18,
         0x668E9188,
         0xBE2104F7,
         0xBF77D46B,
         0xCA5EA7F1,
         0xEEA9659B,
         0x0F8ADFCC,
      };
      shader_hashes_hatching_forward.pixel_shaders = {
         0x08CD7E15,
         0x29315B88,
         0x429B97CC,
         0x44606886,
         0x4C7DCD42,
         0x56C2C63F,
         0x5CC47D9E,
         0x63F480E7,
         0x8F057C4F,
         0x8F377EC3,
         0x90D4031E,
         0xA967D69B,
         0xD84F21B1,
         0xE1F2EE45,
         0xE2A91D7E,
         0xFCF27EAF,
         0x1B3E114A,
         0x3748C2CC,
         0x402860B5,
         0x62E1EAA7,
         0x7BD925C0,
         0x8000B9EE,
         0xC20D03CE,
         0xC76B0AA6,
         0xD46FF13A,
         0xDFEEEF82,
         0xEF956FAB,
         0xF68EC15C,
      };
      shader_hashes_dof_merge.pixel_shaders = {
         0xA13896DC,
         0xFB128186
      };
      shader_hashes_postfx_composite.pixel_shaders = {
         0x0651FD77,
         0x0F539095,
         0x00B5B879,
         0x62EFF154,
         0x9A9B6BB6,
         0x48BE64DD,
         0x968BB1D8,
         0x2D8E0121,
         0x4A5F5CE2,
         0xCAD7869C,
         0x88E8C085,
         0x7DE15276,
         0xD698FE2A,
         0xCB4F0E0B,
         0x2805C72A,
         0x91A02148,
         0xEA314404,
      };
      shader_hashes_postfx_composite_with_depth.pixel_shaders = {
         0x0651FD77,
         0x0F539095,
         0x9A9B6BB6,
         0x48BE64DD,
         0x4A5F5CE2,
         0xCAD7869C,
         0xD698FE2A,
         0xCB4F0E0B,
      };
      
#if DEVELOPMENT
#include "includes\shader_names.h"
      forced_shader_names.emplace(Shader::Hash_StrToNum("5182EC11"), "Effect Merge PS");
      forced_shader_names.emplace(Shader::Hash_StrToNum("987DC89C"), "Copy PS with Alpha Test");
      forced_shader_names.emplace(Shader::Hash_StrToNum("76ACA7AE"), "Alpha Channel PS");
      forced_shader_names.emplace(Shader::Hash_StrToNum("5897B918"), "Copy Depth to Color PS");
      forced_shader_names.emplace(Shader::Hash_StrToNum("A3AB8897"), "Copy Depth to Depth PS");
      forced_shader_names.emplace(Shader::Hash_StrToNum("4E06C8C4"), "I8 Blend PS");
      forced_shader_names.emplace(Shader::Hash_StrToNum("36B871CC"), "Clear Count Buffer CS");
      forced_shader_names.emplace(Shader::Hash_StrToNum("EF422F96"), "Cluster Decal Culling CS");
      forced_shader_names.emplace(Shader::Hash_StrToNum("99EA3DC8"), "Cluster Decal Frustum Culling CS");
      
      forced_shader_names.emplace(Shader::Hash_StrToNum("BB055A34"), "PostEffectAlphaBlendCsCd_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectAlphaBlendCsCd_VS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("BB055A34"), "PostEffectBase_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectBase_VS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("D701336B"), "PostEffectBlur_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectBlur_VS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("F8089F3A"), "PostEffectBlurDirection_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectBlurDirection_VS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("5C9AF816"), "PostEffectBlurEllipse_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectBlurEllipse_VS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("46E7631A"), "PostEffectBlurOld_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectBlurOld_VS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("E2E9CD9C"), "PostEffectBlurRectangle_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectBlurRectangle_VS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("A7838016"), "PostEffectColor_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectColor_VS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("62717FEC"), "PostEffectColorFilterEllipse_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("F530705D"), "PostEffectColorFilterEllipse_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("93218B2B"), "PostEffectColorFilterEllipse_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("44387C1C"), "PostEffectColorFilterEllipse_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("AB620EAC"), "PostEffectColorFilterEllipse_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("8702946F"), "PostEffectColorFilterEllipse_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("B5BB3410"), "PostEffectColorFilterEllipse_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("356ADBCB"), "PostEffectColorFilterEllipse_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("B4715C85"), "PostEffectColorFilterEllipse_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectColorFilterEllipse_VS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("1ED08860"), "PostEffectColorFilterRectangle_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("2FD9EFC3"), "PostEffectColorFilterRectangle_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("8189160F"), "PostEffectColorFilterRectangle_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("0E732AD6"), "PostEffectColorFilterRectangle_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("129751AD"), "PostEffectColorFilterRectangle_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("3FB45354"), "PostEffectColorFilterRectangle_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("34788AA3"), "PostEffectColorFilterRectangle_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("64E13EF3"), "PostEffectColorFilterRectangle_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("21E7062A"), "PostEffectColorFilterRectangle_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectColorFilterRectangle_VS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("3D7DCE9C"), "PostEffectColorPalette_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectColorPalette_VS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("C0F1085B"), "PostEffectCrossFade_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectCrossFade_VS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("1005E68E"), "PostEffectDiffusion_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectDiffusion_VS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("FB128186"), "PostEffectDOF_PS::passMerge.PostEffectFunctorDOFMerge");
      forced_shader_names.emplace(Shader::Hash_StrToNum("EF3DCE04"), "PostEffectDOF_PS::passDownScale4x4.PostEffectFunctorMRT");
      forced_shader_names.emplace(Shader::Hash_StrToNum("3926FAF8"), "PostEffectDOF_PS::passBlur.PostEffectFunctorRenderHexDOF");
      forced_shader_names.emplace(Shader::Hash_StrToNum("A531D673"), "PostEffectDOF_PS::passFinal.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectDOF_VS::passMerge.PostEffectFunctorDOFMerge");
      forced_shader_names.emplace(Shader::Hash_StrToNum("87FDF29C"), "PostEffectDOF_VS::passDownScale4x4.PostEffectFunctorMRT");
      forced_shader_names.emplace(Shader::Hash_StrToNum("CF4C690F"), "PostEffectDOF_VS::passBlur.PostEffectFunctorRenderHexDOF");
      forced_shader_names.emplace(Shader::Hash_StrToNum("805E2AF1"), "PostEffectDotMatrix_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectDotMatrix_VS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("20A31303"), "PostEffectFade_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectFade_VS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("87D8D283"), "PostEffectFxaa_PS::passFinal.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("05C9DD5F"), "PostEffectFxaa_PS::passFinal.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("7B9819BE"), "PostEffectGamma_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectGamma_VS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("C830A790"), "PostEffectGlitchBlockNoise_PS::passGlitch.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectGlitchBlockNoise_VS::passGlitch.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("ACBDF7C1"), "PostEffectGlitchChromaticAberration_PS::passGlitch.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("014750B7"), "PostEffectGlitchChromaticAberration_PS::passGlitch.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectGlitchChromaticAberration_VS::passGlitch.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("36852C37"), "PostEffectGlow_PS::passThreshold.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("27B9D9EE"), "PostEffectGlow_PS::passDownSamplingBlurX0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("2A174B84"), "PostEffectGlow_PS::passDownSamplingBlurY0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("0FE8F790"), "PostEffectGlow_PS::passDownSamplingBlurX1.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("6BFB2CEB"), "PostEffectGlow_PS::passDownSamplingBlurY1.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("07F0BC90"), "PostEffectGlow_PS::passDownSamplingBlurX2.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("EB62526B"), "PostEffectGlow_PS::passDownSamplingBlurY2.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("712E8AF6"), "PostEffectGlow_PS::passDownSamplingBlurX3.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("E31ED7A7"), "PostEffectGlow_PS::passDownSamplingBlurY3.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("0377817F"), "PostEffectGlow_PS::passDownSamplingBlurX4.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("A1C098F0"), "PostEffectGlow_PS::passDownSamplingBlurY4.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("4CFC32B7"), "PostEffectGlow_PS::passDownSamplingBlurX3.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("CEC90C58"), "PostEffectGlow_PS::passDownSamplingBlurX2.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("0F6770C2"), "PostEffectGlow_PS::passDownSamplingBlurX1.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("3A21FBAC"), "PostEffectGlow_PS::passDownSamplingBlurX0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("391D0F58"), "PostEffectGlow_PS::passApplyGlow.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectGlow_VS::passThreshold.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("36852C37"), "PostEffectGlowLight_PS::passThreshold.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("80B5501D"), "PostEffectGlowLight_PS::passDownSamplingBlurX0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("13C11D25"), "PostEffectGlowLight_PS::passDownSamplingBlurY0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("63B1752C"), "PostEffectGlowLight_PS::passDownSamplingBlurX1.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("CC1A91B9"), "PostEffectGlowLight_PS::passDownSamplingBlurY1.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("7967C2E2"), "PostEffectGlowLight_PS::passDownSamplingBlurX2.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("D1BC8445"), "PostEffectGlowLight_PS::passDownSamplingBlurY2.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("919169EF"), "PostEffectGlowLight_PS::passDownSamplingBlurX3.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("71E73670"), "PostEffectGlowLight_PS::passDownSamplingBlurY3.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("95CA0E02"), "PostEffectGlowLight_PS::passDownSamplingBlurX4.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("6F1F2190"), "PostEffectGlowLight_PS::passDownSamplingBlurY4.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("01762784"), "PostEffectGlowLight_PS::passDownSamplingBlurX3.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("1D331BBC"), "PostEffectGlowLight_PS::passDownSamplingBlurX2.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("B2375F8B"), "PostEffectGlowLight_PS::passDownSamplingBlurX1.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("74CE25AD"), "PostEffectGlowLight_PS::passDownSamplingBlurX0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("4BFA6437"), "PostEffectGlowLight_PS::passApplyGlow.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectGlowLight_VS::passThreshold.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("50886E3C"), "PostEffectHatching_PS::passHatching.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectHatching_VS::passHatching.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("857D9C94"), "PostEffectHue_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectHue_VS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("BFA9A843"), "PostEffectMonotone_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("794A5AD0"), "PostEffectMonotone_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectMonotone_VS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("4B9A6A31"), "PostEffectMonotoneSRGBToLinear_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("55C547FA"), "PostEffectMonotoneSRGBToLinear_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectMonotoneSRGBToLinear_VS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("F69A131A"), "PostEffectMonotoneSRGBToLinearLight_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("8D81F3FF"), "PostEffectMonotoneSRGBToLinearLight_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectMonotoneSRGBToLinearLight_VS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("DD55B2A7"), "PostEffectMotionBlur_PS::p0.PostEffectFunctorColorMask");
      forced_shader_names.emplace(Shader::Hash_StrToNum("BB055A34"), "PostEffectMotionBlur_PS::p1.PostEffectFunctorColorMask");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectMotionBlur_VS::p0.PostEffectFunctorColorMask");
      forced_shader_names.emplace(Shader::Hash_StrToNum("27EA459B"), "PostEffectMotionBlurWithDT_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectMotionBlurWithDT_VS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("0FBE551C"), "PostEffectNegative_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("194DCDA6"), "PostEffectNegative_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectNegative_VS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("90A8D0F9"), "PostEffectScroll_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectScroll_VS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("E05124D1"), "PostEffectSearchMode_PS::pass0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectSearchMode_VS::pass0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("1C92202B"), "PostEffectSepia_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("1C2438E9"), "PostEffectSepia_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectSepia_VS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("893C5548"), "PostEffectSepiaOld_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectSepiaOld_VS::p0.PostEffectFunctorDefault");
      //forced_shader_names.emplace(Shader::Hash_StrToNum("5182EC11"), "PostEffectSimHDRAndDOF_PS::passNegativeClamp.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("CE62097E"), "PostEffectSimHDRAndDOF_PS::passCopyNegativeClamp.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("A13896DC"), "PostEffectSimHDRAndDOF_PS::passMerge.PostEffectFunctorDOFMergeForKids");
      forced_shader_names.emplace(Shader::Hash_StrToNum("C67CFC05"), "PostEffectSimHDRAndDOF_PS::passDownScale4x4.PostEffectFunctorMRT");
      forced_shader_names.emplace(Shader::Hash_StrToNum("3E7C3954"), "PostEffectSimHDRAndDOF_PS::passBlur.PostEffectFunctorMRT");
      forced_shader_names.emplace(Shader::Hash_StrToNum("170B9B40"), "PostEffectSimHDRAndDOF_PS::passBlurHex.PostEffectFunctorRenderHexDOFForKids");
      forced_shader_names.emplace(Shader::Hash_StrToNum("E3D0994B"), "PostEffectSimHDRAndDOF_PS::passMeasureLuminanceInit.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("FAA4ADF8"), "PostEffectSimHDRAndDOF_PS::passMeasureLuminanceIterate_0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("0CBFFC15"), "PostEffectSimHDRAndDOF_PS::passMeasureLuminanceIterate_1.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("4E37B98C"), "PostEffectSimHDRAndDOF_PS::passMeasureLuminanceFinal.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("C968565C"), "PostEffectSimHDRAndDOF_PS::passAdaptedInit.PostEffectFunctorOnce");
      forced_shader_names.emplace(Shader::Hash_StrToNum("4CE2701C"), "PostEffectSimHDRAndDOF_PS::passAdaptedLum.PostEffectFunctorSwitchTex");
      forced_shader_names.emplace(Shader::Hash_StrToNum("7734201C"), "PostEffectSimHDRAndDOF_PS::passBright.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("BDA46764"), "PostEffectSimHDRAndDOF_PS::passGaussBlur.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("241BA203"), "PostEffectSimHDRAndDOF_PS::passDownScale2x2.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("F34F7DCC"), "PostEffectSimHDRAndDOF_PS::passRenderBloomInit.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("ACB3BE64"), "PostEffectSimHDRAndDOF_PS::passRenderBloomIterate.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("F44AE41B"), "PostEffectSimHDRAndDOF_PS::passRenderBloomFinal.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("A1F79753"), "PostEffectSimHDRAndDOF_PS::passRenderStar.PostEffectFunctorRenderStarForKids");
      forced_shader_names.emplace(Shader::Hash_StrToNum("F5B64247"), "PostEffectSimHDRAndDOF_PS::passMakeFlare.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("B57A9EA3"), "PostEffectSimHDRAndDOF_PS::passRenderFlare.PostEffectFunctorRenderLensFlare");
      forced_shader_names.emplace(Shader::Hash_StrToNum("3CC880F7"), "PostEffectSimHDRAndDOF_PS::GenerateMaskedScenePass.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("14C5B4F2"), "PostEffectSimHDRAndDOF_PS::GaussBlurPass1.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("718F8A70"), "PostEffectSimHDRAndDOF_PS::RadialBlurPass1.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("67096FD1"), "PostEffectSimHDRAndDOF_PS::HorizontalBlurPass.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("99FAAAE2"), "PostEffectSimHDRAndDOF_PS::VerticalBlurPass.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("B45B1611"), "PostEffectSimHDRAndDOF_PS::RadialBlurPass2.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("8A160B04"), "PostEffectSimHDRAndDOF_PS::passDownScale4x4Distant.PostEffectFunctorRenderDistantDOFForKids");
      forced_shader_names.emplace(Shader::Hash_StrToNum("5EA1922E"), "PostEffectSimHDRAndDOF_PS::passBlurDistant.PostEffectFunctorRenderDistantDOFForKids");
      forced_shader_names.emplace(Shader::Hash_StrToNum("0651FD77"), "PostEffectSimHDRAndDOF_PS::passFinalT0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("0F539095"), "PostEffectSimHDRAndDOF_PS::passFinalT1.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("00B5B879"), "PostEffectSimHDRAndDOF_PS::passFinalT2.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("62EFF154"), "PostEffectSimHDRAndDOF_PS::passFinalT3.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("9A9B6BB6"), "PostEffectSimHDRAndDOF_PS::passFinalT4.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("48BE64DD"), "PostEffectSimHDRAndDOF_PS::passFinalT5.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("968BB1D8"), "PostEffectSimHDRAndDOF_PS::passFinalT6.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("2D8E0121"), "PostEffectSimHDRAndDOF_PS::passFinalT7.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("4A5F5CE2"), "PostEffectSimHDRAndDOF_PS::passFinalT8.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("CAD7869C"), "PostEffectSimHDRAndDOF_PS::passFinalT9.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("88E8C085"), "PostEffectSimHDRAndDOF_PS::passFinalT10.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("7DE15276"), "PostEffectSimHDRAndDOF_PS::passFinalT11.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("D698FE2A"), "PostEffectSimHDRAndDOF_PS::passFinalT12.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("CB4F0E0B"), "PostEffectSimHDRAndDOF_PS::passFinalT13.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("2805C72A"), "PostEffectSimHDRAndDOF_PS::passFinalT14.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("91A02148"), "PostEffectSimHDRAndDOF_PS::passFinalT15.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("CC650FC9"), "PostEffectSimHDRAndDOF_PS::passNegativeClamp.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("EA314404"), "PostEffectSimHDRAndDOF_PS::passFinal.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectSimHDRAndDOF_VS::passNegativeClamp.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("87FDF29C"), "PostEffectSimHDRAndDOF_VS::passDownScale4x4.PostEffectFunctorMRT");
      forced_shader_names.emplace(Shader::Hash_StrToNum("15DE80B7"), "PostEffectSimHDRAndDOF_VS::passBlurHex.PostEffectFunctorRenderHexDOFForKids");
      forced_shader_names.emplace(Shader::Hash_StrToNum("CF4C690F"), "PostEffectSimHDRAndDOF_VS::passBlur.PostEffectFunctorMRT");
      forced_shader_names.emplace(Shader::Hash_StrToNum("892E897D"), "PostEffectSimHDRAndDOF_VS::passFinalT0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("6DB0B638"), "PostEffectSimHDRAndDOF_VS::passRenderBloomIterate.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectSimHDRAndDOF_VS::passRenderBloomIterate.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("05C9DD5F"), "PostEffectSimHDRAndDOF_VS::passRenderBloomIterate.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("E04C71AE"), "PostEffectTexFilter_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectTexFilter_VS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("0C6E129C"), "PostEffectUnsharpMask_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("05C9DD5F"), "PostEffectUnsharpMask_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("8F148C0F"), "PostEffectWatercolorFrame_PS::passMain0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectWatercolorFrame_VS::passMain0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("BF0D8DF0"), "PostEffectWave_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectWave_VS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("5F60B7C7"), "PostEffectWaveOutside_PS::p0.PostEffectFunctorDefault");
      forced_shader_names.emplace(Shader::Hash_StrToNum("42696AF8"), "PostEffectWaveOutside_VS::p0.PostEffectFunctorDefault");
#endif

      game = new BlueReflectionSecondLight();
   }
   else if (ul_reason_for_call == DLL_PROCESS_DETACH)
   {
      g_compute_projection_matrix_hook.reset();
      g_camera_fieldmap_view_proj_matrix_hook.reset();
      g_battle_camera_hook.reset();
      g_active_camera_hook.reset();
      g_search_mode_hook.reset();
      g_battle_mode_hook.reset();
      g_postfx_hatching_hook.reset();
      reshade::unregister_event<reshade::addon_event::bind_render_targets_and_depth_stencil>(BlueReflectionSecondLight::OnBindRenderTargetsAndDepthStencil);
      reshade::unregister_event<reshade::addon_event::clear_render_target_view>(BlueReflectionSecondLight::OnClearRenderTargetView);
      reshade::unregister_event<reshade::addon_event::copy_texture_region>(BlueReflectionSecondLight::OnCopyTextureRegion);
      reshade::unregister_event<reshade::addon_event::unmap_buffer_region>(BlueReflectionSecondLight::OnUnmapBufferRegion);
      reshade::unregister_event<reshade::addon_event::map_buffer_region>(BlueReflectionSecondLight::OnMapBufferRegion);
      reshade::unregister_event<reshade::addon_event::create_pipeline>(BlueReflectionSecondLight::OnCreatePipeline);
      reshade::unregister_event<reshade::addon_event::execute_secondary_command_list>(BlueReflectionSecondLight::OnExecuteSecondaryCommandList);
      reshade::unregister_event<reshade::addon_event::destroy_resource >(BlueReflectionSecondLight::OnDestroyResource);
   }

   CoreMain(hModule, ul_reason_for_call, lpReserved);

   return TRUE;
}