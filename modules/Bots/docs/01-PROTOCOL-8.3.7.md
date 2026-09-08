# Bots module — the 8.3.7 login protocol as this core implements it

Iteration 1 injects a `WorldSession` and never speaks this protocol (§2.3 of the
design). This file records the wire chain anyway, because the roadmap's
alternative backend (`docs/00-DESIGN.md` §10.2) is a real loopback client that
must reproduce it, and because every value below was read from the core rather
than guessed. Client: **8.3.7 (35662)**.

## Packet framing

`PacketHeader` (`WorldSocket.h`):

```
#pragma pack(push, 1)
struct PacketHeader { uint32 Size; uint8 Tag[12]; };   // 16 bytes
```

`Size` is the encrypted payload length; `Tag` is the 12-byte AES-GCM
authentication tag. `IsValidSize()` bounds `Size < 0x40000`. Server→client
packets after the handshake are AES-128-GCM; the first two exchanges (auth
session, continued session) are plaintext.

## Handshake seeds (`WorldSocket.cpp:69-72`)

```
AuthCheckSeed        = C5 C6 98 95 ...
SessionKeySeed       = 58 CB CF 40 ...
ContinuedSessionSeed = 16 AD C0 D4 ...
EncryptionKeySeed    = E9 AB 03 50 ...
```

(16 bytes each; read the array literals at those lines for the full values.)

## Key derivation

* **Realm digest** (proves the client knows the session key):
  `HMAC_SHA256(SHA256(sessionKey || Win64AuthSeed-if-os-is-Wn64),
   localChallenge || serverChallenge || AuthCheckSeed)`, truncated to the first
  24 bytes.
* **Session key** (40 bytes), from the bnet login:
  `SessionKeyGenerator<SHA256>(HMAC(SHA256(key), serverChall || localChall ||
  SessionKeySeed)).Generate(40)`.
* **AES key** (16 bytes) for the encrypted stream:
  `HMAC(sessionKey, localChallenge || serverChallenge || EncryptionKeySeed)`,
  first 16 bytes.
* **AES-128-GCM IV** = 8-byte big-endian counter || 4-byte magic. The magic is
  `0x52565253` ("SRVR") server→client and `0x544E4C43` ("CLNT") client→server.
  The counter increments per packet in each direction independently.

## Connection sequence

1. **Realm connect.** TCP to `WorldServerPort` (default 8085). The client sends
   `CMSG_AUTH_SESSION` (plaintext): account id, a local challenge, the realm
   digest above, and the dos challenge. The server validates the digest against
   the stored `session_key_bnet` and replies `SMSG_AUTH_RESPONSE`.
2. **Character select.** On the same realm socket, `CMSG_ENUM_CHARACTERS`
   (`0x35E8`, empty body) → the server runs the async char-enum and replies
   `SMSG_ENUM_CHARACTERS_RESULT` (`0x2584`). This is the packet the injected
   path intercepts to know `_legitCharacters` is filled (§2.3).
3. **Login.** `CMSG_PLAYER_LOGIN` (`0x35EA`): `ObjectGuid` then `float FarClip`.
   The server replies `SMSG_CONNECT_TO_INSTANCE` carrying an RSA-signed
   `ConnectToKey` and the instance address.
4. **Instance connect.** A **second** TCP connection, to `InstanceServerPort`
   (default 8086). The client sends `CMSG_AUTH_CONTINUED_SESSION` with the
   `ConnectToKey`; the server links it to the session
   (`World::ProcessLinkInstanceSocket`, `World.cpp:334-355`), calls
   `AddInstanceConnection`, then `HandleContinuePlayerLogin`. From here the
   stream is AES-128-GCM with the derived key and per-direction IVs.
5. **Enter world.** `HandleContinuePlayerLogin` builds the `LoginQueryHolder`,
   sends `SMSG_RESUME_COMMS`, and the async holder resolves into
   `HandlePlayerLogin` → `Player::LoadFromDB` → `AddPlayerToMap`.

## Keepalive and time sync (post-login)

* `CMSG_KEEP_ALIVE` (`0x367F`, `STATUS_NEVER`, `PROCESS_INPLACE`,
  `Opcodes.cpp:512`) is handled early and only re-arms the idle timer. The
  injected path skips it and calls `ResetTimeOutTime()` directly (§5).
* `SMSG_TIME_SYNC_REQUEST` (`0x2DA0`) payload is one `uint32 SequenceIndex`
  (`MiscPackets.cpp:128`). The client answers `CMSG_TIME_SYNC_RESPONSE`
  (`0x3A39`) as `uint32 SequenceIndex; uint32 ClientTime` — that is the *read*
  order in `MiscPackets.cpp:135-139`, which differs from the member declaration
  order. `Player::Update` re-issues the request every ~10 s and logs "possible
  cheater" once more than three go unanswered, so a client (or a bot) must answer
  every one.

## Why iteration 1 does not implement this

Reproducing the digest, the 40-byte session-key generator, the AES-GCM IV
counter and the RSA-signed `ConnectToKey` check is a large, security-sensitive
surface whose only payoff over session injection is "the packets cross a real
socket". The injected path reaches the same `HandlePlayerLogin` through the same
public entry points, with the world thread doing the heavy lifting, and is what
`mod-playerbots` itself does. This file exists so the wire backend can be added
later behind the same `BotLifecyclePlan` without re-deriving any of it.
