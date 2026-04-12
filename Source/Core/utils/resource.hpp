#pragma once

#if DEVELOPMENT
std::optional<std::string> GetD3DName(ID3D11DeviceChild* obj)
{
   if (obj == nullptr) return std::nullopt;

   byte data[128] = {};
   UINT size = sizeof(data);
   if (obj->GetPrivateData(WKPDID_D3DDebugObjectName, &size, data) == S_OK)
   {
      if (size > 0) return std::string{ data, data + size };
   }
   return std::nullopt;
}

// Search for wide string debug names as well, with normal string fallback. Both are converted back to non wide string.
std::optional<std::string> GetD3DNameW(ID3D11DeviceChild* obj)
{
   if (obj == nullptr) return std::nullopt;

   byte data[128] = {};
   UINT size = sizeof(data);
   if (obj->GetPrivateData(WKPDID_D3DDebugObjectNameW, &size, data) == S_OK)
   {
      if (size > 0)
      {
         char c_name[128] = {};
         size_t out_size;
         // wide-character-string-to-multibyte-string_safe
         auto ret = wcstombs_s(&out_size, c_name, sizeof(c_name), reinterpret_cast<wchar_t*>(data), size);
         if (ret == 0 && out_size > 0)
         {
            return std::string(c_name, c_name + out_size);
         }
      }
   }
   return GetD3DName(obj);
}
#endif

// Counts the base mip too
uint32_t GetTextureMaxMipLevels(uint32_t width /*>= 1*/, uint32_t height = 0, uint32_t depth = 0)
{
   uint32_t max_dimension = max(max(width, height), depth);
   return static_cast<uint32_t>(std::floor(std::log2(max_dimension))) + 1;
}

// The base level is 0
UINT GetTextureMipSize(UINT base_size, UINT mip_level)
{
   if (base_size == 0)
      return 0;
   // Shift down by mip level, clamp to at least 1, unless the base size was already 0
   return max(1, base_size >> mip_level);
}

uint3 GetTextureMipSize(uint3 base_size, UINT mip_level)
{
   uint3 mip_size;
   mip_size.x = GetTextureMipSize(base_size.x, mip_level);
   mip_size.y = GetTextureMipSize(base_size.y, mip_level);
   mip_size.z = GetTextureMipSize(base_size.z, mip_level);
   return mip_size;
}

// Useful in case we need to find the optimal amount of mips to then linearly resample a texture to a target size
uint32_t GetOptimalTextureMipLevelsForTargetSize(uint32_t src_w, uint32_t src_h, uint32_t target_w, uint32_t target_h)
{
   uint32_t max_mip = GetTextureMaxMipLevels(src_w, src_h);

   uint32_t best_mip = 0;
   float best_cost = std::numeric_limits<float>::infinity();

   // We find the most optimal mip level, that is "closest" to the target, considering two axes
   for (uint32_t mip = 0; mip < max_mip; ++mip)
   {
      uint32_t w = GetTextureMipSize(src_w, mip);
      uint32_t h = GetTextureMipSize(src_h, mip);

      // How much we'd need to scale this mip to reach the target size
      float scale_x = static_cast<float>(target_w) / static_cast<float>(w);
      float scale_y = static_cast<float>(target_h) / static_cast<float>(h);

      // We want scale ~ 1.0 in both directions. Use log2 distance from 1 as a symmetric metric:
      //   cost = |log2(scale_x)| + |log2(scale_y)|
      // This treats 0.5x and 2x as equally "far" from ideal.
      auto log2_abs = [](float x)
      {
         // Avoid log2(0)
         x = max(x, 1e-8f);
         return std::abs(std::log2(x));
      };

      float cost = log2_abs(scale_x) + log2_abs(scale_y);

      if (cost < best_cost)
      {
         best_cost = cost;
         best_mip = mip;
      }
   }

   return best_mip + 1;
}

UINT GetSRVMipLevel(const D3D11_SHADER_RESOURCE_VIEW_DESC& desc)
{
   switch (desc.ViewDimension)
   {
   case D3D11_SRV_DIMENSION_TEXTURE1D:
      return desc.Texture1D.MostDetailedMip;
   case D3D11_SRV_DIMENSION_TEXTURE1DARRAY:
      return desc.Texture1DArray.MostDetailedMip;
   case D3D11_SRV_DIMENSION_TEXTURE2D:
      return desc.Texture2D.MostDetailedMip;
   case D3D11_SRV_DIMENSION_TEXTURE2DARRAY:
      return desc.Texture2DArray.MostDetailedMip;
   case D3D11_SRV_DIMENSION_TEXTURE3D:
      return desc.Texture3D.MostDetailedMip;
   case D3D11_SRV_DIMENSION_TEXTURECUBE:
      return desc.TextureCube.MostDetailedMip;
   case D3D11_SRV_DIMENSION_TEXTURECUBEARRAY:
      return desc.TextureCubeArray.MostDetailedMip;
   case D3D11_SRV_DIMENSION_TEXTURE2DMS:
   case D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY:
   default:
      return 0;
   }
}
UINT GetRTVMipLevel(const D3D11_RENDER_TARGET_VIEW_DESC& desc)
{
   switch (desc.ViewDimension)
   {
   case D3D11_RTV_DIMENSION_TEXTURE1D:
      return desc.Texture1D.MipSlice;
   case D3D11_RTV_DIMENSION_TEXTURE1DARRAY:
      return desc.Texture1DArray.MipSlice;
   case D3D11_RTV_DIMENSION_TEXTURE2D:
      return desc.Texture2D.MipSlice;
   case D3D11_RTV_DIMENSION_TEXTURE2DARRAY:
      return desc.Texture2DArray.MipSlice;
   case D3D11_RTV_DIMENSION_TEXTURE3D:
      return desc.Texture3D.MipSlice;
   case D3D11_RTV_DIMENSION_TEXTURE2DMS:
   case D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY:
   default:
      return 0;
   }
}
UINT GetUAVMipLevel(const D3D11_UNORDERED_ACCESS_VIEW_DESC& desc)
{
   switch (desc.ViewDimension)
   {
   case D3D11_UAV_DIMENSION_TEXTURE1D:
      return desc.Texture1D.MipSlice;
   case D3D11_UAV_DIMENSION_TEXTURE1DARRAY:
      return desc.Texture1DArray.MipSlice;
   case D3D11_UAV_DIMENSION_TEXTURE2D:
      return desc.Texture2D.MipSlice;
   case D3D11_UAV_DIMENSION_TEXTURE2DARRAY:
      return desc.Texture2DArray.MipSlice;
   case D3D11_UAV_DIMENSION_TEXTURE3D:
      return desc.Texture3D.MipSlice;
   default:
      return 0;
   }
}

bool IsMipOf(uint32_t base_w, uint32_t base_h, uint32_t w, uint32_t h)
{
   if (w == 0 || h == 0 || base_w == 0 || base_h == 0)
      return false;

   // Check if w and h are powers-of-two divisions of base
   if (base_w < w || base_h < h)
      return false;

   // Check that downscaling factor is exact power of two
   bool valid_w = (base_w >> (std::countr_zero(base_w) - std::countr_zero(w))) == w;
   bool valid_h = (base_h >> (std::countr_zero(base_h) - std::countr_zero(h))) == h;

   return valid_w && valid_h;
}

void GetResourceInfo(ID3D11Resource* resource, uint4& size, DXGI_FORMAT& format, std::string* type_name = nullptr, std::string* hash = nullptr, std::string* debug_name = nullptr, bool* render_target_flag = nullptr, bool* unordered_access_flag = nullptr)
{
   size = { };
   format = DXGI_FORMAT_UNKNOWN;
   // Note: clearing strings isn't very useful, they shoudl be expected to be null already
   if (type_name)
   {
      *type_name = "";
   }
   if (hash)
   {
      *hash = "";
   }
   if (debug_name)
   {
      *debug_name = "";
   }
   if (render_target_flag)
   {
      *render_target_flag = false;
   }
   if (unordered_access_flag)
   {
      *unordered_access_flag = false;
   }
   if (!resource) return;

   if (hash)
   {
      *hash = std::to_string(std::hash<void*>{}(resource));
   }
#if DEVELOPMENT
   if (debug_name)
   {
      *debug_name = GetD3DNameW(resource).value_or("");
   }
#endif

   // Go in order of popularity
   // Note: it's possible to use "ID3D11Resource::GetType()" instead of this
   com_ptr<ID3D11Texture2D> texture_2d;
   HRESULT hr = resource->QueryInterface(&texture_2d);
   if (SUCCEEDED(hr) && texture_2d)
   {
      D3D11_TEXTURE2D_DESC texture_2d_desc;
      texture_2d->GetDesc(&texture_2d_desc);
      size = uint4{ texture_2d_desc.Width, texture_2d_desc.Height, texture_2d_desc.ArraySize, texture_2d_desc.SampleDesc.Count == 1 ? texture_2d_desc.MipLevels : texture_2d_desc.SampleDesc.Count }; // MS textures can't have mips
      format = texture_2d_desc.Format;
      ASSERT_ONCE_MSG(format != DXGI_FORMAT_UNKNOWN, "Texture format unknown?");
      if (type_name)
      {
         *type_name = "Texture 2D";
         if (texture_2d_desc.SampleDesc.Count != 1)
         {
            *type_name = "Texture 2D MS";
            if (texture_2d_desc.ArraySize != 1)
            {
               *type_name = "Texture 2D MS Array";
            }
         }
         else if (texture_2d_desc.ArraySize != 1)
         {
            *type_name = "Texture 2D Array";
            if (texture_2d_desc.ArraySize == 6 && (texture_2d_desc.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE) != 0)
            {
               *type_name = "Texture 2D Cube";
            }
         }
      }
      if (hash)
      {
         *hash = std::to_string(std::hash<void*>{}(resource));
      }
      if (render_target_flag) 
      {
         *render_target_flag = (texture_2d_desc.BindFlags & D3D11_BIND_RENDER_TARGET) != 0;
      }
      if (unordered_access_flag)
      {
         *unordered_access_flag = (texture_2d_desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0;
      }
      return;
   }
   com_ptr<ID3D11Texture3D> texture_3d;
   hr = resource->QueryInterface(&texture_3d);
   if (SUCCEEDED(hr) && texture_3d)
   {
      D3D11_TEXTURE3D_DESC texture_3d_desc;
      texture_3d->GetDesc(&texture_3d_desc);
      size = uint4{ texture_3d_desc.Width, texture_3d_desc.Height, texture_3d_desc.Depth, texture_3d_desc.MipLevels };
      format = texture_3d_desc.Format;
      ASSERT_ONCE_MSG(format != DXGI_FORMAT_UNKNOWN, "Texture format unknown?");
      if (type_name)
      {
         *type_name = "Texture 3D";
      }
      if (render_target_flag)
      {
         *render_target_flag = (texture_3d_desc.BindFlags & D3D11_BIND_RENDER_TARGET) != 0;
      }
      if (unordered_access_flag)
      {
         *unordered_access_flag = (texture_3d_desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0;
      }
      return;
   }
   com_ptr<ID3D11Texture1D> texture_1d;
   hr = resource->QueryInterface(&texture_1d);
   if (SUCCEEDED(hr) && texture_1d)
   {
      D3D11_TEXTURE1D_DESC texture_1d_desc;
      texture_1d->GetDesc(&texture_1d_desc);
      size = uint4{ texture_1d_desc.Width, texture_1d_desc.ArraySize, 1, texture_1d_desc.MipLevels };
      format = texture_1d_desc.Format;
      ASSERT_ONCE_MSG(format != DXGI_FORMAT_UNKNOWN, "Texture format unknown?");
      if (type_name)
      {
         *type_name = "Texture 1D";
         if (texture_1d_desc.ArraySize != 1)
         {
            *type_name = "Texture 1D Array";
         }
      }
      if (render_target_flag)
      {
         *render_target_flag = (texture_1d_desc.BindFlags & D3D11_BIND_RENDER_TARGET) != 0;
      }
      if (unordered_access_flag)
      {
         *unordered_access_flag = (texture_1d_desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0;
      }
      return;
   }
   com_ptr<ID3D11Buffer> buffer;
   hr = resource->QueryInterface(&buffer);
   if (SUCCEEDED(hr) && buffer)
   {
      D3D11_BUFFER_DESC buffer_desc;
      buffer->GetDesc(&buffer_desc);
      size = uint4{ buffer_desc.ByteWidth, 0, 0, 0 }; // A bit random, but it shall work
      if (type_name)
      {
         *type_name = "Buffer"; // This exact name might be assumed elsewhere, scan the code before changing it
      }
      if (render_target_flag)
      {
         *render_target_flag = false; // Implied by being a buffer!
      }
      if (unordered_access_flag)
      {
         *unordered_access_flag = (buffer_desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0;
      }
      return;
   }
   ASSERT_ONCE_MSG(false, "Unknown texture type");
}
void GetResourceInfo(ID3D11View* view, uint4& size, DXGI_FORMAT& format, std::string* type_name = nullptr, std::string* hash = nullptr, std::string* debug_name = nullptr, bool* render_target_flag = nullptr, bool* unordered_access_flag = nullptr)
{
   if (!view)
   {
      GetResourceInfo((ID3D11Resource*)nullptr, size, format, type_name, hash, debug_name, render_target_flag, unordered_access_flag);
      return;
   }
   // Note that specific cast views have a desc that could tell us the resource type
   com_ptr<ID3D11Resource> view_resource;
   view->GetResource(&view_resource);
   GetResourceInfo(view_resource.get(), size, format, type_name, hash, debug_name, render_target_flag, unordered_access_flag);
#if DEVELOPMENT
   // Add the view debug name as well if it's present
   if (debug_name)
   {
      auto view_debug_name = GetD3DNameW(view);
      if (view_debug_name.has_value())
      {
         if (!debug_name->empty()) // Separator (we could change to \n if we confirmed the way we visualize this text allows multiline)
            *debug_name += " | ";
         *debug_name += view_debug_name.value();
      }
   }
#endif
}

// Note: this is a bit approximate!
bool AreResourcesEqual(ID3D11Resource* resource1, ID3D11Resource* resource2, bool check_format = true, bool check_samples_count = true)
{
	uint4 size1, size2;
	DXGI_FORMAT format1, format2;
	GetResourceInfo(resource1, size1, format1);
	GetResourceInfo(resource2, size2, format2);
	return (check_samples_count ? (size1 == size2) : (size1.x == size2.x && size1.y == size2.y && size1.z == size2.z)) && (!check_format || format1 == format2);
}

bool AreViewsOfSameResource(ID3D11View* view1, ID3D11View* view2)
{
   if (!view1 || !view2)
      return false;
   com_ptr<ID3D11Resource> resource1;
   view1->GetResource(&resource1);
   com_ptr<ID3D11Resource> resource2;
   view2->GetResource(&resource2);
   return resource1 == resource2;
}

template<typename T>
using D3D11_RESOURCE_DESC = std::conditional_t<typeid(T) == typeid(ID3D11Texture2D), D3D11_TEXTURE2D_DESC, std::conditional_t<typeid(T) == typeid(ID3D11Texture3D), D3D11_TEXTURE3D_DESC, std::conditional_t<typeid(T) == typeid(ID3D11Texture1D), D3D11_TEXTURE1D_DESC, D3D11_BUFFER_DESC>>>;

template <typename T>
using D3D11_RESOURCE_VIEW_DESC = std::conditional_t<typeid(T) == typeid(ID3D11ShaderResourceView), D3D11_SHADER_RESOURCE_VIEW_DESC, std::conditional_t<typeid(T) == typeid(ID3D11RenderTargetView), D3D11_RENDER_TARGET_VIEW_DESC, std::conditional_t<typeid(T) == typeid(ID3D11UnorderedAccessView), D3D11_UNORDERED_ACCESS_VIEW_DESC, D3D11_DEPTH_STENCIL_VIEW_DESC>>>;

// TODO: rename to "Generic"
template <typename T = ID3D11Resource>
com_ptr<T> CloneResourceTyped(ID3D11Device* device, ID3D11DeviceContext* device_context, T* source)
{
   if (!source) return nullptr;

   com_ptr<T> cloned_resource;

   // Note: some misc flags like "D3D11_RESOURCE_MISC_SHARED" or "D3D11_RESOURCE_MISC_GUARDED" might theoretically be better removed, but there's no need to until we found a use case.
   // We could also optionally remove the "CPUAccessFlags" given we likely won't need them, but that should be optional if so.
   D3D11_RESOURCE_DESC<T> desc;
   source->GetDesc(&desc);

   // Arguable, but return the same resource if it's immutable, we don't really need to clone it and it'd fail if we didn't provide the initial data,
   // which would require mapping the source resource to be retrieved, and it's just unnecessary.
   if (desc.Usage == D3D11_USAGE_IMMUTABLE)
   {
      return source;
   }

   HRESULT hr = E_FAIL;
   if constexpr (std::is_same_v<T, ID3D11Buffer>)
   {
      hr = device->CreateBuffer(&desc, nullptr, &cloned_resource);
   }
   else if constexpr (std::is_same_v<T, ID3D11Texture2D>)
   {
      hr = device->CreateTexture2D(&desc, nullptr, &cloned_resource);
   }
   else if constexpr (std::is_same_v<T, ID3D11Texture3D>)
   {
      hr = device->CreateTexture3D(&desc, nullptr, &cloned_resource);
   }
   else if constexpr (std::is_same_v<T, ID3D11Texture1D>)
   {
      hr = device->CreateTexture1D(&desc, nullptr, &cloned_resource);
   }
   ASSERT_ONCE(SUCCEEDED(hr));

   if (SUCCEEDED(hr))
   {
      device_context->CopyResource(cloned_resource.get(), source);
   }

   return cloned_resource;
}

com_ptr<ID3D11Resource> CloneResource(ID3D11Device* device, ID3D11DeviceContext* device_context, ID3D11Resource* source)
{
   if (!device || !device_context || !source)
      return nullptr;

   D3D11_RESOURCE_DIMENSION type;
   source->GetType(&type);

   switch (type)
   {
   case D3D11_RESOURCE_DIMENSION_BUFFER:
   {
      return (com_ptr<ID3D11Resource>&&)(CloneResourceTyped<ID3D11Buffer>(device, device_context, static_cast<ID3D11Buffer*>(source)));
   }
   case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
   {
      return (com_ptr<ID3D11Resource>&&)CloneResourceTyped<ID3D11Texture1D>(device, device_context, static_cast<ID3D11Texture1D*>(source));
   }
   case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
   {
      return (com_ptr<ID3D11Resource>&&)CloneResourceTyped<ID3D11Texture2D>(device, device_context, static_cast<ID3D11Texture2D*>(source));
   }
   case D3D11_RESOURCE_DIMENSION_TEXTURE3D:
   {
      return (com_ptr<ID3D11Resource>&&)CloneResourceTyped<ID3D11Texture3D>(device, device_context, static_cast<ID3D11Texture3D*>(source));
   }
   }

   return nullptr;
}

template<typename T = ID3D11Resource>
com_ptr<T> CloneTexture(ID3D11Device* native_device, ID3D11Resource* texture_resource, DXGI_FORMAT overridden_format = DXGI_FORMAT_UNKNOWN, UINT add_bind_flags = (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET), UINT remove_bind_flags = 0, bool black_initial_data = false, bool copy_data = true, ID3D11DeviceContext* native_device_context = nullptr, UINT overridden_mip_levels = -1, UINT overridden_samples_count = -1)
{
   com_ptr<T> cloned_resource;
   ASSERT_ONCE(texture_resource);
   if (texture_resource)
   {
      com_ptr<T> texture;
      HRESULT hr = texture_resource->QueryInterface(&texture);
      if (SUCCEEDED(hr) && texture)
      {
         D3D11_RESOURCE_DESC<T> texture_desc;
         if constexpr (std::is_same_v<T, ID3D11Texture2D> || std::is_same_v<T, ID3D11Texture3D> || std::is_same_v<T, ID3D11Texture1D>)
         {
            texture->GetDesc(&texture_desc);
         }
         else
         {
            static_assert(false, "Clone Resource Type not supported");
         }

         if (overridden_format != DXGI_FORMAT_UNKNOWN)
         {
            texture_desc.Format = overridden_format;
         }
         if (overridden_mip_levels != -1)
         {
            texture_desc.MipLevels = overridden_mip_levels;
            if (overridden_mip_levels != 1)
            {
               texture_desc.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
               // Add the other required flags
               add_bind_flags |= D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
               remove_bind_flags &= ~(D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);
            }
            else
               texture_desc.MiscFlags &= ~D3D11_RESOURCE_MISC_GENERATE_MIPS;
         }
         texture_desc.BindFlags |= add_bind_flags;
         texture_desc.BindFlags &= ~remove_bind_flags;
         if constexpr (std::is_same_v<T, ID3D11Texture2D>)
         {
            if (overridden_samples_count != -1)
            {
               texture_desc.SampleDesc.Count = overridden_samples_count;
            }
         }
         // Hack to clear unwanted flags, we likely don't need any CPU write
         if ((add_bind_flags & (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET)) != 0)
         {
            if ((add_bind_flags & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS)) == 0) // A bit random tests
            {
               ASSERT_ONCE(texture_desc.Usage == 0 && texture_desc.CPUAccessFlags == 0);
            }
            texture_desc.Usage = D3D11_USAGE_DEFAULT;
            texture_desc.CPUAccessFlags = 0;
         }
         bool is_ms = false;
         if constexpr (std::is_same_v<T, ID3D11Texture2D>)
         {
            is_ms = texture_desc.SampleDesc.Count != 1;
         }

         D3D11_SUBRESOURCE_DATA initial_data = {};
         uint8_t* data = nullptr;
         // Initial data isn't supported on MSAA textures
         if (black_initial_data && !is_ms)
         {
            ASSERT_ONCE_MSG(texture_desc.MipLevels == 1, "We only define the initial data for the first mip, the rest will be uncleared memory");

            uint8_t channels = 0;
            uint8_t bits_per_channel = 0;
            const bool supported_format = GetFormatSizeInfo(texture_desc.Format, channels, bits_per_channel);

            if (supported_format)
            {
               // Mips are not included in the initial data
               UINT width, height, depth;
               if constexpr (std::is_same_v<T, ID3D11Texture2D>)
               {
                  width = texture_desc.Width;
                  height = texture_desc.Height;
                  depth = texture_desc.ArraySize;
               }
               else if constexpr (std::is_same_v<T, ID3D11Texture3D>)
               {
                  width = texture_desc.Width;
                  height = texture_desc.Height;
                  depth = texture_desc.Depth;
               }
               else if constexpr (std::is_same_v<T, ID3D11Texture1D>)
               {
                  width = texture_desc.Width;
                  height = 1;
                  depth = 1;
               }

               if (bits_per_channel == 8)
               {
                  data = (uint8_t*)malloc(width * height * depth * channels * sizeof(uint8_t));
                  memset(data, 0, width * height * depth * channels * sizeof(uint8_t));
               }
               else if (bits_per_channel == 16)
               {
                  uint16_t* data_16 = nullptr;
                  data_16 = (uint16_t*)malloc(width * height * depth * channels * sizeof(uint16_t));
                  memset(data_16, 0, width * height * depth * channels * sizeof(uint16_t));
                  data = (uint8_t*)data_16;
               }
               ASSERT_ONCE(bits_per_channel % 8 == 0);

               initial_data.pSysMem = data;
               if constexpr (std::is_same_v<T, ID3D11Texture2D> || std::is_same_v<T, ID3D11Texture3D>)
               {
                  initial_data.SysMemPitch = texture_desc.Width * channels * (bits_per_channel / 8); // Width * bytes per pixel
                  initial_data.SysMemSlicePitch = initial_data.SysMemPitch * texture_desc.Height; // Pitch * Height
               }
               else if constexpr (std::is_same_v<T, ID3D11Texture1D>)
               {
                  initial_data.SysMemPitch = texture_desc.Width * channels * (bits_per_channel / 8);
                  initial_data.SysMemSlicePitch = 0;
               }
            }
         }

         // TODO: use "CloneResourceTyped()" now that we have it
         if constexpr (std::is_same_v<T, ID3D11Texture2D>)
         {
            hr = native_device->CreateTexture2D(&texture_desc, black_initial_data ? &initial_data : nullptr, &cloned_resource);
         }
         else if constexpr (std::is_same_v<T, ID3D11Texture3D>)
         {
            hr = native_device->CreateTexture3D(&texture_desc, black_initial_data ? &initial_data : nullptr, &cloned_resource);
         }
         else if constexpr (std::is_same_v<T, ID3D11Texture1D>)
         {
            hr = native_device->CreateTexture1D(&texture_desc, black_initial_data ? &initial_data : nullptr, &cloned_resource);
         }
         ASSERT_ONCE(SUCCEEDED(hr));

         if (black_initial_data)
         {
            free(data);
            data = nullptr;
         }

         if (copy_data && SUCCEEDED(hr) && cloned_resource.get())
         {
            assert(native_device_context);
            native_device_context->CopyResource(cloned_resource.get(), texture_resource);
         }
      }
   }
   return cloned_resource;
}

// Makes a clone of a resource view and the underlying resource, and returns the reference to the cloned view (the cloned resource isn't directly return, but will be kept alive, and is accessible, through the view)
template <typename T = ID3D11View>
com_ptr<T> CloneResourceAndView(ID3D11Device* device, ID3D11DeviceContext* device_context, T* source_view)
{
   if (!source_view)
      return nullptr;

   com_ptr<ID3D11Resource> source_resource;
   source_view->GetResource(&source_resource);
   com_ptr<ID3D11Resource> cloned_resource = CloneResource(device, device_context, source_resource.get());

   D3D11_RESOURCE_VIEW_DESC<T> desc;
   source_view->GetDesc(&desc);

   com_ptr<T> cloned_resource_view;

   HRESULT hr = E_FAIL;
   if constexpr (std::is_same_v<T, ID3D11ShaderResourceView>)
   {
      hr = device->CreateShaderResourceView(cloned_resource.get(), &desc, &cloned_resource_view);
   }
   else if constexpr (std::is_same_v<T, ID3D11RenderTargetView>)
   {
      hr = device->CreateRenderTargetView(cloned_resource.get(), &desc, &cloned_resource_view);
   }
   else if constexpr (std::is_same_v<T, ID3D11UnorderedAccessView>)
   {
      hr = device->CreateUnorderedAccessView(cloned_resource.get(), &desc, &cloned_resource_view);
   }
   else if constexpr (std::is_same_v<T, ID3D11DepthStencilView>)
   {
      hr = device->CreateDepthStencilView(cloned_resource.get(), &desc, &cloned_resource_view);
   }
   ASSERT_ONCE(SUCCEEDED(hr));

   return cloned_resource_view;
}

// Define source pixel structure (8-bit per channel)
struct R8G8B8A8_UNORM
{
   uint8_t r, g, b, a;
};
struct B8G8R8A8_UNORM
{
   uint8_t b, g, r, a;
};
struct R16G16B16A16_FLOAT
{
   uint16_t r, g, b, a;
};

inline uint16_t ConvertFloatToHalf(float value)
{
   // XMConvertFloatToHalf converts a float to a half, returning the 16-bit unsigned short representation.
   return DirectX::PackedVector::XMConvertFloatToHalf(value);
}

inline __m128 u8rgba_to_unorm4(const uint8_t* rgba)
{
   // load 4 bytes into the low 32 bits
   __m128i bytes = _mm_cvtsi32_si128(*(const int32_t*)rgba); // [r g b a 0 0 0 0 ...]

   // zero-extend the 4 bytes to 4x int32: [r g b a]
   __m128i ints = _mm_cvtepu8_epi32(bytes); // SSE4.1

   __m128 f = _mm_cvtepi32_ps(ints); // int -> float
   return _mm_mul_ps(f, _mm_set1_ps(1.0f / 255.0f));
}

template<typename T>
void ConvertR8G8B8A8toR16G16B16A16(
   const T* src_data,
   R16G16B16A16_FLOAT* dst_data,
   size_t width,
   size_t height,
   size_t depth = 1)
{
   size_t slice_size = width * height;

   for (size_t z = 0; z < depth; ++z)
   {
      size_t slice_offset = slice_size * z;

      for (size_t i = 0; i < slice_size; i++)
      {
         const T& pixel = src_data[slice_offset + i];

         // Read each channel and normalize from 0-255 to 0.0-1.0.
         float r = pixel.r / 255.0f;
         float g = pixel.g / 255.0f;
         float b = pixel.b / 255.0f;
         float a = pixel.a / 255.0f;
         // TODO: use simd instead, like "u8rgba_to_unorm4" above
         //struct Pixel { uint8_t r,g,b,a; } pixel;
         //__m128 rgba = u8rgba_to_unorm4(&pixel.r);

         // Convert normalized floats to half-floats.
         dst_data[i].r = ConvertFloatToHalf(r);
         dst_data[i].g = ConvertFloatToHalf(g);
         dst_data[i].b = ConvertFloatToHalf(b);
         dst_data[i].a = ConvertFloatToHalf(a);
      }
   }
}

#if DEVELOPMENT
// Needs to match in shader
enum class DebugDrawTextureOptionsMask : uint32_t
{
   None = 0,
   Fullscreen = 1 << 0,
   RenderResolutionScale = 1 << 1,
   ShowAlpha = 1 << 2,
   PreMultiplyAlpha = 1 << 3,
   InvertColors = 1 << 4,
   LinearToGamma = 1 << 5,
   GammaToLinear = 1 << 6,
   FlipY = 1 << 7,
   Saturate = 1 << 8,
   RedOnly = 1 << 9,
   BackgroundPassthrough = 1 << 10,
   TextureMultiSample = 1 << 11,
   TextureArray = 1 << 12,
   // If this is true and Texture3D is also true, the texture is a cube. If both are false it's 1D.
   Texture2D = 1 << 13,
   Texture3D = 1 << 14,
   Abs = 1 << 15,
   Zoom4x = 1 << 16,
   Bilinear = 1 << 17,
   Tonemap = 1 << 18,
   SRGB = 1 << 19,
   UVToPixelSpace = 1 << 20,
   Denormalize = 1 << 21,
};
enum class DebugDrawMode : uint32_t
{
   Custom,
   // TODO: rename all of these to "View", or not
   RenderTarget,
   UnorderedAccessView,
   ShaderResource,
   Depth,
   Stencil,
};
static constexpr const char* debug_draw_mode_strings[] = {
    "Custom",
    "Render Target",
    "Unordered Access View",
    "Shader Resource",
    "Depth",
    "Stencil",
};

bool CopyDebugDrawTexture(DebugDrawMode debug_draw_mode, int32_t debug_draw_view_index, reshade::api::command_list* cmd_list, bool is_dispatch /*= false*/)
{
   ID3D11Device* native_device = (ID3D11Device*)(cmd_list->get_device()->get_native());
   ID3D11DeviceContext* native_device_context = (ID3D11DeviceContext*)(cmd_list->get_native());
   DeviceData& device_data = *cmd_list->get_device()->get_private_data<DeviceData>();

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
#endif // DEVELOPMENT
