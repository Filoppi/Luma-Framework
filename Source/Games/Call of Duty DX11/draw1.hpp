#pragma once

namespace DrawDav
{
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
            }
            device_context->OMGetBlendState(&state->blend_state, state->blend_factor, &state->blend_sample_mask);
            device_context->IAGetPrimitiveTopology(&state->primitive_topology);
            device_context->RSGetScissorRects(&state->scissor_rects_num, nullptr); // This will get the number of scissor rects used
            device_context->RSGetScissorRects(&state->scissor_rects_num, &state->scissor_rects[0]);
            device_context->RSGetViewports(&state->viewports_num, nullptr); // This will get the number of viewports used
            device_context->RSGetViewports(&state->viewports_num, &state->viewports[0]);
            // device_context->PSGetShaderResources(0, srv_num, &state->shader_resource_views[0]);
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
            device_context->RSSetScissorRects(state->scissor_rects_num, &state->scissor_rects[0]);
            device_context->RSSetViewports(state->viewports_num, &state->viewports[0]);
            ID3D11ShaderResourceView* const* srvs_const = (ID3D11ShaderResourceView**)std::addressof(state->shader_resource_views[0]); // We can't use "com_ptr"'s "T **operator&()" as it asserts if the object isn't null, even if the reference would be const
            // device_context->PSSetShaderResources(0, srv_num, srvs_const);
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
         UINT valid_render_target_views_bound = 0; // Includes null ones as well if bound between valid RTVs
         UINT uav_num = D3D11_1_UAV_SLOT_COUNT;
      };

      // Store this on the heap instead of inlining it in the stack otherwise it'd allocate way too much
      std::unique_ptr<State> state;
   };
} // namespace DrawDav