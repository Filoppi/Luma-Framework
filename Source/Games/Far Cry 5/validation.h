#pragma once
#include <d3d11.h>
#include <cmath>
namespace FC5
{
   struct CameraJitter { UINT width = 0, height = 0; float x = 0, y = 0; };
   inline bool ValidateCameraJitter(const float* rows, CameraJitter& result)
   {
      const float width = rows[160], height = rows[161];
      const float x = rows[204], y = rows[205];
      if (!std::isfinite(width) || !std::isfinite(height) || width < 1 || height < 1 ||
         width > 16384 || height > 16384 || width != std::floor(width) || height != std::floor(height) ||
         !std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 1 || std::abs(y) > 1 ||
         rows[46] != -1.f || rows[47] != 0.f) return false;
      double nx = 0, ny = 0, norm = 0;
      for (int i = 0; i < 3; ++i)
      {
         nx += double(rows[80 + i]) * rows[92 + i];
         ny += double(rows[84 + i]) * rows[92 + i];
         norm += double(rows[92 + i]) * rows[92 + i];
      }
      if (!std::isfinite(norm) || norm < 0.9 || norm > 1.1 ||
         !std::isfinite(nx) || !std::isfinite(ny)) return false;
      const double measured_x = nx / norm * width / 2;
      const double measured_y = -ny / norm * height / 2;
      if (std::abs(measured_x - x) > 0.01 || std::abs(measured_y - y) > 0.01)
      {
         // Gameplay uses an off-center projection (forest capture: vertical
         // baseline0.13 NDC), unlike the centered benchmark. The previous
         // rotation-projection contains that baseline WITHOUT temporal jitter.
         // Subtract its independently extracted shift; never send the baseline
         // itself to NGX or merely loosen the pixel-jitter bounds.
         double px = 0, py = 0, pn = 0;
         for (int i = 0; i < 3; ++i)
         {
            px += double(rows[96 + i]) * rows[108 + i];
            py += double(rows[100 + i]) * rows[108 + i];
            pn += double(rows[108 + i]) * rows[108 + i];
         }
         if (!std::isfinite(pn) || pn < 0.9 || pn > 1.1 ||
            !std::isfinite(px) || !std::isfinite(py) ||
            std::abs(measured_x - px / pn * width / 2 - x) > 0.01 ||
            std::abs(measured_y + py / pn * height / 2 - y) > 0.01) return false;
      }
      result = { UINT(width), UINT(height), x, y };
      return true;
   }
   inline bool IsRGBA8View(DXGI_FORMAT resource, DXGI_FORMAT view)
   {
      return view == DXGI_FORMAT_R8G8B8A8_UNORM &&
         (resource == DXGI_FORMAT_R8G8B8A8_TYPELESS || resource == view);
   }
   inline bool IsSingleSurface(const D3D11_TEXTURE2D_DESC& d)
   {
      return d.Width && d.Height && d.MipLevels == 1 && d.ArraySize == 1 &&
         d.SampleDesc.Count == 1 && d.SampleDesc.Quality == 0;
   }
   inline bool IsTemporalColorView(DXGI_FORMAT resource, DXGI_FORMAT view, bool allow_fp16)
   {
      return IsRGBA8View(resource, view) || (allow_fp16 && view == DXGI_FORMAT_R16G16B16A16_FLOAT &&
         (resource == view || resource == DXGI_FORMAT_R16G16B16A16_TYPELESS));
   }
   inline bool CanCopyWhole(const D3D11_TEXTURE2D_DESC& a, const D3D11_TEXTURE2D_DESC& b)
   {
      return IsSingleSurface(a) && IsSingleSurface(b) && a.Width == b.Width &&
         a.Height == b.Height && a.Format == b.Format;
   }
   inline bool IsFullViewport(const D3D11_VIEWPORT& v, const D3D11_TEXTURE2D_DESC& d)
   {
      return v.TopLeftX == 0 && v.TopLeftY == 0 &&
         v.Width == float(d.Width) && v.Height == float(d.Height);
   }
   inline bool IsUpscaleSize(UINT iw, UINT ih, UINT ow, UINT oh)
   {
      // First implementation supports uniform upscaling only (rounding tolerance one pixel).
      return iw && ih && ow > iw && oh > ih && ow <= 16384 && oh <= 16384 &&
         std::abs(double(iw) * oh - double(ih) * ow) <= double(ow + oh);
   }
   enum class TemporalRoute { Invalid, DLAA, Upscale };
   inline TemporalRoute SelectTemporalRoute(UINT iw, UINT ih, UINT ow, UINT oh)
   {
      if (iw && ih && iw <= 16384 && ih <= 16384 && iw == ow && ih == oh)
         return TemporalRoute::DLAA;
      return IsUpscaleSize(iw, ih, ow, oh) ? TemporalRoute::Upscale : TemporalRoute::Invalid;
   }
   inline bool IsSceneCameraViewport(const D3D11_VIEWPORT& v, UINT output_width, UINT output_height)
   {
      // Discovery heuristic only. A surviving CB still needs full jitter/size validation.
      // Reject shadow/auxiliary viewports of a different aspect, without caching sizes
      // from previous frames (which would prevent recovery after resolution changes).
      return output_width && output_height && v.TopLeftX == 0 && v.TopLeftY == 0 &&
         std::isfinite(v.Width) && std::isfinite(v.Height) && v.Width >= 1 && v.Height >= 1 &&
         v.Width <= 16384 && v.Height <= 16384 &&
         std::abs(double(v.Width) * output_height - double(v.Height) * output_width) <=
            double(output_width + output_height);
   }
   inline bool CanRouteUpscale(const D3D11_TEXTURE2D_DESC& source,
      const D3D11_TEXTURE2D_DESC& target, DXGI_FORMAT source_view, DXGI_FORMAT target_view,
      UINT iw, UINT ih, UINT ow, UINT oh, bool same_resource, bool allow_fp16 = false)
   {
      return same_resource && IsUpscaleSize(iw, ih, ow, oh) &&
         IsSingleSurface(source) && IsSingleSurface(target) &&
         source.Width == iw && source.Height == ih && target.Width == ow && target.Height == oh &&
         source_view == target_view && IsTemporalColorView(source.Format, source_view, allow_fp16) &&
         IsTemporalColorView(target.Format, target_view, allow_fp16);
   }
}
