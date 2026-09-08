# Bots module — porting guide, 3.3.5a → 8.3.7

The source material is `mod-playerbots/mod-playerbots` (AzerothCore 3.3.5a),
itself derived from `ike3/mangosbot` (CMaNGOS 3.3.5a). The target is
BFA-HavenCore (TrinityCore-derived, 8.3.7 / 35662). This is not a line-by-line
port: 3.3.5a and 8.3.7 differ in packet layout, the login handshake, the
movement system and the scripting API. The strategy is to port the *architecture*
(a bot is a session; behaviour is gated strategies ticked per bot) and re-derive
every wire and DB detail from the 8.3.7 core.

## What "port the architecture, not the code" means here

`mod-playerbots`' design decisions that carry over unchanged:

* A bot is a `WorldSession` with a socket that goes nowhere, added to the world
  like any session. (Here: §2.2–§2.3 of the design.)
* Behaviour is a set of `Trigger` / `Action` / `Strategy` objects ticked per bot;
  the default behaviour is cheap and expensive behaviour is opt-in. (Here: the
  feature gates in §8 — iteration 1 opts into nothing but `Login`.)
* Stability, performance and predictability are valued over realism.

What does **not** carry over:

* 3.3.5a packet structures. 8.3.7 uses the `WorldPackets::` namespace, bit-packed
  GUIDs (`operator<<`/`>>` with a mask byte, `ObjectGuid.cpp:404-430`), and
  AES-GCM framing. Every packet the module touches was re-read from
  `src/server/game/Server/Packets/`.
* The 3.3.5a login handshake. 8.3.7 splits realm and instance connections and
  signs the instance handoff (`docs/01-PROTOCOL-8.3.7.md`).
* 3.3.5a movement. 8.3.7 `MovementInfo` / `CMSG_MOVE_*` are different; movement
  is a later gate, not iteration 1.

## Translation matrix

| Concept (3.3.5a / mod-playerbots) | 8.3.7 equivalent used here | Source |
| --- | --- | --- |
| `PlayerbotHolder` / bot manager | `Bots::BotManager` (roster + worker + plan driver) | this module |
| `PlayerbotAIBase` session subclass | real `WorldSession`, module holds a `BotSession` handle | `WorldSession.h` |
| fake/null socket | real `WorldSocket` over a loopback stub, never `Start()`ed | `WorldSocket.h:76`, `Socket.h:68` |
| bot "login" AI state | `Bots::BotLifecyclePlan` pure state machine | this module |
| `PlayerbotAIConfig` | `Bots::BotConfig` + feature gates | this module |
| bot naming | `Bots::BotIdentity` run-free base-25 names | this module |
| character creation packet | server-side `Player::Create` + `SaveToDB` | `CharacterHandler.cpp:744-796` |
| strategy enable flags | `Bots.Features.*` gates, only `Login` on | §8 |
| per-tick AI | not ported in iteration 1; roadmap item 4 | §10 |

## Reserved-name and naming rules (the part most likely to break silently)

The core rejects a character name that is empty, too short, too long
(`MAX_PLAYER_NAME = 12`), non-alphabetic, or contains **three consecutive
identical letters** (`ObjectMgr.cpp`, checked on create and enum). Plain base-26
index rendering violates the last rule — index 703 renders `AAA`, 540 renders
`TT`. `BotIdentity::RunFreeBase26` is a bijection onto run-free words (radix 25
after the leading letter, each letter differing from its left neighbour) so the
suffix can never contain a run, and `BotManager::Initialize` rejects a
`Bots.NamePrefix` that is itself invalid. See `docs/03-TESTING.md` for the tests
that pin this.

## Feature gates as the porting contract

Every capability beyond login is a named gate defaulting to off (§8). Porting a
`mod-playerbots` behaviour means: flip one gate, implement the behaviour behind
its check, and add the packet/movement/DB work it needs. Nothing else in the
module changes. This keeps "log in and stand in one place" an asserted default
rather than an accident of unfinished code, and makes each later change small and
reviewable.

## Roadmap (mirrors §10 of the design)

1. **Wire-driven character creation.** Replace server-side `Player::Create` with
   a real `CMSG_CHAR_CREATE` and parsers for `SMSG_CHAR_CREATE` /
   `SMSG_ENUM_CHARACTERS`. Removes the last provisioning shortcut.
2. **Real client transport.** A second executor behind the same
   `BotLifecyclePlan` that speaks the AES-GCM protocol in
   `docs/01-PROTOCOL-8.3.7.md` over loopback TCP. Also removes the residual
   `HandleContinuePlayerLogin` race (§7), because a real instance socket
   authenticates and the core calls it on the world thread.
3. **`Bots.Features.Movement`.** `CMSG_MOVE_*` / `MovementInfo`; the smallest
   next behaviour, and the one `mod-playerbots`' `Follow` / `MoveTo` reduce to.
4. **Bot brain skeleton.** The `Trigger` / `Action` / `Strategy` vocabulary,
   ticked through `Unit::AddDelayedEvent` on the owning map, with every strategy
   disabled except "stand still" so §8 stays the single source of truth.

## A note on licensing

`mod-playerbots` is GPLv2; this core is GPLv3 (see the file headers). Code was
not copied — the architecture was re-implemented against the 8.3.7 sources, and
every core fact is cited to a file and line. If a later change does lift code
from a GPLv2 source, the combined work must remain compatible with this core's
GPLv3 terms.
