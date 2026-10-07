// Web only: a ping in a room, and a page told to play at a slow connection
// (`Tide.lag`, platform/web/tide.js).
//
// A ping is the network's round trip: the page notes when each datagram
// arrived, so the wait for the frame that reads it is no part of it. A lag is
// the ping the page's player is to have, there and back, and what the network
// takes already is part of it: on one that takes 30 ms, a lag of 200 is a ping
// of 200, not of 230.
//
// The page stands in for the relay and for WebRTC here: a room whose other end
// sends back what it's sent, the network's time later. The program measures as
// a session does: its ping from when it sent to when the answer arrived, by
// the room's clock, and what the answer took to be read, by its frames. It
// says both, as tide/host.h does (tide_platform_room_ping). The page says the
// least a lag can be (`Tide.minLag`): the ping while it added nothing.
//
// Each step sets what it's about, lets the pings settle, then checks what they
// are on the whole over the second after. The page's time is made up
// (cmake/WebTest.cmake), and a step's starts at its first answer, so none of
// it rests on how fast the machine is.

#include <stdio.h>

#include "tide/platform.h"
#include "tide_web.h"

#define NETWORK 30 // Milliseconds the other end takes to send back
#define SLOWER 80  // ...once the network got slower
#define NEAR 4.0   // What pings may be off by on the whole: the page's timers fire a little late
#define WINDOW 1.0 // Seconds of pings a step checks

static int failures;

static void check(const bool ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) failures++;
}

// The browser's side. A relay that introduces the page to a room's host, or
// gives it a room and a player who joins; and a data channel to them, open at
// once, which sends back what it's sent after `net.real` milliseconds.
// `net.sent` is when the program sent each packet, by its number, and
// `net.held` how long the page held the last one back before the channel had
// it.
static void stand_in_for_the_browser(void)
{
    tide_web_eval(
        "{ window.net = { real: 0, sent: [], held: 0 };"
        "  window.WebSocket = class {"
        "    static CONNECTING = 0; static OPEN = 1;"
        "    constructor() { this.readyState = 0;"
        "      setTimeout(() => { this.readyState = 1; this.say({ relay: 1, ice: [] }); }, 0); }"
        "    say(message) { if (this.readyState === 1 && this.onmessage) this.onmessage({ data: JSON.stringify(message) }); }"
        "    send(text) { const m = JSON.parse(text);"
        "      if (m.join) setTimeout(() => this.say({ joined: 1 }), 0);"
        "      if (m.host) setTimeout(() => { this.say({ hosting: 1 }); this.say({ peer: 7 }); }, 0); }"
        "    close() { this.readyState = 3; setTimeout(() => this.onclose && this.onclose(), 0); }"
        "  };"
        "  window.RTCPeerConnection = class {"
        "    constructor() { this.connectionState = 'connected';"
        "      this.localDescription = { toJSON: () => ({ type: 'offer', sdp: '' }) };"
        "      setTimeout(() => { if (!this.channel && this.ondatachannel)"
        "        this.ondatachannel({ channel: this.createDataChannel() }); }, 0); }"
        "    createDataChannel() {"
        "      const channel = this.channel = { readyState: 'open', bufferedAmount: 0,"
        "        send(bytes) {"
        "          net.held = performance.now() - net.sent[new DataView(bytes.buffer).getUint32(0, true)];"
        "          setTimeout(() => channel.onmessage && channel.onmessage({ data: bytes.buffer }), net.real); } };"
        "      return channel; }"
        "    async setLocalDescription() {}"
        "    async setRemoteDescription() {}"
        "    async addIceCandidate() {}"
        "    close() {}"
        "  }; }");
}

static void page_set(const char *what, const int value)
{
    char script[64];
    snprintf(script, sizeof script, "%s = %d", what, value);
    tide_web_eval(script);
}

// Whether the page held the last packet back about `ms` before it went.
static bool held(const double ms)
{
    char script[64];
    snprintf(script, sizeof script, "Math.abs(net.held - %.1f) <= 4", ms);
    return tide_web_eval(script);
}

// Whether the page says the least a lag can be is about `ms` (`Tide.minLag`).
static bool least(const double ms)
{
    char script[64];
    snprintf(script, sizeof script, "Math.abs(Tide.minLag - %.1f) <= 4", ms);
    return tide_web_eval(script);
}

static bool near(const double a, const double b)
{
    return a - b < NEAR && b - a < NEAR;
}

#define REMEMBERED 4096u // Packets it remembers sending: far more than are ever on their way

typedef struct packet {
    uint32_t number;
    uint32_t sent_ms; // The program's time, by its frames, as it went
} packet;

static tide_transport network;
static tide_address other; // Whom it sends to: the room's host, or the player who joined its room
static bool hosting;
static double now;
static uint32_t sent;
static double sent_at[REMEMBERED]; // When each went, by the room's clock
static double ping_ms;             // The network's round trip of the newest answer, as a session measures it
static uint32_t answer_ms;         // ...and what it took to be read, the frames' waits in it
static bool told;                  // Whether it says what it measures, as tide/host.h does

// A session's frame: what came back, what goes out, and what it measured.
// Whether anything came back.
static bool exchange(void)
{
    const uint32_t ms = (uint32_t)(now * 1000.0);
    bool heard = false;
    packet p;
    tide_address from;
    while (network.receive(network.self, &from, &p, sizeof p) == sizeof p) {
        ping_ms = (network.arrived(network.self) - sent_at[p.number % REMEMBERED]) * 1000.0; // The other end holds nothing
        answer_ms = ms - p.sent_ms;
        heard = true;
    }
    p = (packet){.number = sent++, .sent_ms = ms};
    sent_at[p.number % REMEMBERED] = network.clock(network.self);
    char script[64];
    snprintf(script, sizeof script, "net.sent[%u] = performance.now()", (unsigned)p.number);
    tide_web_eval(script);
    network.send(network.self, other, &p, sizeof p);
    if (told) tide_platform_room_ping(hosting ? -1 : (int32_t)(ping_ms + 0.5), answer_ms);
    return heard;
}

enum { REST, UNTOLD, TOLD, CHANGED, SLOWED, SHORT, HOSTING, DONE };

// Seconds each step's pings are left to settle. A network that changed takes
// the longest: the page goes by what pings are on the whole.
static const double settle[DONE] = {1.0, 1.0, 2.0, 1.0, 5.0, 2.0, 2.0};
static const char *const names[DONE] = {"no lag", "a lag of 200, untold", "told", "a lag of 100", "a slower network",
                                        "a lag of 50", "hosting"};

static int step;
static bool live;      // The step heard an answer: its time counts from then
static double began;
static double pings;   // Its pings, added up
static double answers; // ...and what its answers took
static int count;

static int frame(void *user, const float seconds)
{
    (void)user;
    now += seconds;
    const bool heard = exchange();
    // A step's time starts at its first answer, and it checks 20 pings at
    // least: the page's made-up time can leap while the browser is busy (as
    // it starts, on a slow machine), and a step timed from before that would
    // be over with nothing measured.
    if (!live) {
        if (!heard) return TIDE_KEEP_RUNNING;
        live = true;
        began = now;
    }
    if (now - began < settle[step]) return TIDE_KEEP_RUNNING;
    if (heard) {
        pings += ping_ms;
        answers += (double)answer_ms;
        count++;
    }
    if (now - began < settle[step] + WINDOW || count < 20) return TIDE_KEEP_RUNNING;
    const double ping = pings / count;
    printf("%s: a ping of %.1f ms, answers read after %.1f\n", names[step], ping, answers / count);

    switch (step) {
    case REST:
        check(near(ping, NETWORK), "a ping is the network's round trip");
        check(answers / count > ping, "which an answer takes longer than: it waits for a frame to read it");
        check(held(0), "with no lag, nothing's held back");
        page_set("Tide.lag", 200);
        break;
    case UNTOLD:
        check(near(ping, NETWORK + 200), "a host that doesn't say its ping gets the lag on top of it");
        check(tide_web_eval("Tide.minLag === undefined"), "and its page knows no least lag");
        told = true;
        break;
    case TOLD:
        check(near(ping, 200), "told the ping, the page makes it the lag: there and back, the network's time in it");
        check(held((200 - NETWORK) / 2), "half of what it adds is on the way there");
        check(tide_web_eval("Tide.minLag === undefined"), "pings with something added don't say the least a lag can be");
        page_set("Tide.lag", 100);
        break;
    case CHANGED:
        check(near(ping, 100), "the page can change it as the game runs");
        page_set("net.real", SLOWER);
        break;
    case SLOWED:
        check(near(ping, 100), "a network that got slower leaves less to add");
        page_set("Tide.lag", 50);
        break;
    case SHORT:
        check(near(ping, SLOWER), "a lag shorter than the ping adds nothing");
        check(held(0), "and holds nothing back");
        check(least(SLOWER), "with nothing added, the page says the least a lag can be: the ping");
        // The same page hosts a room, still told to lag: the other end is the player who joined
        network.close(network.self);
        page_set("net.real", NETWORK);
        page_set("Tide.lag", 200);
        if (!tide_platform_room_host(&network)) {
            printf("FAIL: no room to host\n");
            return 1;
        }
        other = (tide_address){.kind = TIDE_ADDRESS_ROOM, .host = 0, .port = 1};
        hosting = true;
        break;
    default:
        check(near(ping, NETWORK), "a page that hosts adds nothing: its machine runs the server");
        check(held(0), "and holds nothing back");
        check(tide_web_eval("Tide.minLag === undefined"), "nor has a least lag");
        break;
    }
    step++;
    live = false;
    pings = answers = 0.0;
    count = 0;
    if (step == DONE) return failures == 0 ? 0 : 1;
    return TIDE_KEEP_RUNNING;
}

int main(void)
{
    tide_platform_open(&(tide_window_desc){.title = "web lag", .width = 64, .height = 64, .hidden = true});
    stand_in_for_the_browser();
    page_set("net.real", NETWORK);
    if (!tide_platform_room_join("K7QF2M", &network, &other)) {
        printf("FAIL: no room to join\n");
        return 1;
    }
    tide_platform_run(frame, NULL);
}
