#include "../taa_inputs.h"

#include <cstdio>
#include <cstdlib>
#include <initializer_list>

static void Check(bool passed, const char* name)
{
   if (!passed)
   {
      std::fprintf(stderr, "FAIL: %s\n", name);
      std::exit(1);
   }
}

int main()
{
   com_ptr<ID3D11Device> device;
   com_ptr<ID3D11DeviceContext> context;
   Check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &device, nullptr, &context)),
      "create WARP device");

   // Small native resources exercise the production helper without a game or NGX dependency.
   const UINT width = 64, height = 36;
   auto make_view = [&](DXGI_FORMAT format, UINT mips = 1, UINT slices = 1, UINT first_mip = 0)
   {
      D3D11_TEXTURE2D_DESC desc = {};
      desc.Width = width;
      desc.Height = height;
      desc.MipLevels = mips;
      desc.ArraySize = slices;
      desc.Format = format;
      desc.SampleDesc.Count = 1;
      desc.Usage = D3D11_USAGE_DEFAULT;
      desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
      com_ptr<ID3D11Texture2D> texture;
      Check(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture)), "create texture");
      D3D11_SHADER_RESOURCE_VIEW_DESC view_desc = {};
      view_desc.Format = format;
      view_desc.ViewDimension = slices == 1 ? D3D11_SRV_DIMENSION_TEXTURE2D : D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
      if (slices == 1)
      {
         view_desc.Texture2D.MostDetailedMip = first_mip;
         view_desc.Texture2D.MipLevels = mips - first_mip;
      }
      else
      {
         view_desc.Texture2DArray.MipLevels = mips;
         view_desc.Texture2DArray.ArraySize = slices;
      }
      com_ptr<ID3D11ShaderResourceView> view;
      Check(SUCCEEDED(device->CreateShaderResourceView(texture.get(), &view_desc, &view)), "create SRV");
      return view;
   };

   int cases = 0;
   for (DXGI_FORMAT colour_format : {DXGI_FORMAT_R11G11B10_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT})
   {
      for (DXGI_FORMAT history_format : {DXGI_FORMAT_R11G11B10_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT})
      {
         com_ptr<ID3D11ShaderResourceView> bound[] = {
            make_view(colour_format), make_view(history_format), make_view(DXGI_FORMAT_R16G16B16A16_FLOAT)};
         ID3D11ShaderResourceView* bindings[] = {bound[0].get(), bound[1].get(), bound[2].get()};
         context->PSSetShaderResources(0, 3, bindings);
         com_ptr<ID3D11ShaderResourceView> live[3];
         context->PSGetShaderResources(0, 3, &live[0]);
         com_ptr<ID3D11Resource> colour, motion, expected_colour, expected_motion;
         bound[0]->GetResource(&expected_colour);
         bound[2]->GetResource(&expected_motion);
         Check(MafiaDE::ReadResolveInput(live[0].get(), width, height, false, colour), "native/upgraded colour");
         Check(MafiaDE::ReadResolveInput(live[2].get(), width, height, true, motion), "native MV alongside FP16 history");
         Check(colour == expected_colour && motion == expected_motion && colour != motion, "preserve live resource identities");
         ++cases;
      }
   }

   com_ptr<ID3D11Resource> resource;
   auto colour = make_view(DXGI_FORMAT_R11G11B10_FLOAT);
   auto r32 = make_view(DXGI_FORMAT_R32_FLOAT);
   auto array = make_view(DXGI_FORMAT_R16G16B16A16_FLOAT, 1, 2);
   auto mip = make_view(DXGI_FORMAT_R16G16B16A16_FLOAT, 2, 1, 1);
   auto check_rejected = [&](ID3D11ShaderResourceView* view, UINT test_width, UINT test_height, bool motion)
   {
      const auto refs = colour.ref_count();
      colour->GetResource(&resource);
      Check(!MafiaDE::ReadResolveInput(view, test_width, test_height, motion, resource), "reject unsupported input");
      Check(!resource && colour.ref_count() == refs, "clear stale output without leaking SRV references");
      ++cases;
   };
   check_rejected(nullptr, width, height, true);
   check_rejected(r32.get(), width, height, false);
   check_rejected(colour.get(), width, height, true);
   check_rejected(colour.get(), width + 1, height, false);
   check_rejected(colour.get(), width, height + 1, false);
   check_rejected(array.get(), width, height, true);
   check_rejected(mip.get(), width, height, true);
   context->ClearState();
   std::printf("PASS: %d native D3D11 resolve-input cases\n", cases);
}
