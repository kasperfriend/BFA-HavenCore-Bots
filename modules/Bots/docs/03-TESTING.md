# Bots module — testing, and what was actually run

This file states plainly which verification the agent performed and which
requires the maintainer's machine. It exists because a clean compile is not
verification (`CLAUDE.md` §Tests), and because most of this module cannot be
compiled in the sandbox it was written in.

## What the sandbox could and could not do

The sandbox has GCC 12 and `make`, but **no cmake, no Boost headers, no MySQL
client headers and no OpenSSL headers**, and the package mirrors are unreachable.
A `worldserver` build is therefore impossible here. Everything that depends on
the core (`BotSocket`, `BotSession`, `BotManager`, the provisioners,
`BotScripts`, and the two CMake edits) was written against APIs read directly
from the core sources — each claim in `docs/00-DESIGN.md` carries the file and
line it was read from — but **none of it was compiled**.

## 1. Standalone unit tests — RUN BY THE AGENT, PASSING

`modules/Bots/tests/` builds with `g++` alone (no Boost, no OpenSSL, no MySQL,
no worldserver) and covers the three core-free units, which are the parts that
are new and therefore risky: the naming scheme, the login state machine, and the
config/gate table.

How to run:

```
bash modules/Bots/tests/run_tests.sh      # from the repository root
```

Output at the time of writing (GCC 12):

```
compiling with g++ (12)
output: modules/Bots/tests/.build/bots_tests

build ok, running

modules/Bots standalone tests
no boost, no openssl, no mysql, no worldserver required

41 test cases, 41 passed, 0 failed, 0 assertions failed
```

Exit status 0 means pass. The 41 cases:

* **`BotIdentity` (14)** — the run-free base-25 naming scheme: worked examples,
  no adjacent-equal letters over 200k indices, encode/decode bijection and
  round-trip over 200k indices, decode rejects malformed and adjacent-equal
  words, character names are alphabetic and within the client limit, prefix
  normalisation and validation, the three-consecutive-identical rule matches the
  core check (`ObjectMgr.cpp`), every valid prefix × index pair up to the name
  limit validates, and a bad prefix cannot be rescued by the suffix.
* **`BotLifecyclePlan` (18)** — the login transition table: the happy path
  reaches `InWorld` in the documented action order, `InWorld` is stable, the
  login gate off means no bot is created (and an already-logged-in bot is left
  alone), session death triggers a retry from every waiting state, retry delay
  gates the return to provisioning, a state timeout escalates to retry then to
  give-up, an unknown character GUID after a ready list is a retry, a loaded
  character mismatch is a permanent failure, `Failed` is absorbing, and the retry
  count is respected across a full simulation.
* **`BotConfig` (9)** — only the login gate is on by default, every gate has a
  unique documented key, config overrides defaults, defaults match the documented
  values, zero/negative timings fall back to defaults, and the gate report lists
  every gate and the master switch.

These tests constrain the executor: `BotManager::ApplyAction` mirrors the exact
action contract the `BotLifecyclePlan` test simulator asserts, so the tested
table is the table the live module runs.

## 2. Runtime self-checks inside the server — WRITTEN, NOT RUN

These fire only in a live `worldserver`, so the agent could not exercise them:

* `BotManager::ApplyAction` logs one explicit `IN WORLD` line per bot on reaching
  `InWorld`, including the account name and character GUID.
* `BotSession::LoadedCharacterMatchesRequest` compares the GUID the core actually
  loaded against the one the module requested; a mismatch drives the plan to a
  permanent `Failed` rather than a silent wrong-character login.
* `BotManager::Initialize` prints the whole feature-gate table at startup, and
  refuses to enable itself if `Bots.NamePrefix` is invalid or the loopback socket
  hub cannot be opened.

A bot that silently fails to enter the world cannot pass these: it either logs
`IN WORLD` or escalates to `RetryLater` / `GiveUp` with an error line.

## 3. Manual test script for the maintainer — NOT RUN

Requires a machine that can build the core. Steps:

1. **Build.** `cmake -DBOTS_MODULE=1 ...` (the default) and build `worldserver`.
   Confirm the configure log lists `Bots` under the static script modules and
   that `AddBotsScripts` is referenced by the generated `ScriptLoader.cpp`.
2. **Database.** Import `modules/Bots/sql/auth/rbac_bots_command.sql` into the
   **auth** database.
3. **Config.** Copy `modules/Bots/conf/bots.conf.dist` into `worldserver.conf`
   (or include it) and set `Bots.Enable = 1`. Leave every feature gate at its
   default — only `Login` is on.
4. **Start.** Launch `worldserver`. Expect in the log:
   * `Bots module enabled. Feature gates:` followed by the table with only
     `Login = ON`.
   * `Loopback socket hub listening on 127.0.0.1:<port>`.
   * `Worker thread started (tick every 1000 ms).`
5. **Spawn.** As an administrator (or from the console), run `.bot login 3`.
   Expect `Queued 3 bot(s) for login.`
6. **Observe a pass.** Within a few seconds per bot, expect:
   * `Provisioned character Bot<X> (...) on account <n>` for each new bot.
   * `Bot Bot<X> (account bot<n>, character <guid>) is IN WORLD.` for each.
   * `.bot status` lists all three as `InWorld`.
7. **In game.** Log in a real character, GM-visible, and confirm the three bots
   are present in the world at their creation position, standing still. They must
   not move, turn, sit, or respond to whispers, invites, duels, trades or group
   prompts, and must not react to being attacked. `/who` and the online count
   should include them.
8. **Stability.** Leave them for several minutes. Confirm:
   * No `possible cheater` time-sync spam in the log (the module answers every
     `SMSG_TIME_SYNC_REQUEST`).
   * No bot is kicked for idleness (the worker re-arms the idle timer).
   * No growth in the worldserver's resident memory from undrained outbound
     packets (the worker drains both sockets each tick).
   * **The residual race in `docs/00-DESIGN.md` §7 does not bite**: across many
     logins (and `.bot logout` / `.bot login` cycles) there is no crash and no
     torn read around `HandleContinuePlayerLogin`.

## Summary

| Layer | Status |
| --- | --- |
| Core-free units (naming, state machine, config) | **Agent-run, 41/41 passing** |
| Core-coupled C++ (`BotSocket`/`BotSession`/`BotManager`/provisioners/`BotScripts`) | Written against read core APIs; **never compiled** — needs the maintainer's build |
| CMake integration (2 files) | Written; **never configured/built** — needs the maintainer's cmake |
| Runtime self-checks and manual script | Written; **not run** — needs a live server |

Nothing here claims more than it did. The agent verified the logic it could
isolate; the integration is reasoned from the core sources line by line and must
be confirmed by a real build and the script above.
