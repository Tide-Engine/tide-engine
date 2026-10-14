#pragma once

// Vulkan under the renderer (gpu_vk.c, see gpu.h), as the window that draws
// with it opens it, shows what it drew and closes it. A desktop window is
// GLFW's (desktop/glfw.c), which loads the system's Vulkan and makes the
// surface, and Android's is its activity's (android/activity.c), which loads
// libvulkan.so; nothing here knows the window but by these.

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
    // Makes the window's surface on `instance`: an error where there's no window now
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
// The window's surface went or another came (Android's activity takes its
// window away in the background and gives one back): what was drawn goes to
// the GPU, the old surface goes, and the window's `surface` makes the next.
// Where it can't (there's no window now), nothing draws in the window until
// it's called again, as with a minimized one; the device and everything made
// on it stay.
void tide_gpu_vk_surface_changed(void);
// Whether the device times what it draws (timestamps on its queue): a test's
// question, as tide_platform_gpu_ms says nothing of whether one can come.
bool tide_gpu_vk_timestamps(void);
// For tests: the device is lost once the frame being drawn is shown, as it is
// when a driver starts over. Everything made on it goes, and the next epoch
// makes the device again (tide_gpu.epoch, which the renderer asks each frame).
void tide_gpu_vk_lose(void);
// For tests: the size of the window's images, as the swapchain was last
// made, and how many times it has been made; 0 by 0 with none.
void tide_gpu_vk_swapchain(int *width, int *height, uint32_t *made);
// For tests: the bytes of compiled pipelines the device took from the cache
// file it was given (tide_gpu.pipeline_cache), 0 for none: no file, one that
// isn't whole, or one for another device or driver.
size_t tide_gpu_vk_pipeline_cache_read(void);

extern const tide_gpu tide_gpu_vulkan;
