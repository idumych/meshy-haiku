// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// End-to-end engine test against a mock MeshCore companion that speaks the
// real wire protocol over loopback TCP.  Exercises the connect handshake,
// contact and channel sync, the message-sync loop, and the send/ack path
// including retry and failure.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Engine.h"

using namespace meshy;

static int g_failures = 0;
static int g_checks = 0;

static void Check(const std::string& name, const std::string& got, const std::string& want)
{
    g_checks++;
    if (got != want) {
        g_failures++;
        printf("FAIL %s\n  got  %s\n  want %s\n", name.c_str(), got.c_str(), want.c_str());
    }
}

static void CheckTrue(const std::string& name, bool cond)
{
    g_checks++;
    if (!cond) {
        g_failures++;
        printf("FAIL %s\n", name.c_str());
    }
}

static std::string Num(long long v)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", v);
    return buf;
}

// ─── Mock companion device ───────────────────────────────────────

class MockDevice {
public:
    MockDevice()
        : fListen(-1), fConn(-1), fPort(0), fRunning(false), fAckOutgoing(true), fSentCount(0)
    {
    }

    bool Start()
    {
        fListen = socket(AF_INET, SOCK_STREAM, 0);
        if (fListen < 0)
            return false;
        int one = 1;
        setsockopt(fListen, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (bind(fListen, (struct sockaddr*)&addr, sizeof(addr)) != 0)
            return false;
        if (listen(fListen, 1) != 0)
            return false;
        socklen_t len = sizeof(addr);
        getsockname(fListen, (struct sockaddr*)&addr, &len);
        fPort = ntohs(addr.sin_port);
        fRunning = true;
        fThread = std::thread(&MockDevice::_Serve, this);
        return true;
    }

    void Stop()
    {
        fRunning = false;
        if (fConn >= 0) {
            shutdown(fConn, SHUT_RDWR);
            close(fConn);
            fConn = -1;
        }
        if (fListen >= 0) {
            close(fListen);
            fListen = -1;
        }
        if (fThread.joinable())
            fThread.join();
    }

    int Port() const { return fPort; }
    void SetAckOutgoing(bool v) { fAckOutgoing = v; }
    int SentCount() const { return fSentCount.load(); }

    // Queues an unsolicited push (an incoming message, say).
    void Push(const ByteBuffer& payload)
    {
        std::lock_guard<std::mutex> g(fLock);
        fPushQueue.push_back(payload);
    }

    std::vector<uint8_t> ReceivedCommands()
    {
        std::lock_guard<std::mutex> g(fLock);
        return fCommands;
    }

private:
    void _Send(const ByteBuffer& payload)
    {
        if (fConn < 0)
            return;
        ByteBuffer f;
        f.push_back(RX_MARKER);
        f.push_back(static_cast<uint8_t>(payload.size() & 0xFF));
        f.push_back(static_cast<uint8_t>((payload.size() >> 8) & 0xFF));
        f.insert(f.end(), payload.begin(), payload.end());
        ssize_t rc = write(fConn, f.data(), f.size());
        (void)rc;
    }

    // The two contacts this mock device knows about.
    static std::string KeyA()
    {
        return "aa00000000000000000000000000000000000000000000000000000000000001";
    }
    static std::string KeyB()
    {
        return "bb00000000000000000000000000000000000000000000000000000000000002";
    }

    ByteBuffer _ContactFrame(const std::string& key_hex, const std::string& name, uint8_t type)
    {
        ByteWriter w;
        w.U8(RESP_CODE_CONTACT);
        w.Bytes(FromHexOrEmpty(key_hex));
        w.U8(type);
        w.U8(0);
        w.U8(EncodePathLen(1, 1));
        ByteBuffer path(64, 0);
        path[0] = 0x7F;
        w.Bytes(path);
        w.StrTruncated(name, 31, 32);
        w.U32(1700000000u);
        w.I32(50100000);
        w.I32(14400000);
        w.U32(1700000001u);
        return w.Take();
    }

    void _HandleCommand(const ByteBuffer& cmd)
    {
        if (cmd.empty())
            return;
        {
            std::lock_guard<std::mutex> g(fLock);
            fCommands.push_back(cmd[0]);
        }

        switch (cmd[0]) {
            case CMD_APP_START: {
                // Firmware answers APP_START with SELF_INFO.
                ByteWriter w;
                w.U8(RESP_CODE_SELF_INFO);
                w.U8(ADV_TYPE_CHAT);
                w.U8(20);
                w.U8(22);
                w.Bytes(FromHexOrEmpty(
                    "cc00000000000000000000000000000000000000000000000000000000000003"));
                w.I32(50000000);
                w.I32(14000000);
                w.U8(0);
                w.U8(0);
                w.U8(0);
                w.U8(0);
                w.U32(869525000u);
                w.U32(250000u);
                w.U8(11);
                w.U8(5);
                w.Str("mocknode");
                _Send(w.Take());
                break;
            }
            case CMD_DEVICE_QUERY: {
                ByteWriter w;
                w.U8(RESP_CODE_DEVICE_INFO);
                w.U8(10);   // fw_ver
                w.U8(50);   // max_contacts / 2
                w.U8(2);    // max_channels — keeps the channel sync short
                w.U32(123456);
                w.StrTruncated("build1", 12, 12);
                w.StrTruncated("MockRadio", 40, 40);
                w.StrTruncated("1.8.0", 20, 20);
                w.U8(0);
                w.U8(1);
                _Send(w.Take());
                break;
            }
            case CMD_GET_DEVICE_TIME: {
                ByteWriter w;
                w.U8(RESP_CODE_CURR_TIME);
                w.U32(static_cast<uint32_t>(time(NULL)));
                _Send(w.Take());
                break;
            }
            case CMD_GET_BATT_AND_STORAGE: {
                ByteWriter w;
                w.U8(RESP_CODE_BATT_AND_STORAGE);
                w.U16(4100);
                w.U32(1024);
                w.U32(8192);
                _Send(w.Take());
                break;
            }
            case CMD_GET_CUSTOM_VARS: {
                ByteWriter w;
                w.U8(RESP_CODE_CUSTOM_VARS);
                w.Str("gps:1");
                _Send(w.Take());
                break;
            }
            case CMD_GET_CONTACTS: {
                ByteWriter start;
                start.U8(RESP_CODE_CONTACTS_START);
                start.U32(2);
                _Send(start.Take());
                _Send(_ContactFrame(KeyA(), "alice", ADV_TYPE_CHAT));
                _Send(_ContactFrame(KeyB(), "repeater1", ADV_TYPE_REPEATER));
                ByteBuffer end;
                end.push_back(RESP_CODE_END_OF_CONTACTS);
                _Send(end);
                break;
            }
            case CMD_GET_CHANNEL: {
                uint8_t idx = cmd.size() > 1 ? cmd[1] : 0;
                ByteWriter w;
                w.U8(RESP_CODE_CHANNEL_INFO);
                w.U8(idx);
                if (idx == 0) {
                    w.StrTruncated("general", 32, 32);
                    w.Bytes(FromHexOrEmpty(Channel::kPublicChannelPsk));
                } else {
                    w.StrTruncated("", 32, 32);  // empty slot
                    w.Bytes(ByteBuffer(16, 0));
                }
                _Send(w.Take());
                break;
            }
            case CMD_SYNC_NEXT_MESSAGE: {
                ByteBuffer queued;
                {
                    std::lock_guard<std::mutex> g(fLock);
                    if (!fPushQueue.empty()) {
                        queued = fPushQueue.front();
                        fPushQueue.erase(fPushQueue.begin());
                    }
                }
                if (!queued.empty()) {
                    _Send(queued);
                } else {
                    ByteBuffer none;
                    none.push_back(RESP_CODE_NO_MORE_MESSAGES);
                    _Send(none);
                }
                break;
            }
            case CMD_SEND_TXT_MSG: {
                fSentCount++;
                // Answer with MSG_SENT carrying an ack code derived from the
                // frame, then (optionally) the matching ACK push.
                ByteBuffer ack = FromHexOrEmpty("11223344");
                ByteWriter w;
                w.U8(RESP_CODE_SENT);
                w.U8(0);
                w.Bytes(ack);
                w.U32(3000);
                _Send(w.Take());
                if (fAckOutgoing) {
                    ByteWriter a;
                    a.U8(PUSH_CODE_SEND_CONFIRMED);
                    a.Bytes(ack);
                    a.U32(450);
                    _Send(a.Take());
                }
                break;
            }
            case CMD_SEND_CHANNEL_TXT_MSG: {
                ByteBuffer ok;
                ok.push_back(RESP_CODE_OK);
                _Send(ok);
                break;
            }
            default: {
                ByteBuffer ok;
                ok.push_back(RESP_CODE_OK);
                _Send(ok);
                break;
            }
        }
    }

    void _Serve()
    {
        fConn = accept(fListen, NULL, NULL);
        if (fConn < 0)
            return;
        ByteBuffer rx;
        uint8_t buf[512];
        while (fRunning) {
            ssize_t n = read(fConn, buf, sizeof(buf));
            if (n <= 0)
                break;
            rx.insert(rx.end(), buf, buf + n);
            while (rx.size() >= FRAME_HEADER_SIZE) {
                if (rx[0] != TX_MARKER) {
                    rx.erase(rx.begin());
                    continue;
                }
                size_t len = rx[1] | (static_cast<size_t>(rx[2]) << 8);
                if (rx.size() < FRAME_HEADER_SIZE + len)
                    break;
                ByteBuffer payload(rx.begin() + FRAME_HEADER_SIZE,
                    rx.begin() + FRAME_HEADER_SIZE + len);
                rx.erase(rx.begin(), rx.begin() + FRAME_HEADER_SIZE + len);
                _HandleCommand(payload);
            }
        }
    }

    int fListen;
    int fConn;
    int fPort;
    std::atomic<bool> fRunning;
    std::atomic<bool> fAckOutgoing;
    std::atomic<int> fSentCount;
    std::thread fThread;
    std::mutex fLock;
    std::vector<ByteBuffer> fPushQueue;
    std::vector<uint8_t> fCommands;
};

// ─── Listener that records what the UI would see ─────────────────

class Recorder : public EngineListener {
public:
    virtual void OnConnectionState(ConnectionState s)
    {
        std::lock_guard<std::mutex> g(fLock);
        fState = s;
        fCv.notify_all();
    }
    virtual void OnSyncProgress(const SyncProgress& p)
    {
        std::lock_guard<std::mutex> g(fLock);
        fSyncActive = p.active;
        fCv.notify_all();
    }
    virtual void OnContactsChanged()
    {
        std::lock_guard<std::mutex> g(fLock);
        fContactEvents++;
        fCv.notify_all();
    }
    virtual void OnChannelsChanged()
    {
        std::lock_guard<std::mutex> g(fLock);
        fChannelEvents++;
        fCv.notify_all();
    }
    virtual void OnMessage(const std::string& key, int64_t id)
    {
        std::lock_guard<std::mutex> g(fLock);
        fMessages.push_back(std::make_pair(key, id));
        fCv.notify_all();
    }
    virtual void OnMessageStatus(int64_t id, int status)
    {
        std::lock_guard<std::mutex> g(fLock);
        fStatuses[id] = status;
        fCv.notify_all();
    }
    virtual void OnChannelMessage(int idx, int64_t id)
    {
        std::lock_guard<std::mutex> g(fLock);
        fChannelMessages.push_back(std::make_pair(idx, id));
        fCv.notify_all();
    }
    virtual void OnDeviceInfoChanged(const DeviceInfo& info)
    {
        std::lock_guard<std::mutex> g(fLock);
        fDevice = info;
        fCv.notify_all();
    }

    template <typename Pred>
    bool WaitFor(Pred p, int timeout_ms = 5000)
    {
        std::unique_lock<std::mutex> g(fLock);
        return fCv.wait_for(g, std::chrono::milliseconds(timeout_ms), p);
    }

    std::mutex fLock;
    std::condition_variable fCv;
    ConnectionState fState = CONN_DISCONNECTED;
    bool fSyncActive = false;
    int fContactEvents = 0;
    int fChannelEvents = 0;
    std::vector<std::pair<std::string, int64_t> > fMessages;
    std::vector<std::pair<int, int64_t> > fChannelMessages;
    std::map<int64_t, int> fStatuses;
    DeviceInfo fDevice;
};

static ByteBuffer IncomingDm(const std::string& key_hex, const std::string& text)
{
    ByteWriter w;
    w.U8(RESP_CODE_CONTACT_MSG_RECV_V3);
    w.U8(static_cast<uint8_t>(-20));  // SNR
    w.U16(0);
    w.Bytes(FromHexOrEmpty(key_hex.substr(0, 12)));  // 6-byte prefix
    w.U8(EncodePathLen(1, 1));
    w.U8(TXT_TYPE_PLAIN);
    w.U32(1700000500u);
    w.StrNul(text);
    return w.Take();
}

static ByteBuffer IncomingChannelMsg(uint8_t idx, const std::string& body)
{
    ByteWriter w;
    w.U8(RESP_CODE_CHANNEL_MSG_RECV_V3);
    w.U8(static_cast<uint8_t>(-18));
    w.U16(0);
    w.U8(idx);
    w.U8(EncodePathLen(2, 1));
    w.U8(TXT_TYPE_PLAIN);
    w.U32(1700000600u);
    w.StrNul(body);
    return w.Take();
}

int main()
{
    const char* db = "/tmp/meshy_engine_test.db";
    remove(db);

    MockDevice device;
    if (!device.Start()) {
        printf("FAIL mock device did not start\n");
        return 1;
    }

    // Queue two incoming messages for the sync loop to drain.
    device.Push(IncomingDm("aa00000000000000000000000000000000000000000000000000000000000001",
        "hello from alice"));
    device.Push(IncomingChannelMsg(0, "alice: hi channel"));

    Engine engine;
    Recorder rec;
    engine.SetListener(&rec);
    CheckTrue("engine start", engine.Start(db));

    char target[64];
    snprintf(target, sizeof(target), "127.0.0.1:%d", device.Port());
    engine.Connect(TRANSPORT_TCP, target);

    CheckTrue("connected", rec.WaitFor([&] { return rec.fState == CONN_CONNECTED; }));

    // The full handshake should land: self info, device info, contacts,
    // channels, then the message queue drains and sync finishes.
    CheckTrue("device info", rec.WaitFor([&] { return !rec.fDevice.model.empty(); }));
    Check("model", rec.fDevice.model, "MockRadio");
    Check("device name", rec.fDevice.name, "mocknode");
    Check("fw", Num(rec.fDevice.fw_ver), "10");

    CheckTrue("contacts synced", rec.WaitFor([&] { return rec.fContactEvents > 0; }));
    std::vector<Contact> contacts = engine.Store().GetContacts();
    Check("contact count", Num(static_cast<long long>(contacts.size())), "2");

    CheckTrue("channels synced", rec.WaitFor([&] { return rec.fChannelEvents > 0; }));
    std::vector<Channel> channels = engine.Store().GetChannels();
    // Slot 1 is empty on the mock, so only the named channel is stored.
    Check("channel count", Num(static_cast<long long>(channels.size())), "1");
    Check("channel name", channels[0].name, "general");

    // Queued messages arrive through the sync loop.
    CheckTrue("incoming dm", rec.WaitFor([&] { return rec.fMessages.size() >= 1; }));
    CheckTrue("incoming channel msg", rec.WaitFor([&] { return rec.fChannelMessages.size() >= 1; }));
    CheckTrue("sync finished", rec.WaitFor([&] { return !rec.fSyncActive; }));

    std::string alice = "aa00000000000000000000000000000000000000000000000000000000000001";
    std::vector<Message> msgs = engine.Store().GetMessages(alice);
    Check("stored dm count", Num(static_cast<long long>(msgs.size())), "1");
    Check("stored dm text", msgs[0].text, "hello from alice");
    Check("dm unread", Num(engine.Store().UnreadCount(alice)), "1");

    std::vector<ChannelMessage> cms = engine.Store().GetChannelMessages(0);
    Check("stored channel msg", Num(static_cast<long long>(cms.size())), "1");
    Check("channel sender", cms[0].sender_name, "alice");
    Check("channel body", cms[0].text, "hi channel");

    // ── Outgoing message: pending -> sent -> delivered ──
    size_t before = rec.fMessages.size();
    engine.SendMessage(alice, "hello back");
    CheckTrue("outgoing stored", rec.WaitFor([&] { return rec.fMessages.size() > before; }));
    int64_t out_id = rec.fMessages.back().second;
    CheckTrue("delivered", rec.WaitFor([&] {
        return rec.fStatuses.count(out_id) && rec.fStatuses[out_id] == MSG_DELIVERED;
    }));

    msgs = engine.Store().GetMessages(alice);
    Check("dm count after send", Num(static_cast<long long>(msgs.size())), "2");
    Check("outgoing text", msgs[1].text, "hello back");
    CheckTrue("outgoing is outgoing", msgs[1].is_outgoing);
    Check("outgoing status", Num(msgs[1].status), Num(MSG_DELIVERED));

    // A successful send records the route that worked.
    std::vector<PathHistoryEntry> hist = engine.Store().GetPathHistory(alice);
    CheckTrue("path recorded", hist.size() == 1 && hist[0].success_count == 1);

    // ── Channel send ──
    engine.SendChannelMessage(0, "from haiku");
    CheckTrue("channel sent", rec.WaitFor([&] { return rec.fChannelMessages.size() >= 2; }));
    cms = engine.Store().GetChannelMessages(0);
    Check("channel msgs after send", Num(static_cast<long long>(cms.size())), "2");
    CheckTrue("channel outgoing", cms[1].is_outgoing);

    // ── Unacked send must retry, then fail ──
    device.SetAckOutgoing(false);
    int sent_before = device.SentCount();
    size_t msgs_before = rec.fMessages.size();
    engine.SendMessage(alice, "no ack expected");
    CheckTrue("second outgoing stored",
        rec.WaitFor([&] { return rec.fMessages.size() > msgs_before; }));
    int64_t fail_id = rec.fMessages.back().second;
    CheckTrue("eventually failed", rec.WaitFor(
        [&] {
            return rec.fStatuses.count(fail_id) && rec.fStatuses[fail_id] == MSG_FAILED;
        },
        40000));
    CheckTrue("retried before failing", device.SentCount() >= sent_before + 2);

    // ── Disconnect ──
    engine.Disconnect();
    CheckTrue("disconnected", rec.WaitFor([&] { return rec.fState == CONN_DISCONNECTED; }));

    engine.SetListener(NULL);
    engine.Stop();
    device.Stop();

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
