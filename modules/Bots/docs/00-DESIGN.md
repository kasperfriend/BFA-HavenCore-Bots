# Bots module — architecture of record

Target core: BFA-HavenCore, TrinityCore-derived, client **8.3.7 (35662)**.
Porting sources: `mod-playerbots/mod-playerbots` (AzerothCore 3.3.5a), which is
itself derived from `ike3/mangosbot` (CMaNGOS 3.3.5a).

Decision taken: **follow the AzerothCore/mod-playerbots model** — a bot is a
`WorldSession` subclass with a socket subclass that goes nowhere, created by the
module and handed to `World::AddSession`. The module drives the core's own
public login entry points. No client protocol, no crypto, no sockets on the
wire.

---

## 1. Constraints, in priority order

1. **Additive only.** The module lives in `modules/Bots/`. Core edits are
   counted, listed and justified in §3. With the module switched off the build
   is byte-identical in behaviour to an untouched core.
2. **Everything off except login.** Every capability a ported bot would
   normally have sits behind a named gate (§8). On first boot a bot logs in and
   stands where it was created. Nothing else.
3. **No dead core hooks.** `ScriptMgr::OnWorldUpdate` and
   `ScriptMgr::OnPlayerUpdate` are *never called* by this core — `grep -rn
   "OnWorldUpdate\|OnPlayerUpdate" src/server | grep -v ScriptMgr` returns one
   unrelated hit in `zone_suramar.cpp`. A module built on them would silently
   never tick. The module owns its own worker thread and uses only hooks that
   are actually invoked.
4. **No guessing.** Every wire format, member offset and DB column quoted below
   carries the file and line it was read from.

---

## 2. How a bot gets into the world

### 2.1 The four core facts that shape the design

**(a) A session without two open sockets is destroyed.**
`WorldSession::Update` returns `false` — which makes `World::UpdateSessions`
`delete` the session (`World.cpp:3120-3128`) — as soon as
`m_Socket[CONNECTION_TYPE_REALM]` is null (`WorldSession.cpp:499`), and
`PlayerDisconnected()` requires *both* the realm and the instance socket to
exist and report `IsOpen()` (`WorldSession.cpp:191-195`).

**(b) `Socket`'s constructor demands a connected stream.**
`Socket(tcp::socket&&)` immediately calls `_socket.remote_endpoint()`
(`shared/Networking/Socket.h:73-76`). `IsOpen()` is non-virtual, `_closed` is
private, and `WorldSocket` fixes `Stream = tcp::socket` — so there is no
"null stream" specialisation available and no way to fake `IsOpen()` without a
real, connected socket object.

**(c) `WorldSocket::SendPacket` only enqueues.**
`WorldSocket.cpp:504-513` pushes an `EncryptablePacket` onto `_bufferQueue` and
returns. Nothing reaches the wire until `WorldSocket::Update()` is called by a
`NetworkThread` (`shared/Networking/NetworkThread.h:134-146`). A bot socket that
nobody updates leaks one heap `WorldPacket` per outbound packet, forever.

**(d) `sScriptMgr->OnPacketSend(this, *packet)` fires on every outbound packet.**
`WorldSession.cpp:302`, immediately before the socket enqueue, on whichever
thread is sending. Signature `OnPacketSend(WorldSession*, WorldPacket const&)`
(`ScriptMgr.h:248`, `ScriptMgr.cpp:1462`). This is the module's window into the
server→bot direction.

### 2.2 Consequences

* `WorldSocket` is `Socket<WorldSocket>` (CRTP, `WorldSocket.h:76`), so it cannot
  be subclassed into a second CRTP type — and `WorldSession` stores a concrete
  `std::shared_ptr<WorldSocket>` in `m_Socket[MAX_CONNECTION_TYPES]`
  (`WorldSession.h:2149`), not a base pointer. There is therefore no
  "`BotSocket : WorldSocket`". The module instead constructs a **real
  `WorldSocket`** over the loopback stub and simply never calls `Start()`.
  `Start()` is the only thing that posts the first `AsyncRead` and the IP-check
  query (`WorldSocket.cpp:55-62`), and the socket is never handed to
  `WorldSocketMgr`, so no `NetworkThread` ever polls it. The module worker calls
  `WorldSocket::Update()` itself: that drains `_bufferQueue` into a
  `MessageBuffer` and `QueuePacket`s it (`WorldSocket.cpp:96-131`), and the base
  `Socket::Update` writes those bytes to the loopback stub, which nobody reads.
  Outbound memory stays bounded, `IsOpen()` stays true because `_closed` is
  never set, and there is no real I/O. `WorldSocket`'s constructor already sets
  `_type = CONNECTION_TYPE_REALM` and does no network work
  (`WorldSocket.cpp:71-78`).
* Because of (b), each `BotSocket` is constructed over a **connected loopback
  TCP stub pair**: the module binds one ephemeral `127.0.0.1` listener, connects
  a socket to it, accepts the peer, closes the listener and *retains* the peer.
  Retaining the peer keeps the stub in `ESTABLISHED`, so the eventual
  `Socket::CloseSocket` → `_socket.shutdown()` (`Socket.h:143-153`) succeeds
  instead of logging an error. Two bots sockets per bot (realm + instance) →
  four file descriptors per bot. The stubs are never read from or written to.
* Because of (d), the module implements the *client* half of the two protocol
  exchanges a standing bot needs, without ever parsing a socket: see §5.

### 2.3 The login sequence, using only public core entry points

The session is added to `sWorld`, so it lives in `World::m_sessions` and the
**world thread** drives it every tick: `World::UpdateSessions` →
`WorldSession::Update` → `ProcessQueryCallbacks` (`WorldSession.cpp:897-910`),
which fires the asynchronous char-enum callback and resolves `_charLoginCallback`
into `HandlePlayerLogin`. The module therefore runs **no login driver of its own**
and must **not** call the opcode handlers directly — that would race the world
thread. It injects client packets through the one public, thread-safe door into
the session's receive path, `QueuePacket`:

```
module thread                                     core world thread (UpdateSessions)
-------------                                     ----------------------------------
provision account + character (§6)
realmSock    = loopback WorldSocket, never Start()ed
instanceSock = loopback WorldSocket, never Start()ed
session = new WorldSession(... realmSock ...)
session->LoadPermissions()                        (RBAC before HasPermission is ever called)
sWorld->AddSession(session)            ────────►  AddSession_ → m_sessions[account] = session
session->QueuePacket(CMSG_ENUM_CHARACTERS) ────►  Update → HandleCharEnumOpcode
                                                  async CHAR_SEL_ENUM → HandleCharEnum
                                                  fills _legitCharacters + sCharacterCache
poll: IsLegitCharacterForAccount(guid)  ◄────────
session->QueuePacket(CMSG_PLAYER_LOGIN)  ──────►  Update → HandlePlayerLoginOpcode
                                                  IsLegitCharacterForAccount ✓, m_playerLoading = guid,
                                                  SendConnectToInstance (RSA-signed, discarded by the stub)
poll: PlayerLoading() && !GetPlayer()   ◄────────
session->AddInstanceConnection(instanceSock) ─►  PlayerDisconnected() now false (both sockets open)
session->HandleContinuePlayerLogin()   ────────►  LoginQueryHolder → DelayQueryHolder
                                                  ProcessQueryCallbacks → HandlePlayerLogin
                                                  Player::LoadFromDB → SetPlayer → AddPlayerToMap → OnPlayerLogin
poll: GetPlayer() && GetPlayer()->IsInWorld() ◄─  bot is standing in the world
```

`QueuePacket(WorldPacket*)` is public (`WorldSession.h:1141`) and forwards to
`_recvQueue.add(...)` (`WorldSession.cpp:312`), a `LockedQueue`, so injecting a
packet from the module thread is safe. `WorldSessionFilter::Process` returns true
while there is no player yet (`WorldSession.cpp:99-104`), and both
`CMSG_ENUM_CHARACTERS` and `CMSG_PLAYER_LOGIN` are `PROCESS_THREADUNSAFE`
(`Opcodes.cpp:397,669`), so the world thread — not a network thread — runs them.
`CMSG_ENUM_CHARACTERS` carries no body (`EnumCharacters::Read()` is empty,
`CharacterPackets.h`); `CMSG_PLAYER_LOGIN` is `ObjectGuid` then `float FarClip`
(`CharacterPackets.cpp:418-422`), serialised with `operator<<(ByteBuffer&,
ObjectGuid const&)` (`ObjectGuid.cpp:404-424`), the exact inverse of the
`operator>>` the handler's `Read()` uses.

Two details that make this work and are easy to get wrong:

* `HandlePlayerLoginOpcode` calls `IsLegitCharacterForAccount`
  (`CharacterHandler.cpp:882`), which consults `_legitCharacters` — a private
  `GuidSet` filled **only** by the async `HandleCharEnum` callback
  (`CharacterHandler.cpp:329,364`). That callback runs inside
  `ProcessQueryCallbacks` on the world thread, so the module must inject
  `CMSG_PLAYER_LOGIN` on a *later* tick, once `IsLegitCharacterForAccount(guid)`
  reports true. Injecting both packets at once makes login fail with "Account
  can't login with that character" and a `KickPlayer()`.
* `PlayerDisconnected()` (`WorldSession.cpp:191-195`) is false only when the
  **realm and the instance** socket are both present and open. In the real client
  flow the instance socket authenticates and `World::ProcessLinkInstanceSocket`
  (`World.cpp:353-354`) calls `AddInstanceConnection` then
  `HandleContinuePlayerLogin`. A bot has no instance handshake, so the module
  calls both itself; each is public (`WorldSession.h:1079`, `:1288`).

`HandleContinuePlayerLogin` is the one step the module invokes directly rather
than through `QueuePacket`, because no realm opcode maps to it — the real trigger
is the instance-socket link. It only reads `PlayerLoading()`/`GetPlayer()`, builds
the holder, calls the thread-safe `SendPacket`, and sets `_charLoginCallback`; the
module calls it once, on the single tick where `PlayerLoading() && !GetPlayer()`
first holds, and never touches the session's query state again. §7 records the
residual ordering constraint against the world thread and the guard used.

`LoginQueryHolder` is defined in `CharacterHandler.cpp:67` and is therefore not
visible to a module — and does not need to be, because the core builds it inside
`HandleContinuePlayerLogin`.

---

## 3. Exactly what this module touches in the core

Two files, no deletions, no reformatting. The module's C++ lives entirely in
`modules/Bots/` and is compiled into the core's existing static `scripts` target,
so there is no new library target and no root `add_subdirectory`.

| File | Change | Why it cannot be avoided |
| --- | --- | --- |
| `cmake/options.cmake` | `option(BOTS_MODULE "..." 1)` | One switch restores a pristine core build. |
| `src/server/scripts/CMakeLists.txt` | under `if (BOTS_MODULE)`: `CollectSourceFiles(${CMAKE_SOURCE_DIR}/modules/Bots ...)` (excluding `tests/`) into the scripts sources, `CollectIncludeDirectories` for the module's own headers, and append `Bots` to `STATIC_SCRIPT_MODULES` before `ConfigureScriptLoader("static" ...)` | `GetScriptModuleList` globs only `src/server/scripts/*` (`ConfigureScripts.cmake:45-62`), so a module outside that tree is invisible to the generated loader. The loader is generated from `STATIC_SCRIPT_MODULES` (`ScriptLoader.cpp.in.cmake`), and appending `Bots` there is what emits the `AddBotsScripts()` call into `AddScripts()`, which `Main.cpp:255` installs via `SetScriptLoader`. `tests/` is excluded because it has its own `main()`. |

`tests/` is excluded from the collected sources and the module's include
directories are added `PRIVATE`, so nothing about the module leaks into other
targets. Turning the module off is `cmake -DBOTS_MODULE=0 ...`, or `git checkout`
those two files — the build is then byte-identical to an untouched core.

Runtime is a separate switch: even with `BOTS_MODULE=1`, the module stays inert
unless `Bots.Enable = 1` (§8). `BOTS_MODULE` decides whether the code is
compiled; `Bots.Enable` decides whether it does anything.

---

## 4. Module layout

```
modules/Bots/
  README.md
  conf/bots.conf.dist            # every key with its default, documented
  docs/00-DESIGN.md              # this file
  docs/01-PROTOCOL-8.3.7.md      # the login protocol as this core implements it
  docs/02-PORTING-GUIDE.md       # 3.3.5a -> 8.3.7 translation matrix + roadmap
  docs/03-TESTING.md             # what was run, what was not, how to run it
  sql/auth/rbac_bots_command.sql # idempotent RBAC rows for `.bot`
  src/
    BotTypes.h                   # namespace-Bots uint aliases (core-free TU support)
    BotIdentity.{h,cpp}          # account/character naming (core-free)
    BotLifecyclePlan.{h,cpp}     # the §2.3 state machine, as a pure function (core-free)
    BotConfig.{h,cpp}            # conf keys + feature gate table (§8)
    BotSocket.{h,cpp}            # loopback stub hub that builds real WorldSockets (§2.2)
    BotSession.{h,cpp}           # module handle on one WorldSession + its two sockets
    BotProvisioner.h             # provisioning interface
    DatabaseBotProvisioner.{h,cpp}  # account + character rows (§6)
    BotManager.{h,cpp}           # roster, worker thread, plan driver, OnPacketSend dispatch
    BotScripts.{h,cpp}           # WorldScript / ServerScript / CommandScript objects
    bots_script_loader.cpp       # AddBotsScripts()
  tests/                         # standalone, g++ only: no boost, no openssl, no mysql
    TestHarness.h
    TestBotIdentity.cpp
    TestBotLifecyclePlan.cpp
    TestBotConfig.cpp
    run_tests.sh
```

There is deliberately **no `modules/Bots/CMakeLists.txt`**: the sources are
compiled as part of the core's `scripts` target (§3), not as a separate library,
which is what keeps the integration to two edited files. `BotIdentity`,
`BotLifecyclePlan` and the gate table in `BotConfig` are free of core includes so
`tests/` can build them with nothing but a C++17 compiler; the core-coupled files
(`BotSocket`, `BotSession`, `BotManager`, the provisioners, `BotScripts`) are not
built by `tests/`.

---

## 5. Keeping a standing bot alive and quiet

Three core mechanisms would otherwise degrade or log-spam a bot that never
answers anything.

**Idle timeout.** `WorldSession::Update` closes the realm socket when
`IsConnectionIdle()` (`WorldSession.cpp:341`, `WorldSession.h:1262`) — i.e. once
`m_timeOutTime` reaches 0. `ResetTimeOutTime()` (`WorldSession.cpp:712`) resets
it to `SocketTimeOutTime`, default 900000 ms (`worldserver.conf.dist:334`). For
a real client it is re-armed from `WorldSocket::ReadDataHandler`
(`WorldSocket.cpp:474`) on every non-early opcode. A bot has no inbound traffic,
so the module's worker thread calls `session->ResetTimeOutTime()` once per tick.
`m_timeOutTime` is `std::atomic<int32>` (`WorldSession.h:1253`) and
`ResetTimeOutTime` reads only config, so this is safe off-thread — and it is
already called off-world-thread by the core itself.

**Time sync.** `Player::Update` calls `SendTimeSync()` every 10 s
(`Player.cpp:1299-1305`), which pushes onto `m_timeSyncQueue` and logs
`"possible cheater"` once the queue exceeds 3 (`Player.cpp:28989-29003`).
`m_timeSyncQueue` is **private** (`Player.h:3123`, after `private:` at :3068),
so the sequence index cannot be read. It does not have to be: the
`SMSG_TIME_SYNC_REQUEST` payload is exactly `uint32 SequenceIndex`
(`MiscPackets.cpp:128`), and `OnPacketSend` (§2.1d) hands the module that
payload with `rpos() == 0`. The module reads the index, builds
`CMSG_TIME_SYNC_RESPONSE` as `uint32 SequenceIndex; uint32 ClientTime` — the
*read* order in `MiscPackets.cpp:135-139`, note it differs from the member
declaration order — and calls `session->QueuePacket(...)`, which is public
(`WorldSession.h:1140`) and thread-safe (`LockedQueue`). The core then processes
it on the world thread via `PROCESS_INPLACE` (`Opcodes.cpp:856`).

**Anti-DOS.** `CMSG_TIME_SYNC_RESPONSE` at 0.1 Hz and `CMSG_KEEP_ALIVE` at the
keepalive interval are both far below `DosProtection`'s per-opcode thresholds,
and both are packets a real client sends at the same rate.

Bot sockets are drained by the worker thread calling `BotSocket::Update()`.
`_bufferQueue` is an `MPSCQueue` (`WorldSocket.h:141`) — many core threads
enqueue from `SendPacket`, one module thread dequeues. That is precisely the
queue's contract, and no core `NetworkThread` ever sees these sockets.

**Liveness.** The world thread owns the `WorldSession`'s lifetime, but it only
deletes one whose realm socket has closed: `WorldSession::Update` returns false —
and `UpdateSessions` then deletes the session — only once
`m_Socket[CONNECTION_TYPE_REALM]` is null or closed (`WorldSession.cpp:481-499`).
The module keeps that socket open (`KeepAlive` re-arms the idle timer so
`IsConnectionIdle()` never fires, and the loopback peer is retained so the socket
never errors), so under normal operation the session is never deleted and the
question does not arise.

For the cases the module does not control — a GM `.kick`, a logout, or shutdown —
the module reads liveness from the socket it holds, never from the session:
`BotSession::IsRealmSocketOpen()` calls `Socket::IsOpen()`, which reads an
`std::atomic<bool>` (`Socket.h:140`), and the module owns the socket by
`shared_ptr` so it outlives the `WorldSession`. Each tick reads that flag first;
if it is false the module stops dereferencing `_session` and the plan retries.
This is best effort: there remains a theoretical TOCTOU window in which the world
thread closes the socket *and* deletes the session within the same tick, between
the module's `IsOpen()` read and a subsequent `_session` dereference. It is
narrow (it needs an external kick at that instant) and cannot occur without one,
because the module never closes the socket itself. `docs/03-TESTING.md` lists it
as a maintainer-verified item; the roadmap's wire-driven backend removes it by
giving the bot a real session the core manages end to end.

---

## 6. Provisioning

### 6.1 Accounts — one per bot

`World::AddSession_` keys the session map by account id and kicks any previous
session for that account (`World.cpp:276-291`), so a shared account would permit
exactly one bot in the world. One `auth.account` row per bot it is.

The injected-session path never runs `WorldSocket::HandleAuthSessionCallback`,
so the `LOGIN_SEL_ACCOUNT_INFO_BY_NAME` constraints (`LoginDatabase.cpp:42-46`:
requires `LENGTH(a.session_key_bnet) = 64`) do not gate login. The module still
writes well-formed rows so that the same accounts work if a real client or the
protocol path in `docs/02-PORTING-GUIDE.md` is ever used against them:

* `battlenet_accounts` — one shared parent row, because
  `LOGIN_SEL_ACCOUNT_INFO_BY_NAME` left-joins it for `locale`, `locked`,
  `lock_country`, `id`, `email`.
* `account` — `username = <ticket>`, `session_key_bnet` = 64 hex chars of a
  module-generated 32-byte key, `os = 'Wn64'`, `expansion` = configured,
  `battlenet_account` = the parent row. `Warden.Enabled` defaults to `false`
  (`World.cpp:1366`) and `InitWarden` is a documented no-op for `"Wn64"`
  (`WorldSession.cpp:944-947`).
* `realmcharacters` — left at 0; only the character-creation limit checks read
  it, and characters are created server-side (§6.2).

The `WorldSession` constructor runs `UPDATE account SET online = 1` when given a
non-null socket (`WorldSession.cpp:150-156`) and the destructor runs
`online = 0` (`WorldSession.cpp:186`), so bot online state stays truthful in the
auth DB for free.

### 6.2 Characters — created server-side

Mirrors the creation block of `HandleCharCreateOpcode`
(`CharacterHandler.cpp:744-796`):

```
Player newChar(botSession);              // session with a null socket is fine here:
newChar.GetMotionMaster()->Initialize(); // Create/SaveToDB only use GetAccountId,
if (!newChar.Create(sObjectMgr->GetGenerator<HighGuid::Player>().Generate(), &createInfo))
    ...                                  // GetRemoteAddress, IsARecruiter, HasPermission
newChar.SetAtLoginFlag(AT_LOGIN_FIRST);
newChar.SaveToDB(loginTrans, charTrans, true);
commit; sCharacterCache->AddCharacterCacheEntry(...);
```

This runs on the module worker thread before any session exists for the
account, so it cannot race the world thread — the same position the core's own
CLI thread occupies. Race/class, starting position, starting level, spells,
skills, action buttons and starting inventory all come from the DB2 stores and
`playercreateinfo*` exactly as they do for a player-created character; nothing
is hardcoded in the module.

Creation is deliberately **not** sent as `CMSG_CHAR_CREATE` in iteration 1: the
created GUID is only reported in `SMSG_CHAR_CREATE` / `SMSG_ENUM_CHARACTERS`,
whose writers are ~200 lines of bit-packed serialisation
(`AuthenticationPackets.cpp:113-200`, `CharacterPackets.h:117+`) that the module
would have to re-implement as a parser. Wire-driven creation is the first item
on the roadmap in `docs/02-PORTING-GUIDE.md`.

---

## 7. Threading

The load-bearing fact is that once `World::AddSession` runs, the **world thread**
owns the session's per-tick work: `World::UpdateSessions` iterates `m_sessions`
and calls `WorldSession::Update`, which unconditionally runs
`ProcessQueryCallbacks` (`WorldSession.cpp:897-910`) — that is what fires the
async char-enum callback and resolves `_charLoginCallback` into `HandlePlayerLogin`.
The module therefore never runs a login driver of its own and never calls the
opcode handlers directly; it only *injects* work and *observes* results.

| Thread | Owns | Touches on the other's data |
| --- | --- | --- |
| module worker (1) | roster, provisioning, login plan, `WorldSocket::Update` (drain), `ResetTimeOutTime`, the single `AddInstanceConnection` + `HandleContinuePlayerLogin` call | reads `GetPlayer()` / `PlayerLoading()` (write-once, set by the world thread) |
| core world thread | `WorldSession::Update`, `ProcessQueryCallbacks`, `HandlePlayerLogin`, the `Player` in world | calls `ScriptMgr::OnPacketSend`, which the module answers with `QueuePacket` |
| core map threads | `Player::Update`, `SendTimeSync` | reach the module only through `OnPacketSend` |

Every crossing point is a documented safe primitive:

* **Module → session (client packets).** `WorldSession::QueuePacket`
  (`WorldSession.h:1141`) forwards to `_recvQueue.add` on a `LockedQueue`
  (`WorldSession.cpp:312`). Injecting `CMSG_ENUM_CHARACTERS` / `CMSG_PLAYER_LOGIN`
  from the worker thread is safe; the world thread dequeues and runs them.
* **Session → module (server packets).** `ScriptMgr::OnPacketSend` runs on
  whatever thread sends, and hands the module a **copy** of the packet
  (`ScriptMgr.cpp:1462`). The module looks the bot up by account id under
  `BotManager::_mutex` and touches only atomic flags (`_characterListReady`) or
  calls the thread-safe `QueuePacket` (time-sync response). The registry lookup
  is O(1) and returns `null` for every non-bot session, so the hook costs real
  players one mutex and one hash.
* **Roster.** One `std::mutex` guards the roster map, the account→session
  registry, the pending-login count and the next index. The worker ticks against
  a snapshot of `shared_ptr<BotRecord>`, and each record has a single writer (the
  worker), so ticking holds no lock across the blocking DB and session-creation
  work. The world thread takes the lock only for the O(1) registry lookup.

**The one residual race, stated plainly.** `HandleContinuePlayerLogin` has no
client opcode — in the real flow `World::ProcessLinkInstanceSocket` calls it on
the world thread (`World.cpp:353-354`) once the instance socket authenticates. A
bot has no instance handshake, so the module calls it (and `AddInstanceConnection`,
which precedes it) directly from the worker thread, once, on the single tick where
`PlayerLoading() && !GetPlayer()` first holds. It writes `_charLoginCallback`,
which the world thread's `ProcessQueryCallbacks` reads. Both are public
(`WorldSession.h:1079,1288`) and the write happens once, but the write and the
world thread's `valid()`/`wait_for()` read of that `std::future` are not
formally synchronised. In practice the module sets it exactly once while the
world thread only reads it, and the value is not observed until a later tick;
`docs/03-TESTING.md` lists "no crash / no torn read across many bot logins" as a
maintainer-verified item, and the roadmap's wire-driven backend (§10.2) removes
the call entirely by authenticating a real instance socket.

Chat commands enqueue spawn/despawn requests onto the worker thread; they never
provision inline. The module never dereferences a `Player*` from the worker
thread except the write-once `GetPlayer()` reads above and `Player::Create` /
`SaveToDB` during provisioning, which happen strictly before that character
exists anywhere in the world — the same position the core's own CLI character
tools occupy. Later AI work runs through `Unit::AddDelayedEvent` /
`EventProcessor` on the owning map, per `CLAUDE.md`.

---

## 8. Feature gates — everything except login is off

`BotConfig` exposes one boolean per capability, checked at the single place the
capability would act. Default `false` for everything that is not `Login`.

```
Bots.Enable                    = 0   # master switch; 0 = module links and does nothing
Bots.Features.Login            = 1   # the only gate on by default
Bots.Features.Movement         = 0
Bots.Features.Combat           = 0
Bots.Features.Chat             = 0
Bots.Features.Group            = 0
Bots.Features.Quests           = 0
Bots.Features.Loot             = 0
Bots.Features.Inventory        = 0
Bots.Features.Trade            = 0
Bots.Features.Mail             = 0
Bots.Features.AuctionHouse     = 0
Bots.Features.Guild            = 0
Bots.Features.Battlegrounds    = 0
Bots.Features.DungeonFinder    = 0
Bots.Features.SkillsTalents    = 0
Bots.Features.ItemsEquipment   = 0
Bots.Features.FollowMaster     = 0
Bots.Features.RespondToInvites = 0
```

With this table, "log in and stand in one place" is an asserted default rather
than an accident of unfinished code. `BotManager` prints the whole table at
startup so a reader of `worldserver.log` can see exactly which capabilities are
live. Every later porting change flips one gate and nothing else.

Idle behaviour in iteration 1: answer `SMSG_TIME_SYNC_REQUEST`, keep the idle
timer armed, and discard every other outbound packet in `BotSocket::Update`.
The bot does not move, turn, sit, respond to invites, whispers, duels, trades or
group prompts, and does not react to being attacked.

---

## 9. Verification strategy, and its honest limits

This sandbox has GCC 12 and make, but **no cmake, no Boost, no MySQL client
headers and no OpenSSL headers**, and the Debian/Ubuntu mirrors are unreachable
(`pypi.org` and `github.com` HTML respond; `apt` and `objects.githubusercontent.com`
do not). A worldserver build is therefore impossible here, and per `CLAUDE.md`
§Tests a clean compile would not count as verification anyway.

Delivered instead, in increasing order of strength:

1. **Standalone unit tests** (`modules/Bots/tests/`) that build with `g++`
   alone. They cover what is new and therefore risky: the login state machine's
   transition table including its failure edges, bot naming and its reserved-name
   guards, the config/gate table's defaults, and the time-sync response framing
   (field order, sizes). `run_tests.sh` builds and runs them; the output is
   pasted into `docs/03-TESTING.md`.
2. **Runtime self-checks inside the server.** `BotManager` verifies at login
   that the character the core loaded is the character the module asked for, and
   logs a single explicit line per bot on reaching `IN_WORLD`. A bot that
   silently fails to enter the world cannot pass.
3. **A documented manual test script** for the maintainer: build, import DB,
   start worldserver, `.bot login 3`, then the exact log lines and in-game
   observations that constitute a pass.

`docs/03-TESTING.md` states plainly which of the three the agent executed and
which require the maintainer's machine.

---

## 10. Roadmap

Recorded now so the seams are visible rather than retrofitted:

1. **Wire-driven character creation** (`CMSG_CHAR_CREATE` +
   `SMSG_CHAR_CREATE`/`SMSG_ENUM_CHARACTERS` parsers) — removes the last
   server-side provisioning shortcut.
2. **A real client transport as an alternative backend**: the module already
   isolates the login plan (§2.3) from its execution, so a second executor that
   speaks AES-GCM over loopback TCP can be added behind the same state machine.
   `docs/01-PROTOCOL-8.3.7.md` records the derivation chain that would need
   implementing.
3. **`Bots.Features.Movement`** — `CMSG_MOVE_*` / `MovementInfo`, the smallest
   next behaviour and the one `mod-playerbots`' `Follow`/`MoveTo` reduce to.
4. **Bot brain skeleton** — the `Trigger`/`Action`/`Strategy` vocabulary from
   `mod-playerbots`, ticked through `Unit::AddDelayedEvent` on the owning map,
   with every strategy disabled except "stand still" so §8 stays the single
   source of truth.
