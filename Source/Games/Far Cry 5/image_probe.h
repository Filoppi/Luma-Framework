#pragma once
// One-shot render-resource readback, not a desktop screenshot. RGBA8 BMP or raw FP16.
// Captures do not apply Windows Auto HDR or the Windows display transform.
namespace FC5
{
   inline bool SaveImage(ID3D11Device* device, ID3D11DeviceContext* ctx,
      ID3D11Resource* resource, const std::filesystem::path& path)
   {
      com_ptr<ID3D11Texture2D> source, staging;
      if (!resource || FAILED(resource->QueryInterface(&source))) return false;
      D3D11_TEXTURE2D_DESC d{}; source->GetDesc(&d);
      const bool hdr = d.Format == DXGI_FORMAT_R16G16B16A16_FLOAT || d.Format == DXGI_FORMAT_R16G16B16A16_TYPELESS;
      if (!IsSingleSurface(d) || (!hdr && !IsRGBA8View(d.Format, DXGI_FORMAT_R8G8B8A8_UNORM)) ||
         d.Width > 16384 || d.Height > 16384) return false;
      d.Usage = D3D11_USAGE_STAGING; d.BindFlags = d.MiscFlags = 0;
      d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
      if (FAILED(device->CreateTexture2D(&d, nullptr, &staging))) return false;
      ctx->CopyResource(staging.get(), source.get());
      const size_t pixel_bytes = hdr ? 8 : 4;
      std::vector<uint8_t> pixels(size_t(d.Width) * d.Height * pixel_bytes);
      D3D11_MAPPED_SUBRESOURCE map{};
      if (FAILED(ctx->Map(staging.get(), 0, D3D11_MAP_READ, 0, &map))) return false;
      for (UINT y = 0; y < d.Height; ++y)
      {
         const auto* src = static_cast<const uint8_t*>(map.pData) + size_t(y) * map.RowPitch;
         auto* dst = pixels.data() + size_t(y) * d.Width * pixel_bytes;
         if (hdr) { std::memcpy(dst, src, size_t(d.Width) * pixel_bytes); continue; }
         for (UINT x = 0; x < d.Width; ++x)
         {
            dst[x*4] = src[x*4+2]; dst[x*4+1] = src[x*4+1];
            dst[x*4+2] = src[x*4]; dst[x*4+3] = 255;
         }
      }
      ctx->Unmap(staging.get(), 0);
      BITMAPFILEHEADER file{}; BITMAPINFOHEADER info{};
      file.bfType = 0x4d42; file.bfOffBits = sizeof(file) + sizeof(info);
      file.bfSize = file.bfOffBits + DWORD(pixels.size());
      info.biSize = sizeof(info); info.biWidth = LONG(d.Width); info.biHeight = -LONG(d.Height);
      info.biPlanes = 1; info.biBitCount = 32; info.biCompression = BI_RGB;
      info.biSizeImage = DWORD(pixels.size());
      // Exclusive creation: never overwrite a user's existing capture.
      HANDLE output = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (output == INVALID_HANDLE_VALUE) return false;
      auto write = [&](const void* data, DWORD size) {
         DWORD written = 0; return WriteFile(output, data, size, &written, nullptr) && written == size;
      };
      const bool ok = (hdr || (write(&file, sizeof(file)) && write(&info, sizeof(info)))) && write(pixels.data(), DWORD(pixels.size()));
      CloseHandle(output);
      return ok;
   }
}
