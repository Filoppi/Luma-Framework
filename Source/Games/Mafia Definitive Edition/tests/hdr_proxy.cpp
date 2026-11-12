// Run at the repository root. MSVC v143:
// cl /nologo /std:c++20 /EHsc /O2 /W4 /WX /Fe:tmp\build-hdr-proxy-test.exe /Fo:tmp\build-hdr-proxy-test.obj "Source\Games\Mafia Definitive Edition\tests\hdr_proxy.cpp" /link d3d11.lib d3dcompiler.lib dxgi.lib
// tmp\build-hdr-proxy-test.exe [--hardware [--static-sentinel]]
#include "../hdr_proxy.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <d3dcompiler.h>
#include <dxgi1_2.h>

static_assert([]
   {
   constexpr auto r11 = DXGI_FORMAT_R11G11B10_FLOAT;
   constexpr auto fp16 = DXGI_FORMAT_R16G16B16A16_FLOAT;
   MafiaDE::HdrProxyPath path;
   if (path.Select(false, fp16, fp16) || !path.Select(true, r11, r11)) return false;
   path.Reset();
   if (path.Select(true, r11, fp16) || path.Select(true, fp16, r11)) return false;
   path.Reset();
   if (!path.Select(true, r11, r11)) return false;
   for (int i = 0; i < 8; ++i)
      if (path.Select(true, fp16, fp16) || path.Select(true, r11, r11)) return false;
   path.Reset();
   return path.Select(true, r11, r11); }(), "A mixed resolve run must stay in HDR until the next resolve gap");

static void Check(bool passed, const char* name)
{
   if (!passed)
   {
      std::fprintf(stderr, "FAIL: %s\n", name);
      std::exit(1);
   }
}

static void CheckHR(HRESULT hr, const char* name)
{
   if (FAILED(hr)) std::fprintf(stderr, "HRESULT=0x%08lX: %s\n", static_cast<unsigned long>(hr), name);
   Check(SUCCEEDED(hr), name);
}

static com_ptr<ID3D11ComputeShader> Compile(ID3D11Device* device, const wchar_t* path)
{
   com_ptr<ID3DBlob> bytecode, errors;
   const HRESULT hr = D3DCompileFromFile(path, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "cs_5_0",
      D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
      0, &bytecode, &errors);
   if (errors) std::fwrite(errors->GetBufferPointer(), 1, errors->GetBufferSize(), stderr);
   CheckHR(hr, "compile production HDR proxy shader");
   com_ptr<ID3D11ComputeShader> shader;
   CheckHR(device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &shader),
      "create production HDR proxy shader");
   return shader;
}

static std::vector<uint8_t> ReadTexture(ID3D11Device* device, ID3D11DeviceContext* context,
   ID3D11Texture2D* source, UINT bytes_per_pixel)
{
   D3D11_TEXTURE2D_DESC desc = {};
   source->GetDesc(&desc);
   desc.Usage = D3D11_USAGE_STAGING;
   desc.BindFlags = desc.MiscFlags = 0;
   desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
   com_ptr<ID3D11Texture2D> staging;
   CheckHR(device->CreateTexture2D(&desc, nullptr, &staging), "create texture readback");
   context->CopyResource(staging.get(), source);
   D3D11_MAPPED_SUBRESOURCE mapped = {};
   CheckHR(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped), "map texture readback");
   std::vector<uint8_t> bytes(size_t(desc.Width) * desc.Height * bytes_per_pixel);
   for (UINT y = 0; y < desc.Height; ++y)
      std::memcpy(bytes.data() + size_t(y) * desc.Width * bytes_per_pixel,
         static_cast<const uint8_t*>(mapped.pData) + size_t(y) * mapped.RowPitch,
         size_t(desc.Width) * bytes_per_pixel);
   context->Unmap(staging.get(), 0);
   return bytes;
}

static UINT ReadCounter(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11UnorderedAccessView* view)
{
   D3D11_BUFFER_DESC desc = {};
   desc.ByteWidth = sizeof(UINT);
   desc.Usage = D3D11_USAGE_DEFAULT;
   com_ptr<ID3D11Buffer> counter, staging;
   CheckHR(device->CreateBuffer(&desc, nullptr, &counter), "create counter copy");
   desc.Usage = D3D11_USAGE_STAGING;
   desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
   CheckHR(device->CreateBuffer(&desc, nullptr, &staging), "create counter readback");
   context->CopyStructureCount(counter.get(), 0, view);
   context->CopyResource(staging.get(), counter.get());
   D3D11_MAPPED_SUBRESOURCE mapped = {};
   CheckHR(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped), "map counter");
   const UINT value = *static_cast<const UINT*>(mapped.pData);
   context->Unmap(staging.get(), 0);
   return value;
}

struct Sentinels
{
   ID3D11Device* device;
   ID3D11DeviceContext* context;
   com_ptr<ID3D11DeviceContext1> context1;
   com_ptr<ID3D11ClassLinkage> linkage;
   com_ptr<ID3D11ClassInstance> instance;
   com_ptr<ID3D11ComputeShader> shader;
   com_ptr<ID3D11ShaderResourceView> srv;
   com_ptr<ID3D11RenderTargetView> rtv;
   com_ptr<ID3D11UnorderedAccessView> uav[2];
   com_ptr<ID3D11Buffer> buffer;
   ID3D11Buffer* expected_buffer = nullptr;
   ID3D11ShaderResourceView* graphics_source = nullptr;
   UINT first = 0, count = 0;
   bool range_supported = false;

   Sentinels(ID3D11Device* d, ID3D11DeviceContext* c, bool linked)
       : device(d), context(c)
   {
      // A real dynamically linked CS checks restoration of the instance array as well as
      // the shader pointer. Its append buffers give displaced u0 and untouched u1 counters.
      const char linked_code[] =
         "interface IValue { float Get(); };"
         "class SentinelValue : IValue { float value; float Get() { return value; } };"
         "SentinelValue definedValue; IValue selected;"
         "RWStructuredBuffer<float> Sink : register(u0);"
         "[numthreads(1,1,1)] void main() { Sink[0] = selected.Get() + definedValue.value; }";
      // Explicit diagnostic option for drivers which crash while compiling the linked
      // sentinel. Numeric/other-state checks still run; class restoration is not claimed.
      const char static_code[] = "RWStructuredBuffer<float> Sink : register(u0);"
                                 "[numthreads(1,1,1)] void main() { Sink[0] = 1.0; }";
      const char* code = linked ? linked_code : static_code;
      com_ptr<ID3DBlob> bytecode, errors;
      const HRESULT hr = D3DCompile(code, std::strlen(code), "state-sentinel", nullptr, nullptr, "main", "cs_5_0",
         D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
         0, &bytecode, &errors);
      if (errors) std::fwrite(errors->GetBufferPointer(), 1, errors->GetBufferSize(), stderr);
      CheckHR(hr, "compile state sentinel");
      if (linked) CheckHR(device->CreateClassLinkage(&linkage), "create sentinel linkage");
      CheckHR(device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), linkage.get(), &shader),
         "create sentinel CS");
      if (linked)
         CheckHR(linkage->GetClassInstance("definedValue", 0, &instance), "get predefined sentinel instance");

      D3D11_TEXTURE2D_DESC desc = {};
      desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
      desc.Format = DXGI_FORMAT_R32_FLOAT;
      desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
      com_ptr<ID3D11Texture2D> input, target;
      CheckHR(device->CreateTexture2D(&desc, nullptr, &input), "create sentinel input");
      CheckHR(device->CreateShaderResourceView(input.get(), nullptr, &srv), "create sentinel SRV");
      CheckHR(device->CreateTexture2D(&desc, nullptr, &target), "create sentinel graphics target");
      CheckHR(device->CreateRenderTargetView(target.get(), nullptr, &rtv), "create sentinel RTV");
      for (UINT i = 0; i < 2; ++i)
      {
         D3D11_BUFFER_DESC bd = {};
         bd.ByteWidth = 64 * sizeof(UINT);
         bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
         bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
         bd.StructureByteStride = sizeof(UINT);
         com_ptr<ID3D11Buffer> append;
         CheckHR(device->CreateBuffer(&bd, nullptr, &append), "create sentinel append buffer");
         D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
         ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
         ud.Buffer.NumElements = 64;
         ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND;
         CheckHR(device->CreateUnorderedAccessView(append.get(), &ud, &uav[i]), "create sentinel append UAV");
      }
      D3D11_BUFFER_DESC bd = {};
      bd.ByteWidth = 1024;
      bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
      CheckHR(device->CreateBuffer(&bd, nullptr, &buffer), "create sentinel constant buffer");
      if (SUCCEEDED(context->QueryInterface(&context1)))
      {
         D3D11_FEATURE_DATA_D3D11_OPTIONS options = {};
         CheckHR(device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options)), "query CB range support");
         range_supported = options.ConstantBufferOffsetting != FALSE;
      }
      ID3D11ClassInstance* classes[] = {instance.get()};
      context->CSSetShader(shader.get(), linked ? classes : nullptr, linked ? 1 : 0);
      ID3D11ShaderResourceView* views[] = {srv.get(), srv.get()};
      context->CSSetShaderResources(0, 2, views);
      context->PSSetShaderResources(0, 2, views);
      graphics_source = srv.get();
      ID3D11RenderTargetView* target_view = rtv.get();
      context->OMSetRenderTargets(1, &target_view, nullptr);
      ID3D11UnorderedAccessView* outputs[] = {uav[0].get(), uav[1].get()};
      const UINT counters[] = {7, 11};
      context->CSSetUnorderedAccessViews(0, 2, outputs, counters);
      ID3D11Buffer* buffers[] = {buffer.get(), buffer.get()};
      context->CSSetConstantBuffers(0, 2, buffers);
      BindBuffer(range_supported ? 2 : 1);
      std::printf("state: Context1=%s CB-range=%s class-instances=%u UAV-counters=7,11\n",
         context1 ? "yes" : "no", range_supported ? "yes (first=16 count=16)" : "unavailable", linked ? 1u : 0u);
   }

   void BindBuffer(int mode)
   {
      expected_buffer = mode == 0 ? nullptr : buffer.get();
      if (mode == 2)
      {
         Check(range_supported, "CB range test supported");
         first = count = 16;
         context1->CSSetConstantBuffers1(0, 1, &expected_buffer, &first, &count);
      }
      else
         context->CSSetConstantBuffers(0, 1, &expected_buffer);
      if (context1)
      {
         com_ptr<ID3D11Buffer> saved;
         context1->CSGetConstantBuffers1(0, 1, &saved, &first, &count);
         Check(saved.get() == expected_buffer, "sentinel b0 binding");
         if (mode == 2) Check(first == 16 && count == 16, "nonzero sentinel CB range established");
      }
   }

   void GraphicsSource(ID3D11ShaderResourceView* source)
   {
      graphics_source = source;
      context->PSSetShaderResources(0, 1, &source);
   }

   void Verify()
   {
      com_ptr<ID3D11ComputeShader> cs;
      ID3D11ClassInstance* classes[256] = {};
      UINT num = 256;
      context->CSGetShader(&cs, classes, &num);
      Check(cs == shader && num == (instance ? 1u : 0u) && (!instance || classes[0] == instance.get()),
         "restore CS shader and class instances");
      for (UINT i = 0; i < num; ++i)
         classes[i]->Release();
      for (UINT i = 0; i < 2; ++i)
      {
         com_ptr<ID3D11ShaderResourceView> input, ps;
         com_ptr<ID3D11UnorderedAccessView> output;
         com_ptr<ID3D11Buffer> cb;
         context->CSGetShaderResources(i, 1, &input);
         context->CSGetUnorderedAccessViews(i, 1, &output);
         context->CSGetConstantBuffers(i, 1, &cb);
         context->PSGetShaderResources(i, 1, &ps);
         Check(input == srv && output == uav[i], "preserve CS t0/t1/u0/u1");
         Check(cb.get() == (i == 0 ? expected_buffer : buffer.get()), "preserve CS b0/b1");
         Check(ps.get() == (i == 0 ? graphics_source : srv.get()), "preserve PS t0/t1");
         Check(ReadCounter(device, context, uav[i].get()) == (i == 0 ? 7u : 11u), "preserve append counters");
      }
      if (context1)
      {
         UINT actual_first = 0, actual_count = 0;
         com_ptr<ID3D11Buffer> cb;
         context1->CSGetConstantBuffers1(0, 1, &cb, &actual_first, &actual_count);
         Check(cb.get() == expected_buffer && actual_first == first && actual_count == count, "restore exact b0 range");
      }
      com_ptr<ID3D11RenderTargetView> target;
      context->OMGetRenderTargets(1, &target, nullptr);
      Check(target == rtv, "preserve game RTV");
   }
};

struct Source
{
   com_ptr<ID3D11Texture2D> texture;
   com_ptr<ID3D11ShaderResourceView> view;
   std::vector<uint8_t> bytes;
   UINT width, height, values, bytes_per_pixel;
   DXGI_FORMAT format;
};

static Source MakeSource(ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format)
{
   const bool r11 = format == DXGI_FORMAT_R11G11B10_FLOAT;
   Source source = {{}, {}, {}, width, height, r11 ? 1984u : 31744u, r11 ? 4u : 8u, format};
   source.bytes.resize(size_t(width) * height * source.bytes_per_pixel);
   for (UINT i = 0; i < width * height; ++i)
   {
      if (r11)
      {
         UINT packed = (i % 1984) | (((i + 733) % 1984) << 11) | (((i + 337) % 992) << 22);
         if (i == width * height - 2) packed = 0;
         if (i == width * height - 1) packed = 1983 | (1983 << 11) | (991u << 22);
         std::memcpy(source.bytes.data() + size_t(i) * 4, &packed, 4);
      }
      else
      {
         uint16_t packed[] = {uint16_t(i % 31744), uint16_t((i + 10633) % 31744),
            uint16_t((i + 21599) % 31744), uint16_t((i * 17) % 31744)};
         if (i == width * height - 2) packed[0] = packed[1] = packed[2] = 0;
         if (i == width * height - 1) packed[0] = packed[1] = packed[2] = 0x7bff;
         std::memcpy(source.bytes.data() + size_t(i) * 8, packed, 8);
      }
   }
   D3D11_TEXTURE2D_DESC desc = {};
   desc.Width = width;
   desc.Height = height;
   desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
   desc.Format = format;
   desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
   const D3D11_SUBRESOURCE_DATA data = {source.bytes.data(), width * source.bytes_per_pixel, 0};
   CheckHR(device->CreateTexture2D(&desc, &data, &source.texture), "create enumerated source");
   CheckHR(device->CreateShaderResourceView(source.texture.get(), nullptr, &source.view), "create enumerated source SRV");
   return source;
}

static UINT Code(const std::vector<uint8_t>& data, UINT pixel, UINT channel, bool r11)
{
   if (r11)
   {
      uint32_t packed = 0;
      std::memcpy(&packed, data.data() + size_t(pixel) * 4, 4);
      return (packed >> (channel == 0 ? 0 : channel == 1 ? 11
                                                         : 22)) &
             (channel == 2 ? 1023 : 2047);
   }
   uint16_t packed = 0;
   std::memcpy(&packed, data.data() + size_t(pixel) * 8 + channel * 2, 2);
   return packed;
}

static double Value(UINT code, UINT mantissa)
{
   const UINT exponent = code >> mantissa;
   const UINT fraction = code & ((1u << mantissa) - 1);
   return exponent == 0 ? std::ldexp(double(fraction), -14 - int(mantissa))
                        : std::ldexp(1.0 + double(fraction) / double(1u << mantissa), int(exponent) - 15);
}

// Independent double-precision nearest-even typed-store reference for diagnosis, not a tolerance.
static UINT ReferenceCode(double value, UINT mantissa)
{
   UINT low = 0, high = (31u << mantissa) - 1;
   while (low < high)
   {
      const UINT mid = (low + high) / 2;
      if (Value(mid, mantissa) < value)
         low = mid + 1;
      else
         high = mid;
   }
   if (low == 0) return 0;
   const double a = value - Value(low - 1, mantissa), b = Value(low, mantissa) - value;
   return a < b || (a == b && (low & 1)) ? low - 1 : low;
}

static bool SameResources(const MafiaDE::HdrProxy& a, const MafiaDE::HdrProxy& b)
{
   return a.input == b.input && a.output == b.output && a.decoded == b.decoded && a.input_srv == b.input_srv && a.output_srv == b.output_srv && a.input_uav == b.input_uav && a.output_uav == b.output_uav && a.decoded_uav == b.decoded_uav && a.maximum_colour == b.maximum_colour && a.width == b.width && a.height == b.height && a.output_format == b.output_format;
}

static void CheckDescriptors(const MafiaDE::HdrProxy& proxy, const Source& source)
{
   ID3D11Texture2D* textures[] = {proxy.input.get(), proxy.output.get(), proxy.decoded.get()};
   for (UINT i = 0; i < 3; ++i)
   {
      D3D11_TEXTURE2D_DESC desc = {};
      textures[i]->GetDesc(&desc);
      Check(desc.Width == source.width && desc.Height == source.height && desc.MipLevels == 1 && desc.ArraySize == 1 && desc.SampleDesc.Count == 1 && desc.Usage == D3D11_USAGE_DEFAULT && desc.CPUAccessFlags == 0 && desc.Format == (i == 2 ? proxy.output_format : DXGI_FORMAT_R16G16B16A16_UNORM) && (desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0 && (desc.BindFlags & D3D11_BIND_RENDER_TARGET) == 0, "private typed proxy texture descriptor");
      Check(textures[i] != source.texture.get(), "never write source texture");
      for (UINT j = 0; j < i; ++j)
         Check(textures[i] != textures[j], "separate input/output/decoded textures");
   }
   ID3D11ShaderResourceView* views[] = {proxy.input_srv.get(), proxy.output_srv.get()};
   ID3D11UnorderedAccessView* outputs[] = {proxy.input_uav.get(), proxy.output_uav.get()};
   for (UINT i = 0; i < 2; ++i)
   {
      D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
      views[i]->GetDesc(&sd);
      D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
      outputs[i]->GetDesc(&ud);
      Check(sd.Format == DXGI_FORMAT_R16G16B16A16_UNORM && sd.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D && ud.Format == DXGI_FORMAT_R16G16B16A16_UNORM && ud.ViewDimension == D3D11_UAV_DIMENSION_TEXTURE2D,
         "explicit UNORM SRV/UAV formats");
   }
}

static void Identity(ID3D11Device* device, ID3D11DeviceContext* context, MafiaDE::HdrProxy& proxy,
   const Source& source, ID3D11ComputeShader* encode, ID3D11ComputeShader* decode, Sentinels& state,
   DXGI_FORMAT destination)
{
   state.GraphicsSource(source.view.get());
   CheckHR(proxy.Prepare(device, context, source.view.get(), source.width, source.height, destination, encode), "prepare HDR proxy");
   state.Verify();
   CheckDescriptors(proxy, source);
   // Identity-model simulation: no source HDR information reaches Decode.
   context->CopyResource(proxy.output.get(), proxy.input.get());
   CheckHR(proxy.Decode(device, context, decode), "decode identity-model output");
   state.Verify();
   Check(ReadTexture(device, context, source.texture.get(), source.bytes_per_pixel) == source.bytes,
      "source bytes unchanged, including alpha and edge texels");
}

static UINT Roundtrip(ID3D11Device* device, ID3D11DeviceContext* context, MafiaDE::HdrProxy& proxy,
   const Source& source, ID3D11ComputeShader* encode, ID3D11ComputeShader* decode, Sentinels& state)
{
   Identity(device, context, proxy, source, encode, decode, state, source.format);
   const auto encoded = ReadTexture(device, context, proxy.input.get(), 8);
   Check(encoded == ReadTexture(device, context, proxy.output.get(), 8), "identity model copied every UNORM byte");
   const auto decoded = ReadTexture(device, context, proxy.decoded.get(), source.bytes_per_pixel);
   const bool r11 = source.format == DXGI_FORMAT_R11G11B10_FLOAT;
   std::printf("roundtrip: %s %ux%u; unique source values per channel=%s; nonnegative RGB only\n",
      r11 ? "R11" : "FP16", source.width, source.height, r11 ? "1984/1984/992" : "31744/31744/31744");
   UINT pixel_mismatches = 0, detailed = 0;
   for (UINT i = 0; i < source.width * source.height; ++i)
   {
      if (std::memcmp(source.bytes.data() + size_t(i) * source.bytes_per_pixel,
             decoded.data() + size_t(i) * source.bytes_per_pixel, r11 ? 4u : 6u) != 0) ++pixel_mismatches;
      for (UINT c = 0; c < 3; ++c)
      {
         const UINT mantissa = r11 ? (c == 2 ? 5 : 6) : 10;
         Check(Code(decoded, i, c, r11) < (31u << mantissa), "decoded RGB finite and nonnegative");
      }
      if (!r11) Check(Code(decoded, i, 3, false) == 0x3c00, "decoded alpha is one");
      Check(Code(encoded, i, 3, false) == 65535, "encoded alpha is one");
   }
   for (UINT c = 0; c < 3; ++c)
   {
      const UINT mantissa = r11 ? (c == 2 ? 5 : 6) : 10;
      UINT mismatches = 0, max_ulp = 0, distinct = 0, reference_mismatches = 0, gpu_reference_differences = 0;
      UINT downward = 0, upward = 0, gpu_truncated_differences = 0;
      double max_absolute = 0, max_relative = 0, square_error = 0;
      std::vector<bool> seen(65536);
      const UINT samples = r11 && c == 2 ? 992u : source.values;
      for (UINT i = 0; i < samples; ++i)
      {
         const UINT before = Code(source.bytes, i, c, r11), after = Code(decoded, i, c, r11);
         const UINT q = Code(encoded, i, c, false);
         const double expected = Value(before, mantissa), actual = Value(after, mantissa);
         const double error = std::abs(actual - expected);
         const UINT ulp = before > after ? before - after : after - before;
         if (ulp != 0) ++mismatches;
         if (after < before) ++downward;
         if (after > before) ++upward;
         max_ulp = (std::max)(max_ulp, ulp);
         max_absolute = (std::max)(max_absolute, error);
         if (expected != 0) max_relative = (std::max)(max_relative, error / expected);
         square_error += error * error;
         if (!seen[q])
         {
            seen[q] = true;
            ++distinct;
         }
         const double inverse = (std::min)(std::expm1(std::pow(double(q) / 65535., 2.2) * 13.707835182644992) / 13.707835182644992,
            Value((31u << mantissa) - 1, mantissa));
         const UINT reference = ReferenceCode(inverse, mantissa);
         const UINT truncated = Value(reference, mantissa) > inverse ? reference - 1 : reference;
         if (reference != before) ++reference_mismatches;
         if (reference != after) ++gpu_reference_differences;
         if (truncated != after) ++gpu_truncated_differences;
         if (r11 && ulp != 0 && detailed < 12)
         {
            const double ideal = std::pow(std::log1p(13.707835182644992 * expected) / 13.707835182644992, 1. / 2.2) * 65535.;
            std::printf("R11 difference: channel=%u pixel=%u before=0x%X after=0x%X value=%.12g decoded=%.12g proxy=%u ideal_proxy=%.12g inverse_double=%.12g reference_code=0x%X\n",
               c, i, before, after, expected, actual, q, ideal, inverse, reference);
            ++detailed;
         }
      }
      std::printf("metrics: %s channel=%u samples=%u mismatches=%u down=%u up=%u maxULP=%u maxAbs=%.12g maxRelative=%.12g rms=%.12g distinctProxyCodes=%u collisions=%u nearestReferenceMismatch=%u gpuVsNearest=%u gpuVsTruncated=%u\n",
         r11 ? "R11" : "FP16", c, samples, mismatches, downward, upward, max_ulp, max_absolute,
         max_relative, std::sqrt(square_error / samples), distinct, samples - distinct,
         reference_mismatches, gpu_reference_differences, gpu_truncated_differences);
   }
   const UINT black = source.width * source.height - 2, top = black + 1;
   for (UINT c = 0; c < 3; ++c)
   {
      Check(Code(decoded, black, c, r11) == 0, "black restored exactly");
      Check(Code(decoded, top, c, r11) == Code(source.bytes, top, c, r11), "finite top restored exactly");
   }
   std::printf("RESULT: %s bit-exact-RGB=%s pixel-mismatches=%u/%u; black/top=exact; source=unchanged; decoded=finite\n",
      r11 ? "R11" : "FP16", pixel_mismatches == 0 ? "yes" : "no", pixel_mismatches, source.width * source.height);
   return pixel_mismatches;
}

static void CheckUpperClamp(ID3D11Device* device, ID3D11DeviceContext* context,
   MafiaDE::HdrProxy& proxy, ID3D11ComputeShader* decode, Sentinels& state)
{
   const float ones[] = {1, 1, 1, 1};
   context->ClearUnorderedAccessViewFloat(proxy.output_uav.get(), ones);
   CheckHR(proxy.Decode(device, context, decode), "decode maximum reconstructed proxy");
   state.Verify();
   const bool r11 = proxy.output_format == DXGI_FORMAT_R11G11B10_FLOAT;
   const auto decoded = ReadTexture(device, context, proxy.decoded.get(), r11 ? 4 : 8);
   for (UINT i = 0; i < proxy.width * proxy.height; ++i)
      for (UINT c = 0; c < 3; ++c)
      {
         const UINT mantissa = r11 ? (c == 2 ? 5 : 6) : 10;
         Check(Code(decoded, i, c, r11) == (31u << mantissa) - 1, "proxy=one clamps to exact finite format maximum");
      }
   std::printf("PASS: %s proxy=one finite-maximum clamp\n", r11 ? "R11" : "FP16");
}

int main(int argc, char** argv)
{
   std::setvbuf(stdout, nullptr, _IONBF, 0);
   const bool hardware = argc >= 2 && std::strcmp(argv[1], "--hardware") == 0;
   const bool static_sentinel = argc == 3 && std::strcmp(argv[2], "--static-sentinel") == 0;
   Check(argc == 1 || (hardware && (argc == 2 || static_sentinel)),
      "usage: build-hdr-proxy-test [--hardware [--static-sentinel]] (run at repo root)");
   if (static_sentinel)
      std::printf("LIMIT: explicit static-sentinel mode; nonempty class-instance restoration is NOT tested on this device.\n");
   com_ptr<IDXGIFactory1> factory;
   com_ptr<IDXGIAdapter1> adapter;
   if (hardware)
   {
      CheckHR(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "create adapter factory");
      for (UINT i = 0;; ++i)
      {
         com_ptr<IDXGIAdapter1> candidate;
         const HRESULT hr = factory->EnumAdapters1(i, &candidate);
         if (hr == DXGI_ERROR_NOT_FOUND) break;
         CheckHR(hr, "enumerate adapter");
         DXGI_ADAPTER_DESC1 desc = {};
         CheckHR(candidate->GetDesc1(&desc), "get adapter description");
         if (desc.VendorId == 0x10de && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
         {
            adapter = std::move(candidate);
            break;
         }
      }
      Check(bool(adapter), "NVIDIA hardware adapter present");
   }
   com_ptr<ID3D11Device> device;
   com_ptr<ID3D11DeviceContext> context;
   D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
   CheckHR(D3D11CreateDevice(adapter.get(), hardware ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_WARP,
              nullptr, 0, &level, 1, D3D11_SDK_VERSION, &device, nullptr, &context),
      "create D3D11 device");
   com_ptr<IDXGIDevice> dxgi_device;
   com_ptr<IDXGIAdapter> actual_adapter;
   CheckHR(device->QueryInterface(&dxgi_device), "query device adapter");
   CheckHR(dxgi_device->GetAdapter(&actual_adapter), "get actual adapter");
   DXGI_ADAPTER_DESC desc = {};
   CheckHR(actual_adapter->GetDesc(&desc), "get actual adapter description");
   std::printf("device=%s adapter=%ls vendor=0x%04X deviceId=0x%04X\n",
      hardware ? "hardware" : "WARP", desc.Description, desc.VendorId, desc.DeviceId);
   if (hardware) Check(desc.VendorId == 0x10de, "test ran on NVIDIA");
   auto encode = Compile(device.get(), L"Shaders/Mafia Definitive Edition/Luma_MafiaDE_HDRProxyEncode.hlsl");
   auto decode = Compile(device.get(), L"Shaders/Mafia Definitive Edition/Luma_MafiaDE_HDRProxyDecode.hlsl");
   Sentinels state(device.get(), context.get(), !static_sentinel);
   state.Verify();
   MafiaDE::HdrProxy proxy;
   Check(proxy.Decode(device.get(), context.get(), decode.get()) == E_INVALIDARG, "reject decode before prepare");
   state.Verify();

   auto r11 = MakeSource(device.get(), 67, 31, DXGI_FORMAT_R11G11B10_FLOAT);
   const UINT r11_differences = Roundtrip(device.get(), context.get(), proxy, r11, encode.get(), decode.get(), state);
   const auto first_decoded = ReadTexture(device.get(), context.get(), proxy.decoded.get(), 4);
   CheckUpperClamp(device.get(), context.get(), proxy, decode.get(), state);
   MafiaDE::HdrProxy previous = proxy;
   Identity(device.get(), context.get(), proxy, r11, encode.get(), decode.get(), state, r11.format);
   Check(SameResources(proxy, previous), "reuse identical dimensions and destination format");
   Check(first_decoded == ReadTexture(device.get(), context.get(), proxy.decoded.get(), 4), "repeat output identical");

   Identity(device.get(), context.get(), proxy, r11, encode.get(), decode.get(), state, DXGI_FORMAT_R16G16B16A16_FLOAT);
   Check(proxy.input != previous.input && proxy.output != previous.output && proxy.decoded != previous.decoded && proxy.maximum_colour != previous.maximum_colour, "replace complete resource set on destination format change");
   previous = proxy;
   auto small_fp16 = MakeSource(device.get(), 67, 31, DXGI_FORMAT_R16G16B16A16_FLOAT);
   Identity(device.get(), context.get(), proxy, small_fp16, encode.get(), decode.get(), state, small_fp16.format);
   Check(SameResources(proxy, previous), "source format change reuses matching destination resources");

   auto fp16 = MakeSource(device.get(), 257, 124, DXGI_FORMAT_R16G16B16A16_FLOAT);
   Roundtrip(device.get(), context.get(), proxy, fp16, encode.get(), decode.get(), state);
   Check(proxy.input != previous.input && proxy.output != previous.output && proxy.decoded != previous.decoded,
      "replace resources on size change");
   CheckUpperClamp(device.get(), context.get(), proxy, decode.get(), state);
   for (int mode = 0; mode < (state.range_supported ? 3 : 2); ++mode)
   {
      state.BindBuffer(mode);
      previous = proxy;
      Identity(device.get(), context.get(), proxy, fp16, encode.get(), decode.get(), state, fp16.format);
      Check(SameResources(proxy, previous), "reuse with null/full/ranged b0");
   }

   previous = proxy;
   auto reject = [&](ID3D11Device* d, ID3D11DeviceContext* c, ID3D11ShaderResourceView* source,
                    UINT width, UINT height, DXGI_FORMAT format, ID3D11ComputeShader* shader)
   {
      Check(proxy.Prepare(d, c, source, width, height, format, shader) == E_INVALIDARG, "reject invalid prepare");
      Check(SameResources(proxy, previous), "rejection leaves cached resources intact");
      state.Verify();
   };
   reject(nullptr, context.get(), fp16.view.get(), fp16.width, fp16.height, fp16.format, encode.get());
   reject(device.get(), nullptr, fp16.view.get(), fp16.width, fp16.height, fp16.format, encode.get());
   reject(device.get(), context.get(), nullptr, fp16.width, fp16.height, fp16.format, encode.get());
   reject(device.get(), context.get(), fp16.view.get(), 0, fp16.height, fp16.format, encode.get());
   reject(device.get(), context.get(), fp16.view.get(), fp16.width - 1, fp16.height, fp16.format, encode.get());
   reject(device.get(), context.get(), fp16.view.get(), fp16.width, fp16.height, DXGI_FORMAT_R16G16B16A16_UNORM, encode.get());
   reject(device.get(), context.get(), fp16.view.get(), fp16.width, fp16.height, fp16.format, nullptr);
   Check(proxy.Decode(nullptr, context.get(), decode.get()) == E_INVALIDARG && proxy.Decode(device.get(), nullptr, decode.get()) == E_INVALIDARG && proxy.Decode(device.get(), context.get(), nullptr) == E_INVALIDARG, "reject invalid decode arguments");
   Check(SameResources(proxy, previous), "invalid decode preserves resources");
   state.Verify();
   CheckHR(device->GetDeviceRemovedReason(), "device remained healthy");
   context->ClearState();
   std::printf("PASS: production encode/decode; exhaustive finite R11/nonnegative FP16; partial 16x16 tiles; private typed resources; identity copy; source immutability; reuse/resize/format-change; rejection; CS shader/t0/u0/counters/b0-range restoration; untouched t1/u1/b1/PS/RTV (%s); class-instance restore=%s\n",
      hardware ? "NVIDIA" : "WARP", static_sentinel ? "NOT TESTED (static sentinel)" : "PASS");
   std::printf("LIMITS: FP16 negatives/NaNs/infinities and alpha roundtrip are outside this nonnegative RGB contract. Identity copy is not NGX, NR, FG or game acceptance. Allocation-failure injection and a pre-Context1 device were not tested.\n");
   if (r11_differences != 0)
      std::printf("RESULT: R11 exact restoration FAILED; differences above remain diagnostics, with no tolerance substitution.\n");
   return r11_differences == 0 ? 0 : 2;
}
