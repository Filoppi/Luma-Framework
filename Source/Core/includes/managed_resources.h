#pragma once

#include <d3d11_4.h>
#include <unordered_map>
#include <vector>
#include "com_ptr.h"
#include "math.h"

using Math::operator"" _h;

// TODO: Test this.
// TODO: Add other operators, !, !=, ==.
// TODO: Move this somewhere else.
// TODO: Give up on all this?
class LumaTexture
{
public:

    LumaTexture(ID3D11Device* device, const D3D11_TEXTURE1D_DESC* tex_desc, const D3D11_SUBRESOURCE_DATA* initial_data = nullptr, const D3D11_SHADER_RESOURCE_VIEW_DESC* srv_desc = nullptr, const D3D11_RENDER_TARGET_VIEW_DESC* rtv_desc = nullptr, const D3D11_UNORDERED_ACCESS_VIEW_DESC* uav_desc = nullptr)
        : device(device)
    {
        Create(tex_desc, initial_data, srv_desc, rtv_desc, uav_desc);
    }

    LumaTexture(ID3D11Device* device, const D3D11_TEXTURE2D_DESC* tex_desc, const D3D11_SUBRESOURCE_DATA* initial_data = nullptr, const D3D11_SHADER_RESOURCE_VIEW_DESC* srv_desc = nullptr, const D3D11_RENDER_TARGET_VIEW_DESC* rtv_desc = nullptr, const D3D11_UNORDERED_ACCESS_VIEW_DESC* uav_desc = nullptr, const D3D11_DEPTH_STENCIL_VIEW_DESC* dsv_desc = nullptr)
        : device(device)
    {
        Create(tex_desc, initial_data, srv_desc, rtv_desc, uav_desc, dsv_desc);
    }

    LumaTexture(ID3D11Device* device, const D3D11_TEXTURE3D_DESC* tex_desc, const D3D11_SUBRESOURCE_DATA* initial_data = nullptr, const D3D11_SHADER_RESOURCE_VIEW_DESC* srv_desc = nullptr, const D3D11_RENDER_TARGET_VIEW_DESC* rtv_desc = nullptr, const D3D11_UNORDERED_ACCESS_VIEW_DESC* uav_desc = nullptr)
        : device(device)
    {
        Create(tex_desc, initial_data, srv_desc, rtv_desc, uav_desc);
    }

    LumaTexture(const LumaTexture& other) noexcept
        : device(other.device),
        texture(other.texture),
        srvs(other.srvs),
        rtvs(other.rtvs),
        uavs(other.uavs),
        dsvs(other.dsvs)
    {
        AddRef();
    }

    LumaTexture(LumaTexture&& other) noexcept
        : device(other.device),
        texture(other.texture),
        srvs(std::move(other.srvs)),
        rtvs(std::move(other.rtvs)),
        uavs(std::move(other.uavs)),
        dsvs(std::move(other.dsvs))
    {
        other.device = nullptr;
        other.texture = nullptr;
    }

public:
    
    LumaTexture& operator=(const LumaTexture& other) noexcept
    {
        if (this != &other)
        {
            Release();

            device = other.device;
            texture = other.texture;
            srvs = other.srvs;
            rtvs = other.rtvs;
            uavs = other.uavs;
            dsvs = other.dsvs;

            AddRef();
        }

        return *this;
    }

    LumaTexture& operator=(LumaTexture&& other) noexcept
    {
        if (this != &other)
        {
            Release();

            device = other.device;
            other.device = nullptr;

            texture = other.texture;
            other.texture = nullptr;

            srvs = std::move(other.srvs);
            rtvs = std::move(other.rtvs);
            uavs = std::move(other.uavs);
            dsvs = std::move(other.dsvs);
        }

        return *this;
    }

public:

     void Create(const D3D11_TEXTURE1D_DESC* tex_desc, const D3D11_SUBRESOURCE_DATA* initial_data = nullptr, const D3D11_SHADER_RESOURCE_VIEW_DESC* srv_desc = nullptr, const D3D11_RENDER_TARGET_VIEW_DESC* rtv_desc = nullptr, const D3D11_UNORDERED_ACCESS_VIEW_DESC* uav_desc = nullptr)
    {
        ensure(device->CreateTexture1D(tex_desc, initial_data, (ID3D11Texture1D**)&texture), >= 0);
        CreateDefaultViews(device, tex_desc->BindFlags, srv_desc, rtv_desc, uav_desc);
    }

    void Create(const D3D11_TEXTURE2D_DESC* tex_desc, const D3D11_SUBRESOURCE_DATA* initial_data = nullptr, const D3D11_SHADER_RESOURCE_VIEW_DESC* srv_desc = nullptr, const D3D11_RENDER_TARGET_VIEW_DESC* rtv_desc = nullptr, const D3D11_UNORDERED_ACCESS_VIEW_DESC* uav_desc = nullptr, const D3D11_DEPTH_STENCIL_VIEW_DESC* dsv_desc = nullptr)
    {
        ensure(device->CreateTexture2D(tex_desc, initial_data, (ID3D11Texture2D**)&texture), >= 0);
        CreateDefaultViews(device, tex_desc->BindFlags, srv_desc, rtv_desc, uav_desc, dsv_desc);
    }

    void Create(const D3D11_TEXTURE3D_DESC* tex_desc, const D3D11_SUBRESOURCE_DATA* initial_data = nullptr, const D3D11_SHADER_RESOURCE_VIEW_DESC* srv_desc = nullptr, const D3D11_RENDER_TARGET_VIEW_DESC* rtv_desc = nullptr, const D3D11_UNORDERED_ACCESS_VIEW_DESC* uav_desc = nullptr)
    {
        ensure(device->CreateTexture3D(tex_desc, initial_data, (ID3D11Texture3D**)&texture), >= 0);
        CreateDefaultViews(device, tex_desc->BindFlags, srv_desc, rtv_desc, uav_desc);
    }

public:

    ID3D11Resource* GetResource() const noexcept
    {
        return (ID3D11Resource*)texture;
    }

    ID3D11ShaderResourceView* GetSRV(uint32_t hash = "default"_h) const
    {
        return srvs.at(hash);
    }

    ID3D11RenderTargetView* GetRTV(uint32_t hash = "default"_h) const
    {
        return rtvs.at(hash);
    }

    ID3D11UnorderedAccessView* GetUAV(uint32_t hash = "default"_h) const
    {
        return uavs.at(hash);
    }

    ID3D11DepthStencilView* GetDSV(uint32_t hash = "default"_h) const
    {
        return dsvs.at(hash);
    }

    void GetTextureDesc(D3D11_TEXTURE1D_DESC* desc) const noexcept
    {
        ((ID3D11Texture1D*)texture)->GetDesc(desc);
    }

    void GetTextureDesc(D3D11_TEXTURE2D_DESC* desc) const noexcept
    {
        ((ID3D11Texture2D*)texture)->GetDesc(desc);
    }

    void GetTextureDesc(D3D11_TEXTURE3D_DESC* desc) const noexcept
    {
        ((ID3D11Texture3D*)texture)->GetDesc(desc);
    }

    ID3D11Device* GetDevice() const noexcept
    {
        return device;
    }

public:

    void AddSRV(uint32_t hash, const D3D11_SHADER_RESOURCE_VIEW_DESC* srv_desc = nullptr) noexcept
    {
        assert(srvs.find(hash) == srvs.end());
        ensure(device->CreateShaderResourceView((ID3D11Resource*)texture, srv_desc, &srvs[hash]), >= 0);
    }

    void AddRTV(uint32_t hash, const D3D11_RENDER_TARGET_VIEW_DESC* rtv_desc = nullptr) noexcept
    {
        assert(rtvs.find(hash) == rtvs.end());
        ensure(device->CreateRenderTargetView((ID3D11Resource*)texture, rtv_desc, &rtvs[hash]), >= 0);
    }

    void AddUAV(uint32_t hash, const D3D11_UNORDERED_ACCESS_VIEW_DESC* uav_desc = nullptr) noexcept
    {
        assert(uavs.find(hash) == uavs.end());
        ensure(device->CreateUnorderedAccessView((ID3D11Resource*)texture, uav_desc, &uavs[hash]), >= 0);
    }

    void AddDSV(uint32_t hash, const D3D11_DEPTH_STENCIL_VIEW_DESC* dsv_desc = nullptr) noexcept
    {
        assert(dsvs.find(hash) == dsvs.end());
        ensure(device->CreateDepthStencilView((ID3D11Resource*)texture, dsv_desc, &dsvs[hash]), >= 0);
    }

public:

    // It clears keys too.
    void Reset() noexcept
    {
        // Reset texture.
        if (texture)
        {
            ((IUnknown*)texture)->Release();
            texture = nullptr;
        }

        // Reset SRVs.
        for (auto& srv : srvs)
        {
            if (srv.second)
            {
                srv.second->Release();
            }
        }
        srvs.clear();

        // Reset RTVs.
        for (auto& rtv : rtvs)
        {
            if (rtv.second)
            {
                rtv.second->Release();
            }
        }
        rtvs.clear();

        // Reset UAVs.
        for (auto& uav : uavs)
        {
            if (uav.second)
            {
                uav.second->Release();
            }
        }
        uavs.clear();

        // Reset DSVs
        for (auto& dsv : dsvs)
        {
            if (dsv.second)
            {
                dsv.second->Release();
            }
        }
        dsvs.clear();
    }

public:

    ~LumaTexture()
    {
        Release();
    }

private:

    void CreateDefaultViews(ID3D11Device* device, UINT bind_flags, const D3D11_SHADER_RESOURCE_VIEW_DESC* srv_desc = nullptr, const D3D11_RENDER_TARGET_VIEW_DESC* rtv_desc = nullptr, const D3D11_UNORDERED_ACCESS_VIEW_DESC* uav_desc = nullptr, const D3D11_DEPTH_STENCIL_VIEW_DESC* dsv_desc = nullptr)
    {
        if (bind_flags & D3D11_BIND_SHADER_RESOURCE)
        {
            ensure(device->CreateShaderResourceView((ID3D11Resource*)texture, srv_desc, &srvs["default"_h]), >= 0);
        }
        if (bind_flags & D3D11_BIND_RENDER_TARGET)
        {
            ensure(device->CreateRenderTargetView((ID3D11Resource*)texture, rtv_desc, &rtvs["default"_h]), >= 0);
        }
        if (bind_flags & D3D11_BIND_UNORDERED_ACCESS)
        {
            ensure(device->CreateUnorderedAccessView((ID3D11Resource*)texture, uav_desc, &uavs["default"_h]), >= 0);
        }
        if (bind_flags & D3D11_BIND_DEPTH_STENCIL)
        {
            ensure(device->CreateDepthStencilView((ID3D11Resource*)texture, dsv_desc, &dsvs["default"_h]), >= 0);
        }
    }

    void Release() noexcept
    {
        // Release texture.
        if (texture)
        {
            ((IUnknown*)texture)->Release();
        }

        // Release SRVs.
        for (auto& srv : srvs)
        {
            if (srv.second)
            {
                srv.second->Release();
            }
        }

        // Release RTVs.
        for (auto& rtv : rtvs)
        {
            if (rtv.second)
            {
                rtv.second->Release();
            }
        }

        // Release UAVs.
        for (auto& uav : uavs)
        {
            if (uav.second)
            {
                uav.second->Release();
            }
        }

        // Release DSVs.
        for (auto& dsv : dsvs)
        {
            if (dsv.second)
            {
                dsv.second->Release();
            }
        }
    }

    void AddRef() noexcept
    {
        // Add reference to texture.
        if (texture)
        {
            ((IUnknown*)texture)->AddRef();
        }

        // Add references to SRVs.
        for (auto& srv : srvs)
        {
            if (srv.second)
            {
                srv.second->AddRef();
            }
        }

        // Add references to RTVs.
        for (auto& rtv : rtvs)
        {
            if (rtv.second)
            {
                rtv.second->AddRef();
            }
        }

        // Add references to UAVs.
        for (auto& uav : uavs)
        {
            if (uav.second)
            {
                uav.second->AddRef();
            }
        }

        // Add references to DSVs.
        for (auto& dsv : dsvs)
        {
            if (dsv.second)
            {
                dsv.second->AddRef();
            }
        }
    }

    ID3D11Device* device = nullptr; // Don't release it! We don't own it.
    void* texture = nullptr;
    std::unordered_map<uint32_t, ID3D11ShaderResourceView*> srvs;
    std::unordered_map<uint32_t, ID3D11RenderTargetView*> rtvs;
    std::unordered_map<uint32_t, ID3D11UnorderedAccessView*> uavs;
    std::unordered_map<uint32_t, ID3D11DepthStencilView*> dsvs;
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