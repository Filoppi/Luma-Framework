#pragma once

#include <cmath>
#include <cstdint>
#include <type_traits>

struct RenderSize
{
   uint32_t width = 0;
   uint32_t height = 0;

   bool operator==(const RenderSize& other) const
   {
      return width == other.width && height == other.height;
   }
};

// A jitter offset in pixels, y down (SR's convention)
struct Jitter
{
   float x;
   float y;
};

// Frostbite's "viewConstants" constant buffer, up to the projection matrix (the same layout as in Mass Effect Andromeda). It's read
// straight from the game's constant buffer memory, so it stays plain data.
struct Camera
{
   float time[4];
   float screen_size[4]; // Width, height, 1 / width, 1 / height
   float view[4][4];
   float projection[4][4]; // Column vectors (w = -z), reverse-Z with an infinite far plane, plus the jitter

   // Whether it's the main camera's: other views share the layout (shadows, reflections, half resolution effects)
   bool IsMain(RenderSize render_size) const;

   // The projection adds the jitter as projection[0][2] and [1][2], so the NDC offset is their negation (w = -z)
   Jitter GetJitter(RenderSize render_size) const
   {
      return {-projection[0][2] * 0.5f * float(render_size.width), projection[1][2] * 0.5f * float(render_size.height)};
   }

   // With an infinite reverse-Z projection, the depth row's w term is the near plane
   float GetNearPlane() const
   {
      return projection[2][3];
   }

   // In radians, from the projection's y scale (1 / tan(fov / 2))
   float GetVerticalFov() const
   {
      return 2.f * atanf(1.f / projection[1][1]);
   }
};
static_assert(std::is_trivially_copyable_v<Camera> && std::is_standard_layout_v<Camera> && sizeof(Camera) == 10 * 4 * sizeof(float));

// Told apart by its screen size and the shape of its matrices. The screen size is checked first: it rejects most other views.
inline bool Camera::IsMain(RenderSize render_size) const
{
   auto nearly_equal = [](float a, float b, float tolerance)
   { return fabsf(a - b) <= tolerance; };

   if (!nearly_equal(screen_size[0], float(render_size.width), 1.f) || !nearly_equal(screen_size[1], float(render_size.height), 1.f))
      return false;

   // An affine view matrix
   const float* view_row = view[3];
   if (!nearly_equal(view_row[0], 0.f, 1e-3f) || !nearly_equal(view_row[1], 0.f, 1e-3f) || !nearly_equal(view_row[2], 0.f, 1e-3f) || !nearly_equal(view_row[3], 1.f, 1e-3f))
      return false;

   // A reverse-Z perspective projection
   const auto& p = projection;
   return p[0][0] > 0.f && p[1][1] > 0.f && p[2][3] > 0.f && nearly_equal(p[3][2], -1.f, 1e-2f) && nearly_equal(p[3][3], 0.f, 1e-3f);
}
