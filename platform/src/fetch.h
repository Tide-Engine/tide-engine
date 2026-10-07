#pragma once

// Web.Fetch on desktop and Android (fetch.c): the provider's part of it
// (tide_io's fetch_begin, fetch_poll and fetch_forget, see tide/io.h), which
// platform/src/io.c hands out. The web has the page's (tide_web.h).

#include <stdbool.h>
#include <stdint.h>

#include "tide/io.h"

uint32_t tide_fetch_begin(const char *url);
bool tide_fetch_poll(uint32_t ticket, tide_io_error *error, uint8_t **data, uint32_t *size);
void tide_fetch_forget(uint32_t ticket);
