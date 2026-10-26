// The network of a run of several players with no window (src/players.h),
// with no window at all: this program starts itself again for the others, as
// a game does for its players, and each one sends datagrams over the made-up
// network and checks what it reads, frame by frame. When a datagram arrives,
// in what order and from whom; what a room nobody opened says, and a match
// that closed; and that an end a player let go of is nobody's. No GPU, so
// every platform's tests run it, starting processes and all.
//
//   tide_platform_players_net        a run of three, seven frames long
//   tide_platform_players_net die    ...in which the second's program ends at frame 3
//   tide_platform_players_net drop   ...in which the second drops and rejoins, as a script's lines do

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "players.h"

static uint32_t me;    // Which player this process is
static uint32_t frame; // The frame running, from 1
static int failures;

static void check(const bool ok, const char *what)
{
    if (ok) return;
    fprintf(stderr, "FAIL: player %u, frame %u: %s\n", (unsigned)me, (unsigned)frame, what);
    failures++;
}

static void say(const tide_transport *t, const tide_address to, const char *text)
{
    t->send(t->self, to, text, (uint32_t)strlen(text) + 1u);
}

// The next datagram, which is text: "" when there's none.
static const char *hear(const tide_transport *t, tide_address *from)
{
    static char text[64];
    const uint32_t size = t->receive(t->self, from, text, sizeof text - 1);
    text[size] = '\0';
    return text;
}

static bool heard(const tide_transport *t, const char *text, const tide_address from)
{
    tide_address said;
    return strcmp(hear(t, &said), text) == 0 && tide_address_equal(said, from);
}

static bool silent(const tide_transport *t)
{
    tide_address from;
    return hear(t, &from)[0] == '\0';
}

static bool code_is(const char *code)
{
    char room[TIDE_ROOM_CODE_LENGTH + 1];
    tide_platform_players_code(room, sizeof room);
    return strcmp(room, code) == 0;
}

// The first opens its match, answers who joins it, and closes it at frame 5.
static void first(void)
{
    static tide_transport net;
    static tide_address two, three;
    if (frame == 1) {
        check(tide_platform_players_open(7777, &net), "its match opens");
        check(code_is("AAAAAA"), "its room is the first player's");
        check(silent(&net), "nobody wrote yet");
    } else if (frame == 2) {
        // What the second sent in frame 1 is read in frame 2: the third's went to a room nobody has
        check(strcmp(hear(&net, &two), "hello2") == 0, "the second's hello arrives a frame after it was sent");
        check(two.kind == TIDE_ADDRESS_ROOM && two.port == 2 && two.host == 1, "from the second player, joining for the first time");
        check(silent(&net), "and nothing else");
        say(&net, two, "welcome");
        say(&net, two, "a");
        say(&net, two, "b");
    } else if (frame == 3) {
        check(strcmp(hear(&net, &three), "byport") == 0, "an address reaches the match open on its port");
        check(three.kind == TIDE_ADDRESS_ROOM && three.port == 3 && three.host == 3, "from the third player, at the third end it opened");
        check(silent(&net), "and nothing else");
        say(&net, three, "hi3");
        tide_address before = three; // The end the third let go of, when its room turned out nobody's
        before.host = 1;
        say(&net, before, "stale");
    } else if (frame == 4) {
        check(silent(&net), "nothing was sent to it in frame 3");
    } else if (frame == 5) {
        // Both wrote in frame 4: in the players' order, the same every run
        check(heard(&net, "from2", two), "the second's comes first");
        check(heard(&net, "from3", three), "then the third's");
        check(silent(&net), "and nothing else");
        net.close(net.self);
        check(code_is(""), "a match that closed is in no room");
    }
}

// The second joins the first's room, by its code.
static void second(void)
{
    static tide_transport net;
    static tide_address server;
    if (frame == 1) {
        tide_transport none;
        tide_address nobody;
        check(!tide_platform_players_join("K7QF2M", &none, &nobody), "a code that's no player's joins nothing");
        check(!tide_platform_players_join("DDDDDD", &none, &nobody), "nor the code of a fourth player, in a run of three");
        check(tide_platform_players_join("AAAAAA", &net, &server), "the first's code joins its room");
        check(server.kind == TIDE_ADDRESS_ROOM && server.port == 0, "at the room's host");
        check(code_is("AAAAAA"), "which is the room it's in");
        say(&net, server, "hello2");
    } else if (frame == 2) {
        check(silent(&net), "an answer takes a frame each way");
        check(!tide_platform_players_failed(), "the room is open");
    } else if (frame == 3) {
        check(heard(&net, "welcome", server) && heard(&net, "a", server) && heard(&net, "b", server), "what the first sent arrives in the order it was sent");
        check(silent(&net), "and nothing else");
    } else if (frame == 4) {
        say(&net, server, "from2");
    } else if (frame == 5) {
        say(&net, server, "late"); // The first closes its match in this frame
    } else if (frame == 6) {
        check(silent(&net), "a match that closed says nothing more");
        check(!tide_platform_players_failed(), "and its room was one: a room fails only before it ever answered");
    }
}

// The third joins a room nobody opened, then reaches the first's match by an address.
static void third(void)
{
    static tide_transport net;
    static tide_address server;
    if (frame == 1) {
        check(tide_platform_players_join("bbb bbb", &net, &server), "a player's code joins, as a player may type it");
        check(code_is("BBBBBB") && !tide_platform_players_failed(), "the second's room, which nothing says isn't open yet");
        say(&net, server, "x");
    } else if (frame == 2) {
        check(tide_platform_players_failed(), "a room nobody opened says so at once");
        net.close(net.self);
        check(!tide_platform_players_connect("", 7777, &net, &server), "no address connects nowhere");
        check(!tide_platform_players_connect("localhost:port", 7777, &net, &server), "nor one whose port is no number");
        check(tide_platform_players_connect("192.168.1.5:9", 7777, &net, &server) && server.port == 9, "an address says its port");
        net.close(net.self);
        check(tide_platform_players_connect("localhost", 7777, &net, &server), "a name connects, which nothing looks up");
        check(server.kind == TIDE_ADDRESS_IPV4 && server.port == 7777, "on the port it's given when it says none");
        check(code_is("") && !tide_platform_players_failed(), "in no room");
        say(&net, server, "byport");
    } else if (frame == 3) {
        check(silent(&net), "an answer takes a frame each way");
    } else if (frame == 4) {
        check(heard(&net, "hi3", server), "the match answers");
        check(silent(&net), "and what it sent to the end this player let go of is nobody's");
        say(&net, server, "from3");
    }
}

// A script's drop: nothing the second sends goes and nothing comes, until it
// rejoins, which asks the host to join again; a leave asks to leave.
static void dropping(void)
{
    static tide_transport net;
    static tide_address other;
    if (me == 1) {
        if (frame == 1) {
            check(tide_platform_players_open(7777, &net), "its match opens");
        } else if (frame == 2) {
            check(strcmp(hear(&net, &other), "hello") == 0, "the second's hello arrives");
            say(&net, other, "lost"); // The second drops in this frame: it never hears it
        } else if (frame == 4) {
            check(silent(&net), "nothing comes from a player that dropped");
        } else if (frame == 6) {
            check(heard(&net, "back", other), "what it says once it rejoined goes again");
            say(&net, other, "welcome back");
        }
        check(tide_platform_players_action() == TIDE_PLAYERS_NONE, "the first's script asked nothing of its match");
    } else if (me == 2) {
        if (frame == 1) {
            check(tide_platform_players_join("AAAAAA", &net, &other), "the first's code joins its room");
            say(&net, other, "hello");
        } else if (frame == 2) {
            tide_platform_players_drop();
            check(tide_platform_players_action() == TIDE_PLAYERS_NONE, "a drop is the platform's: the host has nothing to do");
        } else if (frame == 3) {
            check(silent(&net), "a dropped player hears nothing");
            say(&net, other, "unheard");
        } else if (frame == 5) {
            tide_platform_players_rejoin();
            tide_platform_players_leave();
            check(tide_platform_players_action() == TIDE_PLAYERS_REJOIN, "a rejoin asks the host to join again");
            check(tide_platform_players_action() == TIDE_PLAYERS_LEAVE, "then a leave, to leave, in the script's order");
            check(tide_platform_players_action() == TIDE_PLAYERS_NONE, "and nothing more");
            say(&net, other, "back");
        } else if (frame == 7) {
            check(heard(&net, "welcome back", other), "and it hears again");
        }
    }
}

int main(int argc, char **argv)
{
    tide_headless_desc desc = {.players = 3, .argc = argc, .argv = argv};
    bool die = false, drop = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "die") == 0) die = true;
        if (strcmp(argv[i], "drop") == 0) drop = true;
        if (strcmp(argv[i], "--player") == 0 && i + 2 < argc) {
            desc.player = (uint32_t)atoi(argv[i + 1]);
            desc.hub = (uint32_t)atoi(argv[i + 2]);
        }
    }
    tide_platform_players_configure(&desc);
    me = tide_platform_player();
    char room[TIDE_ROOM_CODE_LENGTH + 1];
    tide_platform_players_room(2, room, sizeof room);
    check(tide_platform_players() == 3 && strcmp(room, "BBBBBB") == 0, "a run of three, whose second player's room is BBBBBB");
    tide_platform_players_room(4, room, sizeof room);
    check(room[0] == '\0', "and which has no fourth");
    tide_platform_players_start();

    int code = TIDE_KEEP_RUNNING;
    for (frame = 1; frame <= 7 && code == TIDE_KEEP_RUNNING; frame++) {
        if (drop) dropping();
        else if (me == 1) first();
        else if (me == 2) second();
        else third();
        if (die && me == 2 && frame == 3) return 7; // As a game that stops: nothing says goodbye
        if (frame < 7) code = tide_platform_players_step(frame); // The run's last frame has nothing to wait for
    }
    if (die) {
        // The first's step says the run ended, with the second's exit code; the third finds nobody at its own
        if (me != 1) return tide_platform_players_finish(code == TIDE_KEEP_RUNNING ? 0 : code);
        const bool ended = code == 7 && frame == 4; // In frame 3, which the loop then counted past
        code = tide_platform_players_finish(code);
        printf("%s: a player's program that ends at frame 3 ends the run there, with its exit code (%d)\n", ended && code == 7 ? "ok" : "FAIL", code);
        return ended && code == 7 ? 0 : 1;
    }
    check(code == TIDE_KEEP_RUNNING, "the run goes to its end");
    code = tide_platform_players_finish(failures ? 1 : 0);
    if (me == 1 && drop) printf("%s: a dropped player's datagrams go neither way until it rejoins\n", code == 0 ? "ok" : "FAIL");
    else if (me == 1) printf("%s: three players' datagrams arrive a frame after they're sent, in order, to whom they're for\n", code == 0 ? "ok" : "FAIL");
    return code;
}
