// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Frame-oriented MeshCore transports (USB serial, TCP), ported from upstream
// Meshy src/transport_base.py, usb_serial.py and tcp.py.
//
// Wire framing: start_marker(1) + length_le(2) + payload(N)
//   Host  -> Device: 0x3C + length + payload
//   Device -> Host:  0x3E + length + payload
//
// Threading: each transport runs a reader thread.  Listener callbacks are
// invoked on that thread, so an implementation must either be thread-safe or
// marshal to its own loop (the Haiku layer posts a BMessage).

#ifndef MESHY_CORE_TRANSPORT_H
#define MESHY_CORE_TRANSPORT_H

#include <stdint.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Bytes.h"

namespace meshy {

static const uint8_t TX_MARKER = 0x3C;  // host -> device
static const uint8_t RX_MARKER = 0x3E;  // device -> host
static const size_t FRAME_HEADER_SIZE = 3;
static const size_t MAX_RX_PAYLOAD = 176;  // firmware 1.16+ may send up to 176
static const size_t MAX_TX_PAYLOAD = 172;  // stay compatible with older firmware

enum ConnectionState {
    CONN_DISCONNECTED = 1,
    CONN_SCANNING = 2,
    CONN_CONNECTING = 3,
    CONN_CONNECTED = 4,
    CONN_DISCONNECTING = 5,
};

const char* ConnectionStateName(ConnectionState s);

struct TransportDevice {
    std::string path;  // /dev/ports/usb0, or "host:port" for TCP
    std::string name;
};

class TransportListener {
public:
    virtual ~TransportListener() {}
    virtual void OnTransportState(ConnectionState state) = 0;
    virtual void OnTransportFrame(const ByteBuffer& payload) = 0;
    // Human-readable, already-logged diagnostics for the UI.
    virtual void OnTransportError(const std::string& message) { (void)message; }
};

class FrameTransport {
public:
    FrameTransport();
    virtual ~FrameTransport();

    // The listener must outlive the transport, or be cleared first.
    void SetListener(TransportListener* l);

    virtual bool Connect(const std::string& device_path) = 0;
    virtual void Disconnect();
    virtual std::vector<TransportDevice> Enumerate() { return std::vector<TransportDevice>(); }

    // Wraps a protocol frame in the wire framing and writes it.  Returns false
    // when not connected or the frame exceeds MAX_TX_PAYLOAD.
    bool SendFrame(const ByteBuffer& data);

    ConnectionState State() const { return fState.load(); }
    bool IsConnected() const { return fState.load() == CONN_CONNECTED; }
    std::string ConnectedAddress() const;
    virtual const char* Label() const { return "Transport"; }

protected:
    // Subclass hooks.
    virtual bool WriteBytes(const uint8_t* data, size_t len) = 0;
    virtual void CloseIo() = 0;
    // Reads into buf; returns bytes read, 0 on timeout, -1 on error/EOF.
    virtual int ReadBytes(uint8_t* buf, size_t len) = 0;
    // TCP also accepts the TX marker, because some bridges echo it.
    virtual bool IsRxMarker(uint8_t b) const { return b == RX_MARKER; }

    void SetState(ConnectionState state);
    void SetConnectedAddress(const std::string& addr);
    void StartReadThread();
    void StopReadThread();
    void ReportError(const std::string& message);
    // Called from the reader thread when the link drops.
    void HandleDisconnect();

    std::atomic<bool> fRunning;

private:
    void _ReadLoop();
    void _ProcessRxBuffer();
    // Snapshot of the listener; callbacks are invoked with no lock held.
    TransportListener* _Listener() const;

    std::atomic<ConnectionState> fState;
    std::string fConnectedAddress;
    mutable std::mutex fAddrLock;
    TransportListener* fListener;
    mutable std::mutex fListenerLock;
    std::thread fReadThread;
    ByteBuffer fRxBuffer;
};

// ─── USB / UART serial ───────────────────────────────────────────

class SerialTransport : public FrameTransport {
public:
    SerialTransport();
    virtual ~SerialTransport();

    virtual bool Connect(const std::string& device_path);
    virtual std::vector<TransportDevice> Enumerate();
    virtual const char* Label() const { return "Serial"; }

    static const int kBaudRate = 115200;

protected:
    virtual bool WriteBytes(const uint8_t* data, size_t len);
    virtual void CloseIo();
    virtual int ReadBytes(uint8_t* buf, size_t len);

private:
    int fFd;
    std::mutex fIoLock;
};

// ─── TCP ─────────────────────────────────────────────────────────

class TcpTransport : public FrameTransport {
public:
    TcpTransport();
    virtual ~TcpTransport();

    // device_path is "host" or "host:port"; the default port is 5000.
    virtual bool Connect(const std::string& device_path);
    virtual const char* Label() const { return "TCP"; }

    static const int kDefaultPort = 5000;

protected:
    virtual bool WriteBytes(const uint8_t* data, size_t len);
    virtual void CloseIo();
    virtual int ReadBytes(uint8_t* buf, size_t len);
    // Some TCP bridges echo the host marker back, so accept both.
    virtual bool IsRxMarker(uint8_t b) const { return b == RX_MARKER || b == TX_MARKER; }

private:
    int fSocket;
    std::mutex fIoLock;
};

}  // namespace meshy

#endif  // MESHY_CORE_TRANSPORT_H
