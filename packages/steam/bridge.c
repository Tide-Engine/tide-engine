// Steam for the lobby package's four slots: what steam.tide's overrides
// call. The Steam user is the identity; Steam's lobbies are the lobbies and
// the quick match; and a lobby's match goes over Steam's own network, as a
// network of the game's (tide/network.h): datagrams by Steam ID
// (ISteamNetworkingMessages), the text a match is joined by being its host's
// Steam ID, and host migration going by the lobby's owner, which Steam
// decides.
//
// Plain C over Steamworks' flat API (steam_flat.h), with callbacks run by
// hand: no C++ and no SDK header. Desktop only, as Steam is.
//
// Everything Steam hands over that another player set (names, lobby data,
// member data, messages) is that player's bytes: text is bounded and made
// valid before it's written as JSON for the engine's reader, and messages go
// to the session as they came, which takes them as hostile.

#if defined(__wasm__) || defined(__ANDROID__)
#error "The steam package is for desktop builds, where Steam is. List it under a condition in tide.packages (#if STEAM, steam, #endif) and build with --define STEAM for desktop only: a web or Android build without the symbol has Tide's own lobbies and rooms."
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "steam_flat.h"
#include "tide/network.h"
#include "tide/text.h"

// Why a call gave nothing, as steam.tide's Why reads it: Lobbies.LobbyFailure's order.
enum { WHY_NONE, WHY_UNREACHABLE, WHY_SIGNED_OUT, WHY_NOT_FOUND, WHY_FULL, WHY_CLOSED, WHY_KICKED, WHY_NOT_MEMBER,
       WHY_NOT_OWNER, WHY_TOO_MANY, WHY_BUSY, WHY_FAILED };

#define REQUESTS 16     // Calls waiting for Steam's answer at once
#define FILTERS 8       // Keys a list or a quick match filters by
#define KEY_BYTES 64    // A data key, with its NUL: longer ones aren't shown
#define VALUE_BYTES 257 // A data value, with its NUL: longer ones are cut
#define PAIRS_MOST 32   // Keys of a lobby's data, or of a member's, that are shown
#define MEMBERS_MOST 256
#define LISTED_MOST 50
#define PEERS_MOST 256  // Players a host's transport remembers, to close their sessions
#define CHANNEL 0       // The ISteamNetworkingMessages channel matches go on

// Lobby data of the package's own: what Steam has no field for.
#define KEY_NAME "name"
#define KEY_MATCH "tide_match"     // How the lobby's match is reached: its host's Steam ID
#define KEY_LISTED "tide_listed"   // "1" while lists have the lobby
#define KEY_OPEN "tide_open"       // "1" while others can join it
#define KEY_KICKED "tide_kicked"   // The Steam IDs its owner sent out, each followed by a comma
#define KEY_ENDED "tide_ended"     // The host that ended the lobby's match for everyone
#define KEY_KEYS "tide_keys"       // A member's own: the keys of its data, each followed by a comma

typedef enum request_kind { REQUEST_NONE, REQUEST_CREATE, REQUEST_JOIN, REQUEST_LIST, REQUEST_QUICK } request_kind;

typedef struct filter {
    char key[KEY_BYTES];
    char value[VALUE_BYTES];
} filter;

typedef struct request {
    request_kind kind;
    SteamAPICall_t call;
    int expects;      // The callback its call answers with
    bool done;
    int why;          // WHY_NONE once it's done and it worked
    uint64_t lobby;   // The lobby it ended in
    // A lobby to make: a create's, and a quick match's when it finds none
    char name[VALUE_BYTES];
    int max;
    bool listed;
    filter data[FILTERS];
    int data_count;
    int seats;
    bool joining;     // A quick match that found a lobby and is on its way in
} request;

typedef struct steam {
    bool started;
    bool ready;
    ISteamUser *user;
    ISteamFriends *friends;
    ISteamMatchmaking *matchmaking;
    ISteamNetworkingMessages *messages;
    HSteamPipe pipe;
    uint64_t me;
    uint64_t lobby;     // The lobby this player is in, or 0
    int version;        // Counts what Steam said changed of it
    bool kicked;        // Its owner's list named this player: it left
    uint64_t invited;   // A lobby a friend asked this player into, not yet taken
    request requests[REQUESTS];
    filter filters[FILTERS]; // For the next list or quick match
    int filter_count;
    int why;
    // The match's network
    int generation;     // Which transport is the live one: an older one's calls do nothing
    bool live;
    uint64_t host;      // Whom this machine joined, or 0 where it hosts
    bool heard;         // ...and whether anything came from it yet
    bool refused;       // ...or Steam said no session with it could be made, before anything did
    uint64_t lost;      // The host whose match this machine was in when it went
    uint64_t meeting;   // The lobby it went to then (steam_migrate)
    uint32_t peers[PEERS_MOST];
    uint32_t peer_count;
} steam;

// One program is one Steam user. A test runs several in one program, against
// a Steam that isn't (test/fake_steam.c), and says whose turn it is.
#ifndef TIDE_STEAM_USERS
#define TIDE_STEAM_USERS 1
#endif
static steam users[TIDE_STEAM_USERS];
static steam *s = &users[0];

#if TIDE_STEAM_USERS > 1
void TideSteamUse(const int32_t user)
{
    s = &users[user];
}

void TideSteamForget(void)
{
    memset(users, 0, sizeof users);
    s = &users[0];
    tide_network_set(NULL);
}
#endif

static char said[65536];
static size_t said_size;
static bool said_whole;

// ---------------------------------------------------------------------------
// Text

static void id_text(const uint64_t id, char out[24])
{
    snprintf(out, 24, "%llu", (unsigned long long)id);
}

// A Steam ID from text a player may have typed: digits alone, and not 0.
static uint64_t id_of(const char *text)
{
    uint64_t id = 0;
    int digits = 0;
    for (const char *c = text; *c; c++, digits++) {
        if (*c < '0' || *c > '9' || digits == 20) return 0;
        if (id > (UINT64_MAX - (uint64_t)(*c - '0')) / 10u) return 0;
        id = id * 10u + (uint64_t)(*c - '0');
    }
    return id;
}

// Another player's text, bounded and valid: at most `size` bytes with its
// NUL, and UTF-8, with a `?` for each byte of text that isn't.
static void clean(const char *from, char *out, const size_t size)
{
    size_t n = 0;
    while (from && from[n] && n + 1 < size) n++;
    if (n) memcpy(out, from, n);
    out[n] = '\0';
    if (tide_utf8_valid(out, (int32_t)n)) return;
    for (size_t i = 0; i < n; i++) {
        if ((unsigned char)out[i] >= 0x80u) out[i] = '?';
    }
}

static void put(const char *text)
{
    const size_t n = strlen(text);
    if (said_size + n >= sizeof said) {
        said_whole = false;
        return;
    }
    memcpy(said + said_size, text, n + 1);
    said_size += n;
}

static void put_text(const char *text)
{
    put("\"");
    for (const char *c = text; *c; c++) {
        char one[8];
        if (*c == '"' || *c == '\\') snprintf(one, sizeof one, "\\%c", *c);
        else if ((unsigned char)*c < 0x20u || *c == 0x7F) snprintf(one, sizeof one, "\\u%04x", (unsigned)(unsigned char)*c);
        else snprintf(one, sizeof one, "%c", *c);
        put(one);
    }
    put("\"");
}

static void put_number(const long long value)
{
    char text[24];
    snprintf(text, sizeof text, "%lld", value);
    put(text);
}

static void put_id(const uint64_t id)
{
    char text[24];
    id_text(id, text);
    put_text(text);
}

static void begin_said(void)
{
    said_size = 0;
    said[0] = '\0';
    said_whole = true;
}

static const char *nothing(const int why)
{
    s->why = why;
    return "";
}

// ---------------------------------------------------------------------------
// Lobbies, as Steam has them

static const char *lobby_data(const uint64_t lobby, const char *key)
{
    const char *value = SteamAPI_ISteamMatchmaking_GetLobbyData(s->matchmaking, lobby, key);
    return value ? value : "";
}

static bool is_member(const uint64_t lobby, const uint64_t user)
{
    int count = SteamAPI_ISteamMatchmaking_GetNumLobbyMembers(s->matchmaking, lobby);
    if (count > MEMBERS_MOST) count = MEMBERS_MOST;
    for (int i = 0; i < count; i++) {
        if (SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex(s->matchmaking, lobby, i) == user) return true;
    }
    return false;
}

// Whether a list of IDs, each followed by a comma, has `id`.
static bool listed_id(const char *list, const uint64_t id)
{
    char text[26];
    id_text(id, text);
    const size_t n = strlen(text);
    text[n] = ',';
    text[n + 1] = '\0';
    for (const char *at = list; (at = strstr(at, text)); at++) {
        if (at == list || at[-1] == ',') return true;
    }
    return false;
}

// A package's own key isn't the game's data.
static bool own_key(const char *key)
{
    return strncmp(key, "tide_", 5) == 0 || strcmp(key, KEY_NAME) == 0;
}

// The lobby's data as a JSON object, less the package's own keys.
static void put_lobby_data(const uint64_t lobby)
{
    put("{");
    int count = SteamAPI_ISteamMatchmaking_GetLobbyDataCount(s->matchmaking, lobby);
    int shown = 0;
    for (int i = 0; i < count && shown < PAIRS_MOST; i++) {
        char key[256], value[8192]; // Steam's own limits
        key[0] = value[0] = '\0';
        if (!SteamAPI_ISteamMatchmaking_GetLobbyDataByIndex(s->matchmaking, lobby, i, key, (int)sizeof key, value,
                                                            (int)sizeof value)) {
            continue;
        }
        key[sizeof key - 1] = value[sizeof value - 1] = '\0';
        if (!key[0] || strlen(key) >= KEY_BYTES || own_key(key)) continue;
        char clean_key[KEY_BYTES], clean_value[VALUE_BYTES];
        clean(key, clean_key, sizeof clean_key);
        clean(value, clean_value, sizeof clean_value);
        if (shown++) put(",");
        put_text(clean_key);
        put(":");
        put_text(clean_value);
    }
    put("}");
}

// A member's data: Steam lists no keys of it, so a member says its own, in a key of its data.
static void put_member_data(const uint64_t lobby, const uint64_t member)
{
    put("{");
    char keys[VALUE_BYTES];
    clean(SteamAPI_ISteamMatchmaking_GetLobbyMemberData(s->matchmaking, lobby, member, KEY_KEYS), keys, sizeof keys);
    int shown = 0;
    for (char *key = keys, *comma; (comma = strchr(key, ',')) && shown < PAIRS_MOST; key = comma + 1) {
        *comma = '\0';
        if (!key[0] || strlen(key) >= KEY_BYTES || own_key(key)) continue;
        char value[VALUE_BYTES];
        clean(SteamAPI_ISteamMatchmaking_GetLobbyMemberData(s->matchmaking, lobby, member, key), value, sizeof value);
        if (shown++) put(",");
        put_text(key);
        put(":");
        put_text(value);
    }
    put("}");
}

// A lobby this player is in, as the relay's lobbies say one: what the lobby
// package's LobbyInfo reads.
static const char *lobby_said(const uint64_t lobby)
{
    char text[VALUE_BYTES];
    begin_said();
    put("{\"id\":");
    put_id(lobby);
    put(",\"game\":\"steam\",\"name\":");
    clean(lobby_data(lobby, KEY_NAME), text, sizeof text);
    put_text(text);
    put(",\"owner\":");
    put_id(SteamAPI_ISteamMatchmaking_GetLobbyOwner(s->matchmaking, lobby));
    put(",\"max\":");
    put_number(SteamAPI_ISteamMatchmaking_GetLobbyMemberLimit(s->matchmaking, lobby));
    put(strcmp(lobby_data(lobby, KEY_LISTED), "1") == 0 ? ",\"public\":true" : ",\"public\":false");
    put(strcmp(lobby_data(lobby, KEY_OPEN), "0") != 0 ? ",\"open\":true" : ",\"open\":false");
    put(",\"data\":");
    put_lobby_data(lobby);
    put(",\"match\":");
    // A match is joined by a Steam ID: anything else there names none
    const uint64_t match = id_of(lobby_data(lobby, KEY_MATCH));
    if (match) put_id(match);
    else put("\"\"");
    put(",\"version\":");
    put_number(s->version);
    put(",\"you\":");
    put_id(s->me);
    put(",\"members\":[");
    int count = SteamAPI_ISteamMatchmaking_GetNumLobbyMembers(s->matchmaking, lobby);
    if (count > MEMBERS_MOST) count = MEMBERS_MOST;
    for (int i = 0; i < count; i++) {
        const uint64_t member = SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex(s->matchmaking, lobby, i);
        if (i) put(",");
        put("{\"id\":");
        put_id(member);
        put(",\"name\":");
        clean(member == s->me ? SteamAPI_ISteamFriends_GetPersonaName(s->friends)
                              : SteamAPI_ISteamFriends_GetFriendPersonaName(s->friends, member),
              text, 65);
        put_text(text);
        put(",\"data\":");
        put_member_data(lobby, member);
        put("}");
    }
    put("]}");
    if (!said_whole) return nothing(WHY_FAILED);
    s->why = WHY_NONE;
    return said;
}

// This player is out of its lobby, by its own doing here.
static void forget_lobby(void)
{
    if (s->lobby) SteamAPI_ISteamMatchmaking_LeaveLobby(s->matchmaking, s->lobby);
    s->lobby = 0;
}

// A kick isn't something Steam has: the owner lists whom it sent out, in the
// lobby's data, and the listed player's own program leaves when it reads so.
static void obey_kicks(void)
{
    if (!s->lobby || !listed_id(lobby_data(s->lobby, KEY_KICKED), s->me)) return;
    forget_lobby();
    s->kicked = true;
}

// ---------------------------------------------------------------------------
// Calls Steam answers later

static request *new_request(const request_kind kind)
{
    for (int i = 0; i < REQUESTS; i++) {
        if (s->requests[i].kind != REQUEST_NONE) continue;
        s->requests[i] = (request){.kind = kind};
        return &s->requests[i];
    }
    return NULL;
}

static int number_of(const request *r)
{
    return r ? (int)(r - s->requests) + 1 : 0;
}

static void finish(request *r, const int why, const uint64_t lobby)
{
    r->done = true;
    r->why = why;
    r->lobby = lobby;
    r->call = 0;
}

static void call(request *r, const SteamAPICall_t handle, const int expects)
{
    r->call = handle;
    r->expects = expects;
    if (!handle) finish(r, WHY_UNREACHABLE, 0);
}

static void start_list(request *r, const filter *filters, const int count, const int seats)
{
    for (int i = 0; i < count; i++) {
        SteamAPI_ISteamMatchmaking_AddRequestLobbyListStringFilter(s->matchmaking, filters[i].key, filters[i].value,
                                                                   k_ELobbyComparisonEqual);
    }
    // Lobbies the package made, that others can join, with room
    SteamAPI_ISteamMatchmaking_AddRequestLobbyListStringFilter(s->matchmaking, KEY_OPEN, "1", k_ELobbyComparisonEqual);
    SteamAPI_ISteamMatchmaking_AddRequestLobbyListFilterSlotsAvailable(s->matchmaking, seats > 1 ? seats : 1);
    SteamAPI_ISteamMatchmaking_AddRequestLobbyListDistanceFilter(s->matchmaking, k_ELobbyDistanceFilterWorldwide);
    SteamAPI_ISteamMatchmaking_AddRequestLobbyListResultCountFilter(s->matchmaking, LISTED_MOST);
    call(r, SteamAPI_ISteamMatchmaking_RequestLobbyList(s->matchmaking), k_iLobbyMatchList);
}

static void start_create(request *r)
{
    int max = r->max > 0 ? r->max : 8;
    if (max > 250) max = 250; // Steam's most
    call(r, SteamAPI_ISteamMatchmaking_CreateLobby(s->matchmaking, r->listed ? k_ELobbyTypePublic : k_ELobbyTypePrivate, max),
         k_iLobbyCreated);
}

static void entered(const uint64_t lobby)
{
    s->lobby = lobby;
    s->kicked = false;
    s->version++;
}

// The lobby this player made: its name and the package's own keys.
static void created(request *r, const uint64_t lobby)
{
    entered(lobby);
    SteamAPI_ISteamMatchmaking_SetLobbyData(s->matchmaking, lobby, KEY_NAME, r->name);
    SteamAPI_ISteamMatchmaking_SetLobbyData(s->matchmaking, lobby, KEY_LISTED, r->listed ? "1" : "0");
    SteamAPI_ISteamMatchmaking_SetLobbyData(s->matchmaking, lobby, KEY_OPEN, "1");
    for (int i = 0; i < r->data_count; i++) {
        SteamAPI_ISteamMatchmaking_SetLobbyData(s->matchmaking, lobby, r->data[i].key, r->data[i].value);
    }
    finish(r, WHY_NONE, lobby);
}

// A callback's field, from its bytes.
static uint64_t read_u64(const uint8_t *bytes, const size_t size, const size_t at)
{
    uint64_t value = 0;
    if (at + sizeof value <= size) memcpy(&value, bytes + at, sizeof value);
    return value;
}

static uint32_t read_u32(const uint8_t *bytes, const size_t size, const size_t at)
{
    uint32_t value = 0;
    if (at + sizeof value <= size) memcpy(&value, bytes + at, sizeof value);
    return value;
}

// LobbyCreated_t's lobby: after the result's 4 bytes where the SDK packs to
// 4 (Linux and macOS), and at 8 where it packs to 8 (Windows).
static uint64_t steam_lobby_created(const uint8_t *bytes, const size_t size)
{
#ifdef _WIN32
    return read_u64(bytes, size, 8);
#else
    return read_u64(bytes, size, 4);
#endif
}

// A quick match's list came: the fullest lobby in it that this player may
// join, or one of its own.
static void quick_listed(request *r, const uint32_t found)
{
    uint64_t best = 0;
    int best_members = -1;
    for (uint32_t i = 0; i < found && i < LISTED_MOST; i++) {
        const uint64_t lobby = SteamAPI_ISteamMatchmaking_GetLobbyByIndex(s->matchmaking, (int)i);
        if (!lobby || listed_id(lobby_data(lobby, KEY_KICKED), s->me)) continue;
        const int members = SteamAPI_ISteamMatchmaking_GetNumLobbyMembers(s->matchmaking, lobby);
        if (members > best_members) {
            best = lobby;
            best_members = members;
        }
    }
    if (!best) {
        start_create(r);
        return;
    }
    r->joining = true;
    call(r, SteamAPI_ISteamMatchmaking_JoinLobby(s->matchmaking, best), k_iLobbyEnter);
}

static void answered(request *r, const uint8_t *bytes, const size_t size, const bool failed)
{
    if (failed) {
        finish(r, WHY_UNREACHABLE, 0);
        return;
    }
    if (r->expects == k_iLobbyMatchList) {
        const uint32_t found = read_u32(bytes, size, 0);
        if (r->kind == REQUEST_QUICK) quick_listed(r, found);
        else finish(r, WHY_NONE, found); // A list's count, in the lobby's place
    } else if (r->expects == k_iLobbyCreated) {
        const uint64_t lobby = steam_lobby_created(bytes, size);
        if ((int32_t)read_u32(bytes, size, 0) != k_EResultOK || !lobby) finish(r, WHY_FAILED, 0);
        else created(r, lobby);
    } else if (r->expects == k_iLobbyEnter) {
        const uint64_t lobby = read_u64(bytes, size, offsetof(LobbyEnter_t, m_ulSteamIDLobby));
        const uint32_t response = read_u32(bytes, size, offsetof(LobbyEnter_t, m_EChatRoomEnterResponse));
        if (response == k_EChatRoomEnterResponseSuccess && lobby) {
            entered(lobby);
            obey_kicks(); // A lobby whose owner sent this player out stays left
            if (s->kicked) finish(r, WHY_KICKED, 0);
            else finish(r, WHY_NONE, lobby);
        } else if (r->kind == REQUEST_QUICK && r->joining) {
            // The lobby it found filled, or went, meanwhile: one of its own
            r->joining = false;
            start_create(r);
        } else {
            finish(r, response == k_EChatRoomEnterResponseFull ? WHY_FULL
                      : response == k_EChatRoomEnterResponseDoesntExist ? WHY_NOT_FOUND
                      : response == k_EChatRoomEnterResponseBanned ? WHY_KICKED
                      : response == k_EChatRoomEnterResponseNotAllowed ? WHY_CLOSED : WHY_FAILED, 0);
        }
    }
}

static void identity_of(SteamNetworkingIdentity *identity, const uint64_t id)
{
    SteamAPI_SteamNetworkingIdentity_Clear(identity);
    SteamAPI_SteamNetworkingIdentity_SetSteamID64(identity, id);
}

// Steam's callbacks, run by hand: once a frame, as the network's `update`.
static void steam_update(void *self)
{
    (void)self;
    if (!s->ready) return;
    SteamAPI_ManualDispatch_RunFrame(s->pipe);
    CallbackMsg_t message;
    while (SteamAPI_ManualDispatch_GetNextCallback(s->pipe, &message)) {
        const uint8_t *bytes = message.m_pubParam;
        const size_t size = message.m_cubParam > 0 && bytes ? (size_t)message.m_cubParam : 0;
        if (message.m_iCallback == k_iSteamAPICallCompleted) {
            const SteamAPICall_t handle = read_u64(bytes, size, offsetof(SteamAPICallCompleted_t, m_hAsyncCall));
            const int kind = (int)read_u32(bytes, size, offsetof(SteamAPICallCompleted_t, m_iCallback));
            const uint32_t result_size = read_u32(bytes, size, offsetof(SteamAPICallCompleted_t, m_cubParam));
            for (int i = 0; i < REQUESTS; i++) {
                request *r = &s->requests[i];
                if (r->kind == REQUEST_NONE || r->done || !handle || r->call != handle) continue;
                uint8_t result[256] = {0};
                bool failed = kind != r->expects || result_size > sizeof result;
                if (!failed && !SteamAPI_ManualDispatch_GetAPICallResult(s->pipe, handle, result, (int)result_size, kind,
                                                                         &failed)) {
                    failed = true;
                }
                answered(r, result, result_size, failed);
                break;
            }
        } else if (message.m_iCallback == k_iLobbyDataUpdate || message.m_iCallback == k_iLobbyChatUpdate) {
            // The same offset in both: the lobby that changed
            if (s->lobby && read_u64(bytes, size, 0) == s->lobby) {
                s->version++;
                obey_kicks();
            }
        } else if (message.m_iCallback == k_iGameLobbyJoinRequested) {
            s->invited = read_u64(bytes, size, offsetof(GameLobbyJoinRequested_t, m_steamIDLobby));
        } else if (message.m_iCallback == k_iSteamNetworkingMessagesSessionRequest) {
            // Another player wants to send this one datagrams. A host takes anyone's, as its session decides
            // who's let into the match; any other machine takes the host's of the match it joined, and its
            // lobby's members', one of whom may host next
            SteamNetworkingIdentity remote;
            memset(&remote, 0, sizeof remote);
            if (size >= sizeof remote) memcpy(&remote, bytes, sizeof remote);
            const uint64_t who = SteamAPI_SteamNetworkingIdentity_GetSteamID64(&remote);
            const bool hosting = s->live && !s->host;
            const bool known = who && (hosting || (s->live && who == s->host) || (s->lobby && is_member(s->lobby, who)));
            if (known) SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser(s->messages, &remote);
        } else if (message.m_iCallback == k_iSteamNetworkingMessagesSessionFailed) {
            // The session's information starts with other things than who it was with, which differ by SDK:
            // all that's taken from it is that a session failed while this machine waits for a host it never heard
            if (s->live && s->host && !s->heard) s->refused = true;
        }
        SteamAPI_ManualDispatch_FreeLastCallback(s->pipe);
    }
}

// ---------------------------------------------------------------------------
// The match's network (tide/network.h)

typedef struct steam_handle {
    steam *user;
    int generation;
} steam_handle;

// A player's address is its account: the low half of its Steam ID. Every
// player's high half is the same as this one's (an individual's account, of
// the same universe), and what comes from any other kind of ID is dropped.
static uint64_t peer_id(const steam *u, const uint32_t account)
{
    return (u->me & 0xFFFFFFFF00000000ull) | account;
}

static steam *live_of(void *self)
{
    const steam_handle *h = self;
    return h->user->live && h->user->generation == h->generation ? h->user : NULL;
}

static void steam_send(void *self, const tide_address to, const void *data, const uint32_t size)
{
    steam *u = live_of(self);
    if (!u || to.kind != TIDE_ADDRESS_NETWORK || !to.host || size > TIDE_NET_MTU) return;
    SteamNetworkingIdentity remote;
    identity_of(&remote, peer_id(u, to.host));
    SteamAPI_ISteamNetworkingMessages_SendMessageToUser(
        u->messages, &remote, data, size,
        k_nSteamNetworkingSend_UnreliableNoNagle | k_nSteamNetworkingSend_AutoRestartBrokenSession, CHANNEL);
    if (u->host) return;
    // A host remembers whom it sent to, to close their sessions with its transport
    for (uint32_t i = 0; i < u->peer_count; i++) {
        if (u->peers[i] == to.host) return;
    }
    if (u->peer_count < PEERS_MOST) u->peers[u->peer_count++] = to.host;
}

static uint32_t steam_receive(void *self, tide_address *from, void *data, const uint32_t capacity)
{
    steam *u = live_of(self);
    while (u) {
        SteamNetworkingMessage_t *message = NULL;
        if (SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel(u->messages, CHANNEL, &message, 1) < 1 || !message) {
            return 0;
        }
        const uint64_t who = SteamAPI_SteamNetworkingIdentity_GetSteamID64(&message->m_identityPeer);
        const int size = message->m_cbSize;
        // From a player like this one, and to a joined match, from its host alone; no longer than a datagram
        const bool ours = who && who != u->me && (who >> 32) == (u->me >> 32) && (!u->host || who == u->host);
        const bool fits = size > 0 && (uint32_t)size <= capacity && message->m_pData;
        if (ours && fits) memcpy(data, message->m_pData, (size_t)size);
        SteamAPI_SteamNetworkingMessage_t_Release(message);
        if (!ours || !fits) continue;
        if (u->host) u->heard = true;
        *from = (tide_address){TIDE_ADDRESS_NETWORK, (uint32_t)who, 0};
        return (uint32_t)size;
    }
    return 0;
}

static void steam_close(void *self)
{
    steam *u = live_of(self);
    if (u) {
        SteamNetworkingIdentity remote;
        if (u->host) {
            identity_of(&remote, u->host);
            SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser(u->messages, &remote);
        }
        for (uint32_t i = 0; i < u->peer_count; i++) {
            identity_of(&remote, peer_id(u, u->peers[i]));
            SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser(u->messages, &remote);
        }
        u->peer_count = 0;
        u->live = false;
    }
    free(self);
}

// The match ended for everyone: the lobby says so, where its players would
// come to take it over (steam_migrated).
static void steam_end(void *self)
{
    const steam *u = live_of(self);
    if (!u || u->host || !u->lobby) return;
    if (SteamAPI_ISteamMatchmaking_GetLobbyOwner(u->matchmaking, u->lobby) != u->me) return;
    char id[24];
    id_text(u->me, id);
    SteamAPI_ISteamMatchmaking_SetLobbyData(u->matchmaking, u->lobby, KEY_ENDED, id);
}

static bool open_transport(const uint64_t host, tide_transport *out)
{
    steam_handle *h = s->ready ? malloc(sizeof *h) : NULL;
    if (!h) return false;
    s->live = true;
    s->host = host;
    s->heard = false;
    s->refused = false;
    s->peer_count = 0;
    *h = (steam_handle){s, ++s->generation};
    *out = (tide_transport){.self = h, .send = steam_send, .receive = steam_receive, .close = steam_close, .end = steam_end};
    return true;
}

static bool steam_host(void *self, tide_transport *out)
{
    (void)self;
    if (!open_transport(0, out)) return false;
    // Another match of this host's: the last one's end isn't this one's
    if (s->lobby && SteamAPI_ISteamMatchmaking_GetLobbyOwner(s->matchmaking, s->lobby) == s->me && lobby_data(s->lobby, KEY_ENDED)[0]) {
        SteamAPI_ISteamMatchmaking_DeleteLobbyData(s->matchmaking, s->lobby, KEY_ENDED);
    }
    return true;
}

static bool steam_join(void *self, const char *address, tide_transport *out, tide_address *server)
{
    (void)self;
    const uint64_t host = id_of(address);
    // A player like this one, and another: anything else is no match's host
    if (!s->ready || !host || host == s->me || (host >> 32) != (s->me >> 32) || !(uint32_t)host) return false;
    if (!open_transport(host, out)) return false;
    s->lost = host;
    *server = (tide_address){TIDE_ADDRESS_NETWORK, (uint32_t)host, 0};
    return true;
}

static void steam_address(void *self, char *out, const size_t size)
{
    (void)self;
    char id[24] = "";
    if (s->live) id_text(s->host ? s->host : s->me, id);
    snprintf(out, size, "%s", id);
}

static bool steam_failed(void *self)
{
    (void)self;
    return s->live && s->host && s->refused && !s->heard;
}

// Where a match's players meet when its host goes: the lobby its host is in.
static void steam_place(void *self, char *out, const size_t size)
{
    (void)self;
    char id[24] = "";
    // A lobby's owner is who hosts next, so only its owner's match has a place to meet
    const bool owns = s->lobby && SteamAPI_ISteamMatchmaking_GetLobbyOwner(s->matchmaking, s->lobby) == s->me;
    if (s->live && !s->host && owns) id_text(s->lobby, id);
    snprintf(out, size, "%s", id);
}

static void steam_migrate(void *self, const char *place)
{
    (void)self;
    s->meeting = id_of(place);
}

// Who hosts the match now is the lobby's owner, which is Steam's to say: it
// hands a lobby on when its owner leaves it or goes offline. While the owner
// is still the host that went, nothing is known.
static int steam_migrated(void *self, tide_transport *out, tide_address *server)
{
    (void)self;
    if (!s->ready || !s->meeting || s->meeting != s->lobby || !is_member(s->lobby, s->me)) return TIDE_NETWORK_LOST;
    if (s->lost && id_of(lobby_data(s->lobby, KEY_ENDED)) == s->lost) return TIDE_NETWORK_ENDED;
    const uint64_t owner = SteamAPI_ISteamMatchmaking_GetLobbyOwner(s->matchmaking, s->lobby);
    if (!owner || owner == s->lost) return TIDE_NETWORK_MOVING;
    if (owner == s->me) return open_transport(0, out) ? TIDE_NETWORK_HOSTS : TIDE_NETWORK_LOST;
    if ((owner >> 32) != (s->me >> 32)) return TIDE_NETWORK_LOST;
    if (!open_transport(owner, out)) return TIDE_NETWORK_LOST;
    s->lost = owner;
    *server = (tide_address){TIDE_ADDRESS_NETWORK, (uint32_t)owner, 0};
    return TIDE_NETWORK_JOINS;
}

// ---------------------------------------------------------------------------
// What steam.tide calls

// Starts Steam, once, and makes its network the game's. False without Steam:
// its client isn't running, or the game isn't one it knows (steam_appid.txt).
bool TideSteamStart(void)
{
    if (!s->started) {
        s->started = true;
        SteamErrMsg error = {0};
        if (SteamAPI_InitFlat(&error) == k_ESteamAPIInitResult_OK) {
            SteamAPI_ManualDispatch_Init();
            s->pipe = SteamAPI_GetHSteamPipe();
            s->user = SteamAPI_SteamUser_v023();
            s->friends = SteamAPI_SteamFriends_v017();
            s->matchmaking = SteamAPI_SteamMatchmaking_v009();
            s->messages = SteamAPI_SteamNetworkingMessages_SteamAPI_v002();
            s->ready = s->user && s->friends && s->matchmaking && s->messages;
            if (s->ready) s->me = SteamAPI_ISteamUser_GetSteamID(s->user);
            if (!s->me) s->ready = false;
        } else {
            error[sizeof error - 1] = '\0';
            fprintf(stderr, "tide: Steam didn't start: %s\n", error);
        }
    }
    if (s->ready) {
        tide_network_set(&(tide_network){.host = steam_host, .join = steam_join, .address = steam_address,
                                         .failed = steam_failed, .update = steam_update, .place = steam_place,
                                         .migrate = steam_migrate, .migrated = steam_migrated});
    }
    return s->ready;
}

const char *TideSteamId(void)
{
    static char id[24];
    id_text(s->ready ? s->me : 0, id);
    return s->ready ? id : "";
}

const char *TideSteamName(void)
{
    static char name[65];
    clean(s->ready ? SteamAPI_ISteamFriends_GetPersonaName(s->friends) : "", name, sizeof name);
    return name;
}

int32_t TideSteamWhy(void)
{
    return s->why;
}

// A key the next list or quick match filters by.
void TideSteamFilter(const char *key, const char *value)
{
    if (s->filter_count == FILTERS || !key[0] || strlen(key) >= KEY_BYTES || strlen(value) >= VALUE_BYTES) return;
    filter *f = &s->filters[s->filter_count++];
    snprintf(f->key, sizeof f->key, "%s", key);
    snprintf(f->value, sizeof f->value, "%s", value);
}

static void take_filters(request *r)
{
    if (r) {
        memcpy(r->data, s->filters, sizeof r->data);
        r->data_count = s->filter_count;
    }
    s->filter_count = 0;
}

// Each of these starts a call Steam answers later, and gives its number: 0
// when it couldn't start. TideSteamDone says when it's answered, and
// TideSteamTake then gives the answer and frees the number.
int32_t TideSteamCreate(const char *name, const int32_t max, const bool listed)
{
    request *r = s->ready ? new_request(REQUEST_CREATE) : NULL;
    take_filters(r); // The data the lobby starts with
    if (!r) return 0;
    snprintf(r->name, sizeof r->name, "%s", name);
    r->max = max;
    r->listed = listed;
    forget_lobby();
    start_create(r);
    return number_of(r);
}

int32_t TideSteamJoin(const char *id)
{
    request *r = s->ready ? new_request(REQUEST_JOIN) : NULL;
    if (!r) return 0;
    const uint64_t lobby = id_of(id);
    forget_lobby();
    if (!lobby) finish(r, WHY_NOT_FOUND, 0);
    else call(r, SteamAPI_ISteamMatchmaking_JoinLobby(s->matchmaking, lobby), k_iLobbyEnter);
    return number_of(r);
}

int32_t TideSteamList(const int32_t seats)
{
    request *r = s->ready ? new_request(REQUEST_LIST) : NULL;
    take_filters(r);
    if (!r) return 0;
    start_list(r, r->data, r->data_count, seats);
    return number_of(r);
}

// Find or create: the fullest listed lobby the filters match, with `seats`
// free, or a new one with the filters as its data. Steam has no one call for
// it, so two players who ask at the same moment can each make a lobby.
int32_t TideSteamQuick(const int32_t seats, const char *name, const int32_t max)
{
    request *r = s->ready ? new_request(REQUEST_QUICK) : NULL;
    take_filters(r);
    if (!r) return 0;
    snprintf(r->name, sizeof r->name, "%s", name);
    r->max = max;
    r->listed = true;
    r->seats = seats;
    forget_lobby();
    start_list(r, r->data, r->data_count, seats);
    return number_of(r);
}

static request *request_of(const int32_t number)
{
    return number >= 1 && number <= REQUESTS && s->requests[number - 1].kind != REQUEST_NONE ? &s->requests[number - 1] : NULL;
}

bool TideSteamDone(const int32_t number)
{
    const request *r = request_of(number);
    return !r || r->done;
}

// A request's lobbies, as the lobby package's list reads them.
static const char *list_said(const uint32_t found)
{
    begin_said();
    put("{\"lobbies\":[");
    int shown = 0;
    for (uint32_t i = 0; i < found && i < LISTED_MOST; i++) {
        const uint64_t lobby = SteamAPI_ISteamMatchmaking_GetLobbyByIndex(s->matchmaking, (int)i);
        if (!lobby || strcmp(lobby_data(lobby, KEY_LISTED), "1") != 0) continue;
        char name[VALUE_BYTES];
        if (shown++) put(",");
        put("{\"id\":");
        put_id(lobby);
        put(",\"name\":");
        clean(lobby_data(lobby, KEY_NAME), name, sizeof name);
        put_text(name);
        put(",\"players\":");
        put_number(SteamAPI_ISteamMatchmaking_GetNumLobbyMembers(s->matchmaking, lobby));
        put(",\"max\":");
        put_number(SteamAPI_ISteamMatchmaking_GetLobbyMemberLimit(s->matchmaking, lobby));
        put(",\"data\":");
        put_lobby_data(lobby);
        put("}");
    }
    put("]}");
    if (!said_whole) return nothing(WHY_FAILED);
    s->why = WHY_NONE;
    return said;
}

// What a finished request came to: the lobby this player is in now (a list:
// the lobbies found), written as JSON, or "" with why (TideSteamWhy).
const char *TideSteamTake(const int32_t number)
{
    request *r = request_of(number);
    if (!r || !r->done) return nothing(r ? WHY_BUSY : WHY_UNREACHABLE);
    const request done = *r;
    r->kind = REQUEST_NONE;
    if (done.why != WHY_NONE) return nothing(done.why);
    if (done.kind == REQUEST_LIST) return list_said((uint32_t)done.lobby);
    return done.lobby == s->lobby ? lobby_said(done.lobby) : nothing(WHY_NOT_MEMBER);
}

// A request's number from the text a ticket keeps it as: 0 for text that's none.
int32_t TideSteamNumber(const char *text)
{
    const uint64_t number = id_of(text);
    return number <= REQUESTS ? (int32_t)number : 0;
}

// The lobby this player is in, by its ID, as JSON, or "" with why.
const char *TideSteamLobby(const char *id)
{
    const uint64_t lobby = id_of(id);
    if (!s->ready) return nothing(WHY_SIGNED_OUT);
    if (!lobby || lobby != s->lobby) return nothing(s->kicked ? WHY_KICKED : WHY_NOT_MEMBER);
    return lobby_said(lobby);
}

// Counts what Steam said changed of the lobby, or -1 for one this player isn't in.
int32_t TideSteamVersion(const char *id)
{
    const uint64_t lobby = id_of(id);
    return s->ready && lobby && lobby == s->lobby ? s->version : -1;
}

void TideSteamLeave(const char *id)
{
    if (s->ready && id_of(id) && id_of(id) == s->lobby) forget_lobby();
}

static bool mine_to_change(const uint64_t lobby, const bool owners)
{
    s->why = !s->ready ? WHY_SIGNED_OUT
           : !lobby || lobby != s->lobby ? WHY_NOT_MEMBER
           : owners && SteamAPI_ISteamMatchmaking_GetLobbyOwner(s->matchmaking, lobby) != s->me ? WHY_NOT_OWNER : WHY_NONE;
    return s->why == WHY_NONE;
}

// A change its owner asks: 0 its name, 1 its match, 2 its owner, 3 a member
// sent out, 4 whether it's open ("1" or "0"), 5 how many it takes, 6 whether
// lists have it ("1" or "0"). False with why.
bool TideSteamSet(const char *id, const int32_t what, const char *value)
{
    const uint64_t lobby = id_of(id);
    if (!mine_to_change(lobby, true)) return false;
    bool ok = true;
    if (what == 0) ok = SteamAPI_ISteamMatchmaking_SetLobbyData(s->matchmaking, lobby, KEY_NAME, value);
    if (what == 1) ok = SteamAPI_ISteamMatchmaking_SetLobbyData(s->matchmaking, lobby, KEY_MATCH, value);
    if (what == 2 || what == 3) {
        const uint64_t member = id_of(value);
        if (!member || member == s->me || !is_member(lobby, member)) {
            s->why = WHY_NOT_FOUND;
            return false;
        }
        if (what == 2) {
            ok = SteamAPI_ISteamMatchmaking_SetLobbyOwner(s->matchmaking, lobby, member);
        } else {
            char list[VALUE_BYTES], next[VALUE_BYTES + 26], one[24];
            clean(lobby_data(lobby, KEY_KICKED), list, sizeof list);
            id_text(member, one);
            // A list that's full forgets its oldest: the lobby keeps the last ones sent out
            while (strlen(list) + strlen(one) + 2 > VALUE_BYTES - 1 && strchr(list, ',')) {
                memmove(list, strchr(list, ',') + 1, strlen(strchr(list, ',') + 1) + 1);
            }
            snprintf(next, sizeof next, "%s%s,", list, one);
            ok = SteamAPI_ISteamMatchmaking_SetLobbyData(s->matchmaking, lobby, KEY_KICKED, next);
        }
    }
    if (what == 4) {
        const bool open = value[0] == '1';
        ok = SteamAPI_ISteamMatchmaking_SetLobbyJoinable(s->matchmaking, lobby, open)
             && SteamAPI_ISteamMatchmaking_SetLobbyData(s->matchmaking, lobby, KEY_OPEN, open ? "1" : "0");
    }
    if (what == 5) ok = SteamAPI_ISteamMatchmaking_SetLobbyMemberLimit(s->matchmaking, lobby, atoi(value));
    if (what == 6) {
        const bool listed = value[0] == '1';
        ok = SteamAPI_ISteamMatchmaking_SetLobbyType(s->matchmaking, lobby, listed ? k_ELobbyTypePublic : k_ELobbyTypePrivate)
             && SteamAPI_ISteamMatchmaking_SetLobbyData(s->matchmaking, lobby, KEY_LISTED, listed ? "1" : "0");
    }
    if (!ok) s->why = WHY_FAILED;
    else s->version++; // What this player changed, it sees at once
    return ok;
}

// A key of the lobby's data (its owner's to set), or of this member's own,
// set, or taken away with `remove`. The package's own keys aren't the game's.
bool TideSteamSetData(const char *id, const bool own, const char *key, const char *value, const bool remove)
{
    const uint64_t lobby = id_of(id);
    if (!mine_to_change(lobby, !own)) return false;
    if (!key[0] || strlen(key) >= KEY_BYTES || own_key(key) || strchr(key, ',') || strlen(value) >= VALUE_BYTES) {
        s->why = WHY_FAILED;
        return false;
    }
    bool ok = true;
    if (!own) {
        ok = remove ? SteamAPI_ISteamMatchmaking_DeleteLobbyData(s->matchmaking, lobby, key)
                    : SteamAPI_ISteamMatchmaking_SetLobbyData(s->matchmaking, lobby, key, value);
    } else {
        // The member's keys, with this one among them or not, then its value
        char keys[VALUE_BYTES], next[VALUE_BYTES + KEY_BYTES + 2] = "";
        clean(SteamAPI_ISteamMatchmaking_GetLobbyMemberData(s->matchmaking, lobby, s->me, KEY_KEYS), keys, sizeof keys);
        for (char *at = keys, *comma; (comma = strchr(at, ',')); at = comma + 1) {
            *comma = '\0';
            if (strcmp(at, key) != 0 && at[0]) snprintf(next + strlen(next), sizeof next - strlen(next), "%s,", at);
        }
        if (!remove) snprintf(next + strlen(next), sizeof next - strlen(next), "%s,", key);
        if (strlen(next) >= VALUE_BYTES) {
            s->why = WHY_TOO_MANY;
            return false;
        }
        SteamAPI_ISteamMatchmaking_SetLobbyMemberData(s->matchmaking, lobby, key, remove ? "" : value);
        SteamAPI_ISteamMatchmaking_SetLobbyMemberData(s->matchmaking, lobby, KEY_KEYS, next);
    }
    if (!ok) s->why = WHY_FAILED;
    else s->version++;
    return ok;
}

// The lobby a friend asked this player into, through Steam's overlay or an
// invite, once: "" with none.
const char *TideSteamInvited(void)
{
    static char id[24];
    id[0] = '\0';
    if (s->invited) id_text(s->invited, id);
    s->invited = 0;
    return id;
}

// Steam's own dialog to ask friends into this player's lobby.
void TideSteamInvite(void)
{
    if (s->ready && s->lobby) SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialog(s->friends, s->lobby);
}
