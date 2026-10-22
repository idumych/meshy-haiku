// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The MeshCore application engine: owns the transport, the database and all
// protocol state, and drives the connect / contact-sync / message-sync state
// machines.  Ported from upstream Meshy's frame_handler.py,
// connection_controller.py and message_controller.py, which upstream keeps as
// mixins over the GTK window; here they are one object with no UI dependency.
//
// Threading model
// ---------------
// The engine runs a single worker thread.  Every piece of engine state is
// touched only on that thread, so no state locks are needed.  Public methods
// are callable from any thread: they enqueue a command and return
// immediately.  Listener callbacks are invoked on the worker thread, so a UI
// implementation must marshal them (the Haiku layer posts a BMessage).

#ifndef MESHY_CORE_ENGINE_H
#define MESHY_CORE_ENGINE_H

#include <stdint.h>

#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "MeshPacket.h"
#include "Models.h"
#include "Protocol.h"
#include "Storage.h"
#include "Transport.h"

namespace meshy {

enum TransportKind {
    TRANSPORT_SERIAL = 0,
    TRANSPORT_TCP = 1,
};

// Progress of the post-connect synchronisation, for the UI banner.
struct SyncProgress {
    bool active = false;
    int contacts_done = 0;
    int contacts_total = 0;
    bool channels = false;
    bool messages = false;
};

class EngineListener {
public:
    virtual ~EngineListener() {}

    virtual void OnConnectionState(ConnectionState) {}
    virtual void OnSyncProgress(const SyncProgress&) {}
    // Something the user should see; already localised by the caller.
    virtual void OnToast(const std::string&) {}
    virtual void OnError(const std::string&) {}

    virtual void OnDeviceInfoChanged(const DeviceInfo&) {}
    virtual void OnContactsChanged() {}
    virtual void OnChannelsChanged() {}
    virtual void OnDiscoveredChanged() {}

    // A direct message was stored (incoming or outgoing).
    virtual void OnMessage(const std::string&, int64_t) {}
    virtual void OnMessageStatus(int64_t, int) {}
    virtual void OnChannelMessage(int, int64_t) {}

    virtual void OnCliReply(const std::string&) {}
    // A reply to a remote CLI command sent to a repeater, room or sensor.
    virtual void OnRemoteCliReply(const std::string&, const std::string&) {}
    virtual void OnLoginResult(const std::string&, bool) {}
    virtual void OnStatusResponse(const std::string&, const StatusResponse&) {}
    virtual void OnTelemetry(const std::string&, const ByteBuffer&) {}
    virtual void OnTraceResult(const TraceResponse&) {}
    virtual void OnStats(const StatsResponse&) {}
    virtual void OnBattery(int, uint32_t, uint32_t) {}
    // A raw packet seen on the air, for the packet log view.
    virtual void OnRawPacket(const LogPacket&) {}
};

class Engine : private TransportListener {
public:
    Engine();
    virtual ~Engine();

    // Opens the database and starts the worker thread.  An empty db_path uses
    // the platform default location.
    bool Start(const std::string& db_path = std::string());
    void Stop();

    // The listener must outlive the engine, or be cleared before it dies.
    void SetListener(EngineListener* l);

    // Storage is safe to read from the UI thread for list population; writes
    // go through the engine so they stay ordered with protocol traffic.
    Storage& Store() { return fStore; }

    // ── Connection ──
    std::vector<TransportDevice> EnumerateSerial();
    void Connect(TransportKind kind, const std::string& device_path);
    void Disconnect();
    ConnectionState State() const;
    DeviceInfo GetDeviceInfo();  // thread-safe snapshot

    // ── Messaging ──
    void SendMessage(const std::string& contact_key_hex, const std::string& text);
    void SendChannelMessage(int channel_index, const std::string& text);
    void SendCliCommand(const std::string& contact_key_hex, const std::string& command);
    void RunLocalCli(const std::string& command);
    void SyncMessages();

    // ── Contacts ──
    void RefreshContacts();
    void AddContact(const Contact& c);
    void RemoveContact(const std::string& contact_key_hex);
    void ResetPath(const std::string& contact_key_hex);
    void ShareContact(const std::string& contact_key_hex);
    void RequestPathDiscovery(const std::string& contact_key_hex);
    void SendAdvert(bool flood);
    void SendDiscoverRequest();
    void ImportDiscovered(const std::string& public_key_hex);

    // ── Channels ──
    void RefreshChannels();
    void SetChannel(int index, const std::string& name, const ByteBuffer& psk);
    void ClearChannel(int index);

    // ── Rooms and repeaters ──
    void Login(const std::string& contact_key_hex, const std::string& password);
    void Logout(const std::string& contact_key_hex);
    void RequestStatus(const std::string& contact_key_hex);
    void RequestTelemetry(const std::string& contact_key_hex);
    void RequestNeighbors(const std::string& contact_key_hex);

    // ── Device settings ──
    void SetDeviceName(const std::string& name);
    void SetDeviceTime(uint32_t timestamp);
    void SetRadioParams(uint32_t freq_khz, uint32_t bw_khz, uint8_t sf, uint8_t cr);
    void SetTxPower(int dbm);
    void SetDeviceLocation(double lat, double lon);
    void SetAutoAddConfig(bool chat, bool repeater, bool room, bool sensor, bool overwrite,
        uint8_t max_hops);
    void SetOtherParams(uint8_t manual_add, uint8_t telemetry, uint8_t loc_policy,
        uint8_t multi_acks);
    void RequestStats(uint8_t sub_type);
    void RequestBattery();
    void Reboot();

    // Sends an already-built protocol frame; for views that drive the device
    // directly (the raw CLI console).
    void SendRawFrame(const ByteBuffer& frame);

private:
    Engine(const Engine&);
    Engine& operator=(const Engine&);

    // TransportListener — called on the transport's reader thread; each one
    // just hands the work to the engine thread.
    virtual void OnTransportState(ConnectionState state);
    virtual void OnTransportFrame(const ByteBuffer& payload);
    virtual void OnTransportError(const std::string& message);

    typedef std::function<void()> Task;
    void _Post(const Task& t);
    // Runs `t` after `delay_ms`; returns a handle that can be cancelled.
    int64_t _PostDelayed(int64_t delay_ms, const Task& t);
    void _Cancel(int64_t handle);
    void _WorkerLoop();

    // ── Frame dispatch (engine thread) ──
    void _HandleFrame(const ByteBuffer& data);
    void _HandleOk();
    void _HandleError(const ByteBuffer& d);
    void _HandleSelfInfo(const ByteBuffer& d);
    void _HandleDeviceInfo(const ByteBuffer& d);
    void _HandleContactsStart(const ByteBuffer& d);
    void _HandleContact(const ByteBuffer& d);
    void _HandleEndOfContacts();
    void _HandleContactMessage(const ByteBuffer& d);
    void _HandleChannelMessage(const ByteBuffer& d);
    void _HandleChannelInfo(const ByteBuffer& d);
    void _HandleNoMoreMessages();
    void _HandleMsgWaiting();
    void _HandleSent(const ByteBuffer& d);
    void _HandleSendConfirmed(const ByteBuffer& d);
    void _HandleAdvert(const ByteBuffer& d);
    void _HandlePathUpdated(const ByteBuffer& d);
    void _HandleLogData(const ByteBuffer& d);
    void _HandleBattery(const ByteBuffer& d);
    void _HandleStats(const ByteBuffer& d);
    void _HandleCliReply(const ByteBuffer& d);
    void _HandleLoginResult(bool success, const ByteBuffer& d);
    void _HandleStatusResponse(const ByteBuffer& d);
    void _HandleTelemetry(const ByteBuffer& d);
    void _HandleTrace(const ByteBuffer& d);
    void _HandleContactDeleted(const ByteBuffer& d);
    void _HandleCurrTime(const ByteBuffer& d);

    // ── Sync state machine (engine thread) ──
    void _StartInitialSync();
    void _SyncNextChannel();
    void _FinishChannelSync();
    void _SendSyncNextMessage();
    void _FinishMessageSync();
    void _EmitSyncProgress();
    void _ScheduleSyncWatchdog();

    // ── Sending (engine thread) ──
    void _DoSendMessage(int64_t row_id);
    void _OnSendTimeout(int64_t row_id);
    void _RetryOrFail(int64_t row_id);

    bool _Send(const ByteBuffer& frame);
    void _Toast(const std::string& text);
    void _Error(const std::string& text);
    EngineListener* _Listener() const;
    // Resolves a 6-byte wire prefix to a stored contact key, or "" if unknown.
    std::string _ResolvePrefix(const std::string& prefix_hex);

    // Tracks an outgoing message awaiting its ack.
    struct PendingSend {
        int64_t row_id = 0;
        std::string contact_key_hex;
        std::string text;
        uint32_t ack_code = 0;   // first 4 bytes of the expected ack
        bool has_ack = false;
        int attempt = 0;
        int64_t timeout_handle = 0;
        double sent_at = 0.0;
    };

    Storage fStore;
    SerialTransport fSerial;
    TcpTransport fTcp;
    FrameTransport* fActive;  // points at fSerial or fTcp, or NULL

    EngineListener* fListener;
    mutable std::mutex fListenerLock;

    // Worker thread plumbing.
    std::thread fThread;
    bool fRunning;
    std::mutex fQueueLock;
    std::condition_variable fQueueCv;
    std::deque<Task> fQueue;
    struct TimerEntry {
        int64_t due_ms;
        int64_t handle;
        Task task;
    };
    std::vector<TimerEntry> fTimers;
    int64_t fNextTimerHandle;
    std::set<int64_t> fCancelled;

    // ── Engine-thread state ──
    DeviceInfo fDevice;
    mutable std::mutex fDeviceLock;  // only because the UI snapshots it
    SyncProgress fSync;
    bool fSyncing = false;
    bool fContactsIterBusy = false;
    std::vector<ContactFrame> fPendingContacts;
    int fChannelSyncIndex = 0;
    int fMaxChannels = 8;
    bool fMsgSyncActive = false;
    int fMsgSyncRetries = 0;
    int64_t fSyncWatchdog = 0;
    std::map<int64_t, PendingSend> fPending;      // row id -> in-flight send
    std::map<uint32_t, int64_t> fAckIndex;        // ack code -> row id
    std::string fLoginTarget;                     // contact awaiting a login reply
    // Channel index by its 1-byte hash, for matching RX-log group text.
    std::map<std::string, int> fChannelByHash;
};

}  // namespace meshy

#endif  // MESHY_CORE_ENGINE_H
