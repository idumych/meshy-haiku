// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// BMessage 'what' codes shared by the Haiku UI.  Codes in the kEngine* block
// are posted from the engine's worker thread by EngineBridge; everything else
// is UI-local.

#ifndef MESHY_HAIKU_UIMESSAGES_H
#define MESHY_HAIKU_UIMESSAGES_H

#include <SupportDefs.h>

// ─── Engine -> UI (posted from the engine worker thread) ─────────

enum {
    kEngineConnectionState = 'eCon',  // int32 "state"
    kEngineSyncProgress = 'eSyn',     // bool "active", int32 "done"/"total", bool flags
    kEngineToast = 'eTst',            // string "text"
    kEngineError = 'eErr',            // string "text"
    kEngineDeviceInfo = 'eDev',
    kEngineContactsChanged = 'eCts',
    kEngineChannelsChanged = 'eChs',
    kEngineDiscoveredChanged = 'eDis',
    kEngineMessage = 'eMsg',          // string "contact", int64 "id"
    kEngineMessageStatus = 'eMSt',    // int64 "id", int32 "status"
    kEngineChannelMessage = 'eCMs',   // int32 "channel", int64 "id"
    kEngineCliReply = 'eCli',         // string "text"
    kEngineRemoteCliReply = 'eRCl',   // string "contact", string "text"
    kEngineLoginResult = 'eLgn',      // string "contact", bool "success"
    kEngineStatusResponse = 'eSta',   // string "contact", plus status fields
    kEngineTelemetry = 'eTel',        // string "contact", data "lpp"
    kEngineTraceResult = 'eTrc',
    kEngineStats = 'eStt',
    kEngineBattery = 'eBat',          // int32 "mv", "used_kb", "total_kb"
    kEngineRawPacket = 'ePkt',
};

// ─── Navigation ──────────────────────────────────────────────────

enum {
    kNavSelected = 'nSel',
    kShowChat = 'nCht',     // string "contact"
    kShowRepeater = 'nRep',  // string "contact"
    kShowSensor = 'nSen',    // string "contact"
    kShowChannel = 'nChn',   // int32 "channel"
};

// Indices into the main card layout; the nav list uses the same order.
enum NavPage {
    kPageConnection = 0,
    kPageContacts,
    kPageChat,
    kPageChannels,
    kPageMap,
    kPageDevice,
    kPageRepeater,
    kPageSensor,
    kPagePackets,
    kPageSettings,
    kPageCount,
};

// ─── Connection view ─────────────────────────────────────────────

enum {
    kMsgRefreshPorts = 'cRef',
    kMsgConnect = 'cCon',
    kMsgDisconnect = 'cDis',
    kMsgTransportChanged = 'cTrn',
};

// ─── Contacts ────────────────────────────────────────────────────

enum {
    kMsgContactSelected = 'ctSe',
    kMsgContactInvoked = 'ctIn',
    kMsgContactRemove = 'ctRm',
    kMsgContactResetPath = 'ctRp',
    kMsgContactShare = 'ctSh',
    kMsgContactFavorite = 'ctFv',
    kMsgContactRename = 'ctRn',
    kMsgContactPathDiscovery = 'ctPd',
    kMsgContactTelemetry = 'ctTe',
    kMsgSendAdvert = 'ctAd',
    kMsgSendAdvertFlood = 'ctAF',
    kMsgDiscoverNodes = 'ctDn',
    kMsgImportDiscovered = 'ctId',
    kMsgRefreshContacts = 'ctRf',
    kMsgShowDiscovered = 'ctSd',
};

// ─── Chat ────────────────────────────────────────────────────────

enum {
    kMsgSendMessage = 'chSn',
    kMsgChatInputChanged = 'chIn',
};

// ─── Channels ────────────────────────────────────────────────────

enum {
    kMsgChannelSelected = 'cnSe',
    kMsgChannelSend = 'cnSn',
    kMsgChannelAdd = 'cnAd',
    kMsgChannelEdit = 'cnEd',
    kMsgChannelRemove = 'cnRm',
    kMsgChannelSave = 'cnSv',
};

// ─── Device and settings ─────────────────────────────────────────

enum {
    kMsgDeviceRefresh = 'dvRf',
    kMsgDeviceReboot = 'dvRb',
    kMsgDeviceStats = 'dvSt',
    kMsgApplyRadio = 'stRa',
    kMsgApplyName = 'stNm',
    kMsgApplyTxPower = 'stTx',
    kMsgApplyLocation = 'stLo',
    kMsgApplyAutoAdd = 'stAa',
    kMsgSyncTime = 'stTi',
};

// ─── Repeater / sensor ───────────────────────────────────────────

enum {
    kMsgRepeaterLogin = 'rpLi',
    kMsgRepeaterLogout = 'rpLo',
    kMsgRepeaterCli = 'rpCl',
    kMsgRepeaterStatus = 'rpSt',
    kMsgRepeaterNeighbors = 'rpNb',
    kMsgSensorRefresh = 'snRf',
};

// ─── Packet log ──────────────────────────────────────────────────

enum {
    kMsgPacketsClear = 'pkCl',
    kMsgPacketsPause = 'pkPa',
};

#endif  // MESHY_HAIKU_UIMESSAGES_H
