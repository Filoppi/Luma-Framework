#pragma once

// The dev UI of the frame capture and the debug draw (see "ui.hpp"), included by "core.hpp" after the trace module
namespace UI
{
   void DrawCaptureTab(reshade::api::effect_runtime* runtime, DeviceData& device_data, bool& open_settings_tab)
   {
      const bool refresh_cloned_pipelines = device_data.cloned_pipelines_changed.exchange(false);
      static int32_t selected_index = -1; // TODO: rename to capture_, and "changed_selected" too
      static uint32_t prev_trace_count = -1; // Default to -1 to trigger a change in the first frame

      bool changed_selected = false;
      bool trace_count_changed = prev_trace_count != Trace::captured_count;
      bool open_capture_tab = trace_count_changed && Trace::captured_count > 0;
      open_settings_tab = trace_count_changed && Trace::captured_count <= 0; // Fall back on settings (and also default to it in the first frame)
      prev_trace_count = Trace::captured_count;

      ImGui::PushID("##CaptureTab");
      bool handle_shader_tab = Trace::captured_count > 0 && ImGui::BeginTabItem(std::format("Captured Commands ({})", Trace::captured_count).c_str(), nullptr, open_capture_tab ? ImGuiTabItemFlags_SetSelected : 0); // No need for "s_mutex" here
      ImGui::PopID();
      if (handle_shader_tab)
      {
         bool list_size_changed = false;

         if (ImGui::Button("Clear Capture and Debug Settings"))
         {
            Trace::captured_count = 0;
            selected_index = -1;
            changed_selected = true;
            open_settings_tab = true;

            Trace::ClearDebugDrawTarget(device_data);
            Trace::ClearTrackBufferTarget(device_data);

            Trace::highlighted_resource = "";

            {
               const std::unique_lock lock(s_mutex_generic);
               for (auto& pair : device_data.pipeline_cache_by_pipeline_handle)
               {
                  auto& cached_pipeline = pair.second;
                  cached_pipeline->custom_name.clear();
                  // Restore the original forced name
                  auto forced_shader_names_it = forced_shader_names.find(pair.second->shader_hashes[0]);
                  if (forced_shader_names_it != forced_shader_names.end())
                  {
                     cached_pipeline->custom_name = forced_shader_names_it->second;
                  }
                  cached_pipeline->replace_draw_type = ShaderReplaceDrawType::None;
                  cached_pipeline->custom_depth_stencil = ShaderCustomDepthStencilType::None;
                  cached_pipeline->redirect_data = { };
               }
            }

            // Only the primary command list should have any capture data, the others would have joined their with it
            {
               CommandListData& cmd_list_data = *runtime->get_command_queue()->get_immediate_command_list()->get_private_data<CommandListData>();
               const std::shared_lock lock_trace_2(cmd_list_data.mutex_trace);
               cmd_list_data.trace_draw_calls_data.clear();
            }
         }

         SameLineIfFits("Show Command List / Thread Info", true);
         list_size_changed |= ImGui::Checkbox("Show Command List / Thread Info", &Trace::show_command_list_info);
         SameLineIfFits("Ignore Vertex Shaders", true);
         list_size_changed |= ImGui::Checkbox("Ignore Vertex Shaders", &Trace::ignore_vertex_shaders);
         SameLineIfFits("Ignore Buffer Writes", true);
         list_size_changed |= ImGui::Checkbox("Ignore Buffer Writes", &Trace::ignore_buffer_writes);
         SameLineIfFits("Ignore Bindings", true);
         list_size_changed |= ImGui::Checkbox("Ignore Bindings", &Trace::ignore_bindings);
         SameLineIfFits("Ignore Custom Draws", true);
         list_size_changed |= ImGui::Checkbox("Ignore Custom Draws", &Trace::ignore_custom_draws);
         SameLineIfFits("Ignore Non Bound Shader Referenced Resources", true);
         ImGui::Checkbox("Ignore Non Bound Shader Referenced Resources", &Trace::ignore_non_bound_shader_referenced_resources);

         if (ImGui::BeginChild("HashList", ImVec2(500, -FLT_MIN), ImGuiChildFlags_ResizeX))
         {
            if (ImGui::BeginListBox("##HashesListBox", ImVec2(-FLT_MIN, -FLT_MIN)))
            {
               const std::shared_lock lock_trace(Trace::s_mutex); // We don't really need "s_mutex" here as when that data is being written ImGUI isn't running, but...
               if (!Trace::running)
               {
                  const std::shared_lock lock_generic(s_mutex_generic);
                  CommandListData& cmd_list_data = *runtime->get_command_queue()->get_immediate_command_list()->get_private_data<CommandListData>();
                  const std::shared_lock lock_trace_2(cmd_list_data.mutex_trace);

#if 1 // Much more optimized, drawing all items is extremely slow otherwise
                  std::vector<uint32_t> trace_draw_calls_index_redirector; // From full list index to filtered list index
                  trace_draw_calls_index_redirector.reserve(Trace::captured_count);
                  std::vector<uint32_t> trace_draw_calls_index_inverse_redirector; // From filtered index to full list index
                  trace_draw_calls_index_inverse_redirector.reserve(Trace::captured_count);

                  // Pre count the elements (some are skipped) otherwise the clipper won't work properly
                  // TODO: do this once on capture
                  size_t actual_trace_count = 0;
                  for (auto index = 0; index < Trace::captured_count; index++) {
                     trace_draw_calls_index_redirector.emplace_back(uint32_t(trace_draw_calls_index_inverse_redirector.size())); // The closest one
                     auto& draw_call_data = cmd_list_data.trace_draw_calls_data.at(index);
                     if (draw_call_data.type == TraceDrawCallData::TraceDrawCallType::Shader) {
                        auto pipeline_handle = draw_call_data.pipeline_handle;
                        const auto pipeline_pair = device_data.pipeline_cache_by_pipeline_handle.find(pipeline_handle);
                        const bool is_valid = pipeline_pair != device_data.pipeline_cache_by_pipeline_handle.end() && pipeline_pair->second != nullptr;
                        if (is_valid) {
                           if (Trace::ignore_vertex_shaders && pipeline_pair->second->HasVertexShader()) continue; // DX11 exclusive behaviour
                        }
                     }
                     else if (draw_call_data.type == TraceDrawCallData::TraceDrawCallType::CPUWrite) {
                        if (Trace::ignore_buffer_writes && draw_call_data.rt_type_name[0] == "Buffer") continue;
                     }
                     else if (draw_call_data.type == TraceDrawCallData::TraceDrawCallType::BindPipeline
                        || draw_call_data.type == TraceDrawCallData::TraceDrawCallType::BindResource) {
                        if (Trace::ignore_bindings) continue;
                     }
                     else if (draw_call_data.type == TraceDrawCallData::TraceDrawCallType::Custom) {
                        if (Trace::ignore_custom_draws && draw_call_data.custom_stage != reshade::api::shader_stage(0)) continue;
                     }
                     actual_trace_count++;
                     trace_draw_calls_index_inverse_redirector.emplace_back(index);
                  }

                  // Scroll to the target (that might have shifted)
                  if (list_size_changed && selected_index >= 0 && selected_index <= trace_draw_calls_index_redirector.size())
                  {
                     uint32_t filtered_index = trace_draw_calls_index_redirector[selected_index];
                     // If our selected index is currently hidden, automatically select the next one in line
                     if (filtered_index >= 0 && filtered_index <= trace_draw_calls_index_inverse_redirector.size())
                     {
                        uint32_t raw_index = trace_draw_calls_index_inverse_redirector[filtered_index];
                        selected_index = raw_index;
                     }
                     float item_height = ImGui::GetTextLineHeightWithSpacing();
                     float scroll_y = filtered_index * item_height;
                     ImGui::SetScrollY(scroll_y - ImGui::GetWindowHeight() * 0.5f + item_height * 0.5f);
                     list_size_changed = false;
                  }

                  ImGuiListClipper clipper;
                  clipper.Begin(actual_trace_count);
                  while (clipper.Step()) {
                     for (int filtered_index = clipper.DisplayStart; filtered_index < clipper.DisplayEnd; filtered_index++) {
                        uint32_t index = trace_draw_calls_index_inverse_redirector[filtered_index]; // Raw index
#else
                  for (uint32_t index = 0; index < Trace::captured_count; index++) {
#endif
                     auto& draw_call_data = cmd_list_data.trace_draw_calls_data.at(index);
                     ASSERT_ONCE_MSG(draw_call_data.command_list, "The code below will probably crash if the command list isn't valid, remember to always assign it when adding a new element to the list");
                     if (draw_call_data.command_list)
                     {
                        com_ptr<ID3D11DeviceContext> native_device_context;
                        HRESULT hr = draw_call_data.command_list->QueryInterface(&native_device_context);
                        ASSERT_ONCE(SUCCEEDED(hr) && native_device_context); // Just to make sure it accidentally wasn't saved as a "ID3D11CommandList", given ReShade's design that mixes them up
                     }

                     auto pipeline_handle = draw_call_data.pipeline_handle;
                     auto thread_id = draw_call_data.thread_id._Get_underlying_id(); // Possibly compiler dependent but whatever, cast to int alternatively
                     const bool is_selected = selected_index == index;
                     // Note that the pipelines can be run more than once so this will return the first one matching (there's only one actually, we don't have separate settings for their running instance, as that's runtime stuff)
                     const auto pipeline_pair = device_data.pipeline_cache_by_pipeline_handle.find(pipeline_handle);
                     const bool is_valid = pipeline_pair != device_data.pipeline_cache_by_pipeline_handle.end() && pipeline_pair->second != nullptr;
                     std::stringstream name;
                     auto text_color = IM_COL32(255, 255, 255, 255); // White

                     std::string command_list_info = "";
                     if (Trace::show_command_list_info)
                     {
                        if (draw_call_data.command_list && draw_call_data.command_list->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) // Deferred
                           command_list_info = " - ~> " + std::to_string(reinterpret_cast<uint64_t>(draw_call_data.command_list.get())) + " - " + std::to_string(thread_id);
                        else // Immediate
                           command_list_info = " - " + std::to_string(thread_id); // No need to fill up the immediate command list ptr every time
                     }

                     bool found_highlighted_resource_write = false;
                     bool found_highlighted_resource_read = false;
                     if (!Trace::highlighted_resource.empty())
                     {
                        for (UINT i = 0; i < TraceDrawCallData::srvs_size; i++)
                        {
                           if (found_highlighted_resource_read) break;
                           found_highlighted_resource_read |= draw_call_data.sr_hash[i] == Trace::highlighted_resource;
                        }
                        for (UINT i = 0; i < TraceDrawCallData::uavs_size; i++)
                        {
                           if (found_highlighted_resource_write) break;
                           found_highlighted_resource_write |= draw_call_data.ua_hash[i] == Trace::highlighted_resource; // We consider UAV as write even if it's not necessarily one
                        }
                        for (UINT i = 0; i < TraceDrawCallData::rtvs_size; i++)
                        {
                           if (found_highlighted_resource_write) break;
                           found_highlighted_resource_write |= draw_call_data.rt_hash[i] == Trace::highlighted_resource;
                        }
                        for (UINT i = 0; i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; i++)
                        {
                           if (found_highlighted_resource_read) break;
                           found_highlighted_resource_read |= draw_call_data.cb_hash[i] == Trace::highlighted_resource;
                        }
                        // Don't set "found_highlighted_resource_read" for the test and write case, it'd just be more confusing
                        if (draw_call_data.depth_state == TraceDrawCallData::DepthStateType::TestAndWrite
                           || draw_call_data.depth_state == TraceDrawCallData::DepthStateType::WriteOnly
                           || draw_call_data.stencil_state == TraceDrawCallData::DepthStateType::TestAndWrite
                           || draw_call_data.stencil_state == TraceDrawCallData::DepthStateType::WriteOnly)
                           found_highlighted_resource_write |= draw_call_data.ds_hash == Trace::highlighted_resource;
                        else
                           found_highlighted_resource_read |= draw_call_data.ds_hash == Trace::highlighted_resource;
                     }

                     // TODO: merge pixel and vertex shader traces if they are both present? Yes, and then add two tabs one for VS data and shader code and one for PS (same)
                     if (is_valid && draw_call_data.type == TraceDrawCallData::TraceDrawCallType::Shader)
                     {
                        const auto pipeline = pipeline_pair->second;

                        // Highlight other draw calls with the same shader
                        bool same_as_selected = false;
                        if (/*!is_selected &&*/ selected_index >= 0 && cmd_list_data.trace_draw_calls_data.size() >= selected_index + 1 && cmd_list_data.trace_draw_calls_data.at(selected_index).type == TraceDrawCallData::TraceDrawCallType::Shader)
                        {
                           auto pipeline_handle_2 = cmd_list_data.trace_draw_calls_data.at(selected_index).pipeline_handle;
                           const auto pipeline_pair_2 = device_data.pipeline_cache_by_pipeline_handle.find(pipeline_handle_2);
                           const bool is_valid_2 = pipeline_pair_2 != device_data.pipeline_cache_by_pipeline_handle.end() && pipeline_pair_2->second != nullptr;
                           if (is_valid_2)
                           {
                              const auto pipeline_2 = pipeline_pair_2->second;
                              if (pipeline_2->shader_hashes == pipeline->shader_hashes)
                              {
#if 1 // Add a space before it
                                 name << "   ";
#endif
                                 same_as_selected = true;
                              }
                           }
                        }

                        name << std::setfill('0');
                        bool written_any_text = false;

                        if (Trace::show_command_list_info)
                        {
                           // Index - Thread ID (command list) - Shader Hash(es) - Shader Name
                           name << std::setw(3) << index << std::setw(0); // Fill up 3 slots for the index so the text is aligned

                           name << command_list_info;

                           written_any_text = true;
                        }

                        const char* sm = pipeline->StageName();

                        // Pick the default color by shader type
                        if (pipeline->HasVertexShader())
                        {
                           if (Trace::ignore_vertex_shaders)
                           {
                              continue;
                           }
                           text_color = IM_COL32(192, 192, 0, 255); // Yellow
                        }
                        else if (pipeline->HasComputeShader() || pipeline->HasGeometryShader())
                        {
                           text_color = IM_COL32(192, 0, 192, 255); // Purple
                        }
                        else if (!pipeline->HasPixelShader()) // Invalid
                        {
                           text_color = IM_COL32(255, 0, 0, 255); // Red
                        }

                        // There should always be at least one
                        for (auto shader_hash : pipeline->shader_hashes)
                        {
                           if (written_any_text)
                              name << " - ";
                           name << sm << " " << PRINT_CRC32(shader_hash);
                           written_any_text = true;
                        }

                        // DX11 specific code
                        const std::shared_lock lock_loading(s_mutex_loading);
                        const auto custom_shader = !pipeline->shader_hashes.empty() ? custom_shaders_cache[pipeline->shader_hashes[0]] : nullptr;

                        // Clone provenance comes from CloneOrigin (recorded at
                        // clone creation), not cache lookups or
                        // patch_application_mode (also stamped on file clones).
                        const bool is_custom_shader_clone = pipeline->cloned && pipeline->clone_origin == Shader::CloneOrigin::File;
                        const bool is_patch_clone = pipeline->cloned && pipeline->clone_origin == Shader::CloneOrigin::Patch;
                        const bool is_replaced_shader_clone = is_custom_shader_clone || is_patch_clone;

                        // Markers: "*" = custom-shader file clone, "#" =
                        // in-place sync, "#S" = sync clone, "#A" = async
                        // clone.
                        constexpr const char* replacement_markers[] = {"", "*", "#", "#S", "#A"};
                        name << replacement_markers[size_t(pipeline->GetReplacement())];

                        // Find if the shader has been modified

                        if (is_replaced_shader_clone)
                        {
                           if (pipeline->HasVertexShader())
                           {
                              text_color = IM_COL32(128, 255, 0, 255); // Yellow + Green
                           }
                           else if (pipeline->HasComputeShader())
                           {
                              text_color = IM_COL32(128, 255, 128, 255); // Purple + Green
                           }
                           else
                           {
                              text_color = IM_COL32(0, 255, 0, 255); // Green
                           }
                        }
                        // Texture upgrades symbols
                        constexpr bool targets_swapchain = false; // TODO: implement
                        if (draw_call_data.any_input_resources_format_upgraded || draw_call_data.any_output_resources_format_upgraded || draw_call_data.any_input_resources_scaled || draw_call_data.any_output_resources_scaled || targets_swapchain)
                        {
                           name << " ";
                        }
                        if (draw_call_data.any_input_resources_format_upgraded)
                        {
                           name << "^";
                        }
                        if (draw_call_data.any_input_resources_scaled)
                        {
                           name << "\\";
                        }
                        if (draw_call_data.any_output_resources_scaled)
                        {
                           name << "/";
                        }
                        if (draw_call_data.any_output_resources_format_upgraded)
                        {
                           name << "v";
                        }
                        if (targets_swapchain)
                        {
                           name << "°";
                        }

                        if (strlen(pipeline->custom_name.c_str()) > 0) // We can not check the string size as it's been allocated to more characters even if they are empty
                        {
                           name << " - " << pipeline->custom_name.c_str(); // Add c string otherwise it will append a billion null terminators
                        }
                        else if (is_custom_shader_clone)
                        {
                           // For now just force picking the first shader linked to the pipeline, there should always only be one (?)
                           if (custom_shader != nullptr && custom_shader->is_hlsl && !custom_shader->file_path.empty())
                           {
                              auto filename_string = custom_shader->file_path.filename().string();
                              if (const auto hash_begin_index = filename_string.find("0x"); hash_begin_index != std::string::npos) // TODO: this doesn't work with hashless shader files
                              {
                                 filename_string.erase(hash_begin_index); // Start deleting from where the shader hash(es) begin (e.g. "0x12345678.xs_n_n.hlsl")
                              }
                              if (filename_string.ends_with("_") || filename_string.ends_with("."))
                              {
                                 filename_string.erase(filename_string.length() - 1);
                              }
                              if (!filename_string.empty())
                              {
                                 name << " - " << filename_string;
                              }
                           }
                        }
                        else
                        {
                           std::optional<std::string> optional_name = GetD3DNameW(reinterpret_cast<ID3D11DeviceChild*>(pipeline->pipeline.handle));
                           if (optional_name.has_value())
                           {
                              name << " - " << optional_name.value().c_str();
                           }
                        }

                        // Print out dangerous blend modes. We only check the first RT for now.
                        // Note: theoretically we should only check this on upgraded textures (and all upgraded textures, not just RGBA) (here and in other places where we do this)! We can't check the format here reliably as it's the non upgraded one in case of indirect upgrades.
                        if (pipeline->HasPixelShader() && IsRGBAFormat(draw_call_data.rtv_format[0], true) /*&& IsSignedFloatFormat(draw_call_data.rtv_format[0])*/ && IsBlendInverted(draw_call_data.blend_desc, 1, false))
                        {
                           text_color = IM_COL32(255, 165, 0, 255); // Orange for Warning
                        }

                        // Highlight loading error
                        if (custom_shader != nullptr && !custom_shader->compilation_errors.empty())
                        {
                           text_color = custom_shader->compilation_error ? IM_COL32(255, 0, 0, 255) : IM_COL32(255, 165, 0, 255); // Red for Error, Orange for Warning
                        }

                        if (draw_call_data.cancelled_or_replaced)
                        {
                           name << " (cancelled/replaced)";
                           text_color = IM_COL32(128, 128, 128, 255); // Grey
                        }
                        else if (is_replaced_shader_clone && draw_call_data.drawn_origin == Shader::CloneOrigin::None)
                        {
                           name << " (ran original)";
                        }
                        if (!draw_call_data.replaced_slots.empty())
                        {
                           name << " (updated)";
                        }

                        if (same_as_selected)
                        {
#if 0 // We already do this better above
                           name << " - (Selected) ";
#endif
                           if (!is_selected)
                           {
                              text_color = IM_COL32(192, 192, 192, 255); // Grey
                           }
                        }
                     }
                     else if (draw_call_data.type == TraceDrawCallData::TraceDrawCallType::CopyResource)
                     {
                        if (Trace::show_command_list_info)
                        {
                           name << std::setfill('0') << std::setw(3) << index << std::setw(0);
                        name << command_list_info;
                           name << " - ";
                        }
                        name << "Copy Resource";
                        text_color = IM_COL32(255, 105, 0, 255); // Orange
                     }
                     else if (draw_call_data.type == TraceDrawCallData::TraceDrawCallType::BindPipeline)
                     {
                        if (Trace::ignore_bindings) continue;
                        
                        if (Trace::show_command_list_info)
                        {
                           name << std::setfill('0') << std::setw(3) << index << std::setw(0);
                        name << command_list_info;
                           name << " - ";
                        }
                        name << "Bind Pipeline";
                        text_color = IM_COL32(30, 200, 10, 255); // Some green
                     }
                     else if (draw_call_data.type == TraceDrawCallData::TraceDrawCallType::BindResource)
                     {
                        if (Trace::ignore_bindings) continue;
                        
                        if (Trace::show_command_list_info)
                        {
                           name << std::setfill('0') << std::setw(3) << index << std::setw(0);
                        name << command_list_info;
                           name << " - ";
                        }
                        name << "Bind Resource";
                        text_color = IM_COL32(30, 200, 10, 255); // Some green
                     }
                     else if (draw_call_data.type == TraceDrawCallData::TraceDrawCallType::CPURead)
                     {
                        if (Trace::show_command_list_info)
                        {
                           name << std::setfill('0') << std::setw(3) << index << std::setw(0);
                        name << command_list_info;
                           name << " - ";
                        }
                        name << "Resource CPU Read";
                        text_color = IM_COL32(255, 40, 0, 255); // Bright Red
                     }
                     else if (draw_call_data.type == TraceDrawCallData::TraceDrawCallType::CPUWrite)
                     {
                        if (Trace::show_command_list_info)
                        {
                           name << std::setfill('0') << std::setw(3) << index << std::setw(0);
                        name << command_list_info;
                           name << " - ";
                        }

                        // Hacky resource type name check
                        if (Trace::ignore_buffer_writes && draw_call_data.rt_type_name[0] == "Buffer")
                        {
                           continue;
                        }

                        name << "Resource CPU Write";

                        text_color = IM_COL32(255, 105, 0, 255); // Orange
                     }
                     else if (draw_call_data.type == TraceDrawCallData::TraceDrawCallType::ClearResource)
                     {
                        if (Trace::show_command_list_info)
                        {
                           name << std::setfill('0') << std::setw(3) << index << std::setw(0);
                        name << command_list_info;
                           name << " - ";
                        }
                        name << "Clear Resource";
                        text_color = IM_COL32(255, 105, 0, 255); // Orange
                     }
                     else if (draw_call_data.type == TraceDrawCallData::TraceDrawCallType::CreateCommandList)
                     {
                        if (Trace::show_command_list_info)
                        {
                           name << std::setfill('0') << std::setw(3) << index << std::setw(0);
                        name << command_list_info;
                           name << " - ";
                        }
                        name << "Create Command List";
                        text_color = IM_COL32(50, 80, 190, 255); // Some Blue
                     }
                     else if (draw_call_data.type == TraceDrawCallData::TraceDrawCallType::AppendCommandList)
                     {
                        if (Trace::show_command_list_info)
                        {
                           name << std::setfill('0') << std::setw(3) << index << std::setw(0);
                        name << command_list_info;
                           name << " - ";
                        }
                        name << "Append Command List";
                        text_color = IM_COL32(50, 80, 190, 255); // Some Blue
                     }
                     else if (draw_call_data.type == TraceDrawCallData::TraceDrawCallType::ResetCommmandList)
                     {
                        if (Trace::show_command_list_info)
                        {
                           name << std::setfill('0') << std::setw(3) << index << std::setw(0);
                        name << command_list_info;
                           name << " - ";
                        }
                        name << "Reset Command List";
                        text_color = IM_COL32(50, 80, 190, 255); // Some Blue
                     }
                     else if (draw_call_data.type == TraceDrawCallData::TraceDrawCallType::FlushCommandList)
                     {
                        if (Trace::show_command_list_info)
                        {
                           name << std::setfill('0') << std::setw(3) << index << std::setw(0);
                        name << command_list_info;
                           name << " - ";
                        }
                        name << "Flush Command List";
                        text_color = IM_COL32(50, 80, 190, 255); // Some Blue
                     }
                     else if (draw_call_data.type == TraceDrawCallData::TraceDrawCallType::Present)
                     {
                        if (Trace::show_command_list_info)
                        {
                           name << std::setfill('0') << std::setw(3) << index << std::setw(0);
                        name << command_list_info;
                           name << " - ";
                        }
                        name << "Present";
                        text_color = IM_COL32(50, 80, 190, 255); // Some Blue

                        // Highlight the resource on the swapchain too, given that "Present" traces aren't tracked the same way
                        if (!Trace::highlighted_resource.empty() && !device_data.swapchains.empty())
                        {
                           // Not fully safe, but it should do for almost all cases
                           reshade::api::swapchain* swapchain = *device_data.swapchains.begin();
                           IDXGISwapChain* native_swapchain = (IDXGISwapChain*)(swapchain->get_native());
                           UINT back_buffer_index = swapchain->get_current_back_buffer_index();
                           com_ptr<ID3D11Texture2D> back_buffer;
                           native_swapchain->GetBuffer(back_buffer_index, IID_PPV_ARGS(&back_buffer));

                           std::string backbuffer_hash = std::to_string(std::hash<void*>{}(back_buffer.get()));
                           if (Trace::highlighted_resource == backbuffer_hash)
                           {
                              found_highlighted_resource_read = true;
                           }
                        }
                     }
                     else if (draw_call_data.type == TraceDrawCallData::TraceDrawCallType::Custom)
                     {
                        if (Trace::ignore_custom_draws && draw_call_data.custom_stage != reshade::api::shader_stage(0)) continue;

                        if (Trace::show_command_list_info)
                        {
                           name << std::setfill('0') << std::setw(3) << index << std::setw(0);
                        name << command_list_info;
                           name << " - ";
                        }
                        name << draw_call_data.custom_name;
                        text_color = IM_COL32(70, 130, 180, 255); // Steel Blue
                     }
                     else
                     {
                        text_color = IM_COL32(255, 0, 0, 255); // Red
                        name << "ERROR: Capture data not found"; // The draw call either had an empty (e.g. pixel) shader set, or the game has since unloaded them
                     }

                     if (found_highlighted_resource_write || found_highlighted_resource_read)
                     {
                        if (found_highlighted_resource_write && found_highlighted_resource_read)
                        {
                           // Highlight there's an error in this case as in any DX version, reading and writing from the same resource isn't supported
                           text_color = IM_COL32(255, 0, 0, 255); // Red
                           name << " - (Highlighted Resource Read And Write)";
                        }
                        else
                        {
                           text_color = IM_COL32(255, 192, 203, 255); // Pink
                           name << (found_highlighted_resource_write ? " - (Highlighted R Write)" : " - (Highlighted R Read)");
                        }
                     }

                     ImGui::PushID(index);
                     ImGui::PushStyleColor(ImGuiCol_Text, text_color);
                     if (ImGui::Selectable(name.str().c_str(), is_selected, ImGuiSelectableFlags_AllowOverlap))
                     {
                        selected_index = index;
                        changed_selected = true;
                     }
                     ImGui::PopStyleColor();
                     if ((draw_call_data.type == TraceDrawCallData::TraceDrawCallType::Shader && is_valid) || (draw_call_data.type == TraceDrawCallData::TraceDrawCallType::Custom && draw_call_data.custom_stage != reshade::api::shader_stage(0)))
                     {
                        ImGui::SameLine();
                        ImGui::SetNextItemAllowOverlap(); // Allow the button to be clickable if rects overlap
                        if (ImGui::SmallButton(ICON_FK_SEARCH))
                        {
                           // Keep the last debugged view type, to quickly browse passes
                           if (const auto target = Trace::FindPassTarget(device_data, cmd_list_data, index, Trace::debug_draw_pipeline_instance_filter_by_thread))
                              Trace::SetDebugDrawTarget(device_data, *target, draw_call_data, true);
                        }
                     }
                     ImGui::PopID();

                     if (is_selected)
                     {
                        ImGui::SetItemDefaultFocus();
                        if (list_size_changed)
                        {
                           ImGui::SetScrollHereY(0.5f); // 0.0 = top, 0.5 = middle, 1.0 = bottom
                        }
                     }
                  }
#if 1
                  }
                  clipper.End();
#endif
               }
               else
               {
                  selected_index = -1;
                  changed_selected = true;
               }
               selected_index = min(selected_index, Trace::captured_count - 1); // Extra safety
               ImGui::EndListBox();
            }
         }
         ImGui::EndChild(); // HashList

         ImGui::SameLine();
         // The constant buffer the tracking read back (of a game pipeline, or a Luma pass by name) and the slot it reads
         const auto draw_tracked_buffer = [&](const TraceDrawCallData& draw_call_data)
         {
            ImGui::SliderInt("Constant Buffer Tracked Index", &Trace::track_buffer_index, 0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1);

            // Hacky way of retrieving the data if the tracked buffer was in a deferred command list and hence couldn't be mapped there
            if (!device_data.track_buffer_data.data_valid && device_data.track_buffer_data.cb)
            {
               // Make sure we have the immediate context, because MapBufferData will fail otherwise.
               ComPtr<ID3D11Device> native_device;
               draw_call_data.command_list->GetDevice(native_device.put());
               ComPtr<ID3D11DeviceContext> native_device_context;
               native_device->GetImmediateContext(native_device_context.put());

               D3D11_BUFFER_DESC desc = {};
               device_data.track_buffer_data.cb->GetDesc(&desc);
               device_data.track_buffer_data.data_valid = Trace::MapBufferData(device_data.track_buffer_data.cb, native_device_context.get(), device_data.track_buffer_data.data, desc.ByteWidth);
            }

            if (device_data.track_buffer_data.data_valid && !device_data.track_buffer_data.data.empty())
            {
               ImGui::NewLine();
               ImGui::Text("Tracked Constant Buffer:");
               ImGui::Text("Resource Hash: %s", device_data.track_buffer_data.hash.c_str());
               const UINT tb_first = device_data.track_buffer_data.first_constant;
               const UINT tb_num = device_data.track_buffer_data.num_constants;
               // Each D3D11.1 "constant" is a float4 = 4 floats in our data vector.
               // tb_num == 0 means D3D11.0 path (no partial-bind info): show the whole buffer.
               const size_t tb_display_begin = (tb_num != 0) ? (size_t)tb_first * 4 : 0;
               const size_t tb_display_end = (tb_num != 0) ? (size_t)(tb_first + tb_num) * 4 : device_data.track_buffer_data.data.size();
               const size_t tb_display_end_clamped = (std::min)(tb_display_end, device_data.track_buffer_data.data.size());
               if (tb_num != 0)
               {
                  ImGui::Text("Partial Bind: first constant %u, %u constants (floats %zu..%zu)", tb_first, tb_num, tb_display_begin, tb_display_end_clamped);
               }
               if (ImGui::BeginChild("TrackBufferScroll", ImVec2(0, 500), ImGuiChildFlags_Borders))
               {
                  // TODO: match with the shader assembly cbs etc (if the data is available)
                  // TODO: add a matrix 4x4 view?
                  if (ImGui::BeginTable("TrackBufferTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                     ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                     ImGui::TableSetupColumn("Float Value", ImGuiTableColumnFlags_WidthStretch);
                     ImGui::TableSetupColumn("Int Value", ImGuiTableColumnFlags_WidthStretch);
                     ImGui::TableHeadersRow();

                     for (size_t i = tb_display_begin; i < tb_display_end_clamped; ++i)
                     {
                        float this_data = device_data.track_buffer_data.data[i];
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        static const char* components[] = { "x", "y", "z", "w" };
                        size_t relative_i = i - tb_display_begin; // 0-based offset within the visible slice
                        size_t group = relative_i / 4; // Split index by float4
                        size_t comp_index = relative_i % 4; // Print out the x/y/w/z identifier
                        ImGui::Text("%zu:%s", group, components[comp_index]);
                        // First print as float
                        ImGui::TableSetColumnIndex(1);
                        // I'm not sure whether the auto float printing handles nan/inf
                        if (std::isnan(this_data))
                        {
                           ImGui::Text("NaN");
                        }
                        else if (std::isinf(this_data))
                        {
                           ImGui::Text("Inf");
                        }
                        else
                        {
                           ImGui::Text("%.7f", this_data); // We need to show quite a bit of precision
                        }
                        // Then print as int (should work as uint/bool as well)
                        ImGui::TableSetColumnIndex(2);
                        ImGui::Text("%i", Math::AsInt(this_data));
                     }
                     ImGui::EndTable();
                  }
               }
               ImGui::EndChild(); // TrackBufferScroll

               if (ImGui::Button("Copy Constant Buffer Data to Clipboard (float)"))
               {
                  std::ostringstream oss;
                  for (size_t i = tb_display_begin; i < tb_display_end_clamped; ++i) {
                     oss << device_data.track_buffer_data.data[i];
                     if (i + 1 < tb_display_end_clamped)
                        oss << '\n';
                  }
                  System::CopyToClipboard(oss.str());
               }
            }
         };

         // The state a trace entry recorded: a game draw's ("pipeline"), or a Luma pass's without one ("CustomDraw()").
         // Slots a draw callback changed ("UpdateDraw()") show what it bound, the replaced ones can show the game's original too.
         const auto draw_state_analysis = [&](const TraceDrawCallData& draw_call_data, CachedPipeline* pipeline, bool debug_draw_shader_enabled)
         {
            const bool has_vs = pipeline && pipeline->HasVertexShader();
            const bool has_ps = pipeline ? pipeline->HasPixelShader() : draw_call_data.custom_stage == reshade::api::shader_stage::pixel;
            const bool has_cs = pipeline ? pipeline->HasComputeShader() : draw_call_data.custom_stage == reshade::api::shader_stage::compute;

            static const TraceDrawCallData* last_draw_call_data = nullptr;
            static std::set<std::pair<TraceDrawCallData::SlotKind, UINT>> original_slots; // Replaced slots showing the original
            if (last_draw_call_data != &draw_call_data)
            {
               last_draw_call_data = &draw_call_data;
               original_slots.clear();
            }
            const auto FindReplacedSlot = [&](TraceDrawCallData::SlotKind kind, UINT slot) -> const TraceDrawCallData::ReplacedSlot*
            {
               const auto replaced_slot = std::ranges::find_if(draw_call_data.replaced_slots, [&](const TraceDrawCallData::ReplacedSlot& replaced_slot)
                  { return replaced_slot.kind == kind && replaced_slot.slot == slot; });
               return replaced_slot != draw_call_data.replaced_slots.end() && draw_call_data.replaced_state ? &*replaced_slot : nullptr;
            };
            const auto SlotData = [&](TraceDrawCallData::SlotKind kind, UINT slot) -> const TraceDrawCallData&
            {
               const auto* replaced_slot = FindReplacedSlot(kind, slot);
               if (!replaced_slot || replaced_slot->change == TraceDrawCallData::SlotChange::Unbound || original_slots.contains({kind, slot}))
                  return draw_call_data;
               return *draw_call_data.replaced_state;
            };
            const auto DrawSlotChange = [&](TraceDrawCallData::SlotKind kind, UINT slot)
            {
               const auto* replaced_slot = FindReplacedSlot(kind, slot);
               if (!replaced_slot)
                  return;
               constexpr const char* slot_change_names[] = {"Added", "Replaced", "Unbound"};
               ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 165, 0, 255)); // Orange
               ImGui::Text("%s by the game", slot_change_names[size_t(replaced_slot->change)]);
               ImGui::PopStyleColor();
               if (replaced_slot->change == TraceDrawCallData::SlotChange::Replaced)
               {
                  bool show_original = original_slots.contains({kind, slot});
                  if (ImGui::Checkbox("Show the Game's Original", &show_original))
                  {
                     if (show_original)
                        original_slots.emplace(kind, slot);
                     else
                        original_slots.erase({kind, slot});
                  }
               }
            };
            bool is_first_draw = true;

            if (has_cs)
            {
               ImGui::Text("Indirect: %s", draw_call_data.draw_dispatch_data.indirect ? "True" : "False");
               ImGui::Text("Dispatch Count: %ux%ux%u", draw_call_data.draw_dispatch_data.dispatch_count.x, draw_call_data.draw_dispatch_data.dispatch_count.y, draw_call_data.draw_dispatch_data.dispatch_count.z);
               is_first_draw = false;
            }
            else if (!pipeline && has_ps)
            {
               ImGui::Text("Vertex Count: %u", draw_call_data.draw_dispatch_data.vertex_count);
               is_first_draw = false;
            }
            else if (has_vs)
            {
               ImGui::Text("Indirect: %s", draw_call_data.draw_dispatch_data.indirect ? "True" : "False");
               ImGui::NewLine();
               ImGui::Text("Vertex Count: %u", draw_call_data.draw_dispatch_data.vertex_count);
               ImGui::Text("Instance Count: %u", draw_call_data.draw_dispatch_data.instance_count);
               ImGui::Text("First Vertex: %u", draw_call_data.draw_dispatch_data.first_vertex);
               ImGui::Text("First Instance: %u", draw_call_data.draw_dispatch_data.first_instance);
               ImGui::Text("Index Count: %u", draw_call_data.draw_dispatch_data.index_count);
               ImGui::Text("First Index: %u", draw_call_data.draw_dispatch_data.first_index);
               ImGui::Text("Vertex Offset: %i", draw_call_data.draw_dispatch_data.vertex_offset);
               ImGui::Text("Indexed: %s", draw_call_data.draw_dispatch_data.indexed ? "True" : "False"); // TODO: do we need this? Is "index_count" enough? The functions would be different.

               auto GetPrimitiveTopologyName = [](D3D_PRIMITIVE_TOPOLOGY topology) -> const char*
                  {
                     switch (topology)
                     {
                     case D3D_PRIMITIVE_TOPOLOGY_UNDEFINED:         return "UNDEFINED";
                     case D3D_PRIMITIVE_TOPOLOGY_POINTLIST:         return "POINTLIST";
                     case D3D_PRIMITIVE_TOPOLOGY_LINELIST:          return "LINELIST";
                     case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP:         return "LINESTRIP";
                     case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST:      return "TRIANGLELIST";
                     case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:     return "TRIANGLESTRIP";
                     case D3D_PRIMITIVE_TOPOLOGY_TRIANGLEFAN:       return "TRIANGLEFAN";
                     case D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ:      return "LINELIST ADJ";
                     case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ:     return "LINESTRIP ADJ";
                     case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ:  return "TRIANGLELIST ADJ";
                     case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ: return "TRIANGLESTRIP ADJ";
                     default:                                       return "UNKNOWN";
                     }
                  };
               ImGui::NewLine();
               ImGui::Text("Primitive Topology: %s", GetPrimitiveTopologyName(draw_call_data.primitive_topology));

               for (size_t i = 0; i < draw_call_data.vertex_buffer_hashes.size(); i++)
               {
                  ImGui::NewLine();
                  ImGui::Text("Vertex Buffer %u: %s", i, draw_call_data.vertex_buffer_hashes[i].c_str());
                  ImGui::Text("Input Layout %u Format: %s", i, GetFormatNameSafe(draw_call_data.input_layouts_formats[i]));
               }

               ImGui::NewLine();
               ImGui::Text("Input Buffer: %s", draw_call_data.input_layout_hash.c_str());
               ImGui::Text("Input Buffer Format: %s", GetFormatNameSafe(draw_call_data.index_buffer_format));
               ImGui::Text("Input Buffer Offset: %u", draw_call_data.index_buffer_offset);

               ImGui::Text("Input Layout: %s", draw_call_data.index_buffer_hash.c_str());
               is_first_draw = false;
            }

            if (has_vs || has_ps || has_cs)
            {
               for (UINT i = 0; i < TraceDrawCallData::srvs_size; i++)
               {
                  const TraceDrawCallData& slot_data = SlotData(TraceDrawCallData::SlotKind::SRV, i);
                  auto srv_format = slot_data.srv_format[i];
                  if (srv_format == DXGI_FORMAT_UNKNOWN) // Resource was not valid
                  {
                     continue;
                  }
                  const bool non_referenced = srv_format == DXGI_FORMAT(-1);
                  if (Trace::ignore_non_bound_shader_referenced_resources && srv_format == DXGI_FORMAT(-1))
                  {
                     continue;
                  }
                  auto sr_format = slot_data.sr_format[i];
                  auto sr_size = slot_data.sr_size[i];
                  auto srv_size = slot_data.srv_size[i];
                  auto sr_hash = slot_data.sr_hash[i];
                  auto sr_type_name = slot_data.sr_type_name[i];
                  auto sr_is_rt = slot_data.sr_is_rt[i];
                  auto sr_is_ua = slot_data.sr_is_ua[i];

                  ImGui::PushID(i);

                  if (!is_first_draw) { ImGui::Text(""); };
                  is_first_draw = false;
                  ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(245, 230, 140, 255)); // Faint Yellow
                  ImGui::Text("SRV Index: %u", i);
                  ImGui::PopStyleColor();
                  DrawSlotChange(TraceDrawCallData::SlotKind::SRV, i);
                  if (non_referenced)
                  {
                     ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255)); // Red
                     ImGui::Text("R Referenced but Not Bound");
                     ImGui::PopStyleColor();
                     continue;
                  }
                  ImGui::Text("R Hash: %s", sr_hash.c_str());
                  if (!slot_data.sr_debug_name[i].empty())
                     ImGui::Text("R Debug Name: %s", slot_data.sr_debug_name[i].c_str());
                  ImGui::Text("R Type: %s", sr_type_name.c_str());
                  if (GetFormatName(sr_format) != nullptr)
                  {
                     ImGui::Text("R Format: %s", GetFormatName(sr_format));
                  }
                  else
                  {
                     ImGui::Text("R Format: %u", sr_format);
                  }
                  if (GetFormatName(srv_format) != nullptr)
                  {
                     ImGui::Text("RV Format: %s", GetFormatName(srv_format));
                  }
                  else
                  {
                     ImGui::Text("RV Format: %u", srv_format);
                  }
                  ImGui::Text("R Size: %ux%ux%ux%u", sr_size.x, sr_size.y, sr_size.z, sr_size.w);
                  ImGui::Text("RV Mip: %u", slot_data.srv_mip[i]);
                  ImGui::Text("RV Size: %ux%ux%u", srv_size.x, srv_size.y, srv_size.z);
                  ImGui::Text("R is RT: %s", sr_is_rt ? "True" : "False"); // TODO: add if they have CPU access, or immutable etc, and also whether they are a depth buffer (DSV)
                  ImGui::Text("R is UA: %s", sr_is_ua ? "True" : "False");
                  bool upgraded = false;
                  {
                     const std::shared_lock lock(device_data.mutex);
                     // TODO: store this information in the trace list, it might expire otherwise, or even be incorrect if ptrs were re-used. Also this info isn't shown if we use indirect texture upgrades.
                     for (auto upgraded_resource_pair : device_data.resource_upgrades.original_upgraded_resources_formats)
                     {
                        void* upgraded_resource_ptr = reinterpret_cast<void*>(upgraded_resource_pair.first);
                        if (sr_hash == std::to_string(std::hash<void*>{}(upgraded_resource_ptr)))
                        {
                           ImGui::Text("R: Direct Upgraded");

                           ImGui::Text("R Original Format: %s", GetFormatName(DXGI_FORMAT(upgraded_resource_pair.second)));

                           if (const auto it = device_data.resource_upgrades.original_upgraded_resource_views_formats.find(reinterpret_cast<uint64_t>(slot_data.srvs[i])); it != device_data.resource_upgrades.original_upgraded_resource_views_formats.end())
                           {
                              const auto& [native_resource, original_view_format] = it->second;
                              ASSERT_ONCE(native_resource == upgraded_resource_pair.first); // Uh!?

                              DXGI_FORMAT upgraded_view_format = slot_data.srv_format[i]; // This only works with direct upgrades, otherwise it'd be the original view format
                              // If the game already tried to create a view in the upgraded format, it means it simply read the format from the upgraded texture,
                              // and thus we can assume the original format would have been the same as the original texture (or anyway the most obvious non typeless version of it)
                              DXGI_FORMAT adjusted_original_view_format = (DXGI_FORMAT(original_view_format) == upgraded_view_format) ? DXGI_FORMAT(upgraded_resource_pair.second) : DXGI_FORMAT(original_view_format);

                              ImGui::Text("RV Original Format: %s", GetFormatName(adjusted_original_view_format));
                              // TODO: if the native texture format is TYPELESS, don't send this warning? Alternatively keep track of how the resource was last used (with what view it was written to, if any), and base the state off of that,
                              // then check the current state of the backbuffer and whether it's currently holding linear or gamma space colors (we don't store that anywhere atm, given it's not that simple).
                              // We could also send a message in case the upgraded format was float and the original format was not linear, but that's kinda obvious already (that the current color encoding might not be the most optimal).
                              if (IsLinearFormat(DXGI_FORMAT(upgraded_resource_pair.second)) != IsLinearFormat(DXGI_FORMAT(adjusted_original_view_format)))
                              {
                                 ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255)); // Red
                                 ImGui::Text("RV Gamma Change");
                                 ImGui::PopStyleColor();
                              }
                           }

                           upgraded = true;
                        
                           break;
                        }
                     }
                     for (auto upgraded_resource_pair : device_data.resource_upgrades.original_resources_to_mirrored_upgraded_resources)
                     {
                        void* upgraded_resource_ptr = reinterpret_cast<void*>(upgraded_resource_pair.first);
                        if (sr_hash == std::to_string(std::hash<void*>{}(upgraded_resource_ptr)))
                        {
                           if (upgraded_resource_pair.second.is_scaled)
                           {
                              ImGui::Text("R Indirect Upgraded Size: %ux%u", upgraded_resource_pair.second.mirror_width, upgraded_resource_pair.second.mirror_height);
                           }
                           ImGui::Text("R: Indirect Upgraded");
                           upgraded = true;
                           break;
                        }
                     }
                  }

                  const bool is_highlighted_resource = Trace::highlighted_resource == sr_hash;
                  if (is_highlighted_resource ? ImGui::Button("Unhighlight Resource") : ImGui::Button("Highlight Resource"))
                  {
                     Trace::highlighted_resource = is_highlighted_resource ? "" : sr_hash;
                  }

                  // TODO: hide the button if the resource is a buffer
                  if (debug_draw_shader_enabled && (Trace::debug_draw_mode != Trace::DebugDrawMode::ShaderResource || Trace::debug_draw_view_index != i) && ImGui::Button("Debug Draw Resource"))
                  {
                     if (Trace::debug_draw_mode == Trace::DebugDrawMode::Depth || Trace::debug_draw_mode == Trace::DebugDrawMode::Stencil)
                     {
                        Trace::debug_draw_options &= ~(uint32_t)Trace::DebugDrawTextureOptionsMask::RedOnly;
                     }
                     Trace::debug_draw_mode = Trace::DebugDrawMode::ShaderResource;
                     Trace::debug_draw_view_index = i;
                     Trace::debug_draw_mip = slot_data.srv_mip[i];
                  }

                  ImGui::PopID();
               }
            }

            if (has_ps || has_cs)
            {
               for (UINT i = 0; i < TraceDrawCallData::uavs_size; i++)
               {
                  const TraceDrawCallData& slot_data = SlotData(TraceDrawCallData::SlotKind::UAV, i);
                  auto uav_format = slot_data.uav_format[i];
                  if (uav_format == DXGI_FORMAT_UNKNOWN) // Resource was not valid
                  {
                     continue;
                  }
                  const bool non_referenced = uav_format == DXGI_FORMAT(-1);
                  if (Trace::ignore_non_bound_shader_referenced_resources && uav_format == DXGI_FORMAT(-1))
                  {
                     continue;
                  }
                  auto ua_format = slot_data.ua_format[i];
                  auto ua_size = slot_data.ua_size[i];
                  auto uav_size = slot_data.uav_size[i];
                  auto ua_hash = slot_data.ua_hash[i];
                  auto ua_type_name = slot_data.ua_type_name[i];
                  auto ua_is_rt = slot_data.ua_is_rt[i];

                  ImGui::PushID(i + TraceDrawCallData::srvs_size); // Offset by the max amount of previous iterations from above

                  if (!is_first_draw) { ImGui::Text(""); };
                  is_first_draw = false;
                  ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(200, 170, 230, 255)); // Faint Purple
                  ImGui::Text("UAV Index: %u", i);
                  ImGui::PopStyleColor();
                  DrawSlotChange(TraceDrawCallData::SlotKind::UAV, i);
                  if (non_referenced)
                  {
                     ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255)); // Red
                     ImGui::Text("R Referenced but Not Bound");
                     ImGui::PopStyleColor();
                     continue;
                  }
                  ImGui::Text("R Hash: %s", ua_hash.c_str());
                  if (!slot_data.ua_debug_name[i].empty())
                     ImGui::Text("R Debug Name: %s", slot_data.ua_debug_name[i].c_str());
                  ImGui::Text("R Type: %s", ua_type_name.c_str());
                  if (GetFormatName(ua_format) != nullptr)
                  {
                     ImGui::Text("R Format: %s", GetFormatName(ua_format));
                  }
                  else
                  {
                     ImGui::Text("R Format: %u", ua_format);
                  }
                  if (GetFormatName(uav_format) != nullptr)
                  {
                     ImGui::Text("RV Format: %s", GetFormatName(uav_format));
                  }
                  else
                  {
                     ImGui::Text("RV Format: %u", uav_format);
                  }
                  ImGui::Text("R Size: %ux%ux%ux%u", ua_size.x, ua_size.y, ua_size.z, ua_size.w);
                  ImGui::Text("RV Mip: %u", slot_data.uav_mip[i]);
                  ImGui::Text("RV Size: %ux%ux%u", uav_size.x, uav_size.y, uav_size.z);
                  ImGui::Text("R is RT: %s", ua_is_rt ? "True" : "False");
                  bool upgraded = false;
                  {
                     const std::shared_lock lock(device_data.mutex);
                     for (auto upgraded_resource_pair : device_data.resource_upgrades.original_upgraded_resources_formats)
                     {
                        void* upgraded_resource_ptr = reinterpret_cast<void*>(upgraded_resource_pair.first);
                        if (ua_hash == std::to_string(std::hash<void*>{}(upgraded_resource_ptr)))
                        {
                           ImGui::Text("R: Direct Upgraded");

                           ImGui::Text("R Original Format: %s", GetFormatName(DXGI_FORMAT(upgraded_resource_pair.second)));

                           if (const auto it = device_data.resource_upgrades.original_upgraded_resource_views_formats.find(reinterpret_cast<uint64_t>(slot_data.uavs[i])); it != device_data.resource_upgrades.original_upgraded_resource_views_formats.end())
                           {
                              const auto& [native_resource, original_view_format] = it->second;
                              ASSERT_ONCE(native_resource == upgraded_resource_pair.first); // Uh!?
                              DXGI_FORMAT upgraded_view_format = slot_data.uav_format[i]; // This only works with direct upgrades, otherwise it'd be the original view format
                              DXGI_FORMAT adjusted_original_view_format = (DXGI_FORMAT(original_view_format) == upgraded_view_format) ? DXGI_FORMAT(upgraded_resource_pair.second) : DXGI_FORMAT(original_view_format);
                              ImGui::Text("RV Original Format: %s", GetFormatName(adjusted_original_view_format));
                              if (IsLinearFormat(DXGI_FORMAT(upgraded_resource_pair.second)) != IsLinearFormat(adjusted_original_view_format))
                              {
                                 ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255)); // Red
                                 ImGui::Text("RV Gamma Change");
                                 ImGui::PopStyleColor();
                              }
                           }

                           upgraded = true;

                           break;
                        }
                     }
                     for (auto upgraded_resource_pair : device_data.resource_upgrades.original_resources_to_mirrored_upgraded_resources)
                     {
                        void* upgraded_resource_ptr = reinterpret_cast<void*>(upgraded_resource_pair.first);
                        if (ua_hash == std::to_string(std::hash<void*>{}(upgraded_resource_ptr)))
                        {
                           if (upgraded_resource_pair.second.is_scaled)
                           {
                              ImGui::Text("R Indirect Upgraded Size: %ux%u", upgraded_resource_pair.second.mirror_width, upgraded_resource_pair.second.mirror_height);
                           }
                           ImGui::Text("R: Indirect Upgraded");
                           upgraded = true;
                           break;
                        }
                     }
                  }

                  const bool is_highlighted_resource = Trace::highlighted_resource == ua_hash;
                  if (is_highlighted_resource ? ImGui::Button("Unhighlight Resource") : ImGui::Button("Highlight Resource"))
                  {
                     Trace::highlighted_resource = is_highlighted_resource ? "" : ua_hash;
                  }

                  if (pipeline && !upgraded && texture_format_upgrades_type == TextureFormatUpgradesType::AllowedEnabled && ImGui::Button("Indirect Upgrade Resource Format (By Shader)"))
                  {
                     auto_texture_format_upgrade_shader_hashes[pipeline->shader_hashes[0]] = AutoTextureFormatUpgradeShaderHash{ std::vector<uint8_t>(), std::vector<uint8_t>{ uint8_t(i) }, false }; // DX11 logic
                  }

                  if (debug_draw_shader_enabled && (Trace::debug_draw_mode != Trace::DebugDrawMode::UnorderedAccessView || Trace::debug_draw_view_index != i) && ImGui::Button("Debug Draw Resource"))
                  {
                     if (Trace::debug_draw_mode == Trace::DebugDrawMode::Depth || Trace::debug_draw_mode == Trace::DebugDrawMode::Stencil)
                     {
                        Trace::debug_draw_options &= ~(uint32_t)Trace::DebugDrawTextureOptionsMask::RedOnly;
                     }
                     Trace::debug_draw_mode = Trace::DebugDrawMode::UnorderedAccessView;
                     Trace::debug_draw_view_index = i;
                     Trace::debug_draw_mip = slot_data.uav_mip[i];
                  }

                  if (pipeline)
                  {
                     bool is_redirection_target = pipeline->redirect_data.target_type == CachedPipeline::RedirectData::RedirectTargetType::UAV && pipeline->redirect_data.target_index == i;
                     if (pipeline->redirect_data.source_type != CachedPipeline::RedirectData::RedirectSourceType::None && is_redirection_target && ImGui::Button("Disable Copy"))
                     {
                        pipeline->redirect_data.source_type = CachedPipeline::RedirectData::RedirectSourceType::None;
                        pipeline->redirect_data.target_type = CachedPipeline::RedirectData::RedirectTargetType::None;
                        pipeline->redirect_data.source_index = 0;
                        pipeline->redirect_data.target_index = 0;
                        is_redirection_target = false;
                     }
                     if ((pipeline->redirect_data.source_type != CachedPipeline::RedirectData::RedirectSourceType::SRV || !is_redirection_target) && ImGui::Button("Copy from SRV"))
                     {
                        pipeline->redirect_data.source_type = CachedPipeline::RedirectData::RedirectSourceType::SRV;
                        pipeline->redirect_data.target_type = CachedPipeline::RedirectData::RedirectTargetType::UAV;
                        pipeline->redirect_data.source_index = 0;
                        pipeline->redirect_data.target_index = i;
                        is_redirection_target = true;
                     }
                     if ((pipeline->redirect_data.source_type != CachedPipeline::RedirectData::RedirectSourceType::UAV || !is_redirection_target) && ImGui::Button("Copy from UAV"))
                     {
                        pipeline->redirect_data.source_type = CachedPipeline::RedirectData::RedirectSourceType::UAV;
                        pipeline->redirect_data.target_type = CachedPipeline::RedirectData::RedirectTargetType::UAV;
                        pipeline->redirect_data.source_index = 0;
                        pipeline->redirect_data.target_index = i;
                        is_redirection_target = true;
                     }
                     if (is_redirection_target)
                     {
                        ImGui::SliderInt("Copy from View Index", &pipeline->redirect_data.source_index, 0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT /*The largest allowed view count by type*/);
                     }
                  }

                  ImGui::PopID();
               }
            }

            if (has_ps)
            {
               auto blend_desc = draw_call_data.blend_desc;

               for (UINT i = 0; i < TraceDrawCallData::rtvs_size; i++)
               {
                  const TraceDrawCallData& slot_data = SlotData(TraceDrawCallData::SlotKind::RTV, i);
                  auto rtv_format = slot_data.rtv_format[i];
                  if (rtv_format == DXGI_FORMAT_UNKNOWN) // Resource was not valid
                  {
                     continue;
                  }
                  const bool non_referenced = rtv_format == DXGI_FORMAT(-1);
                  if (Trace::ignore_non_bound_shader_referenced_resources && rtv_format == DXGI_FORMAT(-1))
                  {
                     continue;
                  }
                  auto rt_format = slot_data.rt_format[i];
                  auto rt_size = slot_data.rt_size[i];
                  auto rtv_size = slot_data.rtv_size[i];
                  auto rt_hash = slot_data.rt_hash[i];
                  auto rt_type_name = slot_data.rt_type_name[i];

                  ImGui::PushID(i + TraceDrawCallData::srvs_size + TraceDrawCallData::uavs_size); // Offset by the max amount of previous iterations from above

                  if (!is_first_draw) { ImGui::Text(""); };
                  is_first_draw = false;
                  ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(160, 200, 255, 255)); // Faint Blue
                  ImGui::Text("RTV Index: %u", i);
                  ImGui::PopStyleColor();
                  DrawSlotChange(TraceDrawCallData::SlotKind::RTV, i);
                  if (non_referenced)
                  {
                     ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255)); // Red
                     ImGui::Text("R Referenced but Not Bound");
                     ImGui::PopStyleColor();
                     continue;
                  }
                  ImGui::Text("R Hash: %s", rt_hash.c_str());
                  if (!slot_data.rt_debug_name[i].empty())
                     ImGui::Text("R Debug Name: %s", slot_data.rt_debug_name[i].c_str());
                  ImGui::Text("R Type: %s", rt_type_name.c_str());
                  if (GetFormatName(rt_format) != nullptr)
                  {
                     ImGui::Text("R Format: %s", GetFormatName(rt_format));
                  }
                  else
                  {
                     ImGui::Text("R Format: %u", rt_format);
                  }
                  if (GetFormatName(rtv_format) != nullptr)
                  {
                     ImGui::Text("RV Format: %s", GetFormatName(rtv_format));
                  }
                  else
                  {
                     ImGui::Text("RV Format: %u", rtv_format);
                  }
                  ImGui::Text("R Size: %ux%ux%ux%u", rt_size.x, rt_size.y, rt_size.z, rt_size.w);
                  ImGui::Text("RV Mip: %u", slot_data.rtv_mip[i]);
                  ImGui::Text("RV Size: %ux%ux%u", rtv_size.x, rtv_size.y, rtv_size.z);
                  ImGui::Text("R is Swapchain: %s", slot_data.rt_is_swapchain[i] ? "True" : "False"); // TODO: add this for compute shaders / UAVs toos
                  bool upgraded = false;
                  bool indirect_upgraded = false;
                  {
                     const std::shared_lock lock(device_data.mutex);
                     // TODO: this is missing the "R is UAV" print
                     for (auto upgraded_resource_pair : device_data.resource_upgrades.original_upgraded_resources_formats)
                     {
                        void* upgraded_resource_ptr = reinterpret_cast<void*>(upgraded_resource_pair.first);
                        if (rt_hash == std::to_string(std::hash<void*>{}(upgraded_resource_ptr)))
                        {
                           ImGui::Text("R: Direct Upgraded");

                           ImGui::Text("R Original Format: %s", GetFormatName(DXGI_FORMAT(upgraded_resource_pair.second)));

                           // TODO: why does this flicker on and off in Deux Ex HR when it writes on the swapchain for material draws?
                           if (const auto it = device_data.resource_upgrades.original_upgraded_resource_views_formats.find(reinterpret_cast<uint64_t>(slot_data.rtvs[i])); it != device_data.resource_upgrades.original_upgraded_resource_views_formats.end())
                           {
                              const auto& [native_resource, original_view_format] = it->second;
                              ASSERT_ONCE(native_resource == upgraded_resource_pair.first); // Uh!?
                              DXGI_FORMAT upgraded_view_format = slot_data.rtv_format[i]; // This only works with direct upgrades, otherwise it'd be the original view format
                              DXGI_FORMAT adjusted_original_view_format = (DXGI_FORMAT(original_view_format) == upgraded_view_format) ? DXGI_FORMAT(upgraded_resource_pair.second) : DXGI_FORMAT(original_view_format);
                              ImGui::Text("RV Original Format: %s", GetFormatName(adjusted_original_view_format));
                              if (IsLinearFormat(DXGI_FORMAT(upgraded_resource_pair.second)) != IsLinearFormat(adjusted_original_view_format))
                              {
                                 ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255)); // Red
                                 ImGui::Text("RV Gamma Change");
                                 ImGui::PopStyleColor();
                              }
                           }

                           upgraded = true;

                           break;
                        }
                     }
                     for (auto upgraded_resource_pair : device_data.resource_upgrades.original_resources_to_mirrored_upgraded_resources)
                     {
                        void* upgraded_resource_ptr = reinterpret_cast<void*>(upgraded_resource_pair.first);
                        if (rt_hash == std::to_string(std::hash<void*>{}(upgraded_resource_ptr)))
                        {
                           if (upgraded_resource_pair.second.is_scaled)
                           {
                              ImGui::Text("R Indirect Upgraded Size: %ux%u", upgraded_resource_pair.second.mirror_width, upgraded_resource_pair.second.mirror_height);
                           }
                           ImGui::Text("R: Indirect Upgraded");
                           upgraded = true;
                           indirect_upgraded = true;
                           break;
                        }
                     }
                  }

                  // Blend mode
                  {
                     bool pop_text_style_color = false;
                     // Print out invalid blend modes.
                     // Note: this is assuming that indirect upgrades always upgrade to a signed float type, which is generally true. Ideally we'd implement something like "GetBestResourceViewUpgradeFormat(slot_data.rtv_format[i])" here, but given it's just a warning, it doesn't really matter
                     if (IsRGBAFormat(slot_data.rtv_format[i], true) && (indirect_upgraded || IsSignedFloatFormat(slot_data.rtv_format[i])) && IsBlendInverted(slot_data.blend_desc, 1, false, i))
                     {
                        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 105, 0, 255)); // Orange
                        pop_text_style_color = true;
                     }

                     const D3D11_RENDER_TARGET_BLEND_DESC1& render_target_blend_desc = blend_desc.IndependentBlendEnable ? blend_desc.RenderTarget[i] : blend_desc.RenderTarget[0];
                     // See "ui_data.blend_mode" for details on usage
                     if (render_target_blend_desc.BlendEnable)
                     {
                        bool has_drawn_blend_rgb_text = false;

                        if (render_target_blend_desc.SrcBlend == D3D11_BLEND::D3D11_BLEND_BLEND_FACTOR || render_target_blend_desc.DestBlend == D3D11_BLEND::D3D11_BLEND_BLEND_FACTOR
                           || render_target_blend_desc.SrcBlend == D3D11_BLEND::D3D11_BLEND_INV_BLEND_FACTOR || render_target_blend_desc.DestBlend == D3D11_BLEND::D3D11_BLEND_INV_BLEND_FACTOR)
                        {
                           ImGui::Text("Blend RGB Mode: Blend Factor (Any)");
                           has_drawn_blend_rgb_text = true;
                        }
                        else if (render_target_blend_desc.SrcBlend == D3D11_BLEND::D3D11_BLEND_ZERO && render_target_blend_desc.DestBlend == D3D11_BLEND::D3D11_BLEND_ZERO)
                        {
                           ImGui::Text("Blend RGB Mode: Zero (Override Color with Zero)");
                           has_drawn_blend_rgb_text = true;
                        }

                        if (!has_drawn_blend_rgb_text && render_target_blend_desc.BlendOp == D3D11_BLEND_OP::D3D11_BLEND_OP_ADD)
                        {
                           if (render_target_blend_desc.SrcBlend == D3D11_BLEND::D3D11_BLEND_ONE && render_target_blend_desc.DestBlend == D3D11_BLEND::D3D11_BLEND_ONE)
                           {
                              ImGui::Text("Blend RGB Mode: Additive Color");
                              has_drawn_blend_rgb_text = true;
                           }
                           else if (render_target_blend_desc.SrcBlend == D3D11_BLEND::D3D11_BLEND_SRC_ALPHA && render_target_blend_desc.DestBlend == D3D11_BLEND::D3D11_BLEND_ONE)
                           {
                              ImGui::Text("Blend RGB Mode: Additive Alpha");
                              has_drawn_blend_rgb_text = true;
                           }
                           else if (render_target_blend_desc.SrcBlend == D3D11_BLEND::D3D11_BLEND_SRC_ALPHA_SAT && render_target_blend_desc.DestBlend == D3D11_BLEND::D3D11_BLEND_ONE)
                           {
                              ImGui::Text("Blend RGB Mode: Additive Alpha (Saturated)");
                              has_drawn_blend_rgb_text = true;
                           }
                           else if (render_target_blend_desc.SrcBlend == D3D11_BLEND::D3D11_BLEND_ONE && render_target_blend_desc.DestBlend == D3D11_BLEND::D3D11_BLEND_INV_SRC_ALPHA)
                           {
                              ImGui::Text("Blend RGB Mode: Premultiplied Alpha"); // The alpha was supposedly (but not necessarily) pre-multiplied in the rgb before the pixel shader output
                              has_drawn_blend_rgb_text = true;
                           }
                           else if (render_target_blend_desc.SrcBlend == D3D11_BLEND::D3D11_BLEND_SRC_ALPHA && render_target_blend_desc.DestBlend == D3D11_BLEND::D3D11_BLEND_INV_SRC_ALPHA)
                           {
                              ImGui::Text("Blend RGB Mode: Straight Alpha");
                              has_drawn_blend_rgb_text = true;
                           }
                           else if (render_target_blend_desc.SrcBlend == D3D11_BLEND::D3D11_BLEND_SRC_ALPHA_SAT && render_target_blend_desc.DestBlend == D3D11_BLEND::D3D11_BLEND_INV_SRC_ALPHA)
                           {
                              ImGui::Text("Blend RGB Mode: Straight Alpha (Saturated)");
                              has_drawn_blend_rgb_text = true;
                           }
                           // Often used for lighting, glow, or compositing effects where the destination alpha controls how much of the source contributes
                           else if (render_target_blend_desc.SrcBlend == D3D11_BLEND::D3D11_BLEND_DEST_ALPHA && render_target_blend_desc.DestBlend == D3D11_BLEND::D3D11_BLEND_ONE)
                           {
                              ImGui::Text("Blend RGB Mode: Reverse Premultiplied Alpha");
                              has_drawn_blend_rgb_text = true;
                           }
                           else if (render_target_blend_desc.SrcBlend == D3D11_BLEND::D3D11_BLEND_DEST_COLOR && render_target_blend_desc.DestBlend == D3D11_BLEND::D3D11_BLEND_ZERO)
                           {
                              ImGui::Text("Blend RGB Mode: Multiplicative Color");
                              has_drawn_blend_rgb_text = true;
                           }
                           // It's enabled but it's as if it was disabled
                           else if (render_target_blend_desc.SrcBlend == D3D11_BLEND::D3D11_BLEND_ONE && render_target_blend_desc.DestBlend == D3D11_BLEND::D3D11_BLEND_ZERO)
                           {
                              ImGui::Text("Blend RGB Mode: Disabled");
                              has_drawn_blend_rgb_text = true;
                           }
                        }
                        // This subtracts the source from the target
                        else if (!has_drawn_blend_rgb_text && render_target_blend_desc.BlendOp == D3D11_BLEND_OP::D3D11_BLEND_OP_REV_SUBTRACT)
                        {
                           if (render_target_blend_desc.SrcBlend == D3D11_BLEND::D3D11_BLEND_ONE && render_target_blend_desc.DestBlend == D3D11_BLEND::D3D11_BLEND_ONE)
                           {
                              ImGui::Text("Blend RGB Mode: Subtractive Color");
                              has_drawn_blend_rgb_text = true;
                           }
                           else if (render_target_blend_desc.SrcBlend == D3D11_BLEND::D3D11_BLEND_SRC_ALPHA && render_target_blend_desc.DestBlend == D3D11_BLEND::D3D11_BLEND_ONE)
                           {
                              ImGui::Text("Blend RGB Mode: Subtractive Alpha");
                              has_drawn_blend_rgb_text = true;
                           }
                           else if (render_target_blend_desc.SrcBlend == D3D11_BLEND::D3D11_BLEND_SRC_ALPHA_SAT && render_target_blend_desc.DestBlend == D3D11_BLEND::D3D11_BLEND_ONE)
                           {
                              ImGui::Text("Blend RGB Mode: Subtractive Alpha (Saturated)");
                              has_drawn_blend_rgb_text = true;
                           }
                           else if (render_target_blend_desc.SrcBlend == D3D11_BLEND::D3D11_BLEND_ZERO && render_target_blend_desc.DestBlend == D3D11_BLEND::D3D11_BLEND_ONE)
                           {
                              ImGui::Text("Blend RGB Mode: Disabled");
                              has_drawn_blend_rgb_text = true;
                           }
                           else
                           {
                              ImGui::Text("Blend RGB Mode: Subtractive (Any)");
                              has_drawn_blend_rgb_text = true;
                           }
                        }

                        if (!has_drawn_blend_rgb_text)
                        {
                           ImGui::Text("Blend RGB Mode: Unknown");
                           has_drawn_blend_rgb_text = true;
                        }

                        if ((render_target_blend_desc.RenderTargetWriteMask & (D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE)) == 0)
                        {
                           ImGui::SameLine();
                           ImGui::Text(" (Ignored)");
                        }

                        // TODO: add "SampleMask" etc (stencil too, together with stencil debug draw, but that goes with depth)
                        ImGui::Text("Blend RGB Mode Details: (Src * %s) %s (Dest * %s)", GetBlendName(render_target_blend_desc.SrcBlend), GetBlendOpName(render_target_blend_desc.BlendOp), GetBlendName(render_target_blend_desc.DestBlend));

                        bool has_drawn_blend_a_text = false;

                        // It's enabled but it's as if it was disabled
                        if (render_target_blend_desc.SrcBlendAlpha == D3D11_BLEND::D3D11_BLEND_BLEND_FACTOR || render_target_blend_desc.DestBlendAlpha == D3D11_BLEND::D3D11_BLEND_BLEND_FACTOR
                           || render_target_blend_desc.SrcBlendAlpha == D3D11_BLEND::D3D11_BLEND_INV_BLEND_FACTOR || render_target_blend_desc.DestBlendAlpha == D3D11_BLEND::D3D11_BLEND_INV_BLEND_FACTOR)
                        {
                           ImGui::Text("Blend A Mode: Blend Factor (Any)");
                           has_drawn_blend_a_text = true;
                        }
                        if (render_target_blend_desc.SrcBlendAlpha == D3D11_BLEND::D3D11_BLEND_ZERO && render_target_blend_desc.DestBlendAlpha == D3D11_BLEND::D3D11_BLEND_ZERO)
                        {
                           ImGui::Text("Blend A Mode: Zero (Overwrite Alpha with Zero)");
                           has_drawn_blend_a_text = true;
                        }

                        if (!has_drawn_blend_a_text && render_target_blend_desc.BlendOpAlpha == D3D11_BLEND_OP::D3D11_BLEND_OP_ADD)
                        {
                           if (render_target_blend_desc.SrcBlendAlpha == D3D11_BLEND::D3D11_BLEND_SRC_ALPHA && render_target_blend_desc.DestBlendAlpha == D3D11_BLEND::D3D11_BLEND_INV_SRC_ALPHA)
                           {
                              ImGui::Text("Blend A Mode: Standard Transparency");
                              has_drawn_blend_a_text = true;
                           }
                           else if (render_target_blend_desc.SrcBlendAlpha == D3D11_BLEND::D3D11_BLEND_SRC_ALPHA_SAT && render_target_blend_desc.DestBlendAlpha == D3D11_BLEND::D3D11_BLEND_INV_SRC_ALPHA)
                           {
                              ImGui::Text("Blend A Mode: Standard Transparency (Saturated)");
                              has_drawn_blend_a_text = true;
                           }
                           else if (render_target_blend_desc.SrcBlendAlpha == D3D11_BLEND::D3D11_BLEND_DEST_ALPHA && render_target_blend_desc.DestBlendAlpha == D3D11_BLEND::D3D11_BLEND_ZERO)
                           {
                              ImGui::Text("Blend A Mode: Multiplicative");
                              has_drawn_blend_a_text = true;
                           }
                           else if (render_target_blend_desc.SrcBlendAlpha == D3D11_BLEND::D3D11_BLEND_ONE && render_target_blend_desc.DestBlendAlpha == D3D11_BLEND::D3D11_BLEND_ONE)
                           {
                              ImGui::Text("Blend A Mode: Additive");
                              has_drawn_blend_a_text = true;
                           }
                           else if (render_target_blend_desc.SrcBlendAlpha == D3D11_BLEND::D3D11_BLEND_ONE && render_target_blend_desc.DestBlendAlpha == D3D11_BLEND::D3D11_BLEND_ZERO)
                           {
                              ImGui::Text("Blend A Mode: Source Alpha (Overwrite Alpha, Blending Disabled)");
                              has_drawn_blend_a_text = true;
                           }
                           else if (render_target_blend_desc.SrcBlendAlpha == D3D11_BLEND::D3D11_BLEND_ZERO && render_target_blend_desc.DestBlendAlpha == D3D11_BLEND::D3D11_BLEND_ONE)
                           {
                              ImGui::Text("Blend A Mode: Destination Alpha (Preserve Alpha)");
                              has_drawn_blend_a_text = true;
                           }
                        }
                        else if (!has_drawn_blend_a_text && render_target_blend_desc.BlendOpAlpha == D3D11_BLEND_OP::D3D11_BLEND_OP_REV_SUBTRACT)
                        {
                           ImGui::Text("Blend A Mode: Subtractive (Any)");
                           has_drawn_blend_a_text = true;
                        }

                        if (!has_drawn_blend_a_text)
                        {
                           ImGui::Text("Blend A Mode: Unknown");
                           has_drawn_blend_a_text = true;
                        }

                        if ((render_target_blend_desc.RenderTargetWriteMask & D3D11_COLOR_WRITE_ENABLE_ALPHA) == 0)
                        {
                           ImGui::SameLine();
                           ImGui::Text("(Ignored)");
                        }

                        ImGui::Text("Blend A Mode Details: (Src * %s) %s (Dest * %s)", GetBlendName(render_target_blend_desc.SrcBlendAlpha), GetBlendOpName(render_target_blend_desc.BlendOpAlpha), GetBlendName(render_target_blend_desc.DestBlendAlpha));
                     }
                     else if (render_target_blend_desc.LogicOpEnable)
                     {
                        ImGui::Text("Logic Op Mode: %s", GetLogicOpName(render_target_blend_desc.LogicOp));
                     }
                     else
                     {
                        ImGui::Text("Blend and Logic Op Mode: Disabled");
                     }

                     if (pop_text_style_color)
                     {
                        ImGui::PopStyleColor();
                     }

                     // This applies even if blending is disabled!
                     if ((render_target_blend_desc.RenderTargetWriteMask & D3D11_COLOR_WRITE_ENABLE_ALL) == D3D11_COLOR_WRITE_ENABLE_ALL)
                     {
                        ImGui::Text("Write Mask: All");
                     }
                     else if ((render_target_blend_desc.RenderTargetWriteMask & D3D11_COLOR_WRITE_ENABLE_ALL) == 0)
                     {
                        ImGui::Text("Write Mask: None");
                     }
                     else
                     {
                        ImGui::Text("Write Mask:");
                        if (render_target_blend_desc.RenderTargetWriteMask & D3D11_COLOR_WRITE_ENABLE_RED)   ImGui::SameLine(), ImGui::Text(" R");
                        if (render_target_blend_desc.RenderTargetWriteMask & D3D11_COLOR_WRITE_ENABLE_GREEN) ImGui::SameLine(), ImGui::Text(" G");
                        if (render_target_blend_desc.RenderTargetWriteMask & D3D11_COLOR_WRITE_ENABLE_BLUE)  ImGui::SameLine(), ImGui::Text(" B");
                        if (render_target_blend_desc.RenderTargetWriteMask & D3D11_COLOR_WRITE_ENABLE_ALPHA) ImGui::SameLine(), ImGui::Text(" A");
                     }
                  }

                  // Show even if all blend modes are disabled, given that it might still be useful to track its state
                  ImGui::Text("Blend Factor: %f %f %f %f", slot_data.blend_factor[0], slot_data.blend_factor[1], slot_data.blend_factor[2], slot_data.blend_factor[3]);

                  const bool is_highlighted_resource = Trace::highlighted_resource == rt_hash;
                  if (is_highlighted_resource ? ImGui::Button("Unhighlight Resource") : ImGui::Button("Highlight Resource"))
                  {
                     Trace::highlighted_resource = is_highlighted_resource ? "" : rt_hash;
                  }

                  if (pipeline && !upgraded && texture_format_upgrades_type == TextureFormatUpgradesType::AllowedEnabled && ImGui::Button("Indirect Upgrade Resource Format (By Shader)"))
                  {
                     auto_texture_format_upgrade_shader_hashes[pipeline->shader_hashes[0]] = AutoTextureFormatUpgradeShaderHash{ std::vector<uint8_t>{ uint8_t(i) }, std::vector<uint8_t>(), false }; // DX11 logic
                  }

                  if (debug_draw_shader_enabled && (Trace::debug_draw_mode != Trace::DebugDrawMode::RenderTarget || Trace::debug_draw_view_index != i) && ImGui::Button("Debug Draw Resource"))
                  {
                     if (Trace::debug_draw_mode == Trace::DebugDrawMode::Depth || Trace::debug_draw_mode == Trace::DebugDrawMode::Stencil)
                     {
                        Trace::debug_draw_options &= ~(uint32_t)Trace::DebugDrawTextureOptionsMask::RedOnly;
                     }
                     Trace::debug_draw_mode = Trace::DebugDrawMode::RenderTarget;
                     Trace::debug_draw_view_index = i;
                     Trace::debug_draw_mip = slot_data.rtv_mip[i];
                  }

                  if (pipeline)
                  {
                     bool is_redirection_target = pipeline->redirect_data.target_type == CachedPipeline::RedirectData::RedirectTargetType::RTV && pipeline->redirect_data.target_index == i;
                     if (pipeline->redirect_data.source_type != CachedPipeline::RedirectData::RedirectSourceType::None && is_redirection_target && ImGui::Button("Disable Copy"))
                     {
                        pipeline->redirect_data.source_type = CachedPipeline::RedirectData::RedirectSourceType::None;
                        pipeline->redirect_data.target_type = CachedPipeline::RedirectData::RedirectTargetType::None;
                        pipeline->redirect_data.source_index = 0;
                        pipeline->redirect_data.target_index = 0;
                        is_redirection_target = false;
                     }
                     if ((pipeline->redirect_data.source_type != CachedPipeline::RedirectData::RedirectSourceType::SRV || !is_redirection_target) && ImGui::Button("Copy from SRV"))
                     {
                        pipeline->redirect_data.source_type = CachedPipeline::RedirectData::RedirectSourceType::SRV;
                        pipeline->redirect_data.target_type = CachedPipeline::RedirectData::RedirectTargetType::RTV;
                        pipeline->redirect_data.source_index = 0;
                        pipeline->redirect_data.target_index = i;
                        is_redirection_target = true;
                     }
                     if ((pipeline->redirect_data.source_type != CachedPipeline::RedirectData::RedirectSourceType::UAV || !is_redirection_target) && ImGui::Button("Copy from UAV"))
                     {
                        pipeline->redirect_data.source_type = CachedPipeline::RedirectData::RedirectSourceType::UAV;
                        pipeline->redirect_data.target_type = CachedPipeline::RedirectData::RedirectTargetType::RTV;
                        pipeline->redirect_data.source_index = 0;
                        pipeline->redirect_data.target_index = i;
                        is_redirection_target = true;
                     }
                     if (is_redirection_target)
                     {
                        ImGui::SliderInt("Copy from View Index", &pipeline->redirect_data.source_index, 0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT /*The largest allowed view count by type*/);
                     }
                  }

                  ImGui::PopID();
               }

               // Only print this when enabled given it's rare (it's MSAA transparency dithering basically)
               if (blend_desc.AlphaToCoverageEnable)
               {
                  if (!is_first_draw) { ImGui::Text(""); };
                  // Don't set "is_first_draw" to true to let depth printing have a space too
                  ImGui::Text("Alpha to Coverage Enable: %s", blend_desc.AlphaToCoverageEnable ? "True" : "False");
               }

               if (!is_first_draw) { ImGui::Text(""); }; // No views drew before, skip space
               is_first_draw = false;
               ImGui::Text("Depth State: %s", TraceDrawCallData::depth_state_names[(size_t)draw_call_data.depth_state]);
               ImGui::Text("Stencil State: %s", TraceDrawCallData::depth_state_names[(size_t)draw_call_data.stencil_state]);

               const TraceDrawCallData& dsv_data = SlotData(TraceDrawCallData::SlotKind::DSV, 0);
               if (dsv_data.dsv_format != DXGI_FORMAT_UNKNOWN && (dsv_data.depth_state != TraceDrawCallData::DepthStateType::Disabled && dsv_data.depth_state != TraceDrawCallData::DepthStateType::Invalid) || dsv_data.stencil_state != TraceDrawCallData::DepthStateType::Disabled)
               {
                  // Note: "ignore_non_bound_shader_referenced_resources" isn't implemented here

                  ImGui::PushID(TraceDrawCallData::rtvs_size + TraceDrawCallData::srvs_size + TraceDrawCallData::uavs_size); // Offset by the max amount of previous iterations from above

                  ImGui::Text("");
                  ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(190, 160, 120, 255)); // Faint Brown
                  ImGui::Text("Depth/Stencil");
                  ImGui::PopStyleColor();
                  DrawSlotChange(TraceDrawCallData::SlotKind::DSV, 0);
                  ImGui::Text("R Hash: %s", dsv_data.ds_hash.c_str());
                  if (!dsv_data.ds_debug_name.empty())
                     ImGui::Text("R Debug Name: %s", dsv_data.ds_debug_name.c_str());
                  if (GetFormatName(dsv_data.ds_format) != nullptr)
                  {
                     ImGui::Text("R Format: %s", GetFormatName(dsv_data.ds_format));
                  }
                  else
                  {
                     ImGui::Text("R Format: %u", dsv_data.ds_format);
                  }
                  if (GetFormatName(dsv_data.dsv_format) != nullptr)
                  {
                     ImGui::Text("RV Format: %s", GetFormatName(dsv_data.dsv_format));
                  }
                  else
                  {
                     ImGui::Text("RV Format: %u", dsv_data.dsv_format);
                  }
                  ImGui::Text("R Size: %ux%u", dsv_data.ds_size.x, dsv_data.ds_size.y); // Should match all the Render Targets size
                  {
                     const std::shared_lock lock(device_data.mutex);
                     for (uint64_t upgraded_resource : device_data.resource_upgrades.upgraded_resources)
                     {
                        void* upgraded_resource_ptr = reinterpret_cast<void*>(upgraded_resource);
                        if (dsv_data.ds_hash == std::to_string(std::hash<void*>{}(upgraded_resource_ptr)))
                        {
                           ImGui::Text("R: Direct Upgraded");
                           break;
                        }
                     }
                     for (auto upgraded_resource_pair : device_data.resource_upgrades.original_resources_to_mirrored_upgraded_resources)
                     {
                        void* upgraded_resource_ptr = reinterpret_cast<void*>(upgraded_resource_pair.first);
                        if (dsv_data.ds_hash == std::to_string(std::hash<void*>{}(upgraded_resource_ptr)))
                        {
                           if (upgraded_resource_pair.second.is_scaled)
                           {
                              ImGui::Text("R Indirect Upgraded Size: %ux%u", upgraded_resource_pair.second.mirror_width, upgraded_resource_pair.second.mirror_height);
                           }
                           ImGui::Text("R: Indirect Upgraded");
                           break;
                        }
                     }
                  }

                  const bool is_highlighted_resource = Trace::highlighted_resource == dsv_data.ds_hash;
                  if (is_highlighted_resource ? ImGui::Button("Unhighlight Resource") : ImGui::Button("Highlight Resource"))
                  {
                     Trace::highlighted_resource = is_highlighted_resource ? "" : dsv_data.ds_hash;
                  }

                  ImGui::PopID();
               }

               const bool has_valid_depth = draw_call_data.depth_state != TraceDrawCallData::DepthStateType::Disabled
                  && draw_call_data.depth_state != TraceDrawCallData::DepthStateType::Invalid;
               if (has_valid_depth && debug_draw_shader_enabled && Trace::debug_draw_mode != Trace::DebugDrawMode::Depth && ImGui::Button("Debug Draw Depth Resource"))
               {
                  Trace::debug_draw_mode = Trace::DebugDrawMode::Depth;
                  Trace::debug_draw_view_index = 0;
                  Trace::debug_draw_options |= (uint32_t)Trace::DebugDrawTextureOptionsMask::RedOnly;
                  Trace::debug_draw_mip = 0;
               }
               const bool has_valid_stencil = draw_call_data.stencil_state != TraceDrawCallData::DepthStateType::Disabled && draw_call_data.stencil_state != TraceDrawCallData::DepthStateType::Invalid;
               if (has_valid_stencil && debug_draw_shader_enabled && Trace::debug_draw_mode != Trace::DebugDrawMode::Stencil && ImGui::Button("Debug Draw Stencil Resource"))
               {
                  Trace::debug_draw_mode = Trace::DebugDrawMode::Stencil;
                  Trace::debug_draw_view_index = 0;
                  Trace::debug_draw_options |= (uint32_t)Trace::DebugDrawTextureOptionsMask::RedOnly;
                  Trace::debug_draw_mip = 0;
               }

               ImGui::Text("");
               ImGui::Text("Scissors Enabled: %s", draw_call_data.scissor_count >= 1 ? "True" : "False");
               const D3D11_VIEWPORT& viewport_0 = draw_call_data.viewports[0]; // Zeroed if none
               ImGui::Text("Viewport 0: x: %s y:%s w: %s h: %s",
                  std::to_string(viewport_0.TopLeftX).c_str(),
                  std::to_string(viewport_0.TopLeftY).c_str(),
                  std::to_string(viewport_0.Width).c_str(),
                  std::to_string(viewport_0.Height).c_str());
            }

            if (has_vs || has_ps || has_cs)
            {
               for (UINT i = 0; i < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT; i++)
               {
                  const TraceDrawCallData& slot_data = SlotData(TraceDrawCallData::SlotKind::Sampler, i);
                  if (int(slot_data.samplers_filter[i]) >= 0)
                  {
                     ImGui::PushID(i + TraceDrawCallData::srvs_size + TraceDrawCallData::uavs_size + TraceDrawCallData::rtvs_size); // Offset by the max amount of previous iterations from above
                     ImGui::Text("");
                     ImGui::Text("Sampler Index: %u", i);
                     DrawSlotChange(TraceDrawCallData::SlotKind::Sampler, i);
                     ImGui::Text("Sampler Filter: %s", GetFilterName(slot_data.samplers_filter[i]));
                     ImGui::Text("Sampler Address U: %s", GetTextureAddressModeName(slot_data.samplers_address_u[i]));
                     ImGui::Text("Sampler Address V: %s", GetTextureAddressModeName(slot_data.samplers_address_v[i]));
                     ImGui::Text("Sampler Address W: %s", GetTextureAddressModeName(slot_data.samplers_address_w[i]));
                     ImGui::Text("Sampler Mip LOD bias: %f", slot_data.samplers_mip_lod_bias[i]);
                     ImGui::PopID();
                  }
               }

               for (UINT i = 0; i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; i++)
               {
                  const TraceDrawCallData& slot_data = SlotData(TraceDrawCallData::SlotKind::CB, i);
                  if (slot_data.cbs[i])
                  {
                     ImGui::PushID(i + TraceDrawCallData::srvs_size + TraceDrawCallData::uavs_size + TraceDrawCallData::rtvs_size + D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT); // Offset by the max amount of previous iterations from above
                     ImGui::Text("");
                     ImGui::Text("CB Index: %u", i);
                     DrawSlotChange(TraceDrawCallData::SlotKind::CB, i);
                     ImGui::Text("CB Hash: %s", slot_data.cb_hash[i].c_str());
                     if (slot_data.cb_num_constants[i] != 0)
                     {
                        const bool is_partial = slot_data.cb_first_constant[i] != 0 || slot_data.cb_num_constants[i] != D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT;
                        ImGui::Text("CB First Constant: %u%s", slot_data.cb_first_constant[i], is_partial ? " (partial)" : "");
                        ImGui::Text("CB Num Constants: %u", slot_data.cb_num_constants[i]);
                     }
                     const bool is_highlighted_resource = Trace::highlighted_resource == slot_data.cb_hash[i];
                     if (is_highlighted_resource ? ImGui::Button("Unhighlight Resource") : ImGui::Button("Highlight Resource"))
                     {
                        Trace::highlighted_resource = is_highlighted_resource ? "" : slot_data.cb_hash[i];
                     }
                     ImGui::PopID();
                  }
               }
            }
         };

         if (ImGui::BeginChild("##ShaderDetails", ImVec2(0, 0)))
         {
            ImGui::BeginDisabled(selected_index == -1);
            if (ImGui::BeginTabBar("##ShadersCodeTab", ImGuiTabBarFlags_None))
            {
               ImGui::PushID("##SettingsTabItem");
               const bool open_settings_tab_item = ImGui::BeginTabItem("Info & Settings");
               ImGui::PopID();
               if (open_settings_tab_item)
               {
                  CommandListData& cmd_list_data = *runtime->get_command_queue()->get_immediate_command_list()->get_private_data<CommandListData>();
                  const std::shared_lock lock_trace(cmd_list_data.mutex_trace);
                  if (selected_index >= 0 && cmd_list_data.trace_draw_calls_data.size() >= selected_index + 1)
                  {
                     auto& draw_call_data = cmd_list_data.trace_draw_calls_data.at(selected_index);
                     auto pipeline_handle = draw_call_data.pipeline_handle;
                     bool reload = false;
                     bool recompile = false;

                     {
                        std::unique_lock lock(s_mutex_generic);
                        const auto pipeline_pair = device_data.pipeline_cache_by_pipeline_handle.find(pipeline_handle);
                        CachedPipeline* const pipeline = pipeline_pair != device_data.pipeline_cache_by_pipeline_handle.end() ? pipeline_pair->second : nullptr;
                        const bool is_luma_pass = draw_call_data.type == TraceDrawCallData::TraceDrawCallType::Custom && draw_call_data.custom_stage != reshade::api::shader_stage(0);
                        if (pipeline || is_luma_pass)
                        {
                           if (ImGui::BeginChild("Settings and Info"))
                           {
                              // The game shader's replacement, or the Luma pass's native shader
                              CachedCustomShader* custom_shader = nullptr; // We probably don't need to lock "s_mutex_loading" for the duration of this read
                              {
                                 const std::shared_lock lock(s_mutex_loading);
                                 if (pipeline)
                                 {
                                    for (auto shader_hash : pipeline->shader_hashes)
                                    {
                                       auto custom_shader_pair = custom_shaders_cache.find(shader_hash);
                                       if (custom_shader_pair != custom_shaders_cache.end())
                                       {
                                          custom_shader = custom_shader_pair->second;
                                       }
                                    }
                                 }
                                 else if (const auto custom_shader_pair = custom_shaders_cache.find(Shader::ShiftHash32ToHash64(draw_call_data.custom_shader_hash)); custom_shader_pair != custom_shaders_cache.end())
                                 {
                                    custom_shader = custom_shader_pair->second;
                                 }
                              }

                              if (const auto definition = native_shaders_definitions.find(draw_call_data.custom_shader_hash); is_luma_pass && definition != native_shaders_definitions.end() && definition->second.file_name)
                                 ImGui::Text("Luma Shader: %s", definition->second.file_name);

                              if (pipeline)
                              {
                                 if (pipeline->HasPixelShader() || pipeline->HasComputeShader())
                                 {
                                    ImGui::Checkbox("Allow Shader Replace Draw NaNs", &allow_replace_draw_nans);
                                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                                       ImGui::SetTooltip("Avoids accidentally enabling \"Draw NaN\" below, which could \"corrupt\" the game's rendering \"forever\".");
                                    ImGui::SliderInt("Shader Replace Draw Type", reinterpret_cast<int*>(&pipeline->replace_draw_type), 0, IM_ARRAYSIZE(CachedPipeline::shader_replace_draw_type_names) - (allow_replace_draw_nans ? 1 : 2), CachedPipeline::shader_replace_draw_type_names[(size_t)pipeline->replace_draw_type], ImGuiSliderFlags_NoInput);
                                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                                       ImGui::SetTooltip("Affects all the instances of this shader.\nNote that \"Draw Purple\" (Magenta) or \"Draw NaN\" might not always work, if it doesn't, it will skip the shader anyway. With compute shaders, the written area might not match at all.\nBe careful with NaNs as they can break temporal passes until the resolution is changed.");

                                    if (pipeline->HasPixelShader())
                                    {
                                       ImGui::SliderInt("Shader Custom Depth/Stencil Type", reinterpret_cast<int*>(&pipeline->custom_depth_stencil), 0, IM_ARRAYSIZE(CachedPipeline::shader_custom_depth_stencil_type_names) - 1, CachedPipeline::shader_custom_depth_stencil_type_names[(size_t)pipeline->custom_depth_stencil], ImGuiSliderFlags_NoInput);
                                       if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                                          ImGui::SetTooltip("Forces a custom depth and stencil state. For example, ignoring depth read tests, or forcing depth writes, meaning the affected polygons will draw on top of everything else.");
                                    }
                                 }

                                 {
                                    // Ensure the string has enough capacity for editing (e.g. 256 chars)
                                    if (pipeline->custom_name.capacity() < 128)
                                       pipeline->custom_name.resize(128);

                                    // ImGui::InputText modifies the buffer in-place
                                    if (ImGui::InputText("Shader Custom Name", pipeline->custom_name.data(), pipeline->custom_name.capacity()))
                                    {
                                       pipeline->custom_name.resize(strlen(pipeline->custom_name.c_str())); // Optional
                                    }
                                 }

                                 if (pipeline->cloned && ImGui::Button("Unload"))
                                 {
                                    pipeline->manually_unloaded = true;
                                    s_mutex_generic.unlock(); // Hack to avoid deadlocks with the device destroying pipelines // TODO: use "out_pipelines_to_destroy" instead (in all similar cases too)
                                    UnloadCustomShaders(device_data, { pipeline_handle }, false);
                                    s_mutex_generic.lock();
                                 }

                                 if (ImGui::Button(pipeline->cloned ? "Recompile" : "Load"))
                                 {
                                    pipeline->manually_unloaded = false;
                                    reload = true;
                                    recompile = true; // If this shader wasn't cloned, we'd need to compile it probably as it might not have already been compiled. If it was cloned, then our intent is to re-compile it anyway
                                 }
                                 if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                                 {
                                    ImGui::SetTooltip("Recompile and/or load/unload the custom shader that replaces the original one.");
                                 }

                                 bool copy_shader_hash = false;
                                 bool copy_shader_hash_exclude_0x = true;
                                 copy_shader_hash |= ImGui::Button("Copy Shader Hash to Clipboard");
                                 ImGui::SameLine();
                                 if (ImGui::Button("0x")) // Add a small button to copy it with the hash, it depends on the usage whether we need it or not
                                 {
                                    copy_shader_hash = true;
                                    copy_shader_hash_exclude_0x = false;
                                 }
                                 if (copy_shader_hash)
                                 {
                                    const std::shared_lock lock(s_mutex_loading);
                                    std::string shader_hash = (pipeline->shader_hashes.size() > 0) ? Shader::Hash_NumToStr(pipeline->shader_hashes[0]) : "????????";
                                    if (!copy_shader_hash_exclude_0x)
                                       shader_hash = "0x" + shader_hash; // Somehow we need to add "0x" in front of it manually // DX11 specific behaviour
                                    System::CopyToClipboard(shader_hash);
                                 }
                              }

                              if (custom_shader && custom_shader->is_hlsl && !custom_shader->file_path.empty() && ImGui::Button("Open hlsl in IDE"))
                              {
                                 // You may need to specify the full path to "code.exe" if it's not in PATH.
                                 HINSTANCE ret_val = ShellExecuteA(nullptr, "open", "code", ("\"" + custom_shader->file_path.string() + "\"").c_str(), nullptr, SW_SHOWNORMAL); // TODO: instruct users on how to use this (add "code" path to VS Code). // TODO: this fails with Mafia II DE...
                                 ASSERT_ONCE(ret_val > (HINSTANCE)32); // Unknown reason
                              }

                              // TODO: add opening the dumped shader file (e.g. "dumped_shaders")
                              if (custom_shader && !custom_shader->file_path.empty() && ImGui::Button("Open in Explorer"))
                              {
                                 System::OpenExplorerToFile(custom_shader->file_path);
                              }

                              bool debug_draw_shader_enabled = false; // Whether this pass instance is the one we are draw debugging
                              if (const auto debug_draw_target = Trace::FindPassTarget(device_data, cmd_list_data, selected_index, Trace::debug_draw_pipeline_instance_filter_by_thread))
                              {
                                 debug_draw_shader_enabled = Trace::IsDebugDrawTarget(*debug_draw_target);

                                 bool has_any_resources = draw_call_data.IsDSVValid();
                                 for (UINT i = 0; i < std::size(draw_call_data.srv_format); i++)
                                    has_any_resources |= draw_call_data.IsSRVValid(i);
                                 for (UINT i = 0; i < std::size(draw_call_data.uav_format); i++)
                                    has_any_resources |= draw_call_data.IsUAVValid(i);
                                 for (UINT i = 0; i < std::size(draw_call_data.rtv_format); i++)
                                    has_any_resources |= draw_call_data.IsRTVValid(i);

                                 // Note: yes, this can be done on vertex shaders too, as they might have resources!
                                 bool debug_draw_shader_just_enabled = false;
                                 // TODO: add a slider to scroll through all the draw calls quickly and show the RTVs etc, like Nsight. Or at least add Next, Prev buttons.
                                 if (has_any_resources && (debug_draw_shader_enabled ? ImGui::Button("Disable Debug Draw Shader Instance") : ImGui::Button("Debug Draw Shader Instance")))
                                 {
                                    ASSERT_ONCE(GetShaderDefineCompiledNumericalValue(DEVELOPMENT_HASH) >= 1); // Development flag is needed in shaders for this to output correctly
                                    ASSERT_ONCE(luma_data_cbuffer_index != -1); // Needed to pass data to the display composition shader
                                    ASSERT_ONCE(device_data.native_pixel_shaders[CompileTimeStringHash("Display Composition")]); // This shader is necessary to draw this debug stuff

                                    if (debug_draw_shader_enabled)
                                       Trace::ClearDebugDrawTarget(device_data);
                                    else
                                       Trace::SetDebugDrawTarget(device_data, *debug_draw_target, draw_call_data, false);
                                    debug_draw_shader_just_enabled = !debug_draw_shader_enabled;
                                    debug_draw_shader_enabled = !debug_draw_shader_enabled;
                                 }
                                 // Show that it's failing to retrieve the texture if we can!
                                 if (!Trace::debug_draw_auto_clear_texture && has_any_resources && debug_draw_shader_enabled && !debug_draw_shader_just_enabled && device_data.debug_draw_texture.get() == nullptr)
                                 {
                                    ImGui::BeginDisabled(true);
                                    ImGui::SameLine();
                                    ImGui::SmallButton(ICON_FK_WARNING);
                                    ImGui::EndDisabled();
                                 }
                                 // Luma passes are recorded after they drew, so their inputs can't be frozen nor their blends disabled
                                 if (has_any_resources && pipeline)
                                 {
                                    ImGui::SameLine();
                                    if (ImGui::Checkbox("Freeze Inputs", &Trace::debug_draw_freeze_inputs) && !Trace::debug_draw_freeze_inputs)
                                    {
                                       device_data.debug_draw_frozen_draw_state_stack.reset();
                                    }
                                    
                                    ImGui::SameLine();
                                    ImGui::BeginDisabled(Trace::debug_draw_mode != Trace::DebugDrawMode::RenderTarget);
                                    ImGui::Checkbox("Disable Blends", &Trace::debug_draw_disable_blends);
                                    ImGui::EndDisabled();
                                 }
                              }

                              const auto track_buffer_target = Trace::FindPassTarget(device_data, cmd_list_data, selected_index, false); // The tracking counts instances across threads
                              bool track_buffer_enabled = track_buffer_target && Trace::IsTrackBufferTarget(*track_buffer_target);
                              if (track_buffer_target && (track_buffer_enabled ? ImGui::Button("Disable Constant Buffer Tracking") : ImGui::Button("Enable Constant Buffer Tracking")))
                              {
                                 if (track_buffer_enabled)
                                    Trace::ClearTrackBufferTarget(device_data);
                                 else
                                    Trace::SetTrackBufferTarget(device_data, *track_buffer_target);
                                 track_buffer_enabled = !track_buffer_enabled;
                              }
                              if (track_buffer_enabled)
                              {
                                 draw_tracked_buffer(draw_call_data);
                              }
                              // Hacky: clear the data here...
                              else
                              {
                                 device_data.track_buffer_data.Clear();
                              }

                              ImGui::NewLine();
                              ImGui::Text("State Analysis:");
                              if (ImGui::BeginChild("StateAnalysisScroll", ImVec2(0, -FLT_MIN), ImGuiChildFlags_Borders)) // I prefer it without a separate scrolling box for now
                              {
                                 draw_state_analysis(draw_call_data, pipeline, debug_draw_shader_enabled);
                              }
                              ImGui::EndChild(); // StateAnalysisScroll
                           }
                           ImGui::EndChild(); // Settings and Info
                        }
                        lock.unlock(); // Needed to prevent "LoadCustomShaders()" from deadlocking, and anyway, there's no need to lock it beyond the for loop above

                        if (!pipeline && !is_luma_pass && (draw_call_data.type == TraceDrawCallData::TraceDrawCallType::CopyResource
                           || draw_call_data.type == TraceDrawCallData::TraceDrawCallType::CPURead
                           || draw_call_data.type == TraceDrawCallData::TraceDrawCallType::CPUWrite
                           || draw_call_data.type == TraceDrawCallData::TraceDrawCallType::BindResource
                           || draw_call_data.type == TraceDrawCallData::TraceDrawCallType::ClearResource
                           || draw_call_data.type == TraceDrawCallData::TraceDrawCallType::Custom))
                        {
                           if (ImGui::BeginChild("Settings and Info"))
                           {
                              const bool has_source_resource = draw_call_data.type == TraceDrawCallData::TraceDrawCallType::CopyResource
                                 || draw_call_data.type == TraceDrawCallData::TraceDrawCallType::CPURead
                                 || draw_call_data.type == TraceDrawCallData::TraceDrawCallType::BindResource;
                              const bool has_target_resource = draw_call_data.type != TraceDrawCallData::TraceDrawCallType::CPURead
                                 && draw_call_data.type != TraceDrawCallData::TraceDrawCallType::BindResource;

                              if (has_source_resource)
                              {
                                 ImGui::PushID(0);
                                 auto sr_format = draw_call_data.sr_format[0];
                                 auto sr_size = draw_call_data.sr_size[0];
                                 auto sr_hash = draw_call_data.sr_hash[0];
                                 auto sr_type_name = draw_call_data.sr_type_name[0];
                                 ImGui::Text("Source R Hash: %s", sr_hash.c_str());
                                 ImGui::Text("Source R Type: %s", sr_type_name.c_str());
                                 if (GetFormatName(sr_format) != nullptr)
                                 {
                                    ImGui::Text("Source R Format: %s", GetFormatName(sr_format));
                                 }
                                 ImGui::Text("Source R Size: %ux%ux%ux%u", sr_size.x, sr_size.y, sr_size.z, sr_size.w);
                                 for (uint64_t upgraded_resource : device_data.resource_upgrades.upgraded_resources)
                                 {
                                    void* upgraded_resource_ptr = reinterpret_cast<void*>(upgraded_resource);
                                    if (sr_hash == std::to_string(std::hash<void*>{}(upgraded_resource_ptr)))
                                    {
                                       ImGui::Text("Source R: Direct Upgraded");
                                       // TODO: add og resource format here
                                       break;
                                    }
                                 }
                                 for (auto upgraded_resource_pair : device_data.resource_upgrades.original_resources_to_mirrored_upgraded_resources)
                                 {
                                    void* upgraded_resource_ptr = reinterpret_cast<void*>(upgraded_resource_pair.first);
                                    if (sr_hash == std::to_string(std::hash<void*>{}(upgraded_resource_ptr)))
                                    {
                                       ImGui::Text("Source R: Indirect Upgraded");
                                       break;
                                    }
                                 }

                                 const bool is_highlighted_resource = Trace::highlighted_resource == sr_hash && (sr_type_name == "Buffer" || sr_format != DXGI_FORMAT_UNKNOWN); // Skip if invalid
                                 if (is_highlighted_resource ? ImGui::Button("Unhighlight Resource") : ImGui::Button("Highlight Resource"))
                                 {
                                    Trace::highlighted_resource = is_highlighted_resource ? "" : sr_hash;
                                 }
                                 ImGui::PopID();
                              }

                              if (has_target_resource)
                              {
                                 if (has_source_resource)
                                    ImGui::Text(""); // Empty line for spacing

                                 auto rt_format = draw_call_data.rt_format[0];
                                 auto rt_size = draw_call_data.rt_size[0];
                                 auto rt_hash = draw_call_data.rt_hash[0];
                                 auto rt_type_name = draw_call_data.rt_type_name[0];

                                 ImGui::PushID(1);
                                 ImGui::Text("Target R Hash: %s", rt_hash.c_str());
                                 ImGui::Text("Target R Type: %s", rt_type_name.c_str());
                                 if (GetFormatName(rt_format) != nullptr)
                                 {
                                    ImGui::Text("Target R Format: %s", GetFormatName(rt_format));
                                 }
                                 else
                                 {
                                    ImGui::Text("Target R Format: %u", rt_format);
                                 }
                                 ImGui::Text("Target R Size: %ux%ux%ux%u", rt_size.x, rt_size.y, rt_size.z, rt_size.w);
                                 for (uint64_t upgraded_resource : device_data.resource_upgrades.upgraded_resources)
                                 {
                                    void* upgraded_resource_ptr = reinterpret_cast<void*>(upgraded_resource);
                                    if (rt_hash == std::to_string(std::hash<void*>{}(upgraded_resource_ptr)))
                                    {
                                       ImGui::Text("Target R: Direct Upgraded");
                                       break;
                                    }
                                 }
                                 for (auto upgraded_resource_pair : device_data.resource_upgrades.original_resources_to_mirrored_upgraded_resources)
                                 {
                                    void* upgraded_resource_ptr = reinterpret_cast<void*>(upgraded_resource_pair.first);
                                    if (rt_hash == std::to_string(std::hash<void*>{}(upgraded_resource_ptr)))
                                    {
                                       ImGui::Text("Target R: Indirect Upgraded");
                                       break;
                                    }
                                 }
#if 0 // TODO: implement for this case (and above)
                                 ImGui::Text("Target R Swapchain: %s", draw_call_data.rt_is_swapchain[0] ? "True" : "False");
#endif

                                 const bool is_highlighted_resource = Trace::highlighted_resource == rt_hash && (rt_type_name == "Buffer" || rt_format != DXGI_FORMAT_UNKNOWN); // Skip if invalid
                                 if (is_highlighted_resource ? ImGui::Button("Unhighlight Resource") : ImGui::Button("Highlight Resource"))
                                 {
                                    Trace::highlighted_resource = is_highlighted_resource ? "" : rt_hash;
                                 }

                                 ImGui::PopID();
                              }

                              if (draw_call_data.type == TraceDrawCallData::TraceDrawCallType::ClearResource)
                              {
                                 if (has_target_resource || has_source_resource)
                                    ImGui::Text(""); // Empty line for spacing

                                 // TODO: split between uint uav and rt float and have special cases depth/stencil
                                 ImGui::Text("Clear Color: %f %f %f %f", draw_call_data.blend_factor[0], draw_call_data.blend_factor[1], draw_call_data.blend_factor[2], draw_call_data.blend_factor[3]);
                              }
                           }
                           ImGui::EndChild(); // Settings and Info
                        }

                        // We need to do this here or it'd deadlock due to "s_mutex_generic" trying to be locked in shared mod again
                        if (reload && pipeline_handle != 0)
                        {
                           LoadCustomShaders(device_data, { pipeline_handle }, recompile);
                        }
                     }
                  }

                  ImGui::EndTabItem(); // Settings
               }

               const bool open_disassembly_tab_item = ImGui::BeginTabItem("Disassembly");
               static bool opened_disassembly_tab_item = false;
               if (open_disassembly_tab_item)
               {
                  static bool pending_disassembly_refresh = false;

                  static const char* asm_types[] = { "Default", "Live Patched", "Custom" };
                  static int asm_type = 0;
                  if (ImGui::SliderInt("Type", &asm_type, 0, std::size(asm_types) - 1, asm_types[asm_type]))
                  {
                     pending_disassembly_refresh = true;
                  }

                  static std::string disasm_string;
                  CommandListData& cmd_list_data = *runtime->get_command_queue()->get_immediate_command_list()->get_private_data<CommandListData>();
                  const std::shared_lock lock_trace(cmd_list_data.mutex_trace);

                  bool refresh_disassembly = changed_selected || opened_disassembly_tab_item != open_disassembly_tab_item || pending_disassembly_refresh;
                  pending_disassembly_refresh = false;

                  if (selected_index >= 0 && cmd_list_data.trace_draw_calls_data.size() >= (selected_index + 1) && refresh_disassembly)
                  {
                     const auto pipeline_handle = cmd_list_data.trace_draw_calls_data.at(selected_index).pipeline_handle;
                     const std::unique_lock lock(s_mutex_generic);
                     if (auto pipeline_pair = device_data.pipeline_cache_by_pipeline_handle.find(pipeline_handle); pipeline_pair != device_data.pipeline_cache_by_pipeline_handle.end() && pipeline_pair->second != nullptr)
                     {
                        if (asm_type == 0 || asm_type == 1)
                        {
                           const std::lock_guard<std::recursive_mutex> lock_dumping(s_mutex_dumping);
                           auto* cache = (!pipeline_pair->second->shader_hashes.empty() && shader_cache.contains(pipeline_pair->second->shader_hashes[0])) ? shader_cache[pipeline_pair->second->shader_hashes[0]] : nullptr;
                           if (asm_type == 0 || !cache || !cache->live_patched_data)
                           {
                              if (cache && cache->disasm.empty())
                              {
                                 auto disasm_code = Shader::DisassembleShader(cache->data, cache->size);
                                 if (disasm_code.has_value())
                                 {
                                    cache->disasm.assign(disasm_code.value());
                                 }
                                 else
                                 {
                                    cache->disasm.assign("DISASSEMBLY FAILED");
                                 }
                              }
                              disasm_string.assign(cache ? cache->disasm : "");
                           }
                           else
                           {
                              if (cache && cache->live_patched_disasm.empty())
                              {
                                 auto disasm_code = Shader::DisassembleShader(cache->live_patched_data, cache->live_patched_size);
                                 if (disasm_code.has_value())
                                 {
                                    cache->live_patched_disasm.assign(disasm_code.value());
                                 }
                                 else
                                 {
                                    cache->live_patched_disasm.assign("DISASSEMBLY FAILED");
                                 }
                              }
                              disasm_string.assign(cache ? cache->live_patched_disasm : "");
                           }
                        }
                        else if (asm_type == 2)
                        {
                           const std::unique_lock lock_loading(s_mutex_loading);
                           auto* cache = (!pipeline_pair->second->shader_hashes.empty() && custom_shaders_cache.contains(pipeline_pair->second->shader_hashes[0])) ? custom_shaders_cache[pipeline_pair->second->shader_hashes[0]] : nullptr;
                           if (cache && cache->disasm.empty())
                           {
                              auto disasm_code = Shader::DisassembleShader(cache->code.data(), cache->code.size());
                              if (disasm_code.has_value())
                              {
                                 cache->disasm.assign(disasm_code.value());
                              }
                              else
                              {
                                 cache->disasm.assign("DISASSEMBLY FAILED");
                              }
                           }
                           disasm_string.assign(cache ? cache->disasm : "");
                        }
                     }
                     else if (const auto& entry = cmd_list_data.trace_draw_calls_data.at(selected_index); entry.custom_shader_hash != 0)
                     {
                        const std::unique_lock lock_loading(s_mutex_loading);
                        const auto custom_shader = custom_shaders_cache.find(Shader::ShiftHash32ToHash64(entry.custom_shader_hash));
                        auto* cache = custom_shader != custom_shaders_cache.end() ? custom_shader->second : nullptr;
                        if (cache && cache->disasm.empty())
                        {
                           auto disasm_code = Shader::DisassembleShader(cache->code.data(), cache->code.size());
                           cache->disasm.assign(disasm_code.has_value() ? disasm_code.value() : "DISASSEMBLY FAILED");
                        }
                        disasm_string.assign(cache ? cache->disasm : "");
                     }
                     else
                     {
                        disasm_string.clear();
                     }
                  }
                  // Force a refresh for later on
                  else if (refresh_disassembly)
                  {
                     pending_disassembly_refresh = true;
                     disasm_string.clear();
                  }

                  if (ImGui::BeginChild("DisassemblyCode"))
                  {
                     ImGui::InputTextMultiline(
                        "##disassemblyCode",
                        disasm_string.data(),
                        disasm_string.length() + 1, // Add the null terminator
                        ImVec2(-FLT_MIN, -FLT_MIN),
                        ImGuiInputTextFlags_ReadOnly);
                  }
                  ImGui::EndChild(); // DisassemblyCode
                  ImGui::EndTabItem(); // Disassembly
               }
               opened_disassembly_tab_item = open_disassembly_tab_item;

               ImGui::PushID("##LiveCodeTabItem");
               const bool open_live_tab_item = ImGui::BeginTabItem("HLSL");
               ImGui::PopID();
               static bool opened_live_tab_item = false;
               if (open_live_tab_item)
               {
                  static bool inline_includes = false;
                  bool inline_includes_toggled = ImGui::Checkbox("Inline Includes", &inline_includes);

                  static std::string hlsl_string;
                  static bool hlsl_error = false;
                  static bool hlsl_warning = false;
                  CommandListData& cmd_list_data = *runtime->get_command_queue()->get_immediate_command_list()->get_private_data<CommandListData>();
                  const std::shared_lock lock_trace(cmd_list_data.mutex_trace);
                  static bool pending_live_code_refresh = false;
                  bool refresh_live_code = changed_selected || inline_includes_toggled || opened_live_tab_item != open_live_tab_item || refresh_cloned_pipelines || pending_live_code_refresh;
                  pending_live_code_refresh = false;
                  if (selected_index >= 0 && cmd_list_data.trace_draw_calls_data.size() >= (selected_index + 1) && refresh_live_code)
                  {
                     bool hlsl_set = false;
                     const auto pipeline_handle = cmd_list_data.trace_draw_calls_data.at(selected_index).pipeline_handle;

                     const std::shared_lock lock(s_mutex_generic);
                     const auto pipeline_pair = device_data.pipeline_cache_by_pipeline_handle.find(pipeline_handle);
                     const bool has_pipeline = pipeline_pair != device_data.pipeline_cache_by_pipeline_handle.end() && pipeline_pair->second != nullptr;
                     const uint32_t luma_shader_hash = cmd_list_data.trace_draw_calls_data.at(selected_index).custom_shader_hash;
                     if (has_pipeline || luma_shader_hash != 0)
                     {
                        const std::shared_lock lock_loading(s_mutex_loading);
                        // The game shader's replacement, or a Luma pass's native shader
                        const auto custom_shader_it = has_pipeline ? (pipeline_pair->second->shader_hashes.empty() ? custom_shaders_cache.end() : custom_shaders_cache.find(pipeline_pair->second->shader_hashes[0]))
                           : custom_shaders_cache.find(Shader::ShiftHash32ToHash64(luma_shader_hash));
                        const auto custom_shader = custom_shader_it != custom_shaders_cache.end() ? custom_shader_it->second : nullptr;
                        // If the custom shader has a compilation error, print that, otherwise read the file text
                        if (custom_shader != nullptr && !custom_shader->compilation_errors.empty())
                        {
                           hlsl_string = custom_shader->compilation_errors;
                           hlsl_error = custom_shader->compilation_error;
                           hlsl_warning = !custom_shader->compilation_error;
                           hlsl_set = true;
                        }
                        else if (custom_shader != nullptr && custom_shader->is_hlsl && !custom_shader->file_path.empty())
                        {
                           if (inline_includes && !custom_shader->preprocessed_code.empty())
                           {
                              // Remove line breaks as there's a billion of them in the preprocessed code
                              std::istringstream in_stream(custom_shader->preprocessed_code);
                              std::ostringstream out_stream;
                              std::string line;
                              unsigned int empty_lines_count = 0;
                              while (std::getline(in_stream, line))
                              {
                                 // Skip lines that begin line "#line n" (unless they also include the name of a header, wrapped around "), for some reason the concatenated source file includes many of these.
                                 // Also skip pragmas as they aren't particularly relevant when viewing the source in a tiny box (this is arguable!).
                                 // Also skip any empty line beyond 2 ones, as usually it's empty spaces (or tabs) generated by the source concatenation.
                                 bool is_meta_line = line.rfind("#line ", 0) == 0 && !line.contains("\"");
                                 bool is_pragma_line = line.rfind("#pragma ", 0) == 0;
                                 if (is_meta_line || is_pragma_line || line.empty() || std::all_of(line.begin(), line.end(), [](unsigned char c) { return std::isspace(c); }))
                                 {
                                    empty_lines_count++;
                                 }
                                 else
                                 {
                                    empty_lines_count = 0;
                                 }

                                 if (!is_meta_line && !is_pragma_line && empty_lines_count <= 2)
                                 {
                                    out_stream << line << '\n';
                                 }
                              }
                              hlsl_string = out_stream.str();
                              hlsl_error = false;
                              hlsl_warning = false;
                              hlsl_set = true;
                           }
                           else
                           {
                              auto result = ReadTextFile(custom_shader->file_path);
                              if (result.has_value())
                              {
                                 hlsl_string.assign(result.value());
                                 hlsl_error = false;
                                 hlsl_warning = false;
                                 hlsl_set = true;
                              }
                           }
                           if (!hlsl_set)
                           {
                              hlsl_string.assign("FAILED TO READ FILE");
                              hlsl_error = true;
                              hlsl_warning = false;
                              hlsl_set = true;
                           }
                        }
                     }

                     if (!hlsl_set)
                     {
                        hlsl_string.clear();
                        hlsl_error = false;
                        hlsl_warning = false;
                     }
                  }
                  else if (refresh_live_code)
                  {
                     hlsl_string.clear();
                     hlsl_error = false;
                     hlsl_warning = false;
                     pending_live_code_refresh = true;
                  }
                  opened_live_tab_item = open_live_tab_item;

                  if (ImGui::BeginChild("LiveCode"))
                  {
                     ImGui::PushStyleColor(ImGuiCol_Text, hlsl_error ? IM_COL32(255, 0, 0, 255) : (hlsl_warning ? IM_COL32(255, 165, 0, 255) : IM_COL32(255, 255, 255, 255))); // Red for Error, Orange for Warning, White for the rest
                     ImGui::InputTextMultiline(
                        "##liveCode",
                        hlsl_string.data(),
                        hlsl_string.length() + 1, // Add the null terminator
                        ImVec2(-FLT_MIN, -FLT_MIN),
                        ImGuiInputTextFlags_ReadOnly);
                     ImGui::PopStyleColor();
                  }
                  ImGui::EndChild(); // LiveCode
                  ImGui::EndTabItem(); // HLSL
               }

               ImGui::EndTabBar(); // ShadersCodeTab
            }
            ImGui::EndDisabled();
         }
         ImGui::EndChild(); // ShaderDetails

         ImGui::EndTabItem(); // Captured Commands
      }
   }

   void DrawDebugDrawSettings(DeviceData& device_data)
   {
      ImGuiInputTextFlags text_flags = ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsNoBlank | ImGuiInputTextFlags_AlwaysOverwrite | ImGuiInputTextFlags_NoUndoRedo;
      if (ImGui::InputTextWithHint("Debug Draw Shader Hash", "12345678", Trace::debug_draw_shader_hash_string, HASH_CHARACTERS_LENGTH + 1, text_flags))
      {
         try
         {
            if (strlen(Trace::debug_draw_shader_hash_string) != HASH_CHARACTERS_LENGTH)
            {
               throw std::invalid_argument("Shader Hash has invalid length");
            }
            Trace::debug_draw_shader_hash = Shader::Hash_StrToNum(&Trace::debug_draw_shader_hash_string[0]);
         }
         catch (const std::exception& e)
         {
            Trace::debug_draw_shader_hash = 0;
         }
         // Keep the pipeline ptr if we are simply clearing the hash // TODO: why???
         if (Trace::debug_draw_shader_hash != 0)
         {
            Trace::debug_draw_pipeline = 0;
            Trace::debug_draw_custom_pass = 0;
         }

         device_data.debug_draw_frozen_draw_state_stack.reset();
         device_data.debug_draw_texture = nullptr;
         device_data.debug_draw_texture_format = DXGI_FORMAT_UNKNOWN;
         device_data.debug_draw_texture_size = {};
      }
      ImGui::SameLine();
      // Allow it to go through if we have a custom debug draw texture set
      bool debug_draw_enabled = Trace::debug_draw_shader_hash != 0 || Trace::debug_draw_pipeline != 0 || Trace::debug_draw_custom_pass != 0 || (Trace::debug_draw_mode == Trace::DebugDrawMode::Custom && device_data.debug_draw_texture.get()); // It wants to be shown (if it manages!)
      // Show the reset button for both conditions or they could get stuck
      if (debug_draw_enabled)
      {
         ImGui::PushID("Debug Draw");
         if (ImGui::SmallButton(ICON_FK_UNDO))
         {
            Trace::ClearDebugDrawTarget(device_data);
         }
         ImGui::PopID();
      }
      else
      {
         const auto& style = ImGui::GetStyle();
         ImVec2 size = ImGui::CalcTextSize(ICON_FK_UNDO);
         size.x += style.FramePadding.x;
         size.y += style.FramePadding.y;
         ImGui::InvisibleButton("", ImVec2(size.x, size.y));
      }
      if (debug_draw_enabled)
      {
         if (Trace::debug_draw_mode != Trace::DebugDrawMode::Custom)
         {
            auto prev_debug_draw_mode = Trace::debug_draw_mode;
            if (ImGui::SliderInt("Debug Draw Mode", &(int&)Trace::debug_draw_mode, int(Trace::DebugDrawMode::Custom) + 1, IM_ARRAYSIZE(Trace::debug_draw_mode_strings) - 1, Trace::debug_draw_mode_strings[(size_t)Trace::debug_draw_mode], ImGuiSliderFlags_NoInput))
            {
               // Make sure to reset it to 0 when we change mode, depth only supports 1 texture etc
               Trace::debug_draw_view_index = 0;
               // Automatically toggle some settings
               if (Trace::debug_draw_mode == Trace::DebugDrawMode::Depth || Trace::debug_draw_mode == Trace::DebugDrawMode::Stencil)
               {
                  Trace::debug_draw_options |= (uint32_t)Trace::DebugDrawTextureOptionsMask::RedOnly;
                  Trace::debug_draw_mip = 0;
               }
               else if (prev_debug_draw_mode == Trace::DebugDrawMode::Depth || prev_debug_draw_mode == Trace::DebugDrawMode::Stencil)
               {
                  Trace::debug_draw_options &= ~(uint32_t)Trace::DebugDrawTextureOptionsMask::RedOnly;
               }
            }
            if (ImGui::Checkbox("Debug Draw: Freeze Inputs", &Trace::debug_draw_freeze_inputs))
            {
               device_data.debug_draw_frozen_draw_state_stack.reset();
            }
            ImGui::Checkbox("Debug Draw: Disable Blends", &Trace::debug_draw_disable_blends);
         }
         if (Trace::debug_draw_mode == Trace::DebugDrawMode::RenderTarget)
         {
            ImGui::SliderInt("Debug Draw: Render Target Index", &Trace::debug_draw_view_index, 0, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT - 1);
         }
         else if (Trace::debug_draw_mode == Trace::DebugDrawMode::UnorderedAccessView)
         {
            ImGui::SliderInt("Debug Draw: Unordered Access View", &Trace::debug_draw_view_index, 0, device_data.uav_max_count - 1);
         }
         else if (Trace::debug_draw_mode == Trace::DebugDrawMode::ShaderResource)
         {
            ImGui::SliderInt("Debug Draw: Pixel Shader Resource Index", &Trace::debug_draw_view_index, 0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - 1);
         }
         const int max_mip = GetTextureMaxMipLevels(D3D11_REQ_TEXTURE1D_U_DIMENSION) - 1;
         ImGui::SliderInt("Debug Draw: Texture Mip", &Trace::debug_draw_mip, 0, max_mip);
         ImGui::Checkbox("Debug Draw: Allow Drawing Replaced Pass", &Trace::debug_draw_replaced_pass);
         ImGui::SliderInt("Debug Draw: Target Pipeline Instance", &Trace::debug_draw_pipeline_target_instance, -1, 100); // In case the same pipeline was run more than once by the game, we can pick one to print
         if (ImGui::Checkbox("Debug Draw: Filter Target Pipeline Instance By Thread", &Trace::debug_draw_pipeline_instance_filter_by_thread) && !Trace::debug_draw_pipeline_instance_filter_by_thread)
         {
            Trace::debug_draw_pipeline_target_thread = std::thread::id();
         }
         bool debug_draw_fullscreen = (Trace::debug_draw_options & (uint32_t)Trace::DebugDrawTextureOptionsMask::Fullscreen) != 0;
         bool debug_draw_rend_res_scale = (Trace::debug_draw_options & (uint32_t)Trace::DebugDrawTextureOptionsMask::RenderResolutionScale) != 0;
         bool debug_draw_red_only = (Trace::debug_draw_options & (uint32_t)Trace::DebugDrawTextureOptionsMask::RedOnly) != 0;
         bool debug_draw_show_alpha = (Trace::debug_draw_options & (uint32_t)Trace::DebugDrawTextureOptionsMask::ShowAlpha) != 0;
         bool debug_draw_premultiply_alpha = (Trace::debug_draw_options & (uint32_t)Trace::DebugDrawTextureOptionsMask::PreMultiplyAlpha) != 0;
         bool debug_draw_invert_colors = (Trace::debug_draw_options & (uint32_t)Trace::DebugDrawTextureOptionsMask::InvertColors) != 0;
         bool debug_draw_linear_to_gamma = (Trace::debug_draw_options & (uint32_t)Trace::DebugDrawTextureOptionsMask::LinearToGamma) != 0;
         bool debug_draw_gamma_to_linear = (Trace::debug_draw_options & (uint32_t)Trace::DebugDrawTextureOptionsMask::GammaToLinear) != 0;
         bool debug_draw_flip_y = (Trace::debug_draw_options & (uint32_t)Trace::DebugDrawTextureOptionsMask::FlipY) != 0;
         bool debug_draw_abs = (Trace::debug_draw_options & (uint32_t)Trace::DebugDrawTextureOptionsMask::Abs) != 0;
         bool debug_draw_saturate = (Trace::debug_draw_options & (uint32_t)Trace::DebugDrawTextureOptionsMask::Saturate) != 0;
         bool debug_draw_background_passthrough = (Trace::debug_draw_options & (uint32_t)Trace::DebugDrawTextureOptionsMask::BackgroundPassthrough) != 0;
         bool debug_draw_zoom_4x = (Trace::debug_draw_options & (uint32_t)Trace::DebugDrawTextureOptionsMask::Zoom4x) != 0;
         bool debug_draw_bilinear = (Trace::debug_draw_options & (uint32_t)Trace::DebugDrawTextureOptionsMask::Bilinear) != 0;
         bool debug_draw_srgb = (Trace::debug_draw_options & (uint32_t)Trace::DebugDrawTextureOptionsMask::SRGB) != 0;
         bool debug_draw_tonemap = (Trace::debug_draw_options & (uint32_t)Trace::DebugDrawTextureOptionsMask::Tonemap) != 0;
         bool debug_draw_uv_to_pixel_space = (Trace::debug_draw_options & (uint32_t)Trace::DebugDrawTextureOptionsMask::UVToPixelSpace) != 0;
         bool debug_draw_denormalize = (Trace::debug_draw_options & (uint32_t)Trace::DebugDrawTextureOptionsMask::Denormalize) != 0;

         // TODO: do these in a template function to shorten the code
         if (ImGui::Checkbox("Debug Draw Options: Fullscreen", &debug_draw_fullscreen))
         {
            if (debug_draw_fullscreen)
            {
               Trace::debug_draw_options |= (uint32_t)Trace::DebugDrawTextureOptionsMask::Fullscreen;
            }
            else
            {
               Trace::debug_draw_options &= ~(uint32_t)Trace::DebugDrawTextureOptionsMask::Fullscreen;
            }
         }
         if (ImGui::Checkbox("Debug Draw Options: Render Resolution Scale", &debug_draw_rend_res_scale))
         {
            if (debug_draw_rend_res_scale)
            {
               Trace::debug_draw_options |= (uint32_t)Trace::DebugDrawTextureOptionsMask::RenderResolutionScale;
            }
            else
            {
               Trace::debug_draw_options &= ~(uint32_t)Trace::DebugDrawTextureOptionsMask::RenderResolutionScale;
            }
         }
         if (ImGui::Checkbox("Debug Draw Options: Background Passthrough", &debug_draw_background_passthrough))
         {
            if (debug_draw_background_passthrough)
            {
               Trace::debug_draw_options |= (uint32_t)Trace::DebugDrawTextureOptionsMask::BackgroundPassthrough;
            }
            else
            {
               Trace::debug_draw_options &= ~(uint32_t)Trace::DebugDrawTextureOptionsMask::BackgroundPassthrough;
            }
         }
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         {
            ImGui::SetTooltip("True: Background passes through the edges\nFalse: forces Black outside of the debugged Texture range");
         }
         if (ImGui::Checkbox("Debug Draw Options: Show Alpha", &debug_draw_show_alpha))
         {
            if (debug_draw_show_alpha)
            {
               Trace::debug_draw_options |= (uint32_t)Trace::DebugDrawTextureOptionsMask::ShowAlpha;
            }
            else
            {
               Trace::debug_draw_options &= ~(uint32_t)Trace::DebugDrawTextureOptionsMask::ShowAlpha;
            }
         }
         ImGui::BeginDisabled(debug_draw_show_alpha); // Alpha takes over red in shaders, so disable red if alpha is on
         if (ImGui::Checkbox("Debug Draw Options: Red Only", &debug_draw_red_only))
         {
            if (debug_draw_red_only)
            {
               Trace::debug_draw_options |= (uint32_t)Trace::DebugDrawTextureOptionsMask::RedOnly;
            }
            else
            {
               Trace::debug_draw_options &= ~(uint32_t)Trace::DebugDrawTextureOptionsMask::RedOnly;
            }
         }
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         {
            ImGui::SetTooltip("Shows textures with only one channel (e.g. red, alpha, depth) as grey-scale instead of in red.");
         }
         ImGui::EndDisabled();
         if (ImGui::Checkbox("Debug Draw Options: Premultiply Alpha", &debug_draw_premultiply_alpha))
         {
            if (debug_draw_premultiply_alpha)
            {
               Trace::debug_draw_options |= (uint32_t)Trace::DebugDrawTextureOptionsMask::PreMultiplyAlpha;
            }
            else
            {
               Trace::debug_draw_options &= ~(uint32_t)Trace::DebugDrawTextureOptionsMask::PreMultiplyAlpha;
            }
         }
         if (ImGui::Checkbox("Debug Draw Options: Invert Colors", &debug_draw_invert_colors))
         {
            if (debug_draw_invert_colors)
            {
               Trace::debug_draw_options |= (uint32_t)Trace::DebugDrawTextureOptionsMask::InvertColors;
            }
            else
            {
               Trace::debug_draw_options &= ~(uint32_t)Trace::DebugDrawTextureOptionsMask::InvertColors;
            }
         }
         ImGui::Checkbox("Debug Draw Options: Auto Gamma", &Trace::debug_draw_auto_gamma);
         if (!Trace::debug_draw_auto_gamma)
         {
            // Draw this first as it's much more likely to be needed
            if (ImGui::Checkbox("Debug Draw Options: Gamma to Linear", &debug_draw_gamma_to_linear))
            {
               if (debug_draw_gamma_to_linear)
               {
                  Trace::debug_draw_options |= (uint32_t)Trace::DebugDrawTextureOptionsMask::GammaToLinear;
               }
               else
               {
                  Trace::debug_draw_options &= ~(uint32_t)Trace::DebugDrawTextureOptionsMask::GammaToLinear;
               }
            }
            if (ImGui::Checkbox("Debug Draw Options: Linear to Gamma", &debug_draw_linear_to_gamma))
            {
               if (debug_draw_linear_to_gamma)
               {
                  Trace::debug_draw_options |= (uint32_t)Trace::DebugDrawTextureOptionsMask::LinearToGamma;
               }
               else
               {
                  Trace::debug_draw_options &= ~(uint32_t)Trace::DebugDrawTextureOptionsMask::LinearToGamma;
               }
            }
         }
         ImGui::BeginDisabled((Trace::debug_draw_options & ((uint32_t)Trace::DebugDrawTextureOptionsMask::LinearToGamma | (uint32_t)Trace::DebugDrawTextureOptionsMask::GammaToLinear)) == 0);
         if (ImGui::Checkbox("Debug Draw Options: sRGB Encode/Decode", &debug_draw_srgb))
         {
            if (debug_draw_srgb)
            {
               Trace::debug_draw_options |= (uint32_t)Trace::DebugDrawTextureOptionsMask::SRGB;
            }
            else
            {
               Trace::debug_draw_options &= ~(uint32_t)Trace::DebugDrawTextureOptionsMask::SRGB;
            }
         }
         ImGui::EndDisabled();
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         {
            ImGui::SetTooltip("Force sRGB \"Gamma\" conversions (as opposed to a generic 2.2 power gamma)");
         }
         if (ImGui::Checkbox("Debug Draw Options: Flip Y", &debug_draw_flip_y))
         {
            if (debug_draw_flip_y)
            {
               Trace::debug_draw_options |= (uint32_t)Trace::DebugDrawTextureOptionsMask::FlipY;
            }
            else
            {
               Trace::debug_draw_options &= ~(uint32_t)Trace::DebugDrawTextureOptionsMask::FlipY;
            }
         }
         if (ImGui::Checkbox("Debug Draw Options: Abs", &debug_draw_abs))
         {
            if (debug_draw_abs)
            {
               Trace::debug_draw_options |= (uint32_t)Trace::DebugDrawTextureOptionsMask::Abs;
            }
            else
            {
               Trace::debug_draw_options &= ~(uint32_t)Trace::DebugDrawTextureOptionsMask::Abs;
            }
         }
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         {
            ImGui::SetTooltip("Useful for Motion Vectors or debugging film grain etc");
         }
         if (ImGui::Checkbox("Debug Draw Options: Saturate", &debug_draw_saturate))
         {
            if (debug_draw_saturate)
            {
               Trace::debug_draw_options |= (uint32_t)Trace::DebugDrawTextureOptionsMask::Saturate;
            }
            else
            {
               Trace::debug_draw_options &= ~(uint32_t)Trace::DebugDrawTextureOptionsMask::Saturate;
            }
         }
         ImGui::BeginDisabled(debug_draw_saturate || !IsFloatFormat(device_data.debug_draw_texture_format));
         if (debug_draw_saturate || !IsFloatFormat(device_data.debug_draw_texture_format))
         {
            debug_draw_tonemap = false; // Make sure to show it as false if it's disabled (though this might be confusing too, as the setting in the restored later)
         }
         if (ImGui::Checkbox("Debug Draw Options: Tonemap", &debug_draw_tonemap))
         {
            if (debug_draw_tonemap)
            {
               Trace::debug_draw_options |= (uint32_t)Trace::DebugDrawTextureOptionsMask::Tonemap;
            }
            else
            {
               Trace::debug_draw_options &= ~(uint32_t)Trace::DebugDrawTextureOptionsMask::Tonemap;
            }
         }
         ImGui::EndDisabled();
         if (ImGui::Checkbox("Debug Draw Options: UV to Pixel Space", &debug_draw_uv_to_pixel_space))
         {
            if (debug_draw_uv_to_pixel_space)
            {
               Trace::debug_draw_options |= (uint32_t)Trace::DebugDrawTextureOptionsMask::UVToPixelSpace;
            }
            else
            {
               Trace::debug_draw_options &= ~(uint32_t)Trace::DebugDrawTextureOptionsMask::UVToPixelSpace;
            }
         }
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         {
            ImGui::SetTooltip("Useful for Motion Vectors");
         }
         if (ImGui::Checkbox("Debug Draw Options: Denormalize", &debug_draw_denormalize))
         {
            if (debug_draw_denormalize)
            {
               Trace::debug_draw_options |= (uint32_t)Trace::DebugDrawTextureOptionsMask::Denormalize;
            }
            else
            {
               Trace::debug_draw_options &= ~(uint32_t)Trace::DebugDrawTextureOptionsMask::Denormalize;
            }
         }
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         {
            ImGui::SetTooltip("Converts from 0 to +1 back to -1 to +1, useful for Motion Vectors or other encoded textures");
         }
         if (ImGui::Checkbox("Debug Draw Options: Zoom 4x", &debug_draw_zoom_4x))
         {
            if (debug_draw_zoom_4x)
            {
               Trace::debug_draw_options |= (uint32_t)Trace::DebugDrawTextureOptionsMask::Zoom4x;
            }
            else
            {
               Trace::debug_draw_options &= ~(uint32_t)Trace::DebugDrawTextureOptionsMask::Zoom4x;
            }
         }
         if (ImGui::Checkbox("Debug Draw Options: Bilinear Sampling", &debug_draw_bilinear))
         {
            if (debug_draw_bilinear)
            {
               Trace::debug_draw_options |= (uint32_t)Trace::DebugDrawTextureOptionsMask::Bilinear;
            }
            else
            {
               Trace::debug_draw_options &= ~(uint32_t)Trace::DebugDrawTextureOptionsMask::Bilinear;
            }
         }
         if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
         {
            ImGui::SetTooltip("Bilinear instead of Nearest Neighbor");
         }
         if (device_data.debug_draw_texture || Trace::debug_draw_auto_clear_texture)
         {
            if (GetFormatName(device_data.debug_draw_texture_format) != nullptr)
            {
               ImGui::Text("Debug Draw Info: Texture (View) Format: %s", GetFormatName(device_data.debug_draw_texture_format));
            }
            else
            {
               ImGui::Text("Debug Draw Info: Texture (View) Format: %u", device_data.debug_draw_texture_format);
            }
            ImGui::Text("Debug Draw Info: Texture Size: %ux%ux%ux%u", device_data.debug_draw_texture_size.x, device_data.debug_draw_texture_size.y, device_data.debug_draw_texture_size.z, device_data.debug_draw_texture_size.w);
         }
         if (ImGui::Checkbox("Debug Draw: Auto Clear Texture", &Trace::debug_draw_auto_clear_texture)) // Is it persistent or not (in case the target texture stopped being found on newer frames). We could also "freeze" it and stop updating it, but we don't need that for now.
         {
            device_data.debug_draw_texture_format = DXGI_FORMAT_UNKNOWN;
            device_data.debug_draw_texture_size = {};
         }
      }
   }
} // namespace UI
