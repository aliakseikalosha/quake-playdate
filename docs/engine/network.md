# Networking

[← Documentation index](../README.md) · [Source index](../source-index.md)

Quake always talks to its server through a *socket* abstraction, even in single player. Which concrete networking exists depends on the build:

| Build | Files compiled | What works |
| --- | --- | --- |
| **Playdate** (the only board in the tree) | `net_main.c`, `net_loop.c`, `net_none.c` | Single player only (loopback). |
| A Darwin/Linux board *other than* the Playdate (none exists any more) | the above minus `net_none.c`, plus `net_dgrm.c`, `net_udp.c`, `net_bsd.c` | Loopback **and** UDP multiplayer. |

The choice is made in [`winquake/CMakeLists.txt`](../build-system.md#winquakecmakeliststxt-the-engine). Since the desktop boards were removed
([Removed boards](../build-system.md#removed-boards)), no configuration in this tree selects the second row: `net_dgrm.c`, `net_udp.c`, `net_bsd.c` (and
the unused `net_vcr.c`) are Quake engine code that is kept but not built.

```
CL_EstablishConnection("local")                 SV_CheckForNewClients
         │                                              │
         ▼                                              ▼
   NET_Connect ──► net_drivers[n].Connect      NET_CheckNewConnections
         │          (Loopback or Datagram)              │
         ▼                                              ▼
   qsocket_t  ◄────── NET_SendMessage / NET_GetMessage ──────►  qsocket_t
```

| File | Role |
| --- | --- |
| [`net.h`](#neth) | Socket and driver structs, the connection-protocol constants |
| [`net_main.c`](#net_mainc) | The driver-independent layer (`NET_*`) |
| [`net_loop.h` / `net_loop.c`](#net_loopc) | In-process loopback driver (single player) |
| [`net_none.c`](#net_nonec) | Driver table for builds without network drivers |
| [`net_bsd.c`](#net_bsdc) | Driver table for builds with UDP (not built here) |
| [`net_dgrm.h` / `net_dgrm.c`](#net_dgrmc) | Datagram driver: reliable messages over an unreliable transport |
| [`net_udp.h` / `net_udp.c`](#net_udpc) | UDP transport (BSD sockets) |
| [`net_vcr.h` / `net_vcr.c`](#net_vcrc-not-built) | Network recording and playback (unused) |

---

## `net.h`

The two driver interfaces and the socket structure.

```c
typedef struct qsocket_s {               // one connection
	struct qsocket_s *next;
	double  connecttime, lastMessageTime, lastSendTime;
	qboolean disconnected, canSend, sendNext;
	int     driver, landriver, socket;   // which drivers own it
	unsigned ackSequence, sendSequence, unreliableSendSequence;
	int     sendMessageLength;     byte sendMessage[NET_MAXMESSAGE];       // 8192
	unsigned receiveSequence, unreliableReceiveSequence;
	int     receiveMessageLength;  byte receiveMessage[NET_MAXMESSAGE];
	struct qsockaddr addr;  char address[NET_NAMELEN];
} qsocket_t;

typedef struct {                          // a "driver": the transport-independent connection layer
	char *name;  qboolean initialized;
	int (*Init) (void);  void (*Shutdown) (void);  void (*Listen) (qboolean state);
	void (*SearchForHosts) (qboolean xmit);
	qsocket_t *(*Connect) (char *host);  qsocket_t *(*CheckNewConnections) (void);
	int (*QGetMessage) (qsocket_t *s);  int (*QSendMessage) (qsocket_t *s, sizebuf_t *d);
	int (*SendUnreliableMessage) (qsocket_t *s, sizebuf_t *d);
	qboolean (*CanSendMessage) (qsocket_t *s);  qboolean (*CanSendUnreliableMessage) (qsocket_t *s);
	void (*Close) (qsocket_t *s);
} net_driver_t;

typedef struct { ... int (*OpenSocket)(int port); int (*Read)(...); int (*Write)(...); ... } net_landriver_t;  // a "LAN driver": raw datagrams (UDP)
```

`NET_PROTOCOL_VERSION` is 3. The header's long comment documents the **connection protocol** (`CCREQ_CONNECT`, `CCREQ_SERVER_INFO`, `CCREQ_PLAYER_INFO`,
`CCREQ_RULE_INFO` and their `CCREP_*` replies) used to find, query and join servers. A packet header is two `unsigned int`s (`NET_HEADERSIZE`): the length and flags
(`NETFLAG_DATA`, `ACK`, `NAK`, `EOM`, `UNRELIABLE`, `CTL`). `hostcache_t hostcache[8]` holds the servers found by `slist`.

The Quake game protocol itself ([`protocol.h`](client.md#protocolh)) rides on top: a connected socket carries `svc_*` messages from the server and `clc_*` messages from the client.

---

## `net_main.c`

The driver-independent layer the rest of the engine calls.

| Function | Purpose |
| --- | --- |
| `NET_Init` | Reads `-port`, `-listen`; allocates `svs.maxclientslimit + 1` `qsocket_t`s on the hunk and a `net_message` buffer; registers `net_messagetimeout` (300 s), `hostname` and the modem/serial config cvars, and commands `slist`, `listen`, `maxplayers`, `port`; initialises every driver in `net_drivers[]`. |
| `NET_Connect(host)` | Tries each driver in turn; `"local"` goes to Loopback. |
| `NET_CheckNewConnections()` | The server asks each driver for a new client. |
| `NET_GetMessage(sock)` | Next message from a socket into `net_message`: `1` reliable, `2` unreliable, `0` none, `-1` connection lost (also when `net_messagetimeout` expires). |
| `NET_SendMessage(sock, data)` / `NET_SendUnreliableMessage` | Reliable (acknowledged, retransmitted) and best-effort delivery. |
| `NET_CanSendMessage(sock)` | Whether the reliable channel is free. |
| `NET_SendToAll(data, blocktime)` | Reliable broadcast to every client, waiting up to `blocktime` for slow ones. |
| `NET_Close(sock)`, `NET_NewQSocket`, `NET_FreeQSocket` | Socket lifetime. |
| `NET_Poll()` | Runs the scheduled poll procedures (server-list searching). `SchedulePollProcedure`, `Slist_Poll`, `Slist_Send`, `NET_Slist_f`. |
| `SetNetTime()` | Updates `net_time` from `Sys_FloatTime`. |
| `NET_Shutdown` | Closes sockets, shuts the drivers down. |

```c
// cl_main.c: how the client connects to the local server
cls.netcon = NET_Connect ("local");            // → net_drivers[0] (Loopback)
if (!cls.netcon) Host_Error ("CL_Connect: connect failed\n");

// a reliable write, then an unreliable one
NET_SendMessage (cls.netcon, &cls.message);
NET_SendUnreliableMessage (cls.netcon, &buf);  // clc_move
```

**Port changes:** the VCR (network record and replay) hooks were removed (`net_vcr.h` is no longer included, no `recording` / `vcrFile`); the message-timeout compare is in `float`; dedicated-server code removed.

---

## `net_loop.c`

The **loopback driver**: client and server in the same process exchange messages through two linked `qsocket_t`s (`loop_client`, `loop_server`), copying between their buffers.
It is the only driver a single-player Playdate game uses, and is why single player and multiplayer share one code path.

- `Loop_Connect("local")` creates the pair; `Loop_CheckNewConnections` hands the server end to `SV_ConnectClient` once.
- Each message is stored in the *peer's* receive buffer with a 4-byte header: a type byte (`1` reliable, `2` unreliable), the 16-bit length, and a padding byte; the next message starts at the next 4-byte boundary
  (`IntAlign`). `Loop_GetMessage` copies the oldest one into `net_message` and returns its type (`0` if none).
- `Loop_SendMessage` / `Loop_SendUnreliableMessage` append to that buffer (a reliable send clears `canSend` until the peer has read it); overflowing `NET_MAXMESSAGE` is a `Sys_Error`.
- `Loop_SearchForHosts` fills `hostcache[0]` with the local game for the "join game" list.

```c
// net_loop.c, Loop_SendMessage: header, then the payload, into the other end's buffer
*buffer++ = 1;                                  // message type: reliable
*buffer++ = data->cursize & 0xff;               // length, little endian
*buffer++ = data->cursize >> 8;
buffer++;                                       // alignment
Q_memcpy (buffer, data->data, data->cursize);
*bufferLength = IntAlign (*bufferLength + data->cursize + 4);
sock->canSend = false;                          // until the peer reads it
```

---

## `net_none.c`

The driver table for builds with no network hardware: exactly one driver, `"Loopback"`, and `net_numlandrivers = 0`.

```c
net_driver_t net_drivers[MAX_NET_DRIVERS] = { { "Loopback", false, Loop_Init, Loop_Listen, Loop_SearchForHosts,
	Loop_Connect, Loop_CheckNewConnections, Loop_GetMessage, Loop_SendMessage, Loop_SendUnreliableMessage,
	Loop_CanSendMessage, Loop_CanSendUnreliableMessage, Loop_Close, Loop_Shutdown } };
int net_numdrivers = 1;
```

## `net_bsd.c`

The driver table for builds with UDP networking: Loopback followed by `"Datagram"` (`Datagram_*`). Despite its name it contains no socket code; the sockets are in `net_udp.c`.

---

## `net_dgrm.c`

The **datagram driver**: builds a connection-oriented, reliable channel on top of an unreliable datagram transport (the `net_landrivers[]`, i.e. UDP).

- **Reliability.** Every reliable message is split into `MAX_DATAGRAM`-sized packets with sequence numbers; the receiver acknowledges (`NETFLAG_ACK`) and the sender retransmits (`ReSendMessage`) until acknowledged; `SendMessageNext` sends the next chunk.
  Unreliable messages are single packets that may be dropped or arrive out of order (and old ones are discarded).
- **Connecting.** `Datagram_Connect` sends `CCREQ_CONNECT` and waits for `CCREP_ACCEPT` (which carries the port to use). `Datagram_CheckNewConnections` is the server side: it answers connection requests (and server/player/rule queries), enforces
  `NET_PROTOCOL_VERSION`, bans (`ban`), and allocates a `qsocket_t`.
- **Server browser.** `Datagram_SearchForHosts` broadcasts `CCREQ_SERVER_INFO` and fills `hostcache`.
- **Diagnostics.** Console commands `net_stats` (`NET_Stats_f`, `PrintStats`), `ban`, `test`, `test2` (query a server for its players and rules).

```c
// SendMessageNext: send up to MAX_DATAGRAM bytes of the pending reliable message; the last piece is flagged EOM
packetLen = NET_HEADERSIZE + dataLen;
packetBuffer.length   = BigLong (packetLen | (NETFLAG_DATA | eom));
packetBuffer.sequence = BigLong (sock->sendSequence++);
Q_memcpy (packetBuffer.data, sock->sendMessage, dataLen);
sfunc.Write (sock->socket, (byte *)&packetBuffer, packetLen, &sock->addr);
```

**Port changes:** overlapping buffer shifts use `memmove` instead of `Q_memcpy` (a copy of overlapping memory); the NeXT include was removed; the dedicated-server check was removed.

## `net_udp.c`

The UDP LAN driver on BSD sockets (`socket`, `bind`, `recvfrom`, `sendto`): `UDP_Init` finds the host name and address and opens the control socket; `UDP_OpenSocket(port)`, `UDP_CloseSocket`, `UDP_Connect`,
`UDP_CheckNewConnections` (asks the accept socket how many bytes are waiting with `ioctl(FIONREAD)`), `UDP_Read` / `UDP_Write` / `UDP_Broadcast`, address conversion (`UDP_AddrToString`, `UDP_StringToAddr`,
`PartialIPAddress`), `UDP_GetNameFromAddr` / `UDP_GetAddrFromName` (DNS), `UDP_AddrCompare`, `UDP_GetSocketPort` / `UDP_SetSocketPort`. It fills one `net_landriver_t` entry (`net_udp.h` declares the `UDP_*` functions).

```c
sock = UDP_OpenSocket (net_hostport);                       // bind to the game port (default 26000)
n = UDP_Read (sock, buffer, sizeof buffer, &fromaddr);      // non-blocking
UDP_Write (sock, buffer, len, &toaddr);
```

Only compiled where `CMAKE_SYSTEM_NAME` is Darwin or Linux and the board is not the Playdate, which no board in this tree satisfies.

---

## `net_vcr.c` (not built)

`net_vcr.h` / `net_vcr.c` implement a "VCR" driver that records all network traffic to a file and plays it back for reproducible multiplayer tests (`VCR_Init`, `VCR_GetMessage`, `VCR_ReadNext`, …).
**Nothing in this tree uses it**: it is not in the CMake source lists, and the recording hooks it relied on were removed from `net_main.c`. It is kept for reference.
