#pragma once

#include "../texture_data/SMAA_AreaTex.h"
#include "../texture_data/SMAA_SearchTex.h"

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
         device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &state->render_target_views[0], &state->depth_stencil_view);
         if constexpr (Mode == DrawStateStackType::FullGraphics)
         {
            for (size_t i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; i++)
            {
               bool rtv_empty = state->render_target_views[i].get() == nullptr;
               if (!rtv_empty)
               {
                  state->render_target_views[i].reset(); // Re-set it as we will re-assign it
                  state->valid_render_target_views_bound++; // The documentation is confusing, but it seems like the UAV start slot you request needs to be >= the number of valid bound RTVs. Alternatively we could check for the first valid UAV?
               }
            }
            state->depth_stencil_view.reset();
            device_context->OMGetRenderTargetsAndUnorderedAccessViews(state->valid_render_target_views_bound, &state->render_target_views[0], &state->depth_stencil_view, state->valid_render_target_views_bound, state->uav_num - state->valid_render_target_views_bound, &state->unordered_access_views[0]);
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
         ASSERT_ONCE(state->vs_instances_count == 0 && state->cs_instances_count == 0);
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
               UINT uav_initial_counts[D3D11_1_UAV_SLOT_COUNT]; // Likely not necessary, we could pass in nullptr
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
      com_ptr<ID3D11RasterizerState> rasterizer_state;
      UINT valid_render_target_views_bound = 0;
      UINT uav_num = D3D11_1_UAV_SLOT_COUNT;
   };

   // Store this on the heap instead of inlining it in the stack otherwise it'd allocate way too much
   std::unique_ptr<State> state;
};

#if DEVELOPMENT
// Expects mutexes to already be locked
void AddTraceDrawCallData(std::vector<TraceDrawCallData>& trace_draw_calls_data, const DeviceData& device_data, ID3D11DeviceContext* native_device_context, uint64_t pipeline_handle,
   std::unordered_map<uint32_t, CachedShader*>& shader_cache, const std::unordered_map<const ID3D11InputLayout*, std::vector<D3D11_INPUT_ELEMENT_DESC>>& input_layouts_descs, const DrawDispatchData& draw_dispatch_data, std::unordered_map<uint64_t, uint64_t> mirrored_rvs_redirector)
{
   TraceDrawCallData trace_draw_call_data;

#if 1
   trace_draw_call_data.pipeline_handle = pipeline_handle;
#else
   trace_draw_call_data.shader_hashes = shader_hash;
   trace_draw_call_data.pipeline_handles.push_back(pipeline_handle);
#endif

   trace_draw_call_data.command_list = native_device_context;

   // In case we redirected resource views (indirect texture upgrades), force print back the original one here,
   // the upgraded one will be visible in the lists anyway
   auto RedirectMirroredRVS = [&]<typename T>(T* ptr) -> T*
   {
      uint64_t handle = reinterpret_cast<uint64_t>(ptr);
      for (const auto& [key, value] : mirrored_rvs_redirector)
      {
         if (value == handle)
            return reinterpret_cast<T*>(key);
      }
      return ptr;
   };

   // Note that the pipelines can be run more than once so this will return the first one matching (there's only one actually, we don't have separate settings for their running instance, as that's runtime stuff)
   const auto pipeline_pair = device_data.pipeline_cache_by_pipeline_handle.find(pipeline_handle);
   const bool is_valid = pipeline_pair != device_data.pipeline_cache_by_pipeline_handle.end() && pipeline_pair->second != nullptr;
   if (is_valid)
   {
      // Expose if needed
      constexpr bool show_unused_bound_resources = false; // If a resource is bound but not read/written to by the shader (this often happens with SRVs and UAVs, rarely with RTVs, given they almost 1:1 match with the shader)
      constexpr bool show_used_unbound_resources = true; // If a resource is not bound (null) but it is read/written to by the shader (which will result in black on read, and nothing on writes)
      // Note that in DX11, some resources might be inherited by the main device context when merging an async device context to the main one, and we couldn't see them yet. Most games don't seem to rely on that.

      const auto pipeline = pipeline_pair->second;
      const CachedShader* cached_shader = (!pipeline->shader_hashes.empty() && shader_cache.contains(pipeline->shader_hashes[0])) ? shader_cache[pipeline->shader_hashes[0]] : nullptr; // DX10/11 exclusive behaviour
      assert(cached_shader);
      if (pipeline->HasPixelShader())
      {
         UINT scissor_viewport_num = 0;
         native_device_context->RSGetScissorRects(&scissor_viewport_num, nullptr); // This will get the number of scissor rects used
         UINT scissor_viewport_num_max = min(scissor_viewport_num, 1);
         D3D11_RECT scissor_rects;
         native_device_context->RSGetScissorRects(&scissor_viewport_num_max, &scissor_rects); // This is useless
         if (scissor_viewport_num_max >= 1)
         {
            trace_draw_call_data.scissors = true;
         }

         native_device_context->RSGetViewports(&scissor_viewport_num, nullptr); // This will get the number of viewports used
         scissor_viewport_num_max = min(scissor_viewport_num, 1);
         D3D11_VIEWPORT viewport;
         native_device_context->RSGetViewports(&scissor_viewport_num_max, &viewport);
         if (scissor_viewport_num_max >= 1)
         {
            trace_draw_call_data.viewport_0 = { viewport.TopLeftX, viewport.TopLeftY, viewport.Width, viewport.Height };
         }

         com_ptr<ID3D11RenderTargetView> rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
         com_ptr<ID3D11UnorderedAccessView> uavs[D3D11_1_UAV_SLOT_COUNT];
         com_ptr<ID3D11DepthStencilView> dsv;
         native_device_context->OMGetRenderTargetsAndUnorderedAccessViews(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], &dsv, 0, device_data.uav_max_count, &uavs[0]);
         dsv = RedirectMirroredRVS(dsv.get());

         com_ptr<ID3D11DepthStencilState> depth_stencil_state;
         native_device_context->OMGetDepthStencilState(&depth_stencil_state, nullptr);
         if (depth_stencil_state)
         {
            D3D11_DEPTH_STENCIL_DESC depth_stencil_desc;
            depth_stencil_state->GetDesc(&depth_stencil_desc);
            if (depth_stencil_desc.DepthEnable)
            {
               if (dsv.get())
               {
                  if (depth_stencil_desc.DepthWriteMask == D3D11_DEPTH_WRITE_MASK_ZERO)
                  {
                     // For now we ignore the "D3D11_COMPARISON_NEVER" as realistically it should never be used
                     if (depth_stencil_desc.DepthFunc != D3D11_COMPARISON_ALWAYS)
                     {
                        trace_draw_call_data.depth_state = TraceDrawCallData::DepthStateType::TestOnly;
                     }
                     // We neither read nor write the depth, so it's essentially disabled
                     else
                     {
                        trace_draw_call_data.depth_state = TraceDrawCallData::DepthStateType::Disabled;
                     }
                  }
                  else //if (depth_stencil_desc.DepthWriteMask == D3D11_DEPTH_WRITE_MASK_ALL) // Implied
                  {
                     if (depth_stencil_desc.DepthFunc != D3D11_COMPARISON_ALWAYS)
                     {
                        trace_draw_call_data.depth_state = TraceDrawCallData::DepthStateType::TestAndWrite;
                     }
                     else
                     {
                        trace_draw_call_data.depth_state = TraceDrawCallData::DepthStateType::WriteOnly;
                     }
                  }
               }
               // Depth texture is missing, unknown consequence
               else
               {
                  trace_draw_call_data.depth_state = TraceDrawCallData::DepthStateType::Invalid;
               }
            }

            trace_draw_call_data.stencil_enabled = depth_stencil_desc.StencilEnable && dsv.get(); // TODO: do better states for it

            if (trace_draw_call_data.depth_state != TraceDrawCallData::DepthStateType::Disabled && trace_draw_call_data.depth_state != TraceDrawCallData::DepthStateType::Invalid)
            {
               D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc;
               dsv->GetDesc(&dsv_desc);
               trace_draw_call_data.dsv_format = dsv_desc.Format;
               ASSERT_ONCE(dsv_desc.Format != DXGI_FORMAT_UNKNOWN); // Unexpected?
               com_ptr<ID3D11Resource> ds_resource;
               dsv->GetResource(&ds_resource);
               uint4 ds_size = {};
               GetResourceInfo(ds_resource.get(), ds_size, trace_draw_call_data.ds_format, nullptr, &trace_draw_call_data.ds_hash, &trace_draw_call_data.ds_debug_name);
               trace_draw_call_data.ds_size.x = ds_size.x;
               trace_draw_call_data.ds_size.y = ds_size.y;
            }
         }

         com_ptr<ID3D11BlendState> blend_state;
         native_device_context->OMGetBlendState(&blend_state, trace_draw_call_data.blend_factor, nullptr);
         if (blend_state)
         {
            D3D11_BLEND_DESC blend_desc;
            blend_state->GetDesc(&blend_desc);
            // We always cache the last one used by the pipeline, hopefully it didn't change between draw calls
            trace_draw_call_data.blend_desc = blend_desc;
            // We don't care for the alpha blend operation (source alpha * dest alpha) as alpha is never read back from destination
         }

         for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT && i < TraceDrawCallData::rtvs_size; i++)
         {
            if ((rtvs[i] != nullptr || (show_used_unbound_resources && cached_shader->rtvs[i])) && (show_unused_bound_resources || cached_shader->rtvs[i]))
            {
               rtvs[i] = RedirectMirroredRVS(rtvs[i].get());
               trace_draw_call_data.rtvs[i] = rtvs[i].get();

               D3D11_RENDER_TARGET_VIEW_DESC rtv_desc = {};
               rtv_desc.Format = DXGI_FORMAT(-1);
               if (rtvs[i])
               {
                  rtvs[i]->GetDesc(&rtv_desc);
                  ASSERT_ONCE(rtv_desc.Format != DXGI_FORMAT_UNKNOWN); // Unexpected?
               }
               trace_draw_call_data.rtv_format[i] = rtv_desc.Format;
               com_ptr<ID3D11Resource> rt_resource;
               if (rtvs[i])
               {
                  rtvs[i]->GetResource(&rt_resource);
                  ASSERT_ONCE(rt_resource != nullptr); // Could happen
               }
               if (rt_resource)
               {
                  // If any of the set RTs are the swapchain, set it to true
                  trace_draw_call_data.rt_is_swapchain[i] |= device_data.back_buffers.contains((uint64_t)rt_resource.get());
                  GetResourceInfo(rt_resource.get(), trace_draw_call_data.rt_size[i], trace_draw_call_data.rt_format[i], &trace_draw_call_data.rt_type_name[i], &trace_draw_call_data.rt_hash[i], &trace_draw_call_data.rt_debug_name[i]);
                  
                  trace_draw_call_data.rtv_mip[i] = GetRTVMipLevel(rtv_desc);
                  uint3 base_size = uint3{ trace_draw_call_data.rt_size[i].x, trace_draw_call_data.rt_size[i].y, trace_draw_call_data.rt_size[i].z };
                  trace_draw_call_data.rtv_size[i] = GetTextureMipSize(base_size, trace_draw_call_data.rtv_mip[i]);
               }
            }
         }
         // These would likely get ignored if they weren't set, so clear them
         if (!dsv)
         {
            trace_draw_call_data.depth_state = TraceDrawCallData::DepthStateType::Disabled;
            trace_draw_call_data.stencil_enabled = false;
         }
         for (UINT i = 0; i < device_data.uav_max_count && i < TraceDrawCallData::uavs_size; i++)
         {
            if ((uavs[i] != nullptr || (show_used_unbound_resources && cached_shader->uavs[i])) && (show_unused_bound_resources || cached_shader->uavs[i]))
            {
               uavs[i] = RedirectMirroredRVS(uavs[i].get());
               trace_draw_call_data.uavs[i] = uavs[i].get();

               D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
               uav_desc.Format = DXGI_FORMAT(-1);
               if (uavs[i]) uavs[i]->GetDesc(&uav_desc);
               trace_draw_call_data.uav_format[i] = uav_desc.Format;

               GetResourceInfo(uavs[i].get(), trace_draw_call_data.ua_size[i], trace_draw_call_data.ua_format[i], &trace_draw_call_data.ua_type_name[i], &trace_draw_call_data.ua_hash[i], &trace_draw_call_data.ua_debug_name[i], &trace_draw_call_data.ua_is_rt[i]);
               
               trace_draw_call_data.uav_mip[i] = GetUAVMipLevel(uav_desc);
               uint3 base_size = uint3{ trace_draw_call_data.ua_size[i].x, trace_draw_call_data.ua_size[i].y, trace_draw_call_data.ua_size[i].z };
               trace_draw_call_data.uav_size[i] = GetTextureMipSize(base_size, trace_draw_call_data.uav_mip[i]);
            }
         }

         com_ptr<ID3D11ShaderResourceView> srvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
         native_device_context->PSGetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, &srvs[0]);
         for (UINT i = 0; i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT && i < TraceDrawCallData::srvs_size; i++)
         {
            if ((srvs[i] != nullptr || (show_used_unbound_resources && cached_shader->srvs[i])) && (show_unused_bound_resources || cached_shader->srvs[i]))
            {
               srvs[i] = RedirectMirroredRVS(srvs[i].get());
               trace_draw_call_data.srvs[i] = srvs[i].get();

               D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
               srv_desc.Format = DXGI_FORMAT(-1);
               if (srvs[i]) srvs[i]->GetDesc(&srv_desc);
               trace_draw_call_data.srv_format[i] = srv_desc.Format;

               GetResourceInfo(srvs[i].get(), trace_draw_call_data.sr_size[i], trace_draw_call_data.sr_format[i], &trace_draw_call_data.sr_type_name[i], &trace_draw_call_data.sr_hash[i], &trace_draw_call_data.sr_debug_name[i], &trace_draw_call_data.sr_is_rt[i], &trace_draw_call_data.sr_is_ua[i]);

               trace_draw_call_data.srv_mip[i] = GetSRVMipLevel(srv_desc);
               uint3 base_size = uint3{ trace_draw_call_data.sr_size[i].x, trace_draw_call_data.sr_size[i].y, trace_draw_call_data.sr_size[i].z };
               trace_draw_call_data.srv_size[i] = GetTextureMipSize(base_size, trace_draw_call_data.srv_mip[i]);
            }
         }

         com_ptr<ID3D11Buffer> cbs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
         native_device_context->PSGetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, &cbs[0]);
         for (UINT i = 0; i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; i++)
         {
            if ((cbs[i] != nullptr || (show_used_unbound_resources && cached_shader->cbs[i])) && (show_unused_bound_resources || cached_shader->cbs[i]))
            {
               trace_draw_call_data.cbs[i] = true;
               trace_draw_call_data.cb_hash[i] = std::to_string(std::hash<void*>{}(cbs[i].get()));
            }
         }

         com_ptr<ID3D11SamplerState> sampler_states[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT];
         native_device_context->PSGetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, &sampler_states[0]);
         for (UINT i = 0; i < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT; i++)
         {
            if ((sampler_states[i] != nullptr || (show_used_unbound_resources && cached_shader->samplers[i])) && (show_unused_bound_resources || cached_shader->samplers[i]))
            {
               D3D11_SAMPLER_DESC desc = {};
               desc.Filter = D3D11_FILTER(-1);
               if (sampler_states[i]) sampler_states[i]->GetDesc(&desc);
               trace_draw_call_data.samplers_filter[i] = desc.Filter;
               trace_draw_call_data.samplers_address_u[i] = desc.AddressU;
               trace_draw_call_data.samplers_address_v[i] = desc.AddressV;
               trace_draw_call_data.samplers_address_w[i] = desc.AddressW;
               trace_draw_call_data.samplers_mip_lod_bias[i] = desc.MipLODBias;
            }
         }
      }
      else if (pipeline->HasVertexShader())
      {
         com_ptr<ID3D11ShaderResourceView> srvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
         native_device_context->VSGetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, &srvs[0]);
         for (UINT i = 0; i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT && i < TraceDrawCallData::srvs_size; i++)
         {
            if ((srvs[i] != nullptr || (show_used_unbound_resources && cached_shader->srvs[i])) && (show_unused_bound_resources || cached_shader->srvs[i]))
            {
               srvs[i] = RedirectMirroredRVS(srvs[i].get());
               trace_draw_call_data.srvs[i] = srvs[i].get();

               D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
               srv_desc.Format = DXGI_FORMAT(-1);
               if (srvs[i]) srvs[i]->GetDesc(&srv_desc);
               trace_draw_call_data.srv_format[i] = srv_desc.Format;

               GetResourceInfo(srvs[i].get(), trace_draw_call_data.sr_size[i], trace_draw_call_data.sr_format[i], &trace_draw_call_data.sr_type_name[i], &trace_draw_call_data.sr_hash[i], &trace_draw_call_data.sr_debug_name[i], &trace_draw_call_data.sr_is_rt[i], &trace_draw_call_data.sr_is_ua[i]);

               trace_draw_call_data.srv_mip[i] = GetSRVMipLevel(srv_desc);
               uint3 base_size = uint3{ trace_draw_call_data.sr_size[i].x, trace_draw_call_data.sr_size[i].y, trace_draw_call_data.sr_size[i].z };
               trace_draw_call_data.srv_size[i] = GetTextureMipSize(base_size, trace_draw_call_data.srv_mip[i]);
            }
         }

         trace_draw_call_data.draw_dispatch_data = draw_dispatch_data;

         com_ptr<ID3D11Buffer> index_buffer;
         native_device_context->IAGetIndexBuffer(&index_buffer, &trace_draw_call_data.index_buffer_format, &trace_draw_call_data.index_buffer_offset);
         com_ptr<ID3D11InputLayout> input_layout;
         native_device_context->IAGetInputLayout(&input_layout);

         trace_draw_call_data.index_buffer_hash = std::to_string(std::hash<void*>{}(index_buffer.get()));
         trace_draw_call_data.input_layout_hash = std::to_string(std::hash<void*>{}(input_layout.get()));

         //TODOFT5: do multiple of these! And print more data, and find the right vertex buffer
         if (input_layouts_descs.contains(input_layout.get()))
         {
            const auto& input_elements_descs = input_layouts_descs.find(input_layout.get());
            for (size_t i = 0; i < input_elements_descs->second.size(); i++)
            {
               com_ptr<ID3D11Buffer> vertex_buffer;
               trace_draw_call_data.input_layouts_formats.push_back(input_elements_descs->second.at(i).Format);
               native_device_context->IAGetVertexBuffers(input_elements_descs->second.at(i).InputSlot, 1, &vertex_buffer, nullptr, nullptr);
               trace_draw_call_data.vertex_buffer_hashes.push_back(std::to_string(std::hash<void*>{}(vertex_buffer.get())));
            }
         }

         native_device_context->IAGetPrimitiveTopology(&trace_draw_call_data.primitive_topology);

         com_ptr<ID3D11Buffer> cbs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
         native_device_context->VSGetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, &cbs[0]);
         for (UINT i = 0; i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; i++)
         {
            if ((cbs[i] != nullptr || (show_used_unbound_resources && cached_shader->cbs[i])) && (show_unused_bound_resources || cached_shader->cbs[i]))
            {
               trace_draw_call_data.cbs[i] = true;
               trace_draw_call_data.cb_hash[i] = std::to_string(std::hash<void*>{}(cbs[i].get()));
            }
         }

         com_ptr<ID3D11SamplerState> sampler_states[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT];
         native_device_context->VSGetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, &sampler_states[0]);
         for (UINT i = 0; i < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT; i++)
         {
            if ((sampler_states[i] != nullptr || (show_used_unbound_resources && cached_shader->samplers[i])) && (show_unused_bound_resources || cached_shader->samplers[i]))
            {
               D3D11_SAMPLER_DESC desc = {};
               desc.Filter = D3D11_FILTER(-1);
               if (sampler_states[i]) sampler_states[i]->GetDesc(&desc);
               trace_draw_call_data.samplers_filter[i] = desc.Filter;
               trace_draw_call_data.samplers_address_u[i] = desc.AddressU;
               trace_draw_call_data.samplers_address_v[i] = desc.AddressV;
               trace_draw_call_data.samplers_address_w[i] = desc.AddressW;
            }
         }
      }
      else if (pipeline->HasComputeShader())
      {
         com_ptr<ID3D11ShaderResourceView> srvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
         native_device_context->CSGetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, &srvs[0]);
         for (UINT i = 0; i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT && i < TraceDrawCallData::srvs_size; i++)
         {
            if ((srvs[i] != nullptr || (show_used_unbound_resources && cached_shader->srvs[i])) && (show_unused_bound_resources || cached_shader->srvs[i]))
            {
               srvs[i] = RedirectMirroredRVS(srvs[i].get());
               trace_draw_call_data.srvs[i] = srvs[i].get();

               D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
               srv_desc.Format = DXGI_FORMAT(-1);
               if (srvs[i]) srvs[i]->GetDesc(&srv_desc);
               trace_draw_call_data.srv_format[i] = srv_desc.Format;

               GetResourceInfo(srvs[i].get(), trace_draw_call_data.sr_size[i], trace_draw_call_data.sr_format[i], &trace_draw_call_data.sr_type_name[i], &trace_draw_call_data.sr_hash[i], &trace_draw_call_data.sr_debug_name[i], &trace_draw_call_data.sr_is_rt[i], &trace_draw_call_data.sr_is_ua[i]);

               trace_draw_call_data.srv_mip[i] = GetSRVMipLevel(srv_desc);
               uint3 base_size = uint3{ trace_draw_call_data.sr_size[i].x, trace_draw_call_data.sr_size[i].y, trace_draw_call_data.sr_size[i].z };
               trace_draw_call_data.srv_size[i] = GetTextureMipSize(base_size, trace_draw_call_data.srv_mip[i]);
            }
         }

         com_ptr<ID3D11UnorderedAccessView> uavs[D3D11_1_UAV_SLOT_COUNT];
         native_device_context->CSGetUnorderedAccessViews(0, device_data.uav_max_count, &uavs[0]);
         for (UINT i = 0; i < device_data.uav_max_count && i < TraceDrawCallData::uavs_size; i++)
         {
            if ((uavs[i] != nullptr || (show_used_unbound_resources && cached_shader->uavs[i])) && (show_unused_bound_resources || cached_shader->uavs[i]))
            {
               uavs[i] = RedirectMirroredRVS(uavs[i].get());
               trace_draw_call_data.uavs[i] = uavs[i].get();

               D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
               uav_desc.Format = DXGI_FORMAT(-1);
               if (uavs[i]) uavs[i]->GetDesc(&uav_desc);
               trace_draw_call_data.uav_format[i] = uav_desc.Format;

               GetResourceInfo(uavs[i].get(), trace_draw_call_data.ua_size[i], trace_draw_call_data.ua_format[i], &trace_draw_call_data.ua_type_name[i], &trace_draw_call_data.ua_hash[i], &trace_draw_call_data.ua_debug_name[i], &trace_draw_call_data.ua_is_rt[i]);

               trace_draw_call_data.uav_mip[i] = GetUAVMipLevel(uav_desc);
               uint3 base_size = uint3{ trace_draw_call_data.ua_size[i].x, trace_draw_call_data.ua_size[i].y, trace_draw_call_data.ua_size[i].z };
               trace_draw_call_data.uav_size[i] = GetTextureMipSize(base_size, trace_draw_call_data.uav_mip[i]);
            }
         }

         trace_draw_call_data.draw_dispatch_data = draw_dispatch_data;

         com_ptr<ID3D11Buffer> cbs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
         native_device_context->CSGetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, &cbs[0]);
         for (UINT i = 0; i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; i++)
         {
            if ((cbs[i] != nullptr || (show_used_unbound_resources && cached_shader->cbs[i])) && (show_unused_bound_resources || cached_shader->cbs[i]))
            {
               trace_draw_call_data.cbs[i] = true;
               trace_draw_call_data.cb_hash[i] = std::to_string(std::hash<void*>{}(cbs[i].get()));
            }
         }

         com_ptr<ID3D11SamplerState> sampler_states[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT];
         native_device_context->CSGetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, &sampler_states[0]);
         for (UINT i = 0; i < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT; i++)
         {
            if ((sampler_states[i] != nullptr || (show_used_unbound_resources && cached_shader->samplers[i])) && (show_unused_bound_resources || cached_shader->samplers[i]))
            {
               D3D11_SAMPLER_DESC desc = {};
               desc.Filter = D3D11_FILTER(-1);
               if (sampler_states[i]) sampler_states[i]->GetDesc(&desc);
               trace_draw_call_data.samplers_filter[i] = desc.Filter;
               trace_draw_call_data.samplers_address_u[i] = desc.AddressU;
               trace_draw_call_data.samplers_address_v[i] = desc.AddressV;
               trace_draw_call_data.samplers_address_w[i] = desc.AddressW;
            }
         }
      }
   }

   if (trace_draw_calls_data.capacity() - trace_draw_calls_data.size() <= 1)
      trace_draw_calls_data.reserve(trace_draw_calls_data.size() + 1000); // Possible optimization
   trace_draw_calls_data.push_back(trace_draw_call_data);
}

// Expects mutexes to already be locked
void AddCustomTraceDrawCallData(std::vector<TraceDrawCallData>& trace_draw_calls_data, ID3D11DeviceContext* native_device_context, const char* name, ID3D11View* target_view, bool insert_before_last = false)
{
   TraceDrawCallData trace_draw_call_data;
   trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
   trace_draw_call_data.command_list = native_device_context;
   trace_draw_call_data.custom_name = name;
   // Re-use the RTV data for simplicity (this is hardcoded to be read by "TraceDrawCallData::TraceDrawCallType::Custom" in imgui)
   GetResourceInfo(target_view, trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);

   if (insert_before_last && !trace_draw_calls_data.empty())
      trace_draw_calls_data.insert(trace_draw_calls_data.end() - 1, trace_draw_call_data);
   else
      trace_draw_calls_data.push_back(trace_draw_call_data);
}
#endif

// Fullscreen (full render target) pass
void DrawCustomPixelShader(ID3D11DeviceContext* device_context, ID3D11DepthStencilState* depth_stencil_state, ID3D11BlendState* blend_state, ID3D11SamplerState* sampler_state, ID3D11VertexShader* vs, ID3D11PixelShader* ps, ID3D11ShaderResourceView* source_resource_texture_view, ID3D11RenderTargetView* target_resource_texture_view, UINT width, UINT height, bool alpha = true)
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

#if DEVELOPMENT
      // Scissors are often set after viewports in games (e.g. Prey), so check them separately.
      // We need to make sure that all the draw calls after SR upscaling run at full resolution and not rendering resolution.
      com_ptr<ID3D11RasterizerState> state;
      device_context->RSGetState(&state);
      if (state.get())
      {
         D3D11_RASTERIZER_DESC state_desc;
         state->GetDesc(&state_desc);
         if (state_desc.ScissorEnable)
         {
            D3D11_RECT scissor_rects[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
            UINT scissor_rects_num = 0;
            // This will get the number of scissor rects used
            device_context->RSGetScissorRects(&scissor_rects_num, nullptr);
            ASSERT_ONCE(scissor_rects_num == 1); // Possibly innocuous as long as it's > 0, but we should only ever have one viewport and one RT!
            device_context->RSGetScissorRects(&scissor_rects_num, &scissor_rects[0]);

            // If this ever triggered, we'd need to replace scissors too after SR upscaling (and make them full resolution).
            ASSERT_ONCE(scissor_rects[0].left == 0 && scissor_rects[0].top == 0 && scissor_rects[0].right == render_target_texture_2d_desc.Width && scissor_rects[0].bottom == render_target_texture_2d_desc.Height);
         }
      }
#endif // DEVELOPMENT

      size.x = render_target_texture_2d_desc.Width;
      size.y = render_target_texture_2d_desc.Height;
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

bool IsRTAlphaBlendDisabled(const D3D11_RENDER_TARGET_BLEND_DESC& rt_blend_desc)
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

bool IsRTRGBBlendDisabled(const D3D11_RENDER_TARGET_BLEND_DESC& rt_blend_desc)
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
bool IsRTBlendDisabled(const D3D11_RENDER_TARGET_BLEND_DESC& rt_blend_desc)
{
   return IsRTRGBBlendDisabled(rt_blend_desc) && IsRTAlphaBlendDisabled(rt_blend_desc);
}

// Helper to know if a blend inverts any source or dest color/alpha, or subtracts one from another,
// all operations that work fine in UNORM (as they are limited to 0-1, even within the blend math) render targets but break with SIGNED FLOAT.
// Alpha checks are separated as often it's manually kept to 0-1 so poses no risk.
bool IsBlendInverted(const D3D11_BLEND_DESC& blend_desc, UINT render_targets = 1, bool check_alpha = false, UINT first_render_target = 0)
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
         return check_alpha;
      }
      return false;
   };

   for (UINT i = first_render_target; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT && i < (render_targets - first_render_target); i++)
   {
      if (blend_desc.RenderTarget[i].BlendEnable)
      {
         if (blend_desc.RenderTarget[i].BlendOp == D3D11_BLEND_OP_SUBTRACT || blend_desc.RenderTarget[i].BlendOp == D3D11_BLEND_OP_REV_SUBTRACT)
         {
            return true;
         }
         if (IsBlendInverted_Internal(blend_desc.RenderTarget[i].SrcBlend, check_alpha) || IsBlendInverted_Internal(blend_desc.RenderTarget[i].DestBlend, check_alpha))
         {
            return true;
         }
         if (check_alpha)
         {
            if (blend_desc.RenderTarget[i].BlendOpAlpha == D3D11_BLEND_OP_SUBTRACT || blend_desc.RenderTarget[i].BlendOpAlpha == D3D11_BLEND_OP_REV_SUBTRACT)
            {
               return true;
            }
            if (IsBlendInverted_Internal(blend_desc.RenderTarget[i].SrcBlendAlpha, check_alpha) || IsBlendInverted_Internal(blend_desc.RenderTarget[i].DestBlendAlpha, check_alpha))
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

void DrawCustomPixelShaderPass(ID3D11Device* device, ID3D11DeviceContext* device_context, ID3D11View* resource_view, const DeviceData& device_data, uint32_t pixel_shader_hash, CustomPixelShaderPassData& data)
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

      DrawCustomPixelShader(device_context, nullptr, nullptr, device_data.sampler_state_point.get(), vs->second.get(), ps->second.get(), data.srv.get(), data.original_or_custom_rtv.get(), data.width, data.height);
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

void SanitizeNaNs(ID3D11Device* device, ID3D11DeviceContext* device_context, ID3D11RenderTargetView* rtv, const DeviceData& device_data, SanitizeNaNsData& data, bool smoothed = false)
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
                  data.texture_2d = CloneTexture<ID3D11Texture2D>(device, resource.get(), DXGI_FORMAT_UNKNOWN, D3D11_BIND_SHADER_RESOURCE | (smoothed ? D3D11_BIND_UNORDERED_ACCESS : 0), D3D11_BIND_RENDER_TARGET, false, false, device_context, smoothed ? 0 : -1); // Create texture with mips for NaN filtering
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
         }

         // Clear them up to avoid overlaps (warnings), this is probably unnecessary as below we use a pixel shader
         ID3D11UnorderedAccessView* null_uavs[2] = { nullptr, nullptr };
         device_context->CSSetUnorderedAccessViews(0, 2, null_uavs, nullptr);
      }

      DrawCustomPixelShader(device_context, nullptr, nullptr, device_data.sampler_state_point.get(), vs->second.get(), ps->second.get(), data.srv.get(), data.original_rtv.get(), data.width, data.height);
   }
}

void DrawSMAA(ID3D11Device* device, ID3D11DeviceContext* device_context, const DeviceData& device_data, ID3D11RenderTargetView* rtv, ID3D11ShaderResourceView* srv_color_tex, ID3D11ShaderResourceView* srv_color_tex_gamma, ID3D11ShaderResourceView* srv_predication_tex = nullptr)
{
   // TODO: SMAA has some constant device data, move that somewhere else.
   
   // Backup IA.
   D3D11_PRIMITIVE_TOPOLOGY primitive_topology_original;
   device_context->IAGetPrimitiveTopology(&primitive_topology_original);

   // Backup VS.
   com_ptr<ID3D11VertexShader> vs_original;
   device_context->VSGetShader(&vs_original, nullptr, nullptr);

   // Backup PS.
   com_ptr<ID3D11PixelShader> ps_original;
   device_context->PSGetShader(&ps_original, nullptr, nullptr);
   std::array<ID3D11SamplerState*, 2> ps_samplers_original = {};
   device_context->PSGetSamplers(0, ps_samplers_original.size(), ps_samplers_original.data());
   std::array<ID3D11ShaderResourceView*, 3> ps_srvs_original = {};
   device_context->PSGetShaderResources(0, ps_srvs_original.size(), ps_srvs_original.data());

   // Backup Viewports.
   UINT num_viewports;
   device_context->RSGetViewports(&num_viewports, nullptr);
   std::vector<D3D11_VIEWPORT> viewports_original(num_viewports);
   device_context->RSGetViewports(&num_viewports, viewports_original.data());

   // Backup Rasterizer.
   com_ptr<ID3D11RasterizerState> rasterizer_original;
   device_context->RSGetState(&rasterizer_original);

   // Backup Blend.
   com_ptr<ID3D11BlendState> blend_original;
   FLOAT blend_factor_original[4];
   UINT sample_mask_original;
   device_context->OMGetBlendState(&blend_original, blend_factor_original, &sample_mask_original);

   // Backup DepthStencil
   com_ptr<ID3D11DepthStencilState> ds_original;
   UINT stencil_ref_original;
   device_context->OMGetDepthStencilState(&ds_original, &stencil_ref_original);

   // Backup RTs.
   std::array<ID3D11RenderTargetView*, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> rtvs_original = {};
   com_ptr<ID3D11DepthStencilView> dsv_original;
   device_context->OMGetRenderTargets(rtvs_original.size(), rtvs_original.data(), &dsv_original);

   // Get passed RTV's texture description.
   com_ptr<ID3D11Resource> resource;
   rtv->GetResource(&resource);
   com_ptr<ID3D11Texture2D> tex;
   auto hr = resource->QueryInterface(&tex);
   assert(SUCCEEDED(hr));
   D3D11_TEXTURE2D_DESC tex_desc;
   tex->GetDesc(&tex_desc);

   // EdgeDetection pass
   //

   // Create viewport
   D3D11_VIEWPORT viewport = {};
   viewport.Width = tex_desc.Width;
   viewport.Height = tex_desc.Height;

   // Create DS.
   static com_ptr<ID3D11DepthStencilState> ds_disable_depth_replace_stencil;
   [[unlikely]] if (!ds_disable_depth_replace_stencil)
   {
      CD3D11_DEPTH_STENCIL_DESC ds_desc(D3D11_DEFAULT);
      ds_desc.DepthEnable = FALSE;
      ds_desc.StencilEnable = TRUE;
      ds_desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
      hr = device->CreateDepthStencilState(&ds_desc, &ds_disable_depth_replace_stencil);
      assert(SUCCEEDED(hr));
   }

   // Create DSV.
   tex_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
   tex_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
   tex.reset();
   hr = device->CreateTexture2D(&tex_desc, nullptr, &tex);
   assert(SUCCEEDED(hr));
   com_ptr<ID3D11DepthStencilView> dsv;
   hr = device->CreateDepthStencilView(tex.get(), nullptr, &dsv);
   assert(SUCCEEDED(hr));

   // Create RT and views.
   tex_desc.Format = DXGI_FORMAT_R8G8_UNORM;
   tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
   tex.reset();
   hr = device->CreateTexture2D(&tex_desc, nullptr, &tex);
   assert(SUCCEEDED(hr));
   com_ptr<ID3D11RenderTargetView> rtv_edge_detection;
   hr = device->CreateRenderTargetView(tex.get(), nullptr, &rtv_edge_detection);
   assert(SUCCEEDED(hr));
   com_ptr<ID3D11ShaderResourceView> srv_edge_detection;
   hr = device->CreateShaderResourceView(tex.get(), nullptr, &srv_edge_detection);
   assert(SUCCEEDED(hr));

   // Bindings.
   device_context->OMSetBlendState(nullptr, nullptr, UINT_MAX);
   device_context->OMSetDepthStencilState(ds_disable_depth_replace_stencil.get(), 1);
   const std::array rtvs_edge_detection = { rtv_edge_detection.get() };
   device_context->OMSetRenderTargets(rtvs_edge_detection.size(), rtvs_edge_detection.data(), dsv.get());
   device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
   device_context->VSSetShader(device_data.native_vertex_shaders.at(Math::CompileTimeStringHash("SMAA Edge Detection VS")).get(), nullptr, 0);
   device_context->PSSetShader(device_data.native_pixel_shaders.at(Math::CompileTimeStringHash("SMAA Edge Detection PS")).get(), nullptr, 0);
   const std::array ps_samplers = { device_data.sampler_state_linear.get(), device_data.sampler_state_point.get() };
   device_context->PSSetSamplers(0, ps_samplers.size(), ps_samplers.data());
   const std::array ps_srvs_edge_detection = { srv_color_tex_gamma, srv_predication_tex };
   device_context->PSSetShaderResources(0, ps_srvs_edge_detection.size(), ps_srvs_edge_detection.data());
   device_context->RSSetViewports(1, &viewport);
   device_context->RSSetState(nullptr);

   static constexpr FLOAT clear_color[4] = {};
   device_context->ClearRenderTargetView(rtv_edge_detection.get(), clear_color);
   device_context->ClearDepthStencilView(dsv.get(), D3D10_CLEAR_STENCIL, 1.0f, 0);
   device_context->Draw(3, 0);

   //

   // BlendingWeightCalculation pass
   //

   // Create area texture.
   static com_ptr<ID3D11ShaderResourceView> srv_area_tex;
   [[unlikely]] if (!srv_area_tex)
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
      tex.reset();
      hr = device->CreateTexture2D(&tex_desc, &subresource_data, &tex);
      assert(SUCCEEDED(hr));
      hr = device->CreateShaderResourceView(tex.get(), nullptr, &srv_area_tex);
      assert(SUCCEEDED(hr));
   }

   // Create search texture.
   static com_ptr<ID3D11ShaderResourceView> srv_search_tex;
   [[unlikely]] if (!srv_search_tex)
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
      tex.reset();
      hr = device->CreateTexture2D(&tex_desc, &subresource_data, &tex);
      assert(SUCCEEDED(hr));
      hr = device->CreateShaderResourceView(tex.get(), nullptr, &srv_search_tex);
      assert(SUCCEEDED(hr));
   }

   // Create DS.
   static com_ptr<ID3D11DepthStencilState> ds_disable_depth_use_stencil;
   [[unlikely]] if (!ds_disable_depth_use_stencil)
   {
      CD3D11_DEPTH_STENCIL_DESC ds_desc(D3D11_DEFAULT);
      ds_desc.DepthEnable = FALSE;
      ds_desc.StencilEnable = TRUE;
      ds_desc.FrontFace.StencilFunc = D3D11_COMPARISON_EQUAL;
      hr = device->CreateDepthStencilState(&ds_desc, &ds_disable_depth_use_stencil);
      assert(SUCCEEDED(hr));
   }

   // Create RT and views.
   tex_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
   tex.reset();
   hr = device->CreateTexture2D(&tex_desc, nullptr, &tex);
   assert(SUCCEEDED(hr));
   com_ptr<ID3D11RenderTargetView> rtv_blending_weight_calculation;
   hr = device->CreateRenderTargetView(tex.get(), nullptr, &rtv_blending_weight_calculation);
   assert(SUCCEEDED(hr));
   com_ptr<ID3D11ShaderResourceView> srv_blending_weight_calculation;
   hr = device->CreateShaderResourceView(tex.get(), nullptr, &srv_blending_weight_calculation);
   assert(SUCCEEDED(hr));

   // Bindings.
   device_context->OMSetDepthStencilState(ds_disable_depth_use_stencil.get(), 1);
   const std::array rtvs_blending_weight_calculation = { rtv_blending_weight_calculation.get() };
   device_context->OMSetRenderTargets(rtvs_blending_weight_calculation.size(), rtvs_blending_weight_calculation.data(), dsv.get());
   device_context->VSSetShader(device_data.native_vertex_shaders.at(Math::CompileTimeStringHash("SMAA Blending Weight Calculation VS")).get(), nullptr, 0);
   device_context->PSSetShader(device_data.native_pixel_shaders.at(Math::CompileTimeStringHash("SMAA Blending Weight Calculation PS")).get(), nullptr, 0);
   const std::array ps_srvs_blending_weight_calculation = { srv_edge_detection.get(), srv_area_tex.get(), srv_search_tex.get() };
   device_context->PSSetShaderResources(0, ps_srvs_blending_weight_calculation.size(), ps_srvs_blending_weight_calculation.data());

   device_context->ClearRenderTargetView(rtv_blending_weight_calculation.get(), clear_color);
   device_context->Draw(3, 0);

   //

   // NeighborhoodBlending pass
   //

   // Bindings.
   device_context->OMSetRenderTargets(1, &rtv, nullptr);
   device_context->VSSetShader(device_data.native_vertex_shaders.at(Math::CompileTimeStringHash("SMAA Neighborhood Blending VS")).get(), nullptr, 0);
   device_context->PSSetShader(device_data.native_pixel_shaders.at(Math::CompileTimeStringHash("SMAA Neighborhood Blending PS")).get(), nullptr, 0);
   const std::array ps_srvs_neighborhood_blending = { srv_color_tex, srv_blending_weight_calculation.get() };
   device_context->PSSetShaderResources(0, ps_srvs_neighborhood_blending.size(), ps_srvs_neighborhood_blending.data());

   device_context->Draw(3, 0);

   //

   // Restore.
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