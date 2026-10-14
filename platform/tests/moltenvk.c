// MoltenVK beside this build's programs (cmake/MoltenVK.cmake), as a macOS
// game has it beside itself: it loads, has what a window draws with
// (VK_KHR_surface and VK_EXT_metal_surface), and makes an instance with them.
// VK_KHR_portability_enumeration is the Vulkan loader's: MoltenVK used
// directly, as here, has none, and lists its devices without it; gpu_vk.c
// asks for it only where it's listed. That needs no window and no GPU, so it
// runs where the GPU tests can't (CI's Macs). The devices it finds
// it describes, as the renderer would weigh them (gpu_vk.c), without needing
// any.

#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define VK_NO_PROTOTYPES
#include "vulkan/vulkan_core.h"

static bool listed(const VkExtensionProperties *list, const uint32_t count, const char *name)
{
    for (uint32_t i = 0; i < count; i++) {
        if (strcmp(list[i].extensionName, name) == 0) return true;
    }
    return false;
}

int main(void)
{
    char path[4096];
    uint32_t size = sizeof path;
    char *slash = _NSGetExecutablePath(path, &size) == 0 ? strrchr(path, '/') : NULL;
    if (!slash || (size_t)(slash + 1 - path) + sizeof "libMoltenVK.dylib" > sizeof path) {
        printf("FAIL: no path for this program\n");
        return 1;
    }
    strcpy(slash + 1, "libMoltenVK.dylib");
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        printf("FAIL: MoltenVK isn't beside the program, or doesn't load: %s\n", dlerror());
        return 1;
    }
    const PFN_vkGetInstanceProcAddr proc = (PFN_vkGetInstanceProcAddr)dlsym(library, "vkGetInstanceProcAddr");
    const PFN_vkEnumerateInstanceVersion version_of =
        proc ? (PFN_vkEnumerateInstanceVersion)proc(VK_NULL_HANDLE, "vkEnumerateInstanceVersion") : NULL;
    const PFN_vkEnumerateInstanceExtensionProperties extensions_of =
        proc ? (PFN_vkEnumerateInstanceExtensionProperties)proc(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties") : NULL;
    const PFN_vkCreateInstance create = proc ? (PFN_vkCreateInstance)proc(VK_NULL_HANDLE, "vkCreateInstance") : NULL;
    if (!version_of || !extensions_of || !create) {
        printf("FAIL: MoltenVK lacks a function every Vulkan has\n");
        return 1;
    }
    uint32_t version = 0;
    version_of(&version);
    printf("MoltenVK's Vulkan: %u.%u\n", VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version));
    VkExtensionProperties extensions[128];
    uint32_t count = 128;
    if (extensions_of(NULL, &count, extensions) < 0) count = 0;
    static const char *needed[3] = {VK_KHR_SURFACE_EXTENSION_NAME, "VK_EXT_metal_surface"};
    uint32_t needed_count = 2;
    int failed = 0;
    for (size_t i = 0; i < 2; i++) {
        const bool has = listed(extensions, count, needed[i]);
        printf("%s: the instance has %s\n", has ? "ok" : "FAIL", needed[i]);
        failed += !has;
    }
    const bool portability = listed(extensions, count, "VK_KHR_portability_enumeration");
    printf("portability enumeration: %s\n", portability ? "the instance has it, as through a loader" : "none, as MoltenVK alone has");
    if (portability) needed[needed_count++] = "VK_KHR_portability_enumeration";
    if (version < VK_API_VERSION_1_3) {
        printf("FAIL: the renderer needs Vulkan 1.3\n");
        failed++;
    }
    if (failed) return 1;

    const VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "Tide", 1, "Tide", 1, VK_API_VERSION_1_3};
    const VkInstanceCreateInfo info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL,
                                       portability ? 0x00000001u : 0u, // VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR
                                       &app, 0, NULL, needed_count, needed};
    VkInstance instance = VK_NULL_HANDLE;
    const VkResult made = create(&info, NULL, &instance);
    printf("%s: MoltenVK makes an instance with the window's extensions (%d)\n", made == VK_SUCCESS ? "ok" : "FAIL", (int)made);
    if (made != VK_SUCCESS) return 1;
    const PFN_vkEnumeratePhysicalDevices devices_of = (PFN_vkEnumeratePhysicalDevices)proc(instance, "vkEnumeratePhysicalDevices");
    const PFN_vkGetPhysicalDeviceProperties properties_of =
        (PFN_vkGetPhysicalDeviceProperties)proc(instance, "vkGetPhysicalDeviceProperties");
    const PFN_vkEnumerateDeviceExtensionProperties device_extensions_of =
        (PFN_vkEnumerateDeviceExtensionProperties)proc(instance, "vkEnumerateDeviceExtensionProperties");
    const PFN_vkDestroyInstance destroy = (PFN_vkDestroyInstance)proc(instance, "vkDestroyInstance");
    VkPhysicalDevice devices[8];
    uint32_t device_count = 8;
    if (!devices_of || devices_of(instance, &device_count, devices) < 0) device_count = 0;
    printf("devices: %u\n", device_count);
    for (uint32_t d = 0; d < device_count && properties_of && device_extensions_of; d++) {
        VkPhysicalDeviceProperties properties;
        properties_of(devices[d], &properties);
        static VkExtensionProperties own[512];
        uint32_t own_count = 512;
        if (device_extensions_of(devices[d], NULL, &own_count, own) < 0) own_count = 0;
        printf("  %s: Vulkan %u.%u, %s, %s\n", properties.deviceName, VK_API_VERSION_MAJOR(properties.apiVersion),
               VK_API_VERSION_MINOR(properties.apiVersion),
               listed(own, own_count, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME) ? "push descriptors" : "descriptor sets",
               listed(own, own_count, VK_KHR_SWAPCHAIN_EXTENSION_NAME) ? "a swapchain" : "no swapchain");
    }
    if (destroy) destroy(instance, NULL);
    return 0;
}
