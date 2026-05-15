#pragma once

#include <d3d11_4.h>
#include <unordered_map>
#include <vector>
#include "com_ptr.h"

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