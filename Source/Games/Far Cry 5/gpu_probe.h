#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>

namespace FC5
{
   // Bounded asynchronous GPU timing. Never flushes or waits for query results.
   // Instance belongs to one device/immediate context. Disabled unless requested.
   struct GPUProbe
   {
      struct Slot
      {
         Microsoft::WRL::ComPtr<ID3D11Query> disjoint;
         std::array<Microsoft::WRL::ComPtr<ID3D11Query>, 4> timestamps;
         bool pending = false;
         unsigned mask = 0;
      };
      std::array<Slot, 8> slots;
      uint64_t samples = 0, dropped = 0, invalid = 0;
      std::array<double, 3> milliseconds{};
      bool failed = false;

      void Poll(ID3D11DeviceContext* ctx)
      {
         for (auto& slot : slots)
         {
            if (!slot.pending) continue;
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT data{};
            const HRESULT hr = ctx->GetData(slot.disjoint.Get(), &data, sizeof(data), D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if (hr == S_FALSE) continue;
            if (FAILED(hr) || data.Disjoint || !data.Frequency || slot.mask != 15)
            { slot.pending = false; ++invalid; continue; }
            std::array<UINT64, 4> ticks{};
            bool ready = true, error = false;
            for (unsigned i = 0; i < 4; ++i)
            {
               const HRESULT result = ctx->GetData(slot.timestamps[i].Get(), &ticks[i], sizeof(ticks[i]), D3D11_ASYNC_GETDATA_DONOTFLUSH);
               ready &= result == S_OK; error |= FAILED(result);
            }
            if (error) { slot.pending = false; ++invalid; continue; }
            if (!ready) continue;
            slot.pending = false;
            if (ticks[1] < ticks[0] || ticks[2] < ticks[1] || ticks[3] < ticks[2]) { ++invalid; continue; }
            for (unsigned i = 0; i < 3; ++i)
               milliseconds[i] += double(ticks[i + 1] - ticks[i]) * 1000.0 / double(data.Frequency);
            ++samples;
         }
      }
      Slot* Begin(ID3D11Device* device, ID3D11DeviceContext* ctx)
      {
         if (failed || ctx->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE) return nullptr;
         for (auto& slot : slots)
         {
            if (slot.pending) continue;
            if (!slot.disjoint)
            {
               D3D11_QUERY_DESC desc{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
               if (FAILED(device->CreateQuery(&desc, &slot.disjoint))) { failed = true; return nullptr; }
               desc.Query = D3D11_QUERY_TIMESTAMP;
               for (auto& stamp : slot.timestamps)
                  if (FAILED(device->CreateQuery(&desc, &stamp))) { failed = true; return nullptr; }
            }
            slot.pending = true; slot.mask = 1;
            ctx->Begin(slot.disjoint.Get());
            ctx->End(slot.timestamps[0].Get());
            return &slot;
         }
         ++dropped;
         return nullptr;
      }
      struct Scope
      {
         ID3D11DeviceContext* ctx;
         Slot* slot;
         Scope(GPUProbe& owner, ID3D11Device* device, ID3D11DeviceContext* context, bool enabled)
            : ctx(context), slot(enabled ? owner.Begin(device, context) : nullptr) {}
         Scope(const Scope&) = delete;
         Scope& operator=(const Scope&) = delete;
         void Mark(unsigned index)
         {
            if (!slot || index < 1 || index > 2 || (slot->mask & (1u << index))) return;
            ctx->End(slot->timestamps[index].Get()); slot->mask |= 1u << index;
         }
         ~Scope()
         {
            if (!slot) return;
            ctx->End(slot->timestamps[3].Get()); slot->mask |= 8;
            ctx->End(slot->disjoint.Get());
         }
      };
   };
}
