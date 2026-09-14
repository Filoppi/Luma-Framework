#pragma once

namespace TemporalAADepth
{
template <typename T, typename Enum>
struct EnumArray
{
    std::array<T, static_cast<size_t>(Enum::Count)> data;

    T& operator[](Enum e)
    {
        return data[static_cast<size_t>(e)];
    }

    const T& operator[](Enum e) const
    {
        return data[static_cast<size_t>(e)];
    }
};

enum class Texture
{
    DepthHistoryRead,
    DepthHistoryWrite,
    Count,
};

struct DrawData
{
    ID3D11ShaderResourceView* input_mv_srv = nullptr;
    ID3D11ShaderResourceView* input_depth_srv = nullptr;
    int width = 0;
    int height = 0;
    bool use_variance_clip = true;
    float variance_scale = 1.0f;
    float2 velocity_scale = { 1.0f, 1.0f };
    bool has_history = false;
};

struct alignas(16) CBufferData
{
    float4 ScreenInfo = { 0.0, 0.0, 0.0, 0.0 };
    int UseVarianceClipping;
    float VarianceScale;
    float2 VelocityScale = { 0.0, 0.0 };
};

struct Resource
{
    ComPtr<ID3D11Texture2D> tex;
    ComPtr<ID3D11UnorderedAccessView> uav;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11RenderTargetView> rtv;
    float2 dimension = { 0.0, 0.0 };
};

class TemporalAADepthPass
{
public:
    void Draw(ID3D11Device* device, ID3D11DeviceContext* device_context, const DeviceData& device_data, const DrawData& data)
    {
        // Init CBuffer
        CBufferData cb_data;
        InitCBuffer(data, cb_data);

        // Create CB
        if (!cbuffer.get())
        {
            D3D11_BUFFER_DESC bd;
            bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            bd.ByteWidth = sizeof(CBufferData);
            bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            bd.MiscFlags = 0;
            bd.StructureByteStride = 0;
            bd.Usage = D3D11_USAGE_DYNAMIC;
            device->CreateBuffer(&bd, nullptr, cbuffer.put());
        }

        // Create Samplers
        if (!linear_sampler.get())
        {
            D3D11_SAMPLER_DESC sampler_desc = {};

            sampler_desc.Filter = D3D11_FILTER::D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
            sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_MODE::D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_MODE::D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_MODE::D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler_desc.MipLODBias = 0.0f;
            sampler_desc.MaxAnisotropy = 0;
            sampler_desc.ComparisonFunc = D3D11_COMPARISON_FUNC::D3D11_COMPARISON_NEVER;
            sampler_desc.BorderColor[0] = 0.0f;
            sampler_desc.BorderColor[1] = 0.0f;
            sampler_desc.BorderColor[2] = 0.0f;
            sampler_desc.BorderColor[3] = 0.0f;
            sampler_desc.MinLOD = 0.0f;
            sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;

            device->CreateSamplerState(&sampler_desc, linear_sampler.put());
        }
        if (!point_sampler.get())
        {
            D3D11_SAMPLER_DESC sampler_desc = {};

            sampler_desc.Filter = D3D11_FILTER::D3D11_FILTER_MIN_MAG_MIP_POINT;
            sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_MODE::D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_MODE::D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_MODE::D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler_desc.MipLODBias = 0.0f;
            sampler_desc.MaxAnisotropy = 0;
            sampler_desc.ComparisonFunc = D3D11_COMPARISON_FUNC::D3D11_COMPARISON_NEVER;
            sampler_desc.BorderColor[0] = 0.0f;
            sampler_desc.BorderColor[1] = 0.0f;
            sampler_desc.BorderColor[2] = 0.0f;
            sampler_desc.BorderColor[3] = 0.0f;
            sampler_desc.MinLOD = 0.0f;
            sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;

            device->CreateSamplerState(&sampler_desc, point_sampler.put());
        }

        // Create Textures
        SetupTextures(device, data.width, data.height);

        ID3D11SamplerState* cs_samplers[2] = { linear_sampler.get(), point_sampler.get() };
        ID3D11ShaderResourceView* srvs[3] = { data.input_depth_srv, resources[Texture::DepthHistoryRead].srv.get(), data.input_mv_srv };
        ID3D11UnorderedAccessView* uavs[1] = { resources[Texture::DepthHistoryWrite].uav.get() };
        ID3D11Buffer* cbvs[] = { cbuffer.get() };
        const auto shader_hash = data.has_history ? Math::CompileTimeStringHash("Temporal AA Depth With History") : Math::CompileTimeStringHash("Temporal AA Depth Without History");
        ID3D11ComputeShader* cs = device_data.native_compute_shaders.at(shader_hash).get();

        D3D11_MAPPED_SUBRESOURCE mapped_buffer;
        device_context->Map(cbuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer);
        memcpy(mapped_buffer.pData, &cb_data, sizeof(cb_data));
        device_context->Unmap(cbuffer.get(), 0);

        device_context->CSSetShader(cs, nullptr, 0);
        device_context->CSSetSamplers(7, 2, &cs_samplers[0]);
        device_context->CSSetConstantBuffers(0, 1, &cbvs[0]);
        device_context->CSSetShaderResources(0, 3, srvs);
        device_context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
        device_context->Dispatch((data.width + 7) / 8, (data.height + 7) / 8, 1);

        // device_context->CopyResource(resources[Texture::DepthHistoryRead].tex.get(), resources[Texture::DepthHistoryWrite].tex.get());
        std::swap(resources[Texture::DepthHistoryRead], resources[Texture::DepthHistoryWrite]);
    }

    EnumArray<Resource, Texture> resources;

private:
    void InitCBuffer(const DrawData& data, CBufferData& cbuffer)
    {
        const float2 resolution = { static_cast<float>(data.width), static_cast<float>(data.height) };

        cbuffer.ScreenInfo.x = resolution.x;
        cbuffer.ScreenInfo.y = resolution.y;
        cbuffer.ScreenInfo.z = 1.f / resolution.x;
        cbuffer.ScreenInfo.w = 1.f / resolution.y;
        cbuffer.UseVarianceClipping = data.use_variance_clip ? 1 : 0;
        cbuffer.VarianceScale = data.variance_scale;
        cbuffer.VelocityScale = data.velocity_scale;
    }

    void SetupTextures(ID3D11Device* device, uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0)
            return;

        if (resources[Texture::DepthHistoryWrite].tex.get())
        {
            if (cached_width == width && cached_height == height)
                return;
        }

        {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = width;
            desc.Height = height;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R32_TYPELESS;
            desc.SampleDesc.Count = 1;
            desc.SampleDesc.Quality = 0;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            desc.CPUAccessFlags = 0;
            desc.MiscFlags = 0;
            desc.MipLevels = 1;

            {
                auto& r = resources[Texture::DepthHistoryWrite];

                D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
                srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
                srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                srv_desc.Texture2D.MostDetailedMip = 0;
                srv_desc.Texture2D.MipLevels = 1;

                D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
                uav_desc.Format = DXGI_FORMAT_R32_FLOAT;
                uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
                uav_desc.Texture2D.MipSlice = 0;

                device->CreateTexture2D(&desc, nullptr, r.tex.put());
                device->CreateShaderResourceView(r.tex.get(), &srv_desc, r.srv.put());
                device->CreateUnorderedAccessView(r.tex.get(), &uav_desc, r.uav.put());
                r.dimension.x = static_cast<float>(desc.Width);
                r.dimension.y = static_cast<float>(desc.Height);
            }

            {
                auto& r = resources[Texture::DepthHistoryRead];

                D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
                srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
                srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                srv_desc.Texture2D.MostDetailedMip = 0;
                srv_desc.Texture2D.MipLevels = 1;

                D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
                uav_desc.Format = DXGI_FORMAT_R32_FLOAT;
                uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
                uav_desc.Texture2D.MipSlice = 0;

                device->CreateTexture2D(&desc, nullptr, r.tex.put());
                device->CreateShaderResourceView(r.tex.get(), &srv_desc, r.srv.put());
                device->CreateUnorderedAccessView(r.tex.get(), &uav_desc, r.uav.put());
                r.dimension.x = static_cast<float>(desc.Width);
                r.dimension.y = static_cast<float>(desc.Height);
            }

            cached_width = width;
            cached_height = height;
        }
    }

    ComPtr<ID3D11Buffer> cbuffer;
    ComPtr<ID3D11SamplerState> linear_sampler;
    ComPtr<ID3D11SamplerState> point_sampler;

    uint32_t cached_width = 0;
    uint32_t cached_height = 0;
};
} // namespace TemporalAADepth