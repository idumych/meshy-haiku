// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Transport framing tests, driven over a loopback TCP server so the real
// reader thread, resynchronisation and partial-read paths are exercised.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Transport.h"

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

// Collects frames and state changes from the transport's reader thread.
class Collector : public TransportListener {
public:
    virtual void OnTransportState(ConnectionState state)
    {
        std::lock_guard<std::mutex> g(fLock);
        fStates.push_back(state);
        fCv.notify_all();
    }

    virtual void OnTransportFrame(const ByteBuffer& payload)
    {
        std::lock_guard<std::mutex> g(fLock);
        fFrames.push_back(ToHex(payload));
        fCv.notify_all();
    }

    virtual void OnTransportError(const std::string& message)
    {
        std::lock_guard<std::mutex> g(fLock);
        fErrors.push_back(message);
    }

    bool WaitForFrames(size_t n, int timeout_ms = 3000)
    {
        std::unique_lock<std::mutex> g(fLock);
        return fCv.wait_for(g, std::chrono::milliseconds(timeout_ms),
            [&] { return fFrames.size() >= n; });
    }

    bool WaitForState(ConnectionState s, int timeout_ms = 3000)
    {
        std::unique_lock<std::mutex> g(fLock);
        return fCv.wait_for(g, std::chrono::milliseconds(timeout_ms), [&] {
            for (size_t i = 0; i < fStates.size(); i++)
                if (fStates[i] == s)
                    return true;
            return false;
        });
    }

    std::vector<std::string> Frames()
    {
        std::lock_guard<std::mutex> g(fLock);
        return fFrames;
    }

private:
    std::mutex fLock;
    std::condition_variable fCv;
    std::vector<std::string> fFrames;
    std::vector<ConnectionState> fStates;
    std::vector<std::string> fErrors;
};

// Minimal loopback server: accepts one connection and exposes it.
class LoopbackServer {
public:
    LoopbackServer() : fListen(-1), fConn(-1), fPort(0) {}

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
        addr.sin_port = 0;  // ephemeral
        if (bind(fListen, (struct sockaddr*)&addr, sizeof(addr)) != 0)
            return false;
        if (listen(fListen, 1) != 0)
            return false;
        socklen_t len = sizeof(addr);
        if (getsockname(fListen, (struct sockaddr*)&addr, &len) != 0)
            return false;
        fPort = ntohs(addr.sin_port);
        return true;
    }

    void AcceptOne() { fConn = accept(fListen, NULL, NULL); }
    int Port() const { return fPort; }
    bool Send(const uint8_t* d, size_t n) { return fConn >= 0 && write(fConn, d, n) == (ssize_t)n; }
    bool Send(const ByteBuffer& b) { return Send(b.data(), b.size()); }

    // Reads exactly n bytes with a simple deadline.
    ByteBuffer Recv(size_t n, int timeout_ms = 3000)
    {
        ByteBuffer out;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        uint8_t buf[256];
        while (out.size() < n && std::chrono::steady_clock::now() < deadline) {
            ssize_t r = read(fConn, buf, n - out.size() < sizeof(buf) ? n - out.size() : sizeof(buf));
            if (r > 0)
                out.insert(out.end(), buf, buf + r);
            else if (r < 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            else
                break;
        }
        return out;
    }

    void CloseConn()
    {
        if (fConn >= 0) {
            close(fConn);
            fConn = -1;
        }
    }

    ~LoopbackServer()
    {
        CloseConn();
        if (fListen >= 0)
            close(fListen);
    }

private:
    int fListen;
    int fConn;
    int fPort;
};

// Wraps a payload in the device->host framing.
static ByteBuffer RxFrame(const ByteBuffer& payload)
{
    ByteBuffer f;
    f.push_back(RX_MARKER);
    f.push_back(static_cast<uint8_t>(payload.size() & 0xFF));
    f.push_back(static_cast<uint8_t>((payload.size() >> 8) & 0xFF));
    f.insert(f.end(), payload.begin(), payload.end());
    return f;
}

int main()
{
    LoopbackServer server;
    if (!server.Start()) {
        printf("FAIL could not start loopback server\n");
        return 1;
    }

    Collector collector;
    TcpTransport tcp;
    tcp.SetListener(&collector);

    char target[64];
    snprintf(target, sizeof(target), "127.0.0.1:%d", server.Port());

    std::thread acceptor([&] { server.AcceptOne(); });
    bool connected = tcp.Connect(target);
    acceptor.join();

    CheckTrue("connect", connected);
    CheckTrue("state connected", tcp.IsConnected());
    Check("connected address", tcp.ConnectedAddress(), target);

    // ── Host -> device framing ──
    ByteBuffer payload = FromHexOrEmpty("0104");  // CMD_GET_CONTACTS-ish
    CheckTrue("send", tcp.SendFrame(payload));
    ByteBuffer wire = server.Recv(3 + payload.size());
    Check("tx framing", ToHex(wire), "3c0200" "0104");

    // A frame over the TX limit must be refused, not truncated.
    CheckTrue("oversized refused", !tcp.SendFrame(ByteBuffer(MAX_TX_PAYLOAD + 1, 0x41)));

    // ── Device -> host framing ──
    server.Send(RxFrame(FromHexOrEmpty("050102030405")));
    CheckTrue("rx one frame", collector.WaitForFrames(1));
    Check("rx payload", collector.Frames()[0], "050102030405");

    // Two frames in a single write must both be delivered.
    ByteBuffer both = RxFrame(FromHexOrEmpty("0601"));
    ByteBuffer second = RxFrame(FromHexOrEmpty("0702"));
    both.insert(both.end(), second.begin(), second.end());
    server.Send(both);
    CheckTrue("rx coalesced", collector.WaitForFrames(3));
    Check("rx coalesced 1", collector.Frames()[1], "0601");
    Check("rx coalesced 2", collector.Frames()[2], "0702");

    // A frame split across writes must be reassembled.
    ByteBuffer split = RxFrame(FromHexOrEmpty("08aabbccdd"));
    server.Send(split.data(), 2);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    server.Send(split.data() + 2, split.size() - 2);
    CheckTrue("rx split", collector.WaitForFrames(4));
    Check("rx split payload", collector.Frames()[3], "08aabbccdd");

    // Garbage before a marker must be skipped, not corrupt the next frame.
    ByteBuffer noisy = FromHexOrEmpty("00112233");
    ByteBuffer good = RxFrame(FromHexOrEmpty("0999"));
    noisy.insert(noisy.end(), good.begin(), good.end());
    server.Send(noisy);
    CheckTrue("rx after noise", collector.WaitForFrames(5));
    Check("rx resync", collector.Frames()[4], "0999");

    // A bogus oversized length must not wedge the parser.
    ByteBuffer bogus;
    bogus.push_back(RX_MARKER);
    bogus.push_back(0xFF);
    bogus.push_back(0xFF);  // length 65535 > MAX_RX_PAYLOAD
    ByteBuffer after = RxFrame(FromHexOrEmpty("0a77"));
    bogus.insert(bogus.end(), after.begin(), after.end());
    server.Send(bogus);
    CheckTrue("rx after bogus length", collector.WaitForFrames(6));
    Check("rx recovered", collector.Frames()[5], "0a77");

    // A zero-length frame is valid and delivers an empty payload.
    server.Send(RxFrame(ByteBuffer()));
    CheckTrue("rx empty frame", collector.WaitForFrames(7));
    Check("rx empty payload", collector.Frames()[6], "");

    // ── Peer close must surface as a disconnect ──
    server.CloseConn();
    CheckTrue("disconnect detected", collector.WaitForState(CONN_DISCONNECTED));
    CheckTrue("not connected", !tcp.IsConnected());
    CheckTrue("send after close fails", !tcp.SendFrame(payload));

    tcp.Disconnect();  // must be safe when already disconnected

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
