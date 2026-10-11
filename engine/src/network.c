#include "tide/network.h"

#include <stdio.h>
#include <string.h>

#include "tide/session.h"

static tide_network registered;
static bool has_registered;

void tide_network_set(const tide_network *network)
{
    has_registered = network != NULL;
    registered = network ? *network : (tide_network){0};
}

const tide_network *tide_network_get(void)
{
    return has_registered ? &registered : NULL;
}

bool tide_network_match_host(tide_network_match *m, const tide_network *network, tide_transport *out)
{
    *m = (tide_network_match){0};
    tide_transport made = {0};
    if (!network->host || !network->host(network->self, &made)) return false;
    // A transport that can't carry a datagram is none
    if (!made.send || !made.receive) {
        if (made.close) made.close(made.self);
        return false;
    }
    *out = made;
    m->network = *network;
    m->on = true;
    return true;
}

bool tide_network_match_join(tide_network_match *m, const tide_network *network, const char *address,
                             tide_transport *out, tide_address *server)
{
    *m = (tide_network_match){0};
    tide_transport made = {0};
    tide_address at = {0};
    if (!network->join || !network->join(network->self, address, &made, &at)) return false;
    if (!made.send || !made.receive) {
        if (made.close) made.close(made.self);
        return false;
    }
    at.kind = TIDE_ADDRESS_NETWORK;
    *out = made;
    *server = at;
    m->network = *network;
    m->on = true;
    return true;
}

void tide_network_match_leave(tide_network_match *m)
{
    *m = (tide_network_match){0};
}

void tide_network_match_update(const tide_network_match *m, const tide_network *now)
{
    if (now && now->update) now->update(now->self);
    // A match stays on the network it was opened on, which still has callbacks to run
    const tide_network *own = &m->network;
    if (m->on && own->update && !(now && now->update == own->update && now->self == own->self)) own->update(own->self);
}

// What `place` wrote, if it's a place: letters and digits, with their NUL.
static bool place_of(const tide_network *network, char out[TIDE_NETWORK_PLACE_BYTES])
{
    char said[TIDE_NETWORK_PLACE_BYTES + 1u] = {0};
    out[0] = '\0';
    if (!network->place || !network->migrate || !network->migrated) return false;
    network->place(network->self, said, TIDE_NETWORK_PLACE_BYTES);
    said[TIDE_NETWORK_PLACE_BYTES] = '\0';
    const size_t n = strlen(said);
    if (!n || n >= TIDE_NETWORK_PLACE_BYTES) return false;
    for (size_t i = 0; i < n; i++) {
        const char c = said[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return false;
    }
    memcpy(out, said, n + 1u);
    return true;
}

// A session says where its players meet again as a room's code and its key.
// A game's network has one text for both, which goes as the key, under a code
// that's no room's: whoever reads it is on the same network, and takes the
// key alone.
#define PLACE_CODE "222222"

void tide_network_match_after(tide_network_match *m, tide_session *session, const double now)
{
    if (!m->on) return;
    const tide_network *network = &m->network;
    char code[TIDE_ROOM_CODE_LENGTH + 1] = "";
    char key[TIDE_ROOM_KEY_LENGTH + 1] = "";
    if (!tide_session_migrating(session, code, key)) {
        m->since = 0.0;
        const tide_session_status status = tide_session_status_of(session);
        if (!status.server && status.client.state == TIDE_SESSION_OFFLINE) {
            m->on = false; // The match is over on this machine: its network has nothing more to say of it
            return;
        }
        if (network->failed && network->failed(network->self)) {
            m->on = false;
            tide_session_fail(session, TIDE_DISCONNECT_FAILED);
            return;
        }
        if (status.server) {
            char place[TIDE_NETWORK_PLACE_BYTES];
            const bool has = place_of(network, place);
            tide_session_set_room(session, has ? PLACE_CODE : "", place);
        }
        return;
    }
    if (!network->migrate || !network->migrated) {
        m->on = false; // Its host's network had a place to meet, and this build's has none
        tide_session_fail(session, TIDE_DISCONNECT_SERVER_LEFT);
        return;
    }
    if (m->since == 0.0) {
        m->since = now > 0.0 ? now : 1e-9;
        network->migrate(network->self, key);
        return;
    }
    tide_transport made = {0};
    tide_address server = {0};
    const int moved = network->migrated(network->self, &made, &server);
    const bool carries = made.send && made.receive;
    if ((moved == TIDE_NETWORK_HOSTS || moved == TIDE_NETWORK_JOINS) && carries) {
        m->since = 0.0;
        server.kind = TIDE_ADDRESS_NETWORK;
        if (moved == TIDE_NETWORK_HOSTS) tide_session_take_over(session, made, now);
        else tide_session_join(session, made, server, now);
        return;
    }
    if (made.close) made.close(made.self); // A transport with an answer that takes none
    if (moved == TIDE_NETWORK_ENDED) {
        m->on = false;
        tide_session_fail(session, TIDE_DISCONNECT_ENDED); // Its host ended it: the goodbye was lost
    } else if (moved != TIDE_NETWORK_MOVING || now - m->since > 30.0) {
        m->on = false;
        tide_session_fail(session, TIDE_DISCONNECT_TIMED_OUT);
    }
}

void tide_network_match_address(const tide_network_match *m, char out[TIDE_NETWORK_ADDRESS_BYTES])
{
    char said[TIDE_NETWORK_ADDRESS_BYTES + 1u] = {0};
    out[0] = '\0';
    if (!m->on || !m->network.address) return;
    m->network.address(m->network.self, said, TIDE_NETWORK_ADDRESS_BYTES);
    said[TIDE_NETWORK_ADDRESS_BYTES - 1u] = '\0';
    size_t n = 0;
    while (said[n] >= 0x20 && said[n] <= 0x7E) n++;
    memcpy(out, said, n);
    out[n] = '\0';
}
