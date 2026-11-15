// The window's images on Vulkan as the window changes, in a hidden window:
// its swapchain is made again at the window's new size when it's resized,
// bigger and smaller, and with everything else when the device is lost while
// frames are shown. The validation layer checks every call on the way
// (tide_gpu_test), and each frame reads a pixel back to show it drew.
//
// On Android the window is the activity's, the screen's size, which the app
// can't set: it asks cmake/run_android.mjs (--steps) to turn the screen to
// landscape and back, which the window follows, and once the device was lost,
// to send the app home, which takes its surface away (frames draw nothing),
// and to bring it to the front, which gives another: a swapchain made on the
// same device, everything made on it kept.

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "gpu_vk.h"
#include "tide/platform.h"
#include "window.h"

// What the device's player would do, asked of the test's runner (Android's)
static void ask(const char *what)
{
    printf("step: %s\n", what);
    fflush(stdout);
}

#ifdef __ANDROID__
// Frames a turn or a trip to the background may take to reach the window before the test fails: the emulator's
// software GPU draws them slower than a desktop's
enum { WAIT_FRAMES = 3600 };
#else
// GLFW's, without its header, and the desktop window's (desktop/glfw.c).
typedef struct GLFWwindow GLFWwindow;
void glfwSetWindowSize(GLFWwindow *window, int width, int height);
GLFWwindow *tide_glfw_window(void);

enum { WAIT_FRAMES = 600 }; // Frames a resize may take to reach the window before the test fails
#endif

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

// Whether the swapchain's images are the window's size, made `more` times since the last check, or once more than
// that where `twice` allows it: a turned screen can reach an Android app as two sizes, a swapchain made at each.
static void check_swapchain(const uint32_t more, const bool twice, const char *what)
{
    static uint32_t before;
    int width, height, image_width, image_height;
    uint32_t times;
    tide_window_pixel_size(&width, &height);
    tide_gpu_vk_swapchain(&image_width, &image_height, &times);
    char said[256];
    const uint32_t made = times - before;
    snprintf(said, sizeof said, "%s: the window's images are its %d by %d pixels (%d by %d), made %u times more%s (%u)", what,
             width, height, image_width, image_height, more, twice ? " or one over" : "", made);
    expect(image_width == width && image_height == height && (made == more || (twice && made == more + 1u)), said);
    before = times;
}

typedef enum { START, GROWING, SHRINKING, LOSING, AWAY, BACK, DONE } stage;

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
        check_swapchain(1, false, "as the window opened");
        from_width = width, from_height = height;
#ifdef __ANDROID__
        ask("turn");
#else
        glfwSetWindowSize(tide_glfw_window(), 160, 120);
#endif
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
#ifdef __ANDROID__
        check_swapchain(1, true, now == GROWING ? "the screen turned" : "the screen turned back");
        expect((width > height) == (now == GROWING), "the window took the screen's other shape");
#else
        check_swapchain(1, false, now == GROWING ? "a window made bigger" : "a window made smaller");
#endif
        from_width = width, from_height = height;
        if (now == GROWING) {
#ifdef __ANDROID__
            ask("upright");
#else
            glfwSetWindowSize(tide_glfw_window(), 40, 30);
#endif
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
        check_swapchain(1, false, "after the device was lost");
#ifdef __ANDROID__
        epoch = tide_gpu_vulkan.epoch();
        ask("home");
        now = AWAY;
#else
        now = DONE;
#endif
        return TIDE_KEEP_RUNNING;
    case AWAY: // In the background: no window, so frames draw nothing in one
        if (!tide_window_unseen()) {
            if (++waited < WAIT_FRAMES) return TIDE_KEEP_RUNNING;
            expect(false, "the app went to the background, and its window with it");
            return 1;
        }
        waited = 0;
        expect(true, "the app went to the background, and its window with it");
        ask("front");
        now = BACK;
        return TIDE_KEEP_RUNNING;
    case BACK: // ...and back, with a window again: a surface of its own, a swapchain made on it, on the same device
        if (tide_window_unseen()) {
            if (++waited < WAIT_FRAMES) return TIDE_KEEP_RUNNING;
            expect(false, "the app came back to the front, with a window");
            return 1;
        }
        // This frame drew into the new surface's images, and the next one shows them again
        if (waited >= 0) {
            waited = -1;
            return TIDE_KEEP_RUNNING;
        }
        expect(tide_gpu_vulkan.epoch() == epoch, "the device and everything made on it stayed while the app was away");
        check_swapchain(1, false, "the app back at the front");
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
