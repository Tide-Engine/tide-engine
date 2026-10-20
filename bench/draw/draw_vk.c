// versus_draw's workloads by hand in Vulkan 1.3 (see versus_draw.h), with no
// window: an image of the target's size (and a depth image for the cubes),
// dynamic rendering, one pipeline for each workload made up front, one
// descriptor set written once, an instance buffer the GPU reads where the
// host writes it (device-local and host-visible where the device has such
// memory, as a desktop GPU with resizable BAR does) and keeps mapped, one
// command buffer recorded again each frame, and one fence. A frame clears,
// draws once, copies its pixel (1, 1) into memory the host caches, and waits
// for the fence. Frames don't overlap, as Tide's benchmark's don't: each reads
// back what it drew, so one instance buffer is enough.
//
// The shaders are WGSL, made SPIR-V by tidec's translator as the program
// starts, as tide_platform_draw's own are: the same arithmetic as Tide's
// renderer's (platform/src/shaders.h) for these shapes, so the pixels are the
// same. As Tide draws, the viewport is upside down, so that clip space's y is
// up and the image's first row is its top, and the cubes' fronts go clockwise.
// TIDE_VULKAN_VALIDATION (anything but empty or 0) checks every call with
// the validation layer, synchronization too, and stops at what it finds.

#include "versus_draw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VK_NO_PROTOTYPES
#include "vulkan/vulkan_core.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h> // After Vulkan's header: it declares its Vulkan functions where Vulkan's types are

#include "spirv.h" // tidec's WGSL to SPIR-V (compiler/src)

#define VK_GLOBAL(X)                                                                                                   \
    X(vkCreateInstance) X(vkEnumerateInstanceVersion) X(vkEnumerateInstanceLayerProperties)                           \
    X(vkEnumerateInstanceExtensionProperties)

#define VK_INSTANCE(X)                                                                                                 \
    X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties)                               \
    X(vkGetPhysicalDeviceProperties2) X(vkGetPhysicalDeviceFeatures2) X(vkGetPhysicalDeviceQueueFamilyProperties)     \
    X(vkGetPhysicalDeviceMemoryProperties) X(vkCreateDevice) X(vkGetDeviceProcAddr) X(vkEnumerateDeviceExtensionProperties)

#define VK_INSTANCE_OPTIONAL(X) X(vkCreateDebugUtilsMessengerEXT) X(vkDestroyDebugUtilsMessengerEXT)

#define VK_DEVICE(X)                                                                                                   \
    X(vkDestroyDevice) X(vkGetDeviceQueue) X(vkDeviceWaitIdle) X(vkQueueSubmit) X(vkAllocateMemory) X(vkFreeMemory)  \
    X(vkMapMemory) X(vkInvalidateMappedMemoryRanges) X(vkCreateBuffer) X(vkDestroyBuffer)                             \
    X(vkGetBufferMemoryRequirements) X(vkBindBufferMemory) X(vkCreateImage) X(vkDestroyImage)                         \
    X(vkGetImageMemoryRequirements) X(vkBindImageMemory) X(vkCreateImageView) X(vkDestroyImageView)                   \
    X(vkCreateSampler) X(vkDestroySampler) X(vkCreateShaderModule) X(vkDestroyShaderModule)                           \
    X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) X(vkCreatePipelineLayout)                          \
    X(vkDestroyPipelineLayout) X(vkCreateGraphicsPipelines) X(vkDestroyPipeline) X(vkCreateCommandPool)               \
    X(vkDestroyCommandPool) X(vkResetCommandPool) X(vkAllocateCommandBuffers) X(vkBeginCommandBuffer)                 \
    X(vkEndCommandBuffer) X(vkCreateFence) X(vkDestroyFence) X(vkWaitForFences) X(vkResetFences)                      \
    X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets)        \
    X(vkCmdPipelineBarrier) X(vkCmdCopyBufferToImage) X(vkCmdCopyImageToBuffer) X(vkCmdBeginRendering)                \
    X(vkCmdEndRendering) X(vkCmdBindPipeline) X(vkCmdSetViewport) X(vkCmdSetScissor) X(vkCmdBindVertexBuffers)         \
    X(vkCmdBindIndexBuffer) X(vkCmdDraw) X(vkCmdDrawIndexed) X(vkCmdBindDescriptorSets)

#define VK_DECLARE(name) static PFN_##name name;
VK_GLOBAL(VK_DECLARE)
VK_INSTANCE(VK_DECLARE)
VK_INSTANCE_OPTIONAL(VK_DECLARE)
VK_DEVICE(VK_DECLARE)
#undef VK_DECLARE

enum { COLOR_FORMAT = VK_FORMAT_R8G8B8A8_UNORM, DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT };

typedef struct vk_buffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
    uint8_t *mapped;
    VkDeviceSize size;
    bool coherent;
} vk_buffer;

typedef struct vk_image {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
} vk_image;

static struct {
    VkInstance instance;
    VkDebugUtilsMessengerEXT messenger;
    VkPhysicalDevice physical;
    VkPhysicalDeviceMemoryProperties memory;
    VkDevice device;
    VkQueue queue;
    uint32_t family;
    vk_image color, depth, texture;
    VkSampler sampler;
    vk_buffer quad, cube_vertices, cube_indices, instances, uniforms, readback;
    VkDescriptorSetLayout set_layout;
    VkPipelineLayout layout;
    VkDescriptorPool pool;
    VkDescriptorSet set;
    VkPipeline pipelines[VD_KINDS];
    VkCommandPool commands;
    VkCommandBuffer cb;
    VkFence fence;
} vk;

static bool check(const VkResult result, const char *what)
{
    if (result == VK_SUCCESS) return true;
    fprintf(stderr, "versus_draw: Vulkan: %s failed (%d)\n", what, (int)result);
    return false;
}

static bool validating(void)
{
    const char *on = getenv("TIDE_VULKAN_VALIDATION");
    return on && *on && strcmp(on, "0") != 0;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL on_validation(const VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                    const VkDebugUtilsMessageTypeFlagsEXT types,
                                                    const VkDebugUtilsMessengerCallbackDataEXT *data, void *user)
{
    (void)user;
    if (!(types & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) || severity < VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        return VK_FALSE;
    }
    fprintf(stderr, "versus_draw: Vulkan validation (%s): %s\n", data->pMessageIdName ? data->pMessageIdName : "", data->pMessage);
    fflush(stderr);
    abort();
}

static bool listed(const VkExtensionProperties *list, const uint32_t count, const char *name)
{
    for (uint32_t i = 0; i < count; i++) {
        if (strcmp(list[i].extensionName, name) == 0) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Memory

static uint32_t memory_type(const uint32_t bits, const VkMemoryPropertyFlags want)
{
    for (uint32_t i = 0; i < vk.memory.memoryTypeCount; i++) {
        if ((bits & (1u << i)) && (vk.memory.memoryTypes[i].propertyFlags & want) == want) return i;
    }
    return UINT32_MAX;
}

// A buffer the host writes (or, `reading`, reads) and keeps mapped: the
// first of these kinds of memory the device has. Instances in device-local
// memory took a frame of 65,536 rects 0.146 ms here, and 0.18 to 0.20 in the
// host's, which the GPU reads across the bus as it draws.
static bool make_buffer(vk_buffer *b, const VkDeviceSize size, const VkBufferUsageFlags usage, const bool reading,
                        const void *data)
{
    *b = (vk_buffer){.size = size};
    const VkBufferCreateInfo info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, size, usage, VK_SHARING_MODE_EXCLUSIVE, 0, NULL};
    if (!check(vkCreateBuffer(vk.device, &info, NULL, &b->buffer), "vkCreateBuffer")) return false;
    VkMemoryRequirements needs;
    vkGetBufferMemoryRequirements(vk.device, b->buffer, &needs);
    enum { VISIBLE = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, COHERENT = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
           CACHED = VK_MEMORY_PROPERTY_HOST_CACHED_BIT, LOCAL = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT };
    static const VkMemoryPropertyFlags written[] = {LOCAL | VISIBLE | COHERENT, VISIBLE | COHERENT, VISIBLE};
    static const VkMemoryPropertyFlags read[] = {VISIBLE | CACHED | COHERENT, VISIBLE | CACHED, VISIBLE | COHERENT, VISIBLE};
    const VkMemoryPropertyFlags *wants = reading ? read : written;
    const size_t count = reading ? sizeof read / sizeof read[0] : sizeof written / sizeof written[0];
    uint32_t type = UINT32_MAX;
    for (size_t i = 0; i < count && type == UINT32_MAX; i++) type = memory_type(needs.memoryTypeBits, wants[i]);
    if (type == UINT32_MAX) return false;
    b->coherent = (vk.memory.memoryTypes[type].propertyFlags & COHERENT) != 0;
    const VkMemoryAllocateInfo allocation = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, needs.size, type};
    void *mapped = NULL;
    if (!check(vkAllocateMemory(vk.device, &allocation, NULL, &b->memory), "vkAllocateMemory")
        || !check(vkBindBufferMemory(vk.device, b->buffer, b->memory, 0), "vkBindBufferMemory")
        || !check(vkMapMemory(vk.device, b->memory, 0, VK_WHOLE_SIZE, 0, &mapped), "vkMapMemory")) {
        return false;
    }
    b->mapped = mapped;
    if (data) memcpy(b->mapped, data, (size_t)size); // Static meshes and uniforms: written once, before any frame reads them
    return true;
}

static void free_buffer(vk_buffer *b)
{
    if (b->buffer) vkDestroyBuffer(vk.device, b->buffer, NULL);
    if (b->memory) vkFreeMemory(vk.device, b->memory, NULL); // Unmapped as it's freed
    *b = (vk_buffer){0};
}

static bool make_image(vk_image *image, const VkFormat format, const uint32_t width, const uint32_t height,
                       const VkImageUsageFlags usage, const VkImageAspectFlags aspect)
{
    const VkImageCreateInfo info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = format,
                                    .extent = {width, height, 1}, .mipLevels = 1, .arrayLayers = 1,
                                    .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = usage,
                                    .sharingMode = VK_SHARING_MODE_EXCLUSIVE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    if (!check(vkCreateImage(vk.device, &info, NULL, &image->image), "vkCreateImage")) return false;
    VkMemoryRequirements needs;
    vkGetImageMemoryRequirements(vk.device, image->image, &needs);
    uint32_t type = memory_type(needs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == UINT32_MAX) type = memory_type(needs.memoryTypeBits, 0);
    const VkMemoryAllocateInfo allocation = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, needs.size, type};
    if (!check(vkAllocateMemory(vk.device, &allocation, NULL, &image->memory), "vkAllocateMemory")
        || !check(vkBindImageMemory(vk.device, image->image, image->memory, 0), "vkBindImageMemory")) {
        return false;
    }
    const VkImageViewCreateInfo view = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, image->image, VK_IMAGE_VIEW_TYPE_2D, format,
                                        {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                         VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY},
                                        {aspect, 0, 1, 0, 1}};
    return check(vkCreateImageView(vk.device, &view, NULL, &image->view), "vkCreateImageView");
}

static void free_image(vk_image *image)
{
    if (image->view) vkDestroyImageView(vk.device, image->view, NULL);
    if (image->image) vkDestroyImage(vk.device, image->image, NULL);
    if (image->memory) vkFreeMemory(vk.device, image->memory, NULL);
    *image = (vk_image){0};
}

// ---------------------------------------------------------------------------
// Commands

static void image_barrier(const VkImage image, const VkImageAspectFlags aspect, const VkImageLayout from, const VkImageLayout to,
                          const VkPipelineStageFlags after, const VkAccessFlags written, const VkPipelineStageFlags before,
                          const VkAccessFlags then)
{
    const VkImageMemoryBarrier b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, NULL, written, then, from, to,
                                    VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image, {aspect, 0, 1, 0, 1}};
    vkCmdPipelineBarrier(vk.cb, after, before, 0, 0, NULL, 0, NULL, 1, &b);
}

// What the host reads, once the copy into it is done.
static void host_barrier(void)
{
    const VkMemoryBarrier b = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, NULL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(vk.cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &b, 0, NULL, 0, NULL);
}

static bool begin_commands(void)
{
    if (!check(vkResetCommandPool(vk.device, vk.commands, 0), "vkResetCommandPool")) return false;
    const VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL,
                                            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL};
    return check(vkBeginCommandBuffer(vk.cb, &begin), "vkBeginCommandBuffer");
}

// The commands go, and are waited for.
static bool submit_and_wait(void)
{
    if (!check(vkEndCommandBuffer(vk.cb), "vkEndCommandBuffer")) return false;
    const VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 0, NULL, NULL, 1, &vk.cb, 0, NULL};
    return check(vkQueueSubmit(vk.queue, 1, &submit, vk.fence), "vkQueueSubmit")
        && check(vkWaitForFences(vk.device, 1, &vk.fence, VK_TRUE, UINT64_MAX), "vkWaitForFences")
        && check(vkResetFences(vk.device, 1, &vk.fence), "vkResetFences");
}

// ---------------------------------------------------------------------------
// Shaders: WGSL, as tidec's translator takes it (entry points `vertex` and
// `fragment`, resources in group 0). Clip space is OpenGL's, as Tide's is:
// the cubes' depth goes from -1 to 1 to 0 to 1 here.

#define UNIFORMS                                                                                                       \
    "struct Uniforms { camera: mat4x4f, screen: vec4f }\n"                                                             \
    "@group(0) @binding(0) var<uniform> u: Uniforms;\n"

#define PLACED "(shape.xy + local) * u.screen.xy + u.screen.zw, 0.0, 1.0)"

static const char rect_wgsl[] = UNIFORMS
    "struct Corner { @builtin(position) position: vec4f, @location(0) tint: vec4f }\n"
    "@vertex fn vertex(@location(0) corner: vec2f, @location(1) shape: vec4f, @location(2) color: vec4f) -> Corner {\n"
    "    let local = (corner * 2.0 - 1.0) * abs(shape.zw);\n"
    "    var out: Corner;\n"
    "    out.tint = color;\n"
    "    out.position = vec4f(" PLACED ";\n"
    "    return out;\n"
    "}\n"
    "@fragment fn fragment(in: Corner) -> @location(0) vec4f { return vec4f(in.tint.rgb * in.tint.a, in.tint.a); }\n";

static const char circle_wgsl[] = UNIFORMS
    "struct Corner { @builtin(position) position: vec4f, @location(0) local: vec2f, @location(1) radius: f32,\n"
    "                @location(2) tint: vec4f }\n"
    "@vertex fn vertex(@location(0) corner: vec2f, @location(1) shape: vec4f, @location(2) color: vec4f) -> Corner {\n"
    "    let extent = abs(shape.zw);\n"
    "    let local = (corner * 2.0 - 1.0) * (extent + 1.0);\n" // Room for its smooth edge
    "    var out: Corner;\n"
    "    out.local = local;\n"
    "    out.radius = extent.x;\n"
    "    out.tint = color;\n"
    "    out.position = vec4f(" PLACED ";\n"
    "    return out;\n"
    "}\n"
    "@fragment fn fragment(in: Corner) -> @location(0) vec4f {\n"
    "    let cover = clamp(in.radius - length(in.local) + 0.5, 0.0, 1.0);\n"
    "    if (cover <= 0.0) { discard; }\n"
    "    return vec4f(in.tint.rgb * (in.tint.a * cover), in.tint.a * cover);\n"
    "}\n";

static const char quad_wgsl[] = UNIFORMS
    "@group(0) @binding(1) var pixels: texture_2d<f32>;\n"
    "@group(0) @binding(2) var how: sampler;\n"
    "struct Corner { @builtin(position) position: vec4f, @location(0) uv: vec2f, @location(1) tint: vec4f }\n"
    "@vertex fn vertex(@location(0) corner: vec2f, @location(1) shape: vec4f, @location(2) color: vec4f) -> Corner {\n"
    "    let local = (corner * 2.0 - 1.0) * abs(shape.zw);\n"
    "    var out: Corner;\n"
    "    out.uv = corner;\n"
    "    out.tint = color;\n"
    "    out.position = vec4f(" PLACED ";\n"
    "    return out;\n"
    "}\n"
    "@fragment fn fragment(in: Corner) -> @location(0) vec4f {\n"
    "    return textureSample(pixels, how, in.uv) * vec4f(in.tint.rgb * in.tint.a, in.tint.a);\n"
    "}\n";

static const char round_wgsl[] = UNIFORMS
    "struct Corner { @builtin(position) position: vec4f, @location(0) local: vec2f, @location(1) extent: vec2f,\n"
    "                @location(2) radius: f32, @location(3) tint: vec4f }\n"
    "@vertex fn vertex(@location(0) corner: vec2f, @location(1) shape: vec4f, @location(2) color: vec4f,\n"
    "                  @location(3) radius: f32) -> Corner {\n"
    "    let extent = abs(shape.zw);\n"
    "    let local = (corner * 2.0 - 1.0) * (extent + 1.0);\n"
    "    var out: Corner;\n"
    "    out.local = local;\n"
    "    out.extent = extent;\n"
    "    out.radius = radius;\n"
    "    out.tint = color;\n"
    "    out.position = vec4f(" PLACED ";\n"
    "    return out;\n"
    "}\n"
    "@fragment fn fragment(in: Corner) -> @location(0) vec4f {\n"
    "    let q = abs(in.local) - in.extent + in.radius;\n"
    "    let d = length(max(q, vec2f(0.0))) + min(max(q.x, q.y), 0.0) - in.radius;\n"
    "    let cover = clamp(0.5 - d, 0.0, 1.0);\n"
    "    if (cover <= 0.0) { discard; }\n"
    "    return vec4f(in.tint.rgb * in.tint.a, in.tint.a) * cover;\n"
    "}\n";

#define CUBE_FRAGMENT                                                                                                  \
    "@fragment fn fragment(in: Corner) -> @location(0) vec4f { return vec4f(in.tint.rgb * in.tint.a, in.tint.a); }\n"

static const char cube_wgsl[] = UNIFORMS
    "struct Corner { @builtin(position) position: vec4f, @location(0) tint: vec4f }\n"
    "@vertex fn vertex(@location(0) position: vec3f, @location(1) color: vec4f, @location(2) place: vec3f) -> Corner {\n"
    "    let p = u.camera * vec4f(position + place, 1.0);\n"
    "    var out: Corner;\n"
    "    out.tint = color;\n"
    "    out.position = vec4f(p.xy, (p.z + p.w) * 0.5, p.w);\n"
    "    return out;\n"
    "}\n" CUBE_FRAGMENT;

static const char cube_matrix_wgsl[] = UNIFORMS
    "struct Corner { @builtin(position) position: vec4f, @location(0) tint: vec4f }\n"
    "@vertex fn vertex(@location(0) position: vec3f, @location(1) color: vec4f, @location(2) m0: vec4f, @location(3) m1: vec4f,\n"
    "                  @location(4) m2: vec4f, @location(5) m3: vec4f) -> Corner {\n"
    "    let p = u.camera * (mat4x4f(m0, m1, m2, m3) * vec4f(position, 1.0));\n"
    "    var out: Corner;\n"
    "    out.tint = color;\n"
    "    out.position = vec4f(p.xy, (p.z + p.w) * 0.5, p.w);\n"
    "    return out;\n"
    "}\n" CUBE_FRAGMENT;

// A cube's corner as the GPU reads it.
typedef struct cube_vertex {
    float position[3];
    uint8_t color[4];
} cube_vertex;

static VkPipeline make_pipeline(const vd_kind kind)
{
    static const char *const sources[VD_KINDS] = {
        [VD_RECTS] = rect_wgsl, [VD_CIRCLES] = circle_wgsl, [VD_QUADS] = quad_wgsl, [VD_CUBES] = cube_wgsl,
        [VD_ROUND_RECTS] = round_wgsl, [VD_CUBE_MATRICES] = cube_matrix_wgsl};
    char error[512] = "";
    size_t words = 0;
    uint32_t *code = tide_wgsl_to_spirv(sources[kind], &words, error, sizeof error);
    if (!code) {
        fprintf(stderr, "versus_draw: a shader isn't WGSL the translator takes: %s\n", error);
        return VK_NULL_HANDLE;
    }
    VkShaderModule module = VK_NULL_HANDLE;
    const VkShaderModuleCreateInfo info = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, words * sizeof(uint32_t), code};
    const bool made = check(vkCreateShaderModule(vk.device, &info, NULL, &module), "vkCreateShaderModule");
    free(code);
    if (!made) return VK_NULL_HANDLE;
    const bool deep = kind == VD_CUBES || kind == VD_CUBE_MATRICES;
    const VkPipelineShaderStageCreateInfo stages[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, module, "vertex", NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, module, "fragment", NULL},
    };
    const uint32_t stride = (uint32_t)vd_instance_size(kind);
    const VkVertexInputBindingDescription bindings[2] = {
        {0, deep ? (uint32_t)sizeof(cube_vertex) : 2u * (uint32_t)sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX},
        {1, stride, VK_VERTEX_INPUT_RATE_INSTANCE},
    };
    VkVertexInputAttributeDescription attributes[6];
    uint32_t count = 0;
    if (deep) {
        attributes[count++] = (VkVertexInputAttributeDescription){0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
        attributes[count++] = (VkVertexInputAttributeDescription){1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(cube_vertex, color)};
        if (kind == VD_CUBES) attributes[count++] = (VkVertexInputAttributeDescription){2, 1, VK_FORMAT_R32G32B32_SFLOAT, 0};
        else for (uint32_t c = 0; c < 4; c++) attributes[count++] = (VkVertexInputAttributeDescription){2 + c, 1, VK_FORMAT_R32G32B32A32_SFLOAT, c * 16u};
    } else {
        attributes[count++] = (VkVertexInputAttributeDescription){0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
        attributes[count++] = (VkVertexInputAttributeDescription){1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0};
        if (kind == VD_ROUND_RECTS) {
            attributes[count++] = (VkVertexInputAttributeDescription){2, 1, VK_FORMAT_R8G8B8A8_UNORM, offsetof(vd_round, rgba)};
            attributes[count++] = (VkVertexInputAttributeDescription){3, 1, VK_FORMAT_R32_SFLOAT, offsetof(vd_round, radius)};
        } else {
            attributes[count++] = (VkVertexInputAttributeDescription){2, 1, VK_FORMAT_R8G8B8A8_UNORM, offsetof(vd_shape, rgba)};
        }
    }
    const VkPipelineVertexInputStateCreateInfo input = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, NULL, 0, 2, bindings,
                                                        count, attributes};
    const VkPipelineInputAssemblyStateCreateInfo assembly = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, NULL, 0,
                                                             VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE};
    const VkPipelineViewportStateCreateInfo viewport = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, NULL, 0, 1, NULL, 1, NULL};
    const VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = deep ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE, .frontFace = VK_FRONT_FACE_CLOCKWISE, .lineWidth = 1.0f};
    const VkPipelineMultisampleStateCreateInfo samples = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                                          .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    const VkPipelineDepthStencilStateCreateInfo depth = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
                                                         .depthTestEnable = deep, .depthWriteEnable = deep,
                                                         .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL};
    // Colors go out times their alpha, over what's there; the cubes are opaque
    const VkPipelineColorBlendAttachmentState over = {
        .blendEnable = !deep, .srcColorBlendFactor = VK_BLEND_FACTOR_ONE, .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD, .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    const VkPipelineColorBlendStateCreateInfo blend = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                                       .attachmentCount = 1, .pAttachments = &over};
    const VkDynamicState dynamic_states[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamic = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, NULL, 0, 2, dynamic_states};
    const VkFormat color_format = COLOR_FORMAT;
    const VkPipelineRenderingCreateInfo rendering = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, .colorAttachmentCount = 1,
                                                     .pColorAttachmentFormats = &color_format,
                                                     .depthAttachmentFormat = deep ? (VkFormat)DEPTH_FORMAT : VK_FORMAT_UNDEFINED};
    const VkGraphicsPipelineCreateInfo pipeline = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .pNext = &rendering, .stageCount = 2, .pStages = stages,
        .pVertexInputState = &input, .pInputAssemblyState = &assembly, .pViewportState = &viewport,
        .pRasterizationState = &raster, .pMultisampleState = &samples, .pDepthStencilState = &depth,
        .pColorBlendState = &blend, .pDynamicState = &dynamic, .layout = vk.layout};
    VkPipeline made_pipeline = VK_NULL_HANDLE;
    check(vkCreateGraphicsPipelines(vk.device, VK_NULL_HANDLE, 1, &pipeline, NULL, &made_pipeline), "vkCreateGraphicsPipelines");
    vkDestroyShaderModule(vk.device, module, NULL);
    return made_pipeline;
}

// ---------------------------------------------------------------------------

static bool open_instance(void)
{
    if (!glfwVulkanSupported()) return false;
#define VK_LOAD(name) name = (PFN_##name)glfwGetInstanceProcAddress(VK_NULL_HANDLE, #name);
    VK_GLOBAL(VK_LOAD)
#undef VK_LOAD
    uint32_t version = VK_API_VERSION_1_0;
    if (!vkCreateInstance || !vkEnumerateInstanceVersion || vkEnumerateInstanceVersion(&version) != VK_SUCCESS
        || version < VK_API_VERSION_1_3) {
        return false;
    }
    const bool validate = validating();
    const char *extensions[4];
    uint32_t extension_count = 0;
    VkExtensionProperties available[256];
    uint32_t available_count = 256;
    if (vkEnumerateInstanceExtensionProperties(NULL, &available_count, available) < 0) available_count = 0;
    VkInstanceCreateFlags flags = 0;
    if (listed(available, available_count, "VK_KHR_portability_enumeration")) { // MoltenVK's devices are listed only so
        extensions[extension_count++] = "VK_KHR_portability_enumeration";
        flags |= 0x00000001; // VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR
    }
    const VkValidationFeatureEnableEXT sync = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    const VkValidationFeaturesEXT features = {VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT, NULL, 1, &sync, 0, NULL};
    bool sync_checked = false;
    if (validate) {
        VkLayerProperties layers[64];
        uint32_t count = 64;
        if (vkEnumerateInstanceLayerProperties(&count, layers) < 0) count = 0;
        bool found = false;
        for (uint32_t i = 0; i < count; i++) found |= strcmp(layers[i].layerName, "VK_LAYER_KHRONOS_validation") == 0;
        if (!found) {
            fprintf(stderr, "versus_draw: TIDE_VULKAN_VALIDATION is set, and Vulkan has no VK_LAYER_KHRONOS_validation\n");
            exit(1);
        }
        extensions[extension_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        VkExtensionProperties layer[64];
        uint32_t layer_count = 64;
        if (vkEnumerateInstanceExtensionProperties("VK_LAYER_KHRONOS_validation", &layer_count, layer) < 0) layer_count = 0;
        sync_checked = listed(layer, layer_count, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
        if (sync_checked) extensions[extension_count++] = VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME;
    }
    const VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "versus_draw", 1, NULL, 0, VK_API_VERSION_1_3};
    const char *const layer = "VK_LAYER_KHRONOS_validation";
    const VkInstanceCreateInfo info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, sync_checked ? &features : NULL, flags, &app,
                                       validate ? 1u : 0u, &layer, extension_count, extensions};
    if (!check(vkCreateInstance(&info, NULL, &vk.instance), "vkCreateInstance")) return false;
    bool found = true;
#define VK_LOAD(name)                                                                                                  \
    name = (PFN_##name)glfwGetInstanceProcAddress(vk.instance, #name);                                                \
    if (!name) found = false;
    VK_INSTANCE(VK_LOAD)
#undef VK_LOAD
#define VK_LOAD(name) name = (PFN_##name)glfwGetInstanceProcAddress(vk.instance, #name);
    VK_INSTANCE_OPTIONAL(VK_LOAD)
#undef VK_LOAD
    if (!found) return false;
    if (validate && vkCreateDebugUtilsMessengerEXT) {
        const VkDebugUtilsMessengerCreateInfoEXT messenger = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT, .pfnUserCallback = on_validation};
        vkCreateDebugUtilsMessengerEXT(vk.instance, &messenger, NULL, &vk.messenger);
    }
    return true;
}

// The device Tide's renderer would pick: Vulkan 1.3 with dynamic rendering, a
// discrete GPU before an integrated one, and its first queue that draws.
static bool open_device(char *name, const size_t name_size)
{
    VkPhysicalDevice devices[16];
    uint32_t count = 16;
    if (vkEnumeratePhysicalDevices(vk.instance, &count, devices) < 0 || !count) return false;
    int best = -1, best_score = 0;
    for (uint32_t d = 0; d < count; d++) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(devices[d], &properties);
        if (properties.apiVersion < VK_API_VERSION_1_3) continue;
        VkPhysicalDeviceVulkan13Features v13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 features = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &v13};
        vkGetPhysicalDeviceFeatures2(devices[d], &features);
        if (!v13.dynamicRendering) continue;
        const int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 4
                        : properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 3
                        : properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU ? 2 : 1;
        if (score > best_score) best = (int)d, best_score = score;
    }
    if (best < 0) return false;
    vk.physical = devices[best];
    VkQueueFamilyProperties families[32];
    uint32_t family_count = 32;
    vkGetPhysicalDeviceQueueFamilyProperties(vk.physical, &family_count, families);
    vk.family = UINT32_MAX;
    for (uint32_t f = 0; f < family_count && vk.family == UINT32_MAX; f++) {
        if (families[f].queueFlags & VK_QUEUE_GRAPHICS_BIT) vk.family = f;
    }
    if (vk.family == UINT32_MAX) return false;
    vkGetPhysicalDeviceMemoryProperties(vk.physical, &vk.memory);
    VkPhysicalDeviceVulkan12Properties driver = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES};
    VkPhysicalDeviceProperties2 properties = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &driver};
    vkGetPhysicalDeviceProperties2(vk.physical, &properties);
    snprintf(name, name_size, "%s, %s %s", properties.properties.deviceName, driver.driverName, driver.driverInfo);
    VkPhysicalDeviceVulkan13Features want13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .dynamicRendering = VK_TRUE};
    const VkPhysicalDeviceFeatures2 want = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &want13};
    VkExtensionProperties extensions[512];
    uint32_t extension_count = 512;
    if (vkEnumerateDeviceExtensionProperties(vk.physical, NULL, &extension_count, extensions) < 0) extension_count = 0;
    const char *const subset = "VK_KHR_portability_subset"; // MoltenVK's, which has to be asked for where it's listed
    const uint32_t enabled = listed(extensions, extension_count, subset) ? 1u : 0u;
    const float priority = 1.0f;
    const VkDeviceQueueCreateInfo queue = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, vk.family, 1, &priority};
    const VkDeviceCreateInfo info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &want, 0, 1, &queue, 0, NULL, enabled, &subset, NULL};
    if (!check(vkCreateDevice(vk.physical, &info, NULL, &vk.device), "vkCreateDevice")) return false;
    bool found = true;
#define VK_LOAD(name)                                                                                                  \
    name = (PFN_##name)vkGetDeviceProcAddr(vk.device, #name);                                                         \
    if (!name) found = false;
    VK_DEVICE(VK_LOAD)
#undef VK_LOAD
    if (!found) return false;
    vkGetDeviceQueue(vk.device, vk.family, 0, &vk.queue);
    return true;
}

static void vk_close(void);

static bool vk_open(const float camera[16], const uint8_t pixels[2 * 2 * 4], char *device, const size_t device_size)
{
    memset(&vk, 0, sizeof vk);
    if (!open_instance() || !open_device(device, device_size)) {
        vk_close();
        return false;
    }
    const VkCommandPoolCreateInfo pool = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, vk.family};
    const VkFenceCreateInfo fence = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, NULL, 0};
    bool ok = check(vkCreateCommandPool(vk.device, &pool, NULL, &vk.commands), "vkCreateCommandPool")
           && check(vkCreateFence(vk.device, &fence, NULL, &vk.fence), "vkCreateFence");
    if (ok) {
        const VkCommandBufferAllocateInfo info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, vk.commands,
                                                  VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
        ok = check(vkAllocateCommandBuffers(vk.device, &info, &vk.cb), "vkAllocateCommandBuffers");
    }
    // The target, its depth, and the quads' texture
    ok = ok
      && make_image(&vk.color, COLOR_FORMAT, VD_WIDTH, VD_HEIGHT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT)
      && make_image(&vk.depth, DEPTH_FORMAT, VD_WIDTH, VD_HEIGHT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT)
      && make_image(&vk.texture, COLOR_FORMAT, 2, 2, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT);
    const VkSamplerCreateInfo nearest = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .magFilter = VK_FILTER_NEAREST,
                                         .minFilter = VK_FILTER_NEAREST, .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                         .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                         .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                         .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .maxLod = 0.0f};
    ok = ok && check(vkCreateSampler(vk.device, &nearest, NULL, &vk.sampler), "vkCreateSampler");

    // Buffers: the static meshes, the uniforms, the instances, and what's read back
    static const float corners[12] = {0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1};
    static const cube_vertex cube[8] = {
        {{-0.5f, -0.5f, -0.5f}, {255, 0, 0, 255}},  {{0.5f, -0.5f, -0.5f}, {0, 255, 0, 255}},
        {{0.5f, 0.5f, -0.5f}, {0, 0, 255, 255}},    {{-0.5f, 0.5f, -0.5f}, {255, 255, 0, 255}},
        {{-0.5f, -0.5f, 0.5f}, {255, 0, 255, 255}}, {{0.5f, -0.5f, 0.5f}, {0, 255, 255, 255}},
        {{0.5f, 0.5f, 0.5f}, {255, 255, 255, 255}}, {{-0.5f, 0.5f, 0.5f}, {128, 128, 128, 255}},
    };
    static const uint16_t triangles[36] = {0, 3, 2, 0, 2, 1, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
                                           3, 7, 6, 3, 6, 2, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5};
    float uniforms[20];
    memcpy(uniforms, camera, 16 * sizeof(float));
    // Logical pixels from the top left to clip space, y up: the viewport turns it the image's way
    const float screen[4] = {2.0f / VD_WIDTH, -2.0f / VD_HEIGHT, -1.0f, 1.0f};
    memcpy(uniforms + 16, screen, sizeof screen);
    ok = ok && make_buffer(&vk.quad, sizeof corners, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, false, corners)
      && make_buffer(&vk.cube_vertices, sizeof cube, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, false, cube)
      && make_buffer(&vk.cube_indices, sizeof triangles, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, false, triangles)
      && make_buffer(&vk.uniforms, sizeof uniforms, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, false, uniforms)
      && make_buffer(&vk.instances, (VkDeviceSize)VD_COUNT * vd_instance_size(VD_CUBE_MATRICES), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     false, NULL)
      && make_buffer(&vk.readback, (VkDeviceSize)VD_WIDTH * VD_HEIGHT * 4u, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, NULL);

    // The texture's pixels, through a buffer of their own
    if (ok) {
        vk_buffer staging;
        ok = make_buffer(&staging, 2 * 2 * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, false, pixels) && begin_commands();
        if (ok) {
            image_barrier(vk.texture.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
            const VkBufferImageCopy copy = {0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {2, 2, 1}};
            vkCmdCopyBufferToImage(vk.cb, staging.buffer, vk.texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            image_barrier(vk.texture.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
            ok = submit_and_wait();
        }
        free_buffer(&staging);
    }

    // One layout and one set for every pipeline: the uniforms, the texture and its sampler
    if (ok) {
        const VkDescriptorSetLayoutBinding bindings[3] = {
            {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, NULL},
            {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL},
            {2, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL},
        };
        const VkDescriptorSetLayoutCreateInfo layout = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, 3, bindings};
        const VkDescriptorPoolSize sizes[3] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1},
                                               {VK_DESCRIPTOR_TYPE_SAMPLER, 1}};
        const VkDescriptorPoolCreateInfo sets = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, 1, 3, sizes};
        ok = check(vkCreateDescriptorSetLayout(vk.device, &layout, NULL, &vk.set_layout), "vkCreateDescriptorSetLayout")
          && check(vkCreateDescriptorPool(vk.device, &sets, NULL, &vk.pool), "vkCreateDescriptorPool");
        if (ok) {
            const VkPipelineLayoutCreateInfo pipeline_layout = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 1, &vk.set_layout,
                                                                0, NULL};
            const VkDescriptorSetAllocateInfo allocation = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, vk.pool, 1, &vk.set_layout};
            ok = check(vkCreatePipelineLayout(vk.device, &pipeline_layout, NULL, &vk.layout), "vkCreatePipelineLayout")
              && check(vkAllocateDescriptorSets(vk.device, &allocation, &vk.set), "vkAllocateDescriptorSets");
        }
        if (ok) {
            const VkDescriptorBufferInfo block = {vk.uniforms.buffer, 0, VK_WHOLE_SIZE};
            const VkDescriptorImageInfo image = {VK_NULL_HANDLE, vk.texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            const VkDescriptorImageInfo sampler = {vk.sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
            const VkWriteDescriptorSet writes[3] = {
                {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, vk.set, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, NULL, &block, NULL},
                {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, vk.set, 1, 0, 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &image, NULL, NULL},
                {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, vk.set, 2, 0, 1, VK_DESCRIPTOR_TYPE_SAMPLER, &sampler, NULL, NULL},
            };
            vkUpdateDescriptorSets(vk.device, 3, writes, 0, NULL);
        }
    }
    for (int k = VD_RECTS; ok && k < VD_KINDS; k++) ok = (vk.pipelines[k] = make_pipeline((vd_kind)k)) != VK_NULL_HANDLE;
    if (!ok) vk_close();
    return ok;
}

static void *vk_instances(const vd_kind kind, const size_t bytes)
{
    (void)kind, (void)bytes;
    return vk.instances.mapped;
}

static void vk_written(const vd_kind kind, const size_t bytes)
{
    (void)kind, (void)bytes; // Coherent memory: nothing to flush. Non-coherent would need vkFlushMappedMemoryRanges here
}

static uint32_t vk_render(const vd_kind kind, const uint32_t count)
{
    const bool deep = kind == VD_CUBES || kind == VD_CUBE_MATRICES;
    if (!begin_commands()) abort();
    // The last frame's copy read the target, and its depth was drawn in: both start again undefined, to be cleared
    image_barrier(vk.color.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    if (deep) {
        const VkPipelineStageFlags tests = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        image_barrier(vk.depth.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                      tests, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, tests,
                      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    }
    const VkRenderingAttachmentInfo color = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = vk.color.view,
                                             .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                             .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                             .clearValue = {.color = {{0.0f, 0.0f, 0.0f, 1.0f}}}};
    const VkRenderingAttachmentInfo depth = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = vk.depth.view,
                                             .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                                             .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                             .clearValue = {.depthStencil = {1.0f, 0}}};
    const VkRenderingInfo rendering = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = {{0, 0}, {VD_WIDTH, VD_HEIGHT}},
                                       .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &color,
                                       .pDepthAttachment = deep ? &depth : NULL};
    vkCmdBeginRendering(vk.cb, &rendering);
    if (count) {
        const VkViewport viewport = {0.0f, (float)VD_HEIGHT, (float)VD_WIDTH, -(float)VD_HEIGHT, 0.0f, 1.0f}; // Upside down: y up
        const VkRect2D all = {{0, 0}, {VD_WIDTH, VD_HEIGHT}};
        vkCmdSetViewport(vk.cb, 0, 1, &viewport);
        vkCmdSetScissor(vk.cb, 0, 1, &all);
        vkCmdBindPipeline(vk.cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipelines[kind]);
        vkCmdBindDescriptorSets(vk.cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.layout, 0, 1, &vk.set, 0, NULL);
        const VkBuffer buffers[2] = {deep ? vk.cube_vertices.buffer : vk.quad.buffer, vk.instances.buffer};
        const VkDeviceSize offsets[2] = {0, 0};
        vkCmdBindVertexBuffers(vk.cb, 0, 2, buffers, offsets);
        if (deep) {
            vkCmdBindIndexBuffer(vk.cb, vk.cube_indices.buffer, 0, VK_INDEX_TYPE_UINT16);
            vkCmdDrawIndexed(vk.cb, 36, count, 0, 0, 0);
        } else {
            vkCmdDraw(vk.cb, 6, count, 0, 0);
        }
    }
    vkCmdEndRendering(vk.cb);
    image_barrier(vk.color.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_ACCESS_TRANSFER_READ_BIT);
    const VkBufferImageCopy copy = {0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {1, 1, 0}, {1, 1, 1}};
    vkCmdCopyImageToBuffer(vk.cb, vk.color.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk.readback.buffer, 1, &copy);
    host_barrier();
    if (!submit_and_wait()) abort();
    if (!vk.readback.coherent) {
        const VkMappedMemoryRange range = {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, NULL, vk.readback.memory, 0, VK_WHOLE_SIZE};
        vkInvalidateMappedMemoryRanges(vk.device, 1, &range);
    }
    const uint8_t *p = vk.readback.mapped;
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static bool vk_frame(uint8_t *rgba)
{
    if (!begin_commands()) return false;
    // The frame's still there, as the last copy left it
    const VkBufferImageCopy copy = {0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {VD_WIDTH, VD_HEIGHT, 1}};
    vkCmdCopyImageToBuffer(vk.cb, vk.color.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vk.readback.buffer, 1, &copy);
    host_barrier();
    if (!submit_and_wait()) return false;
    if (!vk.readback.coherent) {
        const VkMappedMemoryRange range = {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, NULL, vk.readback.memory, 0, VK_WHOLE_SIZE};
        vkInvalidateMappedMemoryRanges(vk.device, 1, &range);
    }
    memcpy(rgba, vk.readback.mapped, (size_t)VD_WIDTH * VD_HEIGHT * 4u);
    return true;
}

static void vk_close(void)
{
    if (vk.device) {
        vkDeviceWaitIdle(vk.device);
        for (int k = 0; k < VD_KINDS; k++) {
            if (vk.pipelines[k]) vkDestroyPipeline(vk.device, vk.pipelines[k], NULL);
        }
        if (vk.layout) vkDestroyPipelineLayout(vk.device, vk.layout, NULL);
        if (vk.pool) vkDestroyDescriptorPool(vk.device, vk.pool, NULL);
        if (vk.set_layout) vkDestroyDescriptorSetLayout(vk.device, vk.set_layout, NULL);
        vk_buffer *buffers[] = {&vk.quad, &vk.cube_vertices, &vk.cube_indices, &vk.instances, &vk.uniforms, &vk.readback};
        for (size_t i = 0; i < sizeof buffers / sizeof buffers[0]; i++) free_buffer(buffers[i]);
        if (vk.sampler) vkDestroySampler(vk.device, vk.sampler, NULL);
        free_image(&vk.texture);
        free_image(&vk.depth);
        free_image(&vk.color);
        if (vk.fence) vkDestroyFence(vk.device, vk.fence, NULL);
        if (vk.commands) vkDestroyCommandPool(vk.device, vk.commands, NULL);
        vkDestroyDevice(vk.device, NULL);
    }
    if (vk.messenger && vkDestroyDebugUtilsMessengerEXT) vkDestroyDebugUtilsMessengerEXT(vk.instance, vk.messenger, NULL);
    if (vk.instance) vkDestroyInstance(vk.instance, NULL);
    memset(&vk, 0, sizeof vk);
}

const vd_api vd_vulkan = {
    .name = "Vulkan",
    .api = "Vulkan 1.3",
    .open = vk_open,
    .instances = vk_instances,
    .written = vk_written,
    .render = vk_render,
    .frame = vk_frame,
    .close = vk_close,
};
