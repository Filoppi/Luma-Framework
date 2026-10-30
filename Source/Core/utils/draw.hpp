#pragma once

#include "../texture_data/SMAA_AreaTex.h"
#include "../texture_data/SMAA_SearchTex.h"

#include "../includes/callbacks.h"
#include "../includes/math.h"

using Math::operator"" _h;

enum class DrawStateStackType
{
   // Same as "FullGraphics" but skips some states that are usually not changed by our code.
   // Note that in DX10-11 when binding game resources as RT or SR etc, they might automatically get unbound
   // from previous incompatible bindings they had, and these slots might not always be restored by this mode.
   SimpleGraphics,
   // Not 100% of the graphics state, but almost everything we'll ever need.
   // Note that if we set a render target that was also set as shader resource of the (e.g.) vertex stage, it won't be restored.
   FullGraphics,
   // Not 100% of the compute state, but almost everything we'll ever need.
   Compute,
};
// Caches all the states we might need to modify to draw a simple pixel shader.
// First call "Cache()" (once) and then call "Restore()" (once).
template<DrawStateStackType Mode = DrawStateStackType::FullGraphics>
struct DrawStateStack
{
   // This is the max according to "PSSetShader()" documentation
   static constexpr UINT max_shader_class_instances = 256;

   // Cache aside the previous resources/states:
   void Cache(ID3D11DeviceContext* device_context, UINT device_max_uav_num)
   {
      state = std::make_unique<State>();

      com_ptr<ID3D11DeviceContext1> device_context_1;
      HRESULT hr = device_context->QueryInterface(&device_context_1);
#if 0 // This happens in some games
      if (SUCCEEDED(hr) && device_context_1)
      {
         ASSERT_ONCE(false); // If this was the case, we'd need to handle the extra parameters of functions like "PSGetConstantBuffers1"
      }
#endif

      state->uav_num = device_max_uav_num;
      if constexpr (Mode == DrawStateStackType::SimpleGraphics || Mode == DrawStateStackType::FullGraphics)
      {
         device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &state->render_target_views[0], &state->depth_stencil_view); // TODO: optimize away for the "OMGetRenderTargetsAndUnorderedAccessViews" call case? We could just get all the RTVs and UAVs from slot 0 and then find the first valid UAV
         if constexpr (Mode == DrawStateStackType::FullGraphics)
         {
            for (size_t i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
            {
               bool rtv_empty = state->render_target_views[i].get() == nullptr;
               if (!rtv_empty)
               {
                  state->render_target_views[i].reset(); // Re-set it as we will re-assign it
                  state->valid_render_target_views_bound = i + 1; // The documentation is confusing, but it seems like the UAV start slot you request needs to be >= the number of valid+null bound RTVs (up to the last valid one). Alternatively we could check for the first valid UAV?
               }
            }
            state->depth_stencil_view.reset();
            device_context->OMGetRenderTargetsAndUnorderedAccessViews(state->valid_render_target_views_bound, &state->render_target_views[0], &state->depth_stencil_view, state->valid_render_target_views_bound, state->uav_num - state->valid_render_target_views_bound, &state->unordered_access_views[0]);
#if ENABLE_DRAW_STATE_VERTEX_BUFFER_CACHE
            device_context->IAGetVertexBuffers(0, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT, &state->vertex_buffers[0], state->vertex_buffer_strides, state->vertex_buffer_offsets);
#endif // ENABLE_DRAW_STATE_VERTEX_BUFFER_CACHE
         }
         device_context->OMGetBlendState(&state->blend_state, state->blend_factor, &state->blend_sample_mask);
         device_context->IAGetPrimitiveTopology(&state->primitive_topology);
         device_context->RSGetScissorRects(&state->scissor_rects_num, nullptr); // This will get the number of scissor rects used
         device_context->RSGetScissorRects(&state->scissor_rects_num, &state->scissor_rects[0]);
         device_context->RSGetViewports(&state->viewports_num, nullptr); // This will get the number of viewports used
         device_context->RSGetViewports(&state->viewports_num, &state->viewports[0]);
         device_context->PSGetShaderResources(0, srv_num, &state->shader_resource_views[0]);
         if (device_context_1)
         {
            device_context_1->PSGetConstantBuffers1(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, &state->constant_buffers[0], state->constant_buffers_first_constant, state->constant_buffers_num_constant);
            device_context_1->VSGetConstantBuffers1(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, &state->vs_constant_buffers[0], state->vs_constant_buffers_first_constant, state->vs_constant_buffers_num_constant);
         }
         else
         {
            device_context->PSGetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, &state->constant_buffers[0]);
            device_context->VSGetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, &state->vs_constant_buffers[0]);
         }
         device_context->OMGetDepthStencilState(&state->depth_stencil_state, &state->stencil_ref);
#if ENABLE_SHADER_CLASS_INSTANCES
         device_context->VSGetShader(&state->vs, &state->vs_instances[0], &state->vs_instances_count);
         device_context->PSGetShader(&state->ps, &state->ps_instances[0], &state->ps_instances_count);
         ASSERT_ONCE(state->vs_instances_count == 0 && state->ps_instances_count == 0); // Make sure they are never used
#else
         device_context->VSGetShader(&state->vs, nullptr, 0);
         device_context->PSGetShader(&state->ps, nullptr, 0);
#endif
         device_context->PSGetSamplers(0, samplers_num, &state->samplers_state[0]);
         device_context->IAGetInputLayout(&state->input_layout);
         device_context->RSGetState(&state->rasterizer_state);

#if 0 // These are not needed until proven otherwise, we don't change, nor rely on these states
         ID3D11Buffer* VSConstantBuffer;
         ID3D11Buffer* VertexBuffer;
         ID3D11Buffer* IndexBuffer;
         UINT IndexBufferOffset, VertexBufferStride, VertexBufferOffset;
         DXGI_FORMAT IndexBufferFormat;
         device_context->VSGetConstantBuffers(0, 1, &VSConstantBuffer);
         device_context->IAGetIndexBuffer(&IndexBuffer, &IndexBufferFormat, &IndexBufferOffset);
         device_context->IAGetVertexBuffers(0, 1, &VertexBuffer, &VertexBufferStride, &VertexBufferOffset);
         device_context->GSGetShader(&state->gs, nullptr, 0); // And others
#endif
      }
      else if constexpr (Mode == DrawStateStackType::Compute)
      {
         device_context->CSGetShaderResources(0, srv_num, &state->shader_resource_views[0]);
         if (device_context_1)
         {
            device_context_1->CSGetConstantBuffers1(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, &state->constant_buffers[0], state->constant_buffers_first_constant, state->constant_buffers_num_constant);
         }
         else
         {
            device_context->CSGetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, &state->constant_buffers[0]);
         }
         device_context->CSGetUnorderedAccessViews(0, state->uav_num, &state->unordered_access_views[0]);
#if ENABLE_SHADER_CLASS_INSTANCES
         device_context->CSGetShader(&state->cs, &state->cs_instances[0], &state->cs_instances_count);
         ASSERT_ONCE(state->cs_instances_count == 0); // Make sure they are never used
#else
         device_context->CSGetShader(&state->cs, nullptr, 0);
#endif
         device_context->CSGetSamplers(0, samplers_num, &state->samplers_state[0]);
      }
   }

   // Restore the previous resources/states:
   void Restore(ID3D11DeviceContext* device_context, bool output_textures = true, bool shaders = true)
   {
      if (!state) return;

      com_ptr<ID3D11DeviceContext1> device_context_1;
      HRESULT hr = device_context->QueryInterface(&device_context_1);

      if constexpr (Mode == DrawStateStackType::SimpleGraphics || Mode == DrawStateStackType::FullGraphics)
      {
         if (output_textures)
         {
            // Set the render targets first because they are "output" and take precedence over SR bindings of the same resource, which would otherwise get nulled
            ID3D11RenderTargetView* const* rtvs_const = (ID3D11RenderTargetView**)std::addressof(state->render_target_views[0]);
            if constexpr (Mode == DrawStateStackType::FullGraphics)
            {
               ID3D11UnorderedAccessView* const* uavs_const = (ID3D11UnorderedAccessView**)std::addressof(state->unordered_access_views[0]);
               UINT uav_initial_counts[D3D11_1_UAV_SLOT_COUNT]; // TODO: Likely not necessary, we could pass in nullptr
               std::ranges::fill(uav_initial_counts, -1u);
               device_context->OMSetRenderTargetsAndUnorderedAccessViews(state->valid_render_target_views_bound, rtvs_const, state->depth_stencil_view.get(), state->valid_render_target_views_bound, state->uav_num - state->valid_render_target_views_bound, uavs_const, &uav_initial_counts[0]);
            }
            else
            {
               device_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs_const, state->depth_stencil_view.get());
            }
         }
         device_context->OMSetBlendState(state->blend_state.get(), state->blend_factor, state->blend_sample_mask);
         device_context->IASetPrimitiveTopology(state->primitive_topology);
#if ENABLE_DRAW_STATE_VERTEX_BUFFER_CACHE
         if constexpr (Mode == DrawStateStackType::FullGraphics)
         {
            ID3D11Buffer* const* vertex_buffers_const = (ID3D11Buffer**)std::addressof(state->vertex_buffers[0]);
            device_context->IASetVertexBuffers(0, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT, vertex_buffers_const, state->vertex_buffer_strides, state->vertex_buffer_offsets);
         }
#endif // ENABLE_DRAW_STATE_VERTEX_BUFFER_CACHE
         device_context->RSSetScissorRects(state->scissor_rects_num, &state->scissor_rects[0]);
         device_context->RSSetViewports(state->viewports_num, &state->viewports[0]);
         ID3D11ShaderResourceView* const* srvs_const = (ID3D11ShaderResourceView**)std::addressof(state->shader_resource_views[0]); // We can't use "com_ptr"'s "T **operator&()" as it asserts if the object isn't null, even if the reference would be const
         device_context->PSSetShaderResources(0, srv_num, srvs_const);
         ID3D11Buffer* const* constant_buffers_const = (ID3D11Buffer**)std::addressof(state->constant_buffers[0]);
         ID3D11Buffer* const* vs_constant_buffers_const = (ID3D11Buffer**)std::addressof(state->vs_constant_buffers[0]);
         if (device_context_1)
         {
            device_context_1->PSSetConstantBuffers1(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, constant_buffers_const, state->constant_buffers_first_constant, state->constant_buffers_num_constant);
            device_context_1->VSSetConstantBuffers1(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, vs_constant_buffers_const, state->vs_constant_buffers_first_constant, state->vs_constant_buffers_num_constant);
         }
         else
         {
            device_context->PSSetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, constant_buffers_const);
            device_context->VSSetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, vs_constant_buffers_const);
         }
         device_context->OMSetDepthStencilState(state->depth_stencil_state.get(), state->stencil_ref);
         if (shaders)
         {
#if ENABLE_SHADER_CLASS_INSTANCES
            ID3D11ClassInstance* const* vs_instances_const = (ID3D11ClassInstance**)std::addressof(state->vs_instances[0]);
            ID3D11ClassInstance* const* ps_instances_const = (ID3D11ClassInstance**)std::addressof(state->ps_instances[0]);
            device_context->VSSetShader(state->vs.get(), vs_instances_const, state->vs_instances_count);
            device_context->PSSetShader(state->ps.get(), ps_instances_const, state->ps_instances_count);
#else
            device_context->VSSetShader(state->vs.get(), nullptr, 0);
            device_context->PSSetShader(state->ps.get(), nullptr, 0);
#endif
         }
         ID3D11SamplerState* const* ps_samplers_state_const = (ID3D11SamplerState**)std::addressof(state->samplers_state[0]);
         device_context->PSSetSamplers(0, samplers_num, ps_samplers_state_const);
         device_context->IASetInputLayout(state->input_layout.get());
         device_context->RSSetState(state->rasterizer_state.get());
      }
      else if constexpr (Mode == DrawStateStackType::Compute)
      {
         ID3D11ShaderResourceView* const* srvs_const = (ID3D11ShaderResourceView**)std::addressof(state->shader_resource_views[0]);
         device_context->CSSetShaderResources(0, srv_num, srvs_const);
         ID3D11Buffer* const* constant_buffers_const = (ID3D11Buffer**)std::addressof(state->constant_buffers[0]);
         if (device_context_1)
         {
            device_context_1->CSSetConstantBuffers1(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, constant_buffers_const, state->constant_buffers_first_constant, state->constant_buffers_num_constant);
         }
         else
         {
            device_context->CSSetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, constant_buffers_const);
         }
         if (output_textures)
         {
            ID3D11UnorderedAccessView* const* uavs_const = (ID3D11UnorderedAccessView**)std::addressof(state->unordered_access_views[0]);
            UINT uav_initial_counts[D3D11_1_UAV_SLOT_COUNT]; // Likely not necessary, we could pass in nullptr
            std::ranges::fill(uav_initial_counts, -1u);
            device_context->CSSetUnorderedAccessViews(0, state->uav_num, uavs_const, uav_initial_counts);
         }
         if (shaders)
         {
#if ENABLE_SHADER_CLASS_INSTANCES
            ID3D11ClassInstance* const* cs_instances_const = (ID3D11ClassInstance**)std::addressof(state->cs_instances[0]);
            device_context->CSSetShader(state->cs.get(), cs_instances_const, state->cs_instances_count);
#else
            device_context->CSSetShader(state->cs.get(), nullptr, 0);
#endif
         }
         ID3D11SamplerState* const* cs_samplers_state_const = (ID3D11SamplerState**)std::addressof(state->samplers_state[0]);
         device_context->CSSetSamplers(0, samplers_num, cs_samplers_state_const);
      }
   }

   // Duplicates all resources (and views) of the state
   void Clone(ID3D11DeviceContext* device_context, const std::vector<ID3D11Buffer*>& luma_cbuffers)
   {
      if (!state) return;

      state->Clone(device_context, luma_cbuffers);
   }

   bool IsValid() const { return state.get() != nullptr; }

   static constexpr size_t samplers_num = []
      {
         if constexpr (Mode == DrawStateStackType::FullGraphics || Mode == DrawStateStackType::Compute)
            return D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT;
         else
            return size_t{ 1 };
      }();
   static constexpr size_t srv_num = []
      {
         if constexpr (Mode == DrawStateStackType::FullGraphics || Mode == DrawStateStackType::Compute)
            return D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;
         else
            return size_t{ 3 }; // We usually don't use them beyond than the first 3
      }();

   // Note: this contains some information that is exclusive to either compute or graphics, but overall it's mostly shared
   struct State
   {
      State()
      {
#if 0 // Not needed
      std::fill(std::begin(constant_buffers_num_constant), std::end(constant_buffers_num_constant), 4096); // Default from docs
      std::fill(std::begin(vs_constant_buffers_num_constant), std::end(vs_constant_buffers_num_constant), 4096); // Default from docs
#endif
      }

      void Clone(ID3D11DeviceContext* device_context, const std::vector<ID3D11Buffer*>& luma_cbuffers)
      {
         com_ptr<ID3D11Device> device;
         device_context->GetDevice(&device);

         depth_stencil_view = CloneResourceAndView(device.get(), device_context, depth_stencil_view.get());
         for (UINT i = 0; i < srv_num; ++i)
         {
            shader_resource_views[i] = CloneResourceAndView(device.get(), device_context, shader_resource_views[i].get());
         }
         for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
         {
            render_target_views[i] = CloneResourceAndView(device.get(), device_context, render_target_views[i].get());
         }
         for (UINT i = 0; i < D3D11_1_UAV_SLOT_COUNT; ++i)
         {
            unordered_access_views[i] = CloneResourceAndView(device.get(), device_context, unordered_access_views[i].get());
         }
         for (UINT i = 0; i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; ++i)
         {
            // Don't clone luma cbuffers, it'd be useless and detrimental as we want them to reflect the latest settings (usually)
            if (std::find(luma_cbuffers.begin(), luma_cbuffers.end(), constant_buffers[i].get()) == luma_cbuffers.end())
               constant_buffers[i] = CloneResourceTyped(device.get(), device_context, constant_buffers[i].get());
            if (std::find(luma_cbuffers.begin(), luma_cbuffers.end(), vs_constant_buffers[i].get()) == luma_cbuffers.end())
               vs_constant_buffers[i] = CloneResourceTyped(device.get(), device_context, vs_constant_buffers[i].get());
         }

         // Note: for now we mostly ignore the vertex shader stuff (like vertex buffers etc), given it'd be complicated to clone and this feature is mostly used to debug post processing
         // Similarly, samplers etc aren't cached as it's barely needed, only stuff that is "live" data is cloned.
      }

      com_ptr<ID3D11BlendState> blend_state;
      FLOAT blend_factor[4] = {1.f, 1.f, 1.f, 1.f};
      UINT blend_sample_mask;
      com_ptr<ID3D11VertexShader> vs;
      com_ptr<ID3D11PixelShader> ps;
      com_ptr<ID3D11ComputeShader> cs;
#if ENABLE_SHADER_CLASS_INSTANCES
      UINT vs_instances_count = max_shader_class_instances;
      UINT ps_instances_count = max_shader_class_instances;
      UINT cs_instances_count = max_shader_class_instances;
      com_ptr<ID3D11ClassInstance> vs_instances[max_shader_class_instances];
      com_ptr<ID3D11ClassInstance> ps_instances[max_shader_class_instances];
      com_ptr<ID3D11ClassInstance> cs_instances[max_shader_class_instances];
#endif
      D3D11_PRIMITIVE_TOPOLOGY primitive_topology;

      // TODO: move some of these to the heap, stack is too big
      com_ptr<ID3D11DepthStencilState> depth_stencil_state;
      UINT stencil_ref;
      com_ptr<ID3D11DepthStencilView> depth_stencil_view;
      com_ptr<ID3D11SamplerState> samplers_state[samplers_num];
      com_ptr<ID3D11ShaderResourceView> shader_resource_views[srv_num];
      com_ptr<ID3D11RenderTargetView> render_target_views[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
      com_ptr<ID3D11UnorderedAccessView> unordered_access_views[D3D11_1_UAV_SLOT_COUNT];
      com_ptr<ID3D11Buffer> constant_buffers[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
      UINT constant_buffers_first_constant[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
      UINT constant_buffers_num_constant[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
      com_ptr<ID3D11Buffer> vs_constant_buffers[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
      UINT vs_constant_buffers_first_constant[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
      UINT vs_constant_buffers_num_constant[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
      D3D11_RECT scissor_rects[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
      UINT scissor_rects_num = 0;
      D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
      UINT viewports_num = 1;
      com_ptr<ID3D11InputLayout> input_layout;
#if ENABLE_DRAW_STATE_VERTEX_BUFFER_CACHE
      com_ptr<ID3D11Buffer> vertex_buffers[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
      UINT vertex_buffer_strides[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
      UINT vertex_buffer_offsets[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
#endif // ENABLE_DRAW_STATE_VERTEX_BUFFER_CACHE
      com_ptr<ID3D11RasterizerState> rasterizer_state;
      UINT valid_render_target_views_bound = 0; // Includes null ones as well if bound between valid RTVs
      UINT uav_num = D3D11_1_UAV_SLOT_COUNT;
   };

   // Store this on the heap instead of inlining it in the stack otherwise it'd allocate way too much
   std::unique_ptr<State> state;
};

// Fullscreen (full render target) pass
void DrawCustomPixelShader(ID3D11DeviceContext* device_context, ID3D11DepthStencilState* depth_stencil_state, ID3D11BlendState* blend_state, ID3D11SamplerState* sampler_state, ID3D11VertexShader* vs, ID3D11PixelShader* ps, ID3D11ShaderResourceView* source_resource_texture_view, ID3D11RenderTargetView* target_resource_texture_view, UINT width, UINT height, bool alpha = true, const char* trace_name = nullptr)
{
   // Set the new resources/states:
   constexpr FLOAT blend_factor_alpha[4] = { 1.f, 1.f, 1.f, 1.f };
   constexpr FLOAT blend_factor[4] = { 1.f, 1.f, 1.f, 0.f }; // TODO: this makes no sense as the blend state is unlikely to use it, use write mask instead
   device_context->OMSetBlendState(blend_state, alpha ? blend_factor_alpha : blend_factor, 0xFFFFFFFF);
   // Note: we don't seem to need to call (and cache+restore) IASetVertexBuffers() (at least not in Prey).
   // That's either because games always have vertices buffers set in there already, or because DX is tolerant enough (we are not seeing any etc errors in the DX log).
   device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
   device_context->RSSetScissorRects(0, nullptr); // Scissors are not needed
   D3D11_VIEWPORT viewport;
   viewport.TopLeftX = 0;
   viewport.TopLeftY = 0;
   viewport.Width = width;
   viewport.Height = height;
   viewport.MinDepth = 0;
   viewport.MaxDepth = 1;
   device_context->RSSetViewports(1, &viewport); // Viewport is always needed
   device_context->PSSetShaderResources(0, 1, &source_resource_texture_view);
   device_context->OMSetDepthStencilState(depth_stencil_state, 0);
   if (sampler_state) // Optional
   {
      device_context->PSSetSamplers(0, 1, &sampler_state);
   }
   device_context->OMSetRenderTargets(1, &target_resource_texture_view, nullptr);
   device_context->VSSetShader(vs, nullptr, 0);
   device_context->PSSetShader(ps, nullptr, 0);
   device_context->IASetInputLayout(nullptr);
   device_context->RSSetState(nullptr);

#if DEVELOPMENT
   com_ptr<ID3D11GeometryShader> gs;
   device_context->GSGetShader(&gs, nullptr, 0);
   ASSERT_ONCE(!gs.get());
   com_ptr<ID3D11HullShader> hs;
   device_context->HSGetShader(&hs, nullptr, 0);
   ASSERT_ONCE(!hs.get());
#endif

   // Finally draw:
   device_context->Draw(4, 0);
   Trace::Draw(device_context, trace_name, 4);
}

// Sets the viewport to the full render target, useful to anticipate upscaling (before the game would have done it natively)
void SetViewportFullscreen(ID3D11DeviceContext* device_context, uint2 size = {})
{
   if (size == uint2{})
   {
      com_ptr<ID3D11RenderTargetView> render_target_view;
      device_context->OMGetRenderTargets(1, &render_target_view, nullptr);

#if DEVELOPMENT
      D3D11_RENDER_TARGET_VIEW_DESC render_target_view_desc;
      render_target_view->GetDesc(&render_target_view_desc);
      ASSERT_ONCE(render_target_view_desc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D); // This should always be the case
#endif // DEVELOPMENT

      D3D11_TEXTURE2D_DESC render_target_texture_2d_desc;
      com_ptr<ID3D11Resource> render_target_resource;
      render_target_view->GetResource(&render_target_resource);
      if (render_target_resource)
      {
         com_ptr<ID3D11Texture2D> render_target_texture_2d;
         HRESULT hr = render_target_resource->QueryInterface(&render_target_texture_2d);
         ASSERT_ONCE(SUCCEEDED(hr));
         if (render_target_texture_2d)
         {
            render_target_texture_2d->GetDesc(&render_target_texture_2d_desc);
         }
         else
         {
            return;
         }
      }
      else
      {
         return;
      }

      size.x = render_target_texture_2d_desc.Width;
      size.y = render_target_texture_2d_desc.Height;
   }

   // A render-resolution scissor clips a fullscreen viewport to the top-left subregion.
   // Keep scissor and viewport coverage in sync whenever this helper expands a target for SR.
   com_ptr<ID3D11RasterizerState> state;
   device_context->RSGetState(&state);
   if (state.get())
   {
      D3D11_RASTERIZER_DESC state_desc;
      state->GetDesc(&state_desc);
      if (state_desc.ScissorEnable)
      {
         UINT scissor_rects_num = 0;
         device_context->RSGetScissorRects(&scissor_rects_num, nullptr);
         scissor_rects_num = min(scissor_rects_num, UINT(D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE));
         if (scissor_rects_num > 0)
         {
            D3D11_RECT scissor_rects[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
            device_context->RSGetScissorRects(&scissor_rects_num, scissor_rects);
            for (UINT i = 0; i < scissor_rects_num; ++i)
            {
               scissor_rects[i] = { 0, 0, static_cast<LONG>(size.x), static_cast<LONG>(size.y) };
            }
            device_context->RSSetScissorRects(scissor_rects_num, scissor_rects);
         }
      }
   }

   D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
   UINT viewports_num = 1;
   device_context->RSGetViewports(&viewports_num, nullptr);
   ASSERT_ONCE(viewports_num == 1); // Possibly innocuous as long as it's > 0, but we should only ever have one viewport and one RT!
   device_context->RSGetViewports(&viewports_num, &viewports[0]);
   for (uint32_t i = 0; i < viewports_num; i++)
   {
      viewports[i].Width = size.x;
      viewports[i].Height = size.y;
   }
   device_context->RSSetViewports(viewports_num, &viewports[0]);
}

template<typename T>
concept IsD3D11BlendDesc = std::is_same_v<T, D3D11_BLEND_DESC> || std::is_same_v<T, D3D11_BLEND_DESC1>;
template<typename T>
concept IsD3D11RenderTargetBlendDesc = std::is_same_v<T, D3D11_RENDER_TARGET_BLEND_DESC> || std::is_same_v<T, D3D11_RENDER_TARGET_BLEND_DESC1>;

// Helper to determine if the logic op is effectively "Disabled" (Source overwrites Destination)
bool IsRTLogicOpDisabled(const D3D11_RENDER_TARGET_BLEND_DESC1& rt_blend_desc)
{
    if (!rt_blend_desc.LogicOpEnable) return true;
    
    // NOOP: Dest = Dest (No change)
    // COPY: Dest = Src (Overwrite, same as blending disabled)
    return rt_blend_desc.LogicOp == D3D11_LOGIC_OP_NOOP || rt_blend_desc.LogicOp == D3D11_LOGIC_OP_COPY;
}

template <IsD3D11RenderTargetBlendDesc T>
bool IsRTAlphaBlendDisabled(const T& rt_blend_desc)
{
   if (!rt_blend_desc.BlendEnable) return true;

   // It's enabled but it's as if it was disabled
   if (rt_blend_desc.BlendOpAlpha == D3D11_BLEND_OP::D3D11_BLEND_OP_ADD)
   {
      if (rt_blend_desc.SrcBlendAlpha == D3D11_BLEND::D3D11_BLEND_ONE && rt_blend_desc.DestBlendAlpha == D3D11_BLEND::D3D11_BLEND_ZERO)
         return true;
   }
   else if (rt_blend_desc.BlendOpAlpha == D3D11_BLEND_OP::D3D11_BLEND_OP_REV_SUBTRACT)
   {
      if (rt_blend_desc.SrcBlendAlpha == D3D11_BLEND::D3D11_BLEND_ZERO && rt_blend_desc.DestBlendAlpha == D3D11_BLEND::D3D11_BLEND_ONE)
         return true;
   }

   if ((rt_blend_desc.RenderTargetWriteMask & D3D11_COLOR_WRITE_ENABLE_ALPHA) == 0) return true;

   return false;
}

template <IsD3D11RenderTargetBlendDesc T>
bool IsRTRGBBlendDisabled(const T& rt_blend_desc)
{
   if (!rt_blend_desc.BlendEnable) return true;

   // It's enabled but it's as if it was disabled
   if (rt_blend_desc.BlendOp == D3D11_BLEND_OP::D3D11_BLEND_OP_ADD)
   {
      if (rt_blend_desc.SrcBlend == D3D11_BLEND::D3D11_BLEND_ONE && rt_blend_desc.DestBlend == D3D11_BLEND::D3D11_BLEND_ZERO)
         return true;
   }
   else if (rt_blend_desc.BlendOp == D3D11_BLEND_OP::D3D11_BLEND_OP_REV_SUBTRACT)
   {
      if (rt_blend_desc.SrcBlend == D3D11_BLEND::D3D11_BLEND_ZERO && rt_blend_desc.DestBlend == D3D11_BLEND::D3D11_BLEND_ONE)
         return true;
   }

   if ((rt_blend_desc.RenderTargetWriteMask & (D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE)) == 0) return true;

   return false;
}

// Check if blending is disabled or equivalent
template <IsD3D11RenderTargetBlendDesc T>
bool IsRTBlendDisabled(const T& rt_blend_desc)
{
   // Logic Op only works on UINT/SINT textures so it's rare for our usage
   if constexpr (std::is_same_v<T, D3D11_RENDER_TARGET_BLEND_DESC1>)
   {
      if (rt_blend_desc.LogicOpEnable)
         return IsRTLogicOpDisabled(rt_blend_desc);
   }
   return IsRTRGBBlendDisabled(rt_blend_desc) && IsRTAlphaBlendDisabled(rt_blend_desc);
}

// Helper to know if a blend inverts any source or dest color/alpha, or subtracts one from another,
// all operations that work fine in UNORM (as they are limited to 0-1, even within the blend math) render targets but break with SIGNED FLOAT.
// Alpha checks are separated as often it's manually kept to 0-1 so poses no risk.
template <IsD3D11BlendDesc T>
bool IsBlendInverted(const T& blend_desc, UINT render_targets = 1, bool check_alpha = false, UINT first_render_target = 0)
{
   auto IsBlendInverted_Internal = [](D3D11_BLEND blend, bool check_alpha)
   {
      switch (blend)
      {
      // We ignore "D3D11_BLEND_INV_BLEND_FACTOR" as usually it'd be set between 0-1 already, posing no risk.
      case D3D11_BLEND_INV_SRC_COLOR:
      case D3D11_BLEND_INV_DEST_COLOR:
      case D3D11_BLEND_INV_SRC1_COLOR:
         return true;
      case D3D11_BLEND_INV_SRC_ALPHA:
      case D3D11_BLEND_INV_DEST_ALPHA:
      case D3D11_BLEND_INV_SRC1_ALPHA:
      case D3D11_BLEND_SRC_ALPHA_SAT: // This is "min(src alpha, 1 - dest alpha)", so it inverts too
         return check_alpha;
      }
      return false;
   };

   for (UINT i = first_render_target; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT && i < (first_render_target + render_targets); i++)
   {
      // Without independent blending, render target 0 describes all of them (so "first_render_target" doesn't apply)
      const auto& render_target_blend_desc = blend_desc.RenderTarget[blend_desc.IndependentBlendEnable ? i : 0];

      if (render_target_blend_desc.BlendEnable)
      {
         if (render_target_blend_desc.BlendOp == D3D11_BLEND_OP_SUBTRACT || render_target_blend_desc.BlendOp == D3D11_BLEND_OP_REV_SUBTRACT)
         {
            return true;
         }
         if (IsBlendInverted_Internal(render_target_blend_desc.SrcBlend, check_alpha) || IsBlendInverted_Internal(render_target_blend_desc.DestBlend, check_alpha))
         {
            return true;
         }
         if (check_alpha)
         {
            if (render_target_blend_desc.BlendOpAlpha == D3D11_BLEND_OP_SUBTRACT || render_target_blend_desc.BlendOpAlpha == D3D11_BLEND_OP_REV_SUBTRACT)
            {
               return true;
            }
            if (IsBlendInverted_Internal(render_target_blend_desc.SrcBlendAlpha, check_alpha) || IsBlendInverted_Internal(render_target_blend_desc.DestBlendAlpha, check_alpha))
            {
               return true;
            }
         }
      }

      if (!blend_desc.IndependentBlendEnable)
      {
         break;
      }
   }
   return false;
}

struct CustomPixelShaderPassData
{
   // Only one of the two will be valid
   com_ptr<ID3D11RenderTargetView> original_or_custom_rtv;
   com_ptr<ID3D11View> original_rv;

   // Temp texture copy
   com_ptr<ID3D11Texture2D> texture_2d;
   com_ptr<ID3D11ShaderResourceView> srv;
   UINT width = 1;
   UINT height = 1;
};

void DrawCustomPixelShaderPass(ID3D11Device* device, ID3D11DeviceContext* device_context, ID3D11View* resource_view, const DeviceData& device_data, uint32_t pixel_shader_hash, CustomPixelShaderPassData& data, const char* trace_name = nullptr)
{
   if (data.original_rv.get() != resource_view)
   {
      if (data.original_rv)
      {
         data = CustomPixelShaderPassData();
      }

      if (resource_view)
      {
         com_ptr<ID3D11Resource> resource;
         resource_view->GetResource(&resource);
         if (resource)
         {
            com_ptr<ID3D11Texture2D> texture_2d;
            resource->QueryInterface(&texture_2d);
            if (texture_2d)
            {
               D3D11_TEXTURE2D_DESC texture_2d_desc;
               texture_2d->GetDesc(&texture_2d_desc);
               // We use a new/temp texture as SRV and keep the original as RTV, that's usually simpler
               data.texture_2d = CloneTexture<ID3D11Texture2D>(device, resource.get(), DXGI_FORMAT_UNKNOWN, D3D11_BIND_SHADER_RESOURCE, D3D11_BIND_RENDER_TARGET, false, false, device_context);
               if (data.texture_2d)
               {
                  HRESULT hr;

                  data.original_rv = resource_view;

                  com_ptr<ID3D11RenderTargetView> rtv;
                  hr = resource_view->QueryInterface(&rtv);
                  if (rtv)
                  {
                     data.original_or_custom_rtv = rtv;
                  }
                  else
                  {
                     hr = device->CreateRenderTargetView(texture_2d.get(), nullptr, &data.original_or_custom_rtv);
                     ASSERT_ONCE(SUCCEEDED(hr));
                  }

                  data.width = texture_2d_desc.Width;
                  data.height = texture_2d_desc.Height;

                  hr = device->CreateShaderResourceView(data.texture_2d.get(), nullptr, &data.srv);
                  ASSERT_ONCE(SUCCEEDED(hr));
               }
            }
         }
      }
   }

   // This is only valid if everything succeeded
   if (data.original_or_custom_rtv)
   {
      const auto vs = device_data.native_vertex_shaders.find(Math::CompileTimeStringHash("Copy VS"));
      const auto ps = device_data.native_pixel_shaders.find(pixel_shader_hash);
      if (vs == device_data.native_vertex_shaders.end() || !vs->second.get()
         || ps == device_data.native_pixel_shaders.end() || !ps->second.get()) return;

      com_ptr<ID3D11Resource> resource;
      data.original_or_custom_rtv->GetResource(&resource); // If we got here, it's valid

      device_context->CopySubresourceRegion(data.texture_2d.get(), 0, 0, 0, 0, resource.get(), 0, nullptr);

      DrawCustomPixelShader(device_context, nullptr, nullptr, device_data.sampler_state_point.get(), vs->second.get(), ps->second.get(), data.srv.get(), data.original_or_custom_rtv.get(), data.width, data.height, true, trace_name);
   }
}

struct SanitizeNaNsData
{
   com_ptr<ID3D11RenderTargetView> original_rtv;

   static constexpr SIZE_T max_levels = 15; // Matches "GetTextureMaxMipLevels(D3D11_REQ_TEXTURE1D_U_DIMENSION)"

   // Temp mipped texture copy
   com_ptr<ID3D11Texture2D> texture_2d;
   com_ptr<ID3D11ShaderResourceView> srv;
   com_ptr<ID3D11UnorderedAccessView> uavs[max_levels] = {}; // Theoretically we don't need the first one.
   UINT width = 1;
   UINT height = 1;
   UINT levels = 1; // >= 1 (includes base)

   bool smoothed = false;
};

void SanitizeNaNs(ID3D11Device* device, ID3D11DeviceContext* device_context, ID3D11RenderTargetView* rtv, const DeviceData& device_data, SanitizeNaNsData& data, bool smoothed = false, const char* trace_name = nullptr)
{
   if (data.original_rtv != rtv)
   {
      if (data.original_rtv)
      {
         data = SanitizeNaNsData();
      }

      if (rtv)
      {
         com_ptr<ID3D11Resource> resource;
         rtv->GetResource(&resource);
         if (resource)
         {
            com_ptr<ID3D11Texture2D> texture_2d;
            resource->QueryInterface(&texture_2d);
            if (texture_2d)
            {
               D3D11_TEXTURE2D_DESC texture_2d_desc;
               texture_2d->GetDesc(&texture_2d_desc);
#if DEVELOPMENT
               D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
               rtv->GetDesc(&rtv_desc);
               ASSERT_ONCE(rtv_desc.Format == texture_2d_desc.Format); // TODO: if this triggers, add support for view formats that don't match the texture format (in case it was typeless)
#endif

               if (IsSignedFloatFormat(texture_2d_desc.Format)) // They couldn't have NaNs otherwise, so we don't even need to worry about supporting sRGB UNORM formats as UAV
               {
                  data.texture_2d = CloneTexture<ID3D11Texture2D>(device, resource.get(), DXGI_FORMAT_UNKNOWN, D3D11_BIND_SHADER_RESOURCE | (smoothed ? D3D11_BIND_UNORDERED_ACCESS : 0), smoothed ? 0 : D3D11_BIND_RENDER_TARGET, false, false, device_context, smoothed ? 0 : -1); // Create texture with mips for NaN filtering
                  if (data.texture_2d)
                  {
                     data.original_rtv = rtv;

                     data.width = texture_2d_desc.Width;
                     data.height = texture_2d_desc.Height;

                     data.smoothed = smoothed;

                     HRESULT hr;

                     // Create full mip chain SRV
                     D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
                     srv_desc.Format = texture_2d_desc.Format;
                     srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                     srv_desc.Texture2D.MipLevels = data.smoothed ? -1 : 1; // Optionally all mips
                     srv_desc.Texture2D.MostDetailedMip = 0;
                     hr = device->CreateShaderResourceView(data.texture_2d.get(), &srv_desc, &data.srv);
                     ASSERT_ONCE(SUCCEEDED(hr));

                     if (data.smoothed)
                     {
                        data.levels = GetTextureMaxMipLevels(data.width, data.height);

#if DEVELOPMENT
                        D3D11_TEXTURE2D_DESC new_texture_2d_desc;
                        data.texture_2d->GetDesc(&new_texture_2d_desc);
                        ASSERT_ONCE(data.levels == new_texture_2d_desc.MipLevels);
#endif

                        // Create UAVs for each specific downscale pass
                        for (UINT i = 0; i < data.levels; ++i)
                        {
                           D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
                           uav_desc.Format = texture_2d_desc.Format;
                           uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
                           uav_desc.Texture2D.MipSlice = i;
                           hr = device->CreateUnorderedAccessView(data.texture_2d.get(), &uav_desc, &data.uavs[i]);
                           ASSERT_ONCE(SUCCEEDED(hr));
                        }
                     }
                  }
               }
            }
         }
      }
   }

   // This is only valid if everything succeeded
   if (data.original_rtv)
   {
      const auto cs = data.smoothed ? device_data.native_compute_shaders.find(Math::CompileTimeStringHash("Gen Sanitized Mip")) : device_data.native_compute_shaders.end();
      const auto vs = device_data.native_vertex_shaders.find(Math::CompileTimeStringHash("Copy VS"));
      const auto ps = device_data.native_pixel_shaders.find(data.smoothed ? Math::CompileTimeStringHash("Sanitize RGBA Mipped") : Math::CompileTimeStringHash("Sanitize RGBA PS"));
      if ((data.smoothed && (cs == device_data.native_compute_shaders.end() || !cs->second.get()))
         || vs == device_data.native_vertex_shaders.end() || !vs->second.get()
         || ps == device_data.native_pixel_shaders.end() || !ps->second.get()) return;

      com_ptr<ID3D11Resource> resource;
      data.original_rtv->GetResource(&resource); // If we got here, it's valid
      // Copy the first mip from the current value
      device_context->CopySubresourceRegion(data.texture_2d.get(), 0, 0, 0, 0, resource.get(), 0, nullptr);

      if (data.smoothed)
      {
         device_context->CSSetShader(cs->second.get(), nullptr, 0);
         for (UINT i = 1; i < data.levels; i++)
         {
            // In DX11 the same resource can't be bound as SRV and UAV/RTV, even if they only view one different mip, it'd be automatically unbound, so we need to use a different UAV slice and compute shaders.
            ID3D11UnorderedAccessView* const uavs[2] = { data.uavs[i - 1].get(), data.uavs[i].get() };
            device_context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);

            // Compute shader thread size is 8x8x1
            UINT x = (GetTextureMipSize(data.width, i) + 7) / 8;
            UINT y = (GetTextureMipSize(data.height, i) + 7) / 8;
            device_context->Dispatch(x, y, 1);
            Trace::Dispatch(device_context, "Sanitize NaNs Mip", x, y, 1);
         }

         // Clear them up to avoid overlaps (warnings), this is probably unnecessary as below we use a pixel shader
         ID3D11UnorderedAccessView* null_uavs[2] = { nullptr, nullptr };
         device_context->CSSetUnorderedAccessViews(0, 2, null_uavs, nullptr);
      }

      DrawCustomPixelShader(device_context, nullptr, nullptr, device_data.sampler_state_point.get(), vs->second.get(), ps->second.get(), data.srv.get(), data.original_rtv.get(), data.width, data.height, true, trace_name);
   }
}

// Drops the target-sized intermediates of "DrawSMAA()" (~83 MB at 4K), e.g. once a game stops drawing SMAA. They are recreated on the next draw.
void ReleaseSMAAIntermediates(DeviceData& device_data)
{
   auto& managed_resources = device_data.managed_resources;
   managed_resources.depth_stencil_views["smaa_dsv"_h].reset();
   managed_resources.render_target_views["smaa_edge_detection"_h].reset();
   managed_resources.shader_resource_views["smaa_edge_detection"_h].reset();
   managed_resources.render_target_views["smaa_blending_weight_calculation"_h].reset();
   managed_resources.shader_resource_views["smaa_blending_weight_calculation"_h].reset();
}

// "cb_smaa_metrics" (optional) is bound to VS and PS b1, the slot the "Luma_SMAA_impl.hlsl" files declare their metrics cbuffer at.
void DrawSMAA(ID3D11Device* device, ID3D11DeviceContext* device_context, DeviceData& device_data, ID3D11RenderTargetView* rtv, ID3D11ShaderResourceView* srv_color_tex, ID3D11ShaderResourceView* srv_color_tex_gamma, ID3D11ShaderResourceView* srv_predication_tex = nullptr, ID3D11Buffer* cb_smaa_metrics = nullptr)
{
   auto& managed_resources = device_data.managed_resources;

   // Backup IA.
   D3D11_PRIMITIVE_TOPOLOGY primitive_topology_original;
   device_context->IAGetPrimitiveTopology(&primitive_topology_original);

   // Backup VS.
   ComPtr<ID3D11VertexShader> vs_original;
   device_context->VSGetShader(vs_original.put(), nullptr, nullptr);

   // Backup PS.
   ComPtr<ID3D11PixelShader> ps_original;
   device_context->PSGetShader(ps_original.put(), nullptr, nullptr);
   std::array<ID3D11SamplerState*, 2> ps_samplers_original = {};
   device_context->PSGetSamplers(0, ps_samplers_original.size(), ps_samplers_original.data());
   std::array<ID3D11ShaderResourceView*, 3> ps_srvs_original = {};
   device_context->PSGetShaderResources(0, ps_srvs_original.size(), ps_srvs_original.data());

   // Backup the metrics cbuffer slot.
   ComPtr<ID3D11Buffer> vs_cb_metrics_original;
   ComPtr<ID3D11Buffer> ps_cb_metrics_original;
   if (cb_smaa_metrics)
   {
      device_context->VSGetConstantBuffers(1, 1, vs_cb_metrics_original.put());
      device_context->PSGetConstantBuffers(1, 1, ps_cb_metrics_original.put());
   }

   // Backup Viewports.
   UINT num_viewports;
   device_context->RSGetViewports(&num_viewports, nullptr);
   std::vector<D3D11_VIEWPORT> viewports_original(num_viewports);
   device_context->RSGetViewports(&num_viewports, viewports_original.data());

   // Backup Rasterizer.
   ComPtr<ID3D11RasterizerState> rasterizer_original;
   device_context->RSGetState(rasterizer_original.put());

   // Backup Blend.
   ComPtr<ID3D11BlendState> blend_original;
   FLOAT blend_factor_original[4];
   UINT sample_mask_original;
   device_context->OMGetBlendState(blend_original.put(), blend_factor_original, &sample_mask_original);

   // Backup DepthStencil
   ComPtr<ID3D11DepthStencilState> ds_original;
   UINT stencil_ref_original;
   device_context->OMGetDepthStencilState(ds_original.put(), &stencil_ref_original);

   // Backup RTs.
   std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> rtvs_original = {};
   ComPtr<ID3D11DepthStencilView> dsv_original;
   device_context->OMGetRenderTargets(rtvs_original.size(), rtvs_original.data(), dsv_original.put());

   // Get passed RTV's texture description.
   ComPtr<ID3D11Resource> resource;
   rtv->GetResource(resource.put());
   ComPtr<ID3D11Texture2D> tex;
   ensure(resource->QueryInterface(tex.put()), >= 0);
   D3D11_TEXTURE2D_DESC tex_desc;
   tex->GetDesc(&tex_desc);
   // The intermediates only share the target's size, not its mips, array, MSAA, usage or misc flags.
   tex_desc.MipLevels = 1;
   tex_desc.ArraySize = 1;
   tex_desc.SampleDesc = {1, 0};
   tex_desc.Usage = D3D11_USAGE_DEFAULT;
   tex_desc.CPUAccessFlags = 0;
   tex_desc.MiscFlags = 0;

   // Rebuild the intermediates whenever the target size changes (e.g. a game's resolution scale), not only with the swapchain.
   if (const auto& edge_detection_rtv = managed_resources.render_target_views["smaa_edge_detection"_h])
   {
      uint4 edge_detection_size;
      DXGI_FORMAT edge_detection_format;
      GetResourceInfo(edge_detection_rtv.get(), edge_detection_size, edge_detection_format);
      if (edge_detection_size.x != tex_desc.Width || edge_detection_size.y != tex_desc.Height)
      {
         ReleaseSMAAIntermediates(device_data);
      }
   }

   if (cb_smaa_metrics)
   {
      device_context->VSSetConstantBuffers(1, 1, &cb_smaa_metrics);
      device_context->PSSetConstantBuffers(1, 1, &cb_smaa_metrics);
   }

   // EdgeDetection pass
   //

   // Create viewport
   D3D11_VIEWPORT viewport = {};
   viewport.Width = tex_desc.Width;
   viewport.Height = tex_desc.Height;

   // Create DS.
   [[unlikely]] if (!managed_resources.depth_stencils["smaa_disable_depth_replace_stencil"_h])
   {
      CD3D11_DEPTH_STENCIL_DESC ds_desc(D3D11_DEFAULT);
      ds_desc.DepthEnable = FALSE;
      ds_desc.StencilEnable = TRUE;
      ds_desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
      ensure(device->CreateDepthStencilState(&ds_desc, managed_resources.depth_stencils["smaa_disable_depth_replace_stencil"_h].put()), >= 0);
   }

   // Create DSV.
   [[unlikely]] if (!managed_resources.depth_stencil_views["smaa_dsv"_h])
   {
      tex_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
      tex_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
      ensure(device->CreateTexture2D(&tex_desc, nullptr, tex.put()), >= 0);
      ensure(device->CreateDepthStencilView(tex.get(), nullptr, managed_resources.depth_stencil_views["smaa_dsv"_h].put()), >= 0);
   }

   // Create RT and views.
   [[unlikely]] if (!managed_resources.render_target_views["smaa_edge_detection"_h])
   {
      tex_desc.Format = DXGI_FORMAT_R8G8_UNORM;
      tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
      ensure(device->CreateTexture2D(&tex_desc, nullptr, tex.put()), >= 0);
      ensure(device->CreateRenderTargetView(tex.get(), nullptr, managed_resources.render_target_views["smaa_edge_detection"_h].put()), >= 0);
      ensure(device->CreateShaderResourceView(tex.get(), nullptr, managed_resources.shader_resource_views["smaa_edge_detection"_h].put()), >= 0);
   }

   // Bindings.
   device_context->OMSetBlendState(nullptr, nullptr, UINT_MAX);
   device_context->OMSetDepthStencilState(managed_resources.depth_stencils["smaa_disable_depth_replace_stencil"_h].get(), 1);
   device_context->OMSetRenderTargets(1, &managed_resources.render_target_views["smaa_edge_detection"_h], managed_resources.depth_stencil_views["smaa_dsv"_h].get());
   device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
   device_context->VSSetShader(device_data.native_vertex_shaders.at("SMAA Edge Detection VS"_h).get(), nullptr, 0);
   device_context->PSSetShader(device_data.native_pixel_shaders.at("SMAA Edge Detection PS"_h).get(), nullptr, 0);
   const std::array ps_samplers = { device_data.sampler_state_linear.get(), device_data.sampler_state_point.get() };
   device_context->PSSetSamplers(0, ps_samplers.size(), ps_samplers.data());
   const std::array ps_srvs_edge_detection = { srv_color_tex_gamma, srv_predication_tex };
   device_context->PSSetShaderResources(0, ps_srvs_edge_detection.size(), ps_srvs_edge_detection.data());
   device_context->RSSetViewports(1, &viewport);
   device_context->RSSetState(nullptr);

   static constexpr FLOAT clear_color[4] = {};
   device_context->ClearRenderTargetView(managed_resources.render_target_views["smaa_edge_detection"_h].get(), clear_color);
   device_context->ClearDepthStencilView(managed_resources.depth_stencil_views["smaa_dsv"_h].get(), D3D11_CLEAR_STENCIL, 1.0f, 0);
   device_context->Draw(3, 0);
   Trace::Draw(device_context, "Luma SMAA Edge Detection", 3);

   //

   // BlendingWeightCalculation pass
   //

   // Create area texture.
   [[unlikely]] if (!managed_resources.shader_resource_views["smaa_area_tex"_h])
   {
      D3D11_TEXTURE2D_DESC tex_desc = {};
      tex_desc.Width = AREATEX_WIDTH;
      tex_desc.Height = AREATEX_HEIGHT;
      tex_desc.MipLevels = 1;
      tex_desc.ArraySize = 1;
      tex_desc.Format = DXGI_FORMAT_R8G8_UNORM;
      tex_desc.SampleDesc.Count = 1;
      tex_desc.Usage = D3D11_USAGE_IMMUTABLE;
      tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
      D3D11_SUBRESOURCE_DATA subresource_data = {};
      subresource_data.pSysMem = areaTexBytes;
      subresource_data.SysMemPitch = AREATEX_PITCH;
      ensure(device->CreateTexture2D(&tex_desc, &subresource_data, tex.put()), >= 0);
      ensure(device->CreateShaderResourceView(tex.get(), nullptr, managed_resources.shader_resource_views["smaa_area_tex"_h].put()), >= 0);
   }

   // Create search texture.
   [[unlikely]] if (!managed_resources.shader_resource_views["smaa_search_tex"_h])
   {
      D3D11_TEXTURE2D_DESC tex_desc = {};
      tex_desc.Width = SEARCHTEX_WIDTH;
      tex_desc.Height = SEARCHTEX_HEIGHT;
      tex_desc.MipLevels = 1;
      tex_desc.ArraySize = 1;
      tex_desc.Format = DXGI_FORMAT_R8_UNORM;
      tex_desc.SampleDesc.Count = 1;
      tex_desc.Usage = D3D11_USAGE_IMMUTABLE;
      tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
      D3D11_SUBRESOURCE_DATA subresource_data = {};
      subresource_data.pSysMem = searchTexBytes;
      subresource_data.SysMemPitch = SEARCHTEX_PITCH;
      ensure(device->CreateTexture2D(&tex_desc, &subresource_data, tex.put()), >= 0);
      ensure(device->CreateShaderResourceView(tex.get(), nullptr, managed_resources.shader_resource_views["smaa_search_tex"_h].put()), >= 0);
   }

   // Create DS.
   [[unlikely]] if (!managed_resources.depth_stencils["smaa_disable_depth_use_stencil"_h])
   {
      CD3D11_DEPTH_STENCIL_DESC ds_desc(D3D11_DEFAULT);
      ds_desc.DepthEnable = FALSE;
      ds_desc.StencilEnable = TRUE;
      ds_desc.FrontFace.StencilFunc = D3D11_COMPARISON_EQUAL;
      ensure(device->CreateDepthStencilState(&ds_desc, managed_resources.depth_stencils["smaa_disable_depth_use_stencil"_h].put()), >= 0);
   }

   // Create RT and views.
   [[unlikely]] if (!managed_resources.render_target_views["smaa_blending_weight_calculation"_h])
   {
      tex_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      ensure(device->CreateTexture2D(&tex_desc, nullptr, tex.put()), >= 0);
      ensure(device->CreateRenderTargetView(tex.get(), nullptr, managed_resources.render_target_views["smaa_blending_weight_calculation"_h].put()), >= 0);
      ensure(device->CreateShaderResourceView(tex.get(), nullptr, managed_resources.shader_resource_views["smaa_blending_weight_calculation"_h].put()), >= 0); 
   }

   // Bindings.
   device_context->OMSetDepthStencilState(managed_resources.depth_stencils["smaa_disable_depth_use_stencil"_h].get(), 1);
   device_context->OMSetRenderTargets(1, &managed_resources.render_target_views["smaa_blending_weight_calculation"_h], managed_resources.depth_stencil_views["smaa_dsv"_h].get());
   device_context->VSSetShader(device_data.native_vertex_shaders.at("SMAA Blending Weight Calculation VS"_h).get(), nullptr, 0);
   device_context->PSSetShader(device_data.native_pixel_shaders.at("SMAA Blending Weight Calculation PS"_h).get(), nullptr, 0);
   const std::array ps_srvs_blending_weight_calculation = { managed_resources.shader_resource_views["smaa_edge_detection"_h].get(), managed_resources.shader_resource_views["smaa_area_tex"_h].get(), managed_resources.shader_resource_views["smaa_search_tex"_h].get() };
   device_context->PSSetShaderResources(0, ps_srvs_blending_weight_calculation.size(), ps_srvs_blending_weight_calculation.data());

   device_context->ClearRenderTargetView(managed_resources.render_target_views["smaa_blending_weight_calculation"_h].get(), clear_color);
   device_context->Draw(3, 0);
   Trace::Draw(device_context, "Luma SMAA Blending Weight Calculation", 3);

   //

   // NeighborhoodBlending pass
   //

   // Bindings.
   device_context->OMSetRenderTargets(1, &rtv, nullptr);
   device_context->VSSetShader(device_data.native_vertex_shaders.at("SMAA Neighborhood Blending VS"_h).get(), nullptr, 0);
   device_context->PSSetShader(device_data.native_pixel_shaders.at("SMAA Neighborhood Blending PS"_h).get(), nullptr, 0);
   const std::array ps_srvs_neighborhood_blending = { srv_color_tex, managed_resources.shader_resource_views["smaa_blending_weight_calculation"_h].get() };
   device_context->PSSetShaderResources(0, ps_srvs_neighborhood_blending.size(), ps_srvs_neighborhood_blending.data());

   device_context->Draw(3, 0);
   Trace::Draw(device_context, "Luma SMAA Neighborhood Blending", 3);

   //

   // Restore.
   if (cb_smaa_metrics)
   {
      device_context->VSSetConstantBuffers(1, 1, vs_cb_metrics_original.get_addressof());
      device_context->PSSetConstantBuffers(1, 1, ps_cb_metrics_original.get_addressof());
   }
   device_context->OMSetBlendState(blend_original.get(), blend_factor_original, sample_mask_original);
   device_context->OMSetDepthStencilState(ds_original.get(), stencil_ref_original);
   device_context->OMSetRenderTargets(rtvs_original.size(), rtvs_original.data(), dsv_original.get());
   device_context->IASetPrimitiveTopology(primitive_topology_original);
   device_context->VSSetShader(vs_original.get(), nullptr, 0);
   device_context->PSSetShader(ps_original.get(), nullptr, 0);
   device_context->PSSetSamplers(0, ps_samplers_original.size(), ps_samplers_original.data());
   device_context->PSSetShaderResources(0, ps_srvs_original.size(), ps_srvs_original.data());
   device_context->RSSetViewports(viewports_original.size(), viewports_original.data());
   device_context->RSSetState(rasterizer_original.get());

   // Release com arrays.
   auto release_com_array = [](auto& array){ for (auto* p : array) if (p) p->Release(); };
   release_com_array(rtvs_original);
   release_com_array(ps_samplers_original);
   release_com_array(ps_srvs_original);
}

#if ENABLE_RCAS
// Whether "DrawRCAS()" can draw: its shaders load asynchronously (and reload in development builds), and its cbuffer is created here.
// Call it before routing a pass into the RCAS source: once it returns true, "DrawRCAS()" always writes its target.
bool PrepareRCAS(ID3D11Device* device, DeviceData& device_data)
{
   const auto vs = device_data.native_vertex_shaders.find("Copy VS"_h);
   const auto ps = device_data.native_pixel_shaders.find("RCAS PS"_h);
   if (vs == device_data.native_vertex_shaders.end() || !vs->second || ps == device_data.native_pixel_shaders.end() || !ps->second)
      return false;

   auto& cb = device_data.managed_resources.buffers["rcas_params"_h];
   if (!cb)
   {
      CD3D11_BUFFER_DESC cb_desc(sizeof(float) * 4, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);
      device->CreateBuffer(&cb_desc, nullptr, cb.put());
   }
   return cb.get() != nullptr;
}

// RCAS sharpening ("Luma_RCAS_PS") of "srv_source" into the whole "rtv", e.g. after SMAA or super resolution.
// Call "PrepareRCAS()" first. Restores the state it changes.
void DrawRCAS(ID3D11DeviceContext* device_context, DeviceData& device_data, ID3D11ShaderResourceView* srv_source, ID3D11RenderTargetView* rtv, float sharpness)
{
   uint4 size;
   DXGI_FORMAT format;
   GetResourceInfo(rtv, size, format);

   // Map only fails with a removed device. We draw anyway, so the target is never left unwritten.
   const auto& cb = device_data.managed_resources.buffers.at("rcas_params"_h);
   if (D3D11_MAPPED_SUBRESOURCE mapped_cb; SUCCEEDED(device_context->Map(cb.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_cb)))
   {
      const float params[4] = {(float)size.x, (float)size.y, sharpness, 0.f};
      std::memcpy(mapped_cb.pData, params, sizeof(params));
      device_context->Unmap(cb.get(), 0);
   }

   DrawStateStack<DrawStateStackType::FullGraphics> draw_state_stack;
   draw_state_stack.Cache(device_context, device_data.uav_max_count);

   device_context->PSSetConstantBuffers(0, 1, cb.get_addressof());
   DrawCustomPixelShader(device_context, device_data.default_depth_stencil_state.get(), device_data.default_blend_state.get(), nullptr,
      device_data.native_vertex_shaders.at("Copy VS"_h).get(), device_data.native_pixel_shaders.at("RCAS PS"_h).get(), srv_source, rtv, size.x, size.y, false, "Luma RCAS");

   draw_state_stack.Restore(device_context);
}
#endif // ENABLE_RCAS

void DrawKarisAverage(ID3D11Device* device, ID3D11DeviceContext* device_context, DeviceData& device_data, ID3D11ShaderResourceView* srv_source, ID3D11ShaderResourceView** srv_out)
{
   auto& managed_resources = device_data.managed_resources;

   // Backup CS.
   ComPtr<ID3D11ComputeShader> cs_original;
   device_context->CSGetShader(cs_original.put(), nullptr, nullptr);
   ComPtr<ID3D11UnorderedAccessView> uav_original;
   device_context->CSGetUnorderedAccessViews(0, 1, uav_original.put());
   ComPtr<ID3D11ShaderResourceView> srv_original;
   device_context->CSGetShaderResources(0, 1, srv_original.put());

   // Get the source resource and texture description.
   ComPtr<ID3D11Resource> resource;
   srv_source->GetResource(resource.put());
   ComPtr<ID3D11Texture2D> tex;
   ensure(resource->QueryInterface(tex.put()), >= 0);
   D3D11_TEXTURE2D_DESC tex_desc;
   tex->GetDesc(&tex_desc);

   // Create RT and views.
   if (!managed_resources.unordered_access_views["luma_karis_average"_h])
   {
      tex_desc.MipLevels = 1;
      tex_desc.ArraySize = 1;
      tex_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
      tex_desc.SampleDesc.Count = 1;
      tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
      ensure(device->CreateTexture2D(&tex_desc, nullptr, tex.put()), >= 0);
      ensure(device->CreateUnorderedAccessView(tex.get(), nullptr, managed_resources.unordered_access_views["luma_karis_average"_h].put()), >= 0);
      ensure(device->CreateShaderResourceView(tex.get(), nullptr, managed_resources.shader_resource_views["luma_karis_average"_h].put()), >= 0);
   }

   // Bindings.
   device_context->CSSetUnorderedAccessViews(0, 1, &managed_resources.unordered_access_views["luma_karis_average"_h], nullptr);
   device_context->CSSetShader(device_data.native_compute_shaders.at("Karis Average CS"_h).get(), nullptr, 0);
   device_context->CSSetShaderResources(0, 1, &srv_source);

   device_context->Dispatch((tex_desc.Width + 8 - 1) / 8, (tex_desc.Height + 8 - 1) / 8, 1);
   Trace::Dispatch(device_context, "Luma Karis Average", (tex_desc.Width + 8 - 1) / 8, (tex_desc.Height + 8 - 1) / 8, 1);

   // Karis averaged out.
   *srv_out = managed_resources.shader_resource_views["luma_karis_average"_h].get();
   (*srv_out)->AddRef();

   // Restore.
   device_context->CSSetUnorderedAccessViews(0, 1, &uav_original, nullptr);
   device_context->CSSetShader(cs_original.get(), nullptr, 0);
   device_context->CSSetShaderResources(0, 1, &srv_original);

   // Reset resolution dependent resources on init swapchain.
   // Some are intentioanlly left out, we will recreate/reset them here.
   auto on_init_swapchain = [&]()
   {
      managed_resources.unordered_access_views["luma_karis_average"_h].reset();
   };

   LumaCallbacks::on_init_swapchain.try_emplace("luma_karis_average"_h, on_init_swapchain);
}

struct alignas(16) CBLumaBloomData
{
   float2 src_size;
   float2 inv_src_size;
   float2 axis;
   float sigma;
};

// "DrawBloom"'s mip chains, sized from the scene (rebuilt on a scene size or mip count change)
// TODO: Reorganize this better.
static struct
{
   std::vector<ID3D11RenderTargetView*> rtv_mips_x;
   std::vector<ID3D11ShaderResourceView*> srv_mips_x;
   std::vector<ID3D11RenderTargetView*> rtv_mips_y;
   std::vector<ID3D11ShaderResourceView*> srv_mips_y;
   UINT last_scene_width = 0;
   UINT last_scene_height = 0;
   int last_nmips = -1;
} draw_bloom_mips;

void DrawBloom(ID3D11Device* device, ID3D11DeviceContext* device_context, DeviceData& device_data, ID3D11ShaderResourceView* srv_scene, int nmips, const float* sigmas, ID3D11ShaderResourceView** srv_bloom)
{
   auto& managed_resources = device_data.managed_resources;

   auto& [rtv_mips_x, srv_mips_x, rtv_mips_y, srv_mips_y, last_scene_width, last_scene_height, last_nmips] = draw_bloom_mips;

   // Backup IA.
   D3D11_PRIMITIVE_TOPOLOGY primitive_topology_original;
   device_context->IAGetPrimitiveTopology(&primitive_topology_original);

   // Backup VS.
   ComPtr<ID3D11VertexShader> vs_original;
   device_context->VSGetShader(vs_original.put(), nullptr, nullptr);

   // Backup PS.
   ComPtr<ID3D11PixelShader> ps_original;
   device_context->PSGetShader(ps_original.put(), nullptr, nullptr);
   ComPtr<ID3D11Buffer> cb_orginal;
   device_context->PSGetConstantBuffers(11, 1, cb_orginal.put());
   ComPtr<ID3D11SamplerState> ps_sampler_original;
   device_context->PSGetSamplers(0, 1, ps_sampler_original.put());
   ComPtr<ID3D11ShaderResourceView> ps_srv_original;
   device_context->PSGetShaderResources(0, 1, ps_srv_original.put());

   // Backup Viewports.
   UINT num_viewports;
   device_context->RSGetViewports(&num_viewports, nullptr);
   std::vector<D3D11_VIEWPORT> viewports_original(num_viewports);
   device_context->RSGetViewports(&num_viewports, viewports_original.data());

   // Backup Rasterizer.
   ComPtr<ID3D11RasterizerState> rasterizer_original;
   device_context->RSGetState(rasterizer_original.put());

   // Backup Blend.
   ComPtr<ID3D11BlendState> blend_original;
   FLOAT blend_factor_original[4];
   UINT sample_mask_original;
   device_context->OMGetBlendState(blend_original.put(), blend_factor_original, &sample_mask_original);

   // Backup RTs.
   std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> rtvs_original = {};
   ComPtr<ID3D11DepthStencilView> dsv_original;
   device_context->OMGetRenderTargets(rtvs_original.size(), rtvs_original.data(), dsv_original.put());

   // Get the scene resource and texture description from the SRV.
   ComPtr<ID3D11Resource> resource;
   srv_scene->GetResource(resource.put());
   ComPtr<ID3D11Texture2D> tex;
   ensure(resource->QueryInterface(tex.put()), >= 0);
   D3D11_TEXTURE2D_DESC tex_desc;
   tex->GetDesc(&tex_desc);

   const auto scene_width = tex_desc.Width;
   const auto scene_height = tex_desc.Height;

   // The bloom mips are sized from the scene; reset them when the scene size or mip count changes
   // Needed for proper support of the new resource scaling feature.
   if (nmips != last_nmips || scene_width != last_scene_width || scene_height != last_scene_height)
   {
      ResetCOMArray(rtv_mips_x);
      ResetCOMArray(srv_mips_x);
      ResetCOMArray(rtv_mips_y);
      ResetCOMArray(srv_mips_y);

      rtv_mips_x.resize(nmips);
      srv_mips_x.resize(nmips);
      rtv_mips_y.resize(nmips);
      srv_mips_y.resize(nmips);

      last_nmips = nmips;
      last_scene_width = scene_width;
      last_scene_height = scene_height;
   }

   const UINT x_mip0_width = tex_desc.Width / 2;
   const UINT x_mip0_height = tex_desc.Height;

   const UINT y_mip0_width = tex_desc.Width / 2;
   const UINT y_mip0_height = tex_desc.Height / 2;

   // Create Y MIPs and views.
   [[unlikely]] if (!rtv_mips_y[0])
   {
      tex_desc.Width = y_mip0_width;
      tex_desc.Height = y_mip0_height;
      tex_desc.MipLevels = nmips;
      tex_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
      tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
      ensure(device->CreateTexture2D(&tex_desc, nullptr, tex.put()), >= 0);

      D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
      rtv_desc.Format = tex_desc.Format;
      rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

      D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
      srv_desc.Format = tex_desc.Format;
      srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
      srv_desc.Texture2D.MipLevels = 1;

      for (int i = 0; i < nmips; ++i)
      {
          rtv_desc.Texture2D.MipSlice = i;
          ensure(device->CreateRenderTargetView(tex.get(), &rtv_desc, &rtv_mips_y[i]), >= 0);
          srv_desc.Texture2D.MostDetailedMip = i;
          ensure(device->CreateShaderResourceView(tex.get(), &srv_desc, &srv_mips_y[i]), >= 0);
      }
   }

   // Create X MIPs and views.
   [[unlikely]] if (!rtv_mips_x[0])
   {
      // Create X MIP0 and views.
      tex_desc.Width = x_mip0_width;
      tex_desc.Height = x_mip0_height;
      tex_desc.MipLevels = 1;
      ensure(device->CreateTexture2D(&tex_desc, nullptr, tex.put()), >= 0);
      ensure(device->CreateRenderTargetView(tex.get(), nullptr, &rtv_mips_x[0]), >= 0);
      ensure(device->CreateShaderResourceView(tex.get(), nullptr, &srv_mips_x[0]), >= 0);

      // Create rest of X MIPs and views.
      for (UINT i = 1; i < nmips; ++i)
      {
         tex_desc.Width = max(1u, x_mip0_width >> i);
         tex_desc.Height = max(1u, x_mip0_height >> i);
         ensure(device->CreateTexture2D(&tex_desc, nullptr, tex.put()), >= 0);
         ensure(device->CreateRenderTargetView(tex.get(), nullptr, &rtv_mips_x[i]), >= 0);
         ensure(device->CreateShaderResourceView(tex.get(), nullptr, &srv_mips_x[i]), >= 0);
      }
   }

   // Create bloom CB.
   //

   [[unlikely]] if (!managed_resources.buffers["luma_bloom_cb"_h])
   {
      D3D11_BUFFER_DESC buffer_desc = {};
      buffer_desc.ByteWidth = sizeof(CBLumaBloomData);
      buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
      buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
      buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
      ensure(device->CreateBuffer(&buffer_desc, nullptr, managed_resources.buffers["luma_bloom_cb"_h].put()), >= 0);
   }

   CBLumaBloomData cb_data;

   auto update_constant_buffer = [&]()
   {
      D3D11_MAPPED_SUBRESOURCE mapped_subresource;
      ensure(device_context->Map(managed_resources.buffers["luma_bloom_cb"_h].get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_subresource), >= 0);
      std::memcpy(mapped_subresource.pData, &cb_data, sizeof(CBLumaBloomData));
      device_context->Unmap(managed_resources.buffers["luma_bloom_cb"_h].get(), 0);
   };

   //

   // Prefilter + downsample pass
   //

   D3D11_VIEWPORT viewport_x = {};
   viewport_x.Width = x_mip0_width;
   viewport_x.Height = x_mip0_height;

   // Update CB.
   cb_data.src_size = float2(scene_width, scene_height);
   cb_data.inv_src_size = float2(1.0f / cb_data.src_size.x, 1.0f / cb_data.src_size.y);
   cb_data.axis = float2(1.0f, 0.0f);
   cb_data.sigma = sigmas[0];
   update_constant_buffer();

   // Bindings.
   device_context->OMSetRenderTargets(1, &rtv_mips_x[0], nullptr);
   device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
   device_context->VSSetShader(device_data.native_vertex_shaders.at(Math::CompileTimeStringHash("Bloom VS")).get(), nullptr, 0);
   device_context->PSSetShader(device_data.native_pixel_shaders.at(Math::CompileTimeStringHash("Bloom Downsample PS")).get(), nullptr, 0);
   device_context->PSSetConstantBuffers(11, 1, &managed_resources.buffers["luma_bloom_cb"_h]);
   const std::array ps_samplers = { device_data.sampler_state_linear.get() };
   device_context->PSSetSamplers(0, ps_samplers.size(), ps_samplers.data());
   device_context->PSSetShaderResources(0, 1, &srv_scene);
   device_context->RSSetViewports(1, &viewport_x);
   device_context->RSSetState(nullptr);

   // Draw X pass.
   device_context->Draw(3, 0);
   Trace::Draw(device_context, "Luma Bloom Downsample X", 3);

   std::vector<D3D11_VIEWPORT> viewports_y(nmips);
   viewports_y[0].Width = y_mip0_width;
   viewports_y[0].Height = y_mip0_height;

   // Update CB.
   cb_data.src_size = float2(x_mip0_width, x_mip0_height);
   cb_data.inv_src_size = float2(1.0f / cb_data.src_size.x, 1.0f / cb_data.src_size.y);
   cb_data.axis = float2(0.0f, 1.0f);
   update_constant_buffer();

   // Bindings.
   device_context->OMSetRenderTargets(1, &rtv_mips_y[0], nullptr);
   device_context->PSSetShader(device_data.native_pixel_shaders.at("Bloom Prefilter PS"_h).get(), nullptr, 0);
   device_context->PSSetShaderResources(0, 1, &srv_mips_x[0]);
   device_context->RSSetViewports(1, &viewports_y[0]);

   // Draw Y pass.
   device_context->Draw(3, 0);
   Trace::Draw(device_context, "Luma Bloom Prefilter Y", 3);

   //

   // Downsample passes
   //

   device_context->PSSetShader(device_data.native_pixel_shaders.at("Bloom Downsample PS"_h).get(), nullptr, 0);

   // Render downsample passes.
   for (UINT i = 1; i < nmips; ++i)
   {
      viewport_x.Width = max(1u, x_mip0_width >> i);
      viewport_x.Height = max(1u, x_mip0_height >> i);

      // Update CB.
      cb_data.src_size = float2(viewports_y[i - 1].Width, viewports_y[i - 1].Height);
      cb_data.axis = float2(1.0f, 0.0f);
      cb_data.inv_src_size = float2(1.0f / cb_data.src_size.x, 1.0f / cb_data.src_size.y);
      cb_data.sigma = sigmas[i];
      update_constant_buffer();

      // Bindings.
       device_context->OMSetRenderTargets(1, &rtv_mips_x[i], nullptr);
       device_context->PSSetShaderResources(0, 1, &srv_mips_y[i - 1]);
       device_context->RSSetViewports(1, &viewport_x);

      // Draw X pass.
      device_context->Draw(3, 0);
      Trace::Draw(device_context, "Luma Bloom Downsample X", 3);

      viewports_y[i].Width = max(1u, y_mip0_width >> i);
      viewports_y[i].Height = max(1u, y_mip0_height >> i);

      // Update CB.
      cb_data.src_size = float2(viewport_x.Width, viewport_x.Height);
      cb_data.axis = float2(0.0f, 1.0f);
      cb_data.inv_src_size = float2(1.0f / cb_data.src_size.x, 1.0f / cb_data.src_size.y);
      update_constant_buffer();

      // Bindings.
      device_context->OMSetRenderTargets(1, &rtv_mips_y[i], nullptr);
      device_context->PSSetShaderResources(0, 1, &srv_mips_x[i]);
      device_context->RSSetViewports(1, &viewports_y[i]);

      // Draw Y pass.
      device_context->Draw(3, 0);
      Trace::Draw(device_context, "Luma Bloom Downsample Y", 3);
   }

   //

   // Upsample passes
   //

   device_context->PSSetShader(device_data.native_pixel_shaders.at("Bloom Upsample PS"_h).get(), nullptr, 0);
   
   [[unlikely]] if (!managed_resources.blends["luma_bloom_blend"_h])
   {
      CD3D11_BLEND_DESC blend_desc(D3D11_DEFAULT);
      blend_desc.RenderTarget[0].BlendEnable = TRUE;
      blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_BLEND_FACTOR;
      blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_BLEND_FACTOR;
      ensure(device->CreateBlendState(&blend_desc, managed_resources.blends["luma_bloom_blend"_h].put()), >= 0);
   }
   
   for (int i = nmips - 1; i > 0; --i)
   {
      // If both dst and src are D3D10_BLEND_BLEND_FACTOR
      // factor of 0.5 will be enegrgy preserving.
      static constexpr FLOAT blend_factor[] = { 0.5f, 0.5f, 0.5f, 0.0f };

      // Update CB.
      cb_data.src_size = float2(viewports_y[i].Width, viewports_y[i].Height);
      cb_data.inv_src_size = float2(1.0f / cb_data.src_size.x, 1.0f / cb_data.src_size.y);
      update_constant_buffer();

       device_context->OMSetRenderTargets(1, &rtv_mips_y[i - 1], nullptr);
       device_context->PSSetShaderResources(0, 1, &srv_mips_y[i]);
       device_context->RSSetViewports(1, &viewports_y[i - 1]);
       device_context->OMSetBlendState(managed_resources.blends["luma_bloom_blend"_h].get(), blend_factor, UINT_MAX);

       device_context->Draw(3, 0);
       Trace::Draw(device_context, "Luma Bloom Upsample", 3);
   }

   //

   // Return the final bloom.
   *srv_bloom = srv_mips_y[0];
   (*srv_bloom)->AddRef();

   // Restore.
   device_context->OMSetBlendState(blend_original.get(), blend_factor_original, sample_mask_original);
   device_context->OMSetRenderTargets(rtvs_original.size(), rtvs_original.data(), dsv_original.get());
   device_context->IASetPrimitiveTopology(primitive_topology_original);
   device_context->VSSetShader(vs_original.get(), nullptr, 0);
   device_context->PSSetShader(ps_original.get(), nullptr, 0);
   device_context->PSSetConstantBuffers(11, 1, &cb_orginal);
   device_context->PSSetSamplers(0, 1, &ps_sampler_original);
   device_context->PSSetShaderResources(0, 1, &ps_srv_original);
   device_context->RSSetViewports(viewports_original.size(), viewports_original.data());
   device_context->RSSetState(rasterizer_original.get());

   // Release com arrays.
   ResetCOMArray(rtvs_original);

   auto reset_mips = [&]()
   {
       ResetCOMArray(rtv_mips_x);
       ResetCOMArray(srv_mips_x);
       ResetCOMArray(rtv_mips_y);
       ResetCOMArray(srv_mips_y);
   };

   LumaCallbacks::on_destroy_device.try_emplace("luma_bloom"_h, reset_mips);
   LumaCallbacks::on_init_swapchain.try_emplace("luma_bloom"_h, reset_mips);
}