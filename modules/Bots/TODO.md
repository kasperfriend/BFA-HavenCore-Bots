# Bots module — TODO / untested

Everything here is **written but not verified on a real build or server**. The
sandbox this module was authored in has GCC 12 but no cmake, Boost, MySQL or
OpenSSL headers, so only the core-free units could be compiled and tested
(`modules/Bots/tests/`, 41/41 passing — see `docs/03-TESTING.md`).

Work top to bottom: the build has to succeed before any runtime item can be
checked. Tick items off as they are confirmed on a machine that can build the
core.

---

## 1. Build — compile the core-coupled half (never compiled)

These files include core headers and were written against APIs read line-by-line
from the source, but have **not** been through a compiler. Expect include-path
and signature drift.

- [ ] `src/BotSocket.{h,cpp}` — loopback stub hub. Check: `Trinity::Asio::IoContext`
      converts to `tcp::acceptor`/`tcp::socket` ctors; `WorldSocket(tcp::socket&&)`
      is public; `Socket::Update()`/`IsOpen()` reachable through `WorldSocket`.
- [ ] `src/BotSession.{h,cpp}` — Check: `WorldPacket(uint32 opcode, size_t res)` ctor;
      `operator<<(ByteBuffer&, ObjectGuid const&)` visible (ObjectGuid.h);
      `QueuePacket`, `AddInstanceConnection`, `HandleContinuePlayerLogin`,
      `SetWorldSession`, `ResetTimeOutTime`, `GetPlayer`, `Player::IsInWorld`/`GetGUID`
      all public and correctly signed.
- [ ] `src/DatabaseBotProvisioner.{h,cpp}` — Check: `sAccountMgr->CreateAccount(...)`
      signature; `AccountMgr::GetId`; `LOGIN_SEL_BNET_ACCOUNT_ID_BY_EMAIL`,
      `LOGIN_INS_BNET_ACCOUNT`, `LOGIN_UPD_EXPANSION` enum names; `setString`/`setUInt8`/
      `setUInt32`; `CharacterDatabase.PQuery`; `Player::Create`, `SaveToDB(bool)`,
      `CleanupsBeforeDelete`, `GetMotionMaster()->Initialize()`,
      `sObjectMgr->GetGenerator<HighGuid::Player>().Generate()`,
      `sCharacterCache->AddCharacterCacheEntry`; `CharacterCreateInfo` ctor arg order;
      a null-socket `WorldSession` is constructible on the worker thread.
- [ ] `src/BotManager.{h,cpp}` — Check: `WorldSession` ctor argument list/order
      (id, name, bnetAccountId, sock, sec, expansion, mute, os, locale, recruiter,
      isRecruiter, AuthFlags, bnetName); `sWorld->AddSession`; `WorldSocket::SetWorldSession`;
      `WorldSession::LoadPermissions`; `ConfigMgr` (`sConfigMgr`) `GetBoolDefault`/
      `GetIntDefault`/`GetStringDefault`; `WorldPacket::GetOpcode`/`size`/`contents`.
- [ ] `src/BotScripts.{h,cpp}` + `src/bots_script_loader.cpp` — Check: `WorldScript`,
      `ServerScript`, `CommandScript` base ctors; `ServerScript::OnPacketSend(WorldSession*, WorldPacket&)`
      override signature; `ChatCommand` ctor `(name, perm, allowConsole, handler, help, children)`;
      `ChatHandler::SendSysMessage`/`PSendSysMessage`.
- [ ] Resolve any `#include` that does not map to a real header path (the module
      relies on the `scripts` target's include dirs + `game`/`game-interface`).

## 2. Build — CMake integration (never configured)

Two core files are edited (`docs/00-DESIGN.md` §3). Neither has been through `cmake`.

- [ ] `cmake/options.cmake` — `option(BOTS_MODULE ... 1)` parses.
- [ ] `src/server/scripts/CMakeLists.txt` — under `if (BOTS_MODULE)`:
      `CollectSourceFiles(${CMAKE_SOURCE_DIR}/modules/Bots ... tests)` picks up only
      `src/*.{h,cpp}` and **excludes `tests/`** (which has its own `main()`);
      `CollectIncludeDirectories` adds `modules/Bots/src`; `Bots` is appended to
      `STATIC_SCRIPT_MODULES`; `target_include_directories(scripts PRIVATE ...)` runs
      **after** `add_library(scripts ...)`.
- [ ] The generated `ScriptLoader.cpp` contains a forward decl and a call to
      `AddBotsScripts()` (grep the build dir's `gen_scriptloader/static/ScriptLoader.cpp`).
- [ ] `cmake -DBOTS_MODULE=0` produces a build with **no** reference to the module
      (pristine-core check).
- [ ] The precompiled header (`USE_SCRIPTPCH`) does not conflict with the module's
      sources being part of the `scripts` target.

## 3. Database — `sql/auth/rbac_bots_command.sql` (never imported)

- [ ] Imports cleanly into the **auth** DB; `rbac_permissions`,
      `rbac_default_permissions` column names/order match this core's schema
      (`(id, name)` and `(secId, permissionId, realmId)` were inferred from
      `AccountMgr.cpp:460-525`, not from a schema file).
- [ ] Permission id `2100` does not collide with anything already present.
- [ ] After import, an Administrator (secId 3) and Console (secId 4) can run `.bot`.

## 4. Runtime — login flow (never run)

- [ ] `.bot login 3` produces, per bot, a `Provisioned character ...` line then a
      `Bot ... is IN WORLD.` line (`docs/03-TESTING.md` §3 for the full script).
- [ ] The injected `CMSG_ENUM_CHARACTERS` → `SMSG_ENUM_CHARACTERS_RESULT` →
      `CMSG_PLAYER_LOGIN` → `HandleContinuePlayerLogin` sequence actually reaches
      `HandlePlayerLogin` and puts the `Player` in the world. **The char-enum step is
      async on the world thread; confirm `_legitCharacters` is filled before the login
      packet is processed** (the module waits for the enum-result packet — verify the
      ordering holds in practice).
- [ ] Bots are visible in-world, standing still at their creation position.
- [ ] `.bot status` lists them as `InWorld`; `.bot gates` prints the gate table.

## 5. Runtime — stability & the known race (never run)

- [ ] No `possible cheater` time-sync spam (every `SMSG_TIME_SYNC_REQUEST` answered
      via `OnPacketSend` → `QueuePacket`).
- [ ] No idle kicks over several minutes (`Maintenance` re-arms the timer).
- [ ] No resident-memory growth from undrained outbound packets (`Maintenance` drains
      both sockets each tick).
- [ ] **Residual race (`docs/00-DESIGN.md` §7):** `HandleContinuePlayerLogin` is called
      from the module worker thread and writes `_charLoginCallback`, which the world
      thread reads in `ProcessQueryCallbacks`. Not formally synchronised. Confirm no
      crash / torn read across many logins and `.bot logout` + `.bot login` cycles.
- [ ] **Liveness TOCTOU (`docs/00-DESIGN.md` §5):** a GM `.kick`/logout/shutdown that
      closes the realm socket *and* deletes the session within one module tick could
      still race the `IsRealmSocketOpen()` guard. Confirm external kicks do not crash
      the worker.
- [ ] `.bot logout` teardown (`KickPlayer` + erase) leaves no dangling session and no
      crash on shutdown.

## 6. Config — `conf/bots.conf.dist` (never loaded by a server)

- [ ] Keys match what `BotConfig::Load` reads (cross-checked during authoring, but
      never round-tripped through the real `ConfigMgr`).
- [ ] `Bots.Enable = 0` (default) → module logs one line and does nothing.
- [ ] An invalid `Bots.NamePrefix` makes `Initialize` refuse to enable (logged), rather
      than failing on the Nth bot.

## 7. Cleanup / known loose ends

- [ ] `BotConfigValues::LoginDelayMs` and `CharacterListDelayMs` are parsed and
      unit-tested but **not yet consumed** by the executor — either wire them into the
      login pacing or drop them (kept for now to avoid churning the passing config tests).
- [ ] `README.md` / docs describe `.bot login` from console or an admin; confirm the
      command behaves sensibly from both (console has no player session).

---

### Verified in the sandbox (for contrast — do not re-litigate)

- Core-free units: `BotIdentity` (run-free naming), `BotLifecyclePlan` (login state
  machine), `BotConfig` (gates) — **41/41 tests pass** via `bash modules/Bots/tests/run_tests.sh`.
- The runtime seams the module depends on were confirmed present in the core source:
  script self-registration (`ScriptRegistry<...>::AddScript` in each base ctor),
  `sScriptMgr->OnPacketSend` (`WorldSession.cpp:303`), `OnStartup`
  (`ScriptMgr.cpp:2226`), `QueuePacket`→`_recvQueue.add` (`WorldSession.cpp:312`),
  `WorldSession::ProcessQueryCallbacks` driving login (`WorldSession.cpp:897-910`).
