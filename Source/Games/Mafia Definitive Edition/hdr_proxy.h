#pragma once

#include "taa_inputs.h"
#include <d3d11_1.h>

namespace MafiaDE
{
   // One colour contract per resolve run after a non-R11 pair is seen. The engine can
   // alternate even R11/R11 and FP16/FP16, so the current pair alone is insufficient.
   struct HdrProxyPath
   {
      bool mixed_formats = false;

      constexpr bool Select(bool requested, DXGI_FORMAT source, DXGI_FORMAT target)
      {
         if (requested && (source != DXGI_FORMAT_R11G11B10_FLOAT || target != DXGI_FORMAT_R11G11B10_FLOAT))
            mixed_formats = true;
         return requested && !mixed_formats;
      }

      constexpr void Reset()
      {
         mixed_formats = false;
      }
   };

   // Fixed, nonnegative finite HDR experiment. The model sees explicit UNORM surfaces;
   // decoding uses only its output, never the original image or a game render target.
   struct HdrProxy
   {
      com_ptr<ID3D11Texture2D> input, output, decoded;
      com_ptr<ID3D11ShaderResourceView> input_srv, output_srv;
      com_ptr<ID3D11UnorderedAccessView> input_uav, output_uav, decoded_uav;
      com_ptr<ID3D11Buffer> maximum_colour;
      UINT width = 0, height = 0;
      DXGI_FORMAT output_format = DXGI_FORMAT_UNKNOWN;

      HRESULT Prepare(ID3D11Device* device, ID3D11DeviceContext* context,
         ID3D11ShaderResourceView* source, UINT w, UINT h,
         DXGI_FORMAT destination_format, ID3D11ComputeShader* encode)
      {
         com_ptr<ID3D11Resource> colour;
         if (!device || !context || !encode || (destination_format != DXGI_FORMAT_R11G11B10_FLOAT && destination_format != DXGI_FORMAT_R16G16B16A16_FLOAT) || !ReadResolveInput(source, w, h, false, colour)) return E_INVALIDARG;

         if (width != w || height != h || output_format != destination_format)
         {
            // Allocation failure must leave the previous complete set usable.
            HdrProxy next;
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = w;
            desc.Height = h;
            desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
            desc.Format = DXGI_FORMAT_R16G16B16A16_UNORM;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            HRESULT hr = device->CreateTexture2D(&desc, nullptr, &next.input);
            if (FAILED(hr)) return hr;
            hr = device->CreateShaderResourceView(next.input.get(), nullptr, &next.input_srv);
            if (FAILED(hr)) return hr;
            hr = device->CreateUnorderedAccessView(next.input.get(), nullptr, &next.input_uav);
            if (FAILED(hr)) return hr;
            hr = device->CreateTexture2D(&desc, nullptr, &next.output);
            if (FAILED(hr)) return hr;
            hr = device->CreateShaderResourceView(next.output.get(), nullptr, &next.output_srv);
            if (FAILED(hr)) return hr;
            hr = device->CreateUnorderedAccessView(next.output.get(), nullptr, &next.output_uav);
            if (FAILED(hr)) return hr;
            desc.Format = destination_format;
            hr = device->CreateTexture2D(&desc, nullptr, &next.decoded);
            if (FAILED(hr)) return hr;
            hr = device->CreateUnorderedAccessView(next.decoded.get(), nullptr, &next.decoded_uav);
            if (FAILED(hr)) return hr;

            // A quantized/reconstructed proxy can decode past the largest finite texel.
            // Clamp in the production shader before the typed UAV conversion can overflow.
            const bool r11 = destination_format == DXGI_FORMAT_R11G11B10_FLOAT;
            // w selects explicit R11 rounding; FP16 keeps its original decode path.
            const float maximum[] = {r11 ? 65024.f : 65504.f, r11 ? 65024.f : 65504.f,
               r11 ? 64512.f : 65504.f, r11 ? 1.f : 0.f};
            D3D11_BUFFER_DESC buffer_desc = {};
            buffer_desc.ByteWidth = sizeof(maximum);
            buffer_desc.Usage = D3D11_USAGE_IMMUTABLE;
            buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            const D3D11_SUBRESOURCE_DATA data = {maximum, 0, 0};
            hr = device->CreateBuffer(&buffer_desc, &data, &next.maximum_colour);
            if (FAILED(hr)) return hr;
            next.width = w;
            next.height = h;
            next.output_format = destination_format;
            *this = std::move(next);
         }

         Dispatch(context, encode, source, input_uav.get(), nullptr);
         return S_OK;
      }

      HRESULT Decode(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11ComputeShader* decode)
      {
         if (!device || !context || !decode || !output_srv || !decoded_uav || !maximum_colour)
            return E_INVALIDARG;
         Dispatch(context, decode, output_srv.get(), decoded_uav.get(), maximum_colour.get());
         return S_OK;
      }

   private:
      void Dispatch(ID3D11DeviceContext* context, ID3D11ComputeShader* shader,
         ID3D11ShaderResourceView* source, ID3D11UnorderedAccessView* target, ID3D11Buffer* buffer)
      {
         com_ptr<ID3D11ComputeShader> previous_shader;
         com_ptr<ID3D11ShaderResourceView> previous_srv;
         com_ptr<ID3D11UnorderedAccessView> previous_uav;
         ID3D11ClassInstance* instances[256] = {};
         UINT instance_count = 256;
         context->CSGetShader(&previous_shader, instances, &instance_count);
         context->CSGetShaderResources(0, 1, &previous_srv);
         context->CSGetUnorderedAccessViews(0, 1, &previous_uav);

         com_ptr<ID3D11DeviceContext1> context1;
         com_ptr<ID3D11Buffer> previous_buffer;
         UINT first_constant = 0, num_constants = 0;
         if (buffer)
         {
            if (SUCCEEDED(context->QueryInterface(&context1)))
               context1->CSGetConstantBuffers1(0, 1, &previous_buffer, &first_constant, &num_constants);
            else
               context->CSGetConstantBuffers(0, 1, &previous_buffer);
            context->CSSetConstantBuffers(0, 1, &buffer);
         }

         // Bind the private output first so a saved u0 cannot suppress the new t0.
         // Null initial counts retain the counters of both displaced and restored UAVs.
         context->CSSetUnorderedAccessViews(0, 1, &target, nullptr);
         context->CSSetShaderResources(0, 1, &source);
         context->CSSetShader(shader, nullptr, 0);
         context->Dispatch((width + 15) / 16, (height + 15) / 16, 1);

         // Remove our input before restoring outputs, then restore inputs. Private outputs
         // never displace graphics bindings, and must be unbound before NGX or a copy uses them.
         ID3D11ShaderResourceView* srv = nullptr;
         context->CSSetShaderResources(0, 1, &srv);
         ID3D11UnorderedAccessView* uav = previous_uav.get();
         context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
         srv = previous_srv.get();
         context->CSSetShaderResources(0, 1, &srv);
         context->CSSetShader(previous_shader.get(), instances, instance_count);
         for (UINT i = 0; i < instance_count; ++i)
            instances[i]->Release();
         if (buffer)
         {
            ID3D11Buffer* saved = previous_buffer.get();
            if (context1)
               context1->CSSetConstantBuffers1(0, 1, &saved, &first_constant, &num_constants);
            else
               context->CSSetConstantBuffers(0, 1, &saved);
         }
      }
   };
} // namespace MafiaDE
