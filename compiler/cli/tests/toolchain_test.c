// Exercise Apple's discovery branch on every native test platform without
// changing the machine's environment or requiring installed Apple tools.
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tide_test.h"

static const char *provided_sdk;
static const char *selected_sdk;
static const char *capture_output;
static int capture_code, environment_code, capture_calls, environment_calls;
static bool capture_arguments;
static char saved_sdk[4096];

static const char *fixture_env(const char *name);
static int fixture_capture(const char *const *args, char *out, size_t size);
static bool fixture_is_dir(const char *path);
static int fixture_setenv(const char *name, const char *value, int overwrite);

#ifndef __APPLE__
#define __APPLE__ 1
#define FIXTURE_APPLE
#endif
#define sys_env fixture_env
#define sys_capture fixture_capture
#define sys_is_dir fixture_is_dir
#define setenv fixture_setenv
#include "../toolchain.c"
#undef sys_env
#undef sys_capture
#undef sys_is_dir
#undef setenv
#ifdef FIXTURE_APPLE
#undef __APPLE__
#undef FIXTURE_APPLE
#endif

static const char *fixture_env(const char *name)
{
    return strcmp(name, "SDKROOT") == 0 ? provided_sdk : NULL;
}

static int fixture_capture(const char *const *args, char *out, const size_t size)
{
    capture_calls++;
    capture_arguments = strcmp(args[0], "/usr/bin/xcrun") == 0 && strcmp(args[1], "--sdk") == 0
        && strcmp(args[2], "macosx") == 0 && strcmp(args[3], "--show-sdk-path") == 0 && !args[4];
    snprintf(out, size, "%s", capture_output);
    return capture_code;
}

static bool fixture_is_dir(const char *path)
{
    return strcmp(path, selected_sdk) == 0;
}

static int fixture_setenv(const char *name, const char *value, const int overwrite)
{
    environment_calls++;
    if (strcmp(name, "SDKROOT") != 0 || overwrite != 1) abort();
    if (!environment_code) snprintf(saved_sdk, sizeof saved_sdk, "%s", value);
    return environment_code;
}

static void reset(void)
{
    provided_sdk = NULL;
    selected_sdk = "/Applications/Selected Xcode.app/Contents/Developer/SDKs/MacOSX.sdk";
    capture_output = "/Applications/Selected Xcode.app/Contents/Developer/SDKs/MacOSX.sdk\n";
    capture_code = environment_code = capture_calls = environment_calls = 0;
    capture_arguments = false;
    saved_sdk[0] = '\0';
}

TIDE_TEST(macos_sdk_follows_selected_developer_tools)
{
    reset();
    TIDE_CHECK(find_macos_sdk());
    TIDE_CHECK(capture_calls == 1 && capture_arguments);
    TIDE_CHECK(environment_calls == 1 && strcmp(saved_sdk, selected_sdk) == 0);
}

TIDE_TEST(macos_sdk_preserves_explicit_sdkroot)
{
    reset();
    provided_sdk = "/custom/SDK";
    TIDE_CHECK(find_macos_sdk());
    TIDE_CHECK(capture_calls == 0 && environment_calls == 0);
}

TIDE_TEST(macos_sdk_rejects_failed_or_invalid_discovery)
{
    reset(); capture_code = 1;
    TIDE_CHECK(!find_macos_sdk() && environment_calls == 0);
    reset(); capture_output = "";
    TIDE_CHECK(!find_macos_sdk() && environment_calls == 0);
    reset(); capture_output = "relative/SDK\n"; selected_sdk = "relative/SDK";
    TIDE_CHECK(!find_macos_sdk() && environment_calls == 0);
    reset(); selected_sdk = "/missing/SDK";
    TIDE_CHECK(!find_macos_sdk() && environment_calls == 0);
    reset(); environment_code = -1;
    TIDE_CHECK(!find_macos_sdk() && environment_calls == 1 && !saved_sdk[0]);
}
