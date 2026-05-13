// yaagc_socket.c — canonical SocketAPI semantics on ESP-IDF / lwIP.
//
// Structurally mirrors third_party/virtualagc/yaAGC/SocketAPI.c +
// agc_utilities.c (FormIoPacket / ParseIoPacket / Unblock / Establish).
// Differences vs upstream:
//   - EstablishSocket uses INADDR_ANY (no gethostname/gethostbyname,
//     which lwIP supports but is overkill for a local listener).
//   - All POSIX socket calls go through lwIP's BSD socket layer
//     (lwip/sockets.h). closesocket() → close(). errno from lwIP.
//   - We don't hook up the AGS / DEDA debug paths (DebugDeda et al).
//     Block II AGC peripherals only.
//   - ChannelOutput also forwards to channel_router so the local LCD
//     keeps tracking even when no socket peer is connected.
//
// This file provides:
//   ChannelInput / ChannelOutput / ChannelRoutine — the three entry
//   points the engine calls each cycle (see agc_engine.c:1942-1952).
// When CONFIG_AGC_YAAGC_SOCKET is enabled, io_callbacks.c forwards
// these calls into here; otherwise they keep the legacy channel_router
// pump_input / on_output behaviour.

#include "yaagc_socket.h"

// Cross-compile cleanly between ESP-IDF + lwIP (target build) and a
// host build used by tests/host/test_yaagc_socket_host.c. Both expose
// the same BSD socket surface so the rest of the file is unchanged.
#if defined(ESP_PLATFORM)
#  include "lwip/sockets.h"
#  include "lwip/netdb.h"
#  include "esp_log.h"
#elif defined(_WIN32)
#  define _WIN32_WINNT 0x0601
#  include <winsock2.h>
#  include <ws2tcpip.h>
   // Win32 send() has no MSG_NOSIGNAL (no SIGPIPE on Windows).
#  ifndef MSG_NOSIGNAL
#    define MSG_NOSIGNAL 0
#  endif
   // Map POSIX-style names to winsock equivalents.
#  define close(fd) closesocket(fd)
#  include <stdio.h>
#  define ESP_LOGI(tag, fmt, ...) fprintf(stderr, "[%s] " fmt "\n", tag, ##__VA_ARGS__)
#  define ESP_LOGE(tag, fmt, ...) fprintf(stderr, "[%s] ERR " fmt "\n", tag, ##__VA_ARGS__)
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <unistd.h>
#  include <stdio.h>
#  define ESP_LOGI(tag, fmt, ...) fprintf(stderr, "[%s] " fmt "\n", tag, ##__VA_ARGS__)
#  define ESP_LOGE(tag, fmt, ...) fprintf(stderr, "[%s] ERR " fmt "\n", tag, ##__VA_ARGS__)
#endif

#include <errno.h>
#if !defined(_WIN32)
#  include <fcntl.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "yaAGC.h"
#include "agc_engine.h"

static const char *TAG = "yaagc_sock";

// ---------------------------------------------------------------------------
// FormIoPacket / ParseIoPacket — copies of agc_utilities.c lines 95-149.
// Packet layout (Virtual AGC Technical Manual, "I/O Specifics"):
//   00pppppp 01pppddd 10dddddd 11dddddd
// 9-bit channel (top bit = uBit/mask flag) + 15-bit value.

static int FormIoPacket_local(int Channel, int Value, unsigned char *Packet)
{
    if (Channel < 0 || Channel > 0x1ff) return 1;
    if (Value < 0 || Value > 0x7fff)    return 1;
    if (Packet == NULL)                 return 1;
    Packet[0] = Channel >> 3;
    Packet[1] = 0x40 | ((Channel << 3) & 0x38) | ((Value >> 12) & 0x07);
    Packet[2] = 0x80 | ((Value >> 6) & 0x3F);
    Packet[3] = 0xc0 | (Value & 0x3F);
    return 0;
}

static int ParseIoPacket_local(const unsigned char *Packet,
                               int *Channel, int *Value, int *uBit)
{
    if (0x00 != (0xc0 & Packet[0])) return 1;
    if (0x40 != (0xc0 & Packet[1])) return 1;
    if (0x80 != (0xc0 & Packet[2])) return 1;
    if (0xc0 != (0xc0 & Packet[3])) return 1;
    *Channel = ((Packet[0] & 0x1F) << 3) | ((Packet[1] >> 3) & 7);
    *Value   = ((Packet[1] << 12) & 0x7000)
             | ((Packet[2] << 6)  & 0x0FC0)
             | (Packet[3]         & 0x003F);
    *uBit    = (0x20 & Packet[0]);
    return 0;
}

// ---------------------------------------------------------------------------
// Server / client state. We allocate our own Client_t array rather than
// reusing the engine's DefaultClients to keep this component independent
// of whether agc_engine.c is built with AGC_ENGINE_C (which defines those
// globals). Layout is identical so the canonical packet flow is preserved.

#define YAAGC_MAX_CLIENTS 4

typedef struct {
    int  sock;                  // -1 when slot is empty
    unsigned char Packet[4];
    int  Size;
    int  ChannelMasks[256];
} yaagc_client_t;

// Static initialisers — zero-init would leave every sock at 0 (a
// plausible fd to lwIP's send()). If the engine task starts before
// yaagc_socket_init runs, ChannelOutput would call send(0, …) and
// trip EBADF, dropping the slot before init had a chance. Initialise
// to -1 explicitly so an early-fire is harmless.
static yaagc_client_t s_clients[YAAGC_MAX_CLIENTS] = {
    [0 ... YAAGC_MAX_CLIENTS - 1] = { .sock = -1 }
};
static int            s_server_sock  = -1;
static int            s_interlace    = 0;
static const int      s_interlace_n  = 50;  // matches SocketInterlaceReload
static int            s_timeout_tick = 0;

// Synthetic-client byte ring. yaagc_socket_inject_packet writes 4-byte
// packets here; yaagc_socket_channel_input drains them just like recv()
// would for a real TCP client. s_clients[0] is reserved with sock = -2
// (SYNTHETIC_SOCK_FD) so the drain loop knows to read from this ring
// instead of calling recv().
#define SYNTHETIC_SOCK_FD (-2)
#define LOCAL_RING_SZ     256          // 64 packets fit
static volatile unsigned char s_local_ring[LOCAL_RING_SZ];
static volatile unsigned      s_local_head = 0;  // producer (inject)
static volatile unsigned      s_local_tail = 0;  // consumer (drain)
static int                    s_local_init = 0;  // set after yaagc_socket_init

static int local_ring_push_byte(unsigned char b)
{
    unsigned next = (s_local_head + 1) % LOCAL_RING_SZ;
    if (next == s_local_tail) return -1;  // full
    s_local_ring[s_local_head] = b;
    s_local_head = next;
    return 0;
}

static int local_ring_pop_byte(unsigned char *out)
{
    if (s_local_head == s_local_tail) return -1;  // empty
    *out = s_local_ring[s_local_tail];
    s_local_tail = (s_local_tail + 1) % LOCAL_RING_SZ;
    return 0;
}

static void unblock_socket(int s)
{
#if defined(_WIN32)
    u_long nonblock = 1;
    ioctlsocket((SOCKET)s, FIONBIO, &nonblock);
#else
    int flags = fcntl(s, F_GETFL, 0);
    if (flags < 0) flags = 0;
    fcntl(s, F_SETFL, flags | O_NONBLOCK);
#endif
}

static int initialize_socket_system(void)
{
#if defined(_WIN32)
    static int started = 0;
    if (started) return 0;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 0), &wsa) != 0) return -1;
    started = 1;
#endif
    return 0;
}

static int establish_listener(uint16_t port)
{
    if (initialize_socket_system() != 0) {
        ESP_LOGE(TAG, "WSAStartup failed");
        return -1;
    }
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        ESP_LOGE(TAG, "socket() failed: errno=%d", errno);
        return -1;
    }
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family      = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port        = htons(port);

    if (bind(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        ESP_LOGE(TAG, "bind(:%u) failed: errno=%d", (unsigned)port, errno);
        close(s);
        return -1;
    }
    if (listen(s, YAAGC_MAX_CLIENTS) < 0) {
        ESP_LOGE(TAG, "listen() failed: errno=%d", errno);
        close(s);
        return -1;
    }
    unblock_socket(s);
    ESP_LOGI(TAG, "listening on 0.0.0.0:%u", (unsigned)port);
    return s;
}

static void update_peripheral_connect(yaagc_client_t *c, agc_t *State)
{
    // Mirror SocketAPI.c::UpdateAgcPeripheralConnect — replay current
    // output state to a freshly-connected client so it can mirror the
    // running engine without losing context.
    unsigned char Packet[4];
    for (int i = 0; i < 16; i++) {
        if (State->OutputChannel10[i] != 0) {
            FormIoPacket_local(010, State->OutputChannel10[i], Packet);
            send(c->sock, (const char *)Packet, 4, 0);
        }
    }
    for (int i = 011; i < 0200; i++) {
        if (State->InputChannel[i] != 0) {
            FormIoPacket_local(i, State->InputChannel[i], Packet);
            send(c->sock, (const char *)Packet, 4, 0);
        }
    }
}

// ---------------------------------------------------------------------------
// Engine-facing entry points. These get called from io_callbacks.c when
// CONFIG_AGC_YAAGC_SOCKET is on. Signatures match the canonical SocketAPI.

void yaagc_socket_channel_output(agc_t *State, int Channel, int Value)
{
    // Per SocketAPI.c: ch7 is captured locally (engine flag), ch013 with
    // bits 8,9 set fires the RHC counter-poke. The engine's WriteIO has
    // already done its mask work — we just broadcast.
    if (Channel == 7) {
        State->InputChannel[7]  = State->OutputChannel7 = (Value & 0160);
        return;
    }
    if (Channel == 013 && (0600 & Value) == 0600 && !CmOrLm) {
        State->Erasable[0][042] = LastRhcPitch;
        State->Erasable[0][043] = LastRhcYaw;
        State->Erasable[0][044] = LastRhcRoll;
    }
    unsigned char Packet[4];
    if (FormIoPacket_local(Channel, Value, Packet)) return;
    for (int i = 0; i < YAAGC_MAX_CLIENTS; i++) {
        yaagc_client_t *c = &s_clients[i];
        if (c->sock < 0) continue;
        int j = send(c->sock, (const char *)Packet, 4, MSG_NOSIGNAL);
        if (j < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            ESP_LOGI(TAG, "client %d dropped (send errno=%d)", i, errno);
            close(c->sock);
            c->sock = -1;
        }
    }
}

int yaagc_socket_channel_input(agc_t *State)
{
    // Throttle. Identical to SocketInterlace in canonical: poll once
    // every s_interlace_n cycles, not every cycle.
    if (s_interlace > 0) { s_interlace--; return 0; }
    s_interlace = s_interlace_n;

    for (int i = 0; i < YAAGC_MAX_CLIENTS; i++) {
        yaagc_client_t *c = &s_clients[i];
        // sock < 0 marks an empty slot — except for the synthetic
        // client (sock == SYNTHETIC_SOCK_FD = -2) which reads from
        // the local ring buffer instead of recv().
        if (c->sock == -1) continue;
        for (int j = c->Size; j < 4; j++) {
            unsigned char ch;
            int k;
            if (c->sock == SYNTHETIC_SOCK_FD) {
                k = (local_ring_pop_byte(&ch) == 0) ? 1 : 0;
                if (k <= 0) break;     // ring empty — try again next cycle
            } else {
                k = recv(c->sock, (char *)&ch, 1, 0);
                if (k == 0 ||
                    (k < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                    // Peer closed (k=0) or hard error — release the slot.
                    // Without this, after enough probe disconnects every
                    // slot ends up with a stale fd and lwIP's socket pool
                    // exhausts (httpd starts returning accept errno 23).
                    ESP_LOGI(TAG, "client %d closed (recv=%d errno=%d)",
                             i, k, errno);
                    close(c->sock);
                    c->sock = -1;
                    c->Size = 0;
                    break;
                }
                if (k < 0) break;       // EAGAIN — no data right now
            }
            // Same signature filter as SocketAPI lines 192-205. Rejects
            // mid-packet garbage so we can resync if the stream skews.
            static const unsigned char Signatures[4] = {0x00, 0x40, 0x80, 0xC0};
            if (Signatures[j] != (ch & 0xC0)) {
                c->Size = 0;
                if ((ch & 0xC0) != 0) { j = -1; continue; }
                j = 0;
            }
            c->Packet[c->Size++] = ch;
        }
        if (c->Size >= 4) {
            int Channel, Value, uBit;
            if (!ParseIoPacket_local(c->Packet, &Channel, &Value, &uBit)) {
                Value &= 077777;
                if (uBit) {
                    c->ChannelMasks[Channel] = Value;
                } else if (Channel & 0x80) {
                    UnprogrammedIncrement(State, Channel, Value);
                    c->Size = 0;
                    return 1;
                } else {
                    Value &= c->ChannelMasks[Channel];
                    Value |= ReadIO(State, Channel) & ~c->ChannelMasks[Channel];
                    WriteIO(State, Channel, Value);
                    if (Channel == 015) {
                        State->InterruptRequests[5] = 1;
                    } else if (Channel == 0173) {
                        State->Erasable[0][RegINLINK] = Value & 077777;
                        State->InterruptRequests[7] = 1;
                    } else if (Channel == 0166) {
                        LastRhcPitch = Value;
                        yaagc_socket_channel_output(State, Channel, Value);
                    } else if (Channel == 0167) {
                        LastRhcYaw = Value;
                        yaagc_socket_channel_output(State, Channel, Value);
                    } else if (Channel == 0170) {
                        LastRhcRoll = Value;
                        yaagc_socket_channel_output(State, Channel, Value);
                    }
                }
            }
            c->Size = 0;
        }
    }
    return 0;
}

void yaagc_socket_channel_routine(agc_t *State)
{
    // Accept new clients. Slot 0 is reserved for the local synthetic
    // client (sock == SYNTHETIC_SOCK_FD = -2 < 0), so it would
    // otherwise fail the `c->sock >= 0 → continue` filter and get
    // overwritten by the first real TCP fd accept() returned. Start
    // at slot 1 to keep slot 0 immutable.
    if (s_server_sock >= 0) {
        for (int i = 1; i < YAAGC_MAX_CLIENTS; i++) {
            yaagc_client_t *c = &s_clients[i];
            if (c->sock >= 0) continue;
            int ns = accept(s_server_sock, NULL, NULL);
            if (ns < 0) break;
            unblock_socket(ns);
            c->sock = ns;
            c->Size = 0;
            for (int m = 0; m < 256; m++) c->ChannelMasks[m] = 077777;
            ESP_LOGI(TAG, "client %d connected (fd=%d)", i, ns);
            update_peripheral_connect(c, State);
            break;
        }
    }
    // Periodic dead-peer check (SocketAPI sends a 0xFF marker every 16
    // ChannelRoutine ticks). The protocol's signature filter discards
    // it on the receive side, so it's only a TCP-level keepalive.
    s_timeout_tick++;
    if ((s_timeout_tick & 017) == 0) {
        unsigned char ping[4] = {0xff, 0xff, 0xff, 0xff};
        for (int i = 0; i < YAAGC_MAX_CLIENTS; i++) {
            yaagc_client_t *c = &s_clients[i];
            if (c->sock < 0) continue;
            int j = send(c->sock, (const char *)ping, 4, MSG_NOSIGNAL);
            if (j < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                ESP_LOGI(TAG, "client %d ping-failed (errno=%d)", i, errno);
                close(c->sock);
                c->sock = -1;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Lifecycle.

int yaagc_socket_init(uint16_t port)
{
    for (int i = 0; i < YAAGC_MAX_CLIENTS; i++) {
        s_clients[i].sock = -1;
        s_clients[i].Size = 0;
        for (int m = 0; m < 256; m++) s_clients[i].ChannelMasks[m] = 077777;
    }
    // Slot 0 is permanently the local synthetic client. Real TCP
    // peers occupy slots 1..N-1.
    s_clients[0].sock = SYNTHETIC_SOCK_FD;
    s_local_head = 0;
    s_local_tail = 0;
    s_local_init = 1;
    s_server_sock = establish_listener(port);
    return s_server_sock < 0 ? -1 : 0;
}

int yaagc_socket_inject_packet(int channel, int value, int is_mask)
{
    if (!s_local_init) return -1;
    unsigned char p[4];
    if (FormIoPacket_local(channel, value & 077777, p)) return -1;
    if (is_mask) p[0] |= 0x20;     // set uBit in packet byte 0
    // All-or-nothing: refuse to write a partial packet if the ring
    // can't hold four bytes. Lets callers retry without resync.
    unsigned head_after_save = s_local_head;
    for (int i = 0; i < 4; i++) {
        if (local_ring_push_byte(p[i]) != 0) {
            s_local_head = head_after_save;  // rollback
            return -1;
        }
    }
    return 0;
}

int yaagc_socket_inject_key(int code)
{
    return yaagc_socket_inject_packet(015, code & 037, 0);
}

int yaagc_socket_inject_uplink_key(int code)
{
    // CCC encoding: bits 14-10 = code, bits 9-5 = ~code (5-bit
    // complement), bits 4-0 = code. UPRPT1's two UPTEST checks pass
    // when LOW5 + MID5 == 037 (i.e. MID5 = ~LOW5) and the equivalent
    // for HI5.
    int c5 = code & 0x1F;
    int mid5 = (~c5) & 0x1F;
    int word = (c5 << 10) | (mid5 << 5) | c5;
    return yaagc_socket_inject_packet(0173, word, 0);
}

void yaagc_socket_shutdown(void)
{
    if (s_server_sock >= 0) { close(s_server_sock); s_server_sock = -1; }
    for (int i = 0; i < YAAGC_MAX_CLIENTS; i++) {
        if (s_clients[i].sock >= 0) {
            close(s_clients[i].sock);
            s_clients[i].sock = -1;
        }
    }
}
