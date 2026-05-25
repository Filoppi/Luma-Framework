#pragma once

#include <d3d11_4.h>
#include <unordered_map>
#include <vector>
#include "com_ptr.h"

// TODO: copy/move.
// TODO: Move this somewhere else.
// TODO: Handle views per MIP?
// TODO: Handle DSV?
// TODO: Handle multiple views?
// TODO: Handle texture arrays?
// TODO: Give up on all this?
class LumaTexture
{
public:

    LumaTexture(ID3D11Device* device, const D3D11_TEXTURE1D_DESC* tex_desc, const D3D11_SUBRESOURCE_DATA* initial_data = nullptr, const D3D11_SHADER_RESOURCE_VIEW_DESC* srv_desc = nullptr, const D3D11_RENDER_TARGET_VIEW_DESC* rtv_desc = nullptr, const D3D11_UNORDERED_ACCESS_VIEW_DESC* uav_desc = nullptr)
    {
        Create(device, tex_desc, initial_data, srv_desc, rtv_desc, uav_desc);
    }

    LumaTexture(ID3D11Device* device, const D3D11_TEXTURE2D_DESC* tex_desc, const D3D11_SUBRESOURCE_DATA* initial_data = nullptr, const D3D11_SHADER_RESOURCE_VIEW_DESC* srv_desc = nullptr, const D3D11_RENDER_TARGET_VIEW_DESC* rtv_desc = nullptr, const D3D11_UNORDERED_ACCESS_VIEW_DESC* uav_desc = nullptr)
    {
        Create(device, tex_desc, initial_data, srv_desc, rtv_desc, uav_desc);
    }

    LumaTexture(ID3D11Device* device, const D3D11_TEXTURE3D_DESC* tex_desc, const D3D11_SUBRESOURCE_DATA* initial_data = nullptr, const D3D11_SHADER_RESOURCE_VIEW_DESC* srv_desc = nullptr, const D3D11_RENDER_TARGET_VIEW_DESC* rtv_desc = nullptr, const D3D11_UNORDERED_ACCESS_VIEW_DESC* uav_desc = nullptr)
    {
        Create(device, tex_desc, initial_data, srv_desc, rtv_desc, uav_desc);
    }

 public:

     void Create(ID3D11Device* device, const D3D11_TEXTURE1D_DESC* tex_desc, const D3D11_SUBRESOURCE_DATA* initial_data = nullptr, const D3D11_SHADER_RESOURCE_VIEW_DESC* srv_desc = nullptr, const D3D11_RENDER_TARGET_VIEW_DESC* rtv_desc = nullptr, const D3D11_UNORDERED_ACCESS_VIEW_DESC* uav_desc = nullptr)
    {
        ensure(device->CreateTexture1D(tex_desc, initial_data, (ID3D11Texture1D**)&texture), >= 0);
        CreateViews(device, tex_desc->BindFlags, srv_desc, rtv_desc, uav_desc);
    }

    void Create(ID3D11Device* device, const D3D11_TEXTURE2D_DESC* tex_desc, const D3D11_SUBRESOURCE_DATA* initial_data = nullptr, const D3D11_SHADER_RESOURCE_VIEW_DESC* srv_desc = nullptr, const D3D11_RENDER_TARGET_VIEW_DESC* rtv_desc = nullptr, const D3D11_UNORDERED_ACCESS_VIEW_DESC* uav_desc = nullptr)
    {
        ensure(device->CreateTexture2D(tex_desc, initial_data, (ID3D11Texture2D**)&texture), >= 0);
        CreateViews(device, tex_desc->BindFlags, srv_desc, rtv_desc, uav_desc);
    }

    void Create(ID3D11Device* device, const D3D11_TEXTURE3D_DESC* tex_desc, const D3D11_SUBRESOURCE_DATA* initial_data = nullptr, const D3D11_SHADER_RESOURCE_VIEW_DESC* srv_desc = nullptr, const D3D11_RENDER_TARGET_VIEW_DESC* rtv_desc = nullptr, const D3D11_UNORDERED_ACCESS_VIEW_DESC* uav_desc = nullptr)
    {
        ensure(device->CreateTexture3D(tex_desc, initial_data, (ID3D11Texture3D**)&texture), >= 0);
        CreateViews(device, tex_desc->BindFlags, srv_desc, rtv_desc, uav_desc);
    }

    ID3D11Resource* GetResource() const
    {
        return (ID3D11Resource*)texture;
    }

    ID3D11ShaderResourceView* GetSRV() const
    {
        return srv;
    }

    ID3D11RenderTargetView* GetRTV() const
    {
        return rtv;
    }

    ID3D11UnorderedAccessView* GetUAV() const
    {
        return uav;
    }

    void GetTextureDesc(D3D11_TEXTURE1D_DESC* desc) const
    {
        ((ID3D11Texture1D*)texture)->GetDesc(desc);
    }

    void GetTextureDesc(D3D11_TEXTURE2D_DESC* desc) const
    {
        ((ID3D11Texture2D*)texture)->GetDesc(desc);
    }

    void GetTextureDesc(D3D11_TEXTURE3D_DESC* desc) const
    {
        ((ID3D11Texture3D*)texture)->GetDesc(desc);
    }

    void Reset()
    {
        if (texture)
        {
            ((IUnknown*)texture)->Release();
            texture = nullptr;
        }
        if (srv)
        {
            srv->Release();
            srv = nullptr;
        }
        if (rtv)
        {
            rtv->Release();
            rtv = nullptr;
        }
        if (uav)
        {
            uav->Release();
            uav = nullptr;
        }
    }

public:

    ~LumaTexture()
    {
        if (texture)
        {
            ((IUnknown*)texture)->Release();
        }
        if (srv)
        {
            srv->Release();
        }
        if (rtv)
        {
            rtv->Release();
        }
        if (uav)
        {
            uav->Release();
        }
    }

private:

    void CreateViews(ID3D11Device* device, UINT bind_flags, const D3D11_SHADER_RESOURCE_VIEW_DESC* srv_desc = nullptr, const D3D11_RENDER_TARGET_VIEW_DESC* rtv_desc = nullptr, const D3D11_UNORDERED_ACCESS_VIEW_DESC* uav_desc = nullptr)
    {
        if (bind_flags & D3D11_BIND_SHADER_RESOURCE)
        {
            ensure(device->CreateShaderResourceView((ID3D11Resource*)texture, srv_desc, &srv), >= 0);
        }
        if (bind_flags & D3D11_BIND_RENDER_TARGET)
        {
            ensure(device->CreateRenderTargetView((ID3D11Resource*)texture, rtv_desc, &rtv), >= 0);
        }
        if (bind_flags & D3D11_BIND_UNORDERED_ACCESS)
        {
            ensure(device->CreateUnorderedAccessView((ID3D11Resource*)texture, uav_desc, &uav), >= 0);
        }
    }

    void* texture = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11UnorderedAccessView* uav = nullptr;
};

struct ManagedResources
{
    // Key should be result of CompileTimeStringHash()!
    // Example usage: shader_resource_views[CompileTimeStringHash("scene")] = srv_scene;
    // Example usage: shader_resource_views["scene"_h] = srv_scene;
    std::unordered_map<uint32_t, ComPtr<ID3D11ShaderResourceView>> shader_resource_views;
    std::unordered_map<uint32_t, ComPtr<ID3D11RenderTargetView>> render_target_views;
    std::unordered_map<uint32_t, ComPtr<ID3D11UnorderedAccessView>> unordered_access_views;
    std::unordered_map<uint32_t, ComPtr<ID3D11DepthStencilView>> depth_stencil_views;
    std::unordered_map<uint32_t, ComPtr<ID3D11Resource>> resources;
    std::unordered_map<uint32_t, ComPtr<ID3D11Buffer>> buffers;
    std::unordered_map<uint32_t, ComPtr<ID3D11Texture1D>> textures_1d;
    std::unordered_map<uint32_t, ComPtr<ID3D11Texture2D>> textures_2d;
    std::unordered_map<uint32_t, ComPtr<ID3D11Texture3D>> textures_3d;
    std::unordered_map<uint32_t, ComPtr<ID3D11InputLayout>> input_layouts;
    std::unordered_map<uint32_t, ComPtr<ID3D11RasterizerState>> rasterizers;
    std::unordered_map<uint32_t, ComPtr<ID3D11SamplerState>> samplers;
    std::unordered_map<uint32_t, ComPtr<ID3D11BlendState>> blends;
    std::unordered_map<uint32_t, ComPtr<ID3D11DepthStencilState>> depth_stencils;
    std::unordered_map<uint32_t, ComPtr<ID3D11VertexShader>> vertex_shaders;
    std::unordered_map<uint32_t, ComPtr<ID3D11ComputeShader>> compute_shaders;
    std::unordered_map<uint32_t, ComPtr<ID3D11PixelShader>> pixel_shaders;
    std::unordered_map<uint32_t, ComPtr<ID3D11DomainShader>> domain_shaders;
    std::unordered_map<uint32_t, ComPtr<ID3D11GeometryShader>> geometry_shaders;
    std::unordered_map<uint32_t, ComPtr<ID3D11HullShader>> hull_shaders;
};

// TODO: Move this somewhere else.
inline void ResetCOMArray(auto& array)
{
    for (auto*& ptr : array)
    {
        if (ptr)
        {
            ptr->Release();
            ptr = nullptr;
        }
    }
}