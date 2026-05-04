#include "..\..\Core\core.hpp"
#include "hooks.hpp"

__int64 __fastcall Hooked_CameraComputeProjectionMatrix(__int64 a1)
{
    auto original_result = g_camera_compute_projection_matrix_hook
    .unsafe_call<__int64>(a1);
    
    return original_result;
}

__int64 __fastcall Hooked_ComputeProjectionMatrix(__int64 a1, float a2, float a3, float a4, float a5, char a6, float a7)
{
    auto original_result = g_compute_projection_matrix_hook
    .unsafe_call<__int64>(a1, a2, a3, a4, a5, a6, a7);
    
    //a2 - vert fov
    //a3 - aspect ratio
    //a4 - near plane
    //45 - far plane
    if (a3 != 1.0 && a4 != 0.0 && a5 != 10000.0)
    {
        int width  = *(int*)(RenderResolution + 0x00);
        int height = *(int*)(RenderResolution + 0x04);
        
        // the matrix is transposed
        *(float*)(a1 + 0x20) = projection_jitters.x * 2.0 / static_cast<float>(width);
        *(float*)(a1 + 0x24) = projection_jitters.y * -2.0 / static_cast<float>(height);
    }
    
#if DEBUG_LOG
    std::stringstream s;
    s << "Computing projection matrix: ";
    s << std::format("Width: {}, Height: {}", *(int*)(RenderResolution + 0x00), *(int*)(RenderResolution + 0x04));
    s << std::format(", Near: {}, Far: {}, Aspect: {}", a4, a5, a3);
    reshade::log::message(reshade::log::level::info, s.str().c_str());
#endif
    
    return original_result;
}