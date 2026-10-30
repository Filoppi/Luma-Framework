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

      // A game draw's entry for one of its shaders. Expects "s_mutex_generic", "s_mutex_dumping" and "device_data.mutex" (shared) locked.
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

      // Whether this is the instance of a pass the debug draw or the constant buffer tracking targets, counting it
      bool IsTargetInstance(std::atomic<int32_t>& instance, int32_t target_instance)
      {
         const int32_t current_instance = instance.fetch_add(1);
         return target_instance == -1 || current_instance == target_instance;
      }

      std::shared_ptr<void> CacheState(ID3D11DeviceContext* native_device_context, const DeviceData& device_data, bool is_dispatch)
      {
         if (is_dispatch)
         {
            auto state = std::make_shared<DrawStateStack<DrawStateStackType::Compute>>();
            state->Cache(native_device_context, device_data.uav_max_count);
            return state;
         }
         auto state = std::make_shared<DrawStateStack<DrawStateStackType::FullGraphics>>();
         state->Cache(native_device_context, device_data.uav_max_count);
         return state;
      }
      void RestoreState(ID3D11DeviceContext* native_device_context, const std::shared_ptr<void>& state, bool is_dispatch)
      {
         if (is_dispatch)
            static_cast<DrawStateStack<DrawStateStackType::Compute>*>(state.get())->Restore(native_device_context);
         else
            static_cast<DrawStateStack<DrawStateStackType::FullGraphics>*>(state.get())->Restore(native_device_context);
      }

      // Restores the pass inputs cloned the first time it drew since they were frozen, so a shader can be edited live on them
      template <DrawStateStackType type>
      void FreezeInputs(ID3D11DeviceContext* native_device_context, DeviceData& device_data)
      {
         if (!device_data.debug_draw_frozen_draw_state_stack)
         {
            auto frozen_state = std::make_shared<DrawStateStack<type>>();
            frozen_state->Cache(native_device_context, device_data.uav_max_count);
            frozen_state->Clone(native_device_context, device_data.GetLumaCBuffers());
            device_data.debug_draw_frozen_draw_state_stack = frozen_state;
         }
         // We keep the original RTVs, UAVs and shaders otherwise live editing of custom shaders won't work.
         // Making the draw on a non frozen UAV could cause iterative passes to not 100% freeze, but if ever needed, we could add a flag for that too.
         static_cast<DrawStateStack<type>*>(device_data.debug_draw_frozen_draw_state_stack.get())->Restore(native_device_context, false, false);
      }

      // Needs the pass's state still bound: copies the constant buffer the tracking selected, of "stage", and makes a copy of it in deferred contexts
      void TrackConstantBuffer(ID3D11DeviceContext* native_device_context, DeviceData& device_data, reshade::api::shader_stage stage)
      {
         com_ptr<ID3D11Buffer> cb;
         UINT cb_first = 0, cb_num = 0;
         com_ptr<ID3D11DeviceContext1> native_device_context_1;
         native_device_context->QueryInterface(&native_device_context_1);
         switch (stage)
         {
         case reshade::api::shader_stage::vertex:
            if (native_device_context_1)
               native_device_context_1->VSGetConstantBuffers1(track_buffer_index, 1, &cb, &cb_first, &cb_num);
            else
               native_device_context->VSGetConstantBuffers(track_buffer_index, 1, &cb);
            break;
         case reshade::api::shader_stage::pixel:
            if (native_device_context_1)
               native_device_context_1->PSGetConstantBuffers1(track_buffer_index, 1, &cb, &cb_first, &cb_num);
            else
               native_device_context->PSGetConstantBuffers(track_buffer_index, 1, &cb);
            break;
         case reshade::api::shader_stage::compute:
            if (native_device_context_1)
               native_device_context_1->CSGetConstantBuffers1(track_buffer_index, 1, &cb, &cb_first, &cb_num);
            else
               native_device_context->CSGetConstantBuffers(track_buffer_index, 1, &cb);
            break;
         default:
            return;
         }
         device_data.track_buffer_data.data_valid = CopyBuffer(cb, native_device_context, device_data.track_buffer_data.data, device_data.track_buffer_data.cb);
         device_data.track_buffer_data.hash = std::to_string(std::hash<void*>{}(cb.get()));
         device_data.track_buffer_data.first_constant = cb_first;
         device_data.track_buffer_data.num_constants = cb_num;
      }

      // Moves the entries queued on this thread for "native_device_context" into its trace at "index" (the other contexts' are dropped), and returns how many.
      // Expects the trace mutexes locked.
      size_t MoveQueued(std::vector<TraceDrawCallData>& trace_draw_calls_data, ID3D11DeviceContext* native_device_context, size_t index = SIZE_MAX)
      {
         const size_t first_index = (std::min)(index, trace_draw_calls_data.size());
         std::erase_if(queued_entries, [native_device_context](const TraceDrawCallData& entry)
            { return entry.command_list.get() != native_device_context; });
         trace_draw_calls_data.insert(trace_draw_calls_data.begin() + first_index, std::make_move_iterator(queued_entries.begin()), std::make_move_iterator(queued_entries.end()));
         const size_t count = queued_entries.size();
         queued_entries.clear();
         return count;
      }

      // Builds an event entry with "setup" and adds it to the trace, while a capture runs
      template <typename TSetup>
      void AddEventEntry(CommandListData& cmd_list_data, ID3D11DeviceContext* command_list, TraceDrawCallData::TraceDrawCallType type, TSetup&& setup)
      {
         if (!running) // Unlocked early out, rechecked under the lock
            return;
         const std::shared_lock lock_trace(s_mutex);
         if (!running)
            return;
         TraceDrawCallData trace_draw_call_data;
         trace_draw_call_data.type = type;
         trace_draw_call_data.command_list = command_list;
         setup(trace_draw_call_data);
         const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
         cmd_list_data.trace_draw_calls_data.push_back(std::move(trace_draw_call_data));
      }

      void SetEventResources(TraceDrawCallData& trace_draw_call_data, ID3D11Resource* source, ID3D11Resource* target)
      {
         // Re-use the SRV and RTV data for simplicity
         if (source)
            GetResourceInfo(source, trace_draw_call_data.sr_size[0], trace_draw_call_data.sr_format[0], &trace_draw_call_data.sr_type_name[0], &trace_draw_call_data.sr_hash[0]);
         if (target)
            GetResourceInfo(target, trace_draw_call_data.rt_size[0], trace_draw_call_data.rt_format[0], &trace_draw_call_data.rt_type_name[0], &trace_draw_call_data.rt_hash[0]);
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
         if (debug_draw_custom_pass == 0 && track_buffer_custom_pass == 0 && !running)
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
         const uint32_t name_hash = Math::CompileTimeStringHash(name);
         if (debug_draw_custom_pass == name_hash && (debug_draw_pipeline_target_thread == std::thread::id() || debug_draw_pipeline_target_thread == std::this_thread::get_id())
             && IsTargetInstance(debug_draw_custom_pass_instance, debug_draw_pipeline_target_instance))
            CopyDebugDrawTexture(debug_draw_mode, debug_draw_view_index, native_device_context, *device_data, stage == reshade::api::shader_stage::compute);
         if (track_buffer_custom_pass == name_hash && IsTargetInstance(track_buffer_custom_pass_instance, track_buffer_pipeline_target_instance))
            TrackConstantBuffer(native_device_context, *device_data, stage);
         if (!running)
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

   void CustomDraw(ID3D11DeviceContext* native_device_context, const char* name, UINT vertex_count)
   {
      DrawDispatchData draw_dispatch_data;
      draw_dispatch_data.vertex_count = vertex_count;
      draw_dispatch_data.instance_count = 1;
      RecordCustomPass(native_device_context, name, reshade::api::shader_stage::pixel, draw_dispatch_data);
   }

   void CustomDispatch(ID3D11DeviceContext* native_device_context, const char* name, UINT thread_group_count_x, UINT thread_group_count_y, UINT thread_group_count_z)
   {
      DrawDispatchData draw_dispatch_data;
      draw_dispatch_data.dispatch_count = {thread_group_count_x, thread_group_count_y, thread_group_count_z};
      RecordCustomPass(native_device_context, name, reshade::api::shader_stage::compute, draw_dispatch_data);
   }

   void AddEvent(CommandListData& cmd_list_data, ID3D11DeviceContext* command_list, TraceDrawCallData::TraceDrawCallType type, ID3D11Resource* source, ID3D11Resource* target)
   {
      AddEventEntry(cmd_list_data, command_list, type, [&](TraceDrawCallData& trace_draw_call_data)
         { SetEventResources(trace_draw_call_data, source, target); });
   }

   void AddClear(reshade::api::command_list* cmd_list, reshade::api::resource_view view, std::initializer_list<float> values)
   {
      if (view.handle == 0)
         return;
      AddEventEntry(*cmd_list->get_private_data<CommandListData>(), (ID3D11DeviceContext*)(cmd_list->get_native()), TraceDrawCallData::TraceDrawCallType::ClearResource, [&](TraceDrawCallData& trace_draw_call_data)
         {
            SetEventResources(trace_draw_call_data, nullptr, reinterpret_cast<ID3D11Resource*>(cmd_list->get_device()->get_resource_from_view(view).handle));
            std::copy_n(values.begin(), (std::min)(values.size(), std::size(trace_draw_call_data.blend_factor)), trace_draw_call_data.blend_factor);
         });
   }

   void AddCopy(CommandListData& cmd_list_data, ID3D11DeviceContext* command_list, ID3D11Resource* source, ID3D11Resource* target, const TraceDrawCallData::CopyInfo& copy_info)
   {
      AddEventEntry(cmd_list_data, command_list, TraceDrawCallData::TraceDrawCallType::CopyResource, [&](TraceDrawCallData& trace_draw_call_data)
         {
            trace_draw_call_data.copy_info = copy_info;
            SetEventResources(trace_draw_call_data, source, target);
         });
   }

   void JoinCommandList(CommandListData& cmd_list_data, CommandListData& secondary_cmd_list_data, ID3D11DeviceContext* command_list, bool is_finish_command_list)
   {
      if (!running)
         return;
      const std::shared_lock lock_trace(s_mutex);
      if (!running)
         return;
      TraceDrawCallData trace_draw_call_data;
      trace_draw_call_data.type = is_finish_command_list ? TraceDrawCallData::TraceDrawCallType::CreateCommandList : TraceDrawCallData::TraceDrawCallType::AppendCommandList;
      trace_draw_call_data.command_list = command_list;
      const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
      cmd_list_data.trace_draw_calls_data.push_back(std::move(trace_draw_call_data));
      const std::unique_lock lock_trace_3(secondary_cmd_list_data.mutex_trace);
      cmd_list_data.trace_draw_calls_data.append_range(secondary_cmd_list_data.trace_draw_calls_data);
      secondary_cmd_list_data.trace_draw_calls_data.clear(); // Clear the command list where the data was from (given it was moved to the other one)
   }

   void Marker(ID3D11DeviceContext* native_device_context, const char* name, ID3D11DeviceChild* target)
   {
      if (!running)
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
      if (!running) // Cheap early out for the hot path, rechecked under the lock
         return;
      const std::shared_lock lock_trace(s_mutex);
      if (!running)
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

   void OnPresent(CommandListData& cmd_list_data)
   {
      // Some games fail to capture input on boot, so use a special key to force a graphics capture
      static bool graphics_capture_key_down = false; // "static" is fine, games usually only have one present call per frame
      bool was_graphics_capture_key_down = graphics_capture_key_down;
      graphics_capture_key_down = GetAsyncKeyState(VK_SUBTRACT) & 0x8000; // Numpad - key
      if (graphics_capture_key_down && !was_graphics_capture_key_down)
      {
         scheduled = true;
      }
      was_graphics_capture_key_down = graphics_capture_key_down;

      const std::unique_lock lock_trace(s_mutex);
      if (running)
      {
#if _DEBUG && LOG_VERBOSE
         reshade::log::message(reshade::log::level::info, "--- Frame Graphics Capture Ended ---");
#endif
         running = false;
         const std::shared_lock lock_trace_2(cmd_list_data.mutex_trace);
         captured_count = cmd_list_data.trace_draw_calls_data.size();
      }
      else if (scheduled)
      {
#if _DEBUG && LOG_VERBOSE
         reshade::log::message(reshade::log::level::info, "--- Frame Graphics Capture Started ---");
#endif
         // Split the trace logic over "two" frames, to make sure we capture everything in between two present calls
         scheduled = false;
         {
            const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
            cmd_list_data.trace_draw_calls_data.clear(); // This leaves the reserved space allocated
         }
         captured_count = 0;
         running = true;
      }
   }

   bool CopyDebugDrawTexture(DebugDrawMode debug_draw_mode, int32_t debug_draw_view_index, ID3D11DeviceContext* native_device_context, DeviceData& device_data, bool is_dispatch)
   {
      com_ptr<ID3D11Device> native_device_ptr;
      native_device_context->GetDevice(&native_device_ptr);
      ID3D11Device* native_device = native_device_ptr.get();

      com_ptr<ID3D11Resource> texture_resource;
      DXGI_FORMAT forced_texture_format = DXGI_FORMAT_UNKNOWN;
      if (debug_draw_mode == DebugDrawMode::RenderTarget || debug_draw_mode == DebugDrawMode::Depth || debug_draw_mode == DebugDrawMode::Stencil)
      {
         com_ptr<ID3D11RenderTargetView> rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
         com_ptr<ID3D11DepthStencilView> dsv;
         native_device_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, &rtvs[0], &dsv);

         if (debug_draw_mode == DebugDrawMode::RenderTarget)
         {
            com_ptr<ID3D11RenderTargetView> rtv = rtvs[debug_draw_view_index];
            if (rtv)
            {
               rtv->GetResource(&texture_resource);
               GetResourceInfo(texture_resource.get(), device_data.debug_draw_texture_size, device_data.debug_draw_texture_format); // Note: this isn't synchronized with the conditions that update "debug_draw_texture" below but it should work anyway
               D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
               rtv->GetDesc(&rtv_desc);
               device_data.debug_draw_texture_format = rtv_desc.Format;
            }
         }
         else if (dsv) // DebugDrawMode::Depth || DebugDrawMode::Stencil
         {
            bool true_depth_false_stencil = debug_draw_mode == DebugDrawMode::Depth;
            dsv->GetResource(&texture_resource);
            GetResourceInfo(texture_resource.get(), device_data.debug_draw_texture_size, device_data.debug_draw_texture_format);
            D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc;
            dsv->GetDesc(&dsv_desc);
            // Note: this format might be exclusive to DSVs and not work with SRVs, so we adjust it
            device_data.debug_draw_texture_format = dsv_desc.Format;
            switch (device_data.debug_draw_texture_format)
            {
            case DXGI_FORMAT_D16_UNORM:
            {
               device_data.debug_draw_texture_format = true_depth_false_stencil ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_UNKNOWN;
            }
            break;
            case DXGI_FORMAT_D24_UNORM_S8_UINT:
            {
               device_data.debug_draw_texture_format = true_depth_false_stencil ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS : DXGI_FORMAT_X24_TYPELESS_G8_UINT;
               forced_texture_format = DXGI_FORMAT_R24G8_TYPELESS;
            }
            break;
            case DXGI_FORMAT_D32_FLOAT:
            {
               device_data.debug_draw_texture_format = true_depth_false_stencil ? DXGI_FORMAT_R32_FLOAT : DXGI_FORMAT_UNKNOWN;
            }
            break;
            case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
            {
               device_data.debug_draw_texture_format = true_depth_false_stencil ? DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS : DXGI_FORMAT_X32_TYPELESS_G8X24_UINT;
               forced_texture_format = DXGI_FORMAT_R32G8X24_TYPELESS;
            }
            break;
            }
         }
      }
      else if (debug_draw_mode == DebugDrawMode::UnorderedAccessView)
      {
         com_ptr<ID3D11UnorderedAccessView> unordered_access_view;

         com_ptr<ID3D11UnorderedAccessView> uavs[D3D11_1_UAV_SLOT_COUNT];
         // Not sure there's a difference between these two but probably the second one is just meant for pixel shader draw calls
         if (is_dispatch)
         {
            native_device_context->CSGetUnorderedAccessViews(0, device_data.uav_max_count, &uavs[0]);
         }
         else
         {
            native_device_context->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, device_data.uav_max_count, &uavs[0]);
         }

         unordered_access_view = uavs[debug_draw_view_index];

         if (unordered_access_view)
         {
            unordered_access_view->GetResource(&texture_resource);
            GetResourceInfo(texture_resource.get(), device_data.debug_draw_texture_size, device_data.debug_draw_texture_format);
            D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc;
            unordered_access_view->GetDesc(&uav_desc);
            device_data.debug_draw_texture_format = uav_desc.Format; // Note: this isn't synchronized with the conditions that update "debug_draw_texture" below but it should work anyway
         }
      }
      else /*if (debug_draw_mode == DebugDrawMode::ShaderResource)*/
      {
         com_ptr<ID3D11ShaderResourceView> shader_resource_view;
         // Note: these might assert if you query an invalid index (there's no way of knowing it without tracking the previous sets)
         if (is_dispatch)
         {
            native_device_context->CSGetShaderResources(debug_draw_view_index, 1, &shader_resource_view);
         }
         else
         {
            native_device_context->PSGetShaderResources(debug_draw_view_index, 1, &shader_resource_view);
         }
         if (shader_resource_view)
         {
            shader_resource_view->GetResource(&texture_resource);
            GetResourceInfo(texture_resource.get(), device_data.debug_draw_texture_size, device_data.debug_draw_texture_format);
            D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
            shader_resource_view->GetDesc(&srv_desc);
            device_data.debug_draw_texture_format = srv_desc.Format;
         }
      }

      // TODO: as optimization, we could skip re-creating the texture every frame if the new attempted texture is identical to the previous one.
      device_data.debug_draw_texture = nullptr; // Always clear it, even if the new creation failed, because we shouldn't really keep the old one (even if "debug_draw_auto_clear_texture" is true)
      if (texture_resource)
      {
         // Note: it's possible to use "ID3D11Resource::GetType()" instead of this
         com_ptr<ID3D11Texture2D> texture_2d;
         texture_resource->QueryInterface(&texture_2d);
         com_ptr<ID3D11Texture3D> texture_3d;
         texture_resource->QueryInterface(&texture_3d);
         com_ptr<ID3D11Texture1D> texture_1d;
         texture_resource->QueryInterface(&texture_1d);
         // For now we re-create it every frame as we don't care for performance
         HRESULT hr = E_FAIL;
         if (texture_2d)
         {
            D3D11_TEXTURE2D_DESC texture_desc;
            texture_2d->GetDesc(&texture_desc);
            ASSERT_ONCE_MSG((texture_desc.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE) != 0 || texture_desc.ArraySize != 6, "Texture Cube Debug Drawing is likely not supported");
            texture_desc.Usage = D3D11_USAGE_DEFAULT;
            texture_desc.CPUAccessFlags = 0;
            texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE; // We don't need "D3D11_BIND_RENDER_TARGET" nor "D3D11_BIND_UNORDERED_ACCESS" nor "D3D11_BIND_DEPTH_STENCIL" for now
            texture_desc.MiscFlags &= ~D3D11_RESOURCE_MISC_GENERATE_MIPS;
            texture_desc.MiscFlags &= ~D3D11_RESOURCE_MISC_SHARED;
            texture_desc.MiscFlags &= ~D3D11_RESOURCE_MISC_TEXTURECUBE; // Remove the cube flag in an attempt to support it anyway as a 2D Array
            if (forced_texture_format != DXGI_FORMAT_UNKNOWN)
               texture_desc.Format = forced_texture_format;
            device_data.debug_draw_texture = nullptr;
            hr = native_device->CreateTexture2D(&texture_desc, nullptr, reinterpret_cast<ID3D11Texture2D**>(&device_data.debug_draw_texture)); // TODO: figure out error, happens sometimes. And make thread safe!
         }
         else if (texture_3d)
         {
            D3D11_TEXTURE3D_DESC texture_desc;
            texture_3d->GetDesc(&texture_desc);
            texture_desc.Usage = D3D11_USAGE_DEFAULT;
            texture_desc.CPUAccessFlags = 0;
            texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            texture_desc.MiscFlags &= ~D3D11_RESOURCE_MISC_GENERATE_MIPS;
            texture_desc.MiscFlags &= ~D3D11_RESOURCE_MISC_SHARED;
            if (forced_texture_format != DXGI_FORMAT_UNKNOWN)
               texture_desc.Format = forced_texture_format;
            device_data.debug_draw_texture = nullptr;
            hr = native_device->CreateTexture3D(&texture_desc, nullptr, reinterpret_cast<ID3D11Texture3D**>(&device_data.debug_draw_texture));
         }
         else if (texture_1d)
         {
            D3D11_TEXTURE1D_DESC texture_desc;
            texture_1d->GetDesc(&texture_desc);
            texture_desc.Usage = D3D11_USAGE_DEFAULT;
            texture_desc.CPUAccessFlags = 0;
            texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            texture_desc.MiscFlags &= ~D3D11_RESOURCE_MISC_GENERATE_MIPS;
            texture_desc.MiscFlags &= ~D3D11_RESOURCE_MISC_SHARED;
            if (forced_texture_format != DXGI_FORMAT_UNKNOWN)
               texture_desc.Format = forced_texture_format;
            device_data.debug_draw_texture = nullptr;
            hr = native_device->CreateTexture1D(&texture_desc, nullptr, reinterpret_cast<ID3D11Texture1D**>(&device_data.debug_draw_texture));
         }
         // Back it up as it gets immediately overwritten or re-used later
         if (SUCCEEDED(hr) && device_data.debug_draw_texture)
         {
            native_device_context->CopyResource(device_data.debug_draw_texture.get(), texture_resource.get());
            return true;
         }
         else
         {
            ASSERT_ONCE("Draw Debug: Target Texture is not 1D/2D/3D (???), or its creation failed");
         }
      }
      return false;
   }

   bool CopyDebugDrawTexture(DebugDrawMode debug_draw_mode, int32_t debug_draw_view_index, reshade::api::command_list* cmd_list, bool is_dispatch)
   {
      return CopyDebugDrawTexture(debug_draw_mode, debug_draw_view_index, (ID3D11DeviceContext*)(cmd_list->get_native()), *cmd_list->get_device()->get_private_data<DeviceData>(), is_dispatch);
   }

   bool MapBufferData(com_ptr<ID3D11Buffer>& cb, ID3D11DeviceContext* native_device_context, std::vector<float>& buffer_data, UINT byte_width)
   {
      D3D11_MAPPED_SUBRESOURCE mapped = {};
      // Map in DX11 here can only be done on non deferred contexts; it will stall the CPU until the GPU has the latest values (no need to flush the command list)
      HRESULT hr = native_device_context->Map(cb.get(), 0, D3D11_MAP_READ, 0, &mapped);
      if (FAILED(hr))
      {
         buffer_data.clear();
         ASSERT_ONCE(native_device_context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE);
         return false;
      }

      bool remainder = byte_width % sizeof(float) != 0;
      size_t num_floats = byte_width / sizeof(float);
      buffer_data.resize(num_floats + (remainder ? 1 : 0)); // Add 1 for safety (the last value might be half trash)
      if (remainder)
      {
         buffer_data[buffer_data.size() - 1] = 0.f; // Clear the last slot as it might not get fully copied
      }
      std::memcpy(buffer_data.data(), mapped.pData, byte_width);

      native_device_context->Unmap(cb.get(), 0);
      return true;
   }

   bool CopyBuffer(com_ptr<ID3D11Buffer> cb, ID3D11DeviceContext* native_device_context, std::vector<float>& buffer_data, com_ptr<ID3D11Buffer>& cb_copy)
   {
      // Always recreate it for now (we could skip doing so if the descs matched, but whatever)
      cb_copy.reset();

      if (cb.get() == nullptr)
      {
         buffer_data.clear();
         return false;
      }

      D3D11_BUFFER_DESC desc = {};
      cb->GetDesc(&desc);

      // Clone it if it can't be read by the CPU, or if we are in a deferred context as we won't be able to map the buffer
      if ((desc.CPUAccessFlags & D3D11_CPU_ACCESS_READ) == 0 || desc.Usage != D3D11_USAGE_STAGING || native_device_context->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED)
      {
         desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
         desc.Usage = D3D11_USAGE_STAGING;
         desc.BindFlags = 0;
         desc.MiscFlags = 0; // Some flags might be incompatible with staging
         desc.StructureByteStride = 0; // Must be zero for safety, as we cleared "D3D11_RESOURCE_MISC_BUFFER_STRUCTURED"

         com_ptr<ID3D11Device> native_device;
         native_device_context->GetDevice(&native_device);
         HRESULT hr = native_device->CreateBuffer(&desc, nullptr, &cb_copy);
         if (FAILED(hr))
         {
            buffer_data.clear();
            ASSERT_ONCE(false);
            return false;
         }
         native_device_context->CopyResource(cb_copy.get(), cb.get());
         cb = cb_copy;
      }

      // This will fail on deferred contexts, that's by design
      return MapBufferData(cb, native_device_context, buffer_data, desc.ByteWidth);
   }

   void Draw(CommandListData& cmd_list_data, DeviceData& device_data, ID3D11DeviceContext* native_device_context, const DrawDispatchData& draw_dispatch_data)
   {
      // TODO: it's not safe to lock global mutexes that might have already been locked by other concurrent functions, while also calling functions in the device or primary device context (in DX11) (because they have their own locks inside that might cause deadlocks!). The solution here would be to cache the data upfront and then lock a mutex to add it to our array. This might be safe if the game threading was already safe though, I'm not 100% sure.
      const std::shared_lock lock_trace(s_mutex);
      if (!running)
         return;
      const std::shared_lock lock_generic(s_mutex_generic);
      const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
      const std::lock_guard<std::recursive_mutex> lock_dumping(s_mutex_dumping);
      const std::shared_lock lock_device(device_data.mutex);
      ASSERT_ONCE(native_device_context);
      const size_t first = cmd_list_data.trace_draw_calls_data.size();
      AddDrawCall(cmd_list_data.trace_draw_calls_data, device_data, native_device_context, cmd_list_data.pipeline_state_original_vertex_shader.handle, draw_dispatch_data);
      if (cmd_list_data.pipeline_state_original_pixel_shader.handle != 0) // Somehow this can happen (e.g. query tests don't require pixel shaders)
         AddDrawCall(cmd_list_data.trace_draw_calls_data, device_data, native_device_context, cmd_list_data.pipeline_state_original_pixel_shader.handle, draw_dispatch_data);
      cmd_list_data.trace_current_draw = {first, cmd_list_data.trace_draw_calls_data.size() - first};
   }

   void Dispatch(CommandListData& cmd_list_data, DeviceData& device_data, ID3D11DeviceContext* native_device_context, const DrawDispatchData& draw_dispatch_data)
   {
      const std::shared_lock lock_trace(s_mutex);
      if (!running)
         return;
      const std::shared_lock lock_generic(s_mutex_generic);
      const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
      const std::lock_guard<std::recursive_mutex> lock_dumping(s_mutex_dumping);
      const std::shared_lock lock_device(device_data.mutex);
      const size_t first = cmd_list_data.trace_draw_calls_data.size();
      AddDrawCall(cmd_list_data.trace_draw_calls_data, device_data, native_device_context, cmd_list_data.pipeline_state_original_compute_shader.handle, draw_dispatch_data);
      cmd_list_data.trace_current_draw = {first, cmd_list_data.trace_draw_calls_data.size() - first};
   }

   void FinishDrawCallbacks(CommandListData& cmd_list_data, ID3D11DeviceContext* native_device_context, bool draw_went_through)
   {
      const std::shared_lock lock_trace(s_mutex);
      if (!running)
      {
         queued_entries.clear();
         return;
      }
      const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
      auto& entries = cmd_list_data.trace_draw_calls_data;
      auto& [first, count] = cmd_list_data.trace_current_draw;
      if (first == SIZE_MAX)
      {
         MoveQueued(entries, native_device_context);
         return;
      }
      for (size_t i = first; i < first + count && i < entries.size(); i++)
      {
         auto& entry = entries[i];
         if (entry.type != TraceDrawCallData::TraceDrawCallType::Shader) continue;
         if (entry.pipeline_handle == cmd_list_data.pipeline_state_original_compute_shader.handle) entry.drawn_origin = Shader::GetShaderOrigin(cmd_list_data, reshade::api::shader_stage::compute);
         else if (entry.pipeline_handle == cmd_list_data.pipeline_state_original_vertex_shader.handle) entry.drawn_origin = Shader::GetShaderOrigin(cmd_list_data, reshade::api::shader_stage::vertex);
         else if (entry.pipeline_handle == cmd_list_data.pipeline_state_original_pixel_shader.handle) entry.drawn_origin = Shader::GetShaderOrigin(cmd_list_data, reshade::api::shader_stage::pixel);
      }
      if (draw_went_through)
         first += MoveQueued(entries, native_device_context, first);
      else
         MoveQueued(entries, native_device_context);
   }

   GameDraw BeginGameDraw(CommandListData& cmd_list_data, DeviceData& device_data, ID3D11DeviceContext* native_device_context, bool is_dispatch)
   {
      GameDraw game_draw;
      game_draw.is_dispatch = is_dispatch;
      const reshade::api::shader_stage stage = is_dispatch ? reshade::api::shader_stage::compute : reshade::api::shader_stage::pixel;
      const uint64_t pipeline = is_dispatch ? cmd_list_data.pipeline_state_original_compute_shader.handle : cmd_list_data.pipeline_state_original_pixel_shader.handle;
      const auto& shader_hashes = is_dispatch ? cmd_list_data.pipeline_state_original_compute_shader_hashes : cmd_list_data.pipeline_state_original_graphics_shader_hashes;
      game_draw.debug_draw = (debug_draw_shader_hash != 0 || debug_draw_pipeline != 0) && (debug_draw_pipeline == 0 || debug_draw_pipeline == pipeline)
         && (debug_draw_shader_hash == 0 || shader_hashes.Contains(debug_draw_shader_hash, stage))
         && (debug_draw_pipeline_target_thread == std::thread::id() || debug_draw_pipeline_target_thread == std::this_thread::get_id());
      if (!game_draw.debug_draw)
         return game_draw;
      if (debug_draw_freeze_inputs)
      {
         if (is_dispatch)
            FreezeInputs<DrawStateStackType::Compute>(native_device_context, device_data);
         else
            FreezeInputs<DrawStateStackType::FullGraphics>(native_device_context, device_data);
      }
      game_draw.pre_draw_state = CacheState(native_device_context, device_data, is_dispatch);
      if (!is_dispatch && debug_draw_disable_blends) // TODO: we never reset these after the draw call? Potentially polluting the game's state? It'd be fine if we are draw debugging!
         native_device_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
      return game_draw;
   }

   bool EndGameDraw(const GameDraw& game_draw, CommandListData& cmd_list_data, DeviceData& device_data, reshade::api::command_list* cmd_list, bool cancelled_or_replaced, const std::function<void()>& draw)
   {
      EndDraw(cmd_list_data, cancelled_or_replaced);
      ID3D11DeviceContext* native_device_context = (ID3D11DeviceContext*)(cmd_list->get_native());

      // Run the draw (instead of delegating it to ReShade) and copy its output. A replaced pass is read back as the game drew it, unless asked otherwise.
      bool drawn = false;
      if (game_draw.debug_draw && IsTargetInstance(debug_draw_pipeline_instance, debug_draw_pipeline_target_instance))
      {
         const bool read_original = cancelled_or_replaced && !debug_draw_replaced_pass;
         std::shared_ptr<void> post_draw_state;
         if (!cancelled_or_replaced)
         {
            draw();
         }
         else if (read_original)
         {
            post_draw_state = CacheState(native_device_context, device_data, game_draw.is_dispatch);
            RestoreState(native_device_context, game_draw.pre_draw_state, game_draw.is_dispatch);
         }
         CopyDebugDrawTexture(debug_draw_mode, debug_draw_view_index, native_device_context, device_data, game_draw.is_dispatch);
         if (read_original)
            RestoreState(native_device_context, post_draw_state, game_draw.is_dispatch);
         drawn = true;
      }

      reshade::api::shader_stage track_buffer_stage = reshade::api::shader_stage(0);
      if (track_buffer_pipeline == 0)
         track_buffer_stage = reshade::api::shader_stage(0);
      else if (game_draw.is_dispatch)
         track_buffer_stage = track_buffer_pipeline == cmd_list_data.pipeline_state_original_compute_shader.handle ? reshade::api::shader_stage::compute : reshade::api::shader_stage(0);
      else if (track_buffer_pipeline == cmd_list_data.pipeline_state_original_pixel_shader.handle)
         track_buffer_stage = reshade::api::shader_stage::pixel;
      else if (track_buffer_pipeline == cmd_list_data.pipeline_state_original_vertex_shader.handle)
         track_buffer_stage = reshade::api::shader_stage::vertex;
      // TODO: make "track_buffer_pipeline_target_instance" by thread (like "debug_draw_pipeline_target_instance"), though it's rarely useful
      if (track_buffer_stage != reshade::api::shader_stage(0) && IsTargetInstance(track_buffer_pipeline_instance, track_buffer_pipeline_target_instance))
         TrackConstantBuffer(native_device_context, device_data, track_buffer_stage);
      return drawn;
   }

   void FlushQueued(CommandListData* cmd_list_data, ID3D11DeviceContext* native_device_context)
   {
      const std::shared_lock lock_trace(s_mutex);
      if (!running || !cmd_list_data)
      {
         queued_entries.clear();
         return;
      }
      const std::unique_lock lock_trace_2(cmd_list_data->mutex_trace);
      MoveQueued(cmd_list_data->trace_draw_calls_data, native_device_context);
   }

   void InitCommandList(CommandListData& cmd_list_data)
   {
      // Pre-allocate to make it faster (whether it's primary or not)
      // Check for the memory as this can crash in x86 games that had little consecutive memory left (e.g. Burnout Paradise Remastered)
#ifdef WIN32
      constexpr size_t reserve_size = 2500;
#else
      constexpr size_t reserve_size = 5000;
#endif
      if (System::CanAllocate(sizeof(TraceDrawCallData) * reserve_size * 2)) // Check for double the space, just to be sure
         cmd_list_data.trace_draw_calls_data.reserve(reserve_size);
   }

   std::optional<PassTarget> FindPassTarget(const DeviceData& device_data, const CommandListData& cmd_list_data, size_t index, bool by_thread)
   {
      if (index >= cmd_list_data.trace_draw_calls_data.size())
         return std::nullopt;
      const TraceDrawCallData& entry = cmd_list_data.trace_draw_calls_data[index];
      PassTarget target;
      bool is_luma_pass = false;
      if (entry.type == TraceDrawCallData::TraceDrawCallType::Custom && entry.custom_stage != reshade::api::shader_stage(0))
      {
         is_luma_pass = true;
         target.custom_pass = Math::CompileTimeStringHash(entry.custom_name);
         target.shader_hash = entry.custom_shader_hash;
         target.stage = entry.custom_stage;
      }
      else if (entry.type == TraceDrawCallData::TraceDrawCallType::Shader)
      {
         const auto pipeline_pair = device_data.pipeline_cache_by_pipeline_handle.find(entry.pipeline_handle);
         if (pipeline_pair == device_data.pipeline_cache_by_pipeline_handle.end() || !pipeline_pair->second || pipeline_pair->second->shader_hashes.empty())
            return std::nullopt;
         const CachedPipeline* pipeline = pipeline_pair->second;
         target.pipeline = entry.pipeline_handle;
         target.shader_hash = pipeline->shader_hashes[0];
         target.stage = pipeline->HasPixelShader() ? reshade::api::shader_stage::pixel : (pipeline->HasComputeShader() ? reshade::api::shader_stage::compute : reshade::api::shader_stage::vertex);
      }
      else
      {
         return std::nullopt;
      }
      if (by_thread)
         target.thread = entry.thread_id;
      for (size_t i = 0; i < index; i++)
      {
         const auto& other = cmd_list_data.trace_draw_calls_data[i];
         if (by_thread && other.thread_id != entry.thread_id)
            continue;
         if (is_luma_pass ? (other.type == TraceDrawCallData::TraceDrawCallType::Custom && strcmp(other.custom_name, entry.custom_name) == 0) : (other.type == TraceDrawCallData::TraceDrawCallType::Shader && other.pipeline_handle == entry.pipeline_handle))
            target.instance++;
      }
      return target;
   }

   bool IsDebugDrawTarget(const PassTarget& target)
   {
      const bool same_pass = target.custom_pass != 0 ? debug_draw_custom_pass == target.custom_pass : (debug_draw_custom_pass == 0 && debug_draw_shader_hash == target.shader_hash);
      return same_pass && (debug_draw_pipeline_target_instance < 0 || debug_draw_pipeline_target_instance == target.instance);
   }

   void SetDebugDrawTarget(DeviceData& device_data, const PassTarget& target, const TraceDrawCallData& entry, bool keep_mode)
   {
      ClearDebugDrawTarget(device_data);
      if (target.custom_pass != 0)
      {
         debug_draw_custom_pass = target.custom_pass;
      }
      else
      {
         debug_draw_pipeline = target.pipeline;
         debug_draw_shader_hash = target.shader_hash;
         const std::string shader_hash_string = Shader::Hash_NumToStr(debug_draw_shader_hash);
         if (shader_hash_string.size() <= HASH_CHARACTERS_LENGTH)
            strcpy(&debug_draw_shader_hash_string[0], shader_hash_string.c_str());
      }
      debug_draw_pipeline_target_instance = target.instance;
      debug_draw_pipeline_target_thread = target.thread;

      const auto prev_debug_draw_mode = debug_draw_mode;
      if (prev_debug_draw_mode == DebugDrawMode::Depth || prev_debug_draw_mode == DebugDrawMode::Stencil)
      {
         debug_draw_options &= ~(uint32_t)DebugDrawTextureOptionsMask::RedOnly;
      }
      if (prev_debug_draw_mode == DebugDrawMode::Custom)
      {
         debug_draw_view_index = 0;
         debug_draw_mip = 0;
      }
      if (!keep_mode || prev_debug_draw_mode == DebugDrawMode::Custom)
      {
         debug_draw_mode = target.stage == reshade::api::shader_stage::pixel ? DebugDrawMode::RenderTarget : (target.stage == reshade::api::shader_stage::compute ? DebugDrawMode::UnorderedAccessView : DebugDrawMode::ShaderResource);
      }
      // Preserve the last view index between passes that have it (e.g. during gbuffers rendering)
      const auto has_view = [&](const auto& view_formats)
      {
         return debug_draw_view_index >= 0 && debug_draw_view_index < int32_t(std::size(view_formats))
            && view_formats[debug_draw_view_index] != DXGI_FORMAT_UNKNOWN && view_formats[debug_draw_view_index] != DXGI_FORMAT(-1);
      };
      if (debug_draw_mode != prev_debug_draw_mode
         || (debug_draw_mode == DebugDrawMode::RenderTarget && !has_view(entry.rtv_format))
         || (debug_draw_mode == DebugDrawMode::ShaderResource && !has_view(entry.srv_format))
         || (debug_draw_mode == DebugDrawMode::UnorderedAccessView && !has_view(entry.uav_format)))
      {
         debug_draw_view_index = 0;
      }
      // Fall back on depth if there main RT isn't valid
      if (debug_draw_mode == DebugDrawMode::RenderTarget && entry.rt_format[0] == DXGI_FORMAT_UNKNOWN && entry.depth_state != TraceDrawCallData::DepthStateType::Disabled)
      {
         debug_draw_mode = DebugDrawMode::Depth;
         debug_draw_options |= (uint32_t)DebugDrawTextureOptionsMask::RedOnly;
      }
      if (debug_draw_mode == DebugDrawMode::Depth || debug_draw_mode == DebugDrawMode::Stencil)
      {
         debug_draw_view_index = 0;
         debug_draw_mip = 0;
      }
      debug_draw_freeze_inputs = false; // Unfreeze inputs automatically every time we swap the target pass
   }

   void ClearDebugDrawTarget(DeviceData& device_data)
   {
      debug_draw_pipeline = 0;
      debug_draw_shader_hash = 0;
      debug_draw_shader_hash_string[0] = 0;
      debug_draw_custom_pass = 0;
      debug_draw_pipeline_target_instance = -1;
      debug_draw_pipeline_target_thread = std::thread::id();
      debug_draw_pipeline_instance = 0;
      debug_draw_custom_pass_instance = 0;
      device_data.debug_draw_frozen_draw_state_stack.reset(); // It might store the state of another type of pass ("DrawStateStack<DrawStateStackType::Compute>" or "DrawStateStack<DrawStateStackType::FullGraphics>")
      device_data.debug_draw_texture = nullptr;
      device_data.debug_draw_texture_format = DXGI_FORMAT_UNKNOWN;
      device_data.debug_draw_texture_size = {};
   }

   bool IsTrackBufferTarget(const PassTarget& target)
   {
      const bool same_pass = target.custom_pass != 0 ? track_buffer_custom_pass == target.custom_pass : (track_buffer_custom_pass == 0 && track_buffer_pipeline == target.pipeline);
      return same_pass && (track_buffer_pipeline_target_instance < 0 || track_buffer_pipeline_target_instance == target.instance);
   }

   void SetTrackBufferTarget(DeviceData& device_data, const PassTarget& target)
   {
      ClearTrackBufferTarget(device_data);
      track_buffer_pipeline = target.custom_pass != 0 ? 0 : target.pipeline;
      track_buffer_custom_pass = target.custom_pass;
      track_buffer_pipeline_target_instance = target.instance;
   }

   void ClearTrackBufferTarget(DeviceData& device_data)
   {
      track_buffer_pipeline = 0;
      track_buffer_custom_pass = 0;
      track_buffer_pipeline_target_instance = -1;
      track_buffer_index = 0;
      track_buffer_pipeline_instance = 0;
      track_buffer_custom_pass_instance = 0;
      device_data.track_buffer_data.Clear();
   }

   void EndFrame(DeviceData& device_data)
   {
      // Clear at the end of every frame and re-capture it in the next frame if its still available
      if (debug_draw_auto_clear_texture)
      {
         device_data.debug_draw_texture = nullptr;
         // Leave "debug_draw_texture_format" and "debug_draw_texture_size", we need that in ImGUI (we'd need to clear it after ImGUI if necessary, but we skip drawing it if the texture isn't valid)
      }
      debug_draw_pipeline_instance = 0;
      debug_draw_custom_pass_instance = 0;
      track_buffer_pipeline_instance = 0;
      track_buffer_custom_pass_instance = 0;
   }

   void UpdateDraw(CommandListData& cmd_list_data, DeviceData& device_data, ID3D11DeviceContext* native_device_context)
   {
      if (!running) // Cheap early out for the hot path, rechecked under the lock
         return;
      const std::shared_lock lock_trace(s_mutex);
      if (!running)
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

   void EndDraw(CommandListData& cmd_list_data, bool cancelled_or_replaced)
   {
      if (!running)
         return;
      const std::shared_lock lock_trace(s_mutex);
      const std::unique_lock lock_trace_2(cmd_list_data.mutex_trace);
      const auto [first, count] = std::exchange(cmd_list_data.trace_current_draw, {SIZE_MAX, 0});
      for (size_t i = first; cancelled_or_replaced && i < first + count && i < cmd_list_data.trace_draw_calls_data.size(); i++)
         cmd_list_data.trace_draw_calls_data[i].cancelled_or_replaced = true;
   }

} // namespace Trace
