// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Engine.h"

#include "Crypto.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>

namespace meshy {

// Timing, matching upstream's controllers.
static const int64_t kSyncWatchdogMs = 15000;   // stalled contact/message sync
static const int64_t kSendTimeoutMs = 8000;     // default wait for an ack
static const int kMaxSendAttempts = 3;
static const int64_t kRetryBackoffMs = 2000;
static const int kMaxMsgSyncRetries = 3;

static int64_t _NowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

static double _NowUnix()
{
    return static_cast<double>(time(NULL));
}

Engine::Engine()
    : fActive(NULL), fListener(NULL), fRunning(false), fNextTimerHandle(1)
{
    fSerial.SetListener(this);
    fTcp.SetListener(this);
}

Engine::~Engine()
{
    Stop();
    fSerial.SetListener(NULL);
    fTcp.SetListener(NULL);
}

bool Engine::Start(const std::string& db_path)
{
    if (!fStore.Open(db_path))
        return false;
    fRunning = true;
    fThread = std::thread(&Engine::_WorkerLoop, this);
    return true;
}

void Engine::Stop()
{
    if (!fRunning)
        return;
    // Drop the link first so the reader thread stops posting work.
    fSerial.Disconnect();
    fTcp.Disconnect();
    {
        std::lock_guard<std::mutex> g(fQueueLock);
        fRunning = false;
        fQueueCv.notify_all();
    }
    if (fThread.joinable())
        fThread.join();
    fStore.Close();
}

void Engine::SetListener(EngineListener* l)
{
    std::lock_guard<std::mutex> g(fListenerLock);
    fListener = l;
}

EngineListener* Engine::_Listener() const
{
    std::lock_guard<std::mutex> g(fListenerLock);
    return fListener;
}

// ─── Worker thread ───────────────────────────────────────────────

void Engine::_Post(const Task& t)
{
    std::lock_guard<std::mutex> g(fQueueLock);
    if (!fRunning)
        return;
    fQueue.push_back(t);
    fQueueCv.notify_all();
}

int64_t Engine::_PostDelayed(int64_t delay_ms, const Task& t)
{
    std::lock_guard<std::mutex> g(fQueueLock);
    if (!fRunning)
        return 0;
    TimerEntry e;
    e.due_ms = _NowMs() + delay_ms;
    e.handle = fNextTimerHandle++;
    e.task = t;
    fTimers.push_back(e);
    fQueueCv.notify_all();
    return e.handle;
}

void Engine::_Cancel(int64_t handle)
{
    if (handle == 0)
        return;
    std::lock_guard<std::mutex> g(fQueueLock);
    fCancelled.insert(handle);
}

void Engine::_WorkerLoop()
{
    for (;;) {
        Task task;
        {
            std::unique_lock<std::mutex> g(fQueueLock);
            for (;;) {
                if (!fRunning && fQueue.empty())
                    return;

                if (!fQueue.empty()) {
                    task = fQueue.front();
                    fQueue.pop_front();
                    break;
                }

                // Find the soonest timer that is due.
                int64_t now = _NowMs();
                int64_t soonest = -1;
                size_t ready = fTimers.size();
                for (size_t i = 0; i < fTimers.size(); i++) {
                    if (fCancelled.count(fTimers[i].handle) > 0)
                        continue;
                    if (fTimers[i].due_ms <= now) {
                        ready = i;
                        break;
                    }
                    if (soonest < 0 || fTimers[i].due_ms < soonest)
                        soonest = fTimers[i].due_ms;
                }
                // Drop cancelled timers whenever we pass through.
                if (!fCancelled.empty()) {
                    std::vector<TimerEntry> keep;
                    keep.reserve(fTimers.size());
                    for (size_t i = 0; i < fTimers.size(); i++) {
                        if (fCancelled.count(fTimers[i].handle) == 0)
                            keep.push_back(fTimers[i]);
                    }
                    if (ready < fTimers.size()) {
                        // Recompute after compaction.
                        TimerEntry due = fTimers[ready];
                        fTimers.swap(keep);
                        fCancelled.clear();
                        for (size_t i = 0; i < fTimers.size(); i++) {
                            if (fTimers[i].handle == due.handle) {
                                fTimers.erase(fTimers.begin() + i);
                                break;
                            }
                        }
                        task = due.task;
                        break;
                    }
                    fTimers.swap(keep);
                    fCancelled.clear();
                    continue;
                }
                if (ready < fTimers.size()) {
                    task = fTimers[ready].task;
                    fTimers.erase(fTimers.begin() + ready);
                    break;
                }

                if (soonest >= 0) {
                    fQueueCv.wait_for(g, std::chrono::milliseconds(soonest - now));
                } else {
                    fQueueCv.wait(g);
                }
            }
        }
        if (task)
            task();
    }
}

// ─── Transport callbacks (reader thread) ─────────────────────────

void Engine::OnTransportState(ConnectionState state)
{
    _Post([this, state] {
        EngineListener* l = _Listener();
        if (l != NULL)
            l->OnConnectionState(state);
        if (state == CONN_CONNECTED) {
            _StartInitialSync();
        } else if (state == CONN_DISCONNECTED) {
            // Abandon anything in flight; it will be retried on reconnect.
            for (std::map<int64_t, PendingSend>::iterator it = fPending.begin();
                 it != fPending.end(); ++it) {
                _Cancel(it->second.timeout_handle);
            }
            fPending.clear();
            fAckIndex.clear();
            fSyncing = false;
            fContactsIterBusy = false;
            fMsgSyncActive = false;
            _Cancel(fSyncWatchdog);
            fSyncWatchdog = 0;
            fSync = SyncProgress();
            _EmitSyncProgress();
        }
    });
}

void Engine::OnTransportFrame(const ByteBuffer& payload)
{
    ByteBuffer copy = payload;
    _Post([this, copy] { _HandleFrame(copy); });
}

void Engine::OnTransportError(const std::string& message)
{
    std::string m = message;
    _Post([this, m] { _Error(m); });
}

// ─── Small helpers ───────────────────────────────────────────────

bool Engine::_Send(const ByteBuffer& frame)
{
    if (fActive == NULL)
        return false;
    return fActive->SendFrame(frame);
}

void Engine::_Toast(const std::string& text)
{
    EngineListener* l = _Listener();
    if (l != NULL)
        l->OnToast(text);
}

void Engine::_Error(const std::string& text)
{
    EngineListener* l = _Listener();
    if (l != NULL)
        l->OnError(text);
}

std::string Engine::_ResolvePrefix(const std::string& prefix_hex)
{
    Contact c;
    if (fStore.GetContactByPrefix(prefix_hex, &c))
        return c.PublicKeyHex();
    return std::string();
}

ConnectionState Engine::State() const
{
    return fActive != NULL ? fActive->State() : CONN_DISCONNECTED;
}

DeviceInfo Engine::GetDeviceInfo()
{
    std::lock_guard<std::mutex> g(fDeviceLock);
    return fDevice;
}

std::vector<TransportDevice> Engine::EnumerateSerial()
{
    return fSerial.Enumerate();
}

// ─── Connection ──────────────────────────────────────────────────

void Engine::Connect(TransportKind kind, const std::string& device_path)
{
    std::string path = device_path;
    _Post([this, kind, path] {
        if (fActive != NULL)
            fActive->Disconnect();
        fActive = (kind == TRANSPORT_TCP) ? static_cast<FrameTransport*>(&fTcp)
                                          : static_cast<FrameTransport*>(&fSerial);
        if (!fActive->Connect(path))
            fActive = NULL;
        // A successful connect fires OnTransportState, which starts the sync.
    });
}

void Engine::Disconnect()
{
    _Post([this] {
        if (fActive != NULL)
            fActive->Disconnect();
        fActive = NULL;
    });
}

// ─── Initial sync ────────────────────────────────────────────────

void Engine::_StartInitialSync()
{
    fSyncing = true;
    fSync = SyncProgress();
    fSync.active = true;
    _EmitSyncProgress();

    _Send(BuildAppStart());
    _Send(BuildDeviceQuery());
    _Send(BuildGetDeviceTime());
    _Send(BuildGetBattAndStorage());
    _Send(BuildGetCustomVars());
    // SELF_INFO arrives unprompted after APP_START; contacts follow it.
    _Send(BuildGetContacts());
    _ScheduleSyncWatchdog();
}

void Engine::_ScheduleSyncWatchdog()
{
    _Cancel(fSyncWatchdog);
    fSyncWatchdog = _PostDelayed(kSyncWatchdogMs, [this] {
        if (!fSyncing && !fMsgSyncActive)
            return;
        // The device stopped answering mid-sync; give up rather than hang the
        // banner forever.
        _Error("Sync timed out");
        fSyncing = false;
        fContactsIterBusy = false;
        fMsgSyncActive = false;
        fSync = SyncProgress();
        _EmitSyncProgress();
    });
}

void Engine::_EmitSyncProgress()
{
    EngineListener* l = _Listener();
    if (l != NULL)
        l->OnSyncProgress(fSync);
}

void Engine::_SyncNextChannel()
{
    if (fChannelSyncIndex >= fMaxChannels) {
        _FinishChannelSync();
        return;
    }
    _Send(BuildGetChannel(static_cast<uint8_t>(fChannelSyncIndex)));
    _ScheduleSyncWatchdog();
}

void Engine::_FinishChannelSync()
{
    fSync.channels = false;
    _EmitSyncProgress();
    EngineListener* l = _Listener();
    if (l != NULL)
        l->OnChannelsChanged();
    // Channels are known, so queued messages can now be pulled safely.
    fSyncing = false;
    _SendSyncNextMessage();
}

void Engine::_SendSyncNextMessage()
{
    fMsgSyncActive = true;
    fSync.messages = true;
    _EmitSyncProgress();
    _Send(BuildSyncNextMessage());
    _ScheduleSyncWatchdog();
}

void Engine::_FinishMessageSync()
{
    fMsgSyncActive = false;
    fMsgSyncRetries = 0;
    fSync.messages = false;
    fSync.active = false;
    _Cancel(fSyncWatchdog);
    fSyncWatchdog = 0;
    _EmitSyncProgress();
}

// ─── Frame dispatch ──────────────────────────────────────────────

void Engine::_HandleFrame(const ByteBuffer& data)
{
    int code = IdentifyResponse(data);
    if (code < 0)
        return;

    switch (code) {
        case RESP_CODE_OK: _HandleOk(); break;
        case RESP_CODE_ERR: _HandleError(data); break;
        case RESP_CODE_SELF_INFO: _HandleSelfInfo(data); break;
        case RESP_CODE_DEVICE_INFO: _HandleDeviceInfo(data); break;
        case RESP_CODE_CONTACTS_START: _HandleContactsStart(data); break;
        case RESP_CODE_CONTACT: _HandleContact(data); break;
        case RESP_CODE_END_OF_CONTACTS: _HandleEndOfContacts(); break;
        case RESP_CODE_CONTACT_MSG_RECV:
        case RESP_CODE_CONTACT_MSG_RECV_V3: _HandleContactMessage(data); break;
        case RESP_CODE_CHANNEL_MSG_RECV:
        case RESP_CODE_CHANNEL_MSG_RECV_V3: _HandleChannelMessage(data); break;
        case RESP_CODE_CHANNEL_INFO: _HandleChannelInfo(data); break;
        case RESP_CODE_NO_MORE_MESSAGES: _HandleNoMoreMessages(); break;
        case RESP_CODE_SENT: _HandleSent(data); break;
        case PUSH_CODE_SEND_CONFIRMED: _HandleSendConfirmed(data); break;
        case PUSH_CODE_MSG_WAITING: _HandleMsgWaiting(); break;
        case PUSH_CODE_ADVERT:
        case PUSH_CODE_NEW_ADVERT: _HandleAdvert(data); break;
        case PUSH_CODE_PATH_UPDATED: _HandlePathUpdated(data); break;
        case PUSH_CODE_LOG_DATA: _HandleLogData(data); break;
        case RESP_CODE_BATT_AND_STORAGE: _HandleBattery(data); break;
        case RESP_CODE_STATS: _HandleStats(data); break;
        case RESP_CODE_CLI_REPLY: _HandleCliReply(data); break;
        case PUSH_CODE_LOGIN_SUCCESS: _HandleLoginResult(true, data); break;
        case PUSH_CODE_LOGIN_FAILED: _HandleLoginResult(false, data); break;
        case PUSH_CODE_STATUS_RESPONSE: _HandleStatusResponse(data); break;
        case PUSH_CODE_TELEMETRY_RESPONSE: _HandleTelemetry(data); break;
        case PUSH_CODE_TRACE_DATA: _HandleTrace(data); break;
        case PUSH_CODE_CONTACT_DELETED: _HandleContactDeleted(data); break;
        case RESP_CODE_CURR_TIME: _HandleCurrTime(data); break;
        case PUSH_CODE_CONTACTS_FULL:
            _Error("The device's contact list is full");
            break;
        case RESP_CODE_DISABLED:
            _Error("The device reports this feature is disabled");
            break;
        default:
            // Unhandled response codes are ignored, as upstream does.
            break;
    }
}

void Engine::_HandleOk()
{
    // Generic acknowledgement; nothing to do.
}

void Engine::_HandleError(const ByteBuffer& d)
{
    ErrorResponse e = ParseErrorResponse(d);
    _Error(e.message);
    // A failed sync step must not leave the banner spinning.
    if (fContactsIterBusy) {
        fContactsIterBusy = false;
        _HandleEndOfContacts();
    }
}

void Engine::_HandleSelfInfo(const ByteBuffer& d)
{
    SelfInfo si = ParseSelfInfo(d);
    if (!si.valid)
        return;
    {
        std::lock_guard<std::mutex> g(fDeviceLock);
        fDevice.name = si.name;
        fDevice.public_key = si.public_key;
        fDevice.tx_power = si.tx_power;
        fDevice.max_tx_power = si.max_tx_power;
        fDevice.radio_freq = si.radio_freq;
        fDevice.radio_bw = si.radio_bw;
        fDevice.radio_sf = si.radio_sf;
        fDevice.radio_cr = si.radio_cr;
        fDevice.has_location = si.adv_lat != 0.0 || si.adv_lon != 0.0;
        fDevice.adv_lat = si.adv_lat;
        fDevice.adv_lon = si.adv_lon;
    }
    EngineListener* l = _Listener();
    if (l != NULL)
        l->OnDeviceInfoChanged(GetDeviceInfo());
}

void Engine::_HandleDeviceInfo(const ByteBuffer& d)
{
    DeviceInfoFrame di = ParseDeviceInfo(d);
    if (!di.valid)
        return;
    {
        std::lock_guard<std::mutex> g(fDeviceLock);
        fDevice.fw_ver = di.fw_ver;
        fDevice.fw_build = di.fw_build;
        fDevice.model = di.model;
        fDevice.ver = di.ver;
        fDevice.max_contacts = di.max_contacts;
        fDevice.max_channels = di.max_channels;
        fDevice.ble_pin = di.ble_pin;
        if (di.has_repeat)
            fDevice.repeat = di.repeat;
        if (di.has_path_hash_mode)
            fDevice.path_hash_mode = di.path_hash_mode;
    }
    if (di.max_channels > 0)
        fMaxChannels = di.max_channels;
    EngineListener* l = _Listener();
    if (l != NULL)
        l->OnDeviceInfoChanged(GetDeviceInfo());
}

void Engine::_HandleContactsStart(const ByteBuffer& d)
{
    fContactsIterBusy = true;
    fPendingContacts.clear();
    ByteReader r(d.data() + 1, d.size() - 1);
    fSync.contacts_total = d.size() >= 5 ? static_cast<int>(r.U32()) : 0;
    fSync.contacts_done = 0;
    fSync.active = true;
    _EmitSyncProgress();
    _ScheduleSyncWatchdog();
}

void Engine::_HandleContact(const ByteBuffer& d)
{
    ContactFrame c = ParseContactFrame(d);
    if (fContactsIterBusy) {
        _ScheduleSyncWatchdog();
        if (c.valid)
            fPendingContacts.push_back(c);  // written in bulk at end-of-contacts
        fSync.contacts_done++;
        _EmitSyncProgress();
        return;
    }
    if (!c.valid)
        return;

    // A single contact update outside bulk sync is saved immediately.
    Contact existing;
    bool had = fStore.GetContact(c.public_key, &existing);
    Contact out = had ? existing : Contact();
    out.public_key = FromHexOrEmpty(c.public_key);
    out.name = c.adv_name;
    out.type = c.type;
    out.flags = c.flags;
    out.path_length = c.out_path_len;
    out.path = FromHexOrEmpty(c.out_path);
    out.path_hash_size = c.path_hash_size;
    if (c.adv_lat != 0.0 || c.adv_lon != 0.0) {
        out.has_location = true;
        out.latitude = c.adv_lat;
        out.longitude = c.adv_lon;
    }
    if (c.last_advert > 0)
        out.last_seen = static_cast<double>(c.last_advert);
    if (c.has_lastmod)
        out.device_lastmod = static_cast<double>(c.lastmod);
    fStore.SaveContact(out);
    EngineListener* l = _Listener();
    if (l != NULL)
        l->OnContactsChanged();
}

void Engine::_HandleEndOfContacts()
{
    fContactsIterBusy = false;

    fStore.BeginBatch();
    for (size_t i = 0; i < fPendingContacts.size(); i++) {
        const ContactFrame& c = fPendingContacts[i];
        Contact existing;
        bool had = fStore.GetContact(c.public_key, &existing);
        Contact out = had ? existing : Contact();
        out.public_key = FromHexOrEmpty(c.public_key);
        out.name = c.adv_name;
        out.type = c.type;
        out.flags = c.flags;
        out.path_length = c.out_path_len;
        out.path = FromHexOrEmpty(c.out_path);
        out.path_hash_size = c.path_hash_size;
        if (c.adv_lat != 0.0 || c.adv_lon != 0.0) {
            out.has_location = true;
            out.latitude = c.adv_lat;
            out.longitude = c.adv_lon;
        }
        if (c.last_advert > 0)
            out.last_seen = static_cast<double>(c.last_advert);
        if (c.has_lastmod)
            out.device_lastmod = static_cast<double>(c.lastmod);
        out.is_active = true;
        fStore.SaveContact(out);
    }
    fStore.EndBatch();
    fPendingContacts.clear();

    EngineListener* l = _Listener();
    if (l != NULL)
        l->OnContactsChanged();

    // Contacts done; walk the channel table next.
    fChannelSyncIndex = 0;
    fSync.channels = true;
    fChannelByHash.clear();
    _EmitSyncProgress();
    _SyncNextChannel();
}

void Engine::_HandleChannelInfo(const ByteBuffer& d)
{
    ChannelInfo ci = ParseChannelInfo(d);
    if (ci.valid) {
        bool blank = true;
        for (size_t i = 0; i < ci.channel_secret.size(); i++) {
            if (ci.channel_secret[i] != 0) {
                blank = false;
                break;
            }
        }
        if (!blank || !ci.channel_name.empty()) {
            Channel ch;
            Channel existing;
            if (fStore.GetChannel(ci.channel_idx, &existing))
                ch = existing;
            ch.index = ci.channel_idx;
            ch.name = ci.channel_name;
            ch.psk = ci.channel_secret;
            fStore.SaveChannel(ch);
            fChannelByHash[ChannelHashBytes(ci.channel_secret)] = ci.channel_idx;
        } else {
            // An empty slot: drop any stale local copy.
            fStore.DeleteChannel(ci.channel_idx);
        }
    }
    if (fSync.channels) {
        fChannelSyncIndex++;
        _SyncNextChannel();
    } else {
        EngineListener* l = _Listener();
        if (l != NULL)
            l->OnChannelsChanged();
    }
}

void Engine::_HandleNoMoreMessages()
{
    _FinishMessageSync();
}

void Engine::_HandleMsgWaiting()
{
    // During the initial sync the channel table is not loaded yet, and an
    // incoming channel message for an unknown channel would be dropped after
    // the device had already removed it from its queue.  The sync pulls the
    // queue itself once channels are known.
    if (fSyncing)
        return;
    fMsgSyncRetries = 0;
    _SendSyncNextMessage();
}

void Engine::_HandleContactMessage(const ByteBuffer& d)
{
    ContactMessage m = ParseContactMessage(d);
    if (m.valid) {
        std::string key = _ResolvePrefix(m.pubkey_prefix);
        if (key.empty()) {
            // Unknown sender: the advert has not arrived yet.  Upstream asks
            // the device for the full contact record.
            _Send(BuildGetContacts());
        } else {
            Message msg;
            msg.contact_key_hex = key;
            msg.sender_key = FromHexOrEmpty(key);
            msg.text = m.text;
            msg.timestamp = m.sender_timestamp > 0 ? static_cast<double>(m.sender_timestamp)
                                                   : _NowUnix();
            msg.is_outgoing = false;
            msg.is_cli = (m.txt_type == TXT_TYPE_CLI_DATA || m.txt_type == TXT_TYPE_CLI_COMMAND);
            msg.status = MSG_DELIVERED;
            msg.uid = ContentUidDm(key, key, false, msg.timestamp, msg.text);

            EngineListener* l = _Listener();
            if (msg.is_cli) {
                // CLI replies belong to the repeater/room console, not the chat log.
                if (l != NULL)
                    l->OnRemoteCliReply(key, m.text);
            } else {
                int64_t id = fStore.AddMessage(msg);
                if (id > 0 && l != NULL)
                    l->OnMessage(key, id);
            }
        }
    }
    // Always continue the loop: one bad frame must not stall the sync.
    fMsgSyncRetries = 0;
    _SendSyncNextMessage();
}

void Engine::_HandleChannelMessage(const ByteBuffer& d)
{
    ChannelMessageFrame m = ParseChannelMessage(d);
    if (m.valid) {
        Channel ch;
        if (fStore.GetChannel(m.channel_idx, &ch)) {
            std::vector<std::string> blocked = fStore.GetBlockedSenders();
            bool is_blocked = std::find(blocked.begin(), blocked.end(), m.sender_name)
                != blocked.end();
            if (!is_blocked) {
                ChannelMessage cm;
                cm.channel_index = m.channel_idx;
                cm.sender_name = m.sender_name;
                cm.text = m.msg_text;
                cm.timestamp = m.sender_timestamp > 0 ? static_cast<double>(m.sender_timestamp)
                                                      : _NowUnix();
                cm.is_outgoing = false;
                cm.uid = ContentUidChannel(cm.channel_index, cm.sender_name, false, cm.timestamp,
                    cm.text);
                int64_t id = fStore.AddChannelMessage(cm);
                EngineListener* l = _Listener();
                if (id > 0 && l != NULL)
                    l->OnChannelMessage(cm.channel_index, id);
            }
        }
    }
    fMsgSyncRetries = 0;
    _SendSyncNextMessage();
}

void Engine::_HandleSent(const ByteBuffer& d)
{
    SentResponse s = ParseSentResponse(d);
    if (!s.valid)
        return;
    // Attach the expected ack to the most recently sent message.
    int64_t newest = 0;
    double newest_at = -1.0;
    for (std::map<int64_t, PendingSend>::iterator it = fPending.begin(); it != fPending.end();
         ++it) {
        if (!it->second.has_ack && it->second.sent_at > newest_at) {
            newest_at = it->second.sent_at;
            newest = it->first;
        }
    }
    if (newest == 0)
        return;

    PendingSend& p = fPending[newest];
    ByteReader r(s.expected_ack);
    p.ack_code = r.U32();
    p.has_ack = true;
    fAckIndex[p.ack_code] = newest;

    fStore.UpdateMessageStatus(newest, MSG_SENT);
    EngineListener* l = _Listener();
    if (l != NULL)
        l->OnMessageStatus(newest, MSG_SENT);

    // Prefer the firmware's own timeout estimate when it gives one.
    int64_t timeout = s.suggested_timeout > 0 ? static_cast<int64_t>(s.suggested_timeout)
                                              : kSendTimeoutMs;
    _Cancel(p.timeout_handle);
    int64_t row = newest;
    p.timeout_handle = _PostDelayed(timeout, [this, row] { _OnSendTimeout(row); });
}

void Engine::_HandleSendConfirmed(const ByteBuffer& d)
{
    SendConfirmed s = ParsePushSendConfirmed(d);
    if (!s.valid)
        return;
    ByteBuffer code = FromHexOrEmpty(s.code);
    ByteReader r(code);
    uint32_t ack = r.U32();

    std::map<uint32_t, int64_t>::iterator ai = fAckIndex.find(ack);
    if (ai == fAckIndex.end())
        return;  // ack for something we are no longer tracking
    int64_t row = ai->second;
    fAckIndex.erase(ai);

    std::map<int64_t, PendingSend>::iterator pi = fPending.find(row);
    if (pi != fPending.end()) {
        _Cancel(pi->second.timeout_handle);
        // Record the route that worked, so smart routing can prefer it.
        Contact c;
        if (fStore.GetContact(pi->second.contact_key_hex, &c) && !c.path.empty()) {
            fStore.RecordPathResult(c.PublicKeyHex(), ToHex(c.path), c.path_hash_size,
                c.path_length, true, s.has_trip_time, static_cast<int>(s.trip_time_ms));
        }
        fPending.erase(pi);
    }

    fStore.UpdateMessageStatus(row, MSG_DELIVERED);
    EngineListener* l = _Listener();
    if (l != NULL)
        l->OnMessageStatus(row, MSG_DELIVERED);
}

void Engine::_HandleAdvert(const ByteBuffer& d)
{
    ContactFrame c = ParseContactFrame(d);
    if (!c.valid)
        return;

    // A known contact gets refreshed; an unknown one lands in the discovered
    // list for the user to add.
    Contact existing;
    if (fStore.GetContact(c.public_key, &existing)) {
        existing.name = c.adv_name.empty() ? existing.name : c.adv_name;
        existing.type = c.type;
        if (c.adv_lat != 0.0 || c.adv_lon != 0.0) {
            existing.has_location = true;
            existing.latitude = c.adv_lat;
            existing.longitude = c.adv_lon;
        }
        existing.last_seen = c.last_advert > 0 ? static_cast<double>(c.last_advert) : _NowUnix();
        fStore.SaveContact(existing);
        EngineListener* l = _Listener();
        if (l != NULL)
            l->OnContactsChanged();
        return;
    }

    DiscoveredContact dc;
    dc.public_key_hex = c.public_key;
    dc.name = c.adv_name;
    dc.type = c.type;
    dc.flags = c.flags;
    dc.path_length = c.out_path_len;
    dc.path = FromHexOrEmpty(c.out_path);
    dc.path_hash_size = c.path_hash_size;
    if (c.adv_lat != 0.0 || c.adv_lon != 0.0) {
        dc.has_location = true;
        dc.latitude = c.adv_lat;
        dc.longitude = c.adv_lon;
    }
    dc.last_seen = c.last_advert > 0 ? static_cast<double>(c.last_advert) : _NowUnix();
    dc.received_at = _NowUnix();
    dc.source = "advert";
    fStore.SaveDiscovered(dc);
    EngineListener* l = _Listener();
    if (l != NULL)
        l->OnDiscoveredChanged();
}

void Engine::_HandlePathUpdated(const ByteBuffer& d)
{
    // [code][pubkey_prefix(6)][path_len][path...]
    if (d.size() < 8)
        return;
    std::string prefix = ToHex(d.data() + 1, 6);
    std::string key = _ResolvePrefix(prefix);
    if (key.empty())
        return;
    int hop_count, hash_size;
    ParsePathLengthByte(d[7], &hop_count, &hash_size);
    size_t path_bytes = hop_count > 0 ? static_cast<size_t>(hop_count) * hash_size : 0;
    if (8 + path_bytes > d.size())
        path_bytes = d.size() - 8;
    ByteBuffer path(d.begin() + 8, d.begin() + 8 + path_bytes);
    fStore.SetContactPath(key, hop_count, path, hash_size);
    EngineListener* l = _Listener();
    if (l != NULL)
        l->OnContactsChanged();
}

void Engine::_HandleLogData(const ByteBuffer& d)
{
    // 0x88 carries the raw radio packet behind a 1-byte code (and, on newer
    // firmware, an SNR byte the decoder does not need).
    if (d.size() < 2)
        return;
    ByteBuffer raw(d.begin() + 1, d.end());
    LogPacket pkt = DecodeLogPacket(raw);
    if (!pkt.valid)
        return;

    EngineListener* l = _Listener();
    if (l != NULL)
        l->OnRawPacket(pkt);

    // Group text carries the path metadata the channel view shows; match it
    // to a channel by the 1-byte PSK hash.
    if (pkt.has_channel) {
        std::map<std::string, int>::iterator it = fChannelByHash.find(pkt.channel_hash);
        if (it == fChannelByHash.end())
            return;
        Channel ch;
        if (!fStore.GetChannel(it->second, &ch))
            return;
        GroupTextPlain g = DecryptGroupText(pkt.ciphertext, ch.psk);
        if (!g.valid)
            return;
        std::vector<std::string> regions = fStore.GetRegions();
        std::string region = ResolveRegionForLogPacket(pkt, regions);
        std::string joined;
        for (size_t i = 0; i < pkt.path.size(); i++)
            joined += pkt.path[i];
        fStore.RecordChannelPath(it->second, g.timestamp, ToHex(crypto::Sha256(g.message)).substr(0, 16),
            false, 0.0, static_cast<int>(pkt.path.size()), joined, region);
    }
}

void Engine::_HandleBattery(const ByteBuffer& d)
{
    BattAndStorage b = ParseBattAndStorage(d);
    if (!b.valid)
        return;
    {
        std::lock_guard<std::mutex> g(fDeviceLock);
        fDevice.battery_mv = b.level;
        if (b.has_storage) {
            fDevice.storage_used_kb = b.used_kb;
            fDevice.storage_total_kb = b.total_kb;
        }
    }
    EngineListener* l = _Listener();
    if (l != NULL)
        l->OnBattery(b.level, b.used_kb, b.total_kb);
}

void Engine::_HandleStats(const ByteBuffer& d)
{
    StatsResponse s = ParseStatsResponse(d);
    if (!s.valid)
        return;
    EngineListener* l = _Listener();
    if (l != NULL)
        l->OnStats(s);
}

void Engine::_HandleCliReply(const ByteBuffer& d)
{
    std::string text;
    if (!ParseCliReply(d, &text))
        return;
    EngineListener* l = _Listener();
    if (l != NULL)
        l->OnCliReply(text);
}

void Engine::_HandleLoginResult(bool success, const ByteBuffer& d)
{
    std::string key = fLoginTarget;
    if (key.empty() && d.size() >= 7)
        key = _ResolvePrefix(ToHex(d.data() + 1, 6));
    fLoginTarget.clear();
    EngineListener* l = _Listener();
    if (l != NULL)
        l->OnLoginResult(key, success);
}

void Engine::_HandleStatusResponse(const ByteBuffer& d)
{
    StatusResponse s = ParseStatusResponse(d);
    if (!s.valid)
        return;
    std::string key;
    if (d.size() >= 8)
        key = _ResolvePrefix(ToHex(d.data() + 2, 6));
    EngineListener* l = _Listener();
    if (l != NULL)
        l->OnStatusResponse(key, s);
}

void Engine::_HandleTelemetry(const ByteBuffer& d)
{
    // [code][reserved][sender_prefix(6)][cayenne lpp...]
    if (d.size() < 8)
        return;
    std::string key = _ResolvePrefix(ToHex(d.data() + 2, 6));
    ByteBuffer lpp(d.begin() + 8, d.end());
    EngineListener* l = _Listener();
    if (l != NULL)
        l->OnTelemetry(key, lpp);
}

void Engine::_HandleTrace(const ByteBuffer& d)
{
    TraceResponse t = ParseTraceResponse(d);
    if (!t.valid)
        return;
    EngineListener* l = _Listener();
    if (l != NULL)
        l->OnTraceResult(t);
}

void Engine::_HandleContactDeleted(const ByteBuffer& d)
{
    if (d.size() < 7)
        return;
    std::string key = _ResolvePrefix(ToHex(d.data() + 1, 6));
    if (key.empty())
        return;
    fStore.SetContactActive(key, false);
    EngineListener* l = _Listener();
    if (l != NULL)
        l->OnContactsChanged();
}

void Engine::_HandleCurrTime(const ByteBuffer& d)
{
    uint32_t t = 0;
    if (!ParseCurrTime(d, &t))
        return;
    // A device clock that is far out makes message ordering nonsense, so
    // correct it the way upstream does.
    uint32_t now = static_cast<uint32_t>(_NowUnix());
    int64_t skew = static_cast<int64_t>(now) - static_cast<int64_t>(t);
    if (skew > 30 || skew < -30) {
        _Send(BuildSetDeviceTime(now));
        _Toast("Corrected the device clock");
    }
}

// ─── Sending ─────────────────────────────────────────────────────

void Engine::SendMessage(const std::string& contact_key_hex, const std::string& text)
{
    std::string key = contact_key_hex;
    std::string body = text;
    _Post([this, key, body] {
        Contact c;
        if (!fStore.GetContact(key, &c)) {
            _Error("Unknown contact");
            return;
        }

        Message m;
        m.contact_key_hex = key;
        m.sender_key = ByteBuffer();
        m.text = body;
        m.timestamp = _NowUnix();
        m.is_outgoing = true;
        m.status = MSG_PENDING;
        m.uid = NewUid();
        int64_t id = fStore.AddMessage(m);
        if (id <= 0) {
            _Error("Could not save the message");
            return;
        }

        PendingSend p;
        p.row_id = id;
        p.contact_key_hex = key;
        p.text = body;
        p.attempt = 0;
        fPending[id] = p;

        EngineListener* l = _Listener();
        if (l != NULL)
            l->OnMessage(key, id);
        _DoSendMessage(id);
    });
}

void Engine::_DoSendMessage(int64_t row_id)
{
    std::map<int64_t, PendingSend>::iterator it = fPending.find(row_id);
    if (it == fPending.end())
        return;
    PendingSend& p = it->second;

    Contact c;
    if (!fStore.GetContact(p.contact_key_hex, &c)) {
        fPending.erase(it);
        return;
    }

    // The firmware truncates over-long text; do it here so the stored copy
    // matches what actually went out.
    std::string text = TruncateUtf8(p.text, MaxDmTextBytes());
    p.sent_at = static_cast<double>(_NowMs());
    if (!_Send(BuildSendTextMsg(c.public_key, text, static_cast<uint8_t>(p.attempt)))) {
        _RetryOrFail(row_id);
        return;
    }

    // If no RESP_CODE_SENT arrives, fall back to a fixed timeout.
    _Cancel(p.timeout_handle);
    p.timeout_handle = _PostDelayed(kSendTimeoutMs, [this, row_id] { _OnSendTimeout(row_id); });
}

void Engine::_OnSendTimeout(int64_t row_id)
{
    _RetryOrFail(row_id);
}

void Engine::_RetryOrFail(int64_t row_id)
{
    std::map<int64_t, PendingSend>::iterator it = fPending.find(row_id);
    if (it == fPending.end())
        return;
    PendingSend& p = it->second;

    if (p.has_ack) {
        fAckIndex.erase(p.ack_code);
        p.has_ack = false;
    }

    p.attempt++;
    if (p.attempt >= kMaxSendAttempts) {
        // Record the failure so smart routing learns this path is unreliable.
        Contact c;
        if (fStore.GetContact(p.contact_key_hex, &c) && !c.path.empty()) {
            fStore.RecordPathResult(c.PublicKeyHex(), ToHex(c.path), c.path_hash_size,
                c.path_length, false, false, 0);
            // Fall back to flood routing for the next attempt, as upstream does.
            if (c.auto_reset_path && c.path_length >= 0)
                fStore.SetContactPath(c.PublicKeyHex(), -1, ByteBuffer(), 1);
        }
        fPending.erase(it);
        fStore.UpdateMessageStatus(row_id, MSG_FAILED);
        EngineListener* l = _Listener();
        if (l != NULL)
            l->OnMessageStatus(row_id, MSG_FAILED);
        return;
    }

    _PostDelayed(kRetryBackoffMs, [this, row_id] { _DoSendMessage(row_id); });
}

void Engine::SendChannelMessage(int channel_index, const std::string& text)
{
    std::string body = text;
    _Post([this, channel_index, body] {
        Channel ch;
        if (!fStore.GetChannel(channel_index, &ch)) {
            _Error("Unknown channel");
            return;
        }
        DeviceInfo di = GetDeviceInfo();
        std::string truncated = TruncateUtf8(body, MaxChannelTextBytes(di.name));

        ChannelMessage cm;
        cm.channel_index = channel_index;
        cm.sender_name = di.name;
        cm.text = truncated;
        cm.timestamp = _NowUnix();
        cm.is_outgoing = true;
        cm.uid = NewUid();
        int64_t id = fStore.AddChannelMessage(cm);

        if (!_Send(BuildSendChannelTextMsg(static_cast<uint8_t>(channel_index), truncated)))
            _Error("Could not send the message");

        EngineListener* l = _Listener();
        if (id > 0 && l != NULL)
            l->OnChannelMessage(channel_index, id);
    });
}

void Engine::SendCliCommand(const std::string& contact_key_hex, const std::string& command)
{
    std::string key = contact_key_hex;
    std::string cmd = command;
    _Post([this, key, cmd] {
        Contact c;
        if (!fStore.GetContact(key, &c))
            return;
        bool supports_cli = (c.flags & CONTACT_FLAG_REMOTE_CLI) != 0;
        uint8_t txt_type = CliTxtType(c.type, supports_cli);
        _Send(BuildSendCliCommand(c.public_key, cmd, 0, 0, txt_type));
    });
}

void Engine::RunLocalCli(const std::string& command)
{
    std::string cmd = command;
    _Post([this, cmd] { _Send(BuildRunCliCommand(cmd)); });
}

void Engine::SyncMessages()
{
    _Post([this] { _SendSyncNextMessage(); });
}

void Engine::SendRawFrame(const ByteBuffer& frame)
{
    ByteBuffer f = frame;
    _Post([this, f] { _Send(f); });
}

// ─── Contacts ────────────────────────────────────────────────────

void Engine::RefreshContacts()
{
    _Post([this] { _Send(BuildGetContacts()); });
}

void Engine::AddContact(const Contact& c)
{
    Contact copy = c;
    _Post([this, copy] {
        fStore.SaveContact(copy);
        _Send(BuildAddUpdateContact(copy.public_key, copy.type, copy.flags,
            EncodePathLen(copy.path_length, copy.path_hash_size), copy.path, copy.name,
            copy.has_location, copy.latitude, copy.longitude, false, 0,
            HasTime(copy.last_seen) ? static_cast<uint32_t>(copy.last_seen) : 0));
        EngineListener* l = _Listener();
        if (l != NULL)
            l->OnContactsChanged();
    });
}

void Engine::RemoveContact(const std::string& contact_key_hex)
{
    std::string key = contact_key_hex;
    _Post([this, key] {
        ByteBuffer pk = FromHexOrEmpty(key);
        _Send(BuildRemoveContact(pk));
        fStore.DeleteContact(key);
        EngineListener* l = _Listener();
        if (l != NULL)
            l->OnContactsChanged();
    });
}

void Engine::ResetPath(const std::string& contact_key_hex)
{
    std::string key = contact_key_hex;
    _Post([this, key] {
        _Send(BuildResetPath(FromHexOrEmpty(key)));
        fStore.SetContactPath(key, -1, ByteBuffer(), 1);
        EngineListener* l = _Listener();
        if (l != NULL)
            l->OnContactsChanged();
    });
}

void Engine::ShareContact(const std::string& contact_key_hex)
{
    std::string key = contact_key_hex;
    _Post([this, key] { _Send(BuildShareContact(FromHexOrEmpty(key))); });
}

void Engine::RequestPathDiscovery(const std::string& contact_key_hex)
{
    std::string key = contact_key_hex;
    _Post([this, key] { _Send(BuildPathDiscovery(FromHexOrEmpty(key))); });
}

void Engine::SendAdvert(bool flood)
{
    _Post([this, flood] { _Send(BuildSendSelfAdvert(flood)); });
}

void Engine::SendDiscoverRequest()
{
    _Post([this] { _Send(BuildSendDiscoverReq()); });
}

void Engine::ImportDiscovered(const std::string& public_key_hex)
{
    std::string key = public_key_hex;
    _Post([this, key] {
        std::vector<DiscoveredContact> all = fStore.GetDiscovered();
        for (size_t i = 0; i < all.size(); i++) {
            if (all[i].public_key_hex != key)
                continue;
            Contact c;
            c.public_key = FromHexOrEmpty(all[i].public_key_hex);
            c.name = all[i].name;
            c.type = all[i].type;
            c.flags = all[i].flags;
            c.path_length = all[i].path_length;
            c.path = all[i].path;
            c.path_hash_size = all[i].path_hash_size;
            c.has_location = all[i].has_location;
            c.latitude = all[i].latitude;
            c.longitude = all[i].longitude;
            c.last_seen = all[i].last_seen;
            fStore.SaveContact(c);
            _Send(BuildAddUpdateContact(c.public_key, c.type, c.flags,
                EncodePathLen(c.path_length, c.path_hash_size), c.path, c.name, c.has_location,
                c.latitude, c.longitude, false, 0,
                HasTime(c.last_seen) ? static_cast<uint32_t>(c.last_seen) : 0));
            fStore.DeleteDiscovered(key);
            EngineListener* l = _Listener();
            if (l != NULL) {
                l->OnContactsChanged();
                l->OnDiscoveredChanged();
            }
            return;
        }
    });
}

// ─── Channels ────────────────────────────────────────────────────

void Engine::RefreshChannels()
{
    _Post([this] {
        fChannelSyncIndex = 0;
        fSync.channels = true;
        fChannelByHash.clear();
        _SyncNextChannel();
    });
}

void Engine::SetChannel(int index, const std::string& name, const ByteBuffer& psk)
{
    std::string n = name;
    ByteBuffer p = psk;
    _Post([this, index, n, p] {
        _Send(BuildSetChannel(static_cast<uint8_t>(index), n, p));
        Channel ch;
        ch.index = index;
        ch.name = n;
        ch.psk = p;
        fStore.SaveChannel(ch);
        fChannelByHash[ChannelHashBytes(p)] = index;
        EngineListener* l = _Listener();
        if (l != NULL)
            l->OnChannelsChanged();
    });
}

void Engine::ClearChannel(int index)
{
    _Post([this, index] {
        _Send(BuildSetChannel(static_cast<uint8_t>(index), std::string(), ByteBuffer(16, 0)));
        fStore.DeleteChannel(index);
        EngineListener* l = _Listener();
        if (l != NULL)
            l->OnChannelsChanged();
    });
}

// ─── Rooms and repeaters ─────────────────────────────────────────

void Engine::Login(const std::string& contact_key_hex, const std::string& password)
{
    std::string key = contact_key_hex;
    std::string pw = password;
    _Post([this, key, pw] {
        fLoginTarget = key;
        _Send(BuildSendLogin(FromHexOrEmpty(key), pw));
    });
}

void Engine::Logout(const std::string& contact_key_hex)
{
    std::string key = contact_key_hex;
    _Post([this, key] { _Send(BuildLogout(FromHexOrEmpty(key))); });
}

void Engine::RequestStatus(const std::string& contact_key_hex)
{
    std::string key = contact_key_hex;
    _Post([this, key] { _Send(BuildSendStatusReq(FromHexOrEmpty(key))); });
}

void Engine::RequestTelemetry(const std::string& contact_key_hex)
{
    std::string key = contact_key_hex;
    _Post([this, key] {
        Contact c;
        if (!fStore.GetContact(key, &c))
            return;
        if (c.type == CONTACT_CHAT)
            _Send(BuildGetTelemetryChat(c.public_key));
        else
            _Send(BuildGetTelemetry(c.public_key));
    });
}

void Engine::RequestNeighbors(const std::string& contact_key_hex)
{
    std::string key = contact_key_hex;
    _Post([this, key] { _Send(BuildGetNeighbors(FromHexOrEmpty(key))); });
}

// ─── Device settings ─────────────────────────────────────────────

void Engine::SetDeviceName(const std::string& name)
{
    std::string n = name;
    _Post([this, n] {
        _Send(BuildSetAdvertName(n));
        {
            std::lock_guard<std::mutex> g(fDeviceLock);
            fDevice.name = n;
        }
        EngineListener* l = _Listener();
        if (l != NULL)
            l->OnDeviceInfoChanged(GetDeviceInfo());
    });
}

void Engine::SetDeviceTime(uint32_t timestamp)
{
    _Post([this, timestamp] { _Send(BuildSetDeviceTime(timestamp)); });
}

void Engine::SetRadioParams(uint32_t freq_khz, uint32_t bw_khz, uint8_t sf, uint8_t cr)
{
    _Post([this, freq_khz, bw_khz, sf, cr] {
        _Send(BuildSetRadioParams(freq_khz, bw_khz, sf, cr));
        _Send(BuildDeviceQuery());
    });
}

void Engine::SetTxPower(int dbm)
{
    _Post([this, dbm] { _Send(BuildSetRadioTxPower(dbm)); });
}

void Engine::SetDeviceLocation(double lat, double lon)
{
    _Post([this, lat, lon] { _Send(BuildSetAdvertLatLon(lat, lon)); });
}

void Engine::SetAutoAddConfig(bool chat, bool repeater, bool room, bool sensor, bool overwrite,
    uint8_t max_hops)
{
    _Post([this, chat, repeater, room, sensor, overwrite, max_hops] {
        _Send(BuildSetAutoAddConfig(chat, repeater, room, sensor, overwrite, max_hops));
    });
}

void Engine::SetOtherParams(uint8_t manual_add, uint8_t telemetry, uint8_t loc_policy,
    uint8_t multi_acks)
{
    _Post([this, manual_add, telemetry, loc_policy, multi_acks] {
        _Send(BuildSetOtherParams(manual_add, telemetry, loc_policy, multi_acks));
    });
}

void Engine::RequestStats(uint8_t sub_type)
{
    _Post([this, sub_type] { _Send(BuildGetStats(sub_type)); });
}

void Engine::RequestBattery()
{
    _Post([this] { _Send(BuildGetBattAndStorage()); });
}

void Engine::Reboot()
{
    _Post([this] { _Send(BuildReboot()); });
}

}  // namespace meshy
