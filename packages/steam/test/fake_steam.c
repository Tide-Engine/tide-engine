// A Steam that isn't: the functions of Steamworks' flat API that the package
// calls (../steam_flat.h), for several users in one program, in the SDK's
// library's place. It's the tests' alone, and no part of what a game gets.
//
// It keeps what Steam keeps for a game: who the users are, lobbies with
// their data, their members' data and their owner, which passes to the
// member there longest when the owner leaves, lists by string filters, and
// messages between users. Calls Steam answers later are answered two of the
// asking user's frames later, as call results through the callbacks' queue,
// and changes to a lobby reach its members as Steam's callbacks.
//
// What it isn't: Steam. It checks none of what Valve's servers do, has no
// overlay, friends or relays, and its lobbies' rules are what the SDK's
// documentation says, as read here.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "steam_flat.h"

#define USERS 4
#define LOBBIES 8
#define PAIRS 24
#define CALLBACKS 64
#define CALLS 16
#define INBOX 128
#define FOUND 16
#define FILTERS 8

#define FIRST_USER 76561197960265728ull // An individual's account, of the public universe: account 0
#define FIRST_LOBBY 109775240917155840ull // A lobby's ID has another kind in its high half

typedef struct pair {
    char key[256];
    char value[512];
} pair;

typedef struct member {
    int user;
    pair data[PAIRS];
    int data_count;
} member;

typedef struct lobby {
    uint64_t id; // 0: none
    int owner;
    int type;
    bool joinable;
    int limit;
    pair data[PAIRS];
    int data_count;
    member members[USERS]; // In the order they joined
    int count;
} lobby;

typedef struct callback {
    int kind;
    uint8_t bytes[160];
    int size;
} callback;

typedef struct api_call {
    SteamAPICall_t handle; // 0: none
    int kind;
    uint8_t bytes[32];
    int size;
    int frames; // Frames of its user's until it's answered
} api_call;

typedef struct datagram {
    int from;
    uint32_t size;
    uint8_t data[1400];
} datagram;

typedef struct user {
    callback callbacks[CALLBACKS];
    uint32_t first, count;
    bool taken; // A callback was handed over and isn't freed yet
    api_call calls[CALLS];
    datagram inbox[INBOX];
    uint32_t inbox_first, inbox_count;
    bool accepted[USERS + 1];  // Whose messages it takes
    bool asked[USERS + 1];     // ...and who it was already told wants to send
    uint64_t found[FOUND];     // The last list
    int found_count;
    pair filters[FILTERS];
    int filter_count;
    int slots;
} user;

static user users[USERS + 1];
static lobby lobbies[LOBBIES];
static int me = 1;
static uint64_t next_lobby = FIRST_LOBBY + 1;
static SteamAPICall_t next_call = 1;
static bool deaf[USERS + 1];
static int started;

// The third user's name is one a JSON writer has to mind.
static const char *const names[USERS + 1] = {"", "Ann", "Bob", "Cat \"C\\\" Ray", "Dan"};

// ---------------------------------------------------------------------------
// The test's own

// Whose turn it is, from 1: every call from here on is this user's program.
void FakeSteamUser(const int32_t number)
{
    me = number;
}

void FakeSteamReset(void)
{
    memset(users, 0, sizeof users);
    memset(lobbies, 0, sizeof lobbies);
    memset(deaf, 0, sizeof deaf);
    me = 1;
    next_lobby = FIRST_LOBBY + 1;
    next_call = 1;
    started = 0;
}

// A user nothing reaches from now on, messages or what becomes of its
// lobbies, as on a network that lost it, or one that's reached again.
void FakeSteamDeafen(const int32_t number, const bool on)
{
    deaf[number] = on;
}

// How often the package started Steam.
int32_t FakeSteamStarts(void)
{
    return started;
}

// A lobby's key, as Steam has it, whoever asks: "" for none.
const char *FakeSteamData(const char *key)
{
    for (int i = 0; i < LOBBIES; i++) {
        if (!lobbies[i].id) continue;
        for (int k = 0; k < lobbies[i].data_count; k++) {
            if (strcmp(lobbies[i].data[k].key, key) == 0) return lobbies[i].data[k].value;
        }
    }
    return "";
}

// A lobby member who isn't a player of the test sets a key of the lobby's
// data, as a program that isn't the package might: any bytes at all.
void FakeSteamForge(const char *key, const char *value);

// ---------------------------------------------------------------------------

static uint64_t id_of(const int number)
{
    return FIRST_USER + (uint64_t)number;
}

static int user_of(const uint64_t id)
{
    return id > FIRST_USER && id <= FIRST_USER + USERS ? (int)(id - FIRST_USER) : 0;
}

static lobby *lobby_of(const uint64_t id)
{
    for (int i = 0; i < LOBBIES; i++) {
        if (id && lobbies[i].id == id) return &lobbies[i];
    }
    return NULL;
}

static member *member_of(lobby *l, const int number)
{
    for (int i = 0; l && i < l->count; i++) {
        if (l->members[i].user == number) return &l->members[i];
    }
    return NULL;
}

static void set_pair(pair *pairs, int *count, const char *key, const char *value)
{
    int at = 0;
    while (at < *count && strcmp(pairs[at].key, key) != 0) at++;
    if (!value) {
        for (int i = at; i + 1 < *count; i++) pairs[i] = pairs[i + 1];
        if (at < *count) --*count;
        return;
    }
    if (at == PAIRS) return;
    if (at == *count) ++*count;
    snprintf(pairs[at].key, sizeof pairs[at].key, "%s", key);
    snprintf(pairs[at].value, sizeof pairs[at].value, "%s", value);
}

static const char *pair_of(const pair *pairs, const int count, const char *key)
{
    for (int i = 0; i < count; i++) {
        if (strcmp(pairs[i].key, key) == 0) return pairs[i].value;
    }
    return "";
}

static void post(const int to, const int kind, const void *bytes, const int size)
{
    user *u = &users[to];
    if (deaf[to] || u->count == CALLBACKS) return;
    callback *c = &u->callbacks[(u->first + u->count++) % CALLBACKS];
    c->kind = kind;
    c->size = size;
    memset(c->bytes, 0, sizeof c->bytes);
    memcpy(c->bytes, bytes, (size_t)size);
}

// Every member hears that the lobby's data changed, or who's in it.
static void tell_data(const lobby *l)
{
    const LobbyDataUpdate_t update = {.m_ulSteamIDLobby = l->id, .m_ulSteamIDMember = l->id, .m_bSuccess = 1};
    for (int i = 0; i < l->count; i++) post(l->members[i].user, k_iLobbyDataUpdate, &update, (int)sizeof update);
}

static void tell_members(const lobby *l, const int changed)
{
    const LobbyChatUpdate_t update = {.m_ulSteamIDLobby = l->id, .m_ulSteamIDUserChanged = id_of(changed),
                                      .m_ulSteamIDMakingChange = id_of(changed), .m_rgfChatMemberStateChange = 1};
    for (int i = 0; i < l->count; i++) post(l->members[i].user, k_iLobbyChatUpdate, &update, (int)sizeof update);
}

static SteamAPICall_t answer_later(const int kind, const void *bytes, const int size)
{
    user *u = &users[me];
    for (int i = 0; i < CALLS; i++) {
        api_call *c = &u->calls[i];
        if (c->handle) continue;
        c->handle = next_call++;
        c->kind = kind;
        c->size = size;
        memcpy(c->bytes, bytes, (size_t)size);
        c->frames = 2;
        return c->handle;
    }
    return 0;
}

// LobbyCreated_t as the SDK lays it out for this platform (steam_flat.h).
static SteamAPICall_t created_later(const int32_t result, const uint64_t id)
{
    uint8_t bytes[16] = {0};
    memcpy(bytes, &result, sizeof result);
#ifdef _WIN32
    memcpy(bytes + 8, &id, sizeof id);
    return answer_later(k_iLobbyCreated, bytes, 16);
#else
    memcpy(bytes + 4, &id, sizeof id);
    return answer_later(k_iLobbyCreated, bytes, 12);
#endif
}

static void leave(lobby *l, const int number)
{
    member *m = member_of(l, number);
    if (!m) return;
    const int at = (int)(m - l->members);
    for (int i = at; i + 1 < l->count; i++) l->members[i] = l->members[i + 1];
    l->count--;
    if (!l->count) {
        *l = (lobby){0};
        return;
    }
    // Steam hands the lobby on when its owner goes: here, to the member there longest
    if (l->owner == number) l->owner = l->members[0].user;
    tell_members(l, number);
    tell_data(l);
}

// ---------------------------------------------------------------------------
// steam_flat.h

static int some_interface;

int32_t SteamAPI_InitFlat(SteamErrMsg *pOutErrMsg)
{
    (void)pOutErrMsg;
    started++;
    return k_ESteamAPIInitResult_OK;
}

void SteamAPI_Shutdown(void)
{
}

void SteamAPI_ManualDispatch_Init(void)
{
}

HSteamPipe SteamAPI_GetHSteamPipe(void)
{
    return me;
}

// A frame of the user's: its calls are a frame nearer their answers.
void SteamAPI_ManualDispatch_RunFrame(const HSteamPipe hSteamPipe)
{
    user *u = &users[hSteamPipe];
    for (int i = 0; i < CALLS; i++) {
        api_call *c = &u->calls[i];
        if (!c->handle || c->frames == 0 || --c->frames > 0) continue;
        const SteamAPICallCompleted_t completed = {.m_hAsyncCall = c->handle, .m_iCallback = c->kind,
                                                   .m_cubParam = (uint32_t)c->size};
        // Its own answers reach a user that nothing else does
        const bool was = deaf[hSteamPipe];
        deaf[hSteamPipe] = false;
        post(hSteamPipe, k_iSteamAPICallCompleted, &completed, (int)sizeof completed);
        deaf[hSteamPipe] = was;
    }
}

bool SteamAPI_ManualDispatch_GetNextCallback(const HSteamPipe hSteamPipe, CallbackMsg_t *pCallbackMsg)
{
    user *u = &users[hSteamPipe];
    if (!u->count || u->taken) return false;
    callback *c = &u->callbacks[u->first];
    *pCallbackMsg = (CallbackMsg_t){.m_hSteamUser = hSteamPipe, .m_iCallback = c->kind, .m_pubParam = c->bytes,
                                    .m_cubParam = c->size};
    u->taken = true;
    return true;
}

void SteamAPI_ManualDispatch_FreeLastCallback(const HSteamPipe hSteamPipe)
{
    user *u = &users[hSteamPipe];
    if (!u->taken) return;
    u->taken = false;
    u->first = (u->first + 1u) % CALLBACKS;
    u->count--;
}

bool SteamAPI_ManualDispatch_GetAPICallResult(const HSteamPipe hSteamPipe, const SteamAPICall_t hSteamAPICall,
                                              void *pCallback, const int cubCallback, const int iCallbackExpected,
                                              bool *pbFailed)
{
    user *u = &users[hSteamPipe];
    for (int i = 0; i < CALLS; i++) {
        api_call *c = &u->calls[i];
        if (c->handle != hSteamAPICall || !hSteamAPICall) continue;
        // As Steam: the size and the kind have to be the answer's own
        if (c->kind != iCallbackExpected || c->size != cubCallback) return false;
        memcpy(pCallback, c->bytes, (size_t)c->size);
        *pbFailed = false;
        c->handle = 0;
        return true;
    }
    return false;
}

ISteamUser *SteamAPI_SteamUser_v023(void)
{
    return (ISteamUser *)&some_interface;
}

ISteamFriends *SteamAPI_SteamFriends_v017(void)
{
    return (ISteamFriends *)&some_interface;
}

ISteamMatchmaking *SteamAPI_SteamMatchmaking_v009(void)
{
    return (ISteamMatchmaking *)&some_interface;
}

ISteamNetworkingMessages *SteamAPI_SteamNetworkingMessages_SteamAPI_v002(void)
{
    return (ISteamNetworkingMessages *)&some_interface;
}

uint64_t SteamAPI_ISteamUser_GetSteamID(ISteamUser *self)
{
    (void)self;
    return id_of(me);
}

const char *SteamAPI_ISteamFriends_GetPersonaName(ISteamFriends *self)
{
    (void)self;
    return names[me];
}

const char *SteamAPI_ISteamFriends_GetFriendPersonaName(ISteamFriends *self, const uint64_t steamIDFriend)
{
    (void)self;
    const int number = user_of(steamIDFriend);
    return names[number];
}

void SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialog(ISteamFriends *self, const uint64_t steamIDLobby)
{
    (void)self;
    (void)steamIDLobby;
}

void SteamAPI_ISteamMatchmaking_AddRequestLobbyListStringFilter(ISteamMatchmaking *self, const char *pchKeyToMatch,
                                                                const char *pchValueToMatch, const int32_t eComparisonType)
{
    (void)self;
    (void)eComparisonType;
    user *u = &users[me];
    if (u->filter_count == FILTERS) return;
    snprintf(u->filters[u->filter_count].key, sizeof u->filters[0].key, "%s", pchKeyToMatch);
    snprintf(u->filters[u->filter_count].value, sizeof u->filters[0].value, "%s", pchValueToMatch);
    u->filter_count++;
}

void SteamAPI_ISteamMatchmaking_AddRequestLobbyListFilterSlotsAvailable(ISteamMatchmaking *self, const int nSlotsAvailable)
{
    (void)self;
    users[me].slots = nSlotsAvailable;
}

void SteamAPI_ISteamMatchmaking_AddRequestLobbyListDistanceFilter(ISteamMatchmaking *self, const int32_t eLobbyDistanceFilter)
{
    (void)self;
    (void)eLobbyDistanceFilter;
}

void SteamAPI_ISteamMatchmaking_AddRequestLobbyListResultCountFilter(ISteamMatchmaking *self, const int cMaxResults)
{
    (void)self;
    (void)cMaxResults;
}

// The public, joinable lobbies whose data has every filter's key as it says, with the room asked for.
SteamAPICall_t SteamAPI_ISteamMatchmaking_RequestLobbyList(ISteamMatchmaking *self)
{
    (void)self;
    user *u = &users[me];
    u->found_count = 0;
    for (int i = 0; i < LOBBIES && u->found_count < FOUND; i++) {
        const lobby *l = &lobbies[i];
        if (!l->id || l->type != k_ELobbyTypePublic || !l->joinable || l->limit - l->count < u->slots) continue;
        bool matches = true;
        for (int f = 0; f < u->filter_count; f++) {
            if (strcmp(pair_of(l->data, l->data_count, u->filters[f].key), u->filters[f].value) != 0) matches = false;
        }
        if (matches) u->found[u->found_count++] = l->id;
    }
    u->filter_count = 0;
    u->slots = 0;
    const LobbyMatchList_t list = {.m_nLobbiesMatching = (uint32_t)u->found_count};
    return answer_later(k_iLobbyMatchList, &list, (int)sizeof list);
}

uint64_t SteamAPI_ISteamMatchmaking_GetLobbyByIndex(ISteamMatchmaking *self, const int iLobby)
{
    (void)self;
    return iLobby >= 0 && iLobby < users[me].found_count ? users[me].found[iLobby] : 0;
}

SteamAPICall_t SteamAPI_ISteamMatchmaking_CreateLobby(ISteamMatchmaking *self, const int32_t eLobbyType, const int cMaxMembers)
{
    (void)self;
    for (int i = 0; i < LOBBIES; i++) {
        lobby *l = &lobbies[i];
        if (l->id) continue;
        *l = (lobby){.id = next_lobby++, .owner = me, .type = eLobbyType, .joinable = true,
                     .limit = cMaxMembers < USERS ? cMaxMembers : USERS, .count = 1};
        l->members[0] = (member){.user = me};
        return created_later(k_EResultOK, l->id);
    }
    return created_later(2, 0); // k_EResultFail
}

SteamAPICall_t SteamAPI_ISteamMatchmaking_JoinLobby(ISteamMatchmaking *self, const uint64_t steamIDLobby)
{
    (void)self;
    lobby *l = lobby_of(steamIDLobby);
    LobbyEnter_t enter = {.m_ulSteamIDLobby = steamIDLobby, .m_EChatRoomEnterResponse = k_EChatRoomEnterResponseSuccess};
    if (!l) {
        enter.m_EChatRoomEnterResponse = k_EChatRoomEnterResponseDoesntExist;
    } else if (!member_of(l, me)) {
        if (!l->joinable) enter.m_EChatRoomEnterResponse = k_EChatRoomEnterResponseNotAllowed;
        else if (l->count >= l->limit) enter.m_EChatRoomEnterResponse = k_EChatRoomEnterResponseFull;
        else {
            l->members[l->count++] = (member){.user = me};
            tell_members(l, me);
        }
    }
    // The answer's size is the SDK's for the platform: 20 bytes where it packs to 4
#ifdef _WIN32
    return answer_later(k_iLobbyEnter, &enter, 24);
#else
    return answer_later(k_iLobbyEnter, &enter, 20);
#endif
}

void SteamAPI_ISteamMatchmaking_LeaveLobby(ISteamMatchmaking *self, const uint64_t steamIDLobby)
{
    (void)self;
    lobby *l = lobby_of(steamIDLobby);
    if (l) leave(l, me);
}

int SteamAPI_ISteamMatchmaking_GetNumLobbyMembers(ISteamMatchmaking *self, const uint64_t steamIDLobby)
{
    (void)self;
    const lobby *l = lobby_of(steamIDLobby);
    return l ? l->count : 0;
}

uint64_t SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex(ISteamMatchmaking *self, const uint64_t steamIDLobby,
                                                          const int iMember)
{
    (void)self;
    const lobby *l = lobby_of(steamIDLobby);
    return l && iMember >= 0 && iMember < l->count ? id_of(l->members[iMember].user) : 0;
}

const char *SteamAPI_ISteamMatchmaking_GetLobbyData(ISteamMatchmaking *self, const uint64_t steamIDLobby, const char *pchKey)
{
    (void)self;
    const lobby *l = lobby_of(steamIDLobby);
    return l ? pair_of(l->data, l->data_count, pchKey) : "";
}

bool SteamAPI_ISteamMatchmaking_SetLobbyData(ISteamMatchmaking *self, const uint64_t steamIDLobby, const char *pchKey,
                                             const char *pchValue)
{
    (void)self;
    lobby *l = lobby_of(steamIDLobby);
    if (!l || l->owner != me) return false; // Only its owner sets a lobby's data
    set_pair(l->data, &l->data_count, pchKey, pchValue);
    tell_data(l);
    return true;
}

int SteamAPI_ISteamMatchmaking_GetLobbyDataCount(ISteamMatchmaking *self, const uint64_t steamIDLobby)
{
    (void)self;
    const lobby *l = lobby_of(steamIDLobby);
    return l ? l->data_count : 0;
}

bool SteamAPI_ISteamMatchmaking_GetLobbyDataByIndex(ISteamMatchmaking *self, const uint64_t steamIDLobby,
                                                    const int iLobbyData, char *pchKey, const int cchKeyBufferSize,
                                                    char *pchValue, const int cchValueBufferSize)
{
    (void)self;
    const lobby *l = lobby_of(steamIDLobby);
    if (!l || iLobbyData < 0 || iLobbyData >= l->data_count) return false;
    snprintf(pchKey, (size_t)cchKeyBufferSize, "%s", l->data[iLobbyData].key);
    snprintf(pchValue, (size_t)cchValueBufferSize, "%s", l->data[iLobbyData].value);
    return true;
}

bool SteamAPI_ISteamMatchmaking_DeleteLobbyData(ISteamMatchmaking *self, const uint64_t steamIDLobby, const char *pchKey)
{
    (void)self;
    lobby *l = lobby_of(steamIDLobby);
    if (!l || l->owner != me) return false;
    set_pair(l->data, &l->data_count, pchKey, NULL);
    tell_data(l);
    return true;
}

const char *SteamAPI_ISteamMatchmaking_GetLobbyMemberData(ISteamMatchmaking *self, const uint64_t steamIDLobby,
                                                          const uint64_t steamIDUser, const char *pchKey)
{
    (void)self;
    lobby *l = lobby_of(steamIDLobby);
    const member *m = member_of(l, user_of(steamIDUser));
    return m ? pair_of(m->data, m->data_count, pchKey) : "";
}

void SteamAPI_ISteamMatchmaking_SetLobbyMemberData(ISteamMatchmaking *self, const uint64_t steamIDLobby, const char *pchKey,
                                                   const char *pchValue)
{
    (void)self;
    lobby *l = lobby_of(steamIDLobby);
    member *m = member_of(l, me);
    if (!m) return;
    set_pair(m->data, &m->data_count, pchKey, pchValue);
    tell_data(l);
}

bool SteamAPI_ISteamMatchmaking_SetLobbyMemberLimit(ISteamMatchmaking *self, const uint64_t steamIDLobby, const int cMaxMembers)
{
    (void)self;
    lobby *l = lobby_of(steamIDLobby);
    if (!l || l->owner != me) return false;
    l->limit = cMaxMembers;
    tell_data(l);
    return true;
}

int SteamAPI_ISteamMatchmaking_GetLobbyMemberLimit(ISteamMatchmaking *self, const uint64_t steamIDLobby)
{
    (void)self;
    const lobby *l = lobby_of(steamIDLobby);
    return l ? l->limit : 0;
}

bool SteamAPI_ISteamMatchmaking_SetLobbyType(ISteamMatchmaking *self, const uint64_t steamIDLobby, const int32_t eLobbyType)
{
    (void)self;
    lobby *l = lobby_of(steamIDLobby);
    if (!l || l->owner != me) return false;
    l->type = eLobbyType;
    return true;
}

bool SteamAPI_ISteamMatchmaking_SetLobbyJoinable(ISteamMatchmaking *self, const uint64_t steamIDLobby, const bool bLobbyJoinable)
{
    (void)self;
    lobby *l = lobby_of(steamIDLobby);
    if (!l || l->owner != me) return false;
    l->joinable = bLobbyJoinable;
    return true;
}

uint64_t SteamAPI_ISteamMatchmaking_GetLobbyOwner(ISteamMatchmaking *self, const uint64_t steamIDLobby)
{
    (void)self;
    lobby *l = lobby_of(steamIDLobby);
    // As Steam: only a member is told who owns a lobby
    return l && member_of(l, me) ? id_of(l->owner) : 0;
}

bool SteamAPI_ISteamMatchmaking_SetLobbyOwner(ISteamMatchmaking *self, const uint64_t steamIDLobby, const uint64_t steamIDNewOwner)
{
    (void)self;
    lobby *l = lobby_of(steamIDLobby);
    if (!l || l->owner != me || !member_of(l, user_of(steamIDNewOwner))) return false;
    l->owner = user_of(steamIDNewOwner);
    tell_data(l);
    return true;
}

void FakeSteamForge(const char *key, const char *value)
{
    for (int i = 0; i < LOBBIES; i++) {
        if (!lobbies[i].id) continue;
        set_pair(lobbies[i].data, &lobbies[i].data_count, key, value);
        tell_data(&lobbies[i]);
    }
}

// Messages. A user's first message to another asks that one for a session,
// and is kept until it's accepted: one that never is, never arrives.
int32_t SteamAPI_ISteamNetworkingMessages_SendMessageToUser(ISteamNetworkingMessages *self,
                                                            const SteamNetworkingIdentity *identityRemote,
                                                            const void *pubData, const uint32_t cubData, const int nSendFlags,
                                                            const int nRemoteChannel)
{
    (void)self;
    (void)nSendFlags;
    (void)nRemoteChannel;
    const int to = user_of(identityRemote->m_id.m_steamID64);
    if (!to || cubData > sizeof users[0].inbox[0].data) return 2;
    user *there = &users[to];
    users[me].accepted[to] = true; // Sending to someone takes what they send back
    if (deaf[to]) return k_EResultOK;
    if (!there->accepted[me] && !there->asked[me]) {
        SteamNetworkingMessagesSessionRequest_t request;
        memset(&request, 0, sizeof request);
        request.m_identityRemote.m_eType = 16; // k_ESteamNetworkingIdentityType_SteamID
        request.m_identityRemote.m_cbSize = 8;
        request.m_identityRemote.m_id.m_steamID64 = id_of(me);
        post(to, k_iSteamNetworkingMessagesSessionRequest, &request, (int)sizeof request);
        there->asked[me] = true;
    }
    if (there->inbox_count == INBOX) return k_EResultOK;
    datagram *d = &there->inbox[(there->inbox_first + there->inbox_count++) % INBOX];
    d->from = me;
    d->size = cubData;
    memcpy(d->data, pubData, cubData);
    return k_EResultOK;
}

static void release(SteamNetworkingMessage_t *message)
{
    free(message->m_pData);
    free(message);
}

int SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel(ISteamNetworkingMessages *self, const int nLocalChannel,
                                                               SteamNetworkingMessage_t **ppOutMessages,
                                                               const int nMaxMessages)
{
    (void)self;
    (void)nLocalChannel;
    user *u = &users[me];
    // The oldest message of a user whose session was accepted: the others wait in line behind none
    for (uint32_t k = 0; k < u->inbox_count && nMaxMessages > 0; k++) {
        const uint32_t at = (u->inbox_first + k) % INBOX;
        datagram *d = &u->inbox[at];
        if (!u->accepted[d->from]) continue;
        SteamNetworkingMessage_t *message = calloc(1, sizeof *message);
        void *data = malloc(d->size ? d->size : 1);
        if (!message || !data) {
            free(message);
            free(data);
            return 0;
        }
        memcpy(data, d->data, d->size);
        message->m_pData = data;
        message->m_cbSize = (int32_t)d->size;
        message->m_identityPeer.m_eType = 16;
        message->m_identityPeer.m_cbSize = 8;
        message->m_identityPeer.m_id.m_steamID64 = id_of(d->from);
        message->m_pfnRelease = release;
        // Out of the line, which closes up behind it
        for (uint32_t j = k; j + 1 < u->inbox_count; j++) {
            u->inbox[(u->inbox_first + j) % INBOX] = u->inbox[(u->inbox_first + j + 1) % INBOX];
        }
        u->inbox_count--;
        *ppOutMessages = message;
        return 1;
    }
    return 0;
}

bool SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser(ISteamNetworkingMessages *self,
                                                             const SteamNetworkingIdentity *identityRemote)
{
    (void)self;
    const int from = user_of(identityRemote->m_id.m_steamID64);
    if (!from) return false;
    users[me].accepted[from] = true;
    return true;
}

bool SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser(ISteamNetworkingMessages *self,
                                                            const SteamNetworkingIdentity *identityRemote)
{
    (void)self;
    const int with = user_of(identityRemote->m_id.m_steamID64);
    if (!with) return false;
    users[me].accepted[with] = false;
    users[me].asked[with] = false;
    // What was on its way from them is gone with the session
    user *u = &users[me];
    uint32_t kept = 0;
    for (uint32_t k = 0; k < u->inbox_count; k++) {
        const datagram d = u->inbox[(u->inbox_first + k) % INBOX];
        if (d.from != with) u->inbox[(u->inbox_first + kept++) % INBOX] = d;
    }
    u->inbox_count = kept;
    return true;
}

void SteamAPI_SteamNetworkingIdentity_Clear(SteamNetworkingIdentity *self)
{
    memset(self, 0, sizeof *self);
}

void SteamAPI_SteamNetworkingIdentity_SetSteamID64(SteamNetworkingIdentity *self, const uint64_t steamID)
{
    self->m_eType = 16;
    self->m_cbSize = 8;
    self->m_id.m_steamID64 = steamID;
}

uint64_t SteamAPI_SteamNetworkingIdentity_GetSteamID64(SteamNetworkingIdentity *self)
{
    return self->m_eType == 16 ? self->m_id.m_steamID64 : 0;
}

void SteamAPI_SteamNetworkingMessage_t_Release(SteamNetworkingMessage_t *self)
{
    self->m_pfnRelease(self);
}
