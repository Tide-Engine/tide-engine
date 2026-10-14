// Vulkan under the renderer (see gpu.h and gpu_vk.h), on desktop: Vulkan 1.3
// with push descriptors, as WebGPU's model has it underneath, so the renderer
// goes through it as it goes through WebGPU's:
//
// - Shaders are SPIR-V that tidec translated from the WGSL (spirv.c), so a
//   pipeline's bindings are WebGPU's: set 0, the uniforms at binding 0 and
//   texture i and its sampler at 1 + 2i and 2 + 2i. Every pipeline has one
//   layout of all of them, and a draw pushes what it's given there.
// - Clip space is WebGPU's in the shaders already (they move depth from -1
//   to 1 to 0 to 1); the viewport is turned upside down, so y goes up in clip
//   space and rows go from the top in every image, the window's too.
// - What's written to a buffer or a texture, a pass sees from its start, as
//   WebGPU's queue writes are: writes and copies are recorded in a command
//   buffer that runs before the pass's, so what a pass draws with is as it was
//   when the pass ended. Each command buffer starts by waiting for everything
//   before it, which keeps every hazard out at the cost of some overlap.
// - Every image rests in the layout it's read in: a texture's to be sampled,
//   a multisampled image's to be drawn into. Whatever uses it otherwise turns
//   it there and back, so nothing keeps track of where each is.
// - A frame's commands go to the GPU as the window shows it, and the frame
//   before last is waited for then: two frames are in flight. What's let go
//   of is destroyed once the GPU is done with the frame that let it go.

#include "gpu_vk.h"

#include "files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VK_NO_PROTOTYPES
#include "vulkan/vulkan_core.h"

#include "tide/page.h"

// ---------------------------------------------------------------------------
// Vulkan's functions, found through the loader

#define VK_GLOBAL(X)                                                                                                   \
    X(vkCreateInstance) X(vkEnumerateInstanceVersion) X(vkEnumerateInstanceLayerProperties)                           \
    X(vkEnumerateInstanceExtensionProperties)

#define VK_INSTANCE(X)                                                                                                 \
    X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties)                               \
    X(vkGetPhysicalDeviceProperties2) X(vkGetPhysicalDeviceFeatures2) X(vkGetPhysicalDeviceQueueFamilyProperties)     \
    X(vkGetPhysicalDeviceMemoryProperties) X(vkGetPhysicalDeviceFormatProperties)                                     \
    X(vkGetPhysicalDeviceImageFormatProperties) X(vkCreateDevice) X(vkGetDeviceProcAddr)                              \
    X(vkEnumerateDeviceExtensionProperties) X(vkDestroySurfaceKHR) X(vkGetPhysicalDeviceSurfaceSupportKHR)            \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) X(vkGetPhysicalDeviceSurfaceFormatsKHR)                              \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR)

#define VK_INSTANCE_OPTIONAL(X) X(vkCreateDebugUtilsMessengerEXT) X(vkDestroyDebugUtilsMessengerEXT)

#define VK_DEVICE(X)                                                                                                   \
    X(vkDestroyDevice) X(vkGetDeviceQueue) X(vkDeviceWaitIdle) X(vkQueueSubmit) X(vkQueueWaitIdle)                    \
    X(vkQueuePresentKHR) X(vkCreateSwapchainKHR) X(vkDestroySwapchainKHR) X(vkGetSwapchainImagesKHR)                  \
    X(vkAcquireNextImageKHR) X(vkAllocateMemory) X(vkFreeMemory) X(vkMapMemory) X(vkCreateBuffer)                     \
    X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) X(vkBindBufferMemory) X(vkCreateImage) X(vkDestroyImage)      \
    X(vkGetImageMemoryRequirements) X(vkBindImageMemory) X(vkCreateImageView) X(vkDestroyImageView)                   \
    X(vkCreateSampler) X(vkDestroySampler) X(vkCreateShaderModule) X(vkDestroyShaderModule)                           \
    X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) X(vkCreatePipelineLayout)                          \
    X(vkDestroyPipelineLayout) X(vkCreateGraphicsPipelines) X(vkDestroyPipeline) X(vkCreateCommandPool)               \
    X(vkDestroyCommandPool) X(vkResetCommandPool) X(vkAllocateCommandBuffers) X(vkBeginCommandBuffer)                 \
    X(vkEndCommandBuffer) X(vkCreateFence) X(vkDestroyFence) X(vkWaitForFences) X(vkResetFences)                      \
    X(vkCreateSemaphore) X(vkDestroySemaphore) X(vkCreateQueryPool) X(vkDestroyQueryPool) X(vkGetQueryPoolResults)    \
    X(vkCmdResetQueryPool) X(vkCmdWriteTimestamp) X(vkCmdPipelineBarrier) X(vkCmdCopyBuffer)                          \
    X(vkCmdCopyBufferToImage) X(vkCmdCopyImageToBuffer) X(vkCmdCopyImage) X(vkCmdBlitImage) X(vkCmdClearColorImage) X(vkCmdClearDepthStencilImage)   \
    X(vkCmdBeginRendering) X(vkCmdEndRendering) X(vkCmdBindPipeline) X(vkCmdSetViewport) X(vkCmdSetScissor)           \
    X(vkCmdSetStencilReference) X(vkCmdBindVertexBuffers) X(vkCmdBindIndexBuffer) X(vkCmdDraw)                        \
    X(vkCmdDrawIndexed) X(vkCmdPushDescriptorSetKHR) X(vkCreatePipelineCache) X(vkDestroyPipelineCache)           \
    X(vkGetPipelineCacheData) X(vkMergePipelineCaches)

#define VK_DECLARE(name) static PFN_##name name;
VK_GLOBAL(VK_DECLARE)
VK_INSTANCE(VK_DECLARE)
VK_INSTANCE_OPTIONAL(VK_DECLARE)
VK_DEVICE(VK_DECLARE)
#undef VK_DECLARE

static PFN_vkGetInstanceProcAddr get_instance_proc;

// ---------------------------------------------------------------------------
// What there is

enum { FRAMES = 2 };                 // Frames in flight
enum { TIME_PAIRS = 4 };             // Pairs of timestamps waiting to be read
enum { BLOCK_BYTES = 64u << 20 };    // Device memory is taken in blocks this big...
enum { DEDICATED_BYTES = 16u << 20 }; // ...and an allocation this big or bigger on its own
enum { RING_BYTES = 4u << 20 };      // A frame's uploads and uniforms start with this much room

// A piece of device memory: an allocation's own, or a part of a block's.
typedef struct vk_alloc {
    VkDeviceMemory memory;
    VkDeviceSize offset, size;
    uint32_t block; // 1 + the block it's in, or 0 for its own allocation
    void *mapped;   // Where the host sees it, for memory the host sees
} vk_alloc;

typedef struct vk_range {
    VkDeviceSize offset, size;
} vk_range;

typedef struct vk_block {
    VkDeviceMemory memory;
    VkDeviceSize size;
    uint32_t type;
    bool images;     // Images and buffers are kept in blocks apart: no granularity between them to keep
    vk_range *free;  // What's free, in order
    uint32_t free_count, free_capacity;
} vk_block;

typedef struct vk_buffer {
    VkBuffer buffer; // VK_NULL_HANDLE: a free place
    vk_alloc memory;
    VkDeviceSize size;
} vk_buffer;

typedef struct vk_texture {
    VkImage image; // VK_NULL_HANDLE: a free place
    vk_alloc memory;
    VkImageView view;   // What's sampled: all of it (a cube as a cube), its depth alone for a depth
    VkImageView attach; // What's drawn into: its first image (each face's, for a cube: faces[i])
    VkImageView faces[TIDE_GPU_CUBE_FACES];
    VkFormat format;
    tide_gpu_format pixels; // An uploaded texture's, as gpu.h has them
    int width, height;
    uint32_t levels, layers;
    VkImageLayout rest;   // The layout it's in between uses
    VkImageUsageFlags usage;
    bool depth;           // A target's depth and stencil
    bool cube;
    bool mipped;          // Its smaller images are made (tide_gpu.mipmaps)
    bool owned;           // A target's, which only the target frees
    bool shared;          // A cube's face's color, which is the cube's image: nothing of its own to free
} vk_texture;

typedef struct vk_pipeline {
    VkPipeline pipeline; // VK_NULL_HANDLE: a free place
    uint32_t texture_count;
    uint8_t reads[TIDE_GPU_MAX_TEXTURES];
    tide_gpu_target_format into;
} vk_pipeline;

typedef struct vk_target {
    bool used;
    tide_gpu_target_format format;
    int width, height;
    uint32_t samples;
    uint32_t color_count;
    tide_gpu_id colors[TIDE_GPU_MAX_COLORS]; // Textures: what's sampled and read back, and a multisampled one's resolves
    tide_gpu_id depth;                        // A texture: 0 for a multisampled one's until its depth is resolved
    // A multisampled one's images of samples, which passes draw into
    VkImage many[TIDE_GPU_MAX_COLORS], many_depth;
    VkImageView many_views[TIDE_GPU_MAX_COLORS], many_depth_view;
    vk_alloc many_memory[TIDE_GPU_MAX_COLORS], many_depth_memory;
    bool bytes;       // Its first color image is 4 bytes a pixel: what's read back
    bool face;        // A cube's face
    tide_gpu_id cube; // ...of this cube
    uint32_t layer;   // ...this one
} vk_target;

// What's destroyed once the frame that let it go is done.
typedef struct vk_garbage {
    VkObjectType type;
    uint64_t handle;
    vk_alloc memory; // Given back with it, where there's any
} vk_garbage;

// Room in host memory a frame writes into: uploads on their way, and uniforms.
typedef struct vk_ring {
    VkBuffer buffer;
    vk_alloc memory;
    uint8_t *mapped;
    VkDeviceSize size, used;
} vk_ring;

typedef struct vk_frame {
    VkCommandPool pool;
    VkCommandBuffer *buffers;
    uint32_t allocated, used;
    VkFence fence;
    bool fenced;     // Its fence is to be waited for...
    uint64_t serial; // ...which signals that submission's done
    VkSemaphore acquired, rendered;
    vk_ring ring;
    vk_garbage *garbage;
    uint32_t garbage_count, garbage_capacity;
} vk_frame;

// Samplers: by tide_gpu_wrap, then tide_gpu_filter; then the one that compares, and the nearest one that clamps.
enum { WRAPS = TIDE_GPU_WRAP_MIRROR + 1, FILTERS = TIDE_GPU_FILTER_MIPMAPS + 1 };
enum { SAMPLER_COMPARE = WRAPS * FILTERS, SAMPLER_COUNT };

static struct {
    bool open;
    tide_vk_window window;
    VkInstance instance;
    VkDebugUtilsMessengerEXT messenger;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue queue;
    uint32_t family;
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceMemoryProperties memory;
    bool compression;   // BC1 and BC3
    bool depth_linear;  // A depth is compared blended between its pixels
    VkFormat window_format; // The window's, which every color image of 4 bytes a pixel has too
    VkFormat depth_format;
    uint32_t epoch;
    bool lost;
    double timestamp_ns; // A timestamp's tick, in nanoseconds: 0 for none

    // The window's images
    VkSwapchainKHR swapchain;
    VkImage images[8];
    VkImageView views[8];
    uint32_t image_count;
    VkExtent2D extent;
    bool stale;                  // To be made again before the next frame draws in it
    VkImage window_depth;
    VkImageView window_depth_view;
    vk_alloc window_depth_memory;
    bool window_depth_new;       // Its layout is still undefined
    uint32_t image;              // The image the frame draws in...
    bool acquired;               // ...once it has one
    bool acquire_waited;         // ...and a submission waits for it
    bool image_drawn;            // ...and a pass drew in it

    vk_frame frames[FRAMES];
    uint32_t frame;
    VkDescriptorSetLayout set_layout;
    VkPipelineLayout layout;
    VkSampler samplers[SAMPLER_COUNT];

    vk_block *blocks;
    uint32_t block_count;
    vk_buffer *buffers;
    uint32_t buffer_count;
    vk_texture *textures;
    uint32_t texture_count;
    vk_pipeline *pipelines;
    uint32_t pipeline_count;
    vk_target *targets;
    uint32_t target_count;

    // Recording: the command buffer outside passes, the pass's, and one for
    // what's uploaded during the pass, which goes before it. After a pass,
    // its command buffer goes on as the one outside passes.
    VkCommandBuffer outside;
    bool drawn; // A pass was recorded since the last barrier that has transfers wait for draws
    VkCommandBuffer pass;
    uint32_t pass_pending; // Where the pass's is in `pending`
    VkCommandBuffer before;
    VkCommandBuffer *pending; // To be submitted, in order: all are in it as they begin
    uint32_t pending_count, pending_capacity;

    // The pass
    bool in_pass, skip_pass;
    tide_gpu_id pass_target; // 0 for the window
    int pass_width, pass_height;
    tide_gpu_id current; // The pipeline the pass uses: 0 for none
    bool bound; // The pass's descriptors are what the draws after it read
    VkDeviceSize uniforms_at;
    tide_gpu_id textures_bound[TIDE_GPU_MAX_TEXTURES];
    uint8_t sampled[TIDE_GPU_MAX_TEXTURES];

    // Timing
    VkQueryPool queries;
    uint32_t query_first, query_count; // Pairs written and not yet read, in a ring of them
    uint32_t query_sent;               // ...of which the first this many went to the GPU, to be read once they're there
    uint64_t query_serial[TIME_PAIRS]; // ...with the submission each went with
    uint64_t timestamp_mask;           // The bits a timestamp has
    bool query_active;
    uint64_t submitted, completed;     // Submissions made, and how many of them the GPU is done with
    int made_width, made_height;       // The window's pixels as the swapchain was made for them
    uint32_t swapchains_made;          // For tests (tide_gpu_vk_swapchain)

    // What the driver compiled, kept across runs (tide_gpu.pipeline_cache)
    VkPipelineCache pipeline_cache;
    char *cache_file;   // Where: NULL for nowhere
    size_t cache_read;  // The bytes of it this device took from the file (tide_gpu_vk_pipeline_cache_read)
    uint64_t cache_hash; // ...and their hash: what's written as the backend closes, unless it's that again
    bool lose_asked;                   // For tests (tide_gpu_vk_lose): lost as the frame is shown
} vk;


static void say_result(const char *what, const VkResult result)
{
    fprintf(stderr, "tide: Vulkan: %s failed (%d)\n", what, (int)result);
}

// A failure that leaves the device unusable: lost, or out of what it needs.
// Everything goes, and the next epoch makes it all again (see vk_epoch).
static void lose(const VkResult result)
{
    if (result == VK_ERROR_DEVICE_LOST && !vk.lost) fprintf(stderr, "tide: the Vulkan device was lost: it's made again\n");
    vk.lost = true;
}

// A call that must work: what isn't VK_SUCCESS stops the program, as a
// backend can't go on without it, but for a lost device.
static bool check(const VkResult result, const char *what)
{
    if (result == VK_SUCCESS) return true;
    if (result == VK_ERROR_DEVICE_LOST) {
        lose(result);
        return false;
    }
    say_result(what, result);
    abort();
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

static bool block_take(vk_block *b, const VkDeviceSize size, const VkDeviceSize align, VkDeviceSize *offset)
{
    for (uint32_t i = 0; i < b->free_count; i++) {
        vk_range *r = &b->free[i];
        const VkDeviceSize start = (r->offset + align - 1u) / align * align;
        if (start + size > r->offset + r->size) continue;
        const VkDeviceSize before = start - r->offset, after = r->offset + r->size - (start + size);
        if (before && after) { // Split in two
            if (b->free_count == b->free_capacity) {
                const uint32_t capacity = b->free_capacity * 2u;
                b->free = tide_realloc(b->free, b->free_capacity * sizeof(vk_range), capacity * sizeof(vk_range));
                b->free_capacity = capacity;
                r = &b->free[i];
            }
            memmove(&b->free[i + 2], &b->free[i + 1], (b->free_count - i - 1u) * sizeof(vk_range));
            b->free[i + 1] = (vk_range){start + size, after};
            b->free[i].size = before;
            b->free_count++;
        } else if (before) {
            r->size = before;
        } else if (after) {
            r->offset = start + size;
            r->size = after;
        } else {
            memmove(&b->free[i], &b->free[i + 1], (b->free_count - i - 1u) * sizeof(vk_range));
            b->free_count--;
        }
        *offset = start;
        return true;
    }
    return false;
}

static void block_give(vk_block *b, const VkDeviceSize offset, const VkDeviceSize size)
{
    uint32_t at = 0;
    while (at < b->free_count && b->free[at].offset < offset) at++;
    const bool joins_before = at > 0 && b->free[at - 1u].offset + b->free[at - 1u].size == offset;
    const bool joins_after = at < b->free_count && offset + size == b->free[at].offset;
    if (joins_before && joins_after) {
        b->free[at - 1u].size += size + b->free[at].size;
        memmove(&b->free[at], &b->free[at + 1u], (b->free_count - at - 1u) * sizeof(vk_range));
        b->free_count--;
    } else if (joins_before) {
        b->free[at - 1u].size += size;
    } else if (joins_after) {
        b->free[at].offset = offset;
        b->free[at].size += size;
    } else {
        if (b->free_count == b->free_capacity) {
            const uint32_t capacity = b->free_capacity * 2u;
            b->free = tide_realloc(b->free, b->free_capacity * sizeof(vk_range), capacity * sizeof(vk_range));
            b->free_capacity = capacity;
        }
        memmove(&b->free[at + 1u], &b->free[at], (b->free_count - at) * sizeof(vk_range));
        b->free[at] = (vk_range){offset, size};
        b->free_count++;
    }
}

// Who uses memory: the GPU alone, the host writing what the GPU reads, or the
// host reading what the GPU wrote, which wants memory the CPU caches (reads
// of what it doesn't cache take tens of milliseconds a frame).
typedef enum vk_memory_use { USE_GPU, USE_UPLOAD, USE_READBACK } vk_memory_use;

// Memory for what `needs` says.
static bool allocate(const VkMemoryRequirements needs, const vk_memory_use use, const bool image, vk_alloc *out)
{
    *out = (vk_alloc){0};
    const bool host_visible = use != USE_GPU;
    const VkMemoryPropertyFlags want = host_visible ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                                    : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    uint32_t type = use == USE_READBACK ? memory_type(needs.memoryTypeBits, want | VK_MEMORY_PROPERTY_HOST_CACHED_BIT) : UINT32_MAX;
    if (type == UINT32_MAX) type = memory_type(needs.memoryTypeBits, want);
    if (type == UINT32_MAX && !host_visible) type = memory_type(needs.memoryTypeBits, 0);
    if (type == UINT32_MAX) return false;
    if (!host_visible && needs.size < DEDICATED_BYTES) {
        for (uint32_t i = 0; i < vk.block_count; i++) {
            vk_block *b = &vk.blocks[i];
            if (b->memory && b->type == type && b->images == image && block_take(b, needs.size, needs.alignment, &out->offset)) {
                out->memory = b->memory;
                out->size = needs.size;
                out->block = i + 1u;
                return true;
            }
        }
        vk_block block = {.size = BLOCK_BYTES, .type = type, .images = image, .free_capacity = 16};
        const VkMemoryAllocateInfo info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, BLOCK_BYTES, type};
        if (vkAllocateMemory(vk.device, &info, NULL, &block.memory) == VK_SUCCESS) {
            block.free = tide_alloc(block.free_capacity * sizeof(vk_range));
            block.free[0] = (vk_range){0, BLOCK_BYTES};
            block.free_count = 1;
            uint32_t place = 0;
            while (place < vk.block_count && vk.blocks[place].memory) place++;
            if (place == vk.block_count) {
                vk.blocks = tide_realloc(vk.blocks, vk.block_count * sizeof(vk_block), (vk.block_count + 1u) * sizeof(vk_block));
                vk.block_count++;
            }
            vk.blocks[place] = block;
            if (block_take(&vk.blocks[place], needs.size, needs.alignment, &out->offset)) {
                out->memory = block.memory;
                out->size = needs.size;
                out->block = place + 1u;
                return true;
            }
        }
        // No block to be had: an allocation of its own, below
    }
    const VkMemoryAllocateInfo info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, needs.size, type};
    if (vkAllocateMemory(vk.device, &info, NULL, &out->memory) != VK_SUCCESS) return false;
    out->size = needs.size;
    if (host_visible && vkMapMemory(vk.device, out->memory, 0, VK_WHOLE_SIZE, 0, &out->mapped) != VK_SUCCESS) {
        vkFreeMemory(vk.device, out->memory, NULL);
        *out = (vk_alloc){0};
        return false;
    }
    return true;
}

static void give_back(const vk_alloc *a)
{
    if (!a->memory) return;
    if (a->block) {
        vk_block *b = &vk.blocks[a->block - 1u];
        block_give(b, a->offset, a->size);
        // A block that's all free again goes, but the last one of its kind, which stays for what comes next
        if (b->free_count == 1 && b->free[0].size == b->size) {
            uint32_t others = 0;
            for (uint32_t i = 0; i < vk.block_count; i++) {
                others += vk.blocks[i].memory && i != a->block - 1u && vk.blocks[i].type == b->type
                       && vk.blocks[i].images == b->images;
            }
            if (others) {
                vkFreeMemory(vk.device, b->memory, NULL);
                free(b->free);
                *b = (vk_block){0};
            }
        }
        return;
    }
    vkFreeMemory(vk.device, a->memory, NULL); // Unmapped as it's freed
}

// ---------------------------------------------------------------------------
// What's let go of, kept until the GPU is done with it

static void discard(const VkObjectType type, const uint64_t handle, const vk_alloc *memory)
{
    if (!handle && !(memory && memory->memory)) return;
    vk_frame *f = &vk.frames[vk.frame];
    if (f->garbage_count == f->garbage_capacity) {
        const uint32_t capacity = f->garbage_capacity ? f->garbage_capacity * 2u : 64u;
        f->garbage = tide_realloc(f->garbage, f->garbage_capacity * sizeof(vk_garbage), capacity * sizeof(vk_garbage));
        f->garbage_capacity = capacity;
    }
    f->garbage[f->garbage_count++] = (vk_garbage){type, handle, memory ? *memory : (vk_alloc){0}};
}

#define DISCARD(type, handle, memory) discard((type), (uint64_t)(uintptr_t)(handle), (memory))

static void destroy(const vk_garbage *g)
{
    switch (g->type) {
    case VK_OBJECT_TYPE_BUFFER: vkDestroyBuffer(vk.device, (VkBuffer)(uintptr_t)g->handle, NULL); break;
    case VK_OBJECT_TYPE_IMAGE: vkDestroyImage(vk.device, (VkImage)(uintptr_t)g->handle, NULL); break;
    case VK_OBJECT_TYPE_IMAGE_VIEW: vkDestroyImageView(vk.device, (VkImageView)(uintptr_t)g->handle, NULL); break;
    case VK_OBJECT_TYPE_PIPELINE: vkDestroyPipeline(vk.device, (VkPipeline)(uintptr_t)g->handle, NULL); break;
    case VK_OBJECT_TYPE_SEMAPHORE: vkDestroySemaphore(vk.device, (VkSemaphore)(uintptr_t)g->handle, NULL); break;
    default: break;
    }
    give_back(&g->memory);
}

static void collect(vk_frame *f)
{
    for (uint32_t i = 0; i < f->garbage_count; i++) destroy(&f->garbage[i]);
    f->garbage_count = 0;
}

// ---------------------------------------------------------------------------
// Buffers of the frame's: what's uploaded on its way, and uniforms

static bool make_buffer(const VkDeviceSize size, const VkBufferUsageFlags usage, const vk_memory_use use, VkBuffer *buffer,
                        vk_alloc *memory)
{
    const VkBufferCreateInfo info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, size, usage,
                                     VK_SHARING_MODE_EXCLUSIVE, 0, NULL};
    if (vkCreateBuffer(vk.device, &info, NULL, buffer) != VK_SUCCESS) return false;
    VkMemoryRequirements needs;
    vkGetBufferMemoryRequirements(vk.device, *buffer, &needs);
    if (!allocate(needs, use, false, memory)
        || vkBindBufferMemory(vk.device, *buffer, memory->memory, memory->offset) != VK_SUCCESS) {
        vkDestroyBuffer(vk.device, *buffer, NULL);
        give_back(memory);
        *buffer = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

static bool ring_make(vk_ring *r, const VkDeviceSize size)
{
    *r = (vk_ring){0};
    if (!make_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, USE_UPLOAD, &r->buffer, &r->memory)) {
        return false;
    }
    r->mapped = r->memory.mapped;
    r->size = size;
    return true;
}

// `size` bytes of the frame's ring, from a multiple of `align`, and `room`
// bytes after where they start: a bigger ring where the ring hasn't that
// room, the old one going once the frame is done.
static VkDeviceSize ring_take(const VkDeviceSize size, const VkDeviceSize align, const VkDeviceSize room, VkBuffer *buffer,
                              uint8_t **at)
{
    vk_ring *r = &vk.frames[vk.frame].ring;
    VkDeviceSize start = (r->used + align - 1u) / align * align;
    if (!r->buffer || start + room > r->size) {
        VkDeviceSize size_now = r->size ? r->size * 2u : RING_BYTES;
        while (size_now < room + align) size_now *= 2u;
        if (r->buffer) DISCARD(VK_OBJECT_TYPE_BUFFER, r->buffer, &r->memory);
        if (!ring_make(r, size_now)) {
            fprintf(stderr, "tide: Vulkan: no memory for %llu bytes of uploads\n", (unsigned long long)size_now);
            abort();
        }
        start = 0;
    }
    r->used = start + size;
    *buffer = r->buffer;
    *at = r->mapped + start;
    return start;
}

// ---------------------------------------------------------------------------
// Commands

// Barriers order what one command writes before what another reads or
// writes, by the stages that do it, as everything recorded before a barrier
// (in the order command buffers are submitted) and everything after it. A pass
// waits for transfers, and its attachments for those of the passes before
// it, but its vertices don't wait for their fragments; transfers wait for the
// draws before them, whose buffers they may overwrite. Images have barriers of
// their own as they turn from one layout to another (turn).

enum {
    DRAW_STAGES = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
    ATTACHMENT_STAGES = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT
                      | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    ATTACHMENT_WRITES = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
    ATTACHMENT_ACCESS = ATTACHMENT_WRITES | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
    DRAW_READS = VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT
               | VK_ACCESS_SHADER_READ_BIT,
};

static void barrier(const VkCommandBuffer cb, const VkPipelineStageFlags from, const VkAccessFlags written,
                    const VkPipelineStageFlags to, const VkAccessFlags then)
{
    const VkMemoryBarrier b = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, NULL, written, then};
    vkCmdPipelineBarrier(cb, from, to, 0, 1, &b, 0, NULL, 0, NULL);
}

// Attachments drawn in before (a window's depth, a multisampled target's
// images, which stay attachments) are drawn in, or resolved, after.
static void barrier_attachments(const VkCommandBuffer cb)
{
    barrier(cb, ATTACHMENT_STAGES, ATTACHMENT_WRITES, ATTACHMENT_STAGES, ATTACHMENT_ACCESS);
}

// What a pass draws with was uploaded before it, and what it draws in was drawn in before it.
static void barrier_pass(const VkCommandBuffer cb)
{
    barrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, DRAW_STAGES | ATTACHMENT_STAGES,
            DRAW_READS | ATTACHMENT_ACCESS);
    barrier_attachments(cb);
}

// Transfers after the draws before them, which may have read what they write
// (a buffer written again), or written what they read.
static void barrier_transfers(const VkCommandBuffer cb)
{
    barrier(cb, DRAW_STAGES | ATTACHMENT_STAGES, ATTACHMENT_WRITES, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
}

// A command buffer of the frame's, recording.
static VkCommandBuffer command_buffer(void)
{
    vk_frame *f = &vk.frames[vk.frame];
    if (f->used == f->allocated) {
        const uint32_t more = f->allocated ? f->allocated : 8u;
        f->buffers = tide_realloc(f->buffers, f->allocated * sizeof(VkCommandBuffer), (f->allocated + more) * sizeof(VkCommandBuffer));
        const VkCommandBufferAllocateInfo info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, f->pool,
                                                  VK_COMMAND_BUFFER_LEVEL_PRIMARY, more};
        check(vkAllocateCommandBuffers(vk.device, &info, f->buffers + f->allocated), "vkAllocateCommandBuffers");
        f->allocated += more;
    }
    const VkCommandBuffer cb = f->buffers[f->used++];
    const VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL,
                                            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL};
    check(vkBeginCommandBuffer(cb, &begin), "vkBeginCommandBuffer");
    return cb;
}

static void queue_up(const VkCommandBuffer cb)
{
    if (vk.pending_count == vk.pending_capacity) {
        const uint32_t capacity = vk.pending_capacity ? vk.pending_capacity * 2u : 16u;
        vk.pending = tide_realloc(vk.pending, vk.pending_capacity * sizeof(VkCommandBuffer), capacity * sizeof(VkCommandBuffer));
        vk.pending_capacity = capacity;
    }
    vk.pending[vk.pending_count++] = cb;
}

// The command buffer for what's done outside passes, and what's uploaded
// for the pass that's on: it runs before that pass.
static VkCommandBuffer outside(void)
{
    VkCommandBuffer cb;
    if (vk.pass) {
        if (!vk.before) { // In `pending` just before the pass
            vk.before = command_buffer();
            queue_up(vk.before);
            memmove(vk.pending + vk.pass_pending + 1u, vk.pending + vk.pass_pending,
                    (vk.pending_count - 1u - vk.pass_pending) * sizeof(VkCommandBuffer));
            vk.pending[vk.pass_pending++] = vk.before;
        }
        cb = vk.before;
    } else {
        if (!vk.outside) {
            vk.outside = command_buffer();
            queue_up(vk.outside);
        }
        cb = vk.outside;
    }
    // The passes before it are done with what it's about to write (vk.drawn counts the pass that's on as
    // being after, as a `before` command buffer is)
    if (vk.drawn) {
        barrier_transfers(cb);
        vk.drawn = false;
    }
    return cb;
}

static void close_outside(void)
{
    if (!vk.outside) return;
    check(vkEndCommandBuffer(vk.outside), "vkEndCommandBuffer");
    vk.outside = VK_NULL_HANDLE;
}

static VkImageAspectFlags aspects_of(const VkFormat format)
{
    return format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT
         ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
}

// The stages that use an image in a layout, and how. An image is only ever
// undefined before the window's is acquired, which a submission waits for at
// color output, or before it was first written: it waits for nothing else. A
// shown one's, for its next pass that frame, is the color output it was left at.
static void layout_use(const VkImageLayout layout, VkPipelineStageFlags *stages, VkAccessFlags *access)
{
    switch (layout) {
    case VK_IMAGE_LAYOUT_UNDEFINED:
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
        *stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, *access = 0;
        break;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        *stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        *access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL: // Its resolve is at color output
        *stages = ATTACHMENT_STAGES, *access = ATTACHMENT_ACCESS;
        break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
        *stages = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, *access = VK_ACCESS_SHADER_READ_BIT;
        break;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        *stages = VK_PIPELINE_STAGE_TRANSFER_BIT, *access = VK_ACCESS_TRANSFER_READ_BIT;
        break;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        *stages = VK_PIPELINE_STAGE_TRANSFER_BIT, *access = VK_ACCESS_TRANSFER_WRITE_BIT;
        break;
    default:
        *stages = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, *access = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        break;
    }
}

// An image's images from `level` and `layer` on, `levels` and `layers` of
// them, from one layout to another, after what used them in the first.
static void turn(const VkCommandBuffer cb, const VkImage image, const VkFormat format, const uint32_t level, const uint32_t levels,
                 const uint32_t layer, const uint32_t layers, const VkImageLayout from, const VkImageLayout to)
{
    if (from == to) return;
    VkPipelineStageFlags before, after;
    VkAccessFlags used, then;
    layout_use(from, &before, &used);
    layout_use(to, &after, &then);
    const VkAccessFlags writes = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                               | VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    const VkImageMemoryBarrier b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, NULL, used & writes, then,
                                    from, to, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image,
                                    {aspects_of(format), level, levels, layer, layers}};
    vkCmdPipelineBarrier(cb, before, after, 0, 0, NULL, 0, NULL, 1, &b);
}

static void turn_texture(const VkCommandBuffer cb, const vk_texture *t, const VkImageLayout from, const VkImageLayout to)
{
    turn(cb, t->image, t->format, 0, t->levels, 0, t->layers, from, to);
}

// Everything recorded goes to the GPU, and is waited for: for what's read
// back, and frames that never show (a test's draws). The frame starts again,
// with all its room.
static void flush(void)
{
    if (vk.lost) return;
    close_outside();
    VkResult result = VK_SUCCESS;
    if (vk.pending_count) {
        const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        const bool wait = vk.acquired && vk.image_drawn && !vk.acquire_waited;
        const VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, wait ? 1u : 0u, &vk.frames[vk.frame].acquired, &stage,
                                     vk.pending_count, vk.pending, 0, NULL};
        if (wait) vk.acquire_waited = true;
        result = vkQueueSubmit(vk.queue, 1, &submit, VK_NULL_HANDLE);
        vk.pending_count = 0;
        vk.submitted++;
        for (uint32_t i = vk.query_sent; i < vk.query_count; i++) vk.query_serial[(vk.query_first + i) % TIME_PAIRS] = vk.submitted;
        vk.query_sent = vk.query_count;
    }
    // ...and what the frames before it sent, which may still be on their way
    if (result == VK_SUCCESS) result = vkQueueWaitIdle(vk.queue);
    if (result != VK_SUCCESS) {
        lose(result);
        return;
    }
    // Everything the GPU had is done: every frame's room, garbage and command buffers are free again
    vk.completed = vk.submitted;
    for (uint32_t i = 0; i < FRAMES; i++) {
        vk_frame *f = &vk.frames[i];
        if (f->fenced) {
            vkWaitForFences(vk.device, 1, &f->fence, VK_TRUE, UINT64_MAX);
            vkResetFences(vk.device, 1, &f->fence);
            f->fenced = false;
        }
        collect(f);
        vkResetCommandPool(vk.device, f->pool, 0);
        f->used = 0;
        f->ring.used = 0;
    }
}

// So many command buffers wait for the GPU, or so much room is taken, with
// no frame shown to send them: they go now, as a program that draws and never
// shows (a test's) would otherwise keep all of them.
static void flush_if_full(void)
{
    const vk_frame *f = &vk.frames[vk.frame];
    if (vk.pending_count >= 512 || f->used >= 1024 || f->ring.used >= (256u << 20)) flush();
}

// ---------------------------------------------------------------------------
// Images

static bool make_image(const VkImageCreateInfo *info, vk_alloc *memory, VkImage *image)
{
    if (vkCreateImage(vk.device, info, NULL, image) != VK_SUCCESS) return false;
    VkMemoryRequirements needs;
    vkGetImageMemoryRequirements(vk.device, *image, &needs);
    if (!allocate(needs, USE_GPU, true, memory) || vkBindImageMemory(vk.device, *image, memory->memory, memory->offset) != VK_SUCCESS) {
        vkDestroyImage(vk.device, *image, NULL);
        give_back(memory);
        *image = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

static VkImageView make_view(const VkImage image, const VkImageViewType type, const VkFormat format, const VkImageAspectFlags aspects,
                             const uint32_t level, const uint32_t levels, const uint32_t layer, const uint32_t layers)
{
    const VkImageViewCreateInfo info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, image, type, format,
                                        {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                         VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY},
                                        {aspects, level, levels, layer, layers}};
    VkImageView view = VK_NULL_HANDLE;
    check(vkCreateImageView(vk.device, &info, NULL, &view), "vkCreateImageView");
    return view;
}

// ---------------------------------------------------------------------------
// The window's images

static void forget_swapchain(void)
{
    for (uint32_t i = 0; i < vk.image_count; i++) vkDestroyImageView(vk.device, vk.views[i], NULL);
    vk.image_count = 0;
    if (vk.swapchain) vkDestroySwapchainKHR(vk.device, vk.swapchain, NULL);
    vk.swapchain = VK_NULL_HANDLE;
    if (vk.window_depth_view) vkDestroyImageView(vk.device, vk.window_depth_view, NULL);
    if (vk.window_depth) vkDestroyImage(vk.device, vk.window_depth, NULL);
    give_back(&vk.window_depth_memory);
    vk.window_depth_memory = (vk_alloc){0};
    vk.window_depth = VK_NULL_HANDLE;
    vk.window_depth_view = VK_NULL_HANDLE;
}

static VkSemaphore rendered[8]; // By the window's image: what its showing waits for

// The window's images at its size now: false while it has none (minimized).
static bool make_swapchain(void)
{
    int width = 0, height = 0;
    vk.window.pixel_size(&width, &height);
    if (width <= 0 || height <= 0) return false;
    VkSurfaceCapabilitiesKHR caps;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.physical, vk.surface, &caps) != VK_SUCCESS) return false;
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width = (uint32_t)width < caps.minImageExtent.width ? caps.minImageExtent.width
                     : (uint32_t)width > caps.maxImageExtent.width ? caps.maxImageExtent.width : (uint32_t)width;
        extent.height = (uint32_t)height < caps.minImageExtent.height ? caps.minImageExtent.height
                      : (uint32_t)height > caps.maxImageExtent.height ? caps.maxImageExtent.height : (uint32_t)height;
    }
    if (!extent.width || !extent.height) return false;
    vkDeviceWaitIdle(vk.device); // Nothing draws in or shows the old images any more
    uint32_t count = caps.minImageCount + 1u;
    if (caps.maxImageCount && count > caps.maxImageCount) count = caps.maxImageCount;
    if (count > 8u) count = 8u;
    // At the display's pace, or as fast as it goes for a window nobody watches
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    if (!vk.window.paced) {
        VkPresentModeKHR modes[16];
        uint32_t mode_count = 16;
        if (vkGetPhysicalDeviceSurfacePresentModesKHR(vk.physical, vk.surface, &mode_count, modes) < 0) mode_count = 0;
        for (uint32_t i = 0; i < mode_count; i++) {
            if (modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) mode = modes[i];
            if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR && mode == VK_PRESENT_MODE_FIFO_KHR) mode = modes[i];
        }
    }
    const VkCompositeAlphaFlagBitsKHR alpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
                                            ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                                            : (VkCompositeAlphaFlagBitsKHR)(caps.supportedCompositeAlpha & -caps.supportedCompositeAlpha);
    const VkSwapchainKHR old = vk.swapchain;
    const VkSwapchainCreateInfoKHR info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = vk.surface,
        .minImageCount = count,
        .imageFormat = vk.window_format,
        .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                    | (caps.supportedUsageFlags & (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)),
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform,
        .compositeAlpha = alpha,
        .presentMode = mode,
        .clipped = VK_TRUE,
        .oldSwapchain = old,
    };
    VkSwapchainKHR made = VK_NULL_HANDLE;
    const VkResult result = vkCreateSwapchainKHR(vk.device, &info, NULL, &made);
    for (uint32_t i = 0; i < vk.image_count; i++) vkDestroyImageView(vk.device, vk.views[i], NULL);
    vk.image_count = 0;
    if (old) vkDestroySwapchainKHR(vk.device, old, NULL);
    vk.swapchain = VK_NULL_HANDLE;
    if (result != VK_SUCCESS) {
        if (result == VK_ERROR_DEVICE_LOST) lose(result);
        return false;
    }
    // As many images as the driver made, which may be more than asked for: eight at most here
    uint32_t images = 0;
    if (vkGetSwapchainImagesKHR(vk.device, made, &images, NULL) != VK_SUCCESS || images > 8u
        || vkGetSwapchainImagesKHR(vk.device, made, &images, vk.images) != VK_SUCCESS) {
        vkDestroySwapchainKHR(vk.device, made, NULL);
        return false;
    }
    vk.swapchain = made;
    vk.image_count = images;
    for (uint32_t i = 0; i < vk.image_count; i++) {
        vk.views[i] = make_view(vk.images[i], VK_IMAGE_VIEW_TYPE_2D, vk.window_format, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1);
        // New ones for new images: a present on the old swapchain may still wait on the old one, which goes once the
        // frame is done (nothing waits for presentation itself)
        if (rendered[i]) DISCARD(VK_OBJECT_TYPE_SEMAPHORE, rendered[i], NULL);
        const VkSemaphoreCreateInfo semaphore = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, NULL, 0};
        check(vkCreateSemaphore(vk.device, &semaphore, NULL, &rendered[i]), "vkCreateSemaphore");
    }
    vk.extent = extent;
    vk.made_width = width, vk.made_height = height; // What it's made again for: another size than this
    vk.swapchains_made++;
    // Its depth and stencil, which every target has: the window's are drawn into and never read
    if (vk.window_depth_view) vkDestroyImageView(vk.device, vk.window_depth_view, NULL);
    if (vk.window_depth) vkDestroyImage(vk.device, vk.window_depth, NULL);
    give_back(&vk.window_depth_memory);
    const VkImageCreateInfo depth = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = vk.depth_format,
        .extent = {extent.width, extent.height, 1}, .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (!make_image(&depth, &vk.window_depth_memory, &vk.window_depth)) {
        fprintf(stderr, "tide: Vulkan: no memory for the window's depth\n");
        abort();
    }
    vk.window_depth_view = make_view(vk.window_depth, VK_IMAGE_VIEW_TYPE_2D, vk.depth_format, aspects_of(vk.depth_format), 0, 1, 0, 1);
    vk.window_depth_new = true;
    vk.stale = false;
    return true;
}

// The image of the window's the frame draws in: false where there's none.
static bool acquire(void)
{
    if (vk.acquired) return true;
    int width = 0, height = 0;
    vk.window.pixel_size(&width, &height);
    if (width <= 0 || height <= 0) return false;
    for (int tries = 0; tries < 2; tries++) {
        if (!vk.swapchain || vk.stale || vk.made_width != width || vk.made_height != height) {
            flush(); // What's recorded draws into nothing of the old images
            if (vk.lost || !make_swapchain()) return false;
        }
        const VkResult result = vkAcquireNextImageKHR(vk.device, vk.swapchain, UINT64_MAX, vk.frames[vk.frame].acquired,
                                                      VK_NULL_HANDLE, &vk.image);
        if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
            if (result == VK_SUBOPTIMAL_KHR) vk.stale = true; // Shown this frame, made again for the next
            vk.acquired = true;
            vk.acquire_waited = false;
            vk.image_drawn = false;
            return true;
        }
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            vk.stale = true;
            continue;
        }
        lose(result);
        return false;
    }
    return false;
}

void tide_gpu_vk_present(void)
{
    if (!vk.open || !vk.device) return;
    if (vk.lost) {
        vk.pending_count = 0;
        vk.outside = vk.pass = vk.before = VK_NULL_HANDLE;
        vk.acquired = false;
        return;
    }
    close_outside();
    vk_frame *f = &vk.frames[vk.frame];
    const bool showing = vk.acquired;
    if (showing && !vk.image_drawn) { // Nothing drew in it: it's shown as it is, which it has to be ready for
        const VkCommandBuffer cb = command_buffer();
        turn(cb, vk.images[vk.image], vk.window_format, 0, 1, 0, 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        check(vkEndCommandBuffer(cb), "vkEndCommandBuffer");
        queue_up(cb);
    }
    if (vk.pending_count || showing) {
        const bool wait = showing && !vk.acquire_waited;
        const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        const VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, wait ? 1u : 0u, &f->acquired, &stage,
                                     vk.pending_count, vk.pending, showing ? 1u : 0u, &rendered[vk.image]};
        const VkResult result = vkQueueSubmit(vk.queue, 1, &submit, f->fence);
        vk.pending_count = 0;
        vk.submitted++;
        f->serial = vk.submitted;
        for (uint32_t i = vk.query_sent; i < vk.query_count; i++) vk.query_serial[(vk.query_first + i) % TIME_PAIRS] = vk.submitted;
        vk.query_sent = vk.query_count;
        if (result != VK_SUCCESS) {
            lose(result);
            vk.acquired = false;
            return;
        }
        f->fenced = true;
    }
    if (showing) {
        const VkPresentInfoKHR present = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, NULL, 1, &rendered[vk.image], 1, &vk.swapchain,
                                          &vk.image, NULL};
        const VkResult result = vkQueuePresentKHR(vk.queue, &present);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) vk.stale = true;
        else if (result != VK_SUCCESS) lose(result);
        vk.acquired = false;
    }
    // The next frame: its room and command buffers are free once the GPU is done with the one before it there
    vk.frame = (vk.frame + 1u) % FRAMES;
    f = &vk.frames[vk.frame];
    if (f->fenced) {
        const VkResult result = vkWaitForFences(vk.device, 1, &f->fence, VK_TRUE, UINT64_MAX);
        if (result != VK_SUCCESS) {
            lose(result);
            return;
        }
        vkResetFences(vk.device, 1, &f->fence);
        f->fenced = false;
        if (f->serial > vk.completed) vk.completed = f->serial;
    }
    collect(f);
    vkResetCommandPool(vk.device, f->pool, 0);
    f->used = 0;
    f->ring.used = 0;
    if (vk.lose_asked) {
        vk.lose_asked = false;
        lose(VK_ERROR_DEVICE_LOST);
    }
}

void tide_gpu_vk_lose(void)
{
    vk.lose_asked = vk.open && vk.device;
}

void tide_gpu_vk_swapchain(int *width, int *height, uint32_t *made)
{
    *width = vk.swapchain ? (int)vk.extent.width : 0;
    *height = vk.swapchain ? (int)vk.extent.height : 0;
    *made = vk.swapchains_made;
}

// ---------------------------------------------------------------------------
// Places: an id is 1 + a place in its kind's array

static tide_gpu_id buffer_place(void)
{
    for (uint32_t i = 0; i < vk.buffer_count; i++) {
        if (!vk.buffers[i].buffer) return i + 1u;
    }
    const uint32_t count = vk.buffer_count ? vk.buffer_count * 2u : 64u;
    vk.buffers = tide_realloc(vk.buffers, vk.buffer_count * sizeof(vk_buffer), count * sizeof(vk_buffer));
    memset(vk.buffers + vk.buffer_count, 0, (count - vk.buffer_count) * sizeof(vk_buffer));
    const uint32_t place = vk.buffer_count;
    vk.buffer_count = count;
    return place + 1u;
}

static tide_gpu_id texture_place(void)
{
    for (uint32_t i = 0; i < vk.texture_count; i++) {
        if (!vk.textures[i].image) return i + 1u;
    }
    const uint32_t count = vk.texture_count ? vk.texture_count * 2u : 64u;
    vk.textures = tide_realloc(vk.textures, vk.texture_count * sizeof(vk_texture), count * sizeof(vk_texture));
    memset(vk.textures + vk.texture_count, 0, (count - vk.texture_count) * sizeof(vk_texture));
    const uint32_t place = vk.texture_count;
    vk.texture_count = count;
    return place + 1u;
}

static tide_gpu_id pipeline_place(void)
{
    for (uint32_t i = 0; i < vk.pipeline_count; i++) {
        if (!vk.pipelines[i].pipeline) return i + 1u;
    }
    const uint32_t count = vk.pipeline_count ? vk.pipeline_count * 2u : 32u;
    vk.pipelines = tide_realloc(vk.pipelines, vk.pipeline_count * sizeof(vk_pipeline), count * sizeof(vk_pipeline));
    memset(vk.pipelines + vk.pipeline_count, 0, (count - vk.pipeline_count) * sizeof(vk_pipeline));
    const uint32_t place = vk.pipeline_count;
    vk.pipeline_count = count;
    return place + 1u;
}

static tide_gpu_id target_place(void)
{
    for (uint32_t i = 0; i < vk.target_count; i++) {
        if (!vk.targets[i].used) return i + 1u;
    }
    const uint32_t count = vk.target_count ? vk.target_count * 2u : 16u;
    vk.targets = tide_realloc(vk.targets, vk.target_count * sizeof(vk_target), count * sizeof(vk_target));
    memset(vk.targets + vk.target_count, 0, (count - vk.target_count) * sizeof(vk_target));
    const uint32_t place = vk.target_count;
    vk.target_count = count;
    return place + 1u;
}

static vk_texture *texture_of(const tide_gpu_id id)
{
    return id && id <= vk.texture_count && vk.textures[id - 1u].image ? &vk.textures[id - 1u] : NULL;
}

// ---------------------------------------------------------------------------
// Buffers

static void vk_buffer_write(const tide_gpu_id id, const size_t offset, const void *data, const size_t size)
{
    if (vk.lost || !size) return;
    const vk_buffer *b = &vk.buffers[id - 1u];
    VkBuffer from;
    uint8_t *at;
    const VkDeviceSize start = ring_take(size, 16u, size, &from, &at);
    memcpy(at, data, size);
    const VkBufferCopy copy = {start, offset, size};
    const VkCommandBuffer cb = outside();
    // Copies in one command buffer have no order of their own: one into the same bytes as another goes after it
    const VkMemoryBarrier after = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, NULL, VK_ACCESS_TRANSFER_WRITE_BIT,
                                   VK_ACCESS_TRANSFER_WRITE_BIT};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &after, 0, NULL, 0, NULL);
    vkCmdCopyBuffer(cb, from, b->buffer, 1, &copy);
}

static tide_gpu_id vk_buffer_make(const tide_gpu_buffer_kind kind, const size_t size, const void *data)
{
    if (vk.lost) return 0;
    const tide_gpu_id id = buffer_place();
    vk_buffer *b = &vk.buffers[id - 1u];
    const VkBufferUsageFlags usage = (kind == TIDE_GPU_VERTICES ? VK_BUFFER_USAGE_VERTEX_BUFFER_BIT : VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
                                   | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (!make_buffer(size ? size : 4u, usage, USE_GPU, &b->buffer, &b->memory)) {
        fprintf(stderr, "tide: Vulkan: no memory for a buffer of %llu bytes\n", (unsigned long long)size);
        abort();
    }
    b->size = size;
    if (data) vk_buffer_write(id, 0, data, size);
    return id;
}

static void vk_buffer_free(const tide_gpu_id id)
{
    vk_buffer *b = &vk.buffers[id - 1u];
    DISCARD(VK_OBJECT_TYPE_BUFFER, b->buffer, &b->memory);
    *b = (vk_buffer){0};
}

// ---------------------------------------------------------------------------
// Textures

static VkFormat format_of(const tide_gpu_format format)
{
    switch (format) {
    case TIDE_GPU_R8: return VK_FORMAT_R8_UNORM;
    case TIDE_GPU_R16F: return VK_FORMAT_R16_SFLOAT;
    case TIDE_GPU_RGBA32I: return VK_FORMAT_R32G32B32A32_SINT;
    default: return VK_FORMAT_R8G8B8A8_UNORM;
    }
}

// An image for a texture: `levels` of it, `layers` of each (6 for a cube),
// at rest in `rest`, zeros until it's written (WebGPU's are too).
static tide_gpu_id make_texture(const VkFormat format, const int width, const int height, const uint32_t levels, const bool cube,
                                const VkImageUsageFlags usage, const VkImageLayout rest, const bool clear)
{
    const tide_gpu_id id = texture_place();
    vk_texture *t = &vk.textures[id - 1u];
    const bool depth = aspects_of(format) != VK_IMAGE_ASPECT_COLOR_BIT;
    const VkImageCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .flags = cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0,
        .imageType = VK_IMAGE_TYPE_2D, .format = format, .extent = {(uint32_t)width, (uint32_t)height, 1},
        .mipLevels = levels, .arrayLayers = cube ? 6u : 1u, .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = usage, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    *t = (vk_texture){.format = format, .width = width, .height = height, .levels = levels, .layers = cube ? 6u : 1u,
                      .rest = rest, .usage = usage, .depth = depth, .cube = cube};
    if (!make_image(&info, &t->memory, &t->image)) {
        fprintf(stderr, "tide: Vulkan: no memory for a texture of %d by %d\n", width, height);
        abort();
    }
    if (depth) {
        t->view = make_view(t->image, VK_IMAGE_VIEW_TYPE_2D, format, VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1);
        t->attach = make_view(t->image, VK_IMAGE_VIEW_TYPE_2D, format, aspects_of(format), 0, 1, 0, 1);
    } else if (cube) {
        t->view = make_view(t->image, VK_IMAGE_VIEW_TYPE_CUBE, format, VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 6);
        for (uint32_t face = 0; face < TIDE_GPU_CUBE_FACES; face++) {
            t->faces[face] = make_view(t->image, VK_IMAGE_VIEW_TYPE_2D, format, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, face, 1);
        }
    } else {
        t->view = make_view(t->image, VK_IMAGE_VIEW_TYPE_2D, format, VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1);
        t->attach = levels > 1 ? make_view(t->image, VK_IMAGE_VIEW_TYPE_2D, format, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1) : t->view;
    }
    const VkCommandBuffer cb = outside();
    if (clear && !depth) {
        turn_texture(cb, t, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        const VkClearColorValue zero = {{0}};
        const VkImageSubresourceRange all = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, t->layers};
        vkCmdClearColorImage(cb, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &all);
        turn_texture(cb, t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, rest);
    } else {
        turn_texture(cb, t, VK_IMAGE_LAYOUT_UNDEFINED, rest);
    }
    return id;
}

static void forget_texture(vk_texture *t)
{
    if (!t->shared) {
        if (t->attach && t->attach != t->view) DISCARD(VK_OBJECT_TYPE_IMAGE_VIEW, t->attach, NULL);
        DISCARD(VK_OBJECT_TYPE_IMAGE_VIEW, t->view, NULL);
        for (uint32_t face = 0; face < TIDE_GPU_CUBE_FACES; face++) DISCARD(VK_OBJECT_TYPE_IMAGE_VIEW, t->faces[face], NULL);
        DISCARD(VK_OBJECT_TYPE_IMAGE, t->image, &t->memory);
    }
    *t = (vk_texture){0};
}

// Pixels into level 0 of layer `layer`: the `width` by `height` of them from
// (x, y), read where they are among `pixels`, rows `stride` pixels apart.
static void write_pixels(vk_texture *t, const void *pixels, const int x, const int y, const int width, const int height,
                         const int stride, const uint32_t layer)
{
    if (width <= 0 || height <= 0) return;
    const size_t pixel = (size_t)tide_gpu_format_bytes(t->pixels), row = (size_t)width * pixel;
    VkBuffer from;
    uint8_t *at;
    const VkDeviceSize start = ring_take(row * (size_t)height, 16u, row * (size_t)height, &from, &at);
    for (int r = 0; r < height; r++) {
        memcpy(at + (size_t)r * row, (const uint8_t *)pixels + (((size_t)(y + r) * (size_t)stride) + (size_t)x) * pixel, row);
    }
    const VkCommandBuffer cb = outside();
    turn(cb, t->image, t->format, 0, 1, layer, 1, t->rest, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    const VkBufferImageCopy copy = {start, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, layer, 1}, {x, y, 0},
                                    {(uint32_t)width, (uint32_t)height, 1}};
    vkCmdCopyBufferToImage(cb, from, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    turn(cb, t->image, t->format, 0, 1, layer, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, t->rest);
}

static tide_gpu_id vk_texture_make(const tide_gpu_format format, const int width, const int height, const void *pixels)
{
    if (vk.lost) return 0;
    const tide_gpu_id id = make_texture(format_of(format), width, height, 1, false,
                                        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, !pixels);
    vk_texture *t = &vk.textures[id - 1u];
    t->pixels = format;
    if (pixels) write_pixels(t, pixels, 0, 0, width, height, width, 0);
    return id;
}

static void vk_texture_write(const tide_gpu_id id, const void *pixels)
{
    vk_texture *t = texture_of(id);
    if (vk.lost || !t) return;
    write_pixels(t, pixels, 0, 0, t->width, t->height, t->width, 0);
}

static void vk_texture_write_part(const tide_gpu_id id, const void *pixels, const int x, const int y, const int width, const int height)
{
    vk_texture *t = texture_of(id);
    if (vk.lost || !t) return;
    write_pixels(t, pixels, x, y, width, height, t->width, 0);
}

static tide_gpu_id vk_texture_blocks(const int width, const int height, const tide_texture_blocks blocks)
{
    if (vk.lost || !vk.compression || !blocks.levels) return 0;
    const bool bc1 = blocks.format == TIDE_BLOCKS_BC1;
    const tide_gpu_id id = make_texture(bc1 ? VK_FORMAT_BC1_RGBA_UNORM_BLOCK : VK_FORMAT_BC3_UNORM_BLOCK, width, height,
                                        blocks.levels, false, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false);
    vk_texture *t = &vk.textures[id - 1u];
    t->mipped = true;
    const VkCommandBuffer cb = outside();
    turn_texture(cb, t, t->rest, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    uint32_t w = (uint32_t)width, h = (uint32_t)height;
    size_t offset = 0;
    for (uint32_t level = 0; level < blocks.levels; level++) {
        const size_t bytes = (size_t)((w + 3u) / 4u) * ((h + 3u) / 4u) * (bc1 ? 8u : 16u);
        VkBuffer from;
        uint8_t *at;
        const VkDeviceSize start = ring_take(bytes, 16u, bytes, &from, &at);
        memcpy(at, blocks.pixels + offset, bytes);
        const VkBufferImageCopy copy = {start, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1}, {0, 0, 0}, {w, h, 1}};
        vkCmdCopyBufferToImage(cb, from, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        offset += bytes;
        if (w > 1) w /= 2;
        if (h > 1) h /= 2;
    }
    turn_texture(cb, t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, t->rest);
    return id;
}

static void vk_texture_free(const tide_gpu_id id)
{
    vk_texture *t = texture_of(id);
    if (t) forget_texture(t);
}

static bool blits(const VkFormat format)
{
    VkFormatProperties properties;
    vkGetPhysicalDeviceFormatProperties(vk.physical, format, &properties);
    const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT
                                    | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    return (properties.optimalTilingFeatures & need) == need;
}

static uint32_t levels_for(const int width, const int height)
{
    uint32_t levels = 1;
    for (int side = width > height ? width : height; side > 1; side /= 2) levels++;
    return levels;
}

// A texture's smaller images, each from the one before it, halved and
// blended: a pixel the mean of the four it's made of. One made with no room
// for them gets a copy of itself with room first, as WebGPU's does.
static void vk_mipmaps(const tide_gpu_id id)
{
    vk_texture *t = texture_of(id);
    if (vk.lost || !t || t->depth || !blits(t->format)) return;
    const uint32_t levels = levels_for(t->width, t->height);
    if (levels < 2) return;
    const VkCommandBuffer cb = outside();
    if (t->levels < levels) {
        vk_texture old = *t;
        const VkImageCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = t->format,
            .extent = {(uint32_t)t->width, (uint32_t)t->height, 1}, .mipLevels = levels, .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = t->usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        t->memory = (vk_alloc){0};
        if (!make_image(&info, &t->memory, &t->image)) {
            *t = old; // No room: it stays as it was, with no smaller images
            return;
        }
        t->levels = levels;
        t->usage = info.usage;
        t->view = make_view(t->image, VK_IMAGE_VIEW_TYPE_2D, t->format, VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1);
        t->attach = make_view(t->image, VK_IMAGE_VIEW_TYPE_2D, t->format, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1);
        turn(cb, t->image, t->format, 0, levels, 0, 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        turn(cb, old.image, old.format, 0, 1, 0, 1, old.rest, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        const VkImageCopy copy = {{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                  {0, 0, 0}, {(uint32_t)t->width, (uint32_t)t->height, 1}};
        vkCmdCopyImage(cb, old.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        turn(cb, t->image, t->format, 0, levels, 0, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, t->rest);
        if (old.attach != old.view) DISCARD(VK_OBJECT_TYPE_IMAGE_VIEW, old.attach, NULL);
        DISCARD(VK_OBJECT_TYPE_IMAGE_VIEW, old.view, NULL);
        DISCARD(VK_OBJECT_TYPE_IMAGE, old.image, &old.memory);
    }
    turn(cb, t->image, t->format, 0, 1, 0, t->layers, t->rest, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    int w = t->width, h = t->height;
    for (uint32_t level = 1; level < t->levels; level++) {
        const int half_w = w > 1 ? w / 2 : 1, half_h = h > 1 ? h / 2 : 1;
        turn(cb, t->image, t->format, level, 1, 0, t->layers, t->rest, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        const VkImageBlit blit = {{VK_IMAGE_ASPECT_COLOR_BIT, level - 1u, 0, t->layers}, {{0, 0, 0}, {w, h, 1}},
                                  {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, t->layers}, {{0, 0, 0}, {half_w, half_h, 1}}};
        vkCmdBlitImage(cb, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                       VK_FILTER_LINEAR);
        turn(cb, t->image, t->format, level, 1, 0, t->layers, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        w = half_w, h = half_h;
    }
    turn(cb, t->image, t->format, 0, t->levels, 0, t->layers, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t->rest);
    t->mipped = true;
}

// A cube: six layers, with room for their smaller images from the start, as
// WebGPU's has. One that's drawn into has the window's bytes, as a target's
// image of bytes has: the pipelines into it are made for those.
static tide_gpu_id vk_cube(const tide_gpu_color color, const int size, const void *const *faces)
{
    if (vk.lost) return 0;
    const VkFormat format = color == TIDE_GPU_COLOR_FLOAT ? VK_FORMAT_R16G16B16A16_SFLOAT
                          : faces ? VK_FORMAT_R8G8B8A8_UNORM : vk.window_format;
    const tide_gpu_id id = make_texture(format, size, size, levels_for(size, size), true,
                                        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                                            | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true);
    vk_texture *t = &vk.textures[id - 1u];
    t->pixels = TIDE_GPU_RGBA8;
    if (faces && color != TIDE_GPU_COLOR_FLOAT) {
        for (uint32_t face = 0; face < TIDE_GPU_CUBE_FACES; face++) write_pixels(t, faces[face], 0, 0, size, size, size, face);
    }
    return id;
}

// ---------------------------------------------------------------------------
// Targets

static VkFormat color_format(const tide_gpu_color color)
{
    return color == TIDE_GPU_COLOR_FLOAT ? VK_FORMAT_R16G16B16A16_SFLOAT : vk.window_format;
}

static tide_gpu_id depth_texture(const int width, const int height)
{
    return make_texture(vk.depth_format, width, height, 1, false,
                        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, false);
}

static VkSampleCountFlags sample_counts(const tide_gpu_target_format format)
{
    VkSampleCountFlags counts = vk.properties.limits.framebufferDepthSampleCounts & vk.properties.limits.framebufferStencilSampleCounts;
    for (uint32_t i = 0; i < tide_gpu_target_colors(format); i++) {
        VkImageFormatProperties properties;
        if (vkGetPhysicalDeviceImageFormatProperties(vk.physical, color_format(tide_gpu_target_color(format, i)), VK_IMAGE_TYPE_2D,
                                                     VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 0,
                                                     &properties) != VK_SUCCESS) {
            return VK_SAMPLE_COUNT_1_BIT;
        }
        counts &= properties.sampleCounts;
    }
    return counts;
}

// The most of 8, 4 and 2 samples that's no more than asked and the device
// has for each of the target's images; and 1 for none, and for depth alone.
static uint32_t vk_target_samples(const tide_gpu_target_format format)
{
    uint32_t samples = tide_gpu_target_samples(format);
    if (samples <= 1u || !tide_gpu_target_colors(format)) return 1u;
    const VkSampleCountFlags counts = sample_counts(format);
    while (samples > 1u && !(counts & samples)) samples >>= 1;
    return samples > 1u ? samples : 1u;
}

static bool many_image(const VkFormat format, const int width, const int height, const uint32_t samples, const VkImageUsageFlags usage,
                       VkImage *image, vk_alloc *memory, VkImageView *view)
{
    const VkImageCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = format,
        .extent = {(uint32_t)width, (uint32_t)height, 1}, .mipLevels = 1, .arrayLayers = 1,
        .samples = (VkSampleCountFlagBits)samples, .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = usage,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (!make_image(&info, memory, image)) return false;
    *view = make_view(*image, VK_IMAGE_VIEW_TYPE_2D, format, aspects_of(format), 0, 1, 0, 1);
    return true;
}

static tide_gpu_id vk_target_make(const tide_gpu_target_format format, const int width, const int height)
{
    if (vk.lost) return 0;
    const tide_gpu_id id = target_place();
    vk_target *t = &vk.targets[id - 1u];
    const uint32_t colors = tide_gpu_target_colors(format);
    *t = (vk_target){.used = true, .format = format, .width = width, .height = height, .color_count = colors,
                     .samples = colors ? vk_target_samples(format) : 1u,
                     .bytes = colors && tide_gpu_target_color(format, 0) == TIDE_GPU_COLOR_BYTES};
    for (uint32_t i = 0; i < colors; i++) {
        const tide_gpu_id texture = make_texture(color_format(tide_gpu_target_color(format, i)), width, height, 1, false,
                                                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
                                                     | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true);
        t = &vk.targets[id - 1u]; // make_texture may have moved nothing here, but be sure of the place
        t->colors[i] = texture;
        vk.textures[texture - 1u].owned = true;
    }
    if (t->samples > 1u) {
        const VkCommandBuffer cb = outside();
        for (uint32_t i = 0; i < colors; i++) {
            const VkFormat f = color_format(tide_gpu_target_color(format, i));
            if (!many_image(f, width, height, t->samples, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                            &t->many[i], &t->many_memory[i], &t->many_views[i])) {
                fprintf(stderr, "tide: Vulkan: no memory for a target of %u samples a pixel\n", (unsigned)t->samples);
                abort();
            }
            turn(cb, t->many[i], f, 0, 1, 0, 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            const VkClearColorValue zero = {{0}};
            const VkImageSubresourceRange all = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdClearColorImage(cb, t->many[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &all);
            turn(cb, t->many[i], f, 0, 1, 0, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        }
        if (!many_image(vk.depth_format, width, height, t->samples,
                        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, &t->many_depth,
                        &t->many_depth_memory, &t->many_depth_view)) {
            fprintf(stderr, "tide: Vulkan: no memory for a target of %u samples a pixel\n", (unsigned)t->samples);
            abort();
        }
        turn(cb, t->many_depth, vk.depth_format, 0, 1, 0, 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        const VkClearDepthStencilValue far = {1.0f, 0};
        const VkImageSubresourceRange whole = {aspects_of(vk.depth_format), 0, 1, 0, 1};
        vkCmdClearDepthStencilImage(cb, t->many_depth, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &far, 1, &whole);
        turn(cb, t->many_depth, vk.depth_format, 0, 1, 0, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
             VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    } else {
        const tide_gpu_id depth = depth_texture(width, height);
        t = &vk.targets[id - 1u];
        t->depth = depth;
        vk.textures[depth - 1u].owned = true;
    }
    return id;
}

// A cube's face as a target: the face's image, and a depth of its own.
static tide_gpu_id vk_cube_face(const tide_gpu_id cube, const uint32_t face)
{
    if (vk.lost) return 0;
    const vk_texture *c = texture_of(cube);
    if (!c) return 0;
    const int size = c->width;
    const bool floats = c->format == VK_FORMAT_R16G16B16A16_SFLOAT;
    const tide_gpu_id depth = depth_texture(size, size);
    const tide_gpu_id id = target_place();
    vk_target *t = &vk.targets[id - 1u];
    *t = (vk_target){.used = true, .width = size, .height = size, .color_count = 1, .samples = 1, .face = true, .cube = cube,
                     .layer = face < TIDE_GPU_CUBE_FACES ? face : 0u, .bytes = !floats, .depth = depth,
                     .format = tide_gpu_target_with(TIDE_GPU_TARGET_DEPTH, floats ? TIDE_GPU_COLOR_FLOAT : TIDE_GPU_COLOR_BYTES)};
    vk.textures[depth - 1u].owned = true;
    return id;
}

static int vk_target_limit(void)
{
    return (int)vk.properties.limits.maxImageDimension2D;
}

static bool vk_float_targets(void)
{
    return true; // Vulkan draws into, blends and samples blended RGBA16F on every device
}

static tide_gpu_id vk_target_texture(const tide_gpu_id id, const uint32_t color)
{
    const vk_target *t = &vk.targets[id - 1u];
    return !t->face && color < t->color_count ? t->colors[color] : 0;
}

static tide_gpu_id vk_target_depth(const tide_gpu_id id)
{
    return vk.targets[id - 1u].depth;
}

static void vk_target_free(const tide_gpu_id id)
{
    vk_target *t = &vk.targets[id - 1u];
    if (!t->face) {
        for (uint32_t i = 0; i < t->color_count; i++) {
            vk_texture *c = texture_of(t->colors[i]);
            if (c) forget_texture(c);
        }
    }
    vk_texture *depth = texture_of(t->depth);
    if (depth) forget_texture(depth);
    for (uint32_t i = 0; i < TIDE_GPU_MAX_COLORS; i++) {
        DISCARD(VK_OBJECT_TYPE_IMAGE_VIEW, t->many_views[i], NULL);
        DISCARD(VK_OBJECT_TYPE_IMAGE, t->many[i], &t->many_memory[i]);
    }
    DISCARD(VK_OBJECT_TYPE_IMAGE_VIEW, t->many_depth_view, NULL);
    DISCARD(VK_OBJECT_TYPE_IMAGE, t->many_depth, &t->many_depth_memory);
    *t = (vk_target){0};
}

// A multisampled target's depth, a sample of each pixel's, into a texture of
// one a pixel: made the first time.
static void vk_resolve_depth(const tide_gpu_id id)
{
    if (vk.lost) return;
    vk_target *t = &vk.targets[id - 1u];
    if (t->samples <= 1u) return;
    if (!t->depth) {
        const tide_gpu_id depth = depth_texture(t->width, t->height);
        t = &vk.targets[id - 1u];
        t->depth = depth;
        vk.textures[depth - 1u].owned = true;
    }
    vk_texture *d = &vk.textures[t->depth - 1u];
    const VkCommandBuffer cb = outside();
    barrier_attachments(cb); // The passes that drew its samples
    turn(cb, d->image, d->format, 0, 1, 0, 1, d->rest, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    const VkRenderingAttachmentInfo depth = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = t->many_depth_view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, .resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT,
        .resolveImageView = d->attach, .resolveImageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    };
    const VkRenderingInfo info = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = {{0, 0}, {(uint32_t)t->width, (uint32_t)t->height}},
                                  .layerCount = 1, .pDepthAttachment = &depth, .pStencilAttachment = &depth};
    vkCmdBeginRendering(cb, &info);
    vkCmdEndRendering(cb);
    turn(cb, d->image, d->format, 0, 1, 0, 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, d->rest);
}

// What's read back goes through this, in memory the host reads.
static struct {
    VkBuffer buffer;
    vk_alloc memory;
    VkDeviceSize size;
} readback;

static void vk_target_read(const tide_gpu_id id, uint8_t *rgba)
{
    const vk_target *t = &vk.targets[id - 1u];
    const size_t bytes = (size_t)t->width * (size_t)t->height * 4u;
    if (vk.lost || !t->bytes) { // Depth alone, or floats first: no bytes to read
        memset(rgba, 0, bytes);
        return;
    }
    if (readback.size < bytes) {
        if (readback.buffer) {
            flush(); // Nothing waits on the old one
            vkDestroyBuffer(vk.device, readback.buffer, NULL);
            give_back(&readback.memory);
        }
        readback.size = 0;
        if (!make_buffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, USE_READBACK, &readback.buffer, &readback.memory)) {
            fprintf(stderr, "tide: Vulkan: no memory to read %llu bytes back\n", (unsigned long long)bytes);
            abort();
        }
        readback.size = bytes;
    }
    const vk_texture *c = t->face ? texture_of(t->cube) : texture_of(t->colors[0]);
    const uint32_t layer = t->face ? t->layer : 0u;
    const VkCommandBuffer cb = outside();
    turn(cb, c->image, c->format, 0, 1, layer, 1, c->rest, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    const VkBufferImageCopy copy = {0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, layer, 1}, {0, 0, 0},
                                    {(uint32_t)t->width, (uint32_t)t->height, 1}};
    vkCmdCopyImageToBuffer(cb, c->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &copy);
    const VkMemoryBarrier seen = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, NULL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &seen, 0, NULL, 0, NULL);
    turn(cb, c->image, c->format, 0, 1, layer, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, c->rest);
    flush();
    if (vk.lost) {
        memset(rgba, 0, bytes);
        return;
    }
    const uint8_t *from = readback.memory.mapped;
    if (c->format == VK_FORMAT_B8G8R8A8_UNORM) { // Blue first: red first, as it's read back
        for (size_t i = 0; i < bytes; i += 4) {
            rgba[i] = from[i + 2], rgba[i + 1] = from[i + 1], rgba[i + 2] = from[i], rgba[i + 3] = from[i + 3];
        }
    } else {
        memcpy(rgba, from, bytes);
    }
}

// ---------------------------------------------------------------------------
// Pipelines

static VkFormat attribute_format(const tide_gpu_attribute_format format)
{
    static const VkFormat formats[] = {
        VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32_SFLOAT, VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R32_SINT, VK_FORMAT_R32G32_SINT, VK_FORMAT_R32G32B32_SINT, VK_FORMAT_R32G32B32A32_SINT,
    }; // By tide_gpu_attribute_format
    return (uint32_t)format < sizeof formats / sizeof formats[0] ? formats[format] : VK_FORMAT_R32_SFLOAT;
}

uint32_t *(*tide_gpu_spirv_of)(const char *wgsl, size_t *count, char *error, size_t error_size);

static tide_gpu_id vk_pipeline_make(const tide_gpu_pipeline_desc *desc)
{
    if (vk.lost) return 0;
    const char *name = desc->name ? desc->name : "a shader";
    const uint32_t *code = desc->spirv;
    size_t words = desc->spirv_words;
    uint32_t *made = NULL;
    if ((!code || !words) && desc->wgsl && tide_gpu_spirv_of) { // A shader written by hand, with WGSL alone
        char error[256];
        made = tide_gpu_spirv_of(desc->wgsl, &words, error, sizeof error);
        if (!made) {
            fprintf(stderr, "tide: the WGSL of %s didn't become SPIR-V: %s\n", name, error);
            abort();
        }
        code = made;
    }
    if (!code || !words) {
        fprintf(stderr, "tide: %s has no SPIR-V for Vulkan: its game was built by a tidec that wrote none\n", name);
        abort();
    }
    const VkShaderModuleCreateInfo module_info = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, words * 4u, code};
    VkShaderModule module;
    const VkResult module_made = vkCreateShaderModule(vk.device, &module_info, NULL, &module);
    free(made);
    if (module_made != VK_SUCCESS) {
        fprintf(stderr, "tide: the SPIR-V of %s didn't make a Vulkan shader module\n", name);
        abort();
    }
    const VkPipelineShaderStageCreateInfo stages[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, module, "vertex", NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, module, "fragment", NULL},
    };
    VkVertexInputBindingDescription bindings[TIDE_GPU_MAX_SLOTS];
    for (uint32_t i = 0; i < desc->slot_count; i++) {
        bindings[i] = (VkVertexInputBindingDescription){i, desc->slots[i].stride,
                                                        desc->slots[i].per_instance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX};
    }
    VkVertexInputAttributeDescription attributes[TIDE_GPU_MAX_ATTRIBUTES];
    for (uint32_t i = 0; i < desc->attribute_count; i++) {
        attributes[i] = (VkVertexInputAttributeDescription){i, desc->attributes[i].slot, attribute_format(desc->attributes[i].format),
                                                            desc->attributes[i].offset};
    }
    const VkPipelineVertexInputStateCreateInfo input = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, NULL, 0,
                                                        desc->slot_count, bindings, desc->attribute_count, attributes};
    const VkPipelineInputAssemblyStateCreateInfo assembly = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, NULL, 0,
                                                             VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE};
    const VkPipelineViewportStateCreateInfo viewport = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, NULL, 0, 1, NULL, 1, NULL};
    // A triangle's front goes clockwise as it's seen: with y up in clip space, by the viewport upside down
    const VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = desc->cull == TIDE_GPU_CULL_BACK ? VK_CULL_MODE_BACK_BIT : desc->cull == TIDE_GPU_CULL_FRONT ? VK_CULL_MODE_FRONT_BIT
                                                                                                          : VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_CLOCKWISE, .lineWidth = 1.0f,
    };
    const uint32_t colors = tide_gpu_target_colors(desc->into);
    const uint32_t samples = colors ? tide_gpu_target_samples(desc->into) : 1u;
    const VkPipelineMultisampleStateCreateInfo multisample = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, NULL, 0,
                                                              (VkSampleCountFlagBits)samples, VK_FALSE, 0.0f, NULL, VK_FALSE, VK_FALSE};
    static const VkCompareOp tests[] = {VK_COMPARE_OP_ALWAYS, VK_COMPARE_OP_NEVER, VK_COMPARE_OP_LESS, VK_COMPARE_OP_EQUAL,
                                        VK_COMPARE_OP_LESS_OR_EQUAL, VK_COMPARE_OP_GREATER, VK_COMPARE_OP_NOT_EQUAL,
                                        VK_COMPARE_OP_GREATER_OR_EQUAL}; // By tide_gpu_stencil_test
    static const VkStencilOp ops[] = {VK_STENCIL_OP_KEEP, VK_STENCIL_OP_REPLACE, VK_STENCIL_OP_INCREMENT_AND_CLAMP,
                                      VK_STENCIL_OP_DECREMENT_AND_CLAMP, VK_STENCIL_OP_ZERO, VK_STENCIL_OP_INVERT}; // By tide_gpu_stencil_op
    const uint32_t test = (uint32_t)desc->stencil_test < 8u ? (uint32_t)desc->stencil_test : 0u;
    const uint32_t op = (uint32_t)desc->stencil_op < 6u ? (uint32_t)desc->stencil_op : 0u;
    // Only where the draw draws: a pixel that fails either test keeps its number
    const VkStencilOpState stencil = {VK_STENCIL_OP_KEEP, ops[op], VK_STENCIL_OP_KEEP, tests[test], 0xFFu, 0xFFu, 0};
    const VkPipelineDepthStencilStateCreateInfo depth = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = desc->depth != TIDE_GPU_DEPTH_NONE,
        .depthWriteEnable = desc->depth == TIDE_GPU_DEPTH_TEST || desc->depth == TIDE_GPU_DEPTH_RESET,
        .depthCompareOp = desc->depth == TIDE_GPU_DEPTH_RESET ? VK_COMPARE_OP_ALWAYS : VK_COMPARE_OP_LESS_OR_EQUAL,
        .stencilTestEnable = !(test == TIDE_GPU_STENCIL_ALWAYS && op == TIDE_GPU_STENCIL_KEEP),
        .front = stencil, .back = stencil, .maxDepthBounds = 1.0f,
    };
    // Each of its outputs blended into the target's image in its place, the way `blend` says; the images past them left as they are
    VkPipelineColorBlendAttachmentState blends[TIDE_GPU_MAX_COLORS];
    VkFormat formats[TIDE_GPU_MAX_COLORS];
    const uint32_t outputs = desc->outputs ? desc->outputs : 1u;
    for (uint32_t i = 0; i < colors; i++) {
        formats[i] = color_format(tide_gpu_target_color(desc->into, i));
        VkPipelineColorBlendAttachmentState b = {.colorWriteMask = 0xF};
        switch (desc->blend) {
        case TIDE_GPU_BLEND_ALPHA:
            b = (VkPipelineColorBlendAttachmentState){VK_TRUE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD,
                                                      VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_OP_ADD, 0xF};
            break;
        case TIDE_GPU_BLEND_ADD:
            b = (VkPipelineColorBlendAttachmentState){VK_TRUE, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_ADD,
                                                      VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_ADD, 0xF};
            break;
        case TIDE_GPU_BLEND_MULTIPLY:
            b = (VkPipelineColorBlendAttachmentState){VK_TRUE, VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD,
                                                      VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_ADD, 0xF};
            break;
        default:
            break;
        }
        if (i >= outputs) b = (VkPipelineColorBlendAttachmentState){.colorWriteMask = 0};
        blends[i] = b;
    }
    const VkPipelineColorBlendStateCreateInfo blend = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, NULL, 0, VK_FALSE,
                                                       VK_LOGIC_OP_COPY, colors, blends, {0}};
    static const VkDynamicState dynamic[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_STENCIL_REFERENCE};
    const VkPipelineDynamicStateCreateInfo dynamics = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, NULL, 0, 3, dynamic};
    const VkPipelineRenderingCreateInfo rendering = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, NULL, 0, colors, formats,
                                                     vk.depth_format, vk.depth_format};
    const VkGraphicsPipelineCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .pNext = &rendering, .stageCount = 2, .pStages = stages,
        .pVertexInputState = &input, .pInputAssemblyState = &assembly, .pViewportState = &viewport,
        .pRasterizationState = &raster, .pMultisampleState = &multisample, .pDepthStencilState = &depth,
        .pColorBlendState = &blend, .pDynamicState = &dynamics, .layout = vk.layout,
    };
    VkPipeline pipeline;
    const VkResult result = vkCreateGraphicsPipelines(vk.device, vk.pipeline_cache, 1, &info, NULL, &pipeline);
    vkDestroyShaderModule(vk.device, module, NULL);
    if (result != VK_SUCCESS) {
        if (result == VK_ERROR_DEVICE_LOST) {
            lose(result);
            return 0;
        }
        fprintf(stderr, "tide: Vulkan made no pipeline of %s (%d)\n", name, (int)result);
        abort();
    }
    const tide_gpu_id id = pipeline_place();
    vk_pipeline *p = &vk.pipelines[id - 1u];
    *p = (vk_pipeline){.pipeline = pipeline, .texture_count = desc->texture_count, .into = desc->into};
    memcpy(p->reads, desc->reads, sizeof p->reads);
    return id;
}

static void vk_pipeline_free(const tide_gpu_id id)
{
    vk_pipeline *p = &vk.pipelines[id - 1u];
    DISCARD(VK_OBJECT_TYPE_PIPELINE, p->pipeline, NULL);
    *p = (vk_pipeline){0};
}

// ---------------------------------------------------------------------------
// Passes

// What the pass turned to draw into, and turns back as it ends.
typedef struct vk_turned {
    VkImage image;
    VkFormat format;
    uint32_t layer;
    VkImageLayout rest, drawn;
} vk_turned;

static vk_turned turned[TIDE_GPU_MAX_COLORS + 1];
static uint32_t turned_count;
static VkBuffer uniform_buffer; // Where the uniforms the draws after them read are

static void turn_for_pass(const VkImage image, const VkFormat format, const uint32_t layer, const VkImageLayout rest,
                          const VkImageLayout drawn)
{
    turn(vk.pass, image, format, 0, 1, layer, 1, rest, drawn);
    turned[turned_count++] = (vk_turned){image, format, layer, rest, drawn};
}

static void vk_begin(const tide_gpu_id target, const bool clear)
{
    vk.in_pass = true;
    vk.skip_pass = true; // Until there's something to draw in
    vk.current = 0;
    vk.bound = false;
    uniform_buffer = VK_NULL_HANDLE; // A pass's draws read the uniforms written in it
    vk.uniforms_at = 0;
    turned_count = 0;
    if (vk.lost) return;
    if (!target && !acquire()) return; // A window with nothing to draw in (minimized): its draws go nowhere
    if (vk.lost) return;
    flush_if_full();
    // What's recorded outside passes since the last one goes before it, and what's uploaded during it, after that
    close_outside();
    vk.pass = command_buffer();
    barrier_pass(vk.pass);
    vk.pass_pending = vk.pending_count;
    queue_up(vk.pass);
    vk.before = VK_NULL_HANDLE;
    vk.skip_pass = false;
    vk.pass_target = target;
    const VkAttachmentLoadOp load = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    VkRenderingAttachmentInfo colors[TIDE_GPU_MAX_COLORS];
    VkRenderingAttachmentInfo depth = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                       .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, .loadOp = load,
                                       .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .clearValue = {.depthStencil = {1.0f, 0}}};
    uint32_t color_count = 0;
    int width, height;
    const VkClearValue black = {.color = {{0.0f, 0.0f, 0.0f, 1.0f}}};
    if (!target) {
        width = (int)vk.extent.width, height = (int)vk.extent.height;
        // Its first pass in a frame starts it cleared: what was there is another frame's, or nothing
        turn(vk.pass, vk.images[vk.image], vk.window_format, 0, 1, 0, 1,
             vk.image_drawn ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        colors[0] = (VkRenderingAttachmentInfo){.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = vk.views[vk.image],
                                                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                .loadOp = vk.image_drawn ? load : VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .clearValue = black};
        color_count = 1;
        if (vk.window_depth_new) {
            turn(vk.pass, vk.window_depth, vk.depth_format, 0, 1, 0, 1, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            vk.window_depth_new = false;
            depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        }
        if (!vk.image_drawn) depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.imageView = vk.window_depth_view;
        vk.image_drawn = true;
    } else {
        const vk_target *t = &vk.targets[target - 1u];
        width = t->width, height = t->height;
        for (uint32_t i = 0; i < t->color_count; i++) {
            const vk_texture *c = t->face ? texture_of(t->cube) : texture_of(t->colors[i]);
            const VkImageView view = t->face ? c->faces[t->layer] : c->attach;
            turn_for_pass(c->image, c->format, t->face ? t->layer : 0u, c->rest, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            colors[i] = (VkRenderingAttachmentInfo){.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                                    .imageView = t->samples > 1u ? t->many_views[i] : view,
                                                    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .loadOp = load,
                                                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .clearValue = black};
            if (t->samples > 1u) { // Each image's texture is what its samples come to, as the pass ends
                colors[i].resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
                colors[i].resolveImageView = view;
                colors[i].resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            }
        }
        color_count = t->color_count;
        if (t->samples > 1u) {
            depth.imageView = t->many_depth_view;
        } else {
            const vk_texture *d = texture_of(t->depth);
            turn_for_pass(d->image, d->format, 0, d->rest, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            depth.imageView = d->attach;
        }
    }
    vk.pass_width = width, vk.pass_height = height;
    const VkRenderingInfo info = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = {{0, 0}, {(uint32_t)width, (uint32_t)height}},
                                  .layerCount = 1, .colorAttachmentCount = color_count, .pColorAttachments = colors,
                                  .pDepthAttachment = &depth, .pStencilAttachment = &depth};
    vkCmdBeginRendering(vk.pass, &info);
    // Upside down: y goes up in clip space, and rows go down from the top
    const VkViewport viewport = {0.0f, (float)height, (float)width, -(float)height, 0.0f, 1.0f};
    vkCmdSetViewport(vk.pass, 0, 1, &viewport);
    const VkRect2D all = {{0, 0}, {(uint32_t)width, (uint32_t)height}};
    vkCmdSetScissor(vk.pass, 0, 1, &all);
    vkCmdSetStencilReference(vk.pass, VK_STENCIL_FACE_FRONT_AND_BACK, 0);
}

static void vk_end(void)
{
    vk.in_pass = false;
    if (vk.skip_pass) return;
    if (vk.lost) { // Nothing recorded goes
        vk.pass = vk.before = VK_NULL_HANDLE;
        return;
    }
    vkCmdEndRendering(vk.pass);
    for (uint32_t i = 0; i < turned_count; i++) {
        turn(vk.pass, turned[i].image, turned[i].format, 0, 1, turned[i].layer, 1, turned[i].drawn, turned[i].rest);
    }
    turned_count = 0;
    if (!vk.pass_target) {
        turn(vk.pass, vk.images[vk.image], vk.window_format, 0, 1, 0, 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
             VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    }
    // What's recorded outside passes next goes on after it, in the same command buffer
    vk.outside = vk.pass;
    vk.drawn = true;
    vk.pass = VK_NULL_HANDLE;
    if (vk.before) check(vkEndCommandBuffer(vk.before), "vkEndCommandBuffer");
    vk.before = VK_NULL_HANDLE;
}

static void vk_scissor(const int x, const int y, const int width, const int height)
{
    if (vk.skip_pass) return;
    const VkRect2D rect = {{x > 0 ? x : 0, y > 0 ? y : 0}, {(uint32_t)(width > 0 ? width : 0), (uint32_t)(height > 0 ? height : 0)}};
    vkCmdSetScissor(vk.pass, 0, 1, &rect);
}

static void vk_use(const tide_gpu_id id)
{
    if (vk.skip_pass) return;
    const vk_pipeline *p = &vk.pipelines[id - 1u];
    // A pipeline is for one kind of target
    const tide_gpu_target_format kind = vk.pass_target ? vk.targets[vk.pass_target - 1u].format : TIDE_GPU_TARGET_COLOR;
    if (p->into != kind) {
        fprintf(stderr, "tide: a pipeline is for targets that hold something else than the one it draws into "
                        "(tide_gpu_pipeline_desc.into)\n");
        abort();
    }
    vk.current = id;
    vk.bound = false;
    vkCmdBindPipeline(vk.pass, VK_PIPELINE_BIND_POINT_GRAPHICS, p->pipeline);
}

static void vk_stencil(const uint32_t value)
{
    if (vk.skip_pass) return;
    vkCmdSetStencilReference(vk.pass, VK_STENCIL_FACE_FRONT_AND_BACK, value < 255u ? value : 255u);
}

static void vk_uniforms(const void *data, const size_t size)
{
    if (vk.skip_pass) return;
    uint8_t *at;
    // Every pipeline reads a block of TIDE_GPU_MAX_UNIFORMS bytes at most, from where they start
    vk.uniforms_at = ring_take(size, vk.properties.limits.minUniformBufferOffsetAlignment, TIDE_GPU_MAX_UNIFORMS, &uniform_buffer, &at);
    memcpy(at, data, size);
    vk.bound = false;
}

static void vk_sample(const uint32_t slot, const tide_gpu_id texture, const tide_gpu_filter filter, const tide_gpu_wrap wrap)
{
    if (vk.skip_pass || slot >= TIDE_GPU_MAX_TEXTURES) return;
    vk.textures_bound[slot] = texture;
    vk.sampled[slot] = (uint8_t)(((uint32_t)wrap < WRAPS ? (uint32_t)wrap : 0u) * FILTERS
                                 + ((uint32_t)filter < FILTERS ? (uint32_t)filter : (uint32_t)TIDE_GPU_FILTER_LINEAR));
    vk.bound = false;
}

static void vk_vertices(const uint32_t slot, const tide_gpu_id buffer, const size_t offset)
{
    if (vk.skip_pass) return;
    const VkDeviceSize at = offset;
    vkCmdBindVertexBuffers(vk.pass, slot, 1, &vk.buffers[buffer - 1u].buffer, &at);
}

static void vk_indices(const tide_gpu_id buffer, const bool wide)
{
    if (vk.skip_pass) return;
    vkCmdBindIndexBuffer(vk.pass, vk.buffers[buffer - 1u].buffer, 0, wide ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
}

// What a draw reads: the uniforms where they were written, and each texture
// through the sampler for how the pipeline reads it. False for a draw with
// no uniforms written in its pass, which draws nothing.
static bool bind(void)
{
    if (vk.bound) return true;
    if (!uniform_buffer) return false;
    const vk_pipeline *p = &vk.pipelines[vk.current - 1u];
    VkDescriptorBufferInfo block = {uniform_buffer, vk.uniforms_at, TIDE_GPU_MAX_UNIFORMS};
    VkDescriptorImageInfo images[TIDE_GPU_MAX_TEXTURES], samplers[TIDE_GPU_MAX_TEXTURES];
    VkWriteDescriptorSet writes[1 + 2 * TIDE_GPU_MAX_TEXTURES];
    uint32_t count = 0;
    if (uniform_buffer) {
        writes[count++] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, VK_NULL_HANDLE, 0, 0, 1,
                                                 VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, NULL, &block, NULL};
    }
    for (uint32_t i = 0; i < p->texture_count && i < TIDE_GPU_MAX_TEXTURES; i++) {
        const vk_texture *t = texture_of(vk.textures_bound[i]);
        if (!t) continue;
        const uint8_t read = p->reads[i];
        uint32_t filter = vk.sampled[i] % FILTERS, wrap = vk.sampled[i] / FILTERS;
        // Read between its smaller images before it has them, it's its own pixels, as a texture with none is
        if (filter == TIDE_GPU_FILTER_MIPMAPS && !t->mipped) filter = TIDE_GPU_FILTER_LINEAR;
        uint32_t sampler;
        if (read == TIDE_GPU_READ_COMPARE) sampler = SAMPLER_COMPARE;
        else if (read == TIDE_GPU_READ_DEPTH || read == TIDE_GPU_READ_DATA) sampler = TIDE_GPU_FILTER_NEAREST; // Clamped
        else if (read == TIDE_GPU_READ_CUBE) sampler = filter; // A cube has no edge to read past
        else sampler = wrap * FILTERS + filter;
        images[i] = (VkDescriptorImageInfo){VK_NULL_HANDLE, t->view, t->rest};
        samplers[i] = (VkDescriptorImageInfo){vk.samplers[sampler], VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
        writes[count++] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, VK_NULL_HANDLE, 1u + 2u * i, 0, 1,
                                                 VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &images[i], NULL, NULL};
        writes[count++] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, VK_NULL_HANDLE, 2u + 2u * i, 0, 1,
                                                 VK_DESCRIPTOR_TYPE_SAMPLER, &samplers[i], NULL, NULL};
    }
    if (count) vkCmdPushDescriptorSetKHR(vk.pass, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.layout, 0, count, writes);
    vk.bound = true;
    return true;
}

static void vk_draw(const uint32_t first, const uint32_t count, const uint32_t instances)
{
    if (vk.skip_pass || !vk.current || !bind()) return;
    vkCmdDraw(vk.pass, count, instances, first, 0);
}

static void vk_draw_indexed(const uint32_t count, const uint32_t instances)
{
    if (vk.skip_pass || !vk.current || !bind()) return;
    vkCmdDrawIndexed(vk.pass, count, instances, 0, 0, 0);
}

// ---------------------------------------------------------------------------
// Timing: timestamps around the game's drawing, read when they're there

static void vk_time_begin(void)
{
    if (vk.lost || !vk.timestamp_ns || vk.query_active || vk.query_count == TIME_PAIRS) return;
    if (!vk.queries) {
        const VkQueryPoolCreateInfo info = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, NULL, 0, VK_QUERY_TYPE_TIMESTAMP, 2 * TIME_PAIRS, 0};
        if (vkCreateQueryPool(vk.device, &info, NULL, &vk.queries) != VK_SUCCESS) return;
    }
    const uint32_t pair = (vk.query_first + vk.query_count) % TIME_PAIRS;
    const VkCommandBuffer cb = outside();
    vkCmdResetQueryPool(cb, vk.queries, 2u * pair, 2);
    vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, vk.queries, 2u * pair);
    vk.query_active = true;
}

static void vk_time_end(void)
{
    if (vk.lost || !vk.query_active) return;
    const uint32_t pair = (vk.query_first + vk.query_count) % TIME_PAIRS;
    vkCmdWriteTimestamp(outside(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, vk.queries, 2u * pair + 1u);
    vk.query_active = false;
    vk.query_count++;
}

static double vk_time_read(void)
{
    // Nothing timed has gone to the GPU yet, or what went isn't done: its reset may not have run, which leaves the last results there
    if (vk.lost || !vk.query_sent || vk.query_serial[vk.query_first] > vk.completed) return -1.0;
    uint64_t results[4]; // Two timestamps, each with whether it's there
    const VkResult result = vkGetQueryPoolResults(vk.device, vk.queries, 2u * vk.query_first, 2, sizeof results, results,
                                                  2 * sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if ((result != VK_SUCCESS && result != VK_NOT_READY) || !results[1] || !results[3]) return -1.0;
    vk.query_first = (vk.query_first + 1u) % TIME_PAIRS;
    vk.query_count--;
    vk.query_sent--;
    const uint64_t ticks = (results[2] - results[0]) & vk.timestamp_mask;
    return (double)ticks * vk.timestamp_ns / 1000000.0;
}

static void vk_time_reset(void)
{
    if (vk.queries) {
        flush(); // What was recorded with it goes, and is done
        vkDestroyQueryPool(vk.device, vk.queries, NULL);
    }
    vk.queries = VK_NULL_HANDLE;
    vk.query_first = vk.query_count = vk.query_sent = 0;
    vk.query_active = false;
}

// ---------------------------------------------------------------------------
// The device

static bool validating(void)
{
    const char *on = getenv("TIDE_VULKAN_VALIDATION");
    return on && *on && strcmp(on, "0") != 0;
}

// What the validation layer finds wrong stops the program, as a web test
// page's WebGL and WebGPU errors fail it.
static VKAPI_ATTR VkBool32 VKAPI_CALL on_validation(const VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                    const VkDebugUtilsMessageTypeFlagsEXT types,
                                                    const VkDebugUtilsMessengerCallbackDataEXT *data, void *user)
{
    (void)user;
    if (!(types & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) || severity < VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        return VK_FALSE;
    }
    // A color written where the target has no image for it goes nowhere, as gpu.h has it (a target of depth
    // alone, a clear of four colors into fewer): the layer says the write is unused, which it is
    static const char unused[] = "Undefined-Value-ShaderOutputNotConsumed";
    if (data->pMessageIdName && strncmp(data->pMessageIdName, unused, sizeof unused - 1u) == 0) return VK_FALSE;
    fprintf(stderr, "tide: Vulkan validation (%s): %s\n", data->pMessageIdName ? data->pMessageIdName : "", data->pMessage);
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

static bool load_device_functions(void)
{
    bool found = true;
#define VK_LOAD(name)                                                                                                  \
    name = (PFN_##name)vkGetDeviceProcAddr(vk.device, #name);                                                         \
    if (!name) found = false;
    VK_DEVICE(VK_LOAD)
#undef VK_LOAD
    return found;
}

// A format the window can have, which every target's image of bytes has too:
// blue first where the surface has that, as most do, or red first.
static VkFormat pick_window_format(void)
{
    VkSurfaceFormatKHR formats[64];
    uint32_t count = 64;
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physical, vk.surface, &count, formats) < 0) count = 0;
    VkFormat found = VK_FORMAT_UNDEFINED;
    for (uint32_t i = 0; i < count; i++) {
        if (formats[i].colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) continue;
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM) return formats[i].format;
        if (formats[i].format == VK_FORMAT_R8G8B8A8_UNORM) found = formats[i].format;
    }
    return found;
}

// ---------------------------------------------------------------------------
// The pipeline cache: what the driver compiled, kept in a file across runs
// (tide_gpu.pipeline_cache), as the driver's data after a header of Tide's,
// which says what it is, how long and its hash. A file cut short or changed
// on the disk never reaches the driver, which needn't check that. Nor does one
// for another device or driver, as its own header says: this device's is
// written over it.

enum { CACHE_HEADER = 24, CACHE_MOST = 256u << 20 };
static const char cache_magic[8] = {'T', 'I', 'D', 'E', 'P', 'S', 'O', '1'};

static uint64_t cache_hash(const uint8_t *data, const size_t size)
{
    uint64_t h = UINT64_C(0xcbf29ce484222325); // FNV-1a
    for (size_t i = 0; i < size; i++) h = (h ^ data[i]) * UINT64_C(0x100000001b3);
    return h;
}

// Whether the driver's data is for this device and driver: its header, of
// least significant bytes first, as the specification has it.
static bool cache_for_device(const uint8_t *data, const size_t size)
{
    if (size < 16u + VK_UUID_SIZE) return false;
    uint32_t length, version, vendor, device;
    memcpy(&length, data, 4);
    memcpy(&version, data + 4, 4);
    memcpy(&vendor, data + 8, 4);
    memcpy(&device, data + 12, 4);
    return length >= 16u + VK_UUID_SIZE && length <= size && version == VK_PIPELINE_CACHE_HEADER_VERSION_ONE
        && vendor == vk.properties.vendorID && device == vk.properties.deviceID
        && memcmp(data + 16, vk.properties.pipelineCacheUUID, VK_UUID_SIZE) == 0;
}

static void read_pipeline_cache(void)
{
    vk.cache_read = 0;
    vk.cache_hash = 0;
    if (!vk.cache_file || !vk.pipeline_cache) return;
    size_t size = 0;
    uint8_t *file = tide_file_read_whole(vk.cache_file, CACHE_HEADER + CACHE_MOST, &size);
    if (!file) return;
    uint64_t length = 0, hash = 0;
    if (size >= CACHE_HEADER) memcpy(&length, file + 8, 8), memcpy(&hash, file + 16, 8);
    const uint8_t *data = file + CACHE_HEADER;
    if (size >= CACHE_HEADER && memcmp(file, cache_magic, sizeof cache_magic) == 0 && length == size - CACHE_HEADER
        && hash == cache_hash(data, (size_t)length) && cache_for_device(data, (size_t)length)) {
        const VkPipelineCacheCreateInfo info = {VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO, NULL, 0, (size_t)length, data};
        VkPipelineCache read;
        if (vkCreatePipelineCache(vk.device, &info, NULL, &read) == VK_SUCCESS) {
            if (vkMergePipelineCaches(vk.device, vk.pipeline_cache, 1, &read) == VK_SUCCESS) {
                vk.cache_read = (size_t)length;
                vk.cache_hash = hash;
            }
            vkDestroyPipelineCache(vk.device, read, NULL);
        }
    }
    free(file);
}

// What the driver has now goes to the file, unless it's what was read.
static void write_pipeline_cache(void)
{
    if (!vk.cache_file || !vk.pipeline_cache || vk.lost) return;
    size_t size = 0;
    if (vkGetPipelineCacheData(vk.device, vk.pipeline_cache, &size, NULL) != VK_SUCCESS || !size || size > CACHE_MOST) return;
    uint8_t *file = tide_alloc(CACHE_HEADER + size);
    if (vkGetPipelineCacheData(vk.device, vk.pipeline_cache, &size, file + CACHE_HEADER) == VK_SUCCESS) {
        const uint64_t length = size, hash = cache_hash(file + CACHE_HEADER, size);
        memcpy(file, cache_magic, sizeof cache_magic);
        memcpy(file + 8, &length, 8);
        memcpy(file + 16, &hash, 8);
        if ((size != vk.cache_read || hash != vk.cache_hash) && tide_file_write_whole(vk.cache_file, file, CACHE_HEADER + size)) {
            vk.cache_read = size, vk.cache_hash = hash;
        }
    }
    free(file);
}

static void vk_pipeline_cache(const char *file)
{
    write_pipeline_cache(); // What was compiled goes where it was to go
    free(vk.cache_file);
    vk.cache_file = NULL;
    if (file) {
        const size_t length = strlen(file) + 1u;
        vk.cache_file = memcpy(tide_alloc(length), file, length);
    }
    read_pipeline_cache();
}

size_t tide_gpu_vk_pipeline_cache_read(void)
{
    return vk.cache_read;
}

static void destroy_device(void)
{
    if (!vk.device) return;
    vkDeviceWaitIdle(vk.device);
    for (uint32_t i = 0; i < vk.target_count; i++) {
        if (vk.targets[i].used) vk_target_free(i + 1u);
    }
    for (uint32_t i = 0; i < vk.texture_count; i++) {
        if (vk.textures[i].image) forget_texture(&vk.textures[i]);
    }
    for (uint32_t i = 0; i < vk.buffer_count; i++) {
        if (vk.buffers[i].buffer) vk_buffer_free(i + 1u);
    }
    for (uint32_t i = 0; i < vk.pipeline_count; i++) {
        if (vk.pipelines[i].pipeline) vk_pipeline_free(i + 1u);
    }
    for (uint32_t i = 0; i < FRAMES; i++) {
        vk_frame *f = &vk.frames[i];
        collect(f);
        if (f->ring.buffer) vkDestroyBuffer(vk.device, f->ring.buffer, NULL);
        give_back(&f->ring.memory);
        vkDestroyCommandPool(vk.device, f->pool, NULL);
        vkDestroyFence(vk.device, f->fence, NULL);
        vkDestroySemaphore(vk.device, f->acquired, NULL);
        free(f->buffers);
        free(f->garbage);
        *f = (vk_frame){0};
    }
    for (uint32_t i = 0; i < 8; i++) {
        if (rendered[i]) vkDestroySemaphore(vk.device, rendered[i], NULL);
        rendered[i] = VK_NULL_HANDLE;
    }
    if (readback.buffer) vkDestroyBuffer(vk.device, readback.buffer, NULL);
    give_back(&readback.memory);
    readback.buffer = VK_NULL_HANDLE, readback.memory = (vk_alloc){0}, readback.size = 0;
    if (vk.queries) vkDestroyQueryPool(vk.device, vk.queries, NULL);
    vk.queries = VK_NULL_HANDLE, vk.query_first = vk.query_count = vk.query_sent = 0, vk.query_active = false;
    vk.made_width = vk.made_height = 0;
    forget_swapchain();
    for (uint32_t i = 0; i < SAMPLER_COUNT; i++) vkDestroySampler(vk.device, vk.samplers[i], NULL);
    vkDestroyPipelineCache(vk.device, vk.pipeline_cache, NULL);
    vk.pipeline_cache = VK_NULL_HANDLE;
    vkDestroyPipelineLayout(vk.device, vk.layout, NULL);
    vkDestroyDescriptorSetLayout(vk.device, vk.set_layout, NULL);
    for (uint32_t i = 0; i < vk.block_count; i++) {
        if (vk.blocks[i].memory) vkFreeMemory(vk.device, vk.blocks[i].memory, NULL);
        free(vk.blocks[i].free);
    }
    free(vk.blocks);
    vk.blocks = NULL, vk.block_count = 0;
    vkDestroyDevice(vk.device, NULL);
    vk.device = VK_NULL_HANDLE;
    vk.pending_count = 0;
    vk.outside = vk.pass = vk.before = VK_NULL_HANDLE;
    vk.acquired = vk.in_pass = vk.drawn = false;
    uniform_buffer = VK_NULL_HANDLE;
}

static bool create_device(const bool say)
{
    VkPhysicalDevice devices[16];
    uint32_t count = 16;
    if (vkEnumeratePhysicalDevices(vk.instance, &count, devices) < 0 || !count) {
        if (say) fprintf(stderr, "tide: Vulkan has no device\n");
        return false;
    }
    int best = -1, best_score = 0;
    uint32_t best_family = 0;
    const char *why = "no device has Vulkan 1.3";
    for (uint32_t d = 0; d < count; d++) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(devices[d], &properties);
        if (properties.apiVersion < VK_API_VERSION_1_3) continue;
        VkExtensionProperties extensions[512];
        uint32_t extension_count = 512;
        vkEnumerateDeviceExtensionProperties(devices[d], NULL, &extension_count, extensions);
        if (!listed(extensions, extension_count, VK_KHR_SWAPCHAIN_EXTENSION_NAME)
            || !listed(extensions, extension_count, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME)) {
            why = "no device has push descriptors and a swapchain";
            continue;
        }
        VkPhysicalDeviceVulkan13Features v13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 features = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &v13};
        vkGetPhysicalDeviceFeatures2(devices[d], &features);
        if (!v13.dynamicRendering || !features.features.independentBlend) {
            why = "no device has dynamic rendering and independent blending";
            continue;
        }
        VkQueueFamilyProperties families[32];
        uint32_t family_count = 32;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &family_count, families);
        int family = -1;
        for (uint32_t f = 0; f < family_count && family < 0; f++) {
            VkBool32 shows = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(devices[d], f, vk.surface, &shows);
            if ((families[f].queueFlags & VK_QUEUE_GRAPHICS_BIT) && shows) family = (int)f;
        }
        if (family < 0) {
            why = "no device draws in the window";
            continue;
        }
        const int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 4
                        : properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 3
                        : properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU ? 2 : 1;
        if (score > best_score) best = (int)d, best_score = score, best_family = (uint32_t)family;
    }
    if (best < 0) {
        if (say) fprintf(stderr, "tide: Vulkan: %s\n", why);
        return false;
    }
    vk.physical = devices[best];
    vk.family = best_family;
    vkGetPhysicalDeviceProperties(vk.physical, &vk.properties);
    vkGetPhysicalDeviceMemoryProperties(vk.physical, &vk.memory);
    vk.window_format = pick_window_format();
    if (vk.window_format == VK_FORMAT_UNDEFINED) {
        if (say) fprintf(stderr, "tide: Vulkan: the window has no format of 4 bytes a pixel\n");
        return false;
    }

    VkExtensionProperties extensions[512];
    uint32_t extension_count = 512;
    vkEnumerateDeviceExtensionProperties(vk.physical, NULL, &extension_count, extensions);
    const char *enabled[3] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME};
    uint32_t enabled_count = 2;
    if (listed(extensions, extension_count, "VK_KHR_portability_subset")) enabled[enabled_count++] = "VK_KHR_portability_subset";
    VkPhysicalDeviceVulkan13Features has13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceFeatures2 has = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &has13};
    vkGetPhysicalDeviceFeatures2(vk.physical, &has);
    // What's read past a buffer's or an image's end is nothing, as on WebGPU
    VkPhysicalDeviceVulkan13Features want13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
                                               .dynamicRendering = VK_TRUE, .robustImageAccess = has13.robustImageAccess};
    VkPhysicalDeviceFeatures2 want = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &want13};
    want.features.independentBlend = VK_TRUE;
    want.features.robustBufferAccess = has.features.robustBufferAccess;
    want.features.textureCompressionBC = has.features.textureCompressionBC;
    vk.compression = has.features.textureCompressionBC;
    const float priority = 1.0f;
    const VkDeviceQueueCreateInfo queue = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, vk.family, 1, &priority};
    const VkDeviceCreateInfo info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &want, 0, 1, &queue, 0, NULL, enabled_count, enabled, NULL};
    const VkResult made = vkCreateDevice(vk.physical, &info, NULL, &vk.device);
    if (made != VK_SUCCESS) {
        if (say) say_result("vkCreateDevice", made);
        vk.device = VK_NULL_HANDLE;
        return false;
    }
    if (!load_device_functions()) {
        if (say) fprintf(stderr, "tide: Vulkan's device lacks a function this needs\n");
        vkDestroyDevice(vk.device, NULL);
        vk.device = VK_NULL_HANDLE;
        return false;
    }
    vkGetDeviceQueue(vk.device, vk.family, 0, &vk.queue);
    VkQueueFamilyProperties families[32];
    uint32_t family_count = 32;
    vkGetPhysicalDeviceQueueFamilyProperties(vk.physical, &family_count, families);
    vk.timestamp_ns = families[vk.family].timestampValidBits && vk.properties.limits.timestampComputeAndGraphics
                    ? (double)vk.properties.limits.timestampPeriod : 0.0;
    const uint32_t bits = families[vk.family].timestampValidBits;
    vk.timestamp_mask = bits >= 64u ? UINT64_MAX : bits ? (UINT64_C(1) << bits) - 1u : 0u;

    // Depth and stencil, which every target has, and which a shader samples as its depth
    const VkFormat depths[] = {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT};
    vk.depth_format = VK_FORMAT_UNDEFINED;
    for (size_t i = 0; i < sizeof depths / sizeof depths[0] && !vk.depth_format; i++) {
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(vk.physical, depths[i], &properties);
        const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if ((properties.optimalTilingFeatures & need) == need) {
            vk.depth_format = depths[i];
            vk.depth_linear = properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        }
    }
    if (!vk.depth_format) {
        if (say) fprintf(stderr, "tide: Vulkan's device has no depth and stencil to sample\n");
        destroy_device();
        return false;
    }

    // One layout for every pipeline: the uniforms, then each texture and its sampler, pushed with each draw
    VkDescriptorSetLayoutBinding bindings[1 + 2 * TIDE_GPU_MAX_TEXTURES];
    const VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[0] = (VkDescriptorSetLayoutBinding){0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, stages, NULL};
    for (uint32_t i = 0; i < TIDE_GPU_MAX_TEXTURES; i++) {
        bindings[1 + 2 * i] = (VkDescriptorSetLayoutBinding){1 + 2 * i, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, stages, NULL};
        bindings[2 + 2 * i] = (VkDescriptorSetLayoutBinding){2 + 2 * i, VK_DESCRIPTOR_TYPE_SAMPLER, 1, stages, NULL};
    }
    const VkDescriptorSetLayoutCreateInfo set = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL,
                                                 VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
                                                 1 + 2 * TIDE_GPU_MAX_TEXTURES, bindings};
    check(vkCreateDescriptorSetLayout(vk.device, &set, NULL, &vk.set_layout), "vkCreateDescriptorSetLayout");
    const VkPipelineLayoutCreateInfo layout = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 1, &vk.set_layout, 0, NULL};
    check(vkCreatePipelineLayout(vk.device, &layout, NULL, &vk.layout), "vkCreatePipelineLayout");

    // Samplers: nearest, blended, and between smaller images too, each clamped, repeated or mirrored past
    // the edges. The first two keep to a texture's own pixels whatever it has
    static const VkSamplerAddressMode wraps[WRAPS] = {VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_REPEAT,
                                                      VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT};
    for (uint32_t wrap = 0; wrap < WRAPS; wrap++) {
        for (uint32_t filter = 0; filter < FILTERS; filter++) {
            const VkFilter between = filter == TIDE_GPU_FILTER_NEAREST ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
            const VkSamplerCreateInfo info = {
                .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .magFilter = between, .minFilter = between,
                .mipmapMode = filter == TIDE_GPU_FILTER_MIPMAPS ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST,
                .addressModeU = wraps[wrap], .addressModeV = wraps[wrap], .addressModeW = wraps[wrap],
                .maxLod = filter == TIDE_GPU_FILTER_MIPMAPS ? VK_LOD_CLAMP_NONE : 0.0f,
            };
            check(vkCreateSampler(vk.device, &info, NULL, &vk.samplers[wrap * FILTERS + filter]), "vkCreateSampler");
        }
    }
    // 1 where the depth that's stored isn't nearer than the shader's number, and that blended between pixels
    const VkFilter compared = vk.depth_linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    const VkSamplerCreateInfo compare = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .magFilter = compared, .minFilter = compared,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST, .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .compareEnable = VK_TRUE, .compareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
    };
    check(vkCreateSampler(vk.device, &compare, NULL, &vk.samplers[SAMPLER_COMPARE]), "vkCreateSampler");

    for (uint32_t i = 0; i < FRAMES; i++) {
        vk_frame *f = &vk.frames[i];
        const VkCommandPoolCreateInfo pool = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL,
                                              VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, vk.family};
        check(vkCreateCommandPool(vk.device, &pool, NULL, &f->pool), "vkCreateCommandPool");
        const VkFenceCreateInfo fence = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, NULL, 0};
        check(vkCreateFence(vk.device, &fence, NULL, &f->fence), "vkCreateFence");
        const VkSemaphoreCreateInfo semaphore = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, NULL, 0};
        check(vkCreateSemaphore(vk.device, &semaphore, NULL, &f->acquired), "vkCreateSemaphore");
    }
    const VkPipelineCacheCreateInfo cache = {VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO, NULL, 0, 0, NULL};
    check(vkCreatePipelineCache(vk.device, &cache, NULL, &vk.pipeline_cache), "vkCreatePipelineCache");
    read_pipeline_cache();
    vk.frame = 0;
    vk.stale = true;
    vk.epoch++;
    return true;
}

static void close_instance(void)
{
    if (vk.surface) vkDestroySurfaceKHR(vk.instance, vk.surface, NULL);
    if (vk.messenger && vkDestroyDebugUtilsMessengerEXT) vkDestroyDebugUtilsMessengerEXT(vk.instance, vk.messenger, NULL);
    if (vk.instance) vkDestroyInstance(vk.instance, NULL);
    vk.surface = VK_NULL_HANDLE;
    vk.messenger = VK_NULL_HANDLE;
    vk.instance = VK_NULL_HANDLE;
}

bool tide_gpu_vk_open(const tide_vk_window *window, const bool say)
{
    memset(&vk, 0, sizeof vk);
    vk.window = *window;
    get_instance_proc = window->get_instance_proc;
#define VK_LOAD(name) name = (PFN_##name)get_instance_proc(VK_NULL_HANDLE, #name);
    VK_GLOBAL(VK_LOAD)
#undef VK_LOAD
    uint32_t version = VK_API_VERSION_1_0;
    if (!vkCreateInstance || !vkEnumerateInstanceVersion || vkEnumerateInstanceVersion(&version) != VK_SUCCESS
        || version < VK_API_VERSION_1_3) {
        if (say) fprintf(stderr, "tide: this machine's Vulkan isn't 1.3\n");
        return false;
    }
    const bool validate = validating();
    if (validate) {
        VkLayerProperties layers[64];
        uint32_t count = 64;
        vkEnumerateInstanceLayerProperties(&count, layers);
        bool found = false;
        for (uint32_t i = 0; i < count; i++) found |= strcmp(layers[i].layerName, "VK_LAYER_KHRONOS_validation") == 0;
        if (!found) {
            fprintf(stderr, "tide: TIDE_VULKAN_VALIDATION is set, and Vulkan has no VK_LAYER_KHRONOS_validation to check with\n");
            exit(1);
        }
    }
    VkExtensionProperties available[256];
    uint32_t available_count = 256;
    vkEnumerateInstanceExtensionProperties(NULL, &available_count, available);
    const char *extensions[32];
    uint32_t extension_count = 0;
    for (uint32_t i = 0; i < window->extension_count && extension_count < 28; i++) extensions[extension_count++] = window->extensions[i];
    if (validate) extensions[extension_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    // ...which checks synchronization too, where the layer can: what one command writes and another reads
    // with no barrier between them, which no driver's own checks see
    const VkValidationFeatureEnableEXT sync = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    const VkValidationFeaturesEXT features = {VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT, NULL, 1, &sync, 0, NULL};
    bool sync_checked = false;
    if (validate) {
        VkExtensionProperties layer[64];
        uint32_t layer_count = 64;
        if (vkEnumerateInstanceExtensionProperties("VK_LAYER_KHRONOS_validation", &layer_count, layer) < 0) layer_count = 0;
        sync_checked = listed(layer, layer_count, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
        if (sync_checked) extensions[extension_count++] = VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME;
    }
    VkInstanceCreateFlags flags = 0;
    if (listed(available, available_count, "VK_KHR_portability_enumeration")) { // MoltenVK's devices are listed only so
        extensions[extension_count++] = "VK_KHR_portability_enumeration";
        flags |= 0x00000001; // VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR
    }
    const VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "Tide", 1, "Tide", 1, VK_API_VERSION_1_3};
    const char *const validation_layer = "VK_LAYER_KHRONOS_validation";
    const VkInstanceCreateInfo info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, sync_checked ? &features : NULL, flags, &app,
                                       validate ? 1u : 0u,
                                       &validation_layer, extension_count, extensions};
    const VkResult made = vkCreateInstance(&info, NULL, &vk.instance);
    if (made != VK_SUCCESS) {
        if (say) say_result("vkCreateInstance", made);
        vk.instance = VK_NULL_HANDLE;
        return false;
    }
    bool found = true;
#define VK_LOAD(name)                                                                                                  \
    name = (PFN_##name)get_instance_proc(vk.instance, #name);                                                         \
    if (!name) found = false;
    VK_INSTANCE(VK_LOAD)
#undef VK_LOAD
#define VK_LOAD(name) name = (PFN_##name)get_instance_proc(vk.instance, #name);
    VK_INSTANCE_OPTIONAL(VK_LOAD)
#undef VK_LOAD
    if (!found) {
        if (say) fprintf(stderr, "tide: Vulkan lacks a function this needs\n");
        close_instance();
        return false;
    }
    if (validate && vkCreateDebugUtilsMessengerEXT) {
        const VkDebugUtilsMessengerCreateInfoEXT messenger = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT, .pfnUserCallback = on_validation,
        };
        vkCreateDebugUtilsMessengerEXT(vk.instance, &messenger, NULL, &vk.messenger);
    }
    const VkResult surface = window->surface(vk.instance, &vk.surface);
    if (surface != VK_SUCCESS) {
        if (say) say_result("making the window's surface", surface);
        vk.surface = VK_NULL_HANDLE;
        close_instance();
        return false;
    }
    if (!create_device(say)) {
        close_instance();
        return false;
    }
    vk.open = true;
    return true;
}

void tide_gpu_vk_close(void)
{
    if (!vk.open) return;
    if (vk.device) {
        vkDeviceWaitIdle(vk.device);
        write_pipeline_cache();
    }
    destroy_device();
    free(vk.cache_file);
    close_instance();
    free(vk.buffers);
    free(vk.textures);
    free(vk.pipelines);
    free(vk.targets);
    free(vk.pending);
    memset(&vk, 0, sizeof vk);
}

bool tide_gpu_vk_timestamps(void)
{
    return vk.device && vk.timestamp_ns > 0.0;
}

// The device there is: another once a lost one is made again.
static uint32_t vk_epoch(void)
{
    if (!vk.open) return 0;
    if (vk.lost) {
        destroy_device();
        vk.lost = false;
        if (!create_device(true)) {
            vk.lost = true;
            return 0;
        }
    }
    return vk.epoch;
}

const tide_gpu tide_gpu_vulkan = {
    .name = "Vulkan",
    .epoch = vk_epoch,
    .pipeline_cache = vk_pipeline_cache,
    .time_begin = vk_time_begin,
    .time_end = vk_time_end,
    .time_read = vk_time_read,
    .time_reset = vk_time_reset,
    .buffer = vk_buffer_make,
    .buffer_write = vk_buffer_write,
    .buffer_free = vk_buffer_free,
    .texture = vk_texture_make,
    .texture_blocks = vk_texture_blocks,
    .texture_write = vk_texture_write,
    .texture_write_part = vk_texture_write_part,
    .texture_free = vk_texture_free,
    .cube = vk_cube,
    .cube_face = vk_cube_face,
    .mipmaps = vk_mipmaps,
    .pipeline = vk_pipeline_make,
    .pipeline_free = vk_pipeline_free,
    .target = vk_target_make,
    .target_limit = vk_target_limit,
    .target_samples = vk_target_samples,
    .float_targets = vk_float_targets,
    .target_texture = vk_target_texture,
    .target_depth = vk_target_depth,
    .resolve_depth = vk_resolve_depth,
    .target_free = vk_target_free,
    .target_read = vk_target_read,
    .begin = vk_begin,
    .end = vk_end,
    .scissor = vk_scissor,
    .use = vk_use,
    .stencil = vk_stencil,
    .uniforms = vk_uniforms,
    .sample = vk_sample,
    .vertices = vk_vertices,
    .indices = vk_indices,
    .draw = vk_draw,
    .draw_indexed = vk_draw_indexed,
};

