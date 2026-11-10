// Vulkan under the renderer (see gpu.h and gpu_vk.h), on desktop: Vulkan 1.3
// with push descriptors, as WebGPU's model has it underneath, so the renderer
// goes through it as it goes through WebGPU's:
//
// - Shaders are SPIR-V that tidec translated from the WGSL (spirv.c), so a
//   pipeline's bindings are WebGPU's: set 0, the uniforms at binding 0,
//   texture i and its sampler at 1 + 2i and 2 + 2i (where it has one of its
//   own), and storage buffer i at 33 + i. Each pipeline's layout has the
//   bindings it reads (layout_for), and a draw or a dispatch pushes what it's
//   given there, or writes a set of the frame's where its layout has more
//   descriptors than the device pushes.
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
#include "tide/gpu_profile.h"
#include "tide/platform.h" // tide_platform_log_note, tide_platform_jobs
#include "tide/worker.h"   // tide_background

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
    X(vkCmdDrawIndexed) X(vkCreatePipelineCache) X(vkDestroyPipelineCache) X(vkGetPipelineCacheData)               \
    X(vkMergePipelineCaches) X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkResetDescriptorPool)          \
    X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) X(vkCmdBindDescriptorSets) X(vkGetFenceStatus)        \
    X(vkCmdFillBuffer) X(vkCreateComputePipelines) X(vkCmdDispatch) X(vkCmdDispatchIndirect) X(vkCmdDrawIndirect)   \
    X(vkCmdDrawIndexedIndirect) X(vkCmdDrawIndexedIndirectCount)

#define VK_DEVICE_OPTIONAL(X) X(vkCmdPushDescriptorSetKHR)

#define VK_DECLARE(name) static PFN_##name name;
VK_GLOBAL(VK_DECLARE)
VK_INSTANCE(VK_DECLARE)
VK_INSTANCE_OPTIONAL(VK_DECLARE)
VK_DEVICE(VK_DECLARE)
VK_DEVICE_OPTIONAL(VK_DECLARE)
#undef VK_DECLARE

static PFN_vkGetInstanceProcAddr get_instance_proc;

// ---------------------------------------------------------------------------
// What there is

enum { FRAMES = 2 };                 // Frames in flight
enum { TIME_PAIRS = 4 };             // Pairs of timestamps waiting to be read
enum { BLOCK_BYTES = 64u << 20 };    // Device memory is taken in blocks this big...
enum { DEDICATED_BYTES = 16u << 20 }; // ...and an allocation this big or bigger on its own
enum { RING_BYTES = 4u << 20 };      // A frame's uploads and uniforms start with this much room
enum { UPLOAD_LOCAL_BYTES = 32u << 20 }; // ...in the GPU's memory up to this much (allocate)

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
    bool storage; // TIDE_GPU_STORAGE: bound as storage, read as indirect numbers, copied and read back
    bool indices; // TIDE_GPU_INDICES: copied from too (buffer_copy)
} vk_buffer;

typedef struct vk_texture {
    VkImage image; // VK_NULL_HANDLE: a free place
    vk_alloc memory;
    VkImageView view;   // What's sampled: all of it (a cube as a cube), its depth alone for a depth
    VkImageView attach; // What's drawn into: its first image (each face's, for a cube: faces[i])
    VkImageView faces[TIDE_GPU_CUBE_FACES];
    VkImageView base; // A cube's or an array's first images alone, which it's sampled through until its smaller ones are made
    bool arrayed;     // An array (tide_gpu.array): `layers` of them, viewed as one, and each its own to draw into...
    VkImageView *layer_views; // ...through these, a view of each layer's first image (`layers` of them)
    tide_gpu_id depth_array;  // An array that's drawn into: its depth array (array_depth), freed with it
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

struct vk_making;

typedef struct vk_pipeline {
    bool used;           // false: a free place
    VkPipeline pipeline; // VK_NULL_HANDLE while it's being made, or once making it lost the device
    struct vk_making *making; // ...on a thread of the pool's (vk_background), till it's settled
    uint32_t texture_count;
    uint8_t reads[TIDE_GPU_MAX_TEXTURES];
    uint32_t sampler_count;                       // The bindings its samplers span (tide_gpu_samplers)...
    uint8_t sampler_reads[TIDE_GPU_MAX_TEXTURES]; // ...and how each reads: TIDE_GPU_READ_DATA for none
    tide_gpu_target_format into;
    bool compute;           // Used in compute passes, and never in a pass
    uint32_t storage_count; // The storage buffers it binds
    uint32_t layout;        // Its place in vk.layouts
} vk_pipeline;

// A pipeline's layout: binding 0, its textures' at 1 + 2i, its samplers' at 2 + 2j, and its storage buffers', each
// pipeline with the same bindings sharing one. Pushed with each draw and dispatch where the
// device pushes that many descriptors (maxPushDescriptors, 32 at the least), and otherwise a set of the frame's.
typedef struct vk_layout {
    uint32_t textures, own, storage; // own: bit j for sampler j
    bool push;
    VkDescriptorSetLayout set;
    VkPipelineLayout layout;
} vk_layout;

typedef struct vk_target {
    bool used;
    tide_gpu_target_format format;
    int width, height;
    uint32_t samples;
    uint32_t color_count;
    tide_gpu_id colors[TIDE_GPU_MAX_COLORS]; // Textures: what's sampled and read back, and a multisampled one's resolves
    tide_gpu_id depth;                        // A texture: 0 for a multisampled one's until its depth is resolved
    tide_gpu_id far;                          // A multisampled one's farthest depth of each pixel's samples (depth_now)
    // A multisampled one's images of samples, which passes draw into
    VkImage many[TIDE_GPU_MAX_COLORS], many_depth;
    VkImageView many_views[TIDE_GPU_MAX_COLORS], many_depth_view;
    vk_alloc many_memory[TIDE_GPU_MAX_COLORS], many_depth_memory;
    // ...whose depth's samples a kernel reads (depth_now) where the device samples a depth of so many: a texture
    // that's its image seen as depth alone, and whether the image is turned for reading till its next use as depth
    bool samples_read;
    tide_gpu_id samples_texture;
    VkImageView samples_view;
    bool samples_turned;
    bool bytes;       // Its first color image is 4 bytes a pixel: what's read back
    bool face;        // A cube's face, or an array's layer
    tide_gpu_id cube; // ...of this cube or array
    uint32_t layer;   // ...this one
    bool array;              // An array's layer: its depth is that layer of...
    tide_gpu_id depth_layers; // ...this depth array
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

// A command pool for passes recorded on the pool's threads (record_pass): one thread at a time has it (`state`)
enum { PASS_POOLS = 64 };
enum { POOL_NONE, POOL_FREE, POOL_TAKEN, POOL_MAKING };
typedef struct vk_pass_pool {
    uint32_t state; // POOL_*, changed atomically
    VkCommandPool pool;
    VkCommandBuffer *buffers;
    uint32_t allocated, used;
} vk_pass_pool;

typedef struct vk_frame {
    vk_pass_pool pass_pools[PASS_POOLS];
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
    // Where there are no push descriptors: the sets the frame's draws read, from pools it resets with its room
    VkDescriptorPool *pools;
    uint32_t pool_count, pool_used; // ...of which those before `pool_used` are full
    bool pool_used_any;             // ...and the one at `pool_used` has sets taken
} vk_frame;

// A storage buffer's bytes on their way back (tide_gpu.buffer_read): copied
// into memory the host reads, there once the submission they went with is done.
typedef struct vk_read {
    uint32_t token;    // 0: a free place
    bool lost;         // The device went before they were there
    VkBuffer buffer;   // VK_NULL_HANDLE once let go of
    vk_alloc memory;
    VkDeviceSize size, room; // What's read, and what the buffer holds
    uint64_t serial;   // The submission the copy goes with
} vk_read;

// Samplers: by tide_gpu_wrap, then tide_gpu_filter; then the one that compares, and the nearest one that clamps.
enum { WRAPS = TIDE_GPU_WRAP_MIRROR + 1, FILTERS = TIDE_GPU_FILTER_MIPMAPS + 1 };
enum { SAMPLER_COMPARE = WRAPS * FILTERS, SAMPLER_COUNT };

#define NO_PUSH UINT32_MAX // vk.pushed before a command buffer's first push

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
    bool alpha_to_one;  // Coverage alpha becomes opaque after selecting samples
    uint32_t indirect;  // What indirect draws can do (gpu.h, TIDE_GPU_INDIRECT_*)
    bool multi_draw;    // ...and vkCmdDrawIndexedIndirect draws many records at once (multiDrawIndirect)
    bool depth_linear;  // A depth is compared blended between its pixels
    VkFormat window_format; // The window's, which every color image of 4 bytes a pixel has too
    VkFormat depth_format;
    uint32_t epoch;
    bool lost;
    bool background; // Pipelines are made on the pool's threads (tide_gpu.background)
    bool push; // Draws push what they read (VK_KHR_push_descriptor), else write sets of the frame's...
    uint32_t max_push; // ...where their layout has this many descriptors at the most (maxPushDescriptors)
    bool depth_max; // A multisampled depth resolves to the farthest of its samples (VK_RESOLVE_MODE_MAX_BIT): depth_now
    double timestamp_ns; // A timestamp's tick, in nanoseconds: 0 for none

    // The window's images
    VkSwapchainKHR swapchain;
    VkImage images[8];
    VkImageView views[8];
    uint32_t image_count;
    VkExtent2D extent;
    bool stale;                  // To be made again before the next frame draws in it
    bool turned;                 // Made upright for a screen that's turned, which the system turns as it shows it
    VkImage window_depth;
    VkImageView window_depth_view;
    vk_alloc window_depth_memory;
    bool window_depth_new;       // Its layout is still undefined
    tide_gpu_id window_far;      // A copy of its depth, for occlusion culling to measure (depth_now)...
    uint32_t window_far_epoch;   // ...made on this device
    uint32_t image;              // The image the frame draws in...
    bool acquired;               // ...once it has one
    bool acquire_waited;         // ...and a submission waits for it
    bool image_drawn;            // ...and a pass drew in it

    vk_frame frames[FRAMES];
    uint32_t frame;
    VkFence flushed; // What a flush's submission signals (see flush)
    vk_layout *layouts;
    uint32_t layout_count;
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

    // Passes recorded on the pool's threads: each graphics pass's commands are written down as it goes
    // (`recording`, pc_*) and recorded into its command buffer by a job as it ends; its place in `pending` holds a
    // tag (pass_tag) until settle_passes puts the command buffer there, before anything is submitted
    struct vk_deferred **deferred;
    uint32_t deferred_count, deferred_capacity;
    struct vk_deferred *recording;

    // The pass
    bool in_pass, skip_pass;
    bool in_compute; // A compute pass, whose command buffer is `pass` as a pass's is
    bool dispatched; // ...and it dispatched: the next dispatch waits for what that wrote
    tide_gpu_id storage_bound[TIDE_GPU_MAX_STORAGE];
    tide_gpu_id pass_target; // 0 for the window
    int pass_width, pass_height;
    tide_gpu_id current; // The pipeline the pass uses: 0 for none
    bool bound; // The pass's descriptors are what the draws after it read
    VkDeviceSize uniforms_at;
    tide_gpu_id textures_bound[TIDE_GPU_MAX_TEXTURES];
    uint8_t sampled[TIDE_GPU_MAX_TEXTURES];
    // What the pass's command buffer was last pushed, through the layout `pushed` (NO_PUSH: nothing yet). Push
    // descriptors are updated incrementally while the layout stays, so a draw pushes only what changed: its uniforms'
    // place, usually, and the textures, samplers and storage that aren't the last draw's.
    uint32_t pushed;
    VkBuffer pushed_uniforms;
    VkDeviceSize pushed_uniforms_at;
    VkImageView pushed_views[TIDE_GPU_MAX_TEXTURES];
    VkImageLayout pushed_view_layouts[TIDE_GPU_MAX_TEXTURES];
    VkSampler pushed_samplers[TIDE_GPU_MAX_TEXTURES];
    VkBuffer pushed_storage[TIDE_GPU_MAX_STORAGE];
    VkDeviceSize pushed_storage_range[TIDE_GPU_MAX_STORAGE];

    // Timing
    VkQueryPool pass_queries;
    uint32_t pass_first, pass_count, pass_sent;
    uint32_t pass_tokens[TIDE_GPU_PROFILE_PENDING];
    uint64_t pass_serial[TIDE_GPU_PROFILE_PENDING];
    bool pass_active;
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
    struct cache_reading *reading; // The file being read on a thread of the pool's, until it's taken (cache_take)
    uint64_t cache_hash; // ...and their hash: what's written as the backend closes, unless it's that again
    bool lose_asked;                   // For tests (tide_gpu_vk_lose): lost as the frame is shown

    // Storage read back (tide_gpu.buffer_read), and buffers of reads that are done, kept for the next
    vk_read *reads;
    uint32_t read_count, read_capacity;
    uint32_t read_token; // The last given
    vk_read spare_reads[4];
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
    // A frame's uploads and uniforms (its ring) in the GPU's own memory where the host can write that, as a
    // desktop GPU lets it through its BAR (all of its memory with resizable BAR, else a window of 256 MB) and an
    // integrated one does anyway: copies out of it and uniforms then don't cross the bus as the GPU runs.
    // Rings up to UPLOAD_LOCAL_BYTES, so two frames take a fraction of such a window;
    // bigger ones (a big texture's upload), or where it's full, are in the host's memory.
    if (use == USE_UPLOAD && needs.size <= UPLOAD_LOCAL_BYTES) {
        const uint32_t local = memory_type(needs.memoryTypeBits, want | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        const VkMemoryAllocateInfo info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, needs.size, local};
        if (local != UINT32_MAX && vkAllocateMemory(vk.device, &info, NULL, &out->memory) == VK_SUCCESS) {
            out->size = needs.size;
            if (vkMapMemory(vk.device, out->memory, 0, VK_WHOLE_SIZE, 0, &out->mapped) == VK_SUCCESS) return true;
            vkFreeMemory(vk.device, out->memory, NULL);
            *out = (vk_alloc){0};
        }
    }
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
    DRAW_STAGES = VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT
                | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
    ATTACHMENT_STAGES = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT
                      | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    ATTACHMENT_WRITES = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
    ATTACHMENT_ACCESS = ATTACHMENT_WRITES | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
    DRAW_READS = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT
               | VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT,
    // What a dispatch reads and writes, its size where it's indirect too
    DISPATCH_STAGES = VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    DISPATCH_ACCESS = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT
                    | VK_ACCESS_SHADER_WRITE_BIT,
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

// A compute pass begins once everything before it is done: transfers, draws
// (whose attachments it may sample) and the compute passes before it, which
// may have read what it writes as well as written what it reads.
static void barrier_compute(const VkCommandBuffer cb)
{
    barrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT | DRAW_STAGES | ATTACHMENT_STAGES | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT | ATTACHMENT_WRITES, DISPATCH_STAGES, DISPATCH_ACCESS);
}

// A dispatch after another in a compute pass, which sees what that one wrote,
// and writes nothing it was still reading.
static void barrier_dispatches(const VkCommandBuffer cb)
{
    barrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, DISPATCH_STAGES, DISPATCH_ACCESS);
}

// What a compute pass wrote, for everything after it: draws' numbers,
// indices, vertices and shaders, compute passes, copies and writes.
static void barrier_computed(const VkCommandBuffer cb)
{
    barrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
            DRAW_STAGES | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
            DRAW_READS | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
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
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: // Sampled by draws and dispatches alike
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
        *stages = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        *access = VK_ACCESS_SHADER_READ_BIT;
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

// A frame's descriptor sets, where there are no push descriptors: from pools
// of 256 sets each, as many as its draws use, which it resets with its room
// once the GPU is done with it.
enum { POOL_SETS = 256 };

static void reset_pools(vk_frame *f)
{
    const uint32_t used = f->pool_used + (f->pool_used_any ? 1u : 0u);
    for (uint32_t i = 0; i < used && i < f->pool_count; i++) vkResetDescriptorPool(vk.device, f->pools[i], 0);
    f->pool_used = 0;
    f->pool_used_any = false;
}

// A set of the frame's, for a draw, in `layout`: VK_NULL_HANDLE where there's no memory for one.
static VkDescriptorSet frame_set(const VkDescriptorSetLayout layout)
{
    vk_frame *f = &vk.frames[vk.frame];
    for (;;) {
        if (f->pool_used == f->pool_count) {
            const VkDescriptorPoolSize sizes[] = {
                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, POOL_SETS},
                {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, POOL_SETS * TIDE_GPU_MAX_TEXTURES},
                {VK_DESCRIPTOR_TYPE_SAMPLER, POOL_SETS * TIDE_GPU_MAX_TEXTURES},
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, POOL_SETS * TIDE_GPU_MAX_STORAGE},
            };
            const VkDescriptorPoolCreateInfo info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, POOL_SETS, 4, sizes};
            VkDescriptorPool pool;
            if (!check(vkCreateDescriptorPool(vk.device, &info, NULL, &pool), "vkCreateDescriptorPool")) return VK_NULL_HANDLE;
            f->pools = tide_realloc(f->pools, f->pool_count * sizeof(VkDescriptorPool), (f->pool_count + 1u) * sizeof(VkDescriptorPool));
            f->pools[f->pool_count++] = pool;
        }
        const VkDescriptorSetAllocateInfo info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, f->pools[f->pool_used], 1,
                                                  &layout};
        VkDescriptorSet set;
        const VkResult result = vkAllocateDescriptorSets(vk.device, &info, &set);
        if (result == VK_SUCCESS) {
            f->pool_used_any = true;
            return set;
        }
        if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL) {
            check(result, "vkAllocateDescriptorSets");
            return VK_NULL_HANDLE;
        }
        f->pool_used++; // That one's full: the next
        f->pool_used_any = false;
    }
}

// Everything recorded goes to the GPU, and is waited for: for what's read
// back, and frames that never show (a test's draws). The frame starts again,
// with all its room.
static void settle_passes(void);
static void reset_pass_pools(vk_frame *f);

static void flush(void)
{
    settle_passes();
    if (vk.lost) return;
    close_outside();
    VkResult result = VK_SUCCESS;
    if (vk.pending_count) {
        const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        const bool wait = vk.acquired && vk.image_drawn && !vk.acquire_waited;
        const VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, wait ? 1u : 0u, &vk.frames[vk.frame].acquired, &stage,
                                     vk.pending_count, vk.pending, 0, NULL};
        if (wait) vk.acquire_waited = true;
        result = vkQueueSubmit(vk.queue, 1, &submit, vk.flushed);
        vk.pending_count = 0;
        vk.submitted++;
        for (uint32_t i = vk.query_sent; i < vk.query_count; i++) vk.query_serial[(vk.query_first + i) % TIME_PAIRS] = vk.submitted;
        vk.query_sent = vk.query_count;
        for (uint32_t i = vk.pass_sent; i < vk.pass_count; i++)
            vk.pass_serial[(vk.pass_first + i) % TIDE_GPU_PROFILE_PENDING] = vk.submitted;
        vk.pass_sent = vk.pass_count;
        // ...and waited for, with what the frames before it sent: a fence a submission signals waits for everything
        // submitted before it too
        if (result == VK_SUCCESS) result = vkWaitForFences(vk.device, 1, &vk.flushed, VK_TRUE, UINT64_MAX);
        if (result == VK_SUCCESS) result = vkResetFences(vk.device, 1, &vk.flushed);
    }
    // Everything the GPU had is done, once the frames sent before are (every submission but a flush's has a frame's
    // fence): every frame's room, garbage and command buffers are free again
    for (uint32_t i = 0; i < FRAMES; i++) {
        vk_frame *f = &vk.frames[i];
        if (f->fenced) {
            if (result == VK_SUCCESS) result = vkWaitForFences(vk.device, 1, &f->fence, VK_TRUE, UINT64_MAX);
            vkResetFences(vk.device, 1, &f->fence);
            f->fenced = false;
        }
    }
    if (result != VK_SUCCESS) {
        lose(result);
        return;
    }
    vk.completed = vk.submitted;
    for (uint32_t i = 0; i < FRAMES; i++) {
        vk_frame *f = &vk.frames[i];
        collect(f);
        if (f->used) vkResetCommandPool(vk.device, f->pool, 0); // Unless none of its command buffers was begun since
        f->used = 0;
        reset_pass_pools(f);
        f->ring.used = 0;
        reset_pools(f);
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
    // A screen that's turned from how it's made (a phone on its side) says so in its transform, and its extent
    // is as it's made. Drawn as the window is, upright at its own size, the system turns it as it shows it:
    // it then says the swapchain is less than it could be, which is so, and that's all it means
    VkSurfaceTransformFlagBitsKHR transform = caps.currentTransform;
    vk.turned = transform != VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
             && (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR);
    if (vk.turned) {
        transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        extent.width = UINT32_MAX; // The window's, below
    }
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
        .preTransform = transform,
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
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, // ...copied by depth_now
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
    if (!vk.surface) return false; // The window went (an Android app in the background), and none came back yet
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
            if (result == VK_SUBOPTIMAL_KHR && !vk.turned) vk.stale = true; // Shown this frame, made again for the next
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

static VkResult (*wait_hook)(uint64_t timeout, VkResult (*wait)(uint64_t timeout));

void tide_gpu_vk_wait_for_test(VkResult (*hook)(uint64_t timeout, VkResult (*wait)(uint64_t timeout)))
{
    wait_hook = hook;
}

static VkResult wait_device_frame(const uint64_t timeout)
{
    return vkWaitForFences(vk.device, 1, &vk.frames[vk.frame].fence, VK_TRUE, timeout);
}

static VkResult wait_frame_for(const uint64_t timeout)
{
    if (!wait_hook) return wait_device_frame(timeout);
    const VkResult result = wait_hook(timeout, wait_device_frame);
    return result == VK_SUCCESS ? wait_device_frame(0) : result;
}

static VkResult wait_frame(double (*update)(void))
{
    if (!update) return wait_frame_for(UINT64_MAX);
    VkResult result = wait_frame_for(0);
    while (result == VK_TIMEOUT) {
        double seconds = update();
        uint64_t timeout = UINT64_MAX;
        if (!(seconds < 0.0)) {
            // Follow the host's next tick, bounded even for an invalid or
            // overdue deadline. The fence wakes us early when it completes.
            if (!(seconds >= 0.001)) seconds = 0.001;
            if (seconds > 0.1) seconds = 0.1;
            timeout = (uint64_t)(seconds * 1e9);
        }
        result = wait_frame_for(timeout);
    }
    return result;
}

void tide_gpu_vk_present(void)
{
    tide_gpu_vk_present_updating(NULL);
}

void tide_gpu_vk_present_updating(double (*update)(void))
{
    if (!vk.open || !vk.device) return;
    settle_passes(); // Before anything is submitted, or what was recorded is let go
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
        for (uint32_t i = vk.pass_sent; i < vk.pass_count; i++)
            vk.pass_serial[(vk.pass_first + i) % TIDE_GPU_PROFILE_PENDING] = vk.submitted;
        vk.pass_sent = vk.pass_count;
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
        if (result == VK_ERROR_OUT_OF_DATE_KHR || (result == VK_SUBOPTIMAL_KHR && !vk.turned)) vk.stale = true;
        else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) lose(result); // Shown, as it is when turned
        vk.acquired = false;
    }
    // The next frame: its room and command buffers are free once the GPU is done with the one before it there
    vk.frame = (vk.frame + 1u) % FRAMES;
    f = &vk.frames[vk.frame];
    if (f->fenced) {
        const VkResult result = wait_frame(update);
        if (result != VK_SUCCESS) {
            lose(result);
            return;
        }
        vkResetFences(vk.device, 1, &f->fence);
        f->fenced = false;
        if (f->serial > vk.completed) vk.completed = f->serial;
    }
    collect(f);
    if (f->used) vkResetCommandPool(vk.device, f->pool, 0); // Unless none of its command buffers was begun since
    f->used = 0;
    reset_pass_pools(f);
    f->ring.used = 0;
    reset_pools(f);
    if (vk.lose_asked) {
        vk.lose_asked = false;
        lose(VK_ERROR_DEVICE_LOST);
    }
}

void tide_gpu_vk_surface_changed(void)
{
    if (!vk.open) return;
    if (vk.device) {
        flush(); // What's recorded goes, and nothing draws in or shows the old surface's images any more
        forget_swapchain();
    }
    if (vk.surface) vkDestroySurfaceKHR(vk.instance, vk.surface, NULL);
    vk.surface = VK_NULL_HANDLE;
    vk.made_width = vk.made_height = 0;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (vk.window.surface(vk.instance, &surface) != VK_SUCCESS) return; // None: the window's away
    VkBool32 shows = VK_FALSE;
    if (vk.device) vkGetPhysicalDeviceSurfaceSupportKHR(vk.physical, vk.family, surface, &shows);
    if (vk.device && !shows) {
        fprintf(stderr, "tide: Vulkan: the window that came back can't be shown by the device it drew with\n");
        vkDestroySurfaceKHR(vk.instance, surface, NULL);
        return;
    }
    vk.surface = surface;
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
        if (!vk.pipelines[i].used) return i + 1u;
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
    const bool storage = kind == TIDE_GPU_STORAGE;
    const VkBufferUsageFlags usage = storage ? VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
                                                   | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT
                                                   | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
                                   : (kind == TIDE_GPU_VERTICES ? VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
                                                                : VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT)
                                         | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (!make_buffer(size ? size : 4u, usage, USE_GPU, &b->buffer, &b->memory)) {
        fprintf(stderr, "tide: Vulkan: no memory for a buffer of %llu bytes\n", (unsigned long long)size);
        abort();
    }
    b->size = size;
    b->storage = storage;
    b->indices = kind == TIDE_GPU_INDICES;
    if (data) {
        vk_buffer_write(id, 0, data, size);
    } else if (storage) { // Zeros, as WebGPU's are: what a shader reads of it before anything wrote it
        vkCmdFillBuffer(outside(), b->buffer, 0, VK_WHOLE_SIZE, 0);
    }
    return id;
}

// A storage buffer, by its id: NULL for none, or for a buffer of another kind.
static const vk_buffer *storage_of(const tide_gpu_id id)
{
    return id && id <= vk.buffer_count && vk.buffers[id - 1u].buffer && vk.buffers[id - 1u].storage ? &vk.buffers[id - 1u] : NULL;
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
        t->base = levels > 1 ? make_view(t->image, VK_IMAGE_VIEW_TYPE_CUBE, format, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6) : t->view;
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

// An array of `layers` images, with `levels` each: viewed as one array to sample (and its first images alone, `base`),
// and each layer's first image to draw into (layer_views). A depth array's views are of its depth.
static tide_gpu_id make_layers(const VkFormat format, const int width, const int height, const uint32_t levels, const uint32_t layers,
                               const VkImageUsageFlags usage, const VkImageLayout rest)
{
    const tide_gpu_id id = texture_place();
    vk_texture *t = &vk.textures[id - 1u];
    const bool depth = aspects_of(format) != VK_IMAGE_ASPECT_COLOR_BIT;
    const VkImageCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = format,
        .extent = {(uint32_t)width, (uint32_t)height, 1}, .mipLevels = levels, .arrayLayers = layers,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = usage,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    *t = (vk_texture){.format = format, .width = width, .height = height, .levels = levels, .layers = layers, .rest = rest,
                      .usage = usage, .depth = depth, .arrayed = true};
    if (!make_image(&info, &t->memory, &t->image)) {
        fprintf(stderr, "tide: Vulkan: no memory for an array of %u layers of %d by %d\n", (unsigned)layers, width, height);
        abort();
    }
    const VkImageAspectFlags read = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    t->view = make_view(t->image, VK_IMAGE_VIEW_TYPE_2D_ARRAY, format, read, 0, levels, 0, layers);
    t->base = levels > 1 ? make_view(t->image, VK_IMAGE_VIEW_TYPE_2D_ARRAY, format, read, 0, 1, 0, layers) : t->view;
    t->attach = t->view;
    t->layer_views = tide_alloc_zeroed(layers, sizeof *t->layer_views);
    for (uint32_t layer = 0; layer < layers; layer++) {
        t->layer_views[layer] = make_view(t->image, VK_IMAGE_VIEW_TYPE_2D, format, aspects_of(format), 0, 1, layer, 1);
    }
    // Black, or the far limit, until drawn into
    const VkCommandBuffer cb = outside();
    turn_texture(cb, t, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    const VkImageSubresourceRange all = {aspects_of(format), 0, levels, 0, layers};
    if (depth) {
        const VkClearDepthStencilValue far = {1.0f, 0};
        vkCmdClearDepthStencilImage(cb, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &far, 1, &all);
    } else {
        const VkClearColorValue zero = {{0}};
        vkCmdClearColorImage(cb, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &all);
    }
    turn_texture(cb, t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, rest);
    return id;
}

static void forget_texture(vk_texture *t)
{
    if (!t->shared) {
        if (t->attach && t->attach != t->view) DISCARD(VK_OBJECT_TYPE_IMAGE_VIEW, t->attach, NULL);
        if (t->base && t->base != t->view) DISCARD(VK_OBJECT_TYPE_IMAGE_VIEW, t->base, NULL);
        DISCARD(VK_OBJECT_TYPE_IMAGE_VIEW, t->view, NULL);
        for (uint32_t face = 0; face < TIDE_GPU_CUBE_FACES; face++) DISCARD(VK_OBJECT_TYPE_IMAGE_VIEW, t->faces[face], NULL);
        for (uint32_t layer = 0; t->layer_views && layer < t->layers; layer++) DISCARD(VK_OBJECT_TYPE_IMAGE_VIEW, t->layer_views[layer], NULL);
        DISCARD(VK_OBJECT_TYPE_IMAGE, t->image, &t->memory);
    }
    free(t->layer_views);
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
    uint32_t levels, total;
    const bool rgba = blocks.format == TIDE_BLOCKS_RGBA8;
    if (vk.lost || (!rgba && !vk.compression) || !blocks.pixels
        || !tide_texture_blocks_size(width, height, blocks.format, &levels, &total)
        || blocks.levels != levels || blocks.bytes != total) return 0;
    const bool bc1 = blocks.format == TIDE_BLOCKS_BC1;
    const tide_gpu_id id = make_texture(rgba ? VK_FORMAT_R8G8B8A8_UNORM : bc1 ? VK_FORMAT_BC1_RGBA_UNORM_BLOCK : VK_FORMAT_BC3_UNORM_BLOCK, width, height,
                                        blocks.levels, false, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false);
    vk_texture *t = &vk.textures[id - 1u];
    t->mipped = true;
    const VkCommandBuffer cb = outside();
    turn_texture(cb, t, t->rest, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    uint32_t w = (uint32_t)width, h = (uint32_t)height;
    size_t offset = 0;
    for (uint32_t level = 0; level < blocks.levels; level++) {
        const size_t bytes = tide_texture_level_bytes(w, h, blocks.format);
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
    if (!t) return;
    vk_texture *depth = texture_of(t->depth_array);
    if (depth) forget_texture(depth);
    forget_texture(texture_of(id));
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
    if (levels < 2 || (t->arrayed && t->levels < levels)) return; // An array has its room from the start, where it blits
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

// An array: of pixels, or drawn into a layer at a time, with a depth array beside it. One that holds depth alone is
// its depth array. Colors have room for their smaller images from the start, where the device blits them.
static tide_gpu_id vk_array(const tide_gpu_target_format format, const int width, const int height, const uint32_t layers,
                            const void *const *pixels)
{
    if (vk.lost || layers == 0 || layers > TIDE_GPU_MAX_LAYERS || layers > vk.properties.limits.maxImageArrayLayers) return 0;
    const VkImageUsageFlags depth_usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
                                        | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!pixels && !tide_gpu_target_colors(format)) {
        return make_layers(vk.depth_format, width, height, 1, layers, depth_usage, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    }
    const VkFormat color = pixels ? VK_FORMAT_R8G8B8A8_UNORM : color_format(tide_gpu_target_color(format, 0));
    const uint32_t levels = blits(color) ? levels_for(width, height) : 1u;
    const tide_gpu_id id = make_layers(color, width, height, levels, layers,
                                       VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                                           | (pixels ? 0u : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT),
                                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vk_texture *t = &vk.textures[id - 1u];
    t->pixels = TIDE_GPU_RGBA8;
    if (pixels) {
        for (uint32_t layer = 0; layer < layers; layer++) write_pixels(t, pixels[layer], 0, 0, width, height, width, layer);
        return id;
    }
    const tide_gpu_id depth = make_layers(vk.depth_format, width, height, 1, layers, depth_usage,
                                          VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    vk.textures[id - 1u].depth_array = depth;
    return id;
}

// An array of blocks: every layer's chain, as an uploaded texture's are (vk_texture_blocks), into one image
static tide_gpu_id vk_array_blocks(const int width, const int height, const uint32_t layers, const tide_texture_blocks blocks)
{
    uint32_t levels, total;
    const bool rgba = blocks.format == TIDE_BLOCKS_RGBA8;
    if (vk.lost || (!rgba && !vk.compression) || !blocks.pixels || layers == 0 || layers > TIDE_GPU_MAX_LAYERS
        || layers > vk.properties.limits.maxImageArrayLayers
        || !tide_texture_blocks_size(width, height, blocks.format, &levels, &total) || blocks.levels != levels
        || blocks.bytes != total) return 0;
    const VkFormat format = rgba ? VK_FORMAT_R8G8B8A8_UNORM : blocks.format == TIDE_BLOCKS_BC1 ? VK_FORMAT_BC1_RGBA_UNORM_BLOCK
                                                                                                : VK_FORMAT_BC3_UNORM_BLOCK;
    const tide_gpu_id id = texture_place();
    vk_texture *t = &vk.textures[id - 1u];
    const VkImageCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = format,
        .extent = {(uint32_t)width, (uint32_t)height, 1}, .mipLevels = levels, .arrayLayers = layers,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    *t = (vk_texture){.format = format, .width = width, .height = height, .levels = levels, .layers = layers,
                      .rest = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .usage = info.usage, .arrayed = true, .mipped = true};
    if (!make_image(&info, &t->memory, &t->image)) {
        fprintf(stderr, "tide: Vulkan: no memory for an array of %u layers of %d by %d\n", (unsigned)layers, width, height);
        abort();
    }
    t->view = make_view(t->image, VK_IMAGE_VIEW_TYPE_2D_ARRAY, format, VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, layers);
    t->base = t->attach = t->view;
    const VkCommandBuffer cb = outside();
    turn_texture(cb, t, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    for (uint32_t layer = 0; layer < layers; layer++) {
        uint32_t w = (uint32_t)width, h = (uint32_t)height;
        size_t offset = (size_t)blocks.bytes * layer;
        for (uint32_t level = 0; level < levels; level++) {
            const size_t bytes = tide_texture_level_bytes(w, h, blocks.format);
            VkBuffer from;
            uint8_t *at;
            const VkDeviceSize start = ring_take(bytes, 16u, bytes, &from, &at);
            memcpy(at, blocks.pixels + offset, bytes);
            const VkBufferImageCopy copy = {start, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, level, layer, 1}, {0, 0, 0}, {w, h, 1}};
            vkCmdCopyBufferToImage(cb, from, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            offset += bytes;
            if (w > 1) w /= 2;
            if (h > 1) h /= 2;
        }
    }
    turn_texture(cb, t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, t->rest);
    return id;
}

// An array of a format, written a part at a time: room for its smaller images where the format blits
static tide_gpu_id vk_array_of(const tide_gpu_format format, const int width, const int height, const uint32_t layers)
{
    if (vk.lost || layers == 0 || layers > TIDE_GPU_MAX_LAYERS || layers > vk.properties.limits.maxImageArrayLayers) return 0;
    const VkFormat f = format_of(format);
    const tide_gpu_id id = make_layers(f, width, height, blits(f) ? levels_for(width, height) : 1u, layers,
                                       VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vk.textures[id - 1u].pixels = format;
    return id;
}

static void vk_array_write(const tide_gpu_id id, const uint32_t layer, const void *pixels, const int x, const int y,
                           const int width, const int height)
{
    vk_texture *t = texture_of(id);
    if (vk.lost || !t || !t->arrayed || layer >= t->layers) return;
    write_pixels(t, pixels, x, y, width, height, t->width, layer);
}

static tide_gpu_id vk_array_depth(const tide_gpu_id id)
{
    const vk_texture *t = texture_of(id);
    return !t || !t->arrayed ? 0 : t->depth ? id : t->depth_array;
}

// An array's layer as a target: that layer of its colors (none for depth alone) and of its depth array.
static tide_gpu_id vk_array_layer(const tide_gpu_id array, const uint32_t layer)
{
    if (vk.lost) return 0;
    const vk_texture *a = texture_of(array);
    if (!a || !a->arrayed || layer >= a->layers) return 0;
    const bool depth_alone = a->depth;
    const bool floats = a->format == VK_FORMAT_R16G16B16A16_SFLOAT;
    const tide_gpu_id id = target_place();
    vk_target *t = &vk.targets[id - 1u];
    *t = (vk_target){.used = true, .width = a->width, .height = a->height, .color_count = depth_alone ? 0u : 1u, .samples = 1,
                     .face = true, .cube = array, .layer = layer, .array = true,
                     .depth_layers = depth_alone ? array : a->depth_array, .bytes = !depth_alone && !floats,
                     .format = depth_alone ? TIDE_GPU_TARGET_DEPTH
                             : tide_gpu_target_with(TIDE_GPU_TARGET_DEPTH, floats ? TIDE_GPU_COLOR_FLOAT : TIDE_GPU_COLOR_BYTES)};
    return id;
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
        // Its samples read by occlusion culling's kernel (depth_now) where the device samples a depth of so many
        t->samples_read = (vk.properties.limits.sampledImageDepthSampleCounts & t->samples) != 0;
        if (!many_image(vk.depth_format, width, height, t->samples,
                        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
                            | (t->samples_read ? VK_IMAGE_USAGE_SAMPLED_BIT : 0u),
                        &t->many_depth, &t->many_depth_memory, &t->many_depth_view)) {
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

// A discrete GPU's own memory: its biggest heap of device-local memory. An
// integrated GPU's (a phone's too) is the system's, so it says none.
static uint64_t vk_device_memory(void)
{
    if (vk.lost || vk.properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) return 0;
    uint64_t most = 0;
    for (uint32_t i = 0; i < vk.memory.memoryHeapCount; i++) {
        const VkMemoryHeap heap = vk.memory.memoryHeaps[i];
        if ((heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) && heap.size > most) most = heap.size;
    }
    return most;
}

// The most a storage buffer bound whole can be: what a shader reaches of one (maxStorageBufferRange), and what one
// buffer and one allocation of its memory can be, rounded down to 16 bytes.
static uint64_t vk_storage_most(void)
{
    VkPhysicalDeviceVulkan11Properties v11 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES};
    VkPhysicalDeviceVulkan13Properties v13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES, .pNext = &v11};
    VkPhysicalDeviceProperties2 properties = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &v13};
    vkGetPhysicalDeviceProperties2(vk.physical, &properties);
    uint64_t most = vk.properties.limits.maxStorageBufferRange;
    if (v11.maxMemoryAllocationSize && v11.maxMemoryAllocationSize < most) most = v11.maxMemoryAllocationSize;
    if (v13.maxBufferSize && v13.maxBufferSize < most) most = v13.maxBufferSize;
    return most & ~(uint64_t)15u;
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
    vk_texture *far = texture_of(t->far);
    if (far) forget_texture(far);
    vk_texture *samples = texture_of(t->samples_texture);
    if (samples) forget_texture(samples); // Its image is the target's, and its view, below
    DISCARD(VK_OBJECT_TYPE_IMAGE_VIEW, t->samples_view, NULL);
    for (uint32_t i = 0; i < TIDE_GPU_MAX_COLORS; i++) {
        DISCARD(VK_OBJECT_TYPE_IMAGE_VIEW, t->many_views[i], NULL);
        DISCARD(VK_OBJECT_TYPE_IMAGE, t->many[i], &t->many_memory[i]);
    }
    DISCARD(VK_OBJECT_TYPE_IMAGE_VIEW, t->many_depth_view, NULL);
    DISCARD(VK_OBJECT_TYPE_IMAGE, t->many_depth, &t->many_depth_memory);
    *t = (vk_target){0};
}

// A multisampled target's samples, turned for a kernel to read them (depth_now), turned back for use as depth.
static void turn_in(const VkCommandBuffer cb, const VkImage image, const VkFormat format, const uint32_t level,
                    const uint32_t levels, const uint32_t layer, const uint32_t layers, const VkImageLayout from,
                    const VkImageLayout to);

static void samples_back(const VkCommandBuffer cb, vk_target *t)
{
    if (!t->samples_turned) return;
    turn_in(cb, t->many_depth, vk.depth_format, 0, 1, 0, 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    t->samples_turned = false;
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
    samples_back(cb, t);
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

// What a target's depth holds now (gpu.h): its own depth texture; for a multisampled one, its samples, turned to be
// read till their next use as depth (samples_back), or on a device that samples no depth of so many, resolved by
// the most of them into a texture of its own; the window's copied. None on a device that can do none of those.
static tide_gpu_id vk_depth_now(const tide_gpu_id id, uint32_t *samples)
{
    *samples = 1;
    if (vk.lost) return 0;
    if (!id) { // The window's, copied: its depth is an image of its own, drawn into in this frame's passes
        if (!vk.window_depth || vk.window_depth_new) return 0;
        vk_texture *far = vk.window_far_epoch == vk.epoch ? texture_of(vk.window_far) : NULL;
        if (far && (far->width != (int)vk.extent.width || far->height != (int)vk.extent.height)) {
            forget_texture(far);
            far = NULL;
        }
        if (!far) {
            vk.window_far = make_texture(vk.depth_format, (int)vk.extent.width, (int)vk.extent.height, 1, false,
                                         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                         VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, false);
            vk.window_far_epoch = vk.epoch;
            far = &vk.textures[vk.window_far - 1u];
            far->owned = true;
        }
        const VkCommandBuffer cb = outside();
        barrier_attachments(cb); // The passes that drew it
        turn(cb, vk.window_depth, vk.depth_format, 0, 1, 0, 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
             VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        turn(cb, far->image, far->format, 0, 1, 0, 1, far->rest, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        const VkImageCopy copy = {.srcSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1},
                                  .dstSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1},
                                  .extent = {vk.extent.width, vk.extent.height, 1}};
        vkCmdCopyImage(cb, vk.window_depth, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, far->image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        turn(cb, far->image, far->format, 0, 1, 0, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, far->rest);
        turn(cb, vk.window_depth, vk.depth_format, 0, 1, 0, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
             VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        return vk.window_far;
    }
    vk_target *t = &vk.targets[id - 1u];
    if (t->samples <= 1u) return t->depth;
    if (t->samples_read) { // Its samples as they are, seen as depth alone, in the layout a depth is read in
        if (!t->samples_texture) {
            const tide_gpu_id texture = texture_place();
            t = &vk.targets[id - 1u];
            t->samples_view = make_view(t->many_depth, VK_IMAGE_VIEW_TYPE_2D, vk.depth_format, VK_IMAGE_ASPECT_DEPTH_BIT,
                                        0, 1, 0, 1);
            vk.textures[texture - 1u] = (vk_texture){
                .image = t->many_depth, .view = t->samples_view, .attach = t->samples_view, .format = vk.depth_format,
                .width = t->width, .height = t->height, .levels = 1, .layers = 1,
                .rest = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
                .depth = true, .owned = true, .shared = true};
            t->samples_texture = texture;
        }
        if (!t->samples_turned) {
            const VkCommandBuffer cb = outside();
            barrier_attachments(cb); // The passes that drew its samples
            turn(cb, t->many_depth, vk.depth_format, 0, 1, 0, 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                 VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
            t->samples_turned = true;
        }
        *samples = t->samples;
        return t->samples_texture;
    }
    if (!vk.depth_max) return 0;
    if (!t->far) {
        const tide_gpu_id far = depth_texture(t->width, t->height);
        t = &vk.targets[id - 1u];
        t->far = far;
        vk.textures[far - 1u].owned = true;
    }
    vk_texture *d = &vk.textures[t->far - 1u];
    const VkCommandBuffer cb = outside();
    barrier_attachments(cb); // The passes that drew its samples
    samples_back(cb, t);
    turn(cb, d->image, d->format, 0, 1, 0, 1, d->rest, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    // Depth alone: its stencil, which a resolve by the most needn't take, is left as it is
    const VkRenderingAttachmentInfo depth = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = t->many_depth_view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, .resolveMode = VK_RESOLVE_MODE_MAX_BIT,
        .resolveImageView = d->attach, .resolveImageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    };
    const VkRenderingInfo info = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = {{0, 0}, {(uint32_t)t->width, (uint32_t)t->height}},
                                  .layerCount = 1, .pDepthAttachment = &depth};
    vkCmdBeginRendering(cb, &info);
    vkCmdEndRendering(cb);
    turn(cb, d->image, d->format, 0, 1, 0, 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, d->rest);
    return t->far;
}

// What's read back goes through this, in memory the host reads.
static struct {
    VkBuffer buffer;
    vk_alloc memory;
    VkDeviceSize size;
} readback;

static void vk_target_read_part(const tide_gpu_id id, const int x, const int y, const int width, const int height,
                                uint8_t *rgba)
{
    const vk_target *t = &vk.targets[id - 1u];
    const size_t bytes = (size_t)width * (size_t)height * 4u;
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
    const VkBufferImageCopy copy = {0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, layer, 1}, {x, y, 0},
                                    {(uint32_t)width, (uint32_t)height, 1}};
    vkCmdCopyImageToBuffer(cb, c->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &copy);
    const VkMemoryBarrier seen = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, NULL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &seen, 0, NULL, 0, NULL);
    turn(cb, c->image, c->format, 0, 1, layer, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, c->rest);
    flush();
    if (vk.lost) {
        memset(rgba, 0, bytes);
        return;
    }
    // Apart (restrict), so the loop below takes many pixels at once
    const uint8_t *restrict from = readback.memory.mapped;
    uint8_t *restrict to = rgba;
    if (c->format == VK_FORMAT_B8G8R8A8_UNORM) { // Blue first: red first, as it's read back
        for (size_t i = 0; i < bytes; i += 4) {
            to[i] = from[i + 2], to[i + 1] = from[i + 1], to[i + 2] = from[i], to[i + 3] = from[i + 3];
        }
    } else {
        memcpy(rgba, from, bytes);
    }
}

static void vk_target_read(const tide_gpu_id id, uint8_t *rgba)
{
    const vk_target *t = &vk.targets[id - 1u];
    vk_target_read_part(id, 0, 0, t->width, t->height, rgba);
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

// The layout of a pipeline that `desc` describes: its place in vk.layouts, made the first time one has its bindings.
static uint32_t layout_for(const tide_gpu_pipeline_desc *desc)
{
    const uint32_t textures = desc->texture_count < TIDE_GPU_MAX_TEXTURES ? desc->texture_count : TIDE_GPU_MAX_TEXTURES;
    const uint32_t storage = desc->storage_count < TIDE_GPU_MAX_STORAGE ? desc->storage_count : TIDE_GPU_MAX_STORAGE;
    uint8_t sampler_reads[TIDE_GPU_MAX_TEXTURES];
    const uint32_t samplers = tide_gpu_samplers(desc, sampler_reads);
    uint32_t own = 0;
    for (uint32_t j = 0; j < samplers; j++) own |= sampler_reads[j] != TIDE_GPU_READ_DATA ? 1u << j : 0u;
    for (uint32_t i = 0; i < vk.layout_count; i++) {
        const vk_layout *l = &vk.layouts[i];
        if (l->textures == textures && l->own == own && l->storage == storage) return i;
    }
    enum { BINDINGS = TIDE_GPU_STORAGE_BINDING + TIDE_GPU_MAX_STORAGE };
    VkDescriptorSetLayoutBinding bindings[BINDINGS];
    const VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
    uint32_t count = 0;
    bindings[count++] = (VkDescriptorSetLayoutBinding){0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, stages, NULL};
    for (uint32_t i = 0; i < TIDE_GPU_MAX_TEXTURES; i++) {
        if (i < textures) bindings[count++] = (VkDescriptorSetLayoutBinding){1 + 2 * i, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, stages, NULL};
        if (own >> i & 1u) bindings[count++] = (VkDescriptorSetLayoutBinding){2 + 2 * i, VK_DESCRIPTOR_TYPE_SAMPLER, 1, stages, NULL};
    }
    for (uint32_t i = 0; i < storage; i++) {
        bindings[count++] = (VkDescriptorSetLayoutBinding){TIDE_GPU_STORAGE_BINDING + i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                                                           stages, NULL};
    }
    vk_layout l = {.textures = textures, .own = own, .storage = storage, .push = vk.push && count <= vk.max_push};
    const VkDescriptorSetLayoutCreateInfo set = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL,
                                                 l.push ? VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR : 0,
                                                 count, bindings};
    check(vkCreateDescriptorSetLayout(vk.device, &set, NULL, &l.set), "vkCreateDescriptorSetLayout");
    const VkPipelineLayoutCreateInfo layout = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 1, &l.set, 0, NULL};
    check(vkCreatePipelineLayout(vk.device, &layout, NULL, &l.layout), "vkCreatePipelineLayout");
    vk.layouts = tide_realloc(vk.layouts, vk.layout_count * sizeof(vk_layout), (vk.layout_count + 1u) * sizeof(vk_layout));
    vk.layouts[vk.layout_count] = l;
    return vk.layout_count++;
}

// A pipeline being made: everything vkCreate*Pipelines reads, which its create
// info points into, and what it gave. The driver compiles it there, tens to
// hundreds of milliseconds cold, so while pipelines are made in the background
// (vk_background) it's made on a thread of the pool's, and many go at once; at
// once otherwise. Its place in vk.pipelines holds it till it's settled
// (settle), on the frame's thread, the only one that touches vk. Nothing
// the threads read (the device, vk.pipeline_cache) changes before every
// pipeline being made is settled (finish_all_making).
typedef struct vk_making {
    tide_background job;
    bool compute;
    // Its SPIR-V, which lasts as long as its shader does (a pipeline is let go of before its game is), or `made`,
    // translated for it (tide_gpu_spirv_of) and the record's own. The module is made of it where the pipeline is
    // (make_work): a driver takes tens of milliseconds over a big shader's, and a big game has hundreds
    const uint32_t *code;
    size_t words;
    uint32_t *made;
    VkShaderModule module;
    VkResult result;
    VkPipeline pipeline;
    char name[128];
    VkComputePipelineCreateInfo compute_info;
    VkGraphicsPipelineCreateInfo info;
    VkPipelineShaderStageCreateInfo stages[2];
    VkVertexInputBindingDescription bindings[TIDE_GPU_MAX_SLOTS];
    VkVertexInputAttributeDescription attributes[TIDE_GPU_MAX_ATTRIBUTES];
    VkPipelineVertexInputStateCreateInfo input;
    VkPipelineInputAssemblyStateCreateInfo assembly;
    VkPipelineViewportStateCreateInfo viewport;
    VkPipelineRasterizationStateCreateInfo raster;
    VkPipelineMultisampleStateCreateInfo multisample;
    VkPipelineDepthStencilStateCreateInfo depth;
    VkPipelineColorBlendAttachmentState blends[TIDE_GPU_MAX_COLORS];
    VkFormat formats[TIDE_GPU_MAX_COLORS];
    VkPipelineColorBlendStateCreateInfo blend;
    VkPipelineDynamicStateCreateInfo dynamics;
    VkPipelineRenderingCreateInfo rendering;
} vk_making;

static void make_work(void *context)
{
    vk_making *m = context;
    const VkShaderModuleCreateInfo module_info = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, m->words * 4u, m->code};
    if (vkCreateShaderModule(vk.device, &module_info, NULL, &m->module) != VK_SUCCESS) {
        fprintf(stderr, "tide: the SPIR-V of %s didn't make a Vulkan shader module\n", m->name);
        abort();
    }
    free(m->made);
    m->made = NULL;
    m->compute_info.stage.module = m->module;
    m->stages[0].module = m->stages[1].module = m->module;
    m->result = m->compute ? vkCreateComputePipelines(vk.device, vk.pipeline_cache, 1, &m->compute_info, NULL, &m->pipeline)
                           : vkCreateGraphicsPipelines(vk.device, vk.pipeline_cache, 1, &m->info, NULL, &m->pipeline);
    vkDestroyShaderModule(vk.device, m->module, NULL);
}

// The place's pipeline, once its thread made it: one the driver refused stops the program, as one made at once does.
static void settle(const tide_gpu_id id)
{
    vk_pipeline *p = &vk.pipelines[id - 1u];
    vk_making *m = p->making;
    p->making = NULL;
    p->pipeline = m->result == VK_SUCCESS ? m->pipeline : VK_NULL_HANDLE;
    if (m->result != VK_SUCCESS) {
        if (m->result != VK_ERROR_DEVICE_LOST) {
            fprintf(stderr, "tide: Vulkan made no %spipeline of %s (%d)\n", m->compute ? "compute " : "", m->name, (int)m->result);
            abort();
        }
        lose(m->result);
    }
    free(m);
}

// Waits for the place's pipeline, if it's being made on the pool's threads, and settles it.
static void finish_making(const tide_gpu_id id)
{
    const vk_pipeline *p = &vk.pipelines[id - 1u];
    if (!p->making) return;
    const tide_jobs *jobs = tide_platform_jobs();
    // Helpers only: what's first in the queue is as likely another pipeline, or a game's own long work, which
    // this thread would then be in for as long as it takes. The pool's threads get to this one.
    while (!__atomic_load_n(&p->making->job.done, __ATOMIC_ACQUIRE)) {
        if (jobs && jobs->help) jobs->help(jobs->self, true);
    }
    settle(id);
}

static void finish_all_making(void)
{
    for (uint32_t i = 0; i < vk.pipeline_count; i++) finish_making(i + 1u);
}

// Makes the pipeline `m` describes, of `desc` and the layout, in a place of its own: on the pool's threads while
// pipelines are made in the background, else before it returns. 0 if the device was lost making it.
static tide_gpu_id make(vk_making *m, const tide_gpu_pipeline_desc *desc, const uint32_t layout)
{
    const tide_gpu_id id = pipeline_place();
    vk_pipeline *p = &vk.pipelines[id - 1u];
    *p = (vk_pipeline){.used = true, .making = m, .texture_count = desc->texture_count, .compute = m->compute,
                       .storage_count = desc->storage_count < TIDE_GPU_MAX_STORAGE ? desc->storage_count : TIDE_GPU_MAX_STORAGE,
                       .layout = layout};
    if (!m->compute) p->into = desc->into;
    memcpy(p->reads, desc->reads, sizeof p->reads);
    p->sampler_count = tide_gpu_samplers(desc, p->sampler_reads);
    m->job = (tide_background){.work = make_work, .context = m};
    const tide_jobs *jobs = vk.background ? tide_platform_jobs() : NULL;
    if (jobs && jobs->background) {
        jobs->background(jobs->self, &m->job);
        return id;
    }
    make_work(m);
    settle(id);
    if (p->pipeline) return id;
    *p = (vk_pipeline){0};
    return 0;
}

// Whether the pipeline is made: one being made on the pool's threads is settled once it is.
static bool vk_pipeline_ready(const tide_gpu_id id)
{
    const vk_pipeline *p = &vk.pipelines[id - 1u];
    if (p->making) {
        if (!__atomic_load_n(&p->making->job.done, __ATOMIC_ACQUIRE)) return false;
        settle(id);
    }
    return p->pipeline != VK_NULL_HANDLE;
}

static void vk_background(const bool on)
{
    vk.background = on;
}

// A compute pipeline of the module's `compute`, in the layout of its bindings
// (layout_for), and through the cache graphics pipelines go through.
static tide_gpu_id compute_pipeline(const tide_gpu_pipeline_desc *desc, vk_making *m)
{
    const uint32_t layout = layout_for(desc);
    m->compute = true;
    m->compute_info = (VkComputePipelineCreateInfo){
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_COMPUTE_BIT, m->module, "compute", NULL},
        .layout = vk.layouts[layout].layout, .basePipelineIndex = -1,
    };
    return make(m, desc, layout);
}

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
    vk_making *m = tide_alloc_zeroed(1, sizeof *m);
    m->code = code;
    m->words = words;
    m->made = made;
    snprintf(m->name, sizeof m->name, "%s", name);
    if (desc->compute) return compute_pipeline(desc, m);
    const uint32_t layout = layout_for(desc);
    m->stages[0] = (VkPipelineShaderStageCreateInfo){VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
                                                     VK_SHADER_STAGE_VERTEX_BIT, VK_NULL_HANDLE, "vertex", NULL};
    m->stages[1] = (VkPipelineShaderStageCreateInfo){VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0,
                                                     VK_SHADER_STAGE_FRAGMENT_BIT, VK_NULL_HANDLE, "fragment", NULL};
    VkVertexInputBindingDescription *bindings = m->bindings;
    for (uint32_t i = 0; i < desc->slot_count; i++) {
        bindings[i] = (VkVertexInputBindingDescription){i, desc->slots[i].stride,
                                                        desc->slots[i].per_instance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX};
    }
    VkVertexInputAttributeDescription *attributes = m->attributes;
    for (uint32_t i = 0; i < desc->attribute_count; i++) {
        attributes[i] = (VkVertexInputAttributeDescription){i, desc->attributes[i].slot, attribute_format(desc->attributes[i].format),
                                                            desc->attributes[i].offset};
    }
    m->input = (VkPipelineVertexInputStateCreateInfo){VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, NULL, 0,
                                                        desc->slot_count, bindings, desc->attribute_count, attributes};
    m->assembly = (VkPipelineInputAssemblyStateCreateInfo){VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, NULL, 0,
                                                             VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE};
    m->viewport = (VkPipelineViewportStateCreateInfo){VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, NULL, 0, 1, NULL, 1, NULL};
    // A triangle's front goes clockwise as it's seen: with y up in clip space, by the viewport upside down
    m->raster = (VkPipelineRasterizationStateCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = desc->cull == TIDE_GPU_CULL_BACK ? VK_CULL_MODE_BACK_BIT : desc->cull == TIDE_GPU_CULL_FRONT ? VK_CULL_MODE_FRONT_BIT
                                                                                                          : VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_CLOCKWISE, .lineWidth = 1.0f,
    };
    const uint32_t colors = tide_gpu_target_colors(desc->into);
    const uint32_t samples = colors ? tide_gpu_target_samples(desc->into) : 1u;
    // A cut-out's alpha is the share of a pixel's samples it draws into, where there are several
    const VkBool32 coverage = desc->alpha_to_coverage && samples > 1u ? VK_TRUE : VK_FALSE;
    m->multisample = (VkPipelineMultisampleStateCreateInfo){VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, NULL, 0,
                                                              (VkSampleCountFlagBits)samples, VK_FALSE, 0.0f, NULL, coverage,
                                                              coverage && vk.alpha_to_one && desc->outputs <= 1u ? VK_TRUE : VK_FALSE};
    static const VkCompareOp tests[] = {VK_COMPARE_OP_ALWAYS, VK_COMPARE_OP_NEVER, VK_COMPARE_OP_LESS, VK_COMPARE_OP_EQUAL,
                                        VK_COMPARE_OP_LESS_OR_EQUAL, VK_COMPARE_OP_GREATER, VK_COMPARE_OP_NOT_EQUAL,
                                        VK_COMPARE_OP_GREATER_OR_EQUAL}; // By tide_gpu_stencil_test
    static const VkStencilOp ops[] = {VK_STENCIL_OP_KEEP, VK_STENCIL_OP_REPLACE, VK_STENCIL_OP_INCREMENT_AND_CLAMP,
                                      VK_STENCIL_OP_DECREMENT_AND_CLAMP, VK_STENCIL_OP_ZERO, VK_STENCIL_OP_INVERT}; // By tide_gpu_stencil_op
    const uint32_t test = (uint32_t)desc->stencil_test < 8u ? (uint32_t)desc->stencil_test : 0u;
    const uint32_t op = (uint32_t)desc->stencil_op < 6u ? (uint32_t)desc->stencil_op : 0u;
    // Only where the draw draws: a pixel that fails either test keeps its number
    const VkStencilOpState stencil = {VK_STENCIL_OP_KEEP, ops[op], VK_STENCIL_OP_KEEP, tests[test], 0xFFu, 0xFFu, 0};
    m->depth = (VkPipelineDepthStencilStateCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = desc->depth != TIDE_GPU_DEPTH_NONE,
        .depthWriteEnable = desc->depth == TIDE_GPU_DEPTH_TEST || desc->depth == TIDE_GPU_DEPTH_RESET
                         || desc->depth == TIDE_GPU_DEPTH_ALONE,
        .depthCompareOp = desc->depth == TIDE_GPU_DEPTH_RESET   ? VK_COMPARE_OP_ALWAYS
                        : desc->depth == TIDE_GPU_DEPTH_EQUAL ? VK_COMPARE_OP_EQUAL
                                                              : VK_COMPARE_OP_LESS_OR_EQUAL,
        .stencilTestEnable = !(test == TIDE_GPU_STENCIL_ALWAYS && op == TIDE_GPU_STENCIL_KEEP),
        .front = stencil, .back = stencil, .maxDepthBounds = 1.0f,
    };
    // Each of its outputs blended into the target's image in its place, the way `blend` says; the images past them left as they are
    VkPipelineColorBlendAttachmentState *blends = m->blends;
    VkFormat *formats = m->formats;
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
        // Depth alone writes no color: the frag still runs, for its discards and its coverage
        if (i >= outputs || desc->depth == TIDE_GPU_DEPTH_ALONE) b = (VkPipelineColorBlendAttachmentState){.colorWriteMask = 0};
        blends[i] = b;
    }
    m->blend = (VkPipelineColorBlendStateCreateInfo){VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, NULL, 0, VK_FALSE,
                                                       VK_LOGIC_OP_COPY, colors, blends, {0}};
    static const VkDynamicState dynamic[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_STENCIL_REFERENCE};
    m->dynamics = (VkPipelineDynamicStateCreateInfo){VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, NULL, 0, 3, dynamic};
    m->rendering = (VkPipelineRenderingCreateInfo){VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, NULL, 0, colors, formats,
                                                     vk.depth_format, vk.depth_format};
    m->info = (VkGraphicsPipelineCreateInfo){
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .pNext = &m->rendering, .stageCount = 2, .pStages = m->stages,
        .pVertexInputState = &m->input, .pInputAssemblyState = &m->assembly, .pViewportState = &m->viewport,
        .pRasterizationState = &m->raster, .pMultisampleState = &m->multisample, .pDepthStencilState = &m->depth,
        .pColorBlendState = &m->blend, .pDynamicState = &m->dynamics, .layout = vk.layouts[layout].layout,
    };
    return make(m, desc, layout);
}

static void vk_pipeline_free(const tide_gpu_id id)
{
    finish_making(id); // Its thread's done with it first
    vk_pipeline *p = &vk.pipelines[id - 1u];
    if (p->pipeline) DISCARD(VK_OBJECT_TYPE_PIPELINE, p->pipeline, NULL);
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

// ---------------------------------------------------------------------------
// Passes recorded on the pool's threads
//
// A graphics pass's commands are written down as the renderer gives them, in the order it gives them, with every
// handle they name (pipelines, descriptors, buffers, images): `pc_*` below. As the pass ends a job of the pool's
// (tide_jobs.background) records them into the pass's command buffer, from a command pool no other thread has at
// that moment (take_pass_pool), so the driver's work for each command is off the frame's thread and passes are
// recorded side by side. The pass's place in `pending` is a tag until settle_passes, before any submission, waits
// for the jobs (helping them) and puts each command buffer in its place: what's submitted, and in what order, is as
// when the frame's thread recorded them. Compute passes and what's done outside passes are recorded as before. With
// no pool, a pass is recorded as it ends, on the frame's thread.

typedef enum pc_kind {
    PC_TURN, PC_BARRIER_PASS, PC_QUERY_RESET, PC_TIMESTAMP, PC_RENDERING, PC_VIEWPORT, PC_SCISSOR, PC_STENCIL,
    PC_PIPELINE, PC_PUSH, PC_SETS, PC_VERTEX, PC_INDEX, PC_DRAW, PC_DRAW_INDEXED, PC_DRAW_INDIRECT,
    PC_DRAW_INDEXED_INDIRECT, PC_DRAW_INDEXED_INDIRECT_COUNT, PC_END_RENDERING,
} pc_kind;

typedef struct pc_head {
    uint32_t kind, size; // size: the head's and what follows it, a multiple of 8
} pc_head;
typedef struct pc_turn {
    VkImage image;
    VkFormat format;
    uint32_t level, levels, layer, layers;
    VkImageLayout from, to;
} pc_turn;
typedef struct pc_query {
    VkQueryPool pool;
    uint32_t index, count;
    VkPipelineStageFlagBits stage;
} pc_query;
typedef struct pc_rendering {
    VkRect2D area;
    uint32_t color_count;
    VkRenderingAttachmentInfo colors[TIDE_GPU_MAX_COLORS], depth;
} pc_rendering;
typedef struct pc_pipeline {
    VkPipelineBindPoint point;
    VkPipeline pipeline;
} pc_pipeline;
typedef struct pc_write {
    uint32_t binding;
    VkDescriptorType type;
    VkDescriptorImageInfo image;
    VkDescriptorBufferInfo buffer;
} pc_write;
typedef struct pc_push {
    VkPipelineBindPoint point;
    VkPipelineLayout layout;
    uint32_t count;
    pc_write writes[1 + 2 * TIDE_GPU_MAX_TEXTURES + TIDE_GPU_MAX_STORAGE]; // `count` of them written down
} pc_push;
typedef struct pc_sets {
    VkPipelineBindPoint point;
    VkPipelineLayout layout;
    VkDescriptorSet set;
} pc_sets;
typedef struct pc_vertex {
    uint32_t slot;
    VkBuffer buffer;
    VkDeviceSize offset;
} pc_vertex;
typedef struct pc_index {
    VkBuffer buffer;
    VkIndexType type;
} pc_index;
typedef struct pc_draws {
    uint32_t first, count, instances;
} pc_draws;
typedef struct pc_indirect {
    VkBuffer buffer, counts;
    VkDeviceSize offset, count_offset;
    uint32_t draws, stride;
} pc_indirect;

typedef struct vk_deferred {
    tide_background job;
    uint8_t *ops; // pc_head and what follows each, `used` bytes of them
    size_t used, capacity;
    uint32_t frame; // Whose command pools it's recorded from
    bool sent;      // To a job (or recorded at once)
    VkCommandBuffer cb; // What it was recorded into, once done: VK_NULL_HANDLE if it couldn't be
    VkResult result;
} vk_deferred;

// A pass's place in `pending` until its command buffer is there: odd, which no handle is
static VkCommandBuffer pass_tag(const uint32_t index)
{
    return (VkCommandBuffer)(uintptr_t)(((uintptr_t)index << 1) | 1u);
}

static bool is_pass_tag(const VkCommandBuffer cb)
{
    return ((uintptr_t)cb & 1u) != 0;
}

// The frame's next record, empty
static vk_deferred *deferred_take(void)
{
    if (vk.deferred_count == vk.deferred_capacity) {
        const uint32_t capacity = vk.deferred_capacity ? vk.deferred_capacity * 2u : 32u;
        vk.deferred = tide_realloc(vk.deferred, vk.deferred_capacity * sizeof *vk.deferred, capacity * sizeof *vk.deferred);
        memset(vk.deferred + vk.deferred_capacity, 0, (capacity - vk.deferred_capacity) * sizeof *vk.deferred);
        vk.deferred_capacity = capacity;
    }
    vk_deferred **at = &vk.deferred[vk.deferred_count];
    if (!*at) { // Kept, with its room, for the frames after
        *at = tide_alloc(sizeof **at);
        memset(*at, 0, sizeof **at);
    }
    vk_deferred *d = *at;
    d->used = 0;
    d->sent = false;
    d->cb = VK_NULL_HANDLE;
    d->result = VK_SUCCESS;
    d->frame = vk.frame;
    vk.deferred_count++;
    return d;
}

static void *pc_add(const pc_kind kind, const size_t size)
{
    vk_deferred *d = vk.recording;
    const size_t whole = (sizeof(pc_head) + size + 7u) & ~(size_t)7u;
    if (d->used + whole > d->capacity) {
        size_t capacity = d->capacity ? d->capacity * 2u : 16384u;
        while (capacity < d->used + whole) capacity *= 2u;
        d->ops = tide_realloc(d->ops, d->capacity, capacity);
        d->capacity = capacity;
    }
    pc_head *h = (pc_head *)(void *)(d->ops + d->used);
    h->kind = kind;
    h->size = (uint32_t)whole;
    d->used += whole;
    return h + 1;
}

static void pc_turn_image(const VkImage image, const VkFormat format, const uint32_t level, const uint32_t levels,
                          const uint32_t layer, const uint32_t layers, const VkImageLayout from, const VkImageLayout to)
{
    if (from == to) return;
    if (!vk.recording) {
        turn(vk.pass, image, format, level, levels, layer, layers, from, to);
        return;
    }
    *(pc_turn *)pc_add(PC_TURN, sizeof(pc_turn)) = (pc_turn){image, format, level, levels, layer, layers, from, to};
}

// An image turned in a command buffer `cb` that may be a pass's being written down
static void turn_in(const VkCommandBuffer cb, const VkImage image, const VkFormat format, const uint32_t level,
                    const uint32_t levels, const uint32_t layer, const uint32_t layers, const VkImageLayout from,
                    const VkImageLayout to)
{
    if (vk.recording && cb == vk.pass) pc_turn_image(image, format, level, levels, layer, layers, from, to);
    else turn(cb, image, format, level, levels, layer, layers, from, to);
}

static void pc_barrier_pass(void)
{
    if (!vk.recording) barrier_pass(vk.pass);
    else (void)pc_add(PC_BARRIER_PASS, 0);
}

static void pc_query_reset(const VkQueryPool pool, const uint32_t index, const uint32_t count)
{
    if (!vk.recording) vkCmdResetQueryPool(vk.pass, pool, index, count);
    else *(pc_query *)pc_add(PC_QUERY_RESET, sizeof(pc_query)) = (pc_query){pool, index, count, 0};
}

static void pc_timestamp(const VkPipelineStageFlagBits stage, const VkQueryPool pool, const uint32_t index)
{
    if (!vk.recording) vkCmdWriteTimestamp(vk.pass, stage, pool, index);
    else *(pc_query *)pc_add(PC_TIMESTAMP, sizeof(pc_query)) = (pc_query){pool, index, 1, stage};
}

static void pc_scissor(const VkRect2D rect)
{
    if (!vk.recording) vkCmdSetScissor(vk.pass, 0, 1, &rect);
    else *(VkRect2D *)pc_add(PC_SCISSOR, sizeof(VkRect2D)) = rect;
}

static void pc_stencil(const uint32_t value)
{
    if (!vk.recording) vkCmdSetStencilReference(vk.pass, VK_STENCIL_FACE_FRONT_AND_BACK, value);
    else *(uint32_t *)pc_add(PC_STENCIL, sizeof(uint32_t)) = value;
}

static void pc_pipeline_bind(const VkPipelineBindPoint point, const VkPipeline pipeline)
{
    if (!vk.recording) vkCmdBindPipeline(vk.pass, point, pipeline);
    else *(pc_pipeline *)pc_add(PC_PIPELINE, sizeof(pc_pipeline)) = (pc_pipeline){point, pipeline};
}

static void pc_push_writes(const VkPipelineBindPoint point, const VkPipelineLayout layout, const uint32_t count,
                           const VkWriteDescriptorSet *writes)
{
    if (!vk.recording) {
        vkCmdPushDescriptorSetKHR(vk.pass, point, layout, 0, count, writes);
        return;
    }
    pc_push *op = pc_add(PC_PUSH, offsetof(pc_push, writes) + count * sizeof(pc_write));
    op->point = point;
    op->layout = layout;
    op->count = count;
    for (uint32_t i = 0; i < count; i++) {
        const VkWriteDescriptorSet *w = &writes[i];
        pc_write *to = &op->writes[i];
        *to = (pc_write){.binding = w->dstBinding, .type = w->descriptorType};
        if (w->pImageInfo) to->image = *w->pImageInfo;
        if (w->pBufferInfo) to->buffer = *w->pBufferInfo;
    }
}

static void pc_sets_bind(const VkPipelineBindPoint point, const VkPipelineLayout layout, const VkDescriptorSet set)
{
    if (!vk.recording) vkCmdBindDescriptorSets(vk.pass, point, layout, 0, 1, &set, 0, NULL);
    else *(pc_sets *)pc_add(PC_SETS, sizeof(pc_sets)) = (pc_sets){point, layout, set};
}

static void pc_vertices(const uint32_t slot, const VkBuffer buffer, const VkDeviceSize offset)
{
    if (!vk.recording) vkCmdBindVertexBuffers(vk.pass, slot, 1, &buffer, &offset);
    else *(pc_vertex *)pc_add(PC_VERTEX, sizeof(pc_vertex)) = (pc_vertex){slot, buffer, offset};
}

static void pc_indices(const VkBuffer buffer, const VkIndexType type)
{
    if (!vk.recording) vkCmdBindIndexBuffer(vk.pass, buffer, 0, type);
    else *(pc_index *)pc_add(PC_INDEX, sizeof(pc_index)) = (pc_index){buffer, type};
}

static void pc_draw(const uint32_t first, const uint32_t count, const uint32_t instances)
{
    if (!vk.recording) vkCmdDraw(vk.pass, count, instances, first, 0);
    else *(pc_draws *)pc_add(PC_DRAW, sizeof(pc_draws)) = (pc_draws){first, count, instances};
}

static void pc_draw_indexed(const uint32_t count, const uint32_t instances)
{
    if (!vk.recording) vkCmdDrawIndexed(vk.pass, count, instances, 0, 0, 0);
    else *(pc_draws *)pc_add(PC_DRAW_INDEXED, sizeof(pc_draws)) = (pc_draws){0, count, instances};
}

// Indirect draws: `counts` for one whose count the GPU has, else `draws` of them `stride` apart
static void pc_indirect_draw(const pc_kind kind, const pc_indirect op)
{
    if (vk.recording) {
        *(pc_indirect *)pc_add(kind, sizeof(pc_indirect)) = op;
    } else if (kind == PC_DRAW_INDIRECT) {
        vkCmdDrawIndirect(vk.pass, op.buffer, op.offset, op.draws, op.stride);
    } else if (kind == PC_DRAW_INDEXED_INDIRECT) {
        vkCmdDrawIndexedIndirect(vk.pass, op.buffer, op.offset, op.draws, op.stride);
    } else {
        vkCmdDrawIndexedIndirectCount(vk.pass, op.buffer, op.offset, op.counts, op.count_offset, op.draws, op.stride);
    }
}

// What a record says, into `cb`
static void replay(const VkCommandBuffer cb, const vk_deferred *d)
{
    for (size_t at = 0; at < d->used;) {
        const pc_head *h = (const pc_head *)(const void *)(d->ops + at);
        const void *op = h + 1;
        at += h->size;
        switch ((pc_kind)h->kind) {
        case PC_TURN: {
            const pc_turn *t = op;
            turn(cb, t->image, t->format, t->level, t->levels, t->layer, t->layers, t->from, t->to);
            break;
        }
        case PC_BARRIER_PASS:
            barrier_pass(cb);
            break;
        case PC_QUERY_RESET: {
            const pc_query *q = op;
            vkCmdResetQueryPool(cb, q->pool, q->index, q->count);
            break;
        }
        case PC_TIMESTAMP: {
            const pc_query *q = op;
            vkCmdWriteTimestamp(cb, q->stage, q->pool, q->index);
            break;
        }
        case PC_RENDERING: {
            const pc_rendering *r = op;
            const VkRenderingInfo info = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = r->area, .layerCount = 1,
                                          .colorAttachmentCount = r->color_count, .pColorAttachments = r->colors,
                                          .pDepthAttachment = &r->depth, .pStencilAttachment = &r->depth};
            vkCmdBeginRendering(cb, &info);
            break;
        }
        case PC_VIEWPORT:
            vkCmdSetViewport(cb, 0, 1, op);
            break;
        case PC_SCISSOR:
            vkCmdSetScissor(cb, 0, 1, op);
            break;
        case PC_STENCIL:
            vkCmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_AND_BACK, *(const uint32_t *)op);
            break;
        case PC_PIPELINE: {
            const pc_pipeline *b = op;
            vkCmdBindPipeline(cb, b->point, b->pipeline);
            break;
        }
        case PC_PUSH: {
            const pc_push *push = op;
            VkWriteDescriptorSet writes[1 + 2 * TIDE_GPU_MAX_TEXTURES + TIDE_GPU_MAX_STORAGE];
            for (uint32_t i = 0; i < push->count; i++) {
                const pc_write *w = &push->writes[i];
                const bool buffer = w->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || w->type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                writes[i] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, VK_NULL_HANDLE, w->binding, 0, 1,
                                                   w->type, buffer ? NULL : &w->image, buffer ? &w->buffer : NULL, NULL};
            }
            vkCmdPushDescriptorSetKHR(cb, push->point, push->layout, 0, push->count, writes);
            break;
        }
        case PC_SETS: {
            const pc_sets *b = op;
            vkCmdBindDescriptorSets(cb, b->point, b->layout, 0, 1, &b->set, 0, NULL);
            break;
        }
        case PC_VERTEX: {
            const pc_vertex *v = op;
            vkCmdBindVertexBuffers(cb, v->slot, 1, &v->buffer, &v->offset);
            break;
        }
        case PC_INDEX: {
            const pc_index *i = op;
            vkCmdBindIndexBuffer(cb, i->buffer, 0, i->type);
            break;
        }
        case PC_DRAW: {
            const pc_draws *draw = op;
            vkCmdDraw(cb, draw->count, draw->instances, draw->first, 0);
            break;
        }
        case PC_DRAW_INDEXED: {
            const pc_draws *draw = op;
            vkCmdDrawIndexed(cb, draw->count, draw->instances, 0, 0, 0);
            break;
        }
        case PC_DRAW_INDIRECT: {
            const pc_indirect *i = op;
            vkCmdDrawIndirect(cb, i->buffer, i->offset, i->draws, i->stride);
            break;
        }
        case PC_DRAW_INDEXED_INDIRECT: {
            const pc_indirect *i = op;
            vkCmdDrawIndexedIndirect(cb, i->buffer, i->offset, i->draws, i->stride);
            break;
        }
        case PC_DRAW_INDEXED_INDIRECT_COUNT: {
            const pc_indirect *i = op;
            vkCmdDrawIndexedIndirectCount(cb, i->buffer, i->offset, i->counts, i->count_offset, i->draws, i->stride);
            break;
        }
        case PC_END_RENDERING:
            vkCmdEndRendering(cb);
            break;
        }
    }
}

// A command pool of the frame's that no other thread has, till it's given back: NULL if every one is taken
// (PASS_POOLS of them) or none can be made
static vk_pass_pool *take_pass_pool(vk_frame *f)
{
    for (uint32_t i = 0; i < PASS_POOLS; i++) {
        uint32_t free_pool = POOL_FREE;
        if (__atomic_compare_exchange_n(&f->pass_pools[i].state, &free_pool, POOL_TAKEN, false, __ATOMIC_ACQUIRE,
                                        __ATOMIC_RELAXED)) return &f->pass_pools[i];
    }
    for (uint32_t i = 0; i < PASS_POOLS; i++) {
        uint32_t none = POOL_NONE;
        if (!__atomic_compare_exchange_n(&f->pass_pools[i].state, &none, POOL_MAKING, false, __ATOMIC_ACQUIRE,
                                         __ATOMIC_RELAXED)) continue;
        vk_pass_pool *p = &f->pass_pools[i];
        const VkCommandPoolCreateInfo info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL,
                                              VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, vk.family};
        if (vkCreateCommandPool(vk.device, &info, NULL, &p->pool) != VK_SUCCESS) {
            __atomic_store_n(&p->state, POOL_NONE, __ATOMIC_RELEASE);
            return NULL;
        }
        __atomic_store_n(&p->state, POOL_TAKEN, __ATOMIC_RELAXED);
        return p;
    }
    return NULL;
}

// A record's commands into a command buffer of its own: a job of the pool's, or the frame's thread
static void record_pass(void *context)
{
    vk_deferred *d = context;
    vk_frame *f = &vk.frames[d->frame];
    vk_pass_pool *p = take_pass_pool(f);
    if (!p) {
        d->result = VK_ERROR_OUT_OF_HOST_MEMORY;
        return;
    }
    VkResult result = VK_SUCCESS;
    if (p->used == p->allocated) {
        const uint32_t more = p->allocated ? p->allocated : 4u;
        p->buffers = tide_realloc(p->buffers, p->allocated * sizeof(VkCommandBuffer), (p->allocated + more) * sizeof(VkCommandBuffer));
        const VkCommandBufferAllocateInfo info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, p->pool,
                                                  VK_COMMAND_BUFFER_LEVEL_PRIMARY, more};
        result = vkAllocateCommandBuffers(vk.device, &info, p->buffers + p->allocated);
        if (result == VK_SUCCESS) p->allocated += more;
    }
    if (result == VK_SUCCESS) {
        const VkCommandBuffer cb = p->buffers[p->used++];
        const VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL,
                                                VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL};
        result = vkBeginCommandBuffer(cb, &begin);
        if (result == VK_SUCCESS) {
            replay(cb, d);
            result = vkEndCommandBuffer(cb);
        }
        if (result == VK_SUCCESS) d->cb = cb;
    }
    d->result = result;
    __atomic_store_n(&p->state, POOL_FREE, __ATOMIC_RELEASE);
}

// The pass written down goes to be recorded
static void send_pass(vk_deferred *d)
{
    d->sent = true;
    // Ahead of what else is queued (a helper): pipelines being made in the background are hundreds of jobs of
    // tens of milliseconds each, and a frame's passes don't wait their turn behind them
    d->job = (tide_background){.work = record_pass, .context = d, .helper = true};
    const tide_jobs *jobs = tide_platform_jobs();
    if (jobs && jobs->background) {
        jobs->background(jobs->self, &d->job);
        return;
    }
    record_pass(d);
    __atomic_store_n(&d->job.done, 1u, __ATOMIC_RELEASE);
}

// Every pass sent is recorded, and in its place in `pending`; then the records are free for the next. Meanwhile
// this thread records passes too, and only those (helpers): never a pipeline queued for the pool's threads, which
// the driver can take seconds over, with the window not answering. A pass that couldn't be recorded loses the device, as a command that fails does.
static void settle_passes(void)
{
    if (!vk.deferred_count) return;
    const tide_jobs *jobs = tide_platform_jobs();
    VkResult failed = VK_SUCCESS;
    for (uint32_t i = 0; i < vk.deferred_count; i++) {
        vk_deferred *d = vk.deferred[i];
        if (!d->sent) continue;
        while (!__atomic_load_n(&d->job.done, __ATOMIC_ACQUIRE)) {
            if (jobs && jobs->help) jobs->help(jobs->self, true);
        }
        if (d->result != VK_SUCCESS && failed == VK_SUCCESS) failed = d->result;
    }
    uint32_t kept = 0;
    for (uint32_t i = 0; i < vk.pending_count; i++) {
        VkCommandBuffer cb = vk.pending[i];
        if (is_pass_tag(cb)) {
            const uint32_t index = (uint32_t)((uintptr_t)cb >> 1);
            const vk_deferred *d = index < vk.deferred_count ? vk.deferred[index] : NULL;
            cb = d && d->sent ? d->cb : VK_NULL_HANDLE;
        }
        if (cb) vk.pending[kept++] = cb;
    }
    vk.pending_count = kept;
    vk.deferred_count = 0;
    if (failed != VK_SUCCESS) lose(failed);
}

// The frame's pools for passes, empty again: none of their command buffers is pending or still being run
static void reset_pass_pools(vk_frame *f)
{
    for (uint32_t i = 0; i < PASS_POOLS; i++) {
        vk_pass_pool *p = &f->pass_pools[i];
        if (p->state == POOL_FREE && p->used) {
            vkResetCommandPool(vk.device, p->pool, 0);
            p->used = 0;
        }
    }
}

static void turn_for_pass(const VkImage image, const VkFormat format, const uint32_t layer, const VkImageLayout rest,
                          const VkImageLayout drawn)
{
    pc_turn_image(image, format, 0, 1, layer, 1, rest, drawn);
    turned[turned_count++] = (vk_turned){image, format, layer, rest, drawn};
}

static void vk_begin(const tide_gpu_id target, const bool clear)
{
    vk.in_pass = true;
    vk.skip_pass = true; // Until there's something to draw in
    vk.current = 0;
    vk.bound = false;
    vk.pushed = NO_PUSH; // A command buffer of its own
    uniform_buffer = VK_NULL_HANDLE; // A pass's draws read the uniforms written in it
    vk.uniforms_at = 0;
    turned_count = 0;
    if (vk.lost) return;
    if (!target && !acquire()) return; // A window with nothing to draw in (minimized): its draws go nowhere
    if (vk.lost) return;
    flush_if_full();
    // What's recorded outside passes since the last one goes before it, and what's uploaded during it, after that
    close_outside();
    // Written down, and recorded on the pool's threads as it ends
    vk.pass = pass_tag(vk.deferred_count);
    vk.recording = deferred_take();
    if (vk.pass_active) {
        const uint32_t at = (vk.pass_first + vk.pass_count) % TIDE_GPU_PROFILE_PENDING;
        pc_query_reset(vk.pass_queries, 2 * at, 2);
        pc_timestamp(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, vk.pass_queries, 2 * at);
    }
    pc_barrier_pass();
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
        pc_turn_image(vk.images[vk.image], vk.window_format, 0, 1, 0, 1,
                      vk.image_drawn ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        colors[0] = (VkRenderingAttachmentInfo){.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = vk.views[vk.image],
                                                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                .loadOp = vk.image_drawn ? load : VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .clearValue = black};
        color_count = 1;
        if (vk.window_depth_new) {
            pc_turn_image(vk.window_depth, vk.depth_format, 0, 1, 0, 1, VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            vk.window_depth_new = false;
            depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        }
        if (!vk.image_drawn) depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.imageView = vk.window_depth_view;
        vk.image_drawn = true;
    } else {
        vk_target *t = &vk.targets[target - 1u];
        samples_back(vk.pass, t); // A kernel read its samples since the last pass on it
        width = t->width, height = t->height;
        for (uint32_t i = 0; i < t->color_count; i++) {
            const vk_texture *c = t->face ? texture_of(t->cube) : texture_of(t->colors[i]);
            const VkImageView view = t->array ? c->layer_views[t->layer] : t->face ? c->faces[t->layer] : c->attach;
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
        } else if (t->array) { // That layer of its array's depth
            const vk_texture *d = texture_of(t->depth_layers);
            turn_for_pass(d->image, d->format, t->layer, d->rest, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            depth.imageView = d->layer_views[t->layer];
        } else {
            const vk_texture *d = texture_of(t->depth);
            turn_for_pass(d->image, d->format, 0, d->rest, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            depth.imageView = d->attach;
        }
    }
    vk.pass_width = width, vk.pass_height = height;
    pc_rendering *rendering = pc_add(PC_RENDERING, sizeof(pc_rendering));
    *rendering = (pc_rendering){.area = {{0, 0}, {(uint32_t)width, (uint32_t)height}}, .color_count = color_count, .depth = depth};
    memcpy(rendering->colors, colors, color_count * sizeof colors[0]);
    // Upside down: y goes up in clip space, and rows go down from the top
    *(VkViewport *)pc_add(PC_VIEWPORT, sizeof(VkViewport)) = (VkViewport){0.0f, (float)height, (float)width, -(float)height, 0.0f, 1.0f};
    pc_scissor((VkRect2D){{0, 0}, {(uint32_t)width, (uint32_t)height}});
    pc_stencil(0);
}

static void vk_end(void)
{
    vk.in_pass = false;
    if (vk.skip_pass) return;
    if (vk.lost) { // Nothing recorded goes
        vk.pass = vk.before = VK_NULL_HANDLE;
        vk.recording = NULL; // Never sent: its place in `pending` goes as it's settled
        return;
    }
    (void)pc_add(PC_END_RENDERING, 0);
    for (uint32_t i = 0; i < turned_count; i++) {
        pc_turn_image(turned[i].image, turned[i].format, 0, 1, turned[i].layer, 1, turned[i].drawn, turned[i].rest);
    }
    turned_count = 0;
    if (!vk.pass_target) {
        pc_turn_image(vk.images[vk.image], vk.window_format, 0, 1, 0, 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    }
    if (vk.pass_active) {
        const uint32_t at = (vk.pass_first + vk.pass_count) % TIDE_GPU_PROFILE_PENDING;
        pc_timestamp(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, vk.pass_queries, 2 * at + 1);
        vk.pass_count++;
    }
    send_pass(vk.recording);
    vk.recording = NULL;
    // What's recorded outside passes next goes in a command buffer after it (the pass's is the pool's)
    vk.outside = VK_NULL_HANDLE;
    vk.drawn = true;
    vk.pass = VK_NULL_HANDLE;
    if (vk.before) check(vkEndCommandBuffer(vk.before), "vkEndCommandBuffer");
    vk.before = VK_NULL_HANDLE;
}

static void vk_scissor(const int x, const int y, const int width, const int height)
{
    if (vk.skip_pass) return;
    const VkRect2D rect = {{x > 0 ? x : 0, y > 0 ? y : 0}, {(uint32_t)(width > 0 ? width : 0), (uint32_t)(height > 0 ? height : 0)}};
    pc_scissor(rect);
}

static void vk_use(const tide_gpu_id id)
{
    if (vk.skip_pass) return;
    finish_making(id); // The renderer draws with one made in the background once it's ready: this is for one that isn't
    const vk_pipeline *p = &vk.pipelines[id - 1u];
    if (p->compute != vk.in_compute) {
        fprintf(stderr, p->compute ? "tide: a compute pipeline is used in a pass: it's for compute passes (compute_begin)\n"
                                   : "tide: a pipeline that draws is used in a compute pass\n");
        abort();
    }
    if (vk.in_compute) {
        vk.current = id;
        vk.bound = false;
        vkCmdBindPipeline(vk.pass, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
        return;
    }
    if (id == vk.current) return; // Bound already (the check below was made as it was), with what it reads
    // A pipeline is for one kind of target
    const tide_gpu_target_format kind = vk.pass_target ? vk.targets[vk.pass_target - 1u].format : TIDE_GPU_TARGET_COLOR;
    if (p->into != kind) {
        fprintf(stderr, "tide: a pipeline is for targets that hold something else than the one it draws into "
                        "(tide_gpu_pipeline_desc.into)\n");
        abort();
    }
    vk.current = id;
    vk.bound = false;
    pc_pipeline_bind(VK_PIPELINE_BIND_POINT_GRAPHICS, p->pipeline);
}

static void vk_stencil(const uint32_t value)
{
    if (vk.skip_pass) return;
    pc_stencil(value < 255u ? value : 255u);
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

static void vk_image(const uint32_t slot, const tide_gpu_id texture)
{
    if (vk.skip_pass || slot >= TIDE_GPU_MAX_TEXTURES) return;
    vk.textures_bound[slot] = texture;
    vk.bound = false;
}

static void vk_sampler(const uint32_t slot, const tide_gpu_filter filter, const tide_gpu_wrap wrap)
{
    if (vk.skip_pass || slot >= TIDE_GPU_MAX_TEXTURES) return;
    vk.sampled[slot] = (uint8_t)(((uint32_t)wrap < WRAPS ? (uint32_t)wrap : 0u) * FILTERS
                                 + ((uint32_t)filter < FILTERS ? (uint32_t)filter : (uint32_t)TIDE_GPU_FILTER_LINEAR));
    vk.bound = false;
}

static void vk_vertices(const uint32_t slot, const tide_gpu_id buffer, const size_t offset)
{
    if (vk.skip_pass) return;
    pc_vertices(slot, vk.buffers[buffer - 1u].buffer, offset);
}

static void vk_indices(const tide_gpu_id buffer, const bool wide)
{
    if (vk.skip_pass) return;
    pc_indices(vk.buffers[buffer - 1u].buffer, wide ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
}

// What a draw reads: the uniforms where they were written, each texture, and
// each sampler of the kind the pipeline reads it as. False for a draw with
// no uniforms written in its pass, which draws nothing.
static bool bind(void)
{
    if (vk.bound) return true;
    if (!uniform_buffer) return false;
    const vk_pipeline *p = &vk.pipelines[vk.current - 1u];
    const VkPipelineBindPoint point = vk.in_compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
    const vk_layout *layout = &vk.layouts[p->layout];
    // Pushed through this layout already in this command buffer: only what changed is pushed again
    const bool kept = layout->push && vk.pushed == p->layout;
    VkDescriptorBufferInfo block = {uniform_buffer, vk.uniforms_at, TIDE_GPU_MAX_UNIFORMS};
    VkDescriptorImageInfo images[TIDE_GPU_MAX_TEXTURES], samplers[TIDE_GPU_MAX_TEXTURES];
    VkDescriptorBufferInfo storage[TIDE_GPU_MAX_STORAGE];
    VkWriteDescriptorSet writes[1 + 2 * TIDE_GPU_MAX_TEXTURES + TIDE_GPU_MAX_STORAGE];
    uint32_t count = 0;
    if (!kept || vk.pushed_uniforms != uniform_buffer || vk.pushed_uniforms_at != vk.uniforms_at) {
        writes[count++] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, VK_NULL_HANDLE, 0, 0, 1,
                                                 VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, NULL, &block, NULL};
    }
    for (uint32_t i = 0; i < p->texture_count && i < TIDE_GPU_MAX_TEXTURES; i++) {
        const vk_texture *t = texture_of(vk.textures_bound[i]);
        if (!t) continue;
        // Until its smaller images are made only its first is bound, so what's read between them is its own
        // pixels, as a texture with none is, through whichever of the pipeline's samplers reads it
        const VkImageView first = t->cube || t->arrayed ? t->base : t->attach;
        const VkImageView view = t->mipped || t->levels < 2 || t->depth || !first ? t->view : first;
        if (kept && vk.pushed_views[i] == view && vk.pushed_view_layouts[i] == t->rest) continue;
        images[i] = (VkDescriptorImageInfo){VK_NULL_HANDLE, view, t->rest};
        writes[count++] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, VK_NULL_HANDLE, 1u + 2u * i, 0, 1,
                                                 VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &images[i], NULL, NULL};
    }
    for (uint32_t j = 0; j < p->sampler_count && j < TIDE_GPU_MAX_TEXTURES; j++) {
        const uint8_t read = p->sampler_reads[j];
        if (read == TIDE_GPU_READ_DATA) continue; // A binding it hasn't
        const uint32_t filter = vk.sampled[j] % FILTERS, wrap = vk.sampled[j] / FILTERS;
        uint32_t sampler;
        if (read == TIDE_GPU_READ_COMPARE) sampler = SAMPLER_COMPARE;
        else if (read == TIDE_GPU_READ_DEPTH) sampler = TIDE_GPU_FILTER_NEAREST; // Clamped
        else if (read == TIDE_GPU_READ_CUBE) sampler = filter; // A cube has no edge to read past
        else sampler = wrap * FILTERS + filter;
        if (kept && vk.pushed_samplers[j] == vk.samplers[sampler]) continue;
        samplers[j] = (VkDescriptorImageInfo){vk.samplers[sampler], VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
        writes[count++] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, VK_NULL_HANDLE, 2u + 2u * j, 0, 1,
                                                 VK_DESCRIPTOR_TYPE_SAMPLER, &samplers[j], NULL, NULL};
    }
    // Each storage buffer whole, at its binding after the textures'
    for (uint32_t i = 0; i < p->storage_count; i++) {
        const vk_buffer *b = storage_of(vk.storage_bound[i]);
        if (!b) continue;
        // ...as far as a shader reaches one (past that, an index reads nothing, as past its end)
        const VkDeviceSize reach = vk.properties.limits.maxStorageBufferRange;
        const VkDeviceSize range = b->size < 4u ? 4u : b->size < reach ? b->size : reach;
        if (kept && vk.pushed_storage[i] == b->buffer && vk.pushed_storage_range[i] == range) continue;
        storage[i] = (VkDescriptorBufferInfo){b->buffer, 0, range};
        writes[count++] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, VK_NULL_HANDLE,
                                                 TIDE_GPU_STORAGE_BINDING + i, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL,
                                                 &storage[i], NULL};
    }
    if (count && layout->push) {
        pc_push_writes(point, layout->layout, count, writes);
        // What each binding now holds, for the next draw's
        if (!kept) {
            memset(vk.pushed_views, 0, sizeof vk.pushed_views);
            memset(vk.pushed_samplers, 0, sizeof vk.pushed_samplers);
            memset(vk.pushed_storage, 0, sizeof vk.pushed_storage);
        }
        vk.pushed = p->layout;
        for (uint32_t i = 0; i < count; i++) {
            const VkWriteDescriptorSet *w = &writes[i];
            if (w->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
                vk.pushed_uniforms = w->pBufferInfo->buffer;
                vk.pushed_uniforms_at = w->pBufferInfo->offset;
            } else if (w->descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) {
                vk.pushed_views[(w->dstBinding - 1u) / 2u] = w->pImageInfo->imageView;
                vk.pushed_view_layouts[(w->dstBinding - 1u) / 2u] = w->pImageInfo->imageLayout;
            } else if (w->descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER) {
                vk.pushed_samplers[(w->dstBinding - 2u) / 2u] = w->pImageInfo->sampler;
            } else {
                vk.pushed_storage[w->dstBinding - TIDE_GPU_STORAGE_BINDING] = w->pBufferInfo->buffer;
                vk.pushed_storage_range[w->dstBinding - TIDE_GPU_STORAGE_BINDING] = w->pBufferInfo->range;
            }
        }
    } else if (count) {
        // A set of the frame's, with what the draw reads, bound for it: the GPU reads it as the frame runs
        const VkDescriptorSet set = frame_set(layout->set);
        if (!set) return false;
        for (uint32_t i = 0; i < count; i++) writes[i].dstSet = set;
        vkUpdateDescriptorSets(vk.device, count, writes, 0, NULL);
        pc_sets_bind(point, layout->layout, set);
    }
    vk.bound = true;
    return true;
}

static void vk_draw(const uint32_t first, const uint32_t count, const uint32_t instances)
{
    if (vk.skip_pass || !vk.current || !bind()) return;
    pc_draw(first, count, instances);
}

static void vk_draw_indexed(const uint32_t count, const uint32_t instances)
{
    if (vk.skip_pass || !vk.current || !bind()) return;
    pc_draw_indexed(count, instances);
}

// ---------------------------------------------------------------------------
// Compute: storage buffers, compute passes, what the GPU's numbers draw, and
// what's copied and read back.
//
// A compute pass is a command buffer of its own, as a pass is: what's written
// or uploaded while it's on goes into one before it (`before`), what's recorded
// outside passes after it goes on in its own, and its barriers are by the
// stages that write and read (barrier_compute, barrier_dispatches,
// barrier_computed).

static void vk_storage(const uint32_t slot, const tide_gpu_id buffer)
{
    if (vk.skip_pass || slot >= TIDE_GPU_MAX_STORAGE) return;
    vk.storage_bound[slot] = buffer;
    vk.bound = false;
}

static void vk_compute_begin(void)
{
    vk.in_compute = true;
    vk.skip_pass = true; // Until it has a command buffer
    vk.current = 0;
    vk.bound = false;
    vk.pushed = NO_PUSH; // A command buffer of its own
    vk.dispatched = false;
    uniform_buffer = VK_NULL_HANDLE; // A compute pass's dispatches read the uniforms written in it
    vk.uniforms_at = 0;
    if (vk.lost) return;
    flush_if_full();
    if (vk.lost) return;
    close_outside();
    vk.pass = command_buffer();
    barrier_compute(vk.pass);
    vk.pass_pending = vk.pending_count;
    queue_up(vk.pass);
    vk.before = VK_NULL_HANDLE;
    vk.skip_pass = false;
}

static void vk_compute_end(void)
{
    vk.in_compute = false;
    if (vk.skip_pass) return;
    vk.skip_pass = true;
    if (vk.lost) { // Nothing recorded goes
        vk.pass = vk.before = VK_NULL_HANDLE;
        return;
    }
    if (vk.dispatched) barrier_computed(vk.pass);
    vk.outside = vk.pass; // What's recorded outside passes next goes on after it
    vk.pass = VK_NULL_HANDLE;
    if (vk.before) check(vkEndCommandBuffer(vk.before), "vkEndCommandBuffer");
    vk.before = VK_NULL_HANDLE;
}

// Whether a dispatch can go, with what it reads bound and after the one before it.
static bool dispatching(void)
{
    if (!vk.in_compute || vk.skip_pass || !vk.current || !bind()) return false;
    if (vk.dispatched) barrier_dispatches(vk.pass);
    vk.dispatched = true;
    return true;
}

static void vk_dispatch(const uint32_t x, const uint32_t y, const uint32_t z)
{
    if (!x || !y || !z) return; // None run
    if (!dispatching()) return;
    const uint32_t *most = vk.properties.limits.maxComputeWorkGroupCount;
    vkCmdDispatch(vk.pass, x < most[0] ? x : most[0], y < most[1] ? y : most[1], z < most[2] ? z : most[2]);
}

static void vk_dispatch_indirect(const tide_gpu_id buffer, const size_t offset)
{
    const vk_buffer *b = storage_of(buffer);
    if (!b || offset % 4u || offset > b->size || b->size - offset < 12u || !dispatching()) return;
    vkCmdDispatchIndirect(vk.pass, b->buffer, offset);
}

static void vk_draw_indirect(const tide_gpu_id buffer, const size_t offset)
{
    const vk_buffer *b = storage_of(buffer);
    if (vk.skip_pass || vk.in_compute || !vk.current || !b || offset % 4u || offset > b->size || b->size - offset < 16u
        || !bind()) return;
    pc_indirect_draw(PC_DRAW_INDIRECT, (pc_indirect){.buffer = b->buffer, .offset = offset, .draws = 1, .stride = 16});
}

static void vk_draw_indexed_indirect(const tide_gpu_id buffer, const size_t offset)
{
    const vk_buffer *b = storage_of(buffer);
    if (vk.skip_pass || vk.in_compute || !vk.current || !b || offset % 4u || offset > b->size || b->size - offset < 20u
        || !bind()) return;
    pc_indirect_draw(PC_DRAW_INDEXED_INDIRECT, (pc_indirect){.buffer = b->buffer, .offset = offset, .draws = 1, .stride = 20});
}

static void vk_draw_indexed_indirect_count(const tide_gpu_id buffer, const size_t offset, const uint32_t most,
                                           const tide_gpu_id counts, const size_t count_offset)
{
    const vk_buffer *b = storage_of(buffer), *c = storage_of(counts);
    if (vk.skip_pass || vk.in_compute || !vk.current || !b || !c || !most || offset % 4u || offset > b->size
        || (b->size - offset) / 20u < most || count_offset % 4u || count_offset > c->size || c->size - count_offset < 4u
        || !bind()) return;
    if (vk.indirect & TIDE_GPU_INDIRECT_COUNT) { // One call, as many as the GPU counted
        const uint32_t limit = vk.properties.limits.maxDrawIndirectCount;
        pc_indirect_draw(PC_DRAW_INDEXED_INDIRECT_COUNT,
                         (pc_indirect){b->buffer, c->buffer, offset, count_offset, most < limit ? most : limit, 20});
    } else if (vk.multi_draw && most <= vk.properties.limits.maxDrawIndirectCount) { // ...or all of them, those
        pc_indirect_draw(PC_DRAW_INDEXED_INDIRECT,                                     // past the count of none
                         (pc_indirect){.buffer = b->buffer, .offset = offset, .draws = most, .stride = 20});
    } else {
        for (uint32_t i = 0; i < most; i++) {
            pc_indirect_draw(PC_DRAW_INDEXED_INDIRECT,
                             (pc_indirect){.buffer = b->buffer, .offset = offset + (size_t)i * 20u, .draws = 1, .stride = 20});
        }
    }
}

static uint32_t vk_indirect(void)
{
    return vk.indirect;
}

// Copies and writes in one command buffer have no order of their own: one
// that reads or writes what one before wrote goes after it.
static void barrier_copies(const VkCommandBuffer cb)
{
    barrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
}

// Whether `size` bytes from `offset` are inside a buffer of `has`.
static bool inside(const VkDeviceSize has, const size_t offset, const size_t size)
{
    return offset <= has && size <= has - offset;
}

static void vk_buffer_copy(const tide_gpu_id from, const size_t from_offset, const tide_gpu_id to, const size_t to_offset,
                           const size_t size)
{
    const vk_buffer *a = storage_of(from), *b = storage_of(to);
    if (!a && from && from <= vk.buffer_count && vk.buffers[from - 1u].buffer && vk.buffers[from - 1u].indices) {
        a = &vk.buffers[from - 1u]; // An index buffer is copied from too
    }
    if (vk.lost || !size || !a || !b || !inside(a->size, from_offset, size) || !inside(b->size, to_offset, size)) return;
    // Bytes copied onto themselves are no copy Vulkan (or WebGPU) makes
    if (from == to && from_offset < to_offset + size && to_offset < from_offset + size) return;
    const VkCommandBuffer cb = outside();
    barrier_copies(cb);
    const VkBufferCopy copy = {from_offset, to_offset, size};
    vkCmdCopyBuffer(cb, a->buffer, b->buffer, 1, &copy);
}

// Whether the GPU is done with submission `serial`, asking the fences of the
// frames it's waited on by without waiting for them.
static bool reached(const uint64_t serial)
{
    if (serial <= vk.completed) return true;
    if (vk.lost || serial > vk.submitted) return false;
    for (uint32_t i = 0; i < FRAMES; i++) {
        const vk_frame *f = &vk.frames[i];
        if (!f->fenced || f->serial < serial) continue;
        const VkResult status = vkGetFenceStatus(vk.device, f->fence);
        if (status == VK_SUCCESS) {
            vk.completed = f->serial; // Everything submitted before it is done too
            return true;
        }
        if (status != VK_NOT_READY) lose(status);
    }
    return false;
}

// A read's buffer let go of: kept for the next read where there's room for
// it, or destroyed once the GPU is done with the frame.
static void release_read(vk_read *r)
{
    if (r->buffer && !vk.lost) {
        // In a free spare place, or in place of the smallest spare where it's bigger
        vk_read *s = &vk.spare_reads[0];
        for (uint32_t i = 0; i < sizeof vk.spare_reads / sizeof vk.spare_reads[0]; i++) {
            if (!vk.spare_reads[i].buffer) {
                s = &vk.spare_reads[i];
                break;
            }
            if (vk.spare_reads[i].room < s->room) s = &vk.spare_reads[i];
        }
        if (!s->buffer || s->room < r->room) {
            if (s->buffer) DISCARD(VK_OBJECT_TYPE_BUFFER, s->buffer, &s->memory);
            *s = (vk_read){.buffer = r->buffer, .memory = r->memory, .room = r->room};
            r->buffer = VK_NULL_HANDLE;
        }
    }
    if (r->buffer) DISCARD(VK_OBJECT_TYPE_BUFFER, r->buffer, &r->memory);
    *r = (vk_read){0};
}

static vk_read *read_of(const uint32_t token)
{
    for (uint32_t i = 0; i < vk.read_count; i++) {
        if (token && vk.reads[i].token == token) return &vk.reads[i];
    }
    return NULL;
}

static uint32_t vk_buffer_read(const tide_gpu_id buffer, const size_t offset, const size_t size)
{
    const vk_buffer *b = storage_of(buffer);
    if (vk.lost || !size || !b || !inside(b->size, offset, size)) return 0;
    uint32_t place = 0;
    while (place < vk.read_count && vk.reads[place].token) place++;
    if (place == vk.read_count) {
        if (vk.read_count == vk.read_capacity) {
            const uint32_t capacity = vk.read_capacity ? vk.read_capacity * 2u : 16u;
            vk.reads = tide_realloc(vk.reads, vk.read_capacity * sizeof(vk_read), capacity * sizeof(vk_read));
            vk.read_capacity = capacity;
        }
        vk.reads[vk.read_count++] = (vk_read){0};
    }
    vk_read *r = &vk.reads[place];
    // Memory the host reads, of a spare read's where one has room and isn't far bigger
    VkDeviceSize room = 256;
    while (room < size) room *= 2u;
    for (uint32_t i = 0; i < sizeof vk.spare_reads / sizeof vk.spare_reads[0] && !r->buffer; i++) {
        vk_read *s = &vk.spare_reads[i];
        if (s->buffer && s->room >= room && s->room <= room * 4u) {
            r->buffer = s->buffer, r->memory = s->memory, r->room = s->room;
            *s = (vk_read){0};
        }
    }
    if (!r->buffer) {
        if (!make_buffer(room, VK_BUFFER_USAGE_TRANSFER_DST_BIT, USE_READBACK, &r->buffer, &r->memory)) {
            *r = (vk_read){0};
            return 0;
        }
        r->room = room;
    }
    do vk.read_token++;
    while (!vk.read_token || read_of(vk.read_token));
    r->token = vk.read_token;
    r->size = size;
    r->serial = vk.submitted + 1u; // Everything recorded goes with the next submission, whichever submits it
    const VkCommandBuffer cb = outside();
    barrier_copies(cb);
    const VkBufferCopy copy = {offset, 0, size};
    vkCmdCopyBuffer(cb, b->buffer, r->buffer, 1, &copy);
    barrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
    return r->token;
}

static int vk_buffer_read_done(const uint32_t token, void *data)
{
    vk_read *r = read_of(token);
    if (!r) return -1;
    if (!r->lost && !reached(r->serial)) {
        if (!vk.lost) return 0;
        r->lost = true; // What was recorded with it never goes to the GPU
    }
    const bool lost = r->lost;
    if (!lost) memcpy(data, r->memory.mapped, r->size);
    release_read(r);
    return lost ? -1 : 1;
}

static void vk_buffer_read_wait(void)
{
    for (uint32_t i = 0; i < vk.read_count; i++) {
        const vk_read *r = &vk.reads[i];
        if (r->token && !r->lost && r->serial > vk.completed) {
            flush(); // Everything goes, and is waited for
            return;
        }
    }
}

// Every read's memory goes with the device: those not yet done are lost.
static void forget_reads(void)
{
    for (uint32_t i = 0; i < vk.read_count; i++) {
        vk_read *r = &vk.reads[i];
        if (r->buffer) vkDestroyBuffer(vk.device, r->buffer, NULL);
        give_back(&r->memory);
        r->buffer = VK_NULL_HANDLE;
        r->memory = (vk_alloc){0};
        if (r->token) r->lost = true; // ...and whatever had come back with them, which nobody took
    }
    for (uint32_t i = 0; i < sizeof vk.spare_reads / sizeof vk.spare_reads[0]; i++) {
        vk_read *s = &vk.spare_reads[i];
        if (s->buffer) vkDestroyBuffer(vk.device, s->buffer, NULL);
        give_back(&s->memory);
        *s = (vk_read){0};
    }
}

// ---------------------------------------------------------------------------
// Timing: timestamps around the game's drawing, read when they're there

static bool vk_pass_time_begin(const uint32_t token)
{
    if (vk.lost || !vk.timestamp_ns || vk.pass_active || vk.pass_count == TIDE_GPU_PROFILE_PENDING) return false;
    if (!vk.pass_queries) {
        const VkQueryPoolCreateInfo info = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, NULL, 0,
                                           VK_QUERY_TYPE_TIMESTAMP, 2 * TIDE_GPU_PROFILE_PENDING, 0};
        if (vkCreateQueryPool(vk.device, &info, NULL, &vk.pass_queries) != VK_SUCCESS) return false;
    }
    const uint32_t at = (vk.pass_first + vk.pass_count) % TIDE_GPU_PROFILE_PENDING;
    vk.pass_tokens[at] = token;
    vk.pass_active = true;
    return true;
}

static void vk_pass_time_end(void)
{
    vk.pass_active = false;
}

static double vk_pass_time_read(uint32_t *token)
{
    if (vk.lost || !vk.pass_sent || vk.pass_serial[vk.pass_first] > vk.completed) return -1.0;
    uint64_t results[4];
    const uint32_t at = vk.pass_first;
    const VkResult result = vkGetQueryPoolResults(vk.device, vk.pass_queries, 2 * at, 2, sizeof results, results,
        2 * sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    if ((result != VK_SUCCESS && result != VK_NOT_READY) || !results[1] || !results[3]) return -1.0;
    *token = vk.pass_tokens[at];
    vk.pass_first = (at + 1) % TIDE_GPU_PROFILE_PENDING;
    vk.pass_count--;
    vk.pass_sent--;
    return (double)((results[2] - results[0]) & vk.timestamp_mask) * vk.timestamp_ns / 1000000.0;
}

static void vk_pass_time_reset(void)
{
    if (vk.pass_queries) {
        flush();
        vkDestroyQueryPool(vk.device, vk.pass_queries, NULL);
    }
    vk.pass_queries = VK_NULL_HANDLE;
    vk.pass_first = vk.pass_count = vk.pass_sent = 0;
    vk.pass_active = false;
}

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
// page's WebGPU errors fail it.
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
#define VK_LOAD(name) name = (PFN_##name)vkGetDeviceProcAddr(vk.device, #name);
    VK_DEVICE_OPTIONAL(VK_LOAD)
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

// A read of the cache's file: the file checked and made a cache of the driver's (cache_read_work, on any thread:
// it touches nothing of vk but the device, which lasts until the read is taken), which the frame's thread then
// takes (cache_take). A big game's file is hundreds of megabytes: a second or so to read, hash and hand the
// driver, in which a window would not answer.
typedef struct cache_reading {
    tide_background job;
    char *file;
    VkPipelineCache read; // What it gave, or none
    size_t length;
    uint64_t hash;
} cache_reading;

static void cache_read_work(void *context)
{
    cache_reading *r = context;
    size_t size = 0;
    uint8_t *file = tide_file_read_whole(r->file, CACHE_HEADER + CACHE_MOST, &size);
    if (!file) return;
    uint64_t length = 0, hash = 0;
    if (size >= CACHE_HEADER) memcpy(&length, file + 8, 8), memcpy(&hash, file + 16, 8);
    const uint8_t *data = file + CACHE_HEADER;
    if (size >= CACHE_HEADER && memcmp(file, cache_magic, sizeof cache_magic) == 0 && length == size - CACHE_HEADER
        && hash == cache_hash(data, (size_t)length) && cache_for_device(data, (size_t)length)) {
        const VkPipelineCacheCreateInfo info = {VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO, NULL, 0, (size_t)length, data};
        if (vkCreatePipelineCache(vk.device, &info, NULL, &r->read) == VK_SUCCESS) {
            r->length = (size_t)length;
            r->hash = hash;
        } else r->read = VK_NULL_HANDLE;
    }
    free(file);
}

static void finish_all_making(void);

// The frame's thread takes what a read gave: the file's cache becomes the device's, with what the device compiled
// meanwhile (the little one merged into the big one). No thread compiles through either as they change.
static void cache_take(cache_reading *r)
{
    if (r->read) {
        finish_all_making();
        if (vk.pipeline_cache && vkMergePipelineCaches(vk.device, r->read, 1, &vk.pipeline_cache) == VK_SUCCESS) {
            vkDestroyPipelineCache(vk.device, vk.pipeline_cache, NULL);
            vk.pipeline_cache = r->read;
            vk.cache_read = r->length;
            vk.cache_hash = r->hash;
        } else vkDestroyPipelineCache(vk.device, r->read, NULL);
    }
    free(r->file);
    free(r);
}

// Waits for the file being read, if one is, and takes it: before the cache is written, switched or let go.
static void finish_cache_read(void)
{
    cache_reading *r = vk.reading;
    if (!r) return;
    const tide_jobs *jobs = tide_platform_jobs();
    while (!__atomic_load_n(&r->job.done, __ATOMIC_ACQUIRE)) {
        if (jobs && jobs->help) jobs->help(jobs->self, true);
    }
    vk.reading = NULL;
    cache_take(r);
}

static bool vk_pipeline_cache_ready(void)
{
    cache_reading *r = vk.reading;
    if (!r) return true;
    if (!__atomic_load_n(&r->job.done, __ATOMIC_ACQUIRE)) return false;
    vk.reading = NULL;
    cache_take(r);
    return true;
}

static void read_pipeline_cache(void)
{
    finish_cache_read();
    vk.cache_read = 0;
    vk.cache_hash = 0;
    if (!vk.cache_file || !vk.pipeline_cache) return;
    cache_reading *r = tide_alloc_zeroed(1, sizeof *r);
    const size_t length = strlen(vk.cache_file) + 1u;
    r->file = memcpy(tide_alloc(length), vk.cache_file, length);
    // Where pipelines are made in the background, so is this: ahead of them in the queue (a helper), as they wait
    // for it (tide_gpu.pipeline_cache_ready)
    const tide_jobs *jobs = vk.background ? tide_platform_jobs() : NULL;
    if (jobs && jobs->background) {
        r->job = (tide_background){.work = cache_read_work, .context = r, .helper = true};
        vk.reading = r;
        jobs->background(jobs->self, &r->job);
        return;
    }
    cache_read_work(r);
    cache_take(r);
}

// What the driver has now goes to the file, unless it's what was read.
static void write_pipeline_cache(void)
{
    finish_cache_read(); // What the file had is in what's written
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
    finish_all_making(); // The cache isn't changed under the threads that compile through it
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
    finish_cache_read();
    finish_all_making();
    settle_passes(); // No job still records with what's let go below
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
        if (vk.pipelines[i].used) vk_pipeline_free(i + 1u);
    }
    for (uint32_t i = 0; i < FRAMES; i++) {
        vk_frame *f = &vk.frames[i];
        collect(f);
        if (f->ring.buffer) vkDestroyBuffer(vk.device, f->ring.buffer, NULL);
        give_back(&f->ring.memory);
        vkDestroyCommandPool(vk.device, f->pool, NULL);
        for (uint32_t p = 0; p < PASS_POOLS; p++) {
            if (f->pass_pools[p].pool) vkDestroyCommandPool(vk.device, f->pass_pools[p].pool, NULL);
            free(f->pass_pools[p].buffers);
        }
        vkDestroyFence(vk.device, f->fence, NULL);
        vkDestroySemaphore(vk.device, f->acquired, NULL);
        for (uint32_t p = 0; p < f->pool_count; p++) vkDestroyDescriptorPool(vk.device, f->pools[p], NULL);
        free(f->pools);
        free(f->buffers);
        free(f->garbage);
        *f = (vk_frame){0};
    }
    if (vk.flushed) vkDestroyFence(vk.device, vk.flushed, NULL);
    vk.flushed = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < 8; i++) {
        if (rendered[i]) vkDestroySemaphore(vk.device, rendered[i], NULL);
        rendered[i] = VK_NULL_HANDLE;
    }
    if (readback.buffer) vkDestroyBuffer(vk.device, readback.buffer, NULL);
    give_back(&readback.memory);
    readback.buffer = VK_NULL_HANDLE, readback.memory = (vk_alloc){0}, readback.size = 0;
    forget_reads();
    if (vk.pass_queries) vkDestroyQueryPool(vk.device, vk.pass_queries, NULL);
    if (vk.queries) vkDestroyQueryPool(vk.device, vk.queries, NULL);
    vk.pass_queries = VK_NULL_HANDLE;
    vk.pass_first = vk.pass_count = vk.pass_sent = 0;
    vk.pass_active = false;
    vk.queries = VK_NULL_HANDLE, vk.query_first = vk.query_count = vk.query_sent = 0, vk.query_active = false;
    vk.made_width = vk.made_height = 0;
    forget_swapchain();
    for (uint32_t i = 0; i < SAMPLER_COUNT; i++) vkDestroySampler(vk.device, vk.samplers[i], NULL);
    vkDestroyPipelineCache(vk.device, vk.pipeline_cache, NULL);
    vk.pipeline_cache = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < vk.layout_count; i++) {
        vkDestroyPipelineLayout(vk.device, vk.layouts[i].layout, NULL);
        vkDestroyDescriptorSetLayout(vk.device, vk.layouts[i].set, NULL);
    }
    free(vk.layouts);
    vk.layouts = NULL, vk.layout_count = 0;
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
    vk.acquired = vk.in_pass = vk.in_compute = vk.drawn = false;
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
    const char *why = NULL;
    uint32_t newest = 0; // Of the devices' Vulkan, for saying so when none is 1.3
    for (uint32_t d = 0; d < count; d++) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(devices[d], &properties);
        if (properties.apiVersion > newest) newest = properties.apiVersion;
        if (properties.apiVersion < VK_API_VERSION_1_3) continue;
        VkExtensionProperties extensions[512];
        uint32_t extension_count = 512;
        vkEnumerateDeviceExtensionProperties(devices[d], NULL, &extension_count, extensions);
        if (!listed(extensions, extension_count, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            why = "no device has a swapchain";
            continue;
        }
        VkPhysicalDeviceVulkan13Features v13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 features = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &v13};
        vkGetPhysicalDeviceFeatures2(devices[d], &features);
        if (!v13.dynamicRendering || !features.features.independentBlend) {
            why = "no device has dynamic rendering and independent blending";
            continue;
        }
        // Compute as WebGPU's defaults have it, which every render backend has: a pipeline's storage
        // buffers in each stage (4, the least every device has: a shader's GPU lists are ranges of two pools), and
        // workgroups of 256 invocations, 64 deep
        const VkPhysicalDeviceLimits *limits = &properties.limits;
        if (limits->maxPerStageDescriptorStorageBuffers < TIDE_GPU_MAX_STORAGE || limits->maxComputeWorkGroupInvocations < 256u
            || limits->maxComputeWorkGroupSize[0] < 256u || limits->maxComputeWorkGroupSize[1] < 256u
            || limits->maxComputeWorkGroupSize[2] < 64u || limits->maxComputeSharedMemorySize < 16384u) {
            why = "no device computes with the storage buffers and workgroups compute shaders are given";
            continue;
        }
        VkQueueFamilyProperties families[32];
        uint32_t family_count = 32;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &family_count, families);
        int family = -1;
        for (uint32_t f = 0; f < family_count && family < 0; f++) {
            VkBool32 shows = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(devices[d], f, vk.surface, &shows);
            const VkQueueFlags both = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
            if ((families[f].queueFlags & both) == both && shows) family = (int)f;
        }
        if (family < 0) {
            why = "no device draws and computes in the window";
            continue;
        }
        const int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 4
                        : properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 3
                        : properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU ? 2 : 1;
        if (score > best_score) best = (int)d, best_score = score, best_family = (uint32_t)family;
    }
    if (best < 0) {
        if (say && why) fprintf(stderr, "tide: Vulkan: %s\n", why);
        if (say && !why) {
            fprintf(stderr, "tide: Vulkan: no device has Vulkan 1.3, the newest being %u.%u\n", VK_API_VERSION_MAJOR(newest),
                    VK_API_VERSION_MINOR(newest));
        }
        return false;
    }
    vk.physical = devices[best];
    vk.family = best_family;
    vkGetPhysicalDeviceProperties(vk.physical, &vk.properties);
    vkGetPhysicalDeviceMemoryProperties(vk.physical, &vk.memory);
    { // For a player's problem: which GPU, and its driver (tide_platform_log_note)
        char note[512];
        snprintf(note, sizeof note, "GPU: %s, Vulkan %u.%u.%u, driver %u (0x%08x), vendor 0x%04x, device 0x%04x",
                 vk.properties.deviceName, VK_API_VERSION_MAJOR(vk.properties.apiVersion),
                 VK_API_VERSION_MINOR(vk.properties.apiVersion), VK_API_VERSION_PATCH(vk.properties.apiVersion),
                 vk.properties.driverVersion, vk.properties.driverVersion, vk.properties.vendorID,
                 vk.properties.deviceID);
        tide_platform_log_note(note);
    }
    vk.window_format = pick_window_format();
    if (vk.window_format == VK_FORMAT_UNDEFINED) {
        if (say) fprintf(stderr, "tide: Vulkan: the window has no format of 4 bytes a pixel\n");
        return false;
    }

    VkExtensionProperties extensions[512];
    uint32_t extension_count = 512;
    vkEnumerateDeviceExtensionProperties(vk.physical, NULL, &extension_count, extensions);
    const char *enabled[3] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    uint32_t enabled_count = 1;
    // Push descriptors where there are some; TIDE_VULKAN_DESCRIPTOR_SETS has a test write sets where there are too
    const char *sets = getenv("TIDE_VULKAN_DESCRIPTOR_SETS");
    vk.push = listed(extensions, extension_count, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME) && !(sets && *sets && strcmp(sets, "0") != 0);
    if (vk.push) { // ...a pipeline's whole layout at once, where it has that few descriptors: 32 at the least
        VkPhysicalDevicePushDescriptorPropertiesKHR pushes = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR};
        VkPhysicalDeviceProperties2 properties = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &pushes};
        vkGetPhysicalDeviceProperties2(vk.physical, &properties);
        vk.max_push = pushes.maxPushDescriptors;
        vk.push = vk.max_push > 0;
    }
    { // Whether a multisampled depth resolves to its farthest sample: occlusion culling's measure (depth_now)
        VkPhysicalDeviceDepthStencilResolveProperties resolves = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES};
        VkPhysicalDeviceProperties2 properties = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &resolves};
        vkGetPhysicalDeviceProperties2(vk.physical, &properties);
        vk.depth_max = (resolves.supportedDepthResolveModes & VK_RESOLVE_MODE_MAX_BIT) != 0;
    }
    if (vk.push) enabled[enabled_count++] = VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME;
    if (listed(extensions, extension_count, "VK_KHR_portability_subset")) enabled[enabled_count++] = "VK_KHR_portability_subset";
    VkPhysicalDeviceVulkan12Features has12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan13Features has13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .pNext = &has12};
    VkPhysicalDeviceFeatures2 has = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &has13};
    vkGetPhysicalDeviceFeatures2(vk.physical, &has);
    // Many indirect draws in one call, as many as the GPU counted (drawIndirectCount, core in 1.2 but optional), or
    // as many as asked for (multiDrawIndirect); TIDE_VULKAN_INDIRECT_COUNT=0 turns both off, for tests of the
    // draw-a-record fallback (gpu.h, draw_indexed_indirect_count)
    const char *counted = getenv("TIDE_VULKAN_INDIRECT_COUNT");
    const bool many = !(counted && strcmp(counted, "0") == 0);
    VkPhysicalDeviceVulkan12Features want12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
                                               .drawIndirectCount = many ? has12.drawIndirectCount : VK_FALSE};
    // What's read past a buffer's or an image's end is nothing, as on WebGPU
    VkPhysicalDeviceVulkan13Features want13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
                                               .pNext = &want12, .dynamicRendering = VK_TRUE,
                                               .robustImageAccess = has13.robustImageAccess};
    VkPhysicalDeviceFeatures2 want = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &want13};
    want.features.multiDrawIndirect = many ? has.features.multiDrawIndirect : VK_FALSE;
    vk.multi_draw = want.features.multiDrawIndirect;
    vk.indirect = (want12.drawIndirectCount ? (uint32_t)TIDE_GPU_INDIRECT_COUNT : 0u)
                | (has.features.drawIndirectFirstInstance ? (uint32_t)TIDE_GPU_INDIRECT_FIRST_INSTANCE : 0u);
    want.features.independentBlend = VK_TRUE;
    want.features.robustBufferAccess = has.features.robustBufferAccess;
    want.features.textureCompressionBC = has.features.textureCompressionBC;
    vk.compression = has.features.textureCompressionBC;
    want.features.alphaToOne = has.features.alphaToOne;
    // A draw's first instance as the GPU's numbers have it (gpu.h has them 0), where a device takes another
    want.features.drawIndirectFirstInstance = has.features.drawIndirectFirstInstance;
    vk.alpha_to_one = has.features.alphaToOne;
    const float priority = 1.0f;
    const VkDeviceQueueCreateInfo queue = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, vk.family, 1, &priority};
    const VkDeviceCreateInfo info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &want, 0, 1, &queue, 0, NULL, enabled_count, enabled, NULL};
    const VkResult made = vkCreateDevice(vk.physical, &info, NULL, &vk.device);
    if (made != VK_SUCCESS) {
        if (say) say_result("vkCreateDevice", made);
        vk.device = VK_NULL_HANDLE;
        return false;
    }
    if (!load_device_functions() || (vk.push && !vkCmdPushDescriptorSetKHR)) {
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
    const VkFenceCreateInfo flushed = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, NULL, 0};
    check(vkCreateFence(vk.device, &flushed, NULL, &vk.flushed), "vkCreateFence");
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
        finish_all_making();
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
    free(vk.reads);
    memset(&vk, 0, sizeof vk);
}

bool tide_gpu_vk_timestamps(void)
{
    return vk.device && vk.timestamp_ns > 0.0;
}

bool tide_gpu_vk_alpha_to_one(void)
{
    return vk.device && vk.alpha_to_one;
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
    .pipeline_cache_ready = vk_pipeline_cache_ready,
    .device_memory = vk_device_memory,
    .pass_time_begin = vk_pass_time_begin,
    .pass_time_end = vk_pass_time_end,
    .pass_time_read = vk_pass_time_read,
    .pass_time_reset = vk_pass_time_reset,
    .time_begin = vk_time_begin,
    .time_end = vk_time_end,
    .time_read = vk_time_read,
    .time_reset = vk_time_reset,
    .buffer = vk_buffer_make,
    .storage_most = vk_storage_most,
    .buffer_write = vk_buffer_write,
    .buffer_free = vk_buffer_free,
    .texture = vk_texture_make,
    .texture_blocks = vk_texture_blocks,
    .texture_write = vk_texture_write,
    .texture_write_part = vk_texture_write_part,
    .texture_free = vk_texture_free,
    .cube = vk_cube,
    .cube_face = vk_cube_face,
    .array = vk_array,
    .array_depth = vk_array_depth,
    .array_blocks = vk_array_blocks,
    .array_of = vk_array_of,
    .array_write = vk_array_write,
    .array_layer = vk_array_layer,
    .mipmaps = vk_mipmaps,
    .pipeline = vk_pipeline_make,
    .pipeline_free = vk_pipeline_free,
    .background = vk_background,
    .pipeline_ready = vk_pipeline_ready,
    .target = vk_target_make,
    .target_limit = vk_target_limit,
    .target_samples = vk_target_samples,
    .target_texture = vk_target_texture,
    .target_depth = vk_target_depth,
    .resolve_depth = vk_resolve_depth,
    .depth_now = vk_depth_now,
    .target_free = vk_target_free,
    .target_read = vk_target_read,
    .target_read_part = vk_target_read_part,
    .begin = vk_begin,
    .end = vk_end,
    .scissor = vk_scissor,
    .use = vk_use,
    .stencil = vk_stencil,
    .uniforms = vk_uniforms,
    .image = vk_image,
    .sampler = vk_sampler,
    .vertices = vk_vertices,
    .indices = vk_indices,
    .draw = vk_draw,
    .draw_indexed = vk_draw_indexed,
    .storage = vk_storage,
    .compute_begin = vk_compute_begin,
    .compute_end = vk_compute_end,
    .dispatch = vk_dispatch,
    .dispatch_indirect = vk_dispatch_indirect,
    .draw_indirect = vk_draw_indirect,
    .draw_indexed_indirect = vk_draw_indexed_indirect,
    .draw_indexed_indirect_count = vk_draw_indexed_indirect_count,
    .indirect = vk_indirect,
    .buffer_copy = vk_buffer_copy,
    .buffer_read = vk_buffer_read,
    .buffer_read_done = vk_buffer_read_done,
    .buffer_read_wait = vk_buffer_read_wait,
};

