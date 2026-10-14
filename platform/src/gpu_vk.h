#pragma once

// Vulkan under the renderer (gpu_vk.c, see gpu.h), as the window that draws
// with it opens it, shows what it drew and closes it. A desktop window is
// GLFW's (desktop/glfw.c), which loads the system's Vulkan and makes the
// surface; nothing here knows the window but by these.

#include <stdbool.h>
#include <stdint.h>

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES // Every function is found through the loader
#endif
#include "vulkan/vulkan_core.h"

#include "gpu.h"

typedef struct tide_vk_window {
    PFN_vkGetInstanceProcAddr get_instance_proc; // The system loader's, which every other function is found through
    // The instance extensions the window's surfaces need
    const char *const *extensions;
    uint32_t extension_count;
    // Makes the window's surface on `instance`
    VkResult (*surface)(VkInstance instance, VkSurfaceKHR *surface);
    // The window's size in its pixels, as the surface is drawn: 0 by 0 when it's minimized
    void (*pixel_size)(int *width, int *height);
    // At the display's pace (vsync), or as fast as it goes, as a test's hidden window does
    bool paced;
} tide_vk_window;

// Finds a device to draw in the window with: Vulkan 1.3, with push
// descriptors, and a queue that draws and shows in the window. False, having
// said why when `say` is set, where there's none: the window draws with
// OpenGL then. TIDE_VULKAN_VALIDATION in the environment, set to anything but
// 0, has the Khronos validation layer check every call, and stops the program
// at the first thing it finds wrong.
bool tide_gpu_vk_open(const tide_vk_window *window, bool say);
// The frame's work goes to the GPU, and what it drew into the window shows.
void tide_gpu_vk_present(void);
// Waits for the GPU and lets everything go, before the window goes.
void tide_gpu_vk_close(void);
// Whether the device times what it draws (timestamps on its queue): a test's
// question, as tide_platform_gpu_ms says nothing of whether one can come.
bool tide_gpu_vk_timestamps(void);

extern const tide_gpu tide_gpu_vulkan;
