# Bots module

Player bots for BFA-HavenCore (TrinityCore-derived, client **8.3.7 / 35662**),
ported in architecture from `mod-playerbots` (AzerothCore 3.3.5a) /
`ike3/mangosbot` (CMaNGOS 3.3.5a).

**Iteration 1 does exactly one thing: a bot logs in and stands where it was
created.** Every other capability is behind a feature gate that defaults to off.

## How it works, in one paragraph

A bot is a real `WorldSession` the module constructs over a loopback TCP stub
that goes nowhere, then hands to `World::AddSession`. From that point the core's
**world thread** drives it exactly like a player session. The module logs the bot
in by injecting client packets (`CMSG_ENUM_CHARACTERS`, `CMSG_PLAYER_LOGIN`)
through the public, thread-safe `WorldSession::QueuePacket`, and answers the two
server→client packets a standing bot must (`SMSG_TIME_SYNC_REQUEST`, and the
idle timer). No client protocol, no crypto, no packets on the wire.

## Read these, in order

1. `docs/00-DESIGN.md` — the architecture of record. Constraints (§1), how a bot
   gets into the world (§2), the exact core touch (§3), layout (§4), keepalive and
   time sync (§5), provisioning (§6), threading and its one residual race (§7),
   feature gates (§8), verification (§9), roadmap (§10).
2. `docs/03-TESTING.md` — what was actually run (41 standalone unit tests,
   passing) versus what needs the maintainer's machine (everything that touches
   the core, which was never compiled here).
3. `TODO.md` — the actionable checklist of everything not yet built or run,
   with the specific API/signature risks to confirm on a real build.
4. `docs/01-PROTOCOL-8.3.7.md` — the real login protocol and crypto, recorded for
   the roadmap's wire-driven backend. Iteration 1 does not implement it.
5. `docs/02-PORTING-GUIDE.md` — the 3.3.5a → 8.3.7 translation matrix and the
   porting contract (one gate per behaviour).

## Building

The module is compiled into the core's static `scripts` target. Two core files
are touched (`docs/00-DESIGN.md` §3):

* `cmake/options.cmake` — `option(BOTS_MODULE ... 1)`.
* `src/server/scripts/CMakeLists.txt` — collects `modules/Bots` sources (excluding
  `tests/`) into the scripts library and appends `Bots` to `STATIC_SCRIPT_MODULES`
  so the generated loader calls `AddBotsScripts()`.

Build with `-DBOTS_MODULE=1` (the default). To restore a pristine core, build with
`-DBOTS_MODULE=0` or `git checkout` those two files.

## Enabling

`BOTS_MODULE=1` only compiles the code; the module stays inert at runtime until
you turn it on:

1. Import `sql/auth/rbac_bots_command.sql` into the **auth** database (grants the
   `.bot` command to administrators and console).
2. Copy `conf/bots.conf.dist` into `worldserver.conf` (or include it) and set
   `Bots.Enable = 1`. Leave every feature gate at its default.
3. Start `worldserver`, then `.bot login 3`.

`.bot login <count> | logout | status | gates` are the commands. Expect an
`IN WORLD` log line per bot; see `docs/03-TESTING.md` §3 for the full pass
criteria.

## Running the standalone tests

The core-free units (naming, login state machine, config) are unit-tested with
nothing but a C++17 compiler:

```
bash modules/Bots/tests/run_tests.sh      # from the repository root
```

This is the only part the agent could verify in the sandbox it was written in.
