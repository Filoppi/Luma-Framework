#pragma once

#include <vector>
#include <functional>

// struct cause maybe we want to make some members private in future
// 
// So far std::unoredered_map may be the best solution for this.
// std::unordered_set could be better, but it won't work with std::function.
// std::vector could be too much manual management for the user (developer)?
//
// Signal user that callback was executed?
struct LumaCallbacks final
{
    // Executed after swapchaiin resize and after swapchain creation.
    static inline std::unordered_map<uint32_t, std::function<void()>> on_init_swapchain;
};