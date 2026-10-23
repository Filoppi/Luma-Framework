#pragma once

// Luma MCP backend (DEVELOPMENT only), included by "core.hpp" inside its anonymous namespace, after the dev globals it drives.
// A named pipe ("\\.\pipe\luma-mcp-<pid>") serves any number of clients, usually "Scripts/luma_mcp.py" (the stdio MCP bridge, one
// per agent session). Their requests share one queue and the game's state (knobs, the last trace).
// Requests run serially on the render thread from "OnPresent" (before display composition), so they share the dev UI's
// trace, debug draw and constant buffer tracking state without new locks in the draw hot paths, and a debug draw copy
// requested here is consumed before it could be composed on screen.
// Wire format, both ways: little endian u32 byte length + UTF-8 payload.
// Request: the tool name line, then "key=value" lines. Response: one JSON object with an "ok" field.
namespace Mcp
{
   class JsonWriter
   {
   public:
      std::string out;

      JsonWriter& BeginObject()
      {
         Separate();
         out += '{';
         comma = false;
         return *this;
      }
      JsonWriter& EndObject()
      {
         out += '}';
         comma = true;
         return *this;
      }
      JsonWriter& BeginArray()
      {
         Separate();
         out += '[';
         comma = false;
         return *this;
      }
      JsonWriter& EndArray()
      {
         out += ']';
         comma = true;
         return *this;
      }
      JsonWriter& Key(std::string_view key)
      {
         Separate();
         WriteString(key);
         out += ':';
         comma = false;
         return *this;
      }
      JsonWriter& Value(std::string_view value)
      {
         Separate();
         WriteString(value);
         comma = true;
         return *this;
      }
      // Otherwise pointers would pick the bool overload
      JsonWriter& Value(const char* value)
      {
         return Value(std::string_view(value ? value : ""));
      }
      JsonWriter& Value(bool value)
      {
         Separate();
         out += value ? "true" : "false";
         comma = true;
         return *this;
      }
      JsonWriter& Value(double value)
      {
         Separate();
         // JSON has no NaN/Inf literals
         if (std::isfinite(value))
            std::format_to(std::back_inserter(out), "{}", value);
         else
            std::format_to(std::back_inserter(out), "\"{}\"", value);
         comma = true;
         return *this;
      }
      template <typename T>
         requires std::is_integral_v<T> && (!std::is_same_v<T, bool>)
      JsonWriter& Value(T value)
      {
         Separate();
         out += std::to_string(value);
         comma = true;
         return *this;
      }
      template <typename T>
      JsonWriter& Field(std::string_view key, const T& value)
      {
         Key(key);
         return Value(value);
      }
      JsonWriter& Hash(std::string_view key, uint32_t hash)
      {
         return Field(key, Shader::Hash_NumToStr(hash, true));
      }
      JsonWriter& Format(std::string_view key, DXGI_FORMAT format)
      {
         return Field(key, GetFormatNameSafe(format));
      }
      // As UTF-8: "path::string()" uses the ANSI code page and throws on characters outside it
      JsonWriter& Path(std::string_view key, const std::filesystem::path& path)
      {
         const std::u8string utf8 = path.u8string();
         return Field(key, std::string_view(reinterpret_cast<const char*>(utf8.data()), utf8.size()));
      }

   private:
      bool comma = false;

      void Separate()
      {
         if (comma)
            out += ',';
      }
      void WriteString(std::string_view value)
      {
         out += '"';
         for (const char c : value)
         {
            switch (c)
            {
            case '"':
               out += "\\\"";
               break;
            case '\\':
               out += "\\\\";
               break;
            case '\n':
               out += "\\n";
               break;
            case '\r':
               out += "\\r";
               break;
            case '\t':
               out += "\\t";
               break;
            default:
               if (static_cast<unsigned char>(c) < 0x20)
                  out += std::format("\\u{:04x}", static_cast<unsigned char>(c));
               else
                  out += c;
            }
         }
         out += '"';
      }
   };

   // The dev UI's debug draw pass selection, overwritten by a readback and restored once the job finishes
   struct DebugDrawSelection
   {
      uint64_t pipeline;
      uint32_t shader_hash;
      int32_t target_instance;
      std::thread::id target_thread;
      DebugDrawMode mode;
      int32_t view_index;
      bool freeze_inputs;
      bool disable_blends;
      bool replaced_pass;

      static DebugDrawSelection Current()
      {
         return {debug_draw_pipeline, debug_draw_shader_hash, debug_draw_pipeline_target_instance, debug_draw_pipeline_target_thread, debug_draw_mode, debug_draw_view_index, debug_draw_freeze_inputs, debug_draw_disable_blends, debug_draw_replaced_pass};
      }
      void Apply() const
      {
         debug_draw_pipeline = pipeline;
         debug_draw_shader_hash = shader_hash;
         debug_draw_pipeline_target_instance = target_instance;
         debug_draw_pipeline_target_thread = target_thread;
         debug_draw_mode = mode;
         debug_draw_view_index = view_index;
         debug_draw_freeze_inputs = freeze_inputs;
         debug_draw_disable_blends = disable_blends;
         debug_draw_replaced_pass = replaced_pass;
      }
   };

   // The dev UI's constant buffer tracking selection, same as above
   struct TrackBufferSelection
   {
      uint64_t pipeline;
      int32_t target_instance;
      int32_t index;

      static TrackBufferSelection Current()
      {
         return {track_buffer_pipeline, track_buffer_pipeline_target_instance, track_buffer_index};
      }
      void Apply() const
      {
         track_buffer_pipeline = pipeline;
         track_buffer_pipeline_target_instance = target_instance;
         track_buffer_index = index;
      }
   };

   struct Job
   {
      std::string tool;
      std::unordered_map<std::string, std::string> args;
      mutable std::unordered_set<std::string> read_args; // To report the arguments no tool read (typos, renamed keys)
      JsonWriter result;
      std::string error;
      HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
      // Readbacks, written to disk by the pipe thread before it answers, so the render thread only pays for the copy
      std::vector<std::pair<std::filesystem::path, std::vector<char>>> files;

      // Multi frame state (render thread only)
      DeviceData* device_data = nullptr;
      uint32_t phase = 0;
      uint32_t frames = 0; // Captures done, or frames waited for a pass
      // The pass a readback targets (see "ResolveTarget")
      uint64_t pipeline_handle = 0;
      uint32_t shader_hash = 0;
      const char* stage = "";
      int32_t instance = 0;
      std::thread::id thread;
      std::vector<std::string> series; // Per frame results of multi frame reads
      struct HashCounts
      {
         const char* stage;
         std::vector<uint32_t> per_frame;
      };
      std::unordered_map<uint32_t, HashCounts> hash_counts; // Draws per shader hash over consecutive captures
      std::optional<DebugDrawSelection> saved_debug_draw;
      std::optional<TrackBufferSelection> saved_track_buffer;

      Job()
      {
         result.BeginObject().Field("ok", true);
      }
      ~Job()
      {
         CloseHandle(done);
      }

      const std::string* Arg(const char* key) const
      {
         read_args.emplace(key);
         const auto it = args.find(key);
         return it != args.end() && !it->second.empty() ? &it->second : nullptr;
      }
      int64_t IntArg(const char* key, int64_t default_value) const
      {
         const std::string* value = Arg(key);
         return value ? std::strtoll(value->c_str(), nullptr, 0) : default_value;
      }
      int64_t IntArg(const char* key, int64_t default_value, int64_t min, int64_t max) const
      {
         return std::clamp(IntArg(key, default_value), min, max);
      }
      bool BoolArg(const char* key, bool default_value) const
      {
         const std::string* value = Arg(key);
         return value ? (*value == "1" || *value == "true") : default_value;
      }
      // Accepts "0x1234ABCD" or "1234ABCD"
      std::optional<uint32_t> HashArg(const char* key) const
      {
         const std::string* value = Arg(key);
         if (!value)
            return std::nullopt;
         return uint32_t(std::strtoul(value->c_str(), nullptr, 16));
      }
   };

   std::mutex s_mutex_jobs;
   std::vector<std::shared_ptr<Job>> pending_jobs; // Guarded by "s_mutex_jobs"
   std::string active_tool;                        // Guarded by "s_mutex_jobs", the running job's, for the status and timeout replies
   std::atomic<bool> has_pending_jobs = false;     // Set under "s_mutex_jobs", read without it
   std::shared_ptr<Job> active_job;                // Render thread only
   std::thread server_thread;
   std::atomic<bool> server_stop = false;
   std::atomic<bool> server_running = false;
   std::atomic<int> connected_clients = 0; // Skips the per draw bookkeeping nobody would read
   // Written at every present, so "status" can answer from the pipe thread while the game doesn't present
   std::atomic<uint64_t> last_present_ms = 0; // GetTickCount64()
   std::atomic<uint32_t> last_frame_index = 0;
   std::wstring pipe_name;

   // Game dev values (toggles, tweakables, counters and textures) exposed to "luma_dev_values", registered by the game code.
   // Values are read and written on the render thread at present, where games usually swap their per frame counters.
   // Some are persisted user settings: writing them here bypasses the config, fine for A/B tests.
   enum class DevValueKind : uint8_t
   {
      Toggle,
      Float,
      Int,
      Counter, // Read only
      Texture, // Read with "luma_read_resource view=registered:<name>"
   };
   // Called with the validated value (on the render thread) instead of a plain write, for values whose change has side effects (a mirror
   // into the shader settings, a mode switch). Returns an error, or nothing.
   using DevValueSetter = std::function<std::string(DeviceData& device_data, double value)>;
   // Returns a texture, or a view of it, borrowed (null while it isn't allocated), see "MCP_GAME_TEXTURE"
   using DevTextureGetter = ID3D11DeviceChild* (*)(DeviceData & device_data);
   struct DevValue
   {
      std::string name;
      DevValueKind kind;
      void* value;
      const void* owner = nullptr; // Lets per device values be removed together (e.g. "&device_data")
      double min = -DBL_MAX;       // Range of floats and ints (games index arrays with the ints)
      double max = DBL_MAX;
      DevTextureGetter texture = nullptr;
      DevValueSetter set; // Optional
   };
   std::mutex s_mutex_dev_values;
   std::vector<DevValue> dev_values; // Guarded by "s_mutex_dev_values"

   // Needs "s_mutex_dev_values"
   DevValue* FindDevValue(std::string_view name)
   {
      const auto it = std::find_if(dev_values.begin(), dev_values.end(), [&](const DevValue& dev_value)
         { return dev_value.name == name; });
      return it != dev_values.end() ? &*it : nullptr;
   }
   void RegisterDevValue(DevValue dev_value)
   {
      const std::lock_guard lock(s_mutex_dev_values);
      std::erase_if(dev_values, [&](const DevValue& other)
         { return other.name == dev_value.name; });
      dev_values.push_back(std::move(dev_value));
   }
   void RegisterToggles(std::initializer_list<std::pair<const char*, bool*>> values)
   {
      for (const auto& [name, value] : values)
         RegisterDevValue({name, DevValueKind::Toggle, value});
   }
   // A toggle the shaders read as a 0/1 float from the settings buffer too (e.g. "&cb_luma_global_settings.GameSettings.LumaBloomEnable")
   void RegisterMirroredToggle(const char* name, bool* value, float* mirror)
   {
      RegisterDevValue({name, DevValueKind::Toggle, value, nullptr, -DBL_MAX, DBL_MAX, nullptr, [value, mirror](DeviceData&, double new_value)
         {
            *value = new_value != 0.0;
            *mirror = *value ? 1.f : 0.f;
            return std::string(); }});
   }
   struct FloatValue
   {
      const char* name;
      float* value;
      float min = -FLT_MAX;
      float max = FLT_MAX;
   };
   void RegisterValues(std::initializer_list<FloatValue> values)
   {
      for (const auto& [name, value, min, max] : values)
         RegisterDevValue({name, DevValueKind::Float, value, nullptr, min, max});
   }
   struct IntValue
   {
      const char* name;
      int* value;
      int min;
      int max;
      DevValueSetter set = {};
   };
   void RegisterInts(std::initializer_list<IntValue> values)
   {
      for (const auto& [name, value, min, max, set] : values)
         RegisterDevValue({name, DevValueKind::Int, value, nullptr, double(min), double(max), nullptr, set});
   }
   // Prefer the per frame copy of a counter (the value from the last finished frame) over the one being incremented
   void RegisterCounter(std::string name, const uint32_t* value, const void* owner = nullptr)
   {
      RegisterDevValue({std::move(name), DevValueKind::Counter, const_cast<uint32_t*>(value), owner}); // Never written
   }
   void RegisterCounters(std::initializer_list<std::pair<const char*, const uint32_t*>> values, const void* owner = nullptr)
   {
      for (const auto& [name, value] : values)
         RegisterCounter(name, value, owner);
   }
   // Luma's own textures: its passes run natively, outside the ReShade events the pass readback hooks. They are read at present, so they
   // hold their last content (usually this frame's).
   void RegisterTextures(std::initializer_list<std::pair<const char*, DevTextureGetter>> textures)
   {
      for (const auto& [name, get] : textures)
         RegisterDevValue({name, DevValueKind::Texture, nullptr, nullptr, -DBL_MAX, DBL_MAX, get});
   }
// A "RegisterTextures" entry for a member of the game's device data (a texture or a view of it), from where "GetGameDeviceData" is visible
#define MCP_GAME_TEXTURE(name, member) {name, [](DeviceData& device_data) -> ID3D11DeviceChild* { return GetGameDeviceData(device_data).member.get(); }}
   // "texture" is the game's code: call it without "s_mutex_dev_values"
   com_ptr<ID3D11Resource> GetTexture(DevTextureGetter texture, DeviceData& device_data)
   {
      com_ptr<ID3D11Resource> resource;
      ID3D11DeviceChild* child = texture(device_data);
      if (child && FAILED(child->QueryInterface(&resource)))
      {
         com_ptr<ID3D11View> view;
         if (SUCCEEDED(child->QueryInterface(&view)))
            view->GetResource(&resource);
      }
      return resource;
   }
   void Unregister(const void* owner)
   {
      const std::lock_guard lock(s_mutex_dev_values);
      std::erase_if(dev_values, [&](const DevValue& dev_value)
         { return dev_value.owner == owner; });
   }

   // Attaches a note to the draw being processed in the current capture (e.g. the game's motion vector decision), shown by "luma_trace_list".
   // Call it from "Game::OnDrawOrDispatch", where the shader entries of the current draw are the last ones of "cmd_list_data".
   // "value" is appended as "text=value" (formatted only while capturing, so it can be called on every draw).
   void Annotate(CommandListData& cmd_list_data, std::string_view text, std::optional<int64_t> value = std::nullopt)
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

   constexpr uint32_t readback_max_frames = 3; // A pass not hit within these frames is reported as missing

   template <size_t N>
   const char* EnumName(const char* const (&names)[N], size_t value)
   {
      return value < N && names[value][0] != '\0' ? names[value] : "invalid";
   }

   // In "TraceDrawCallData::TraceDrawCallType" and "TraceDrawCallData::CopyKind" order
   constexpr const char* trace_type_names[] = {"shader", "copy", "clear", "bind_pipeline", "bind_resource", "cpu_read", "cpu_write", "present", "create_command_list", "append_command_list", "reset_command_list", "flush_command_list", "custom"};
   constexpr const char* copy_kind_names[] = {"resource", "region", "resolve", "buffer_region"};

   const Shader::CachedPipeline* FindPipeline(const DeviceData& device_data, uint64_t pipeline_handle)
   {
      const auto it = device_data.pipeline_cache_by_pipeline_handle.find(pipeline_handle);
      return it != device_data.pipeline_cache_by_pipeline_handle.end() ? it->second : nullptr;
   }

   // In "Shader::CachedPipeline::Replacement" order
   constexpr const char* replacement_names[] = {"none", "file", "patch_inplace", "patch_sync", "patch_async"};
   const char* ReplacementName(const Shader::CachedPipeline& pipeline)
   {
      return replacement_names[size_t(pipeline.GetReplacement())];
   }
   bool IsReplaced(const Shader::CachedPipeline& pipeline)
   {
      return pipeline.GetReplacement() != Shader::CachedPipeline::Replacement::None;
   }

   // Needs "s_mutex_loading" (shared)
   const Shader::CachedCustomShader* FindCustomShader(uint32_t shader_hash)
   {
      const auto it = custom_shaders_cache.find(shader_hash);
      return it != custom_shaders_cache.end() ? it->second : nullptr;
   }

   void WriteCustomShader(JsonWriter* w, const Shader::CachedCustomShader& custom_shader, bool with_disasm)
   {
      w->Key("custom_shader").BeginObject();
      w->Path("file", custom_shader.file_path);
      w->Field("is_hlsl", custom_shader.is_hlsl);
      w->Field("is_luma_native", custom_shader.is_luma_native);
      w->Field("compilation_failed", custom_shader.compilation_error);
      w->Field("compilation_errors", custom_shader.compilation_errors);
      if (with_disasm)
         w->Field("disasm", custom_shader.disasm);
      w->EndObject();
   }

   bool IsDeferred(const TraceDrawCallData& entry)
   {
      return entry.command_list && entry.command_list->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED;
   }

   // Needs "s_mutex_generic" and "s_mutex_loading" (shared)
   void WriteTraceEntrySummary(JsonWriter* w, const DeviceData& device_data, const TraceDrawCallData& entry, size_t index)
   {
      w->Field("index", index);
      w->Field("type", EnumName(trace_type_names, size_t(entry.type)));
      if (IsDeferred(entry))
         w->Field("deferred", true);
      if (entry.type == TraceDrawCallData::TraceDrawCallType::Custom)
         w->Field("name", entry.custom_name);
      if (entry.type == TraceDrawCallData::TraceDrawCallType::Shader)
      {
         if (const auto* pipeline = FindPipeline(device_data, entry.pipeline_handle))
         {
            w->Field("stage", pipeline->StageName());
            w->Hash("hash", pipeline->shader_hashes[0]);
            w->Field("replaced", ReplacementName(*pipeline));
            if (!pipeline->custom_name.empty() && pipeline->custom_name[0] != '\0')
               w->Field("name", pipeline->custom_name.c_str());
            const auto* custom_shader = FindCustomShader(pipeline->shader_hashes[0]);
            if (custom_shader && !custom_shader->compilation_errors.empty())
               w->Field("compilation_failed", custom_shader->compilation_error);
         }
         else
         {
            w->Field("stage", "unknown"); // Destroyed since the capture
         }
         if (entry.clone_variant != Shader::ShaderVariant::Original)
            w->Field("variant", "patched");
      }
      std::string flags;
      if (entry.any_input_resources_format_upgraded)
         flags += '^';
      if (entry.any_input_resources_scaled)
         flags += '\\';
      if (entry.any_output_resources_scaled)
         flags += '/';
      if (entry.any_output_resources_format_upgraded)
         flags += 'v';
      if (!flags.empty())
         w->Field("upgrade_flags", flags);
      if (entry.IsRTVValid(0))
         w->Field("rt0", std::format("{} {}x{}", GetFormatNameSafe(entry.rtv_format[0]), entry.rtv_size[0].x, entry.rtv_size[0].y));
      else if (entry.type != TraceDrawCallData::TraceDrawCallType::Shader && !entry.rt_hash[0].empty()) // A whole resource target, see "WriteTraceEntryDetails"
         w->Field("rt0", std::format("{} {}x{}", GetFormatNameSafe(entry.rt_format[0]), entry.rt_size[0].x, entry.rt_size[0].y));
      if (entry.IsDSVValid() && entry.depth_state != TraceDrawCallData::DepthStateType::Disabled)
         w->Field("depth", EnumName(TraceDrawCallData::depth_state_names, size_t(entry.depth_state)));
      if (entry.type == TraceDrawCallData::TraceDrawCallType::CopyResource && entry.copy_info.kind != TraceDrawCallData::CopyKind::Resource)
         w->Field("copy_kind", EnumName(copy_kind_names, size_t(entry.copy_info.kind)));
      if (!entry.note.empty())
         w->Field("note", entry.note);
   }

   constexpr const char* comparison_func_names[] = {"", "NEVER", "LESS", "EQUAL", "LESS_EQUAL", "GREATER", "NOT_EQUAL", "GREATER_EQUAL", "ALWAYS"};
   constexpr const char* stencil_op_names[] = {"", "KEEP", "ZERO", "REPLACE", "INCR_SAT", "DECR_SAT", "INVERT", "INCR", "DECR"};
   constexpr const char* cull_mode_names[] = {"", "NONE", "FRONT", "BACK"};
   constexpr const char* fill_mode_names[] = {"", "", "WIREFRAME", "SOLID"};

   void WriteStencilFace(JsonWriter* w, const char* key, const D3D11_DEPTH_STENCILOP_DESC& face)
   {
      w->Key(key).BeginObject();
      w->Field("func", EnumName(comparison_func_names, face.StencilFunc)).Field("fail", EnumName(stencil_op_names, face.StencilFailOp));
      w->Field("depth_fail", EnumName(stencil_op_names, face.StencilDepthFailOp)).Field("pass", EnumName(stencil_op_names, face.StencilPassOp));
      w->EndObject();
   }

   // Pixel shader entries only, the trace doesn't record this state for other stages
   void WriteRenderState(JsonWriter* w, const TraceDrawCallData& entry)
   {
      const D3D11_DEPTH_STENCIL_DESC depth_stencil = entry.has_depth_stencil_state ? entry.depth_stencil_desc : CD3D11_DEPTH_STENCIL_DESC(D3D11_DEFAULT);
      w->Key("depth_stencil").BeginObject();
      w->Field("state_bound", entry.has_depth_stencil_state).Field("depth_enable", bool(depth_stencil.DepthEnable));
      w->Field("depth_write", depth_stencil.DepthWriteMask == D3D11_DEPTH_WRITE_MASK_ALL).Field("depth_func", EnumName(comparison_func_names, depth_stencil.DepthFunc));
      w->Field("stencil_enable", bool(depth_stencil.StencilEnable));
      if (depth_stencil.StencilEnable)
      {
         w->Field("stencil_ref", entry.stencil_ref).Field("read_mask", uint32_t(depth_stencil.StencilReadMask)).Field("write_mask", uint32_t(depth_stencil.StencilWriteMask));
         WriteStencilFace(w, "front", depth_stencil.FrontFace);
         WriteStencilFace(w, "back", depth_stencil.BackFace);
      }
      w->EndObject();

      const D3D11_RASTERIZER_DESC rasterizer = entry.has_rasterizer_state ? entry.rasterizer_desc : CD3D11_RASTERIZER_DESC(D3D11_DEFAULT);
      w->Key("rasterizer").BeginObject();
      w->Field("state_bound", entry.has_rasterizer_state).Field("cull", EnumName(cull_mode_names, rasterizer.CullMode)).Field("fill", EnumName(fill_mode_names, rasterizer.FillMode));
      w->Field("front_ccw", bool(rasterizer.FrontCounterClockwise)).Field("depth_bias", rasterizer.DepthBias).Field("slope_scaled_depth_bias", rasterizer.SlopeScaledDepthBias);
      w->Field("depth_clip", bool(rasterizer.DepthClipEnable)).Field("scissor_enable", bool(rasterizer.ScissorEnable)).Field("multisample", bool(rasterizer.MultisampleEnable));
      w->EndObject();

      w->Field("viewport_count", entry.viewport_count).Key("viewports").BeginArray();
      for (UINT i = 0; i < entry.viewport_count; i++)
      {
         const auto& viewport = entry.viewports[i];
         w->BeginArray().Value(viewport.TopLeftX).Value(viewport.TopLeftY).Value(viewport.Width).Value(viewport.Height).Value(viewport.MinDepth).Value(viewport.MaxDepth).EndArray();
      }
      w->EndArray();
      w->Field("scissor_count", entry.scissor_count).Key("scissor_rects").BeginArray();
      for (UINT i = 0; i < entry.scissor_count; i++)
      {
         const auto& rect = entry.scissor_rects[i];
         w->BeginArray().Value(rect.left).Value(rect.top).Value(rect.right).Value(rect.bottom).EndArray();
      }
      w->EndArray();
   }

   void WriteViews(JsonWriter* w, const char* key, size_t count, auto is_valid, auto write)
   {
      w->Key(key).BeginArray();
      for (size_t i = 0; i < count; i++)
      {
         if (!is_valid(i))
            continue;
         w->BeginObject().Field("slot", i);
         write(i);
         w->EndObject();
      }
      w->EndArray();
   }

   // Needs "s_mutex_generic" and "s_mutex_loading" (shared)
   void WriteTraceEntryDetails(JsonWriter* w, const DeviceData& device_data, const TraceDrawCallData& entry, size_t index)
   {
      WriteTraceEntrySummary(w, device_data, entry, index);
      w->Field("thread", entry.thread_id._Get_underlying_id());
      w->Field("patch_enabled", entry.patch_enabled);

      const auto* pipeline = entry.type == TraceDrawCallData::TraceDrawCallType::Shader ? FindPipeline(device_data, entry.pipeline_handle) : nullptr;
      if (pipeline)
      {
         w->Field("replace_draw_type", Shader::CachedPipeline::shader_replace_draw_type_names[size_t(pipeline->replace_draw_type)]);
         if (const auto* custom_shader = FindCustomShader(pipeline->shader_hashes[0]))
            WriteCustomShader(w, *custom_shader, false);
      }

      const auto& dd = entry.draw_dispatch_data;
      w->Key("draw").BeginObject();
      w->Field("vertex_count", dd.vertex_count).Field("instance_count", dd.instance_count).Field("first_vertex", dd.first_vertex).Field("first_instance", dd.first_instance);
      w->Field("index_count", dd.index_count).Field("first_index", dd.first_index).Field("vertex_offset", dd.vertex_offset).Field("indexed", dd.indexed);
      w->Key("dispatch").BeginArray().Value(dd.dispatch_count.x).Value(dd.dispatch_count.y).Value(dd.dispatch_count.z).EndArray();
      w->Field("indirect", dd.indirect);
      w->Field("topology", uint32_t(entry.primitive_topology));
      w->EndObject();

      w->Field("depth_state", EnumName(TraceDrawCallData::depth_state_names, size_t(entry.depth_state)));
      w->Field("stencil_state", EnumName(TraceDrawCallData::depth_state_names, size_t(entry.stencil_state)));
      if (pipeline && pipeline->HasPixelShader())
         WriteRenderState(w, entry);
      if (entry.type == TraceDrawCallData::TraceDrawCallType::CopyResource)
      {
         const auto& copy = entry.copy_info;
         w->Key("copy").BeginObject().Field("kind", EnumName(copy_kind_names, size_t(copy.kind))).Field("source_subresource", copy.source_subresource).Field("dest_subresource", copy.dest_subresource);
         w->Key("dest_offset").BeginArray().Value(copy.dest_offset.x).Value(copy.dest_offset.y).Value(copy.dest_offset.z).EndArray();
         if (copy.has_box)
            w->Key("source_box").BeginArray().Value(copy.box.left).Value(copy.box.top).Value(copy.box.front).Value(copy.box.right).Value(copy.box.bottom).Value(copy.box.back).EndArray();
         if (copy.kind == TraceDrawCallData::CopyKind::Resolve)
            w->Format("resolve_format", copy.resolve_format);
         w->EndObject();
      }

      const auto write_resource = [&](DXGI_FORMAT resource_format, DXGI_FORMAT view_format, const uint4& size, const uint3& view_size, UINT mip, const std::string& type, const std::string& hash, const std::string& debug_name)
      {
         w->Format("format", resource_format).Format("view_format", view_format);
         w->Key("size").BeginArray().Value(size.x).Value(size.y).Value(size.z).Value(size.w).EndArray();
         w->Key("view_size").BeginArray().Value(view_size.x).Value(view_size.y).Value(view_size.z).EndArray();
         w->Field("mip", mip).Field("type", type).Field("resource", hash);
         if (!debug_name.empty())
            w->Field("debug_name", debug_name);
      };

      // Entries without a shader (copies, clears, CPU writes, Luma's passes) record whole resources instead of views: the target in
      // "rt_*[0]" (copy destination) and the source in "sr_*[0]" (copy source, bound buffer)
      if (entry.type != TraceDrawCallData::TraceDrawCallType::Shader)
      {
         const auto write_whole_resource = [&](const char* key, DXGI_FORMAT format, const uint4& size, const std::string& type, const std::string& hash)
         {
            if (hash.empty())
               return;
            w->Key(key).BeginObject().Format("format", format);
            w->Key("size").BeginArray().Value(size.x).Value(size.y).Value(size.z).Value(size.w).EndArray();
            w->Field("type", type).Field("resource", hash).EndObject();
         };
         write_whole_resource("source", entry.sr_format[0], entry.sr_size[0], entry.sr_type_name[0], entry.sr_hash[0]);
         write_whole_resource("target", entry.rt_format[0], entry.rt_size[0], entry.rt_type_name[0], entry.rt_hash[0]);
      }

      WriteViews(w, "rtvs", TraceDrawCallData::rtvs_size, [&](size_t i)
         { return entry.IsRTVValid(i); }, [&](size_t i)
         {
         write_resource(entry.rt_format[i], entry.rtv_format[i], entry.rt_size[i], entry.rtv_size[i], entry.rtv_mip[i], entry.rt_type_name[i], entry.rt_hash[i], entry.rt_debug_name[i]);
         if (entry.rt_is_swapchain[i]) w->Field("swapchain", true);
         const auto& blend = entry.blend_desc.RenderTarget[entry.blend_desc.IndependentBlendEnable ? i : 0];
         w->Field("write_mask", uint32_t(blend.RenderTargetWriteMask));
         if (blend.BlendEnable)
         {
            w->Field("blend", std::format("{} {} {} / alpha {} {} {}", GetBlendName(blend.SrcBlend), GetBlendOpName(blend.BlendOp), GetBlendName(blend.DestBlend), GetBlendName(blend.SrcBlendAlpha), GetBlendOpName(blend.BlendOpAlpha), GetBlendName(blend.DestBlendAlpha)));
         } });
      w->Field("alpha_to_coverage", bool(entry.blend_desc.AlphaToCoverageEnable));
      // Clear entries store the clear values there (depth and stencil in the first two for depth clears)
      w->Key(entry.type == TraceDrawCallData::TraceDrawCallType::ClearResource ? "clear_values" : "blend_factor").BeginArray().Value(entry.blend_factor[0]).Value(entry.blend_factor[1]).Value(entry.blend_factor[2]).Value(entry.blend_factor[3]).EndArray();

      WriteViews(w, "srvs", TraceDrawCallData::srvs_size, [&](size_t i)
         { return entry.IsSRVValid(i); }, [&](size_t i)
         {
         write_resource(entry.sr_format[i], entry.srv_format[i], entry.sr_size[i], entry.srv_size[i], entry.srv_mip[i], entry.sr_type_name[i], entry.sr_hash[i], entry.sr_debug_name[i]);
         if (entry.sr_is_rt[i]) w->Field("is_rt", true);
         if (entry.sr_is_ua[i]) w->Field("is_ua", true); });
      WriteViews(w, "uavs", TraceDrawCallData::uavs_size, [&](size_t i)
         { return entry.IsUAVValid(i); }, [&](size_t i)
         {
         write_resource(entry.ua_format[i], entry.uav_format[i], entry.ua_size[i], entry.uav_size[i], entry.uav_mip[i], entry.ua_type_name[i], entry.ua_hash[i], entry.ua_debug_name[i]);
         if (entry.ua_is_rt[i]) w->Field("is_rt", true); });
      if (entry.IsDSVValid())
      {
         w->Key("dsv").BeginObject().Format("format", entry.ds_format).Format("view_format", entry.dsv_format);
         w->Key("size").BeginArray().Value(entry.ds_size.x).Value(entry.ds_size.y).EndArray();
         w->Field("resource", entry.ds_hash);
         if (!entry.ds_debug_name.empty())
            w->Field("debug_name", entry.ds_debug_name);
         w->EndObject();
      }
      WriteViews(w, "cbs", D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, [&](size_t i)
         { return entry.cbs[i]; }, [&](size_t i)
         {
         w->Field("resource", entry.cb_hash[i]);
         if (entry.cb_num_constants[i] != 0) w->Field("first_constant", entry.cb_first_constant[i]).Field("num_constants", entry.cb_num_constants[i]); });
      WriteViews(w, "samplers", D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT, [&](size_t i)
         { return entry.samplers_filter[i] != static_cast<D3D11_FILTER>(-1); }, [&](size_t i)
         {
         w->Field("filter", GetFilterName(entry.samplers_filter[i])).Field("filter_id", uint32_t(entry.samplers_filter[i]));
         w->Field("address", std::format("{} {} {}", GetTextureAddressModeName(entry.samplers_address_u[i]), GetTextureAddressModeName(entry.samplers_address_v[i]), GetTextureAddressModeName(entry.samplers_address_w[i])));
         w->Key("address_ids").BeginArray().Value(uint32_t(entry.samplers_address_u[i])).Value(uint32_t(entry.samplers_address_v[i])).Value(uint32_t(entry.samplers_address_w[i])).EndArray();
         w->Field("mip_lod_bias", entry.samplers_mip_lod_bias[i]); });

      if (!entry.input_layout_hash.empty())
      {
         w->Field("input_layout", entry.input_layout_hash);
         w->Key("input_formats").BeginArray();
         for (const DXGI_FORMAT format : entry.input_layouts_formats)
            w->Value(GetFormatNameSafe(format));
         w->EndArray();
      }
      if (!entry.vertex_buffer_hashes.empty())
      {
         w->Key("vertex_buffers").BeginArray();
         for (const std::string& hash : entry.vertex_buffer_hashes)
            w->Value(hash);
         w->EndArray();
      }
      if (!entry.index_buffer_hash.empty())
         w->Field("index_buffer", entry.index_buffer_hash).Format("index_format", entry.index_buffer_format).Field("index_offset", entry.index_buffer_offset);
   }

   // "luma_trace_list" filters, parsed once per request (a capture can have tens of thousands of entries).
   // The defaults hide what the dev UI hides by default: VS entries, bindings and buffer writes.
   struct TraceFilter
   {
      bool include_vs;
      bool include_bindings;
      bool include_buffer_writes;
      bool replaced_only;
      const std::string* type;
      std::optional<uint32_t> hash;
      const std::string* stage;
      const std::string* resource;

      bool Matches(const DeviceData& device_data, const TraceDrawCallData& entry) const
      {
         using Type = TraceDrawCallData::TraceDrawCallType;
         const Shader::CachedPipeline* pipeline = entry.type == Type::Shader ? FindPipeline(device_data, entry.pipeline_handle) : nullptr;
         if ((pipeline && pipeline->HasVertexShader() && !include_vs) || ((entry.type == Type::BindPipeline || entry.type == Type::BindResource) && !include_bindings) || (entry.type == Type::CPUWrite && entry.rt_type_name[0] == "Buffer" && !include_buffer_writes))
            return false;
         if ((type && *type != EnumName(trace_type_names, size_t(entry.type))) || (hash && (!pipeline || pipeline->shader_hashes[0] != *hash)) || (stage && (!pipeline || _stricmp(stage->c_str(), pipeline->StageName()) != 0)) || (replaced_only && (!pipeline || !IsReplaced(*pipeline))))
            return false;
         const auto has_resource = [&](const auto& hashes)
         { return std::ranges::find(hashes, *resource) != std::end(hashes); };
         return !resource || entry.ds_hash == *resource || has_resource(entry.rt_hash) || has_resource(entry.sr_hash) || has_resource(entry.ua_hash) || has_resource(entry.cb_hash);
      }
   };

   // Picks the pass a readback targets ("job.pipeline_handle", "shader_hash", "stage", "instance", "thread"), either from the last
   // capture ("index") or by "hash" + "instance" (Nth draw with that shader in a frame). Instances are counted like the runtime counter
   // the debug draw and constant buffer tracking use: across threads, except deferred passes, filtered by their recording thread.
   bool ResolveTarget(Job* job, DeviceData& device_data, CommandListData& cmd_list_data)
   {
      const std::shared_lock lock_generic(s_mutex_generic);
      const Shader::CachedPipeline* pipeline = nullptr;
      if (job->Arg("index"))
      {
         const std::shared_lock lock_trace(cmd_list_data.mutex_trace);
         const size_t index = size_t(job->IntArg("index", 0));
         if (trace_running || index >= cmd_list_data.trace_draw_calls_data.size())
         {
            job->error = std::format("Trace index {} is out of range (captured {}), run luma_trace_capture first", index, cmd_list_data.trace_draw_calls_data.size());
            return false;
         }
         const TraceDrawCallData& entry = cmd_list_data.trace_draw_calls_data[index];
         pipeline = entry.type == TraceDrawCallData::TraceDrawCallType::Shader ? FindPipeline(device_data, entry.pipeline_handle) : nullptr;
         if (!pipeline)
         {
            job->error = std::format("Trace index {} is not a shader pass with a live pipeline", index);
            return false;
         }
         const bool deferred = IsDeferred(entry);
         job->thread = deferred ? entry.thread_id : std::thread::id();
         job->instance = 0;
         for (size_t i = 0; i < index; i++)
         {
            const auto& other = cmd_list_data.trace_draw_calls_data[i];
            if (other.pipeline_handle == entry.pipeline_handle && (!deferred || other.thread_id == entry.thread_id))
               job->instance++;
         }
      }
      else if (const auto hash = job->HashArg("hash"))
      {
         const auto it = device_data.pipeline_caches_by_shader_hash.find(*hash);
         if (it == device_data.pipeline_caches_by_shader_hash.end() || it->second.empty())
         {
            job->error = std::format("Shader {} has no live pipeline", Shader::Hash_NumToStr(*hash, true));
            return false;
         }
         pipeline = *it->second.begin();
         job->instance = int32_t(job->IntArg("instance", 0));
      }
      else
      {
         job->error = "Pass either \"index\" (from luma_trace_list) or \"hash\" (+ optional \"instance\")";
         return false;
      }
      job->pipeline_handle = pipeline->pipeline.handle;
      job->shader_hash = pipeline->shader_hashes[0];
      job->stage = pipeline->StageName();
      return true;
   }

   struct TextureLayout
   {
      UINT width = 1, height = 1, depth = 1, slices = 1, mips = 1;
      DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
   };

   // A CPU readable copy of the texture's description (single sampled, no bindings), plus the texture's layout
   template <typename T>
   HRESULT CreateStagingCopy(ID3D11Device* native_device, ID3D11Resource* source, TextureLayout* layout, com_ptr<ID3D11Resource>* staging)
   {
      com_ptr<T> texture;
      source->QueryInterface(&texture);
      D3D11_RESOURCE_DESC<T> desc;
      texture->GetDesc(&desc);
      layout->width = desc.Width;
      layout->mips = desc.MipLevels;
      layout->format = desc.Format;
      desc.Usage = D3D11_USAGE_STAGING;
      desc.BindFlags = 0;
      desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
      desc.MiscFlags = 0;
      if constexpr (std::is_same_v<T, ID3D11Texture3D>)
      {
         layout->height = desc.Height;
         layout->depth = desc.Depth;
         return native_device->CreateTexture3D(&desc, nullptr, reinterpret_cast<ID3D11Texture3D**>(&*staging));
      }
      else if constexpr (std::is_same_v<T, ID3D11Texture2D>)
      {
         layout->height = desc.Height;
         layout->slices = desc.ArraySize;
         desc.SampleDesc = {1, 0};
         return native_device->CreateTexture2D(&desc, nullptr, reinterpret_cast<ID3D11Texture2D**>(&*staging));
      }
      else
      {
         layout->slices = desc.ArraySize;
         return native_device->CreateTexture1D(&desc, nullptr, reinterpret_cast<ID3D11Texture1D**>(&*staging));
      }
   }

   // Reads one mip (all array or depth slices) of "source" back as raw rows of "row_pitch" bytes, ordered [slice][z][row], into
   // "job.files" (the pipe thread writes them to "path"), and describes it in "job.result".
   // "view_format" is how to interpret the data, the resource format if unknown.
   bool ReadTexture(ID3D11DeviceContext* native_device_context, ID3D11Resource* source, DXGI_FORMAT view_format, uint32_t mip, std::filesystem::path path, Job* job, std::string* error)
   {
      com_ptr<ID3D11Device> native_device;
      native_device_context->GetDevice(&native_device);
      D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
      source->GetType(&dimension);

      com_ptr<ID3D11Resource> copy_source(source);
      UINT samples = 1;
      if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
      {
         com_ptr<ID3D11Texture2D> texture;
         source->QueryInterface(&texture);
         D3D11_TEXTURE2D_DESC desc;
         texture->GetDesc(&desc);
         samples = desc.SampleDesc.Count;
         if (samples > 1)
         {
            // Staging textures can't be multisampled, resolve every slice first
            const DXGI_FORMAT resolve_format = view_format != DXGI_FORMAT_UNKNOWN ? view_format : desc.Format;
            com_ptr<ID3D11Texture2D> resolved = IsTypelessFormat(resolve_format) ? nullptr : CloneTexture<ID3D11Texture2D>(native_device.get(), source, resolve_format, D3D11_BIND_SHADER_RESOURCE, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_UNORDERED_ACCESS, false, false, native_device_context, -1, 1);
            if (!resolved)
            {
               *error = std::format("The multisampled {} texture can't be resolved for readback", GetFormatNameSafe(resolve_format));
               return false;
            }
            for (UINT slice = 0; slice < desc.ArraySize; slice++)
               native_device_context->ResolveSubresource(resolved.get(), slice, source, slice, resolve_format);
            copy_source = nullptr;
            resolved->QueryInterface(&copy_source);
         }
      }
      TextureLayout layout;
      com_ptr<ID3D11Resource> staging;
      HRESULT hr = E_FAIL;
      if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
         hr = CreateStagingCopy<ID3D11Texture2D>(native_device.get(), copy_source.get(), &layout, std::addressof(staging));
      else if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE3D)
         hr = CreateStagingCopy<ID3D11Texture3D>(native_device.get(), copy_source.get(), &layout, std::addressof(staging));
      else if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE1D)
         hr = CreateStagingCopy<ID3D11Texture1D>(native_device.get(), copy_source.get(), &layout, std::addressof(staging));
      if (FAILED(hr) || !staging)
      {
         *error = std::format("Failed to create the staging texture for {} (0x{:08X})", GetFormatNameSafe(layout.format), uint32_t(hr));
         return false;
      }
      if (mip >= layout.mips)
      {
         *error = std::format("Mip {} is out of range ({} mips)", mip, layout.mips);
         return false;
      }
      if (view_format == DXGI_FORMAT_UNKNOWN)
         view_format = layout.format;
      native_device_context->CopyResource(staging.get(), copy_source.get());

      const UINT mip_width = GetTextureMipSize(layout.width, mip);
      const UINT mip_height = GetTextureMipSize(layout.height, mip);
      const UINT mip_depth = GetTextureMipSize(layout.depth, mip);
      const bool block_compressed = (layout.format >= DXGI_FORMAT_BC1_TYPELESS && layout.format <= DXGI_FORMAT_BC5_SNORM) || (layout.format >= DXGI_FORMAT_BC6H_TYPELESS && layout.format <= DXGI_FORMAT_BC7_UNORM_SRGB);
      const UINT rows = block_compressed ? (mip_height + 3) / 4 : mip_height;

      std::vector<char> bytes;
      UINT row_pitch = 0;
      for (UINT slice = 0; slice < layout.slices; slice++)
      {
         D3D11_MAPPED_SUBRESOURCE mapped = {};
         // Stalls until the GPU caught up, fine for a one off read
         hr = native_device_context->Map(staging.get(), D3D11CalcSubresource(mip, slice, layout.mips), D3D11_MAP_READ, 0, &mapped);
         if (FAILED(hr))
         {
            *error = std::format("Failed to map the staging texture (0x{:08X})", uint32_t(hr));
            return false;
         }
         row_pitch = mapped.RowPitch;
         const size_t plane_bytes = size_t(rows) * row_pitch; // The rows of a plane are contiguous
         bytes.reserve(plane_bytes * mip_depth * layout.slices);
         for (UINT z = 0; z < mip_depth; z++)
         {
            const char* plane = static_cast<const char*>(mapped.pData) + size_t(z) * mapped.DepthPitch;
            bytes.insert(bytes.end(), plane, plane + plane_bytes);
         }
         native_device_context->Unmap(staging.get(), D3D11CalcSubresource(mip, slice, layout.mips));
      }

      auto& w = job->result;
      w.Path("path", path);
      w.Field("dimension", dimension == D3D11_RESOURCE_DIMENSION_TEXTURE3D ? "3d" : (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE1D ? "1d" : "2d"));
      w.Field("width", mip_width).Field("height", mip_height).Field("depth", mip_depth).Field("slices", layout.slices);
      w.Field("mip", mip).Field("mips", layout.mips).Field("samples", samples);
      w.Format("format", layout.format).Field("format_id", uint32_t(layout.format));
      w.Format("view_format", view_format).Field("view_format_id", uint32_t(view_format));
      w.Field("row_pitch", row_pitch).Field("rows", rows).Field("block_compressed", block_compressed);
      job->files.emplace_back(std::move(path), std::move(bytes));
      return true;
   }

   // Super resolution tap: wraps every "sr_implementations" entry (see "Init()") to record what games feed the upscalers
   constexpr const char* sr_resource_names[] = {"source_color", "motion_vectors", "depth_buffer", "exposure", "bias_mask", "transparency_alpha", "output_color"};
   constexpr size_t sr_history_size = 256;

   struct SrCapture
   {
      std::vector<std::pair<const char*, com_ptr<ID3D11Resource>>> textures; // GPU copies, read back at present
      bool draw_succeeded;
   };
   std::atomic<bool> sr_capture_armed = false;
   std::mutex s_mutex_sr_capture;
   std::optional<SrCapture> sr_capture; // Guarded by "s_mutex_sr_capture", set by the draw that found the capture armed

   class SrTap final : public SR::SuperResolutionImpl
   {
   public:
      explicit SrTap(std::unique_ptr<SR::SuperResolutionImpl> wrapped) : inner(std::move(wrapped))
      {
      }

      bool HasInit(const SR::InstanceData* data) const override
      {
         return inner->HasInit(data);
      }
      bool IsSupported(const SR::InstanceData* data) const override
      {
         return inner->IsSupported(data);
      }
      bool Init(SR::InstanceData*& data, ID3D11Device* device, IDXGIAdapter* adapter) override
      {
         init_count++;
         return inner->Init(data, device, adapter);
      }
      void Deinit(SR::InstanceData*& data, ID3D11Device* optional_device) override
      {
         deinit_count++;
         inner->Deinit(data, optional_device);
      }
      bool UpdateSettings(SR::InstanceData* data, ID3D11DeviceContext* command_list, const SR::SettingsData& settings_data) override
      {
         const bool succeeded = inner->UpdateSettings(data, command_list, settings_data);
         const std::lock_guard lock(mutex);
         settings = settings_data;
         update_count++;
         last_update_succeeded = succeeded;
         return succeeded;
      }
      bool Draw(const SR::InstanceData* data, ID3D11DeviceContext* command_list, const DrawData& draw_data) override
      {
         ID3D11Resource* const resources[] = {draw_data.source_color, draw_data.motion_vectors, draw_data.depth_buffer, draw_data.exposure, draw_data.bias_mask, draw_data.transparency_alpha, draw_data.output_color};
         const bool capture = sr_capture_armed.load() && sr_capture_armed.exchange(false);
         com_ptr<ID3D11Device> native_device;
         SrCapture captured;
         if (capture)
         {
            command_list->GetDevice(&native_device);
            for (size_t i = 0; i < std::size(resources) - 1; i++) // Inputs before the upscaler runs
               if (resources[i])
                  captured.textures.emplace_back(sr_resource_names[i], CloneResource(native_device.get(), command_list, resources[i]));
         }
         captured.draw_succeeded = inner->Draw(data, command_list, draw_data);
         QueueInjectedPassTrace(command_list, "Luma Super Resolution", draw_data.output_color);
         if (capture && draw_data.output_color)
            captured.textures.emplace_back(sr_resource_names[std::size(resources) - 1], CloneResource(native_device.get(), command_list, draw_data.output_color));
         {
            const std::lock_guard lock(mutex);
            last_draw = draw_data;
            // The resource descriptions cost a few queries and allocations per draw, only worth it with a client that can read them
            if (connected_clients > 0)
            {
               for (size_t i = 0; i < std::size(resources); i++)
               {
                  last_resources[i] = {};
                  if (resources[i])
                     GetResourceInfo(resources[i], last_resources[i].size, last_resources[i].format, nullptr, &last_resources[i].resource);
               }
            }
            history[draw_count % sr_history_size] = {cb_luma_global_settings.FrameIndex, draw_data.frame_index, draw_data.jitter_x, draw_data.jitter_y, draw_data.pre_exposure, draw_data.vert_fov, draw_data.render_width, draw_data.render_height, draw_data.reset, captured.draw_succeeded};
            draw_count++;
            failures += captured.draw_succeeded ? 0 : 1;
            resets += draw_data.reset ? 1 : 0;
         }
         const bool succeeded = captured.draw_succeeded;
         if (capture)
         {
            const std::lock_guard lock(s_mutex_sr_capture);
            sr_capture = std::move(captured);
         }
         return succeeded;
      }
      int GetJitterPhases(const SR::InstanceData* data) const override
      {
         return inner->GetJitterPhases(data);
      }
      bool NeedsStateRestoration() const override
      {
         return inner->NeedsStateRestoration();
      }

      void Write(JsonWriter* w, size_t history_count) const
      {
         const std::lock_guard lock(mutex);
         w->Field("init_count", init_count.load()).Field("deinit_count", deinit_count.load());
         w->Field("update_settings_count", update_count).Field("last_update_settings_succeeded", last_update_succeeded);
         w->Key("settings").BeginObject();
         w->Key("output").BeginArray().Value(settings.output_width).Value(settings.output_height).EndArray();
         w->Key("render").BeginArray().Value(settings.render_width).Value(settings.render_height).EndArray();
         w->Field("dynamic_resolution", settings.dynamic_resolution).Field("hdr", settings.hdr).Field("inverted_depth", settings.inverted_depth);
         w->Field("mvs_jittered", settings.mvs_jittered).Field("mvs_x_scale", settings.mvs_x_scale).Field("mvs_y_scale", settings.mvs_y_scale);
         w->Field("auto_exposure", settings.auto_exposure).Field("render_preset", settings.render_preset);
         w->EndObject();

         w->Field("draw_count", draw_count).Field("draw_failures", failures).Field("resets", resets);
         if (draw_count == 0)
            return;
         w->Key("last_draw").BeginObject();
         w->Field("reset", last_draw.reset).Key("render").BeginArray().Value(last_draw.render_width).Value(last_draw.render_height).EndArray();
         w->Field("jitter_x", last_draw.jitter_x).Field("jitter_y", last_draw.jitter_y).Field("pre_exposure", last_draw.pre_exposure);
         w->Field("vert_fov", last_draw.vert_fov).Field("near_plane", last_draw.near_plane).Field("far_plane", last_draw.far_plane);
         w->Field("time_delta", last_draw.time_delta).Field("frame_index", last_draw.frame_index).Field("user_sharpness", last_draw.user_sharpness);
         w->Key("resources").BeginObject();
         for (size_t i = 0; i < std::size(sr_resource_names); i++)
         {
            const auto& resource = last_resources[i];
            w->Key(sr_resource_names[i]);
            if (resource.resource.empty())
            {
               w->Value("none"); // Or drawn before a client connected, see "connected_clients"
               continue;
            }
            w->BeginObject().Format("format", resource.format).Key("size").BeginArray().Value(resource.size.x).Value(resource.size.y).Value(resource.size.z).EndArray().Field("resource", resource.resource).EndObject();
         }
         w->EndObject().EndObject();

         // Newest last. Repeated jitters or frames without a draw expose latch and phase bugs.
         const size_t count = (std::min)({history_count, draw_count, sr_history_size});
         std::set<std::pair<float, float>> unique_jitters;
         uint32_t consecutive_repeats = 0;
         w->Key("history").BeginArray();
         for (size_t i = draw_count - count; i < draw_count; i++)
         {
            const auto& record = history[i % sr_history_size];
            if (i > draw_count - count)
            {
               const auto& previous = history[(i - 1) % sr_history_size];
               consecutive_repeats += (previous.jitter_x == record.jitter_x && previous.jitter_y == record.jitter_y) ? 1 : 0;
            }
            unique_jitters.emplace(record.jitter_x, record.jitter_y);
            w->BeginArray().Value(record.luma_frame).Value(record.frame_index).Value(record.jitter_x).Value(record.jitter_y).Value(record.pre_exposure).Value(record.vert_fov);
            w->Value(record.render_width).Value(record.render_height).Value(record.reset).Value(record.succeeded).EndArray();
         }
         w->EndArray();
         w->Field("history_columns", "luma_frame, frame_index, jitter_x, jitter_y, pre_exposure, vert_fov, render_width, render_height, reset, succeeded");
         w->Field("unique_jitters", unique_jitters.size()).Field("consecutive_repeated_jitters", consecutive_repeats);
      }

   private:
      struct ResourceSummary
      {
         uint4 size = {};
         DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
         std::string resource;
      };
      struct DrawRecord
      {
         uint32_t luma_frame = 0;
         unsigned long long frame_index = 0;
         float jitter_x = 0.f;
         float jitter_y = 0.f;
         float pre_exposure = 0.f;
         float vert_fov = 0.f;
         unsigned int render_width = 0;
         unsigned int render_height = 0;
         bool reset = false;
         bool succeeded = false;
      };

      std::unique_ptr<SR::SuperResolutionImpl> inner;
      std::atomic<uint32_t> init_count = 0;
      std::atomic<uint32_t> deinit_count = 0;
      mutable std::mutex mutex; // Guards everything below
      SR::SettingsData settings;
      uint32_t update_count = 0;
      bool last_update_succeeded = false;
      DrawData last_draw;
      ResourceSummary last_resources[std::size(sr_resource_names)];
      std::array<DrawRecord, sr_history_size> history = {};
      size_t draw_count = 0;
      uint32_t failures = 0;
      uint32_t resets = 0;
   };

   // Where readbacks go ("out_dir" can only pick a subfolder): an elevated game must not write where a non elevated client asks.
   // Not under the game's "Luma" folder, a non empty one there would shadow the repository shaders.
   std::filesystem::path ReadbackRoot()
   {
      return std::filesystem::temp_directory_path() / "luma-mcp";
   }

   // Request arguments are UTF-8 (a narrow "path" would read them in the ANSI code page)
   std::filesystem::path Utf8Path(std::string_view utf8)
   {
      return std::u8string_view(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size());
   }

   // "dir" is "ReadbackRoot()" or inside it, ".." and junctions resolved. Never throws (pipe thread).
   // ponytail: checked, then written (a junction swapped in between wins), fine for a DEVELOPMENT only tool
   bool IsInReadbackRoot(const std::filesystem::path& dir)
   {
      std::error_code ec;
      const std::filesystem::path root = std::filesystem::weakly_canonical(ReadbackRoot(), ec);
      if (ec || root.empty())
         return false;
      const std::filesystem::path canonical_dir = std::filesystem::weakly_canonical(dir, ec);
      return !ec && std::mismatch(root.begin(), root.end(), canonical_dir.begin(), canonical_dir.end()).first == root.end();
   }

   // "<out_dir>/<stem>_f<frame>.bin"
   std::filesystem::path ReadbackPath(const Job& job, std::string_view stem)
   {
      const std::string file = std::format("{}_f{}.bin", stem, cb_luma_global_settings.FrameIndex);
      if (const std::string* out_dir = job.Arg("out_dir"))
         return Utf8Path(*out_dir) / file;
      return ReadbackRoot() / file;
   }

   // A readback's pass may not draw in the frame after the request: false (and the error) once its miss budget is spent
   bool KeepWaiting(Job* job, std::string_view view)
   {
      if (++job->frames < readback_max_frames)
         return true;
      job->error = std::format("The pass was not drawn, or had no {} at that slot, in {} frames (check \"instance\" and \"slot\")", view, readback_max_frames);
      return false;
   }

   // Every "Run*" returns true once its job finished (successfully or not), false to run again next present
   bool RunStatus(Job* job, DeviceData& device_data)
   {
      auto& w = job->result;
      w.Field("game", Globals::GAME_NAME).Field("version", Globals::VERSION);
      w.Field("pid", GetCurrentProcessId()).Field("bits", sizeof(void*) * 8);
      w.Path("exe_path", System::GetModulePath()); // ReShade writes its log next to it by default
      w.Field("frame_index", cb_luma_global_settings.FrameIndex).Field("presenting", true);
      w.Key("output_resolution").BeginArray().Value(device_data.output_resolution.x).Value(device_data.output_resolution.y).EndArray();
      w.Key("render_resolution").BeginArray().Value(device_data.render_resolution.x).Value(device_data.render_resolution.y).EndArray();
      w.Field("mod_active", IsModActive(device_data));
      w.Field("file_clones", CountFileClones(device_data));
      {
         const std::shared_lock lock_loading(s_mutex_loading);
         w.Field("has_compilation_errors", !shaders_compilation_errors.empty());
      }
      w.Field("trace_count", trace_count);
      w.Field("swapchain_upgrade_type", uint32_t(swapchain_upgrade_type));
      w.Field("swapchain_format_upgrade_type", uint32_t(swapchain_format_upgrade_type));
      w.Field("texture_format_upgrades_type", uint32_t(texture_format_upgrades_type));
      {
         const std::shared_lock lock_device(device_data.mutex);
         w.Key("swapchains").BeginArray();
         for (reshade::api::swapchain* swapchain : device_data.swapchains)
         {
            DXGI_SWAP_CHAIN_DESC desc = {};
            ((IDXGISwapChain*)(swapchain->get_native()))->GetDesc(&desc);
            w.BeginObject().Format("format", desc.BufferDesc.Format).Field("width", desc.BufferDesc.Width).Field("height", desc.BufferDesc.Height).Field("buffers", desc.BufferCount).EndObject();
         }
         w.EndArray();
      }
      MEMORYSTATUSEX memory_status = {sizeof(MEMORYSTATUSEX)};
      if (GlobalMemoryStatusEx(&memory_status))
      {
         // Process address space, what x86 games run out of
         w.Field("virtual_used_mb", (memory_status.ullTotalVirtual - memory_status.ullAvailVirtual) >> 20);
         w.Field("virtual_total_mb", memory_status.ullTotalVirtual >> 20);
         w.Field("system_memory_load_percent", memory_status.dwMemoryLoad);
      }
      return true;
   }

   bool RunTraceCapture(Job* job, DeviceData& device_data, CommandListData& cmd_list_data)
   {
      {
         const std::unique_lock lock_trace(s_mutex_trace);
         if (job->phase == 0)
         {
            if (trace_running || trace_scheduled)
               return false; // Another capture is in flight
            trace_scheduled = true;
            job->phase = 1;
            return false;
         }
         if (job->phase == 1)
         {
            if (trace_running)
               job->phase = 2;
            return false;
         }
         if (trace_running)
            return false;
      }
      job->frames++;
      // Locked after reading the list, "OnReShadePresent" locks the global trace mutex first
      const auto capture_again = [&job]
      {
         const std::unique_lock lock_trace(s_mutex_trace);
         trace_scheduled = true;
         job->phase = 1;
         return false;
      };

      auto& w = job->result;
      if (const auto trigger = job->HashArg("trigger"))
      {
         std::vector<size_t> trigger_indices;
         {
            const std::shared_lock lock_generic(s_mutex_generic);
            const std::shared_lock lock_trace(cmd_list_data.mutex_trace);
            for (size_t i = 0; i < cmd_list_data.trace_draw_calls_data.size() && trigger_indices.size() < 32; i++)
            {
               const auto& entry = cmd_list_data.trace_draw_calls_data[i];
               const auto* pipeline = entry.type == TraceDrawCallData::TraceDrawCallType::Shader ? FindPipeline(device_data, entry.pipeline_handle) : nullptr;
               if (pipeline && pipeline->shader_hashes[0] == *trigger)
                  trigger_indices.push_back(i);
            }
         }
         // Keep capturing until the pass shows up
         if (job->frames < uint32_t(job->IntArg("max_frames", 120, 1, 3600)) && trigger_indices.empty()) // Read even when found right away
            return capture_again();
         w.Field("count", trace_count).Field("frames_captured", job->frames).Field("trigger_found", !trigger_indices.empty()).Key("trigger_indices").BeginArray();
         for (const size_t index : trigger_indices)
            w.Value(index);
         w.EndArray();
         return true;
      }

      // Draw counts per shader over consecutive captures (a capture spans two presents, so every other frame)
      const uint32_t frames = uint32_t(job->IntArg("frames", 1, 1, 600));
      if (frames > 1)
      {
         {
            const std::shared_lock lock_generic(s_mutex_generic);
            const std::shared_lock lock_trace(cmd_list_data.mutex_trace);
            for (const auto& entry : cmd_list_data.trace_draw_calls_data)
            {
               const auto* pipeline = entry.type == TraceDrawCallData::TraceDrawCallType::Shader ? FindPipeline(device_data, entry.pipeline_handle) : nullptr;
               if (!pipeline)
                  continue;
               auto& counts = job->hash_counts.try_emplace(pipeline->shader_hashes[0], Job::HashCounts{pipeline->StageName(), {}}).first->second.per_frame;
               counts.resize(job->frames, 0); // Zeroes for the captures that didn't draw it
               counts.back()++;
            }
         }
         if (job->frames < frames)
            return capture_again();
         std::vector<std::pair<uint32_t, uint32_t>> totals; // Hash, total draws
         for (auto& [hash, counts] : job->hash_counts)
         {
            counts.per_frame.resize(job->frames, 0);
            uint32_t total = 0;
            for (const uint32_t count : counts.per_frame)
               total += count;
            totals.emplace_back(hash, total);
         }
         std::sort(totals.begin(), totals.end(), [](const auto& a, const auto& b)
            { return a.second > b.second; });
         const size_t top = size_t(job->IntArg("top", 64, 1, 5000));
         // Long windows: per hash aggregates instead of a per frame array
         const bool summary = job->BoolArg("summary", false);
         w.Field("count", trace_count).Field("frames_captured", job->frames).Field("distinct_shaders", totals.size()).Key("hash_counts").BeginArray();
         for (size_t i = 0; i < (std::min)(top, totals.size()); i++)
         {
            const auto& counts = job->hash_counts[totals[i].first];
            w.BeginObject().Hash("hash", totals[i].first).Field("stage", counts.stage).Field("total", totals[i].second);
            if (summary)
            {
               const auto [min, max] = std::minmax_element(counts.per_frame.begin(), counts.per_frame.end());
               w.Field("frames_seen", std::ranges::count_if(counts.per_frame, [](uint32_t count)
                                         { return count != 0; }))
                  .Field("min", *min)
                  .Field("max", *max)
                  .EndObject();
               continue;
            }
            w.Key("per_frame").BeginArray();
            for (const uint32_t count : counts.per_frame)
               w.Value(count);
            w.EndArray().EndObject();
         }
         w.EndArray();
         return true;
      }

      w.Field("count", trace_count).Field("frames_captured", job->frames);
      return true;
   }

   bool RunTraceList(Job* job, DeviceData& device_data, CommandListData& cmd_list_data)
   {
      const size_t offset = size_t((std::max)(job->IntArg("offset", 0), int64_t(0)));
      const size_t limit = size_t(job->IntArg("limit", 300, 1, 5000));
      const std::shared_lock lock_trace(s_mutex_trace);
      if (trace_running)
      {
         job->error = "A capture is in progress";
         return true;
      }
      const std::shared_lock lock_generic(s_mutex_generic);
      const std::shared_lock lock_loading(s_mutex_loading);
      const std::shared_lock lock_trace_2(cmd_list_data.mutex_trace);
      auto& w = job->result;
      w.Field("total", cmd_list_data.trace_draw_calls_data.size());
      w.Key("entries").BeginArray();
      const TraceFilter filter{.include_vs = job->BoolArg("include_vs", false),
         .include_bindings = job->BoolArg("include_bindings", false),
         .include_buffer_writes = job->BoolArg("include_buffer_writes", false),
         .replaced_only = job->BoolArg("replaced_only", false),
         .type = job->Arg("type"),
         .hash = job->HashArg("hash"),
         .stage = job->Arg("stage"),
         .resource = job->Arg("resource")};
      size_t matched = 0;
      for (size_t i = 0; i < cmd_list_data.trace_draw_calls_data.size(); i++)
      {
         const auto& entry = cmd_list_data.trace_draw_calls_data[i];
         if (!filter.Matches(device_data, entry))
            continue;
         if (matched >= offset && matched < offset + limit)
         {
            w.BeginObject();
            WriteTraceEntrySummary(&w, device_data, entry, i);
            w.EndObject();
         }
         matched++;
      }
      w.EndArray();
      w.Field("matched", matched).Field("offset", offset).Field("returned", matched > offset ? (std::min)(matched - offset, limit) : 0);
      return true;
   }

   bool RunTraceGet(Job* job, DeviceData& device_data, CommandListData& cmd_list_data)
   {
      const std::shared_lock lock_trace(s_mutex_trace);
      const std::shared_lock lock_generic(s_mutex_generic);
      const std::shared_lock lock_loading(s_mutex_loading);
      const std::shared_lock lock_trace_2(cmd_list_data.mutex_trace);
      const size_t index = size_t(job->IntArg("index", -1));
      if (trace_running || index >= cmd_list_data.trace_draw_calls_data.size())
      {
         job->error = std::format("Trace index {} is out of range (captured {})", index, cmd_list_data.trace_draw_calls_data.size());
         return true;
      }
      WriteTraceEntryDetails(&job->result, device_data, cmd_list_data.trace_draw_calls_data[index], index);
      return true;
   }

   bool RunReadResource(Job* job, DeviceData& device_data, CommandListData& cmd_list_data, ID3D11DeviceContext* native_device_context, reshade::api::swapchain* swapchain)
   {
      const std::string* view = job->Arg("view");
      if (job->phase == 0)
      {
         // Final targets, read right away at present
         if (view && (*view == "swapchain" || *view == "ui"))
         {
            const bool back_buffer = *view == "swapchain";
            ID3D11Resource* resource = back_buffer ? reinterpret_cast<ID3D11Resource*>(swapchain->get_current_back_buffer().handle) : device_data.ui_texture.get();
            if (!resource)
            {
               job->error = back_buffer ? "The swapchain has no current back buffer" : "There's no separate UI texture (UI separation is off, or the UI hasn't drawn yet)";
               return true;
            }
            if (ReadTexture(native_device_context, resource, DXGI_FORMAT_UNKNOWN, uint32_t(job->IntArg("mip", 0)), ReadbackPath(*job, *view), job, &job->error))
               job->result.Field("view", *view).Field("note", "Read before Luma's display composition: no output encoding, gamma correction, gamut mapping or UI composition yet");
            return true;
         }
         if (view && view->starts_with("registered:"))
         {
            const std::string name = view->substr(11);
            DevTextureGetter texture = nullptr;
            {
               const std::lock_guard lock(s_mutex_dev_values);
               const DevValue* dev_value = FindDevValue(name);
               if (!dev_value || dev_value->kind != DevValueKind::Texture)
               {
                  job->error = "Unknown registered texture " + name + " (see luma_dev_values, kind texture)";
                  return true;
               }
               texture = dev_value->texture;
            }
            const com_ptr<ID3D11Resource> resource = GetTexture(texture, device_data);
            if (!resource)
            {
               job->error = name + " is not allocated (its feature is off, or hasn't run yet)";
               return true;
            }
            if (ReadTexture(native_device_context, resource.get(), DXGI_FORMAT_UNKNOWN, uint32_t(job->IntArg("mip", 0)), ReadbackPath(*job, name), job, &job->error))
               job->result.Field("view", *view).Field("note", "Read at present: the texture's last content, usually this frame's");
            return true;
         }

         static const std::unordered_map<std::string, DebugDrawMode> modes = {
            {"rtv", DebugDrawMode::RenderTarget}, {"srv", DebugDrawMode::ShaderResource}, {"uav", DebugDrawMode::UnorderedAccessView}, {"depth", DebugDrawMode::Depth}, {"stencil", DebugDrawMode::Stencil}};
         const auto mode = modes.find(view ? *view : "rtv");
         if (mode == modes.end())
         {
            job->error = "\"view\" must be one of rtv, srv, uav, depth, stencil, swapchain, ui, registered:<name>";
            return true;
         }
         if (!ResolveTarget(job, device_data, cmd_list_data))
            return true;
         // Debug draw hooks pixel and compute shader draws only
         const bool is_compute = job->stage == std::string_view("CS");
         if (!is_compute && job->stage != std::string_view("PS"))
         {
            job->error = std::format("{} passes can't be read back, use the PS entry of the same draw", job->stage);
            return true;
         }
         if (is_compute && mode->second != DebugDrawMode::ShaderResource && mode->second != DebugDrawMode::UnorderedAccessView)
         {
            job->error = "Compute passes only have srv and uav views";
            return true;
         }

         // Restored in "OnPresent" once the job finishes. Blends stay on and inputs unfrozen, so the pass runs as the game drew it.
         job->saved_debug_draw = DebugDrawSelection::Current();
         DebugDrawSelection{job->pipeline_handle, job->shader_hash, job->instance, job->thread, mode->second, int32_t(job->IntArg("slot", 0)), false, false, job->BoolArg("replaced", true)}.Apply();
         device_data.debug_draw_texture = nullptr;
         job->result.Hash("hash", job->shader_hash).Field("instance", job->instance).Field("view", mode->first).Field("slot", debug_draw_view_index);
         job->phase = 1;
         return false;
      }

      if (!device_data.debug_draw_texture)
         return !KeepWaiting(job, "view");
      const auto path = ReadbackPath(*job, std::format("{}_{}_{}{}", Shader::Hash_NumToStr(job->shader_hash), job->instance, view ? *view : "rtv", debug_draw_view_index));
      ReadTexture(native_device_context, device_data.debug_draw_texture.get(), device_data.debug_draw_texture_format, uint32_t(job->IntArg("mip", 0)), path, job, &job->error);
      return true;
   }

   bool RunReadConstantBuffer(Job* job, DeviceData& device_data, CommandListData& cmd_list_data, ID3D11DeviceContext* native_device_context)
   {
      if (job->phase == 0)
      {
         if (!ResolveTarget(job, device_data, cmd_list_data))
            return true;
         if (job->stage == std::string_view("GS") || job->stage == std::string_view("XS"))
         {
            job->error = std::format("Constant buffer tracking doesn't support {} passes", job->stage);
            return true;
         }
         const int64_t slot = job->IntArg("slot", 0);
         if (slot < 0 || slot >= D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT)
         {
            job->error = "\"slot\" is out of range";
            return true;
         }
         // Restored in "OnPresent" once the job finishes. The tracking has no thread filter.
         job->saved_track_buffer = TrackBufferSelection::Current();
         TrackBufferSelection{job->pipeline_handle, job->instance, int32_t(slot)}.Apply();
         device_data.track_buffer_data.Clear();
         job->result.Hash("hash", job->shader_hash).Field("stage", job->stage).Field("instance", job->instance).Field("slot", slot);
         job->phase = 1;
         return false;
      }

      auto& data = device_data.track_buffer_data;
      // Buffers bound in deferred contexts were only copied, map them on the immediate one
      if (!data.data_valid && data.cb)
      {
         D3D11_BUFFER_DESC desc = {};
         data.cb->GetDesc(&desc);
         data.data_valid = MapBufferData(data.cb, native_device_context, data.data, desc.ByteWidth);
      }
      if (!data.data_valid)
         return !KeepWaiting(job, "constant buffer");
      std::string hex;
      hex.reserve(data.data.size() * 8);
      for (const float value : data.data)
         std::format_to(std::back_inserter(hex), "{:08X}", std::bit_cast<uint32_t>(value));
      auto& w = job->result;
      // Multi frame series: keep the tracking armed and collect one read per frame
      const uint32_t frames = uint32_t(job->IntArg("frames", 1, 1, 600));
      if (frames > 1)
      {
         job->series.push_back(std::format("{}:{}", cb_luma_global_settings.FrameIndex, hex));
         job->frames = 0; // The miss budget is per read
         if (job->series.size() < frames)
         {
            data.Clear();
            return false;
         }
         w.Field("resource", data.hash).Key("series").BeginArray();
         for (const std::string& entry : job->series)
            w.Value(entry);
         w.EndArray();
      }
      else
      {
         w.Field("resource", data.hash).Field("bytes", data.data.size() * sizeof(float));
         if (data.num_constants != 0)
            w.Field("first_constant", data.first_constant).Field("num_constants", data.num_constants);
         w.Field("hex_dwords", hex);
      }
      data.Clear();
      return true;
   }

   bool RunDevValues(Job* job, DeviceData& device_data)
   {
      const std::string* filter = job->Arg("filter");
      auto& w = job->result;
      w.Key("values").BeginArray();
      std::vector<std::pair<std::string, DevTextureGetter>> textures; // Written after the lock, see "GetTexture()"
      std::unique_lock lock(s_mutex_dev_values);
      for (const DevValue& dev_value : dev_values)
      {
         if (filter && dev_value.name.find(*filter) == std::string::npos)
            continue;
         if (dev_value.kind == DevValueKind::Texture)
         {
            textures.emplace_back(dev_value.name, dev_value.texture);
            continue;
         }
         w.BeginObject().Field("name", dev_value.name);
         switch (dev_value.kind)
         {
         case DevValueKind::Toggle:
            w.Field("kind", "toggle").Field("value", *static_cast<const bool*>(dev_value.value));
            break;
         case DevValueKind::Float:
            w.Field("kind", "float").Field("value", *static_cast<const float*>(dev_value.value));
            if (dev_value.min != -FLT_MAX || dev_value.max != FLT_MAX)
               w.Field("min", dev_value.min).Field("max", dev_value.max);
            break;
         case DevValueKind::Int:
            w.Field("kind", "int").Field("value", *static_cast<const int*>(dev_value.value)).Field("min", dev_value.min).Field("max", dev_value.max);
            break;
         case DevValueKind::Counter:
            w.Field("kind", "counter").Field("value", *static_cast<const uint32_t*>(dev_value.value));
            break;
         case DevValueKind::Texture: // No-op: skipped above
            break;
         }
         w.EndObject();
      }
      lock.unlock();
      for (const auto& [name, texture] : textures)
      {
         w.BeginObject().Field("name", name).Field("kind", "texture");
         const com_ptr<ID3D11Resource> resource = GetTexture(texture, device_data);
         w.Field("allocated", bool(resource));
         if (resource)
         {
            uint4 size = {};
            DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
            GetResourceInfo(resource.get(), size, format);
            w.Format("format", format).Key("size").BeginArray().Value(size.x).Value(size.y).Value(size.z).Value(size.w).EndArray();
         }
         w.EndObject();
      }
      w.EndArray();
      return true;
   }

   bool RunSetDevValue(Job* job, DeviceData& device_data)
   {
      const std::string* name = job->Arg("name");
      const std::string* value = job->Arg("value");
      if (!name || !value)
      {
         job->error = "Pass \"name\" and \"value\"";
         return true;
      }
      std::unique_lock lock(s_mutex_dev_values);
      DevValue* dev_value = FindDevValue(*name);
      if (!dev_value)
      {
         job->error = "Unknown dev value " + *name + " (see luma_dev_values)";
         return true;
      }
      // Strict, a typo must not silently become 0 or false
      double parsed = 0.0;
      char* end = nullptr;
      if (dev_value->kind == DevValueKind::Toggle)
      {
         if (*value != "1" && *value != "true" && *value != "0" && *value != "false")
         {
            job->error = *name + " takes 1, 0, true or false";
            return true;
         }
         parsed = (*value == "1" || *value == "true") ? 1.0 : 0.0;
      }
      else if (dev_value->kind == DevValueKind::Float)
         parsed = std::strtof(value->c_str(), &end);
      else if (dev_value->kind == DevValueKind::Int)
         parsed = double(std::strtoll(value->c_str(), &end, 0));
      else
      {
         job->error = "Counters and textures are read only";
         return true;
      }
      if (end && *end != '\0')
      {
         job->error = "\"" + *value + "\" is not a number";
         return true;
      }
      if (parsed < dev_value->min || parsed > dev_value->max)
      {
         job->error = std::format("{} takes {} to {}", *name, dev_value->min, dev_value->max);
         return true;
      }
      job->result.Field("name", *name).Field("value", parsed);
      device_data.cb_luma_global_settings_dirty = true; // For values mirrored into the settings buffer
      if (dev_value->set)
      {
         // The game's code (a mode switch, a settings mirror), not under the lock
         const DevValueSetter set = dev_value->set;
         lock.unlock();
         job->error = set(device_data, parsed);
      }
      else if (dev_value->kind == DevValueKind::Toggle)
         *static_cast<bool*>(dev_value->value) = parsed != 0.0;
      else if (dev_value->kind == DevValueKind::Float)
         *static_cast<float*>(dev_value->value) = float(parsed);
      else
         *static_cast<int*>(dev_value->value) = int(parsed);
      return true;
   }

   bool RunSrState(Job* job, DeviceData& device_data)
   {
      auto& w = job->result;
      w.Field("has_drawn_sr", device_data.has_drawn_sr.load()).Field("texture_mip_lod_bias_offset", device_data.texture_mip_lod_bias_offset);
      w.Key("implementations").BeginArray();
#if ENABLE_SR
      const size_t history = size_t(job->IntArg("history", 32, 0, int64_t(sr_history_size))); // Read even without implementations
      for (const auto& [type, implementation] : sr_implementations)
      {
         w.BeginObject().Field("type", SR::GetTypeName(type));
         if (const auto* tap = dynamic_cast<const SrTap*>(implementation.get()))
            tap->Write(&w, history);
         w.EndObject();
      }
      w.EndArray();
      w.Field("sr_type", SR::GetTypeName(device_data.sr_type));
      w.Field("sr_suppressed", device_data.sr_suppressed.load()).Field("force_reset_sr", device_data.force_reset_sr.load());
      w.Field("sr_render_resolution_scale", device_data.sr_render_resolution_scale.load());
#else
      w.EndArray();
      job->read_args.emplace("history");
#endif
      return true;
   }

   bool RunSrCapture(Job* job, ID3D11DeviceContext* native_device_context)
   {
      if (job->phase == 0)
      {
         {
            const std::lock_guard lock(s_mutex_sr_capture);
            sr_capture.reset();
         }
         sr_capture_armed = true;
         job->phase = 1;
         return false;
      }
      std::optional<SrCapture> captured;
      {
         const std::lock_guard lock(s_mutex_sr_capture);
         captured.swap(sr_capture);
      }
      const uint32_t max_frames = uint32_t(job->IntArg("max_frames", 30, 1, 3600)); // Read even when the first frame has the capture
      if (!captured)
      {
         if (++job->frames < max_frames)
            return false;
         sr_capture_armed = false;
         job->error = std::format("No super resolution draw in {} frames (off, suppressed, or its pass isn't reached)", job->frames);
         return true;
      }
      auto& w = job->result;
      w.Field("draw_succeeded", captured->draw_succeeded).Key("textures").BeginArray();
      for (const auto& [name, texture] : captured->textures)
      {
         w.BeginObject().Field("name", name);
         std::string error = texture ? "" : "Failed to copy it";
         if (texture)
            ReadTexture(native_device_context, texture.get(), DXGI_FORMAT_UNKNOWN, 0, ReadbackPath(*job, std::string("sr_") + name), job, &error);
         if (!error.empty())
            w.Field("error", error);
         w.EndObject();
      }
      w.EndArray();
      return true;
   }

   bool RunShaderList(Job* job, DeviceData& device_data)
   {
      const std::string* stage = job->Arg("stage");
      const bool replaced_only = job->BoolArg("replaced_only", false);
      // Paged like "trace_list", ~150 B per entry: the default stays under an agent's tool output cap
      const size_t offset = size_t((std::max)(job->IntArg("offset", 0), int64_t(0)));
      const size_t limit = size_t(job->IntArg("limit", 200, 1, 100000));
      const std::shared_lock lock_generic(s_mutex_generic);
      const std::lock_guard lock_dumping(s_mutex_dumping);
      auto& w = job->result;
      size_t matched = 0;
      const auto write_shader = [&](uint32_t hash, const Shader::CachedPipeline* pipeline, size_t pipelines)
      {
         const size_t index = matched++;
         if (index < offset || index >= offset + limit)
            return;
         const auto cached_it = shader_cache.find(hash);
         const auto* cached = cached_it != shader_cache.end() ? cached_it->second : nullptr;
         w.BeginObject().Hash("hash", hash);
         if (pipeline)
            w.Field("stage", pipeline->StageName()).Field("pipelines", pipelines).Field("replaced", ReplacementName(*pipeline));
         else
            w.Field("live", false);
         if (cached)
         {
            w.Field("version", cached->type_and_version).Field("bytes", cached->size);
            if (cached->found_reflections)
            {
               const auto write_slots = [&](const char* key, const bool* slots, size_t count)
               {
                  w.Key(key).BeginArray();
                  for (size_t i = 0; i < count; i++)
                     if (slots[i])
                        w.Value(i);
                  w.EndArray();
               };
               write_slots("cbs", cached->cbs, std::size(cached->cbs));
               write_slots("srvs", cached->srvs, std::size(cached->srvs));
               write_slots("uavs", cached->uavs, std::size(cached->uavs));
               write_slots("rtvs", cached->rtvs, std::size(cached->rtvs));
               write_slots("samplers", cached->samplers, std::size(cached->samplers));
            }
         }
         w.EndObject();
      };
      w.Key("shaders").BeginArray();
      size_t live = 0;
      for (const auto& [hash, pipelines] : device_data.pipeline_caches_by_shader_hash)
      {
         if (pipelines.empty())
            continue;
         live++;
         const auto* pipeline = *pipelines.begin();
         if ((stage && _stricmp(stage->c_str(), pipeline->StageName()) != 0) || (replaced_only && !IsReplaced(*pipeline)))
            continue;
         write_shader(hash, pipeline, pipelines.size());
      }
      // Loaded at some point but without a live pipeline now (the dump folder can still have them)
      if (job->BoolArg("include_unloaded", false) && !replaced_only)
      {
         for (const auto& [hash, cached] : shader_cache)
         {
            const auto live_it = device_data.pipeline_caches_by_shader_hash.find(hash);
            if (live_it != device_data.pipeline_caches_by_shader_hash.end() && !live_it->second.empty())
               continue;
            if (stage && (cached->type_and_version.size() < 2 || _strnicmp(stage->c_str(), cached->type_and_version.c_str(), 2) != 0))
               continue;
            write_shader(hash, nullptr, 0);
         }
      }
      w.EndArray().Field("live", live).Field("loaded", shader_cache.size());
      w.Field("matched", matched).Field("offset", offset).Field("returned", matched > offset ? (std::min)(matched - offset, limit) : 0);
      return true;
   }

   bool RunDumpShader(Job* job)
   {
      const auto hash = job->HashArg("hash");
      if (!hash)
      {
         job->error = "Pass \"hash\"";
         return true;
      }
      const std::lock_guard lock_dumping(s_mutex_dumping);
      if (!shader_cache.contains(*hash)) // "DumpShader()" expects a known hash
      {
         job->error = std::format("Shader {} was never loaded", Shader::Hash_NumToStr(*hash, true));
         return true;
      }
      DumpShader(*hash);
      // The name "DumpShader()" writes (plus a ".meta" in DEVELOPMENT)
      const std::string& version = shader_cache[*hash]->type_and_version;
      job->result.Path("file", shaders_dump_path / (Shader::Hash_NumToStr(*hash, true) + (version.empty() ? "" : "." + version) + ".cso"));
      return true;
   }

   bool RunShaderState(Job* job, DeviceData& device_data)
   {
      auto& w = job->result;
      const std::shared_lock lock_generic(s_mutex_generic);
      const std::shared_lock lock_loading(s_mutex_loading);
      if (const auto hash = job->HashArg("hash"))
      {
         w.Hash("hash", *hash);
         w.Key("pipelines").BeginArray();
         if (const auto it = device_data.pipeline_caches_by_shader_hash.find(*hash); it != device_data.pipeline_caches_by_shader_hash.end())
         {
            for (const auto* pipeline : it->second)
            {
               w.BeginObject().Field("stage", pipeline->StageName()).Field("replaced", ReplacementName(*pipeline)).Field("cloned", pipeline->cloned);
               w.Field("replace_draw_type", Shader::CachedPipeline::shader_replace_draw_type_names[size_t(pipeline->replace_draw_type)]).EndObject();
            }
         }
         w.EndArray();
         if (const auto* custom_shader = FindCustomShader(*hash))
            WriteCustomShader(&w, *custom_shader, job->BoolArg("disasm", false));
         return true;
      }
      w.Field("compilation_errors", shaders_compilation_errors);
      w.Key("replaced").BeginArray();
      for (const auto& [handle, pipeline] : device_data.pipeline_cache_by_pipeline_handle)
      {
         if (!pipeline || !IsReplaced(*pipeline))
            continue;
         w.BeginObject().Hash("hash", pipeline->shader_hashes[0]).Field("stage", pipeline->StageName()).Field("replaced", ReplacementName(*pipeline));
         if (const auto* custom_shader = FindCustomShader(pipeline->shader_hashes[0]))
            w.Path("file", custom_shader->file_path.filename());
         w.EndObject();
      }
      w.EndArray();
      return true;
   }

   bool RunReloadShaders(Job* job, DeviceData& device_data)
   {
      if (job->phase == 0)
      {
         // Like the dev UI's "Reload Shaders" / "Unload Shaders" buttons, processed in "OnReShadePresent"
         ForceToggleShaders(device_data, !job->BoolArg("unload", false));
         job->phase = 1;
         return false;
      }
      if (needs_load_shaders || needs_unload_shaders)
         return false;
      job->result.Field("file_clones", CountFileClones(device_data));
      const std::shared_lock lock_loading(s_mutex_loading);
      job->result.Field("compilation_errors", shaders_compilation_errors);
      return true;
   }

   bool RunGetSettings(Job* job)
   {
      auto& w = job->result;
      {
         const std::shared_lock lock_reshade(s_mutex_reshade);
         const auto& settings = cb_luma_global_settings;
         w.Field("display_mode", display_mode_preset_strings[std::min<size_t>(size_t(settings.DisplayMode), std::size(display_mode_preset_strings) - 1)]);
         w.Field("scene_peak_white", settings.ScenePeakWhite).Field("scene_paper_white", settings.ScenePaperWhite).Field("ui_paper_white", settings.UIPaperWhite);
         w.Field("sr_type", settings.SRType).Field("frame_index", settings.FrameIndex);
         w.Key("dev_settings").BeginArray();
         for (size_t i = 0; i < CB::LumaDevSettings::SettingsNum; i++)
         {
            w.BeginObject().Field("index", i).Field("name", cb_luma_dev_settings_names[i]).Field("value", settings.DevSettings.Settings[i]);
            w.Field("default", cb_luma_dev_settings_default_value.Settings[i]).Field("min", cb_luma_dev_settings_min_value.Settings[i]).Field("max", cb_luma_dev_settings_max_value.Settings[i]).EndObject();
         }
         w.EndArray();
         // The per game struct has no reflection, its layout is in the game's "main.cpp" and shaders
         const float* game_settings = reinterpret_cast<const float*>(&settings.GameSettings);
         w.Key("game_settings").BeginArray();
         for (size_t i = 0; i < sizeof(settings.GameSettings) / sizeof(float); i++)
            w.BeginObject().Field("index", i).Field("float", game_settings[i]).Field("uint", std::bit_cast<uint32_t>(game_settings[i])).EndObject();
         w.EndArray();
      }
      const std::shared_lock lock_defines(s_mutex_shader_defines);
      // Define values are a single character
      const auto value_of = [](const auto& define)
      { return std::string_view(define.GetValue(), strnlen(define.GetValue(), 1)); };
      {
         // Everything the custom shaders were compiled with, Core's own defines included ("GAME_*", "LUMA_*_CB_INDEX"...)
         const std::shared_lock lock_loading(s_mutex_loading);
         w.Key("compile_defines").BeginObject();
         for (size_t i = 0; i + 1 < custom_shaders_compile_defines.size(); i += 2)
            w.Field(custom_shaders_compile_defines[i].c_str(), custom_shaders_compile_defines[i + 1]);
         w.EndObject();
      }
      w.Key("defines").BeginArray();
      for (const auto& define : shader_defines_data)
      {
         if (define.IsEmpty())
            continue;
         w.BeginObject().Field("name", define.editable_data.GetName()).Field("value", value_of(define.editable_data));
         w.Field("compiled", value_of(define.compiled_data)).Field("default", value_of(define.default_data));
         w.Field("editable", define.IsValueEditable());
         if (define.HasTooltip())
            w.Field("tooltip", define.GetTooltip());
         w.EndObject();
      }
      w.EndArray();
      return true;
   }

   bool RunSetSetting(Job* job, DeviceData& device_data)
   {
      const std::string* name = job->Arg("name");
      const std::string* value = job->Arg("value");
      if (!name || !value)
      {
         job->error = "Pass \"name\" and \"value\"";
         return true;
      }
      if (name->starts_with("define:"))
      {
         const std::string define_name = name->substr(7);
         const std::unique_lock lock_defines(s_mutex_shader_defines);
         for (auto& define : shader_defines_data)
         {
            if (define_name != define.editable_data.GetName())
               continue;
            if (!define.IsValueEditable() || value->size() != 1)
            {
               job->error = "The define is not editable, or the value is not a single character";
               return true;
            }
            define.SetValue((*value)[0]);
            job->result.Field("needs_compilation", define.NeedsCompilation());
            return true;
         }
         job->error = "Unknown define " + define_name;
         return true;
      }

      const std::unique_lock lock_reshade(s_mutex_reshade);
      auto& settings = cb_luma_global_settings;
      float* target = nullptr;
      if (*name == "scene_peak_white")
         target = &settings.ScenePeakWhite;
      else if (*name == "scene_paper_white")
         target = &settings.ScenePaperWhite;
      else if (*name == "ui_paper_white")
         target = &settings.UIPaperWhite;
      else if (name->starts_with("dev:") || name->starts_with("game:"))
      {
         const bool dev = name->starts_with("dev:");
         const size_t index = std::strtoul(name->c_str() + (dev ? 4 : 5), nullptr, 0);
         float* base = dev ? settings.DevSettings.Settings : reinterpret_cast<float*>(&settings.GameSettings);
         const size_t count = dev ? CB::LumaDevSettings::SettingsNum : sizeof(settings.GameSettings) / sizeof(float);
         if (index < count)
            target = base + index;
      }
      if (!target)
      {
         job->error = "Unknown setting, use scene_peak_white, scene_paper_white, ui_paper_white, dev:<index>, game:<index> or define:<NAME>";
         return true;
      }
      // "game:" slots can be uints, pass "as_uint=1" to write the raw bits
      *target = job->BoolArg("as_uint", false) ? std::bit_cast<float>(uint32_t(std::strtoul(value->c_str(), nullptr, 0))) : std::strtof(value->c_str(), nullptr);
      device_data.cb_luma_global_settings_dirty = true;
      job->result.Field("value", *target).Field("note", "Not persisted to the config, games that rewrite these every frame will override it");
      return true;
   }

   // Lets a knob change settle (history resets, targets rebuilt) before the bridge reads back again
   bool RunWaitFrames(Job* job)
   {
      if (++job->frames < uint32_t(job->IntArg("frames", 1, 1, 600)))
         return false;
      job->result.Field("frames", job->frames).Field("frame_index", cb_luma_global_settings.FrameIndex);
      return true;
   }

   bool RunJob(Job* job, DeviceData& device_data, CommandListData& cmd_list_data, ID3D11DeviceContext* native_device_context, reshade::api::swapchain* swapchain)
   {
      const std::string& tool = job->tool;
      if (tool == "status")
         return RunStatus(job, device_data);
      if (tool == "wait_frames")
         return RunWaitFrames(job);
      if (tool == "trace_capture")
         return RunTraceCapture(job, device_data, cmd_list_data);
      if (tool == "trace_list")
         return RunTraceList(job, device_data, cmd_list_data);
      if (tool == "trace_get")
         return RunTraceGet(job, device_data, cmd_list_data);
      if (tool == "read_resource")
         return RunReadResource(job, device_data, cmd_list_data, native_device_context, swapchain);
      if (tool == "read_cbuffer")
         return RunReadConstantBuffer(job, device_data, cmd_list_data, native_device_context);
      if (tool == "shader_state")
         return RunShaderState(job, device_data);
      if (tool == "reload_shaders")
         return RunReloadShaders(job, device_data);
      if (tool == "get_settings")
         return RunGetSettings(job);
      if (tool == "set_setting")
         return RunSetSetting(job, device_data);
      if (tool == "dev_values")
         return RunDevValues(job, device_data);
      if (tool == "set_dev_value")
         return RunSetDevValue(job, device_data);
      if (tool == "sr_state")
         return RunSrState(job, device_data);
      if (tool == "sr_capture")
         return RunSrCapture(job, native_device_context);
      if (tool == "shader_list")
         return RunShaderList(job, device_data);
      if (tool == "dump_shader")
         return RunDumpShader(job);
      job->error = "Unknown tool " + tool;
      return true;
   }

   bool ReadExact(HANDLE pipe, void* data, DWORD size)
   {
      auto* bytes = static_cast<char*>(data);
      while (size > 0)
      {
         DWORD read = 0;
         if (!ReadFile(pipe, bytes, size, &read, nullptr) || read == 0)
            return false;
         bytes += read;
         size -= read;
      }
      return true;
   }

   std::shared_ptr<Job> ParseRequest(const std::string& request)
   {
      auto job = std::make_shared<Job>();
      size_t line_start = 0;
      bool first_line = true;
      while (line_start <= request.size())
      {
         size_t line_end = request.find('\n', line_start);
         if (line_end == std::string::npos)
            line_end = request.size();
         std::string_view line(request.data() + line_start, line_end - line_start);
         if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
         if (first_line)
            job->tool = line;
         else if (const size_t equals = line.find('='); equals != std::string_view::npos)
            job->args.emplace(line.substr(0, equals), line.substr(equals + 1));
         first_line = false;
         line_start = line_end + 1;
      }
      return job;
   }

   // "status" answered by the pipe thread while the game doesn't present (minimized, paused, hung, or a loading screen that doesn't
   // present), when every queued tool would only time out
   std::optional<std::string> StatusWithoutPresents()
   {
      const uint64_t since_present_ms = GetTickCount64() - last_present_ms;
      if (since_present_ms < 1000)
         return std::nullopt;
      JsonWriter w;
      w.BeginObject().Field("ok", true).Field("game", Globals::GAME_NAME).Field("pid", GetCurrentProcessId());
      w.Field("presenting", false).Field("ms_since_present", since_present_ms).Field("frame_index", last_frame_index.load());
      {
         const std::lock_guard lock(s_mutex_jobs);
         w.Field("pending_jobs", pending_jobs.size()).Field("active_tool", active_tool);
      }
      w.Field("note", "Answered without the render thread: the game isn't presenting, the other tools would time out").EndObject();
      return std::move(w.out);
   }

   // One connected client's requests, on its own thread
   void ServeClient(HANDLE pipe)
   {
      connected_clients++;
      const auto write_response = [pipe](const std::string& response)
      {
         const uint32_t response_size = uint32_t(response.size());
         DWORD written = 0;
         return WriteFile(pipe, &response_size, sizeof(response_size), &written, nullptr) && WriteFile(pipe, response.data(), response_size, &written, nullptr) && written == response_size;
      };
      uint32_t size = 0;
      while (!server_stop && ReadExact(pipe, &size, sizeof(size)) && size <= (1u << 20))
      {
         std::string request(size, '\0');
         if (!ReadExact(pipe, request.data(), size))
            break;
         auto job = ParseRequest(request);
         if (job->tool == "status")
         {
            if (const std::optional<std::string> status = StatusWithoutPresents())
            {
               if (!write_response(*status))
                  break;
               continue;
            }
         }
         const DWORD timeout_ms = DWORD(job->IntArg("timeout_ms", 15000, 100, 600000)); // Mirrored by "TIMEOUT_MS" in "Scripts/luma_mcp.py"
         // Readbacks may only land inside "ReadbackRoot()": refused before the job runs, and checked again per file below (the stems come from arguments too)
         // Not "Arg()", so a tool that doesn't take it still reports it in "ignored_args"
         const auto out_dir = job->args.find("out_dir");
         const bool refused = out_dir != job->args.end() && !out_dir->second.empty() && !IsInReadbackRoot(Utf8Path(out_dir->second));
         if (!refused)
         {
            const std::lock_guard lock(s_mutex_jobs);
            pending_jobs.push_back(job);
            has_pending_jobs = true;
         }
         const ULONGLONG start = GetTickCount64();
         bool finished = false;
         while (!refused && !server_stop && !(finished = WaitForSingleObject(job->done, 50) == WAIT_OBJECT_0) && GetTickCount64() - start < timeout_ms)
         {
         }
         // A timed out job that never started is dropped (nobody reads its result), a started one finishes (and restores the dev UI state) on its own
         bool dropped = false;
         std::string running_tool;
         if (!refused && !finished)
         {
            const std::lock_guard lock(s_mutex_jobs);
            dropped = std::erase(pending_jobs, job) != 0;
            has_pending_jobs = !pending_jobs.empty();
            running_tool = active_tool;
         }
         const auto path_error = [](std::string_view error, std::string_view key, const std::filesystem::path& path)
         {
            JsonWriter w;
            w.BeginObject().Field("ok", false).Field("error", error).Path(key, path).EndObject();
            return std::move(w.out);
         };
         const auto timeout_error = [&]
         {
            const std::string_view reason = (dropped ? "it was queued behind an earlier call that is still running, and was dropped" : "the game might not be presenting (minimized or paused?) or the job is waiting for a pass that doesn't draw");
            JsonWriter w;
            w.BeginObject().Field("ok", false).Field("error", std::format("Timed out after {} ms, {}", timeout_ms, reason));
            w.Field("ms_since_present", GetTickCount64() - last_present_ms).Field("active_tool", running_tool).EndObject();
            return std::move(w.out);
         };
         constexpr std::string_view outside_root = "out_dir must be the \"root\" folder or a subfolder of it";
         std::string response;
         if (refused)
         {
            response = path_error(outside_root, "root", ReadbackRoot());
         }
         else if (finished)
         {
            response = std::move(job->result.out);
         }
         else
         {
            response = timeout_error();
         }
         // Readbacks are written here rather than on the render thread
         for (const auto& [path, bytes] : job->files)
         {
            if (!IsInReadbackRoot(path.parent_path()))
            {
               response = path_error(outside_root, "root", ReadbackRoot());
               break;
            }
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file.write(bytes.data(), std::streamsize(bytes.size())))
            {
               response = path_error("Failed to write", "path", path);
               break;
            }
         }
         if (!write_response(response))
            break;
      }
      connected_clients--;
      DisconnectNamedPipe(pipe);
      CloseHandle(pipe);
   }

   void ServerThread()
   {
      // The default pipe security of an elevated game (e.g. a "Run as administrator" compatibility flag) has a high integrity label and no
      // write access for its user, so a non elevated bridge couldn't talk to it: grant the game's user and lower the label to medium
      std::wstring sddl = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)";
      HANDLE token = nullptr;
      if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
      {
         const std::unique_ptr<void, decltype(&CloseHandle)> token_owner(token, &CloseHandle);
         alignas(TOKEN_USER) BYTE token_user[256];
         DWORD size = 0;
         LPWSTR user_sid = nullptr;
         if (GetTokenInformation(token, TokenUser, token_user, sizeof(token_user), &size) && ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(token_user)->User.Sid, &user_sid))
         {
            const std::unique_ptr<wchar_t, decltype(&LocalFree)> user_sid_owner(user_sid, &LocalFree);
            sddl += std::format(L"(A;;GA;;;{})", user_sid);
         }
      }
      sddl += L"S:(ML;;NW;;;ME)";
      SECURITY_ATTRIBUTES security_attributes = {sizeof(SECURITY_ATTRIBUTES)};
      if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &security_attributes.lpSecurityDescriptor, nullptr))
         security_attributes.lpSecurityDescriptor = nullptr;

      struct Client
      {
         std::thread thread;
         std::atomic<bool> finished = false;
      };
      std::vector<std::shared_ptr<Client>> clients;
      const auto unfinished = [&clients]
      {
         return std::ranges::any_of(clients, [](const std::shared_ptr<Client>& client)
            { return !client->finished; });
      };
      while (!server_stop)
      {
         // One listening instance at a time, each connected client keeps its own
         HANDLE pipe = CreateNamedPipeW(pipe_name.c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, PIPE_UNLIMITED_INSTANCES, 1 << 16, 1 << 16, 0, &security_attributes);
         if (pipe == INVALID_HANDLE_VALUE)
         {
            Sleep(1000);
            continue;
         }
         if (!(ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED) || server_stop)
         {
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
            continue;
         }
         std::erase_if(clients, [](const std::shared_ptr<Client>& client)
            {
               if (!client->finished)
                  return false;
               client->thread.join();
               return true; });
         auto client = std::make_shared<Client>();
         client->thread = std::thread([client, pipe]
            {
               ServeClient(pipe);
               client->finished = true; });
         clients.push_back(std::move(client));
      }
      // Not joined: on DLL unload the loader lock keeps a thread from exiting (see "Shutdown"). Cancelled repeatedly, a client could
      // enter a blocking read right after a cancel.
      for (int i = 0; i < 150 && unfinished(); i++)
      {
         for (const auto& client : clients)
         {
            if (!client->finished)
            {
               CancelSynchronousIo(client->thread.native_handle());
            }
         }
         Sleep(10);
      }
      for (const auto& client : clients)
      {
         client->thread.detach();
      }
      LocalFree(security_attributes.lpSecurityDescriptor);
      server_running = false;
   }
   // Called from "Init()": starts the pipe server and registers Core's knobs
   void Start()
   {
      // The live safe Core knobs. Not here: "debug_draw_*" (the readbacks drive them), "trace_ignore_*" ("luma_trace_list" filters
      // instead), and what needs shaders, resources or samplers recreated ("custom_sdr_gamma", "enable_ui_separation", the mip bias).
      RegisterToggles({{"core.hide_ui", &hide_ui}, {"core.force_disable_display_composition", &force_disable_display_composition}, {"core.ignore_upgraded_samplers", &ignore_upgraded_samplers},
         {"core.enable_upgraded_texture_resource_copy_redirection", &enable_upgraded_texture_resource_copy_redirection}});
      RegisterInts({{"core.frame_sleep_ms", &frame_sleep_ms, 0, 100}, {"core.frame_sleep_interval", &frame_sleep_interval, 1, 30}});
#if ENABLE_FIDELITY_SK && DEVELOPMENT && !defined(NDEBUG) // As "FidelityFX::draw_debug_view"
      RegisterToggles({{"core.fsr_debug_view", &FidelityFX::draw_debug_view}});
#endif
      // "DrawSMAA" and "DrawBloom"'s intermediates, the bloom's mips through "mip"
      RegisterTextures({{"core.smaa_edges", [](DeviceData& device_data) -> ID3D11DeviceChild*
                           { return device_data.managed_resources.shader_resource_views["smaa_edge_detection"_h].get(); }},
         {"core.smaa_weights", [](DeviceData& device_data) -> ID3D11DeviceChild*
            { return device_data.managed_resources.shader_resource_views["smaa_blending_weight_calculation"_h].get(); }},
         {"core.bloom", [](DeviceData&) -> ID3D11DeviceChild*
            { return draw_bloom_mips.srv_mips_y.empty() ? nullptr : draw_bloom_mips.srv_mips_y[0]; }}});

      // "Init()" runs again when ReShade re-inits the addon without unloading it (The Witcher creates a probe D3D9 device first):
      // assigning over the first run's joinable thread would call std::terminate. Keep that server, or restart one that has exited.
      if (server_thread.joinable())
      {
         if (server_running)
            return;
         server_thread.join();
      }
      pipe_name = L"\\\\.\\pipe\\luma-mcp-" + std::to_wstring(GetCurrentProcessId());
      server_running = true;
      server_thread = std::thread(ServerThread);
   }

   // Called from "OnPresent" on the render thread, before display composition
   void OnPresent(DeviceData& device_data, CommandListData& cmd_list_data, ID3D11DeviceContext* native_device_context, reshade::api::swapchain* swapchain)
   {
      last_present_ms = GetTickCount64();
      last_frame_index = cb_luma_global_settings.FrameIndex;
      if (!active_job && !has_pending_jobs) // Unlocked early out, every present comes through here
         return;
      if (!active_job)
      {
         const std::lock_guard lock(s_mutex_jobs);
         if (pending_jobs.empty())
            return;
         active_job = pending_jobs.front();
         pending_jobs.erase(pending_jobs.begin());
         has_pending_jobs = !pending_jobs.empty();
         active_tool = active_job->tool;
      }
      // Multi frame jobs stay on the device they started on
      if (active_job->device_data && active_job->device_data != &device_data)
         return;
      active_job->device_data = &device_data;

      Job& job = *active_job;
      if (!RunJob(&job, device_data, cmd_list_data, native_device_context, swapchain))
         return;
      if (job.saved_debug_draw)
      {
         job.saved_debug_draw->Apply();
         device_data.debug_draw_texture = nullptr;
      }
      if (job.saved_track_buffer)
         job.saved_track_buffer->Apply();
      if (!job.error.empty())
      {
         job.result = {};
         job.result.BeginObject().Field("ok", false).Field("error", job.error);
      }
      // Arguments no tool read (a typo, or a key the backend doesn't know) would otherwise silently fall back to defaults
      std::vector<std::string_view> ignored_args;
      for (const auto& [key, value] : job.args)
         if (!job.read_args.contains(key))
            ignored_args.push_back(key);
      if (!ignored_args.empty())
      {
         job.result.Key("ignored_args").BeginArray();
         for (const std::string_view key : ignored_args)
            job.result.Value(key);
         job.result.EndArray();
      }
      job.result.EndObject();
      SetEvent(active_job->done);
      active_job.reset();
      const std::lock_guard lock(s_mutex_jobs);
      active_tool.clear();
   }

   // Called on DLL unload
   void Shutdown()
   {
      if (!server_thread.joinable())
         return;
      server_stop = true;
      // Unblock the pending connection wait by connecting to ourselves; the server thread then cancels its clients' reads
      CancelSynchronousIo(server_thread.native_handle());
      HANDLE self = CreateFileW(pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
      if (self != INVALID_HANDLE_VALUE)
         CloseHandle(self);
      server_thread.detach();
      for (int i = 0; i < 200 && server_running; i++)
         Sleep(10);
   }
} // namespace Mcp
