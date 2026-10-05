#pragma once

// When a UDP socket's datagrams arrived: some time before the frame that reads
// them, which is what a session's ping leaves out (tide_transport.arrived).
// The system knows, each its own way (arrivals.c). Sessions' UDP (udp.c) and
// WebRTC's (rtc/ice.c) both read their sockets through this.

#include <stddef.h>
#include <stdint.h>

typedef struct tide_arrivals tide_arrivals;

// Reads `socket` from here on: the system's handle of an IPv4 UDP socket,
// bound and not blocking. `room` is how many bytes of datagrams it may hold
// that nobody read yet, where it holds them itself. NULL if it can't start.
tide_arrivals *tide_arrivals_start(uintptr_t socket, uint32_t room);
// The next datagram: its size, or 0 when there's none. Where it's from, in the
// host's byte order, and when it arrived: seconds by tide_time_now_ns. Longer
// ones than `capacity` are dropped.
size_t tide_arrivals_next(tide_arrivals *a, uint32_t *ip, uint16_t *port, void *out, size_t capacity, double *arrived);
// Before the socket closes.
void tide_arrivals_stop(tide_arrivals *a);
