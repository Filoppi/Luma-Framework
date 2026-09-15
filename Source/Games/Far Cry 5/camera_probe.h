#pragma once
// Camera bindings come from live shader bytecode, not developer shader-dump files.
// No game memory patches and no writes to the game's constant buffers.
namespace FC5
{
   struct CameraProbe
   {
      struct Binding { int slot = -1; UINT used = 0; };
      static inline std::unordered_map<uint64_t, Binding> bindings;
      static inline std::mutex bindings_mutex;
      std::unordered_set<ID3D11Buffer*> sampled;
      std::vector<CameraJitter> candidates;
      com_ptr<ID3D11Buffer> staging;
      PerfCounter readback_cpu;
      PerfCounter detection_cpu;
      std::filesystem::file_time_type request_time{};

      static void OnPipeline(reshade::api::device* device, reshade::api::pipeline_layout,
         uint32_t count, const reshade::api::pipeline_subobject* subobjects, reshade::api::pipeline)
      {
         if (device->get_api() != reshade::api::device_api::d3d11 || !Shader::d3d_reflect) return;
         for (uint32_t i = 0; i < count; ++i)
         {
         if (subobjects[i].type != reshade::api::pipeline_subobject_type::vertex_shader &&
            subobjects[i].type != reshade::api::pipeline_subobject_type::pixel_shader) continue;
         if (subobjects[i].count != 1 || !subobjects[i].data) continue;
         const auto& shader = *static_cast<const reshade::api::shader_desc*>(subobjects[i].data);
         if (!shader.code || !shader.code_size) continue;
         const auto hash = Shader::BinToHash(static_cast<const uint8_t*>(shader.code), shader.code_size);
         { std::lock_guard lock(bindings_mutex); if (bindings.contains(hash)) continue; }
         Binding result;
            com_ptr<ID3D11ShaderReflection> reflection;
            if (SUCCEEDED(Shader::d3d_reflect(shader.code, shader.code_size, IID_PPV_ARGS(&reflection))))
            {
               D3D11_SHADER_INPUT_BIND_DESC resource{};
               const char* name = "CViewportShaderParameterProvider";
               if (SUCCEEDED(reflection->GetResourceBindingDescByName(name, &resource)))
               {
                  auto* cb = reflection->GetConstantBufferByName(name);
                  D3D11_SHADER_VARIABLE_DESC projection{}, inverse{}, viewrot{}, previous{};
                  if (SUCCEEDED(cb->GetVariableByName("ProjectionMatrix")->GetDesc(&projection)) &&
                     SUCCEEDED(cb->GetVariableByName("InvProjectionMatrix")->GetDesc(&inverse)) &&
                     SUCCEEDED(cb->GetVariableByName("ViewRotProjectionMatrix")->GetDesc(&viewrot)) &&
                     SUCCEEDED(cb->GetVariableByName("ViewRotProjectionMatrix_Previous")->GetDesc(&previous)) &&
                     projection.StartOffset == 128 && inverse.StartOffset == 0 &&
                     viewrot.StartOffset == 320 && previous.StartOffset == 384)
                  {
                     result.used = ((projection.uFlags & D3D_SVF_USED) ? 1 : 0) |
                        ((inverse.uFlags & D3D_SVF_USED) ? 2 : 0) |
                        ((viewrot.uFlags & D3D_SVF_USED) ? 4 : 0) |
                        ((previous.uFlags & D3D_SVF_USED) ? 8 : 0);
                     if (result.used) result.slot = int(resource.BindPoint);
                  }
               }
            }
         std::lock_guard lock(bindings_mutex);
         bindings.emplace(hash, result);
         }
      }
      Binding Find(uint64_t hash, bool)
      {
         std::lock_guard lock(bindings_mutex);
         const auto it = bindings.find(hash);
         return it == bindings.end() ? Binding{} : it->second;
      }

      void Capture(ID3D11Device* device, ID3D11DeviceContext* ctx, uint64_t hash, bool pixel, uint64_t frame, bool log = true,
         UINT output_width = 0, UINT output_height = 0)
      {
         if (sampled.size() >= 4) return;
#if DEVELOPMENT
         PerfScope detection_timer(detection_cpu);
#endif
         const auto binding = Find(hash, pixel);
         if (binding.slot < 0) return;
         // Reject non-camera shaders before querying context interfaces/bindings.
         // Same predicate as the live filter below; discovery still sees all CBs.
         if (!log && (pixel || !(binding.used & 4))) return;
         com_ptr<ID3D11DeviceContext1> ctx1;
         com_ptr<ID3D11Buffer> cb;
         UINT first = 0, count = 4096;
         if (SUCCEEDED(ctx->QueryInterface(&ctx1)))
         {
            if (pixel) ctx1->PSGetConstantBuffers1(binding.slot, 1, &cb, &first, &count);
            else ctx1->VSGetConstantBuffers1(binding.slot, 1, &cb, &first, &count);
         }
         else if (pixel) ctx->PSGetConstantBuffers(binding.slot, 1, &cb);
         else ctx->VSGetConstantBuffers(binding.slot, 1, &cb);
         if (!cb || sampled.contains(cb.get())) return;
         if (!log)
         {
            // Do not spend a blocking read on data that cannot produce a live candidate.
            D3D11_VIEWPORT viewport{}; UINT viewport_count = 1;
            ctx->RSGetViewports(&viewport_count, &viewport);
            if (viewport_count != 1 || !IsSceneCameraViewport(viewport, output_width, output_height)) return;
         }
         D3D11_BUFFER_DESC desc{};
         cb->GetDesc(&desc);
         constexpr UINT bytes = 896;
         const uint64_t offset = uint64_t(first) * 16;
         if (count * 16ull < bytes || offset + bytes > desc.ByteWidth) return;
         const auto upload = UploadProbe::BeforeRead(device, cb.get(), desc, offset);
         CameraJitter uploaded_candidate;
         // Discovery keeps full GPU logging. Live path keeps the same shader,
         // viewport and matrix validation; the guard also demands fresh uploads,
         // an immediate-only device and periodic full-byte GPU verification.
         if (!log && !pixel && (binding.used & 4) && UploadProbe::TryUse(device, ctx, upload, uploaded_candidate))
         {
            sampled.insert(cb.get());
            candidates.push_back(uploaded_candidate);
            return;
         }
         if (!staging)
         {
            D3D11_BUFFER_DESC s{};
            s.ByteWidth = bytes; s.Usage = D3D11_USAGE_STAGING; s.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            if (FAILED(device->CreateBuffer(&s, nullptr, &staging))) return;
         }
         D3D11_BOX box{ UINT(offset), 0, 0, UINT(offset + bytes), 1, 1 };
         const auto readback_start = PerfCounter::Clock::now();
         ctx->CopySubresourceRegion(staging.get(), 0, 0, 0, 0, cb.get(), 0, &box);
         D3D11_MAPPED_SUBRESOURCE mapped{};
         if (FAILED(ctx->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)))
         { readback_cpu.Add(readback_start); return; }
         float data[bytes / 4];
         std::memcpy(data, mapped.pData, bytes);
         ctx->Unmap(staging.get(), 0);
         readback_cpu.Add(readback_start);
         UploadProbe::Compare(device, upload, data);
         // Shadow views are useful for identification too; log actual viewport,
         // do not mistake the first camera-like CB for the main camera.
         sampled.insert(cb.get());
         CameraJitter candidate;
         if (!pixel && (binding.used & 4) && ValidateCameraJitter(data, candidate))
            candidates.push_back(candidate);
         if (!log) return;
         char line[320];
         std::snprintf(line, sizeof(line), "[FC5 camera] frame=%llu hash=%08X stage=%s slot=%d buffer=%p used=0x%X",
            frame, uint32_t(hash), pixel ? "PS" : "VS", binding.slot, cb.get(), binding.used);
         reshade::log::message(reshade::log::level::info, line);
         for (UINT row = 0; row < 56; ++row)
         {
            if (row >= 28 && row < 31) continue;
            if (row >= 35 && row < 40) continue;
            std::snprintf(line, sizeof(line), "[FC5 camera] frame=%llu buffer=%p c%u=%.9g,%.9g,%.9g,%.9g",
               frame, cb.get(), row, data[row * 4], data[row * 4 + 1], data[row * 4 + 2], data[row * 4 + 3]);
            reshade::log::message(reshade::log::level::info, line);
         }
      }

      bool Requested()
      {
         std::error_code error;
         const auto path = ControlDirectory() / "camera.request";
         const auto time = std::filesystem::last_write_time(path, error);
         if (error || time == request_time) return false;
         request_time = time;
         return true;
      }
   };
}
