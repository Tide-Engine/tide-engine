// grid.Decode: the engine's PNG decoder (tide/png.h) against the platform's
// writer (platform/src/png.c), and a PNG sent in a command on the loopback
// network, decoded in its handler, the same on the server and its client,
// with the world's hash pinned (the same natively and on the web).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "png.h" // platform/src: the writer
#include "tide/png.h"
#include "tide_test.h"

// The pinned world: the same bits on every platform
#ifndef DECODE_WORLD_HASH
#define DECODE_WORLD_HASH 0x08aab1af804e284eull
#endif

static tide_world world;
static tide_local local;
static tide_draw_list draw;
static tide_gui gui;
static tide_devices devices;

enum { WIDTH = 12, HEIGHT = 8 };

// The picture that goes through the match
static void picture_pixel(const int x, const int y, uint8_t rgb[3])
{
    rgb[0] = (uint8_t)(x * 20);
    rgb[1] = (uint8_t)(y * 30);
    rgb[2] = (uint8_t)((x * y * 7) % 256);
}

static bool channel_is(const float value, const uint8_t byte)
{
    return value == (float)byte / 255.0f;
}

static void frame(void)
{
    tide_draw_reset(&draw);
    tide_gui_begin(&gui, &devices, tide_f2(640, 480), NULL);
    tide_frame(&world, NULL, 1, &world, NULL, 1.0f, &local, &draw, &gui);
    tide_gui_end(&gui, &draw);
}

TIDE_TEST(decode_reads_back_what_the_writer_wrote)
{
    // Pictures of a few sizes: flat, a gradient, noise (which the writer stores
    // as it is), and a single pixel
    static const int sizes[][2] = {{1, 1}, {7, 3}, {64, 48}, {300, 2}, {2, 300}};
    uint32_t noise = 12345;
    for (size_t s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
        const int w = sizes[s][0], h = sizes[s][1];
        uint8_t *rgba = malloc((size_t)w * h * 4);
        for (int i = 0; i < w * h; i++) {
            noise = noise * 1664525u + 1013904223u;
            rgba[4 * i] = s == 0 ? 200 : s == 1 ? 30 : (uint8_t)(noise >> 24);
            rgba[4 * i + 1] = s == 0 ? 100 : (uint8_t)(i * 3);
            rgba[4 * i + 2] = (uint8_t)(i % 7 * 36);
            rgba[4 * i + 3] = 255;
        }
        size_t size = 0;
        uint8_t *png = tide_png_encode(rgba, w, h, &size);
        TIDE_REQUIRE(png && size > 0);
        tide_png_image image;
        TIDE_REQUIRE(tide_png_decode(png, size, &image, NULL));
        TIDE_CHECK(image.width == (uint32_t)w && image.height == (uint32_t)h);
        bool same = true;
        for (int i = 0; same && i < w * h; i++) {
            const tide_color c = image.pixels[i];
            same = channel_is(c.r, rgba[4 * i]) && channel_is(c.g, rgba[4 * i + 1]) && channel_is(c.b, rgba[4 * i + 2]) && c.a == 1.0f;
        }
        TIDE_CHECK(same);
        // Cut anywhere, it's no PNG; and neither is a byte changed in the pixels' data
        for (size_t cut = 0; cut < size; cut += size / 7 + 1) {
            tide_png_image partial;
            TIDE_CHECK(!tide_png_decode(png, cut, &partial, NULL));
            TIDE_CHECK(partial.pixels == NULL);
        }
        png[size - 20] ^= 0x55;
        TIDE_CHECK(!tide_png_decode(png, size, &image, NULL));
        free(image.pixels);
        free(png);
        free(rgba);
    }
    TIDE_CHECK(!tide_png_decode(NULL, 0, &(tide_png_image){0}, NULL));
    TIDE_CHECK(!tide_png_decode((const uint8_t *)"not a png at all, not even close", 32, &(tide_png_image){0}, NULL));
}

// Puts `size` bytes where the game's view sends them from, and has it send them
static void send_bytes(tide_client *client, const uint8_t *bytes, const size_t size)
{
    tide_text_use(NULL, &local.heap);
    tide_list_release(&local.Ui.png, TIDE_IN_LOCAL);
    for (size_t i = 0; i < size; i++) *(uint8_t *)tide_list_add(&local.Ui.png, 1, TIDE_IN_LOCAL) = bytes[i];
    local.Ui.send = true;
    frame();
    uint32_t packed = 0;
    const uint8_t *data = tide_local_command(&local, 0, &packed);
    TIDE_REQUIRE(data && packed > 0);
    uint32_t channel = 0;
    TIDE_REQUIRE(tide_game_api.check_command(data, packed, &channel));
    TIDE_REQUIRE(tide_client_send_command(client, data, packed));
    tide_local_command_sent(&local, 0);
}

TIDE_TEST(decode_in_a_command_handler_is_the_same_world_everywhere)
{
    double now = 0;
    tide_loopback *net = tide_loopback_create(7741);
    tide_loopback_set_conditions(net, (tide_net_conditions){.latency = 0.04, .jitter = 0.02, .loss = 0.1});
    tide_server *server = tide_server_create(&(tide_server_desc){.game = &tide_game_api, .tick_rate = 60,
                                               .transports = {tide_loopback_endpoint(net, 1)}}, now);
    tide_client *client = tide_client_create(&(tide_client_desc){.game = &tide_game_api,
                                               .transport = tide_loopback_endpoint(net, 2),
                                               .server = tide_loopback_address(1), .lead = 2}, now);
    for (int i = 0; i < 120; i++) {
        now += 1.0 / 60;
        tide_loopback_set_time(net, now);
        tide_client_update(client, now);
        tide_server_update(server, now);
    }
    TIDE_REQUIRE(tide_client_status_of(client).state == TIDE_SESSION_CONNECTED);
    tide_local_init(&local);
    tide_local_set_session(&local, TIDE_SESSION_CONNECTED, tide_client_status_of(client).player, 0, false, false, "");
    // The picture, through the platform's writer
    uint8_t rgba[WIDTH * HEIGHT * 4];
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            picture_pixel(x, y, rgba + (y * WIDTH + x) * 4);
            rgba[(y * WIDTH + x) * 4 + 3] = 255;
        }
    }
    size_t size = 0;
    uint8_t *png = tide_png_encode(rgba, WIDTH, HEIGHT, &size);
    TIDE_REQUIRE(png && size > 0);
    // Bytes that aren't a PNG, then the picture, then the picture cut short: the
    // grid ends as the picture, with one decode good and two refused
    const uint8_t noise[25] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25};
    send_bytes(client, noise, sizeof noise);
    send_bytes(client, png, size);
    send_bytes(client, png, size * 3 / 4);
    free(png);
    for (int i = 0; i < 240; i++) {
        now += 1.0 / 60;
        tide_loopback_set_time(net, now);
        tide_client_update(client, now);
        tide_server_update(server, now);
    }
    const tide_world *authoritative = tide_server_world(server);
    const tide_world *verified = tide_client_verified_world(client);
    TIDE_REQUIRE(authoritative && verified);
    TIDE_CHECK(authoritative->Art.decoded == 1 && authoritative->Art.bad == 2 && authoritative->Art.other == 0);
    TIDE_CHECK(verified->Art.decoded == 1 && verified->Art.bad == 2);
    for (int pass = 0; pass < 2; pass++) {
        const tide_world *w = pass ? verified : authoritative;
        int32_t grid_size[3];
        tide_grid_size(w->Art.pixels, grid_size);
        TIDE_CHECK(grid_size[0] == WIDTH && grid_size[1] == HEIGHT);
        bool same = true;
        for (int y = 0; same && y < HEIGHT; y++) {
            for (int x = 0; same && x < WIDTH; x++) {
                const tide_color *c = tide_grid_read(&w->heap, w->Art.pixels, x, y, 0, &tide_shape_Grid2_Color);
                uint8_t rgb[3];
                picture_pixel(x, y, rgb);
                // Every pixel is opaque, so no cell is zero: each is there
                same = c && channel_is(c->r, rgb[0]) && channel_is(c->g, rgb[1]) && channel_is(c->b, rgb[2]) && c->a == 1.0f;
            }
        }
        TIDE_CHECK(same);
        // Outside the picture there's nothing
        TIDE_CHECK(tide_grid_read(&w->heap, w->Art.pixels, WIDTH, 0, 0, &tide_shape_Grid2_Color) == NULL);
    }
    // The client never had to be sent the world again: its ticks hashed as the server's did
    TIDE_CHECK(tide_client_status_of(client).resyncs == 0);
    // The same bits on every platform (the server's world is ticks ahead by now, so the confirmed one is pinned)
    const uint64_t hash = tide_world_hash(verified);
    printf("decode: world hash %llx\n", (unsigned long long)hash);
    TIDE_CHECK(hash == DECODE_WORLD_HASH);
    tide_local_free(&local);
    tide_client_destroy(client);
    tide_server_destroy(server);
    tide_loopback_destroy(net);
}
