// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Transport.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include <cstdlib>

namespace meshy {

const char* ConnectionStateName(ConnectionState s)
{
    switch (s) {
        case CONN_DISCONNECTED: return "DISCONNECTED";
        case CONN_SCANNING: return "SCANNING";
        case CONN_CONNECTING: return "CONNECTING";
        case CONN_CONNECTED: return "CONNECTED";
        case CONN_DISCONNECTING: return "DISCONNECTING";
    }
    return "UNKNOWN";
}

// ─── FrameTransport ──────────────────────────────────────────────

FrameTransport::FrameTransport()
    : fRunning(false), fState(CONN_DISCONNECTED), fListener(NULL)
{
}

FrameTransport::~FrameTransport()
{
    // Subclasses close their own handles before reaching here; this is the
    // last line of defence against a reader thread outliving the object.
    StopReadThread();
}

void FrameTransport::SetListener(TransportListener* l)
{
    std::lock_guard<std::mutex> guard(fListenerLock);
    fListener = l;
}

std::string FrameTransport::ConnectedAddress() const
{
    std::lock_guard<std::mutex> guard(fAddrLock);
    return fConnectedAddress;
}

void FrameTransport::SetConnectedAddress(const std::string& addr)
{
    std::lock_guard<std::mutex> guard(fAddrLock);
    fConnectedAddress = addr;
}

// Callbacks run with no transport lock held: a listener is allowed to call
// straight back in (a send that fails during OnTransportFrame reaches
// HandleDisconnect -> SetState), which would deadlock on a held mutex.  The
// listener must outlive the transport, or be cleared before it is destroyed.
TransportListener* FrameTransport::_Listener() const
{
    std::lock_guard<std::mutex> guard(fListenerLock);
    return fListener;
}

void FrameTransport::SetState(ConnectionState state)
{
    ConnectionState old = fState.exchange(state);
    if (old == state)
        return;
    TransportListener* l = _Listener();
    if (l != NULL)
        l->OnTransportState(state);
}

void FrameTransport::ReportError(const std::string& message)
{
    TransportListener* l = _Listener();
    if (l != NULL)
        l->OnTransportError(message);
}

bool FrameTransport::SendFrame(const ByteBuffer& data)
{
    if (fState.load() != CONN_CONNECTED)
        return false;
    if (data.size() > MAX_TX_PAYLOAD) {
        ReportError("frame too large to send");
        return false;
    }

    ByteBuffer frame;
    frame.reserve(FRAME_HEADER_SIZE + data.size());
    frame.push_back(TX_MARKER);
    frame.push_back(static_cast<uint8_t>(data.size() & 0xFF));
    frame.push_back(static_cast<uint8_t>((data.size() >> 8) & 0xFF));
    frame.insert(frame.end(), data.begin(), data.end());

    if (!WriteBytes(frame.data(), frame.size())) {
        ReportError("write failed");
        HandleDisconnect();
        return false;
    }
    return true;
}

void FrameTransport::StartReadThread()
{
    StopReadThread();
    fRunning.store(true);
    fRxBuffer.clear();
    fReadThread = std::thread(&FrameTransport::_ReadLoop, this);
}

void FrameTransport::StopReadThread()
{
    fRunning.store(false);
    if (fReadThread.joinable()) {
        if (std::this_thread::get_id() == fReadThread.get_id())
            fReadThread.detach();  // disconnect requested from inside the loop
        else
            fReadThread.join();
    }
}

void FrameTransport::_ReadLoop()
{
    uint8_t buf[512];
    while (fRunning.load()) {
        int n = ReadBytes(buf, sizeof(buf));
        if (n < 0) {
            if (fRunning.load())
                HandleDisconnect();
            return;
        }
        if (n == 0)
            continue;  // timeout; check fRunning and poll again
        fRxBuffer.insert(fRxBuffer.end(), buf, buf + n);
        _ProcessRxBuffer();
    }
}

void FrameTransport::_ProcessRxBuffer()
{
    while (fRxBuffer.size() >= FRAME_HEADER_SIZE) {
        if (!IsRxMarker(fRxBuffer[0])) {
            // Resynchronise on the next marker, dropping the noise before it.
            size_t idx = 0;
            bool found = false;
            for (size_t i = 0; i < fRxBuffer.size(); i++) {
                if (IsRxMarker(fRxBuffer[i])) {
                    idx = i;
                    found = true;
                    break;
                }
            }
            if (!found) {
                fRxBuffer.clear();
                return;
            }
            fRxBuffer.erase(fRxBuffer.begin(), fRxBuffer.begin() + idx);
            continue;
        }

        size_t payload_len
            = static_cast<size_t>(fRxBuffer[1]) | (static_cast<size_t>(fRxBuffer[2]) << 8);
        if (payload_len > MAX_RX_PAYLOAD) {
            // Not a real header; drop this byte and look for the next marker.
            fRxBuffer.erase(fRxBuffer.begin());
            continue;
        }

        size_t frame_size = FRAME_HEADER_SIZE + payload_len;
        if (fRxBuffer.size() < frame_size)
            return;  // wait for the rest

        ByteBuffer payload(fRxBuffer.begin() + FRAME_HEADER_SIZE, fRxBuffer.begin() + frame_size);
        fRxBuffer.erase(fRxBuffer.begin(), fRxBuffer.begin() + frame_size);

        TransportListener* l = _Listener();
        if (l != NULL)
            l->OnTransportFrame(payload);
    }
}

void FrameTransport::HandleDisconnect()
{
    ConnectionState s = fState.load();
    if (s == CONN_DISCONNECTED || s == CONN_DISCONNECTING)
        return;
    fRunning.store(false);
    CloseIo();
    SetConnectedAddress(std::string());
    SetState(CONN_DISCONNECTED);
}

void FrameTransport::Disconnect()
{
    if (fState.load() == CONN_DISCONNECTED)
        return;
    SetState(CONN_DISCONNECTING);
    fRunning.store(false);
    CloseIo();  // unblocks the reader
    StopReadThread();
    SetConnectedAddress(std::string());
    SetState(CONN_DISCONNECTED);
}

// ─── SerialTransport ─────────────────────────────────────────────

SerialTransport::SerialTransport() : fFd(-1) {}

SerialTransport::~SerialTransport()
{
    Disconnect();
}

std::vector<TransportDevice> SerialTransport::Enumerate()
{
    std::vector<TransportDevice> out;
#ifdef __HAIKU__
    // Haiku publishes serial devices under /dev/ports (usb0, usb1, serial1…).
    static const char* kDirs[] = { "/dev/ports" };
#else
    static const char* kDirs[] = { "/dev" };
#endif
    for (size_t d = 0; d < sizeof(kDirs) / sizeof(kDirs[0]); d++) {
        DIR* dir = opendir(kDirs[d]);
        if (dir == NULL)
            continue;
        struct dirent* ent;
        while ((ent = readdir(dir)) != NULL) {
            std::string name = ent->d_name;
            if (name == "." || name == "..")
                continue;
#ifdef __HAIKU__
            bool match = true;  // everything under /dev/ports is a serial port
#else
            bool match = name.compare(0, 6, "ttyUSB") == 0
                || name.compare(0, 6, "ttyACM") == 0;
#endif
            if (!match)
                continue;
            TransportDevice dev;
            dev.path = std::string(kDirs[d]) + "/" + name;
            dev.name = name;
            out.push_back(dev);
        }
        closedir(dir);
    }
    return out;
}

bool SerialTransport::Connect(const std::string& device_path)
{
    Disconnect();
    SetState(CONN_CONNECTING);

    int fd = open(device_path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        ReportError(std::string("cannot open ") + device_path + ": " + strerror(errno));
        SetState(CONN_DISCONNECTED);
        return false;
    }

    struct termios tio;
    memset(&tio, 0, sizeof(tio));
    if (tcgetattr(fd, &tio) != 0) {
        ReportError(std::string("tcgetattr failed: ") + strerror(errno));
        close(fd);
        SetState(CONN_DISCONNECTED);
        return false;
    }

    cfmakeraw(&tio);
    cfsetispeed(&tio, B115200);
    cfsetospeed(&tio, B115200);
    tio.c_cflag |= (CLOCAL | CREAD);   // ignore modem lines, enable receiver
    tio.c_cflag &= ~CSTOPB;            // one stop bit
    tio.c_cflag &= ~PARENB;            // no parity
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |= CS8;                // 8 data bits
#ifdef CRTSCTS
    tio.c_cflag &= ~CRTSCTS;           // no hardware flow control
#endif
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        ReportError(std::string("tcsetattr failed: ") + strerror(errno));
        close(fd);
        SetState(CONN_DISCONNECTED);
        return false;
    }
    tcflush(fd, TCIOFLUSH);

    {
        std::lock_guard<std::mutex> guard(fIoLock);
        fFd = fd;
    }
    SetConnectedAddress(device_path);
    SetState(CONN_CONNECTED);
    StartReadThread();
    return true;
}

bool SerialTransport::WriteBytes(const uint8_t* data, size_t len)
{
    std::lock_guard<std::mutex> guard(fIoLock);
    if (fFd < 0)
        return false;
    size_t written = 0;
    while (written < len) {
        ssize_t n = write(fFd, data + written, len - written);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd pfd;
                pfd.fd = fFd;
                pfd.events = POLLOUT;
                if (poll(&pfd, 1, 1000) <= 0)
                    return false;
                continue;
            }
            return false;
        }
        written += static_cast<size_t>(n);
    }
    return true;
}

int SerialTransport::ReadBytes(uint8_t* buf, size_t len)
{
    int fd;
    {
        std::lock_guard<std::mutex> guard(fIoLock);
        fd = fFd;
    }
    if (fd < 0)
        return -1;

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    int pr = poll(&pfd, 1, 200);  // short timeout so Disconnect() is responsive
    if (pr == 0)
        return 0;
    if (pr < 0)
        return errno == EINTR ? 0 : -1;
    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
        return -1;

    ssize_t n = read(fd, buf, len);
    if (n < 0)
        return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? 0 : -1;
    if (n == 0)
        return -1;  // device went away
    return static_cast<int>(n);
}

void SerialTransport::CloseIo()
{
    std::lock_guard<std::mutex> guard(fIoLock);
    if (fFd >= 0) {
        close(fFd);
        fFd = -1;
    }
}

// ─── TcpTransport ────────────────────────────────────────────────

TcpTransport::TcpTransport() : fSocket(-1) {}

TcpTransport::~TcpTransport()
{
    Disconnect();
}

bool TcpTransport::Connect(const std::string& device_path)
{
    Disconnect();
    SetState(CONN_CONNECTING);

    std::string host = device_path;
    int port = kDefaultPort;
    size_t colon = device_path.rfind(':');
    // Guard against an IPv6 literal without brackets.
    if (colon != std::string::npos && device_path.find(':') == colon) {
        host = device_path.substr(0, colon);
        port = atoi(device_path.c_str() + colon + 1);
        if (port <= 0)
            port = kDefaultPort;
    }

    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = NULL;
    int rc = getaddrinfo(host.c_str(), portstr, &hints, &res);
    if (rc != 0 || res == NULL) {
        ReportError(std::string("cannot resolve ") + host + ": " + gai_strerror(rc));
        SetState(CONN_DISCONNECTED);
        return false;
    }

    int sock = -1;
    for (struct addrinfo* ai = res; ai != NULL; ai = ai->ai_next) {
        sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sock < 0)
            continue;
        if (connect(sock, ai->ai_addr, ai->ai_addrlen) == 0)
            break;
        close(sock);
        sock = -1;
    }
    freeaddrinfo(res);

    if (sock < 0) {
        ReportError(std::string("cannot connect to ") + device_path + ": " + strerror(errno));
        SetState(CONN_DISCONNECTED);
        return false;
    }

    int one = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    {
        std::lock_guard<std::mutex> guard(fIoLock);
        fSocket = sock;
    }
    SetConnectedAddress(device_path);
    SetState(CONN_CONNECTED);
    StartReadThread();
    return true;
}

bool TcpTransport::WriteBytes(const uint8_t* data, size_t len)
{
    std::lock_guard<std::mutex> guard(fIoLock);
    if (fSocket < 0)
        return false;
    size_t sent = 0;
    while (sent < len) {
#ifdef MSG_NOSIGNAL
        ssize_t n = send(fSocket, data + sent, len - sent, MSG_NOSIGNAL);
#else
        ssize_t n = send(fSocket, data + sent, len - sent, 0);
#endif
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

int TcpTransport::ReadBytes(uint8_t* buf, size_t len)
{
    int sock;
    {
        std::lock_guard<std::mutex> guard(fIoLock);
        sock = fSocket;
    }
    if (sock < 0)
        return -1;

    struct pollfd pfd;
    pfd.fd = sock;
    pfd.events = POLLIN;
    int pr = poll(&pfd, 1, 200);
    if (pr == 0)
        return 0;
    if (pr < 0)
        return errno == EINTR ? 0 : -1;
    if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
        return -1;

    ssize_t n = recv(sock, buf, len, 0);
    if (n < 0)
        return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? 0 : -1;
    if (n == 0)
        return -1;  // peer closed
    return static_cast<int>(n);
}

void TcpTransport::CloseIo()
{
    std::lock_guard<std::mutex> guard(fIoLock);
    if (fSocket >= 0) {
        shutdown(fSocket, SHUT_RDWR);
        close(fSocket);
        fSocket = -1;
    }
}

}  // namespace meshy
