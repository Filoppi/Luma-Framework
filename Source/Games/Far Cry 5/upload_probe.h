#pragma once
#include "upload_snapshot.h"
#include "native_upload_hooks.h"
#include <mutex>

namespace FC5::UploadProbe
{
   struct Entry
   {
      UploadSnapshot snapshot; UploadTrust trust; UINT size = 0;
      uint64_t compared_generation = 0, map_context = 0;
      bool publish_pending = false;
   };
   struct Stats
   {
      uint64_t comparisons = 0, missing = 0, exact = 0, jitter_equal = 0, jitter_different = 0;
      uint64_t reused = 0, uploads = 0, rejected = 0, capped = 0;
      uint64_t fast = 0, gpu = 0, invalidations = 0, stale = 0;
      uint64_t deferred_uploads = 0, secondary = 0;
   };
   struct Device
   {
      std::unordered_map<uint64_t, Entry> buffers;
      Stats stats;
      PerfCounter copy_cpu;
      UploadDeviceGate gate;
      uint64_t epoch = 1;
   };
   inline std::mutex mutex;
   inline std::unordered_map<uint64_t, Device> devices;
   struct Context { uint64_t device = 0; bool immediate = false, ready = false; };
   inline std::unordered_map<uint64_t, Context> contexts;
   struct Sample
   {
      std::array<float, 224> rows{};
      bool valid = false;
      uint64_t resource = 0, generation = 0, epoch = 0;
      bool fresh = false;
   };

   // Watch only buffers selected by the existing camera probe, not every upload.
   // First use misses intentionally; subsequent Map/Unmap pairs can be observed.
   inline Sample BeforeRead(ID3D11Device* device, ID3D11Buffer* buffer, const D3D11_BUFFER_DESC& desc, uint64_t offset)
   {
      std::lock_guard lock(mutex);
      const auto key = reinterpret_cast<uint64_t>(device);
      if (!devices.contains(key) && devices.size() >= 8) return {};
      auto& d = devices[key];
      ++d.stats.comparisons;
      if (desc.BindFlags != D3D11_BIND_CONSTANT_BUFFER || desc.Usage != D3D11_USAGE_DYNAMIC ||
         !(desc.CPUAccessFlags & D3D11_CPU_ACCESS_WRITE) || desc.ByteWidth > 65536 || desc.MiscFlags != 0)
      { ++d.stats.missing; return {}; }
      const auto resource = reinterpret_cast<uint64_t>(buffer);
      if (!d.buffers.contains(resource) && d.buffers.size() >= 64)
      { ++d.stats.capped; ++d.stats.missing; return {}; }
      auto& e = d.buffers[resource];
      if (e.size != desc.ByteWidth || e.snapshot.offset != offset)
      {
         e = {}; e.size = desc.ByteWidth; e.snapshot.offset = offset;
      }
      if (!e.snapshot.valid) { ++d.stats.missing; return {}; }
      if (e.compared_generation == e.snapshot.generation) ++d.stats.reused;
      e.compared_generation = e.snapshot.generation;
      const bool fresh = e.snapshot.Fresh(d.epoch, GetCurrentThreadId());
      if (!fresh) ++d.stats.stale;
      return { e.snapshot.rows, true, resource, e.snapshot.generation, d.epoch, fresh };
   }
   inline bool TryUse(ID3D11Device* device, ID3D11DeviceContext* ctx, const Sample& cpu, CameraJitter& jitter)
   {
      if (!cpu.valid || !cpu.fresh || ctx->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE ||
         !ValidateCameraJitter(cpu.rows.data(), jitter)) return false;
      std::lock_guard lock(mutex);
      const auto d = devices.find(reinterpret_cast<uint64_t>(device));
      if (d == devices.end() || !d->second.gate.Safe() || d->second.epoch != cpu.epoch) return false;
      const auto e = d->second.buffers.find(cpu.resource);
      if (e == d->second.buffers.end() || e->second.snapshot.generation != cpu.generation ||
         !e->second.snapshot.Fresh(cpu.epoch, GetCurrentThreadId()) ||
         !e->second.trust.Ready(cpu.epoch, jitter.width, jitter.height)) return false;
      ++d->second.stats.fast;
      return true;
   }
   inline void Compare(ID3D11Device* device, const Sample& cpu, const float* gpu)
   {
      CameraJitter a{}, b{};
      const bool jitter_equal = cpu.valid && ValidateCameraJitter(cpu.rows.data(), a) && ValidateCameraJitter(gpu, b) &&
         a.width == b.width && a.height == b.height && a.x == b.x && a.y == b.y;
      std::lock_guard lock(mutex);
      const auto it = devices.find(reinterpret_cast<uint64_t>(device));
      if (it == devices.end()) return;
      auto& d = it->second;
      auto& s = d.stats;
      ++s.gpu;
      if (!cpu.valid) return;
      const bool exact = std::memcmp(cpu.rows.data(), gpu, UploadSnapshot::Bytes) == 0;
      if (exact) ++s.exact;
      if (jitter_equal) ++s.jitter_equal; else ++s.jitter_different;
      // A fresh snapshot disagreeing with the GPU permanently disables this device's
      // fast path. The current draw still consumes GPU data, never the bad snapshot.
      if (cpu.fresh && !exact) d.gate.audit_failed = true;
      const auto e = d.buffers.find(cpu.resource);
      if (e != d.buffers.end() && e->second.snapshot.generation == cpu.generation && cpu.epoch == d.epoch)
      {
         if (cpu.fresh && exact && jitter_equal && e->second.snapshot.Fresh(cpu.epoch, GetCurrentThreadId()))
            e->second.trust.Confirm(cpu.epoch, a.width, a.height);
         else e->second.trust.Invalidate();
      }
   }
   inline void OnNativeMap(ID3D11DeviceContext* ctx, ID3D11Resource* resource,
      UINT subresource, D3D11_MAP access, const D3D11_MAPPED_SUBRESOURCE* data)
   {
      if (subresource != 0) return;
      std::lock_guard lock(mutex);
      const auto c = contexts.find(reinterpret_cast<uint64_t>(ctx));
      if (c == contexts.end() || !c->second.ready) return;
      const auto d = devices.find(c->second.device);
      if (d == devices.end()) return;
      const auto e = d->second.buffers.find(reinterpret_cast<uint64_t>(resource));
      if (e == d->second.buffers.end()) return;
      auto& entry = e->second;
      entry.publish_pending = false;
      entry.map_context = reinterpret_cast<uint64_t>(ctx);
      if (!c->second.immediate)
      {
         entry.snapshot.Invalidate(); entry.trust.Invalidate();
         ++d->second.stats.deferred_uploads;
         return;
      }
      entry.snapshot.Begin(data ? data->pData : nullptr, 0, UINT64_MAX, entry.size,
         access != D3D11_MAP_READ, GetCurrentThreadId(), d->second.epoch, access == D3D11_MAP_WRITE_DISCARD);
   }
   inline void BeforeNativeUnmap(ID3D11DeviceContext* ctx, ID3D11Resource* resource, UINT subresource)
   {
      if (subresource != 0) return;
      std::lock_guard lock(mutex);
      const auto c = contexts.find(reinterpret_cast<uint64_t>(ctx));
      if (c == contexts.end() || !c->second.ready || !c->second.immediate) return;
      const auto d = devices.find(c->second.device);
      if (d == devices.end()) return;
      const auto e = d->second.buffers.find(reinterpret_cast<uint64_t>(resource));
      if (e == d->second.buffers.end()) return;
      const auto start = PerfCounter::Clock::now();
      auto& entry = e->second;
      entry.publish_pending = entry.map_context == reinterpret_cast<uint64_t>(ctx) &&
         entry.snapshot.End(GetCurrentThreadId(), d->second.epoch);
      entry.snapshot.valid = false; // Not visible until the original Unmap returns.
      if (!entry.publish_pending) ++d->second.stats.rejected;
      d->second.copy_cpu.Add(start);
   }
   inline void AfterNativeUnmap(ID3D11DeviceContext* ctx, ID3D11Resource* resource, UINT subresource)
   {
      if (subresource != 0) return;
      std::lock_guard lock(mutex);
      const auto c = contexts.find(reinterpret_cast<uint64_t>(ctx));
      if (c == contexts.end() || !c->second.ready || !c->second.immediate) return;
      const auto d = devices.find(c->second.device);
      if (d == devices.end()) return;
      const auto e = d->second.buffers.find(reinterpret_cast<uint64_t>(resource));
      if (e == d->second.buffers.end()) return;
      auto& entry = e->second;
      if (entry.publish_pending && entry.map_context == reinterpret_cast<uint64_t>(ctx) &&
         entry.snapshot.epoch == d->second.epoch && entry.snapshot.thread == GetCurrentThreadId())
      { entry.snapshot.valid = true; ++d->second.stats.uploads; }
      entry.publish_pending = false;
   }
   inline void OnInitDevice(reshade::api::device* device)
   {
      if (device->get_api() != reshade::api::device_api::d3d11) return;
      std::lock_guard lock(mutex);
      if (!devices.contains(device->get_native()) && devices.size() >= 8) return;
      devices[device->get_native()].gate.device_seen = true;
   }
   inline void OnInitCommandList(reshade::api::command_list* list)
   {
      auto* device = list->get_device();
      if (device->get_api() != reshade::api::device_api::d3d11) return;
      // D3D11 emits this for BOTH contexts and finished ID3D11CommandList objects.
      // QI instead of blindly treating the native pointer as a context.
      com_ptr<ID3D11DeviceContext> ctx;
      auto* native = reinterpret_cast<IUnknown*>(list->get_native());
      const bool is_context = native && SUCCEEDED(native->QueryInterface(IID_PPV_ARGS(&ctx)));
      const bool immediate = is_context && ctx->GetType() == D3D11_DEVICE_CONTEXT_IMMEDIATE;
      {
         std::lock_guard lock(mutex);
         if (!devices.contains(device->get_native()) && devices.size() >= 8) return;
         auto& gate = devices[device->get_native()].gate;
         if (immediate) gate.immediate_seen = true; else gate.deferred_seen = true;
         if (is_context)
         {
            if (contexts.size() >= 128) { gate.native_failed = true; return; }
            contexts[reinterpret_cast<uint64_t>(ctx.get())] = { device->get_native(), immediate, false };
         }
      }
      if (!is_context) return; // Finished lists are invalidated at secondary execution.
      // MinHook suspends threads; never hold the capture mutex while installing.
      const bool installed = NativeUploads::Install(ctx.get());
      {
         std::lock_guard lock(mutex);
         contexts[reinterpret_cast<uint64_t>(ctx.get())].ready = installed;
         auto& gate = devices[device->get_native()].gate;
         if (!installed) gate.native_failed = true;
         else if (immediate) gate.native_capture = true;
      }
      char line[180];
      std::snprintf(line, sizeof(line), "[FC5 upload context] native=%p immediate=%d hooks=%d", ctx.get(), immediate, installed);
      reshade::log::message(reshade::log::level::info, line);
   }
   inline void OnDestroyCommandList(reshade::api::command_list* list)
   {
      if (list->get_device()->get_api() != reshade::api::device_api::d3d11) return;
      std::lock_guard lock(mutex);
      contexts.erase(list->get_native());
   }
   inline void OnSecondary(reshade::api::command_list* list, reshade::api::command_list*)
   {
      auto* device = list->get_device();
      if (device->get_api() != reshade::api::device_api::d3d11) return;
      std::lock_guard lock(mutex);
      if (auto d = devices.find(device->get_native()); d != devices.end())
      {
         d->second.gate.deferred_seen = true;
         ++d->second.stats.secondary;
         for (auto& [key, entry] : d->second.buffers)
         { entry.snapshot.Invalidate(); entry.trust.Invalidate(); entry.publish_pending = false; }
      }
   }
   inline void Invalidate(reshade::api::device* device, reshade::api::resource resource)
   {
      if (device->get_api() != reshade::api::device_api::d3d11) return;
      std::lock_guard lock(mutex);
      const auto d = devices.find(device->get_native());
      if (d == devices.end()) return;
      const auto e = d->second.buffers.find(resource.handle);
      if (e == d->second.buffers.end()) return;
      e->second.snapshot.Invalidate(); e->second.trust.Invalidate();
      e->second.publish_pending = false;
      ++d->second.stats.invalidations;
   }
   inline bool OnCopy(reshade::api::command_list* list, reshade::api::resource, reshade::api::resource dest)
   { Invalidate(list->get_device(), dest); return false; }
   inline bool OnCopyBuffer(reshade::api::command_list* list, reshade::api::resource, uint64_t,
      reshade::api::resource dest, uint64_t, uint64_t)
   { Invalidate(list->get_device(), dest); return false; }
   inline bool OnUpdate(reshade::api::device* device, const void*, reshade::api::resource dest, uint64_t, uint64_t)
   { Invalidate(device, dest); return false; }
   inline bool OnUpdateCommand(reshade::api::command_list* list, const void*, reshade::api::resource dest, uint64_t, uint64_t)
   { Invalidate(list->get_device(), dest); return false; }
   inline void AdvanceFrame(ID3D11Device* device)
   {
      std::lock_guard lock(mutex);
      if (auto d = devices.find(reinterpret_cast<uint64_t>(device)); d != devices.end()) ++d->second.epoch;
   }
   inline void OnDestroyResource(reshade::api::device* device, reshade::api::resource resource)
   {
      if (device->get_api() != reshade::api::device_api::d3d11) return;
      std::lock_guard lock(mutex);
      if (auto d = devices.find(device->get_native()); d != devices.end()) d->second.buffers.erase(resource.handle);
   }
   inline void OnDestroyDevice(reshade::api::device* device)
   {
      if (device->get_api() != reshade::api::device_api::d3d11) return;
      std::lock_guard lock(mutex);
      devices.erase(device->get_native());
      std::erase_if(contexts, [&](const auto& c) { return c.second.device == device->get_native(); });
   }
   inline void Log(ID3D11Device* device, uint64_t frame)
   {
      Stats s{}; PerfCounter timing; size_t buffers = 0; UploadDeviceGate gate;
      {
         std::lock_guard lock(mutex);
         const auto it = devices.find(reinterpret_cast<uint64_t>(device));
         if (it == devices.end()) return;
         auto& d = it->second; s = d.stats; d.stats = {};
         timing = d.copy_cpu; d.copy_cpu = {}; buffers = d.buffers.size();
         gate = d.gate;
      }
      char line[800];
      std::snprintf(line, sizeof(line),
         "[FC5 upload] CONTEXT frame=%llu buffers=%zu requested=%llu fast=%llu gpu=%llu missing=%llu stale=%llu exact=%llu jitterEqual=%llu jitterDifferent=%llu reused=%llu uploads=%llu rejected=%llu capped=%llu invalidations=%llu deviceSeen=%d immediateSeen=%d deferredSeen=%d auditFailed=%d nativeCapture=%d nativeFailed=%d deferredUploads=%llu secondary=%llu",
         frame, buffers, s.comparisons, s.fast, s.gpu, s.missing, s.stale, s.exact, s.jitter_equal, s.jitter_different, s.reused, s.uploads, s.rejected, s.capped,
         s.invalidations, gate.device_seen, gate.immediate_seen, gate.deferred_seen, gate.audit_failed,
         gate.native_capture, gate.native_failed, s.deferred_uploads, s.secondary);
      reshade::log::message(reshade::log::level::info, line);
      timing.Log("upload-observer-copy-only", frame);
   }
   inline void Register()
   {
      NativeUploads::on_map = OnNativeMap;
      NativeUploads::before_unmap = BeforeNativeUnmap;
      NativeUploads::after_unmap = AfterNativeUnmap;
      reshade::register_event<reshade::addon_event::destroy_resource>(OnDestroyResource);
      reshade::register_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
      reshade::register_event<reshade::addon_event::init_device>(OnInitDevice);
      reshade::register_event<reshade::addon_event::init_command_list>(OnInitCommandList);
      reshade::register_event<reshade::addon_event::destroy_command_list>(OnDestroyCommandList);
      reshade::register_event<reshade::addon_event::execute_secondary_command_list>(OnSecondary);
      reshade::register_event<reshade::addon_event::copy_resource>(OnCopy);
      reshade::register_event<reshade::addon_event::copy_buffer_region>(OnCopyBuffer);
      reshade::register_event<reshade::addon_event::update_buffer_region>(OnUpdate);
      reshade::register_event<reshade::addon_event::update_buffer_region_command>(OnUpdateCommand);
   }
   inline void Unregister(bool process_exit = false)
   {
      if (!process_exit) NativeUploads::Shutdown();
      reshade::unregister_event<reshade::addon_event::destroy_resource>(OnDestroyResource);
      reshade::unregister_event<reshade::addon_event::destroy_device>(OnDestroyDevice);
      reshade::unregister_event<reshade::addon_event::init_device>(OnInitDevice);
      reshade::unregister_event<reshade::addon_event::init_command_list>(OnInitCommandList);
      reshade::unregister_event<reshade::addon_event::destroy_command_list>(OnDestroyCommandList);
      reshade::unregister_event<reshade::addon_event::execute_secondary_command_list>(OnSecondary);
      reshade::unregister_event<reshade::addon_event::copy_resource>(OnCopy);
      reshade::unregister_event<reshade::addon_event::copy_buffer_region>(OnCopyBuffer);
      reshade::unregister_event<reshade::addon_event::update_buffer_region>(OnUpdate);
      reshade::unregister_event<reshade::addon_event::update_buffer_region_command>(OnUpdateCommand);
   }
}
