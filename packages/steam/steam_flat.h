// The part of Steamworks' flat C API this package calls, declared here: the
// SDK isn't in this repository or in tide (its license doesn't let it be
// passed on), and a game's C has no C++ to include its headers with.
//
// Written from Steamworks SDK 1.60's public/steam/steam_api_flat.h,
// steam_api.h, isteammatchmaking.h, isteamfriends.h, isteamuser.h,
// isteamnetworkingmessages.h and steamnetworkingtypes.h, and the callbacks'
// numbers from their headers. The functions are exported by the SDK's
// redistributable library (steam_api64.dll, libsteam_api.so,
// libsteam_api.dylib), which a game puts in its folder (README.md).
//
// Another SDK version: an interface's accessor carries its version in its
// name (SteamAPI_SteamUser_v023), and a newer SDK may count further. Every
// such name is in this file, so that's where a newer SDK's are written. The
// structs here are the layouts the SDK has on 64-bit Windows, Linux and
// macOS: its packing differs between them (8 and 4), and each field below
// lands at the same offset under both.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef uint64_t SteamAPICall_t;
typedef int32_t HSteamPipe;
typedef int32_t HSteamUser;
typedef char SteamErrMsg[1024];

// Interfaces are C++ objects the flat functions take as their first argument.
typedef struct ISteamUser ISteamUser;
typedef struct ISteamFriends ISteamFriends;
typedef struct ISteamMatchmaking ISteamMatchmaking;
typedef struct ISteamNetworkingMessages ISteamNetworkingMessages;

enum {
    k_ESteamAPIInitResult_OK = 0,
    k_EResultOK = 1,
    k_ELobbyTypePrivate = 0, // Joined by its ID or an invite; no list has it
    k_ELobbyTypePublic = 2,  // Lists have it
    k_ELobbyComparisonEqual = 0,
    k_ELobbyDistanceFilterWorldwide = 3,
    k_EChatRoomEnterResponseSuccess = 1,
    k_EChatRoomEnterResponseDoesntExist = 2,
    k_EChatRoomEnterResponseNotAllowed = 3,
    k_EChatRoomEnterResponseFull = 4,
    k_EChatRoomEnterResponseBanned = 6,
    // Datagrams: unreliable, sent at once, and a session that broke starts again with the next one
    k_nSteamNetworkingSend_UnreliableNoNagle = 1,
    k_nSteamNetworkingSend_AutoRestartBrokenSession = 32,
};

// What ISteamNetworkingMessages names a peer by: 136 bytes, of which this
// package only ever uses a Steam ID, through the SDK's own functions below.
typedef struct SteamNetworkingIdentity {
    int32_t m_eType;
    int32_t m_cbSize;
    union {
        uint64_t m_steamID64;
        char m_szUnknownRawString[128];
    } m_id;
} SteamNetworkingIdentity;
_Static_assert(sizeof(SteamNetworkingIdentity) == 136, "SteamNetworkingIdentity is 136 bytes in the SDK");

// A message as ReceiveMessagesOnChannel hands it over: Steam's memory, given
// back with SteamAPI_SteamNetworkingMessage_t_Release.
typedef struct SteamNetworkingMessage_t {
    void *m_pData;
    int32_t m_cbSize;
    uint32_t m_conn;
    SteamNetworkingIdentity m_identityPeer;
    int64_t m_nConnUserData;
    int64_t m_usecTimeReceived;
    int64_t m_nMessageNumber;
    void (*m_pfnFreeData)(struct SteamNetworkingMessage_t *);
    void (*m_pfnRelease)(struct SteamNetworkingMessage_t *);
    int32_t m_nChannel;
    int32_t m_nFlags;
    int64_t m_nUserData;
    uint16_t m_idxLane;
    uint16_t pad;
} SteamNetworkingMessage_t;

// Callbacks, as SteamAPI_ManualDispatch_GetNextCallback gives them.
typedef struct CallbackMsg_t {
    HSteamUser m_hSteamUser;
    int32_t m_iCallback;
    uint8_t *m_pubParam;
    int32_t m_cubParam;
} CallbackMsg_t;

enum {
    k_iGameLobbyJoinRequested = 333,
    k_iLobbyEnter = 504,
    k_iLobbyDataUpdate = 505,
    k_iLobbyChatUpdate = 506,
    k_iLobbyMatchList = 510,
    k_iLobbyCreated = 513,
    k_iSteamAPICallCompleted = 703,
    k_iSteamNetworkingMessagesSessionRequest = 1251,
    k_iSteamNetworkingMessagesSessionFailed = 1252,
};

// Each is read from a callback's bytes field by field (bridge.c), at the
// offsets these have, which are the SDK's under both of its packings but
// where a comment says otherwise.
typedef struct SteamAPICallCompleted_t {
    SteamAPICall_t m_hAsyncCall;
    int32_t m_iCallback;
    uint32_t m_cubParam;
} SteamAPICallCompleted_t;

typedef struct LobbyMatchList_t {
    uint32_t m_nLobbiesMatching;
} LobbyMatchList_t;

// m_ulSteamIDLobby is at offset 8 on Windows and 4 on Linux and macOS, where
// the SDK packs to 4 bytes: read with steam_lobby_created (bridge.c).
typedef struct LobbyCreated_t {
    int32_t m_eResult;
    uint64_t m_ulSteamIDLobby;
} LobbyCreated_t;

typedef struct LobbyEnter_t {
    uint64_t m_ulSteamIDLobby;
    uint32_t m_rgfChatPermissions;
    bool m_bLocked;
    uint32_t m_EChatRoomEnterResponse;
} LobbyEnter_t;

typedef struct LobbyDataUpdate_t {
    uint64_t m_ulSteamIDLobby;
    uint64_t m_ulSteamIDMember;
    uint8_t m_bSuccess;
} LobbyDataUpdate_t;

typedef struct LobbyChatUpdate_t {
    uint64_t m_ulSteamIDLobby;
    uint64_t m_ulSteamIDUserChanged;
    uint64_t m_ulSteamIDMakingChange;
    uint32_t m_rgfChatMemberStateChange;
} LobbyChatUpdate_t;

typedef struct GameLobbyJoinRequested_t {
    uint64_t m_steamIDLobby;
    uint64_t m_steamIDFriend;
} GameLobbyJoinRequested_t;

typedef struct SteamNetworkingMessagesSessionRequest_t {
    SteamNetworkingIdentity m_identityRemote;
} SteamNetworkingMessagesSessionRequest_t;

// Starting, and running callbacks by hand, which is how C gets them.
int32_t SteamAPI_InitFlat(SteamErrMsg *pOutErrMsg);
void SteamAPI_Shutdown(void);
void SteamAPI_ManualDispatch_Init(void);
HSteamPipe SteamAPI_GetHSteamPipe(void);
void SteamAPI_ManualDispatch_RunFrame(HSteamPipe hSteamPipe);
bool SteamAPI_ManualDispatch_GetNextCallback(HSteamPipe hSteamPipe, CallbackMsg_t *pCallbackMsg);
void SteamAPI_ManualDispatch_FreeLastCallback(HSteamPipe hSteamPipe);
bool SteamAPI_ManualDispatch_GetAPICallResult(HSteamPipe hSteamPipe, SteamAPICall_t hSteamAPICall, void *pCallback,
                                              int cubCallback, int iCallbackExpected, bool *pbFailed);

ISteamUser *SteamAPI_SteamUser_v023(void);
ISteamFriends *SteamAPI_SteamFriends_v017(void);
ISteamMatchmaking *SteamAPI_SteamMatchmaking_v009(void);
ISteamNetworkingMessages *SteamAPI_SteamNetworkingMessages_SteamAPI_v002(void);

uint64_t SteamAPI_ISteamUser_GetSteamID(ISteamUser *self);
const char *SteamAPI_ISteamFriends_GetPersonaName(ISteamFriends *self);
const char *SteamAPI_ISteamFriends_GetFriendPersonaName(ISteamFriends *self, uint64_t steamIDFriend);
void SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialog(ISteamFriends *self, uint64_t steamIDLobby);

SteamAPICall_t SteamAPI_ISteamMatchmaking_RequestLobbyList(ISteamMatchmaking *self);
void SteamAPI_ISteamMatchmaking_AddRequestLobbyListStringFilter(ISteamMatchmaking *self, const char *pchKeyToMatch,
                                                                const char *pchValueToMatch, int32_t eComparisonType);
void SteamAPI_ISteamMatchmaking_AddRequestLobbyListFilterSlotsAvailable(ISteamMatchmaking *self, int nSlotsAvailable);
void SteamAPI_ISteamMatchmaking_AddRequestLobbyListDistanceFilter(ISteamMatchmaking *self, int32_t eLobbyDistanceFilter);
void SteamAPI_ISteamMatchmaking_AddRequestLobbyListResultCountFilter(ISteamMatchmaking *self, int cMaxResults);
uint64_t SteamAPI_ISteamMatchmaking_GetLobbyByIndex(ISteamMatchmaking *self, int iLobby);
SteamAPICall_t SteamAPI_ISteamMatchmaking_CreateLobby(ISteamMatchmaking *self, int32_t eLobbyType, int cMaxMembers);
SteamAPICall_t SteamAPI_ISteamMatchmaking_JoinLobby(ISteamMatchmaking *self, uint64_t steamIDLobby);
void SteamAPI_ISteamMatchmaking_LeaveLobby(ISteamMatchmaking *self, uint64_t steamIDLobby);
int SteamAPI_ISteamMatchmaking_GetNumLobbyMembers(ISteamMatchmaking *self, uint64_t steamIDLobby);
uint64_t SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex(ISteamMatchmaking *self, uint64_t steamIDLobby, int iMember);
const char *SteamAPI_ISteamMatchmaking_GetLobbyData(ISteamMatchmaking *self, uint64_t steamIDLobby, const char *pchKey);
bool SteamAPI_ISteamMatchmaking_SetLobbyData(ISteamMatchmaking *self, uint64_t steamIDLobby, const char *pchKey,
                                             const char *pchValue);
int SteamAPI_ISteamMatchmaking_GetLobbyDataCount(ISteamMatchmaking *self, uint64_t steamIDLobby);
bool SteamAPI_ISteamMatchmaking_GetLobbyDataByIndex(ISteamMatchmaking *self, uint64_t steamIDLobby, int iLobbyData,
                                                    char *pchKey, int cchKeyBufferSize, char *pchValue,
                                                    int cchValueBufferSize);
bool SteamAPI_ISteamMatchmaking_DeleteLobbyData(ISteamMatchmaking *self, uint64_t steamIDLobby, const char *pchKey);
const char *SteamAPI_ISteamMatchmaking_GetLobbyMemberData(ISteamMatchmaking *self, uint64_t steamIDLobby,
                                                          uint64_t steamIDUser, const char *pchKey);
void SteamAPI_ISteamMatchmaking_SetLobbyMemberData(ISteamMatchmaking *self, uint64_t steamIDLobby, const char *pchKey,
                                                   const char *pchValue);
bool SteamAPI_ISteamMatchmaking_SetLobbyMemberLimit(ISteamMatchmaking *self, uint64_t steamIDLobby, int cMaxMembers);
int SteamAPI_ISteamMatchmaking_GetLobbyMemberLimit(ISteamMatchmaking *self, uint64_t steamIDLobby);
bool SteamAPI_ISteamMatchmaking_SetLobbyType(ISteamMatchmaking *self, uint64_t steamIDLobby, int32_t eLobbyType);
bool SteamAPI_ISteamMatchmaking_SetLobbyJoinable(ISteamMatchmaking *self, uint64_t steamIDLobby, bool bLobbyJoinable);
uint64_t SteamAPI_ISteamMatchmaking_GetLobbyOwner(ISteamMatchmaking *self, uint64_t steamIDLobby);
bool SteamAPI_ISteamMatchmaking_SetLobbyOwner(ISteamMatchmaking *self, uint64_t steamIDLobby, uint64_t steamIDNewOwner);

int32_t SteamAPI_ISteamNetworkingMessages_SendMessageToUser(ISteamNetworkingMessages *self,
                                                            const SteamNetworkingIdentity *identityRemote,
                                                            const void *pubData, uint32_t cubData, int nSendFlags,
                                                            int nRemoteChannel);
int SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel(ISteamNetworkingMessages *self, int nLocalChannel,
                                                               SteamNetworkingMessage_t **ppOutMessages,
                                                               int nMaxMessages);
bool SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser(ISteamNetworkingMessages *self,
                                                             const SteamNetworkingIdentity *identityRemote);
bool SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser(ISteamNetworkingMessages *self,
                                                            const SteamNetworkingIdentity *identityRemote);
void SteamAPI_SteamNetworkingIdentity_Clear(SteamNetworkingIdentity *self);
void SteamAPI_SteamNetworkingIdentity_SetSteamID64(SteamNetworkingIdentity *self, uint64_t steamID);
uint64_t SteamAPI_SteamNetworkingIdentity_GetSteamID64(SteamNetworkingIdentity *self);
void SteamAPI_SteamNetworkingMessage_t_Release(SteamNetworkingMessage_t *self);
