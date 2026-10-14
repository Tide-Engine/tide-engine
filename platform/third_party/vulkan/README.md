# Vulkan headers

`vulkan_core.h`, `vk_platform.h` and `vk_video/` are unmodified from the
`vulkan-sdk-1.4.350.0` tag of https://github.com/KhronosGroup/Vulkan-Headers
(`include/vulkan/` and `include/vk_video/`). `LICENSE.md` is that repository's
license note, and `Apache-2.0.txt` the license its headers are under.

Only `platform/src/gpu_vk.c` and the desktop window include them. They declare
Vulkan's types, constants and function pointers: the program asks the system's
Vulkan loader for every function by name, so nothing of Khronos's is linked.
