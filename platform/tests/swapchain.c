// The window's images on Vulkan as the window changes, in a hidden window:
// its swapchain is made again at the window's new size when it's resized,
// bigger and smaller, and with everything else when the device is lost while
// frames are shown. The validation layer checks every call on the way
// (tide_gpu_test), and each frame reads a pixel back to show it drew.

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "gpu_vk.h"
#include "tide/platform.h"
#include "window.h"

// GLFW's, without its header, and the desktop window's (desktop/glfw.c).
typedef struct GLFWwindow GLFWwindow;
void glfwSetWindowSize(GLFWwindow *window, int width, int height);
GLFWwindow *tide_glfw_window(void);

enum { WAIT_FRAMES = 600 }; // Frames a resize may take to reach the window before the test fails

static int failures;

static void expect(const bool ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) failures++;
}

// A frame's drawing: the window cleared, and a list read back, of a color of the frame's own.
static bool draw(const int frame)
{
    const tide_color color = {(float)(frame % 7) / 6.0f, 0.5f, (float)(frame % 3) / 2.0f, 1.0f};
    tide_draw_list list = {0};
    tide_draw_clear(&list, color);
    tide_platform_draw(&list);
    const tide_float2 point = tide_f2(1.0f, 1.0f);
    uint32_t got = 0;
    tide_platform_read_pixels(&list, &point, 1, &got);
    tide_draw_free(&list);
    const uint32_t r = (uint32_t)(color.r * 255.0f + 0.5f), g = (uint32_t)(color.g * 255.0f + 0.5f),
                   b = (uint32_t)(color.b * 255.0f + 0.5f);
    const uint32_t wanted = r << 24 | g << 16 | b << 8 | 255u;
    uint32_t far = 0;
    for (int shift = 8; shift < 32; shift += 8) {
        const int d = (int)((got >> shift) & 255u) - (int)((wanted >> shift) & 255u);
        if ((uint32_t)(d < 0 ? -d : d) > far) far = (uint32_t)(d < 0 ? -d : d);
    }
    return far <= 2u;
}

// Whether the swapchain's images are the window's size, made `made` times in all.
static void check_swapchain(const uint32_t made, const char *what)
{
    int width, height, image_width, image_height;
    uint32_t times;
    tide_window_pixel_size(&width, &height);
    tide_gpu_vk_swapchain(&image_width, &image_height, &times);
    char said[256];
    snprintf(said, sizeof said, "%s: the window's images are its %d by %d pixels (%d by %d), made %u times (%u)", what, width,
             height, image_width, image_height, made, times);
    expect(image_width == width && image_height == height && times == made, said);
}

typedef enum { START, GROWING, SHRINKING, LOSING, DONE } stage;

static int frame(void *user, const float seconds)
{
    (void)user, (void)seconds;
    static int frames, waited, from_width, from_height;
    static stage now = START;
    static uint32_t epoch;
    static bool drew = true;
    drew = draw(frames) && drew;
    frames++;
    int width, height;
    tide_window_pixel_size(&width, &height);
    switch (now) {
    case START:
        if (frames < 3) return TIDE_KEEP_RUNNING;
        if (strcmp(tide_platform_renderer(), "Vulkan") != 0) {
            printf("FAIL: the window draws with %s, where this test is Vulkan's\n", tide_platform_renderer());
            return 1;
        }
        check_swapchain(1, "as the window opened");
        from_width = width, from_height = height;
        glfwSetWindowSize(tide_glfw_window(), 160, 120);
        now = GROWING;
        return TIDE_KEEP_RUNNING;
    case GROWING:
    case SHRINKING:
        if (width == from_width && height == from_height) {
            if (++waited < WAIT_FRAMES) return TIDE_KEEP_RUNNING;
            expect(false, "the window took its new size");
            return 1;
        }
        // This frame drew at the new size, and the next one shows it again: one swapchain more
        if (waited >= 0) {
            waited = -1;
            return TIDE_KEEP_RUNNING;
        }
        waited = 0;
        check_swapchain(now == GROWING ? 2u : 3u, now == GROWING ? "a window made bigger" : "a window made smaller");
        from_width = width, from_height = height;
        if (now == GROWING) {
            glfwSetWindowSize(tide_glfw_window(), 40, 30);
            now = SHRINKING;
        } else {
            epoch = tide_gpu_vulkan.epoch();
            tide_gpu_vk_lose(); // As this frame is shown
            now = LOSING;
        }
        return TIDE_KEEP_RUNNING;
    case LOSING:
        // The frame after the loss drew on another device, in a swapchain made on it
        expect(tide_gpu_vulkan.epoch() != epoch, "a lost device was made again as the next frame drew");
        check_swapchain(4, "after the device was lost");
        now = DONE;
        return TIDE_KEEP_RUNNING;
    case DONE:
        if (frames % 4) return TIDE_KEEP_RUNNING; // A few frames more on the new device
        expect(drew, "every frame drew, and read back what it drew");
        printf("%d frames, %d failed\n", frames, failures);
        return failures ? 1 : 0;
    }
    return 1;
}

int main(void)
{
    tide_platform_open(&(tide_window_desc){.title = "swapchain", .width = 64, .height = 64, .hidden = true});
    tide_platform_run(frame, NULL);
}
