#pragma once

#include "camera.h"

// The game's TAA resolve compute shaders. Bindings (from their reflection names): t0 depth, t1 motion vectors, t3 color, u2 the resolved
// color, u3 the color history (the same values), plus the game's own history buffers. The depth of field variant also resolves a second
// layer (t5/t6 into u4/u5) that SR knows nothing about.
enum class ResolveVariant
{
   None,
   Main,
   DepthOfField,
};

// Which resolve a dispatch is, from its compute shader's hash. It runs for every draw and dispatch.
inline ResolveVariant GetResolveVariant(const ShaderHashesList<OneShaderPerPipeline>& hashes)
{
   constexpr uint32_t kMainHash = 0x060ACA92;
   constexpr uint32_t kDepthOfFieldHash = 0x1ABE3340;
   if (hashes.Contains(kMainHash, reshade::api::shader_stage::compute))
      return ResolveVariant::Main;
   if (hashes.Contains(kDepthOfFieldHash, reshade::api::shader_stage::compute))
      return ResolveVariant::DepthOfField;
   return ResolveVariant::None;
}

// The resolve's resources SR uses, as bound for its dispatch
struct ResolveResources
{
   com_ptr<ID3D11Resource> depth;   // t0
   com_ptr<ID3D11Resource> mvs;     // t1
   com_ptr<ID3D11Resource> color;   // t3
   com_ptr<ID3D11Resource> output;  // u2
   com_ptr<ID3D11Resource> history; // u3
};

// The resource behind a view, or null for no view
inline com_ptr<ID3D11Resource> GetViewResource(ID3D11View* view)
{
   com_ptr<ID3D11Resource> resource;
   if (view)
      view->GetResource(&resource);
   return resource;
}

// The resource bound to a compute shader resource view slot
inline com_ptr<ID3D11Resource> GetComputeShaderResource(ID3D11DeviceContext* context, UINT slot)
{
   com_ptr<ID3D11ShaderResourceView> view;
   context->CSGetShaderResources(slot, 1, &view);
   return GetViewResource(view.get());
}

// The resource bound to a compute unordered access view slot
inline com_ptr<ID3D11Resource> GetComputeUnorderedAccessResource(ID3D11DeviceContext* context, UINT slot)
{
   com_ptr<ID3D11UnorderedAccessView> view;
   context->CSGetUnorderedAccessViews(slot, 1, &view);
   return GetViewResource(view.get());
}

// Read once per resolve, at its dispatch
inline ResolveResources GetResolveResources(ID3D11DeviceContext* context)
{
   ResolveResources resources;
   resources.depth = GetComputeShaderResource(context, 0);
   resources.mvs = GetComputeShaderResource(context, 1);
   resources.color = GetComputeShaderResource(context, 3);
   resources.output = GetComputeUnorderedAccessResource(context, 2);
   resources.history = GetComputeUnorderedAccessResource(context, 3);
   return resources;
}

// Width and height, 0 x 0 for no texture
inline RenderSize GetTextureSize(ID3D11Resource* texture)
{
   uint4 size;
   DXGI_FORMAT format;
   GetResourceInfo(texture, size, format);
   return {size.x, size.y};
}
