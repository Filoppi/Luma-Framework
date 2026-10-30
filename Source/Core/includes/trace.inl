#pragma once

// Included by "core.hpp" after the capture state and the shader caches (see "trace.hpp").
namespace Trace
{
   namespace
   {
      // Entries recorded without the command list's trace at hand (Luma passes, game hooks), moved into it after Core's draw and present hooks
      // (a pass drawn outside of them is moved at the next hook on its thread, later than where it ran)
      thread_local std::vector<TraceDrawCallData> queued_entries;

      // Records the state bound for "stage", the slots "reflection" uses, or all the bound ones without it. Expects "device_data.mutex" (shared).
      void CaptureState(TraceDrawCallData& trace_draw_call_data, const DeviceData& device_data, ID3D11DeviceContext* native_device_context, reshade::api::shader_stage stage, const CachedShader* reflection, const DrawDispatchData& draw_dispatch_data)
      {
         trace_draw_call_data.command_list = native_device_context;

         // Query D3D11.1 context once for the whole function (used to retrieve CB partial-binding offsets)
         com_ptr<ID3D11DeviceContext1> device_context_1;
         native_device_context->QueryInterface(&device_context_1);

         // In case we redirected resource views (indirect texture upgrades), force print back the original one here,
         // the upgraded one will be visible in the lists anyway
         auto RedirectMirroredRVS = [&]<typename T>(T* ptr) -> T*
         {
            uint64_t handle = reinterpret_cast<uint64_t>(ptr);
            for (const auto& [key, value] : device_data.resource_upgrades.original_resource_views_to_mirrored_upgraded_resource_views)
            {
               if (value == handle)
                  return reinterpret_cast<T*>(key);
            }
            return ptr;
         };
         auto FlagUpgradedResources = [&](auto* rv, TraceDrawCallData::DepthStateType depth_state = TraceDrawCallData::DepthStateType::Invalid, TraceDrawCallData::DepthStateType stencil_state = TraceDrawCallData::DepthStateType::Invalid)
         {
            com_ptr<ID3D11Resource> resource;
            if (rv)
            {
               rv->GetResource(&resource);

               const bool upgraded = device_data.resource_upgrades.original_resources_to_mirrored_upgraded_resources.contains((uint64_t)resource.get()) || device_data.resource_upgrades.upgraded_resources.contains((uint64_t)resource.get()) || (/*swapchain_upgrade_type > SwapchainUpgradeType::None &&*/ device_data.back_buffers.contains((uint64_t)resource.get())); // TODO: expose "swapchain_upgrade_type" here or something like that
               const bool scaled = [&]() {
                  auto it = device_data.resource_upgrades.original_resources_to_mirrored_upgraded_resources.find((uint64_t)resource.get());
                  return it != device_data.resource_upgrades.original_resources_to_mirrored_upgraded_resources.end() && it->second.is_scaled;
               }();

               using ViewType = std::remove_pointer_t<decltype(rv)>;
               if constexpr (std::is_same_v<ViewType, ID3D11ShaderResourceView>)
               {
                  trace_draw_call_data.any_input_resources_format_upgraded |= upgraded;
                  trace_draw_call_data.any_input_resources_scaled |= scaled;
               }
               else if constexpr (std::is_same_v<ViewType, ID3D11RenderTargetView>)
               {
                  trace_draw_call_data.any_output_resources_format_upgraded |= upgraded;
                  trace_draw_call_data.any_output_resources_scaled |= scaled;
               }
               else if constexpr (std::is_same_v<ViewType, ID3D11UnorderedAccessView>)
               {
                  trace_draw_call_data.any_input_resources_format_upgraded |= upgraded;
                  trace_draw_call_data.any_output_resources_format_upgraded |= upgraded;
                  trace_draw_call_data.any_input_resources_scaled |= scaled;
                  trace_draw_call_data.any_output_resources_scaled |= scaled;
               }
               else if constexpr (std::is_same_v<ViewType, ID3D11DepthStencilView>)
               {
                  if (depth_state == TraceDrawCallData::DepthStateType::TestOnly ||
                      depth_state == TraceDrawCallData::DepthStateType::TestAndWrite ||
                      stencil_state == TraceDrawCallData::DepthStateType::TestOnly ||
                      stencil_state == TraceDrawCallData::DepthStateType::TestAndWrite)
                  {
                     trace_draw_call_data.any_input_resources_format_upgraded |= upgraded;
                     trace_draw_call_data.any_input_resources_scaled |= scaled;
                  }
                  if (depth_state == TraceDrawCallData::DepthStateType::WriteOnly ||
                     depth_state == TraceDrawCallData::DepthStateType::TestAndWrite ||
                     stencil_state == TraceDrawCallData::DepthStateType::WriteOnly ||
                     stencil_state == TraceDrawCallData::DepthStateType::TestAndWrite)
                  {
                     trace_draw_call_data.any_output_resources_format_upgraded |= upgraded;
                     trace_draw_call_data.any_output_resources_scaled |= scaled;
                  }
               }
            }
         };

         // Expose if needed
         constexpr bool show_unused_bound_resources = false; // If a resource is bound but not read/written to by the shader (this often happens with SRVs and UAVs, rarely with RTVs, given they almost 1:1 match with the shader)
         constexpr bool show_used_unbound_resources = true; // If a resource is not bound (null) but it is read/written to by the shader (which will result in black on read, and nothing on writes)
         // Note that in DX11, some resources might be inherited by the main device context when merging an async device context to the main one, and we couldn't see them yet. Most games don't seem to rely on that.

         // Without reflections (Luma's own shaders), every bound slot is recorded
         static const CachedShader no_reflection = {};
         const CachedShader* cached_shader = reflection ? reflection : &no_reflection;
         const bool show_unused_bound = show_unused_bound_resources || !reflection;
         if (stage == reshade::api::shader_stage::pixel)
         {
            // The first call gets the number of bound rects/viewports
            native_device_context->RSGetScissorRects(&trace_draw_call_data.scissor_count, nullptr);
            native_device_context->RSGetScissorRects(&trace_draw_call_data.scissor_count, trace_draw_call_data.scissor_rects);

            native_device_context->RSGetViewports(&trace_draw_call_data.viewport_count, nullptr);
            native_device_context->RSGetViewports(&trace_draw_call_data.viewport_count, trace_draw_call_data.viewports);

            com_ptr<ID3D11RasterizerState> rasterizer_state;
            native_device_context->RSGetState(&rasterizer_state);
            trace_draw_call_data.has_rasterizer_state = rasterizer_state.get() != nullptr;
            if (rasterizer_state)
            {
               rasterizer_state->GetDesc(&trace_draw_call_data.rasterizer_desc);
            }

            com_ptr<ID3D11RenderTargetView> rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
            com_ptr<ID3D11UnorderedAccessView> uavs[D3D11_1_UAV_SLOT_COUNT];
            com_ptr<ID3D11DepthStencilView> dsv;
            native_device_context->OMGetRenderTargetsAndUnorderedAccessViews(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], &dsv, 0, device_data.uav_max_count, &uavs[0]);
            dsv = RedirectMirroredRVS(dsv.get());
         trace_draw_call_data.dsv = dsv.get();

            com_ptr<ID3D11DepthStencilState> depth_stencil_state;
            UINT stencil_ref;
            native_device_context->OMGetDepthStencilState(&depth_stencil_state, &stencil_ref);
            trace_draw_call_data.stencil_ref = stencil_ref;
            if (depth_stencil_state)
            {
               D3D11_DEPTH_STENCIL_DESC depth_stencil_desc;
               depth_stencil_state->GetDesc(&depth_stencil_desc);
               trace_draw_call_data.has_depth_stencil_state = true;
               trace_draw_call_data.depth_stencil_desc = depth_stencil_desc;

               D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
               if (dsv.get())
               {
                  dsv->GetDesc(&dsv_desc);
               }

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

               bool has_valid_stencil_dsv = dsv.get() && (dsv_desc.Format == DXGI_FORMAT_D24_UNORM_S8_UINT || dsv_desc.Format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT);
               bool any_stencil_pass_op_writes = depth_stencil_desc.FrontFace.StencilFailOp != D3D11_STENCIL_OP_KEEP || depth_stencil_desc.FrontFace.StencilDepthFailOp != D3D11_STENCIL_OP_KEEP || depth_stencil_desc.FrontFace.StencilPassOp != D3D11_STENCIL_OP_KEEP || depth_stencil_desc.BackFace.StencilFailOp != D3D11_STENCIL_OP_KEEP || depth_stencil_desc.BackFace.StencilDepthFailOp != D3D11_STENCIL_OP_KEEP || depth_stencil_desc.BackFace.StencilPassOp != D3D11_STENCIL_OP_KEEP; // Note: "D3D11_STENCIL_OP_KEEP" isn't 0, so it's possitive that 0 is also treated as "NOP"
               bool any_stencil_func_tests = depth_stencil_desc.FrontFace.StencilFunc != D3D11_COMPARISON_ALWAYS || depth_stencil_desc.BackFace.StencilFunc != D3D11_COMPARISON_ALWAYS;
         
               bool stencil_enabled_write = depth_stencil_desc.StencilEnable && has_valid_stencil_dsv && depth_stencil_desc.StencilWriteMask != 0 && any_stencil_pass_op_writes;
               bool stencil_enabled_read = depth_stencil_desc.StencilEnable && has_valid_stencil_dsv && depth_stencil_desc.StencilReadMask != 0 && any_stencil_func_tests; // "stencil_ref" doesn't really tell us anything regarding read/write
               if (stencil_enabled_read && stencil_enabled_write)
               {
                  trace_draw_call_data.stencil_state = TraceDrawCallData::DepthStateType::TestAndWrite;
               }
               else if (stencil_enabled_read)
               {
                  trace_draw_call_data.stencil_state = TraceDrawCallData::DepthStateType::TestOnly;
               }
               else if (stencil_enabled_write)
               {
                  trace_draw_call_data.stencil_state = TraceDrawCallData::DepthStateType::WriteOnly;
               }

               if ((trace_draw_call_data.depth_state != TraceDrawCallData::DepthStateType::Disabled && trace_draw_call_data.depth_state != TraceDrawCallData::DepthStateType::Invalid)
                  || (trace_draw_call_data.stencil_state != TraceDrawCallData::DepthStateType::Disabled && trace_draw_call_data.stencil_state != TraceDrawCallData::DepthStateType::Invalid))
               {
                  trace_draw_call_data.dsv_format = dsv_desc.Format;
                  ASSERT_ONCE(dsv_desc.Format != DXGI_FORMAT_UNKNOWN); // Unexpected?

                  FlagUpgradedResources(dsv.get(), trace_draw_call_data.depth_state, trace_draw_call_data.stencil_state);

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
               com_ptr<ID3D11BlendState1> blend_state_1;
               blend_state->QueryInterface(&blend_state_1);
               // We always cache the last one used by the pipeline, hopefully it didn't change between draw calls
               D3D11_BLEND_DESC1 blend_desc_1 = {};
               if (blend_state_1)
               {
                  blend_state_1->GetDesc1(&blend_desc_1);
               }
               else
               {
                  D3D11_BLEND_DESC blend_desc;
                  blend_state->GetDesc(&blend_desc);
                  blend_desc_1.AlphaToCoverageEnable = blend_desc.AlphaToCoverageEnable;
                  blend_desc_1.IndependentBlendEnable = blend_desc.IndependentBlendEnable;
                  for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
                  {
                     // The other parameters stay defaulted
                     blend_desc_1.RenderTarget[i].BlendEnable           = blend_desc.RenderTarget[i].BlendEnable;
                     blend_desc_1.RenderTarget[i].SrcBlend              = blend_desc.RenderTarget[i].SrcBlend;
                     blend_desc_1.RenderTarget[i].DestBlend             = blend_desc.RenderTarget[i].DestBlend;
                     blend_desc_1.RenderTarget[i].BlendOp               = blend_desc.RenderTarget[i].BlendOp;
                     blend_desc_1.RenderTarget[i].SrcBlendAlpha         = blend_desc.RenderTarget[i].SrcBlendAlpha;
                     blend_desc_1.RenderTarget[i].DestBlendAlpha        = blend_desc.RenderTarget[i].DestBlendAlpha;
                     blend_desc_1.RenderTarget[i].BlendOpAlpha          = blend_desc.RenderTarget[i].BlendOpAlpha;
                     blend_desc_1.RenderTarget[i].RenderTargetWriteMask = blend_desc.RenderTarget[i].RenderTargetWriteMask;
                  }
               }
               trace_draw_call_data.blend_desc = blend_desc_1;
               // We don't care for the alpha blend operation (source alpha * dest alpha) as alpha is never read back from destination
            }

            for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT && i < TraceDrawCallData::rtvs_size; i++)
            {
               rtvs[i] = RedirectMirroredRVS(rtvs[i].get());
               trace_draw_call_data.rtvs[i] = rtvs[i].get();
               if ((rtvs[i] != nullptr || (show_used_unbound_resources && cached_shader->rtvs[i])) && (show_unused_bound || cached_shader->rtvs[i]))
               {
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
                  FlagUpgradedResources(rtvs[i].get());
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
               trace_draw_call_data.stencil_state = TraceDrawCallData::DepthStateType::Disabled;
            }
            for (UINT i = 0; i < device_data.uav_max_count && i < TraceDrawCallData::uavs_size; i++)
            {
               uavs[i] = RedirectMirroredRVS(uavs[i].get());
               trace_draw_call_data.uavs[i] = uavs[i].get();
               if ((uavs[i] != nullptr || (show_used_unbound_resources && cached_shader->uavs[i])) && (show_unused_bound || cached_shader->uavs[i]))
               {
                  D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
                  uav_desc.Format = DXGI_FORMAT(-1);
                  if (uavs[i]) uavs[i]->GetDesc(&uav_desc);
                  trace_draw_call_data.uav_format[i] = uav_desc.Format;

                  FlagUpgradedResources(uavs[i].get());
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
               srvs[i] = RedirectMirroredRVS(srvs[i].get());
               trace_draw_call_data.srvs[i] = srvs[i].get();
               if ((srvs[i] != nullptr || (show_used_unbound_resources && cached_shader->srvs[i])) && (show_unused_bound || cached_shader->srvs[i]))
               {
                  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
                  srv_desc.Format = DXGI_FORMAT(-1);
                  if (srvs[i]) srvs[i]->GetDesc(&srv_desc);
                  trace_draw_call_data.srv_format[i] = srv_desc.Format;

                  FlagUpgradedResources(srvs[i].get());
                  GetResourceInfo(srvs[i].get(), trace_draw_call_data.sr_size[i], trace_draw_call_data.sr_format[i], &trace_draw_call_data.sr_type_name[i], &trace_draw_call_data.sr_hash[i], &trace_draw_call_data.sr_debug_name[i], &trace_draw_call_data.sr_is_rt[i], &trace_draw_call_data.sr_is_ua[i]);

                  trace_draw_call_data.srv_mip[i] = GetSRVMipLevel(srv_desc);
                  uint3 base_size = uint3{ trace_draw_call_data.sr_size[i].x, trace_draw_call_data.sr_size[i].y, trace_draw_call_data.sr_size[i].z };
                  trace_draw_call_data.srv_size[i] = GetTextureMipSize(base_size, trace_draw_call_data.srv_mip[i]);
               }
            }

            com_ptr<ID3D11Buffer> cbs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
            UINT cbs_first_constant[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
            UINT cbs_num_constants[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
            if (device_context_1)
               device_context_1->PSGetConstantBuffers1(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, &cbs[0], cbs_first_constant, cbs_num_constants);
            else
               native_device_context->PSGetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, &cbs[0]);
            for (UINT i = 0; i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; i++)
            {
               trace_draw_call_data.cb_buffers[i] = cbs[i].get();
               if ((cbs[i] != nullptr || (show_used_unbound_resources && cached_shader->cbs[i])) && (show_unused_bound || cached_shader->cbs[i]))
               {
                  trace_draw_call_data.cbs[i] = true;
                  trace_draw_call_data.cb_hash[i] = std::to_string(std::hash<void*>{}(cbs[i].get()));
                  trace_draw_call_data.cb_first_constant[i] = cbs_first_constant[i];
                  trace_draw_call_data.cb_num_constants[i] = cbs_num_constants[i];
               }
            }

            com_ptr<ID3D11SamplerState> sampler_states[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT];
            native_device_context->PSGetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, &sampler_states[0]);
            for (UINT i = 0; i < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT; i++)
            {
               trace_draw_call_data.sampler_states[i] = sampler_states[i].get();
               if ((sampler_states[i] != nullptr || (show_used_unbound_resources && cached_shader->samplers[i])) && (show_unused_bound || cached_shader->samplers[i]))
               {
                  D3D11_SAMPLER_DESC desc = {};
                  desc.Filter = D3D11_FILTER(-1); // TODO: walk back to the source sampler instead of the one luma possibly replaced?
                  if (sampler_states[i]) sampler_states[i]->GetDesc(&desc);
                  trace_draw_call_data.samplers_filter[i] = desc.Filter;
                  trace_draw_call_data.samplers_address_u[i] = desc.AddressU;
                  trace_draw_call_data.samplers_address_v[i] = desc.AddressV;
                  trace_draw_call_data.samplers_address_w[i] = desc.AddressW;
                  trace_draw_call_data.samplers_mip_lod_bias[i] = desc.MipLODBias;
               }
            }
         }
         else if (stage == reshade::api::shader_stage::vertex)
         {
            com_ptr<ID3D11ShaderResourceView> srvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
            native_device_context->VSGetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, &srvs[0]);
            for (UINT i = 0; i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT && i < TraceDrawCallData::srvs_size; i++)
            {
               srvs[i] = RedirectMirroredRVS(srvs[i].get());
               trace_draw_call_data.srvs[i] = srvs[i].get();
               if ((srvs[i] != nullptr || (show_used_unbound_resources && cached_shader->srvs[i])) && (show_unused_bound || cached_shader->srvs[i]))
               {
                  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
                  srv_desc.Format = DXGI_FORMAT(-1);
                  if (srvs[i]) srvs[i]->GetDesc(&srv_desc);
                  trace_draw_call_data.srv_format[i] = srv_desc.Format;

                  FlagUpgradedResources(srvs[i].get());
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
            if (device_data.input_layouts_descs.contains(input_layout.get()))
            {
               const auto& input_elements_descs = device_data.input_layouts_descs.find(input_layout.get());
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
            UINT cbs_first_constant[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
            UINT cbs_num_constants[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
            if (device_context_1)
               device_context_1->VSGetConstantBuffers1(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, &cbs[0], cbs_first_constant, cbs_num_constants);
            else
               native_device_context->VSGetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, &cbs[0]);
            for (UINT i = 0; i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; i++)
            {
               trace_draw_call_data.cb_buffers[i] = cbs[i].get();
               if ((cbs[i] != nullptr || (show_used_unbound_resources && cached_shader->cbs[i])) && (show_unused_bound || cached_shader->cbs[i]))
               {
                  trace_draw_call_data.cbs[i] = true;
                  trace_draw_call_data.cb_hash[i] = std::to_string(std::hash<void*>{}(cbs[i].get()));
                  trace_draw_call_data.cb_first_constant[i] = cbs_first_constant[i];
                  trace_draw_call_data.cb_num_constants[i] = cbs_num_constants[i];
               }
            }

            com_ptr<ID3D11SamplerState> sampler_states[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT];
            native_device_context->VSGetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, &sampler_states[0]);
            for (UINT i = 0; i < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT; i++)
            {
               trace_draw_call_data.sampler_states[i] = sampler_states[i].get();
               if ((sampler_states[i] != nullptr || (show_used_unbound_resources && cached_shader->samplers[i])) && (show_unused_bound || cached_shader->samplers[i]))
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
         else if (stage == reshade::api::shader_stage::compute)
         {
            com_ptr<ID3D11ShaderResourceView> srvs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
            native_device_context->CSGetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, &srvs[0]); // TODO: use the CSGetShaderResources1 version (all around) which also has the plane desc
            for (UINT i = 0; i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT && i < TraceDrawCallData::srvs_size; i++)
            {
               srvs[i] = RedirectMirroredRVS(srvs[i].get());
               trace_draw_call_data.srvs[i] = srvs[i].get();
               if ((srvs[i] != nullptr || (show_used_unbound_resources && cached_shader->srvs[i])) && (show_unused_bound || cached_shader->srvs[i]))
               {
                  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
                  srv_desc.Format = DXGI_FORMAT(-1);
                  if (srvs[i]) srvs[i]->GetDesc(&srv_desc);
                  trace_draw_call_data.srv_format[i] = srv_desc.Format;

                  FlagUpgradedResources(srvs[i].get());
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
               uavs[i] = RedirectMirroredRVS(uavs[i].get());
               trace_draw_call_data.uavs[i] = uavs[i].get();
               if ((uavs[i] != nullptr || (show_used_unbound_resources && cached_shader->uavs[i])) && (show_unused_bound || cached_shader->uavs[i]))
               {
                  D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
                  uav_desc.Format = DXGI_FORMAT(-1);
                  if (uavs[i]) uavs[i]->GetDesc(&uav_desc);
                  trace_draw_call_data.uav_format[i] = uav_desc.Format;

                  FlagUpgradedResources(uavs[i].get());
                  GetResourceInfo(uavs[i].get(), trace_draw_call_data.ua_size[i], trace_draw_call_data.ua_format[i], &trace_draw_call_data.ua_type_name[i], &trace_draw_call_data.ua_hash[i], &trace_draw_call_data.ua_debug_name[i], &trace_draw_call_data.ua_is_rt[i]);

                  trace_draw_call_data.uav_mip[i] = GetUAVMipLevel(uav_desc);
                  uint3 base_size = uint3{ trace_draw_call_data.ua_size[i].x, trace_draw_call_data.ua_size[i].y, trace_draw_call_data.ua_size[i].z };
                  trace_draw_call_data.uav_size[i] = GetTextureMipSize(base_size, trace_draw_call_data.uav_mip[i]);
               }
            }

            trace_draw_call_data.draw_dispatch_data = draw_dispatch_data;

            com_ptr<ID3D11Buffer> cbs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
            UINT cbs_first_constant[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
            UINT cbs_num_constants[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
            if (device_context_1)
               device_context_1->CSGetConstantBuffers1(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, &cbs[0], cbs_first_constant, cbs_num_constants);
            else
               native_device_context->CSGetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, &cbs[0]);
            for (UINT i = 0; i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; i++)
            {
               trace_draw_call_data.cb_buffers[i] = cbs[i].get();
               if ((cbs[i] != nullptr || (show_used_unbound_resources && cached_shader->cbs[i])) && (show_unused_bound || cached_shader->cbs[i]))
               {
                  trace_draw_call_data.cbs[i] = true;
                  trace_draw_call_data.cb_hash[i] = std::to_string(std::hash<void*>{}(cbs[i].get()));
                  trace_draw_call_data.cb_first_constant[i] = cbs_first_constant[i];
                  trace_draw_call_data.cb_num_constants[i] = cbs_num_constants[i];
               }
            }

            com_ptr<ID3D11SamplerState> sampler_states[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT];
            native_device_context->CSGetSamplers(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, &sampler_states[0]);
            for (UINT i = 0; i < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT; i++)
            {
               trace_draw_call_data.sampler_states[i] = sampler_states[i].get();
               if ((sampler_states[i] != nullptr || (show_used_unbound_resources && cached_shader->samplers[i])) && (show_unused_bound || cached_shader->samplers[i]))
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

      // Needs "s_mutex_generic" (shared). Returns the shader stage whose state a game draw's entry records, and its reflections.
      reshade::api::shader_stage FindStage(const DeviceData& device_data, uint64_t pipeline_handle, const CachedShader** cached_shader)
      {
         *cached_shader = nullptr;
         // Note that the pipelines can be run more than once so this will return the first one matching (there's only one actually, we don't have separate settings for their running instance, as that's runtime stuff)
         const auto pipeline_pair = device_data.pipeline_cache_by_pipeline_handle.find(pipeline_handle);
         if (pipeline_pair == device_data.pipeline_cache_by_pipeline_handle.end() || pipeline_pair->second == nullptr)
            return reshade::api::shader_stage(0);
         const CachedPipeline* pipeline = pipeline_pair->second;
         if (const auto shader = !pipeline->shader_hashes.empty() ? shader_cache.find(pipeline->shader_hashes[0]) : shader_cache.end(); shader != shader_cache.end()) // DX10/11 exclusive behaviour
            *cached_shader = shader->second;
         assert(*cached_shader);
         if (pipeline->HasPixelShader())
            return reshade::api::shader_stage::pixel;
         if (pipeline->HasVertexShader())
            return reshade::api::shader_stage::vertex;
         if (pipeline->HasComputeShader())
            return reshade::api::shader_stage::compute;
         return reshade::api::shader_stage(0);
      }

      // Luma passes don't have their command list's data at hand
      DeviceData* FindDeviceData(ID3D11DeviceContext* native_device_context)
      {
         com_ptr<ID3D11Device> native_device;
         native_device_context->GetDevice(&native_device);
         const std::shared_lock lock(s_mutex_device);
         const auto device_data = std::ranges::find(global_devices_data, native_device.get(), &DeviceData::native_device);
         return device_data != global_devices_data.end() ? *device_data : nullptr;
      }

      // Needs "device_data.mutex" (shared). The "native_shaders_definitions" key of the bound pixel (or compute) shader, 0 if it isn't one of Luma's.
      uint32_t FindNativeShaderHash(const DeviceData& device_data, ID3D11DeviceContext* native_device_context, reshade::api::shader_stage stage)
      {
         const auto find = [](const auto& native_shaders, const auto* shader) -> uint32_t
         {
            const auto it = std::ranges::find_if(native_shaders, [shader](const auto& native_shader)
               { return native_shader.second.get() == shader; });
            return it != native_shaders.end() ? it->first : 0;
         };
         if (stage == reshade::api::shader_stage::compute)
         {
            com_ptr<ID3D11ComputeShader> shader;
            native_device_context->CSGetShader(&shader, nullptr, nullptr);
            return shader ? find(device_data.native_compute_shaders, shader.get()) : 0;
         }
         com_ptr<ID3D11PixelShader> shader;
         native_device_context->PSGetShader(&shader, nullptr, nullptr);
         return shader ? find(device_data.native_pixel_shaders, shader.get()) : 0;
      }

      struct NativeShaderReflection
      {
         const void* code = nullptr; // Read again when the shader is recompiled
         bool found = false;
         CachedShader reflection;
      };
      std::mutex s_mutex_native_reflections;
      std::unordered_map<uint32_t, NativeShaderReflection> native_reflections; // By "native_shaders_definitions" key

      // The slots a Luma shader uses, read like the ones of game shaders ("ReflectShaderSlots()"). False if it can't be reflected.
      bool FindNativeShaderReflection(uint32_t native_shader_hash, CachedShader& reflection)
      {
         const std::shared_lock lock_loading(s_mutex_loading);
         const auto custom_shader = custom_shaders_cache.find(Shader::ShiftHash32ToHash64(native_shader_hash));
         if (custom_shader == custom_shaders_cache.end() || !custom_shader->second || custom_shader->second->code.empty() || !Shader::d3d_reflect)
            return false;
         const std::lock_guard lock(s_mutex_native_reflections);
         NativeShaderReflection& native_reflection = native_reflections[native_shader_hash];
         const auto& code = custom_shader->second->code;
         if (native_reflection.code != code.data())
         {
            native_reflection = {code.data()};
            com_ptr<ID3D11ShaderReflection> shader_reflector;
            D3D11_SHADER_DESC shader_desc;
            if (SUCCEEDED(Shader::d3d_reflect(code.data(), code.size(), IID_ID3D11ShaderReflection, (void**)&shader_reflector)) && SUCCEEDED(shader_reflector->GetDesc(&shader_desc)))
            {
               bool found_any_rtvs = false;
               bool found_any_other_bindings = false;
               ReflectShaderSlots(shader_reflector.get(), shader_desc, native_reflection.reflection, found_any_rtvs, found_any_other_bindings);
               native_reflection.found = true;
            }
         }
         if (native_reflection.found)
            reflection = native_reflection.reflection;
         return native_reflection.found;
      }

      void RecordCustomPass(ID3D11DeviceContext* native_device_context, const char* name, reshade::api::shader_stage stage, const DrawDispatchData& draw_dispatch_data)
      {
         if (debug_draw_custom_pass == 0 && !trace_running)
            return;
         DeviceData* device_data = FindDeviceData(native_device_context);
         if (!device_data)
            return;
         uint32_t shader_hash;
         {
            const std::shared_lock lock_device(device_data->mutex);
            shader_hash = FindNativeShaderHash(*device_data, native_device_context, stage);
         }
         // Unnamed passes go by their shader's name
         if (!name)
         {
            const auto definition = native_shaders_definitions.find(shader_hash);
            name = definition != native_shaders_definitions.end() && definition->second.file_name ? definition->second.file_name : "Custom Draw";
         }

         // The pass drew right before, its state is still bound
         if (debug_draw_custom_pass != 0 && debug_draw_custom_pass == Math::CompileTimeStringHash(name) && (debug_draw_pipeline_target_thread == std::thread::id() || debug_draw_pipeline_target_thread == std::this_thread::get_id()))
         {
            const int32_t instance = debug_draw_custom_pass_instance.fetch_add(1);
            if (debug_draw_pipeline_target_instance == -1 || instance == debug_draw_pipeline_target_instance)
               CopyDebugDrawTexture(debug_draw_mode, debug_draw_view_index, native_device_context, *device_data, stage == reshade::api::shader_stage::compute);
         }
         if (!trace_running)
            return;
         TraceDrawCallData trace_draw_call_data;
         trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
         trace_draw_call_data.custom_name = name;
         trace_draw_call_data.custom_stage = stage;
         trace_draw_call_data.custom_shader_hash = shader_hash;
         CachedShader reflection;
         const bool has_reflection = shader_hash != 0 && FindNativeShaderReflection(shader_hash, reflection);
         {
            const std::shared_lock lock_device(device_data->mutex);
            CaptureState(trace_draw_call_data, *device_data, native_device_context, stage, has_reflection ? &reflection : nullptr, draw_dispatch_data);
         }
         // The pixel shader state doesn't include the draw arguments
         trace_draw_call_data.draw_dispatch_data = draw_dispatch_data;
         queued_entries.push_back(std::move(trace_draw_call_data));
      }
   }

   void ReflectShaderSlots(ID3D11ShaderReflection* shader_reflector, const D3D11_SHADER_DESC& shader_desc, CachedShader& cached_shader, bool& found_any_rtvs, bool& found_any_other_bindings)
   {
      HRESULT hr;
      // RTVs
      for (UINT i = 0; i < shader_desc.OutputParameters; ++i)
      {
         D3D11_SIGNATURE_PARAMETER_DESC shader_output_desc;
         hr = shader_reflector->GetOutputParameterDesc(i, &shader_output_desc);
         if (SUCCEEDED(hr) && strcmp(shader_output_desc.SemanticName, "SV_Target") == 0)
         {
            ASSERT_ONCE(shader_output_desc.SemanticIndex == shader_output_desc.Register && shader_output_desc.SemanticIndex < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT);
            cached_shader.rtvs[shader_output_desc.SemanticIndex] = true;
            found_any_rtvs = true;
         }
      }

      // SRVs and UAVs and samplers (it works for compute shaders too)
      for (UINT i = 0; i < shader_desc.BoundResources; ++i)
      {
         D3D11_SHADER_INPUT_BIND_DESC bind_desc;
         hr = shader_reflector->GetResourceBindingDesc(i, &bind_desc);
         if (SUCCEEDED(hr))
         {
            for (UINT j = 0; j < bind_desc.BindCount; ++j)
            {
               if (bind_desc.Type == D3D_SIT_TEXTURE)
               {
                  ASSERT_ONCE(bind_desc.BindPoint + j < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT);
                  cached_shader.srvs[bind_desc.BindPoint + j] = true;
                  found_any_other_bindings = true;
               }
               else if (bind_desc.Type == D3D_SIT_UAV_RWTYPED)
               {
                  ASSERT_ONCE(bind_desc.BindPoint + j < D3D11_1_UAV_SLOT_COUNT);
                  cached_shader.uavs[bind_desc.BindPoint + j] = true;
                  found_any_other_bindings = true;
               }
               // Verify that the cbuffer index and bind points match (they should always do)
               else if (bind_desc.Type == D3D_SIT_CBUFFER)
               {
                  ASSERT_ONCE(bind_desc.BindPoint + j < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT);
                  //ASSERT_ONCE(cached_shader.cbs[bind_desc.BindPoint + j]);
                  cached_shader.cbs[bind_desc.BindPoint + j] = true;
               }
               // These are always valid, even when the other ones aren't, so don't set "found_any_other_bindings" to true
               else if (bind_desc.Type == D3D_SIT_SAMPLER)
               {
                  ASSERT_ONCE(bind_desc.BindPoint + j < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT);
                  cached_shader.samplers[bind_desc.BindPoint + j] = true;
               }
            }
         }
      }
   }

   void Draw(ID3D11DeviceContext* native_device_context, const char* name, UINT vertex_count)
   {
      DrawDispatchData draw_dispatch_data;
      draw_dispatch_data.vertex_count = vertex_count;
      draw_dispatch_data.instance_count = 1;
      RecordCustomPass(native_device_context, name, reshade::api::shader_stage::pixel, draw_dispatch_data);
   }

   void Dispatch(ID3D11DeviceContext* native_device_context, const char* name, UINT thread_group_count_x, UINT thread_group_count_y, UINT thread_group_count_z)
   {
      DrawDispatchData draw_dispatch_data;
      draw_dispatch_data.dispatch_count = {thread_group_count_x, thread_group_count_y, thread_group_count_z};
      RecordCustomPass(native_device_context, name, reshade::api::shader_stage::compute, draw_dispatch_data);
   }

   void Marker(ID3D11DeviceContext* native_device_context, const char* name, ID3D11DeviceChild* target)
   {
      if (!trace_running)
         return;
      TraceDrawCallData trace_draw_call_data;
      trace_draw_call_data.type = TraceDrawCallData::TraceDrawCallType::Custom;
      trace_draw_call_data.command_list = native_device_context;
      trace_draw_call_data.custom_name = name;
      // Re-use the RTV data for simplicity (this is hardcoded to be read by "TraceDrawCallData::TraceDrawCallType::Custom" in imgui)
      if (com_ptr<ID3D11View> view; target && SUCCEEDED(target->QueryInterface(&view)))
      {
         GetResourceInfo(view.get(), trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);
      }
      else if (com_ptr<ID3D11Resource> resource; target && SUCCEEDED(target->QueryInterface(&resource)))
      {
         GetResourceInfo(resource.get(), trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);
      }
      queued_entries.push_back(std::move(trace_draw_call_data));
   }

   void Annotate(CommandListData& cmd_list_data, std::string_view text, std::optional<int64_t> value)
   {
      if (!trace_running) // Cheap early out for the hot path, rechecked under the lock
         return;
      const std::shared_lock lock_trace(s_mutex_trace);
      if (!trace_running)
         return;
      const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
      // Games can push their own custom entries after the shader ones
      for (auto it = cmd_list_data.trace_draw_calls_data.rbegin(); it != cmd_list_data.trace_draw_calls_data.rend(); ++it)
      {
         if (it->type != TraceDrawCallData::TraceDrawCallType::Shader)
            continue;
         if (!it->note.empty())
            it->note += ' ';
         it->note += text;
         if (value)
            std::format_to(std::back_inserter(it->note), "={}", *value);
         return;
      }
   }

   void UpdateDraw(CommandListData& cmd_list_data, DeviceData& device_data, ID3D11DeviceContext* native_device_context)
   {
      if (!trace_running) // Cheap early out for the hot path, rechecked under the lock
         return;
      const std::shared_lock lock_trace(s_mutex_trace);
      if (!trace_running)
         return;
      const std::shared_lock lock_generic(s_mutex_generic);
      const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
      const std::lock_guard<std::recursive_mutex> lock_dumping(s_mutex_dumping);
      const std::shared_lock lock_device(device_data.mutex);
      const auto [first, count] = cmd_list_data.trace_current_draw;
      for (size_t i = first; i < first + count && i < cmd_list_data.trace_draw_calls_data.size(); i++)
      {
         TraceDrawCallData& entry = cmd_list_data.trace_draw_calls_data[i];
         if (entry.type != TraceDrawCallData::TraceDrawCallType::Shader)
            continue;
         // Every bound slot, the replacement shader can read slots the original one didn't
         const CachedShader* cached_shader;
         auto current = std::make_shared<TraceDrawCallData>();
         CaptureState(*current, device_data, native_device_context, FindStage(device_data, entry.pipeline_handle, &cached_shader), nullptr, entry.draw_dispatch_data);

         entry.replaced_slots.clear();
         const auto compare = [&](TraceDrawCallData::SlotKind kind, const auto* original, const auto* replaced, size_t size)
         {
            for (size_t slot = 0; slot < size; slot++)
            {
               if (original[slot] == replaced[slot])
                  continue;
               const auto change = !original[slot] ? TraceDrawCallData::SlotChange::Added : (!replaced[slot] ? TraceDrawCallData::SlotChange::Unbound : TraceDrawCallData::SlotChange::Replaced);
               entry.replaced_slots.push_back({kind, uint32_t(slot), change});
            }
         };
         compare(TraceDrawCallData::SlotKind::RTV, entry.rtvs, current->rtvs, TraceDrawCallData::rtvs_size);
         compare(TraceDrawCallData::SlotKind::SRV, entry.srvs, current->srvs, TraceDrawCallData::srvs_size);
         compare(TraceDrawCallData::SlotKind::UAV, entry.uavs, current->uavs, TraceDrawCallData::uavs_size);
         compare(TraceDrawCallData::SlotKind::DSV, &entry.dsv, &current->dsv, 1);
         compare(TraceDrawCallData::SlotKind::CB, entry.cb_buffers, current->cb_buffers, std::size(entry.cb_buffers));
         compare(TraceDrawCallData::SlotKind::Sampler, entry.sampler_states, current->sampler_states, std::size(entry.sampler_states));
         entry.replaced_state = entry.replaced_slots.empty() ? nullptr : std::move(current);
      }
   }

   void AddDrawCall(std::vector<TraceDrawCallData>& trace_draw_calls_data, const DeviceData& device_data, ID3D11DeviceContext* native_device_context, uint64_t pipeline_handle, const DrawDispatchData& draw_dispatch_data)
   {
      TraceDrawCallData trace_draw_call_data;
      trace_draw_call_data.pipeline_handle = pipeline_handle;
      const CachedShader* cached_shader;
      const reshade::api::shader_stage stage = FindStage(device_data, pipeline_handle, &cached_shader);
      CaptureState(trace_draw_call_data, device_data, native_device_context, stage, cached_shader, draw_dispatch_data);

      if (trace_draw_calls_data.capacity() - trace_draw_calls_data.size() <= 1)
         trace_draw_calls_data.reserve(trace_draw_calls_data.size() + 1000); // Possible optimization
      trace_draw_calls_data.push_back(std::move(trace_draw_call_data));
   }

   size_t MoveQueued(std::vector<TraceDrawCallData>& trace_draw_calls_data, ID3D11DeviceContext* native_device_context, size_t index)
   {
      const size_t first_index = (std::min)(index, trace_draw_calls_data.size());
      std::erase_if(queued_entries, [native_device_context](const TraceDrawCallData& entry)
         { return entry.command_list.get() != native_device_context; });
      trace_draw_calls_data.insert(trace_draw_calls_data.begin() + first_index, std::make_move_iterator(queued_entries.begin()), std::make_move_iterator(queued_entries.end()));
      const size_t count = queued_entries.size();
      queued_entries.clear();
      return count;
   }

   void EndDraw(CommandListData& cmd_list_data, bool cancelled_or_replaced)
   {
      if (!trace_running)
         return;
      const std::shared_lock lock_trace(s_mutex_trace);
      const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
      const auto [first, count] = std::exchange(cmd_list_data.trace_current_draw, {});
      for (size_t i = first; cancelled_or_replaced && i < first + count && i < cmd_list_data.trace_draw_calls_data.size(); i++)
         cmd_list_data.trace_draw_calls_data[i].cancelled_or_replaced = true;
   }

   void ClearQueued()
   {
      queued_entries.clear();
   }
} // namespace Trace
