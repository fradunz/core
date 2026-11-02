# Battle bot behavior in Arathi Basin (AB)

Reference for the rules of the AB bot behavior. It records what was decided, what is implemented and what is still
missing. Warsong Gulch (WSG) is not affected by anything described here.

Status legend for each rule:
- **[decided]** confirmed by the project owner.
- **[assumption]** implementation guess, not confirmed.
- **[done]** implemented and compiling; **[tested]** validated in game; **[todo]** not implemented yet.

## 1. Map sources and files

| What | Where |
|---|---|
| AB map source (hubs and legs) | `contrib/battlebot_routes/ab/ab_graph.json` |
| AB generator | `contrib/battlebot_routes/ab/gen_graph.py` (`python3 contrib/battlebot_routes/ab/gen_graph.py`) |
| AB generated code | `BattleBotABGraph.h` (hub enum) and `BattleBotABGraph.cpp` (legs, hubs, edges, `vPaths_AB`), between the `BEGIN/END GENERATED` markers |
| WSG route snapshot | `contrib/battlebot_routes/wsg/wsg_routes.json` (see section 10) |
| WSG extractor | `contrib/battlebot_routes/wsg/extract_routes.py` (`--check` verifies the snapshot is current) |
| Graph logic (hand written) | `BattleBotABGraph.cpp/.h`: `GetHub`, `GetFlagHub`, `FindNearestHub`, `GetRouteLength`, `GetNextLeg`, `GetLegEnd`, `IsOneWayLeg` |
| Coordinator | `ABCoordinator.cpp/.h` (owned by `PlayerBotMgr::GetABCoordinator()`) |
| Movement / decisions | `BattleBotWaypoints.cpp` (`StartNewPathToObjectiveAB`, `ABCaptureFlag`, `WanderAround*`, `MoveToNextPoint`, `UpdatePathProgress`) and `BattleBotAI.cpp` (`DecideNextAction`, `IsCombatFlagStale`, `OnJustDied`, `OnLeaveBattleGround`) |

To change the AB map: edit `ab_graph.json`, run the generator and review the diff. Never edit the generated regions by
hand. The first and last point of every leg must match a hub exactly; `noRev` = one-way leg (can only be walked from its
first point to its last point). The generator is idempotent: running it on an unchanged JSON leaves the C++ untouched.

Flag nodes (`BG_AB_Nodes`): Stables=0, Blacksmith=1, Farm=2, Lumber=3, GoldMine=4. Flag hubs: S, B, F, L, M.

## 2. Map and routes

- The map is a graph of hubs (25) and short legs (36). The old AB routes were removed. **[decided] [done]**
- The route is the shortest one in yards (Floyd–Warshall, weight = leg length), authoritative, with no extra weights,
  honoring one-way legs. **[decided] [done]**
- Simulated distances: from AS → S 162, M 487, B 512, L 519, F 706; from HS → F 217, B 471, M 485, L 563, S 773.
- Intermediate points are approximate: arrival tolerance and a random offset, no stopping at the point. Only the banner
  requires an exact approach. **[decided] [done]**
- Graveyard = hub at the respawn point, with a single exit route; no special case in the code. **[decided] [done]**
- AS and HS are the starting nodes. **[decided] [done]**
- Arrival radius: the rules ask for ~8–10 yd; implemented **6–9 yd (points) and 6–8 yd (hubs)**. Reason: the shortest
  distance between two hubs is SGY–SH2 = 11.7 yd; a hub radius above ~9 yd would make a bot that respawns at SGY "arrive"
  at SH2 without moving. **[deliberate deviation from the rules]**

## 3. Decisions at hubs

- The objective is re-evaluated at every hub. An intermediate hub never makes the bot wait. **[decided] [done]**
- Waiting happens only at the last hub of an attack, only when the last leg is longer than 100 yd and the flag is not neutral
  (a neutral flag is not an attack: no request, no wait): 15 s or 1 ally with the same objective, whichever comes first; queue
  per objective (side does not matter); whoever arrives after the release goes straight in; defense never waits.
  **[decided] [todo: phase 2d]**
- `DecideNextAction` cascade in AB: `ResumeSavedPath` → `StartNewPathToObjective` (→ `StartNewPathToObjectiveAB`) →
  `ResumeSavedPath` → `StartNewPathFromBeginning` → `StartNewPathFromAnywhere`. **[done]**
- The arrival check also runs between the 1000 ms updates (`UpdatePathProgress`, fast branch in AB), so there is no
  1–2 s pause at hubs. **[done] [tested]**
- After combat, if there is no enemy, no attacker and no cast, the bot resumes its route without waiting for the combat
  flag to expire (`IsCombatFlagStale`). AB only; WSG keeps the old behavior. **[done] [tested]**
- On reaching a flag hub with mode ATTACK the bot calls `ABCaptureFlag`; with mode DEFEND, `WanderAroundHub`.
- Match start: the bot goes to the AS/HS hub (±2 yd) and, during WAIT_JOIN, walks randomly 0–7 yd around it every 4–10 s
  (`WanderAtStart`). **[decided] [done] [tested]**

## 4. Coordinator (`ABCoordinator`)

- Lives outside the battleground classes, owned by `PlayerBotMgr`, one state per instance, bots identified by guid,
  periodic cleanup (`PURGE_INTERVAL` = 30 s). **[decided] [done]**
- A single mutex covers every operation: a bot that leaves the BG is updated on another map, possibly on another
  thread. **[done]**
- Human players do not interfere with the coordinator at all (they count in no role and no slot). **[decided] [done]**
- `epoch` is bumped whenever the state of a flag changes.

### Roles

| Role | Share | Typical size (teams of 10–15) |
|---|---|---|
| Defender | 30% | 4–5 |
| Capturer | 30% | 4–5 |
| General | 40% | 5–7 |

- Deficit draw: deficit = share × (n+1) − count; candidates with deficit ≥ best − tolerance − 0.001; random pick among
  them. **Tolerance = 1.0** (allows up to 2 of difference between roles). **[decided] [done]**
- Objective against the role: the bot fulfills the objective; the role only adjusts behavior. **[decided]**

### Weights

- Defense weight = 20% × flags held; attack weight = the inverse (100 − defense). A flag being converted counts for both
  teams. **[decided] [done]** (`GetDefenseWeight`)
- "Our flag" for defense = held or contested by us. A flag contested by the enemy does not open a new defense slot; a
  defender already assigned to it stays valid. **[assumption]**

### Slots

- Defense slots = `5 × defense_weight / 100`, **truncated** (not rounded), **per flag**: every flag the team holds has that many
  defender slots (`DEFENSE_SLOTS_AT_FULL_WEIGHT` = 5). With 3 flags (60%) that is 3 defenders on each of them, 9 in total.
  **[decided] [done]** (`GetDefenseSlots`)
- Capture: up to 5 per flag (`CAPTURE_SLOTS_PER_NODE`), flag chosen uniformly at random, no distance weighting.
  **[decided] [done]**. The owner will test the hypothesis that, once support requests exist, few bots actually reach
  the farthest flag.
- Home flag: the capturable flag closest to the base of the team (shortest route from AS or HS: Stables for the Alliance, Farm for
  the Horde) is never left without attackers. A bot that is about to attack picks it instead of its random choice while it has
  fewer than `HOME_FLAG_MIN_ATTACKERS` = 1 attacker (supporters do not count). **[decided] [done]**
- Defending our flag: the Defender has priority; the General stays with a chance equal to the defense weight; the
  Capturer only defends if the weight is 100%. **[decided] [done]**
- Capturing a neutral/enemy flag: the Defender only captures if there is no defense slot; the General rolls against the
  weight; the Capturer always captures, except at weight 100%. **[decided] [done]**
- Objective re-evaluation: always on death; on a state change, **if the current objective is still valid the bot never
  changes**. With an excess, the bot that took the objective last loses the slot (`since`, tie-break by guid); who stays
  is irrelevant in practice. **[decided] [done]** (`KeepObjective`; dropping `since` is an optional simplification)
- Bots that have not re-evaluated after a change do not count against slots. **[assumption]**
- A defender walks randomly 0–15 yd around the flag hub, every 5–12 s, only out of combat and on foot
  (`WanderAroundHub`). It does **not** patrol graph legs. **[decided] [done]**
- Wander destinations are validated on the navmesh (no crossing doors, path ≤ 2× straight line + 3 yd).

## 5. Support requests (defense and attack) — **[done: phase 2b, untested]**

Rules **[decided]**:
- 3 slots in addition to the requester, in total, for the whole life of the request.
- Defense: 15 s deadline; a renewal only extends the deadline and never reopens slots. Attack: fixed 20 s, never renewed; after
  that it accepts nobody, and the bots that already accepted keep going to the hub.
- One request per hub, per type and per team. The hub of a request is the **flag hub** (the destination), not the hub where the
  bot was, so the help can come from any side. A team has **only one live attack request** at a time, for any flag.
- Defense renews with no interval while two or more enemies are near the hub or a capture is in progress; attack is never renewed (fixed 20 s).
- Chance of answering, capped at 100%:
  - defense: Defender 2× defense weight, General 1×, Capturer 0.5×;
  - attack: Defender 0.5× attack weight, General 1×, Capturer 2×.
- Only bots within 2 legs or 150 yd of the requesting hub are considered; only when arriving at a hub; rolled once per
  request.
- After accepting, the bot goes to the destination even if the request has expired; it re-evaluates only on arrival and
  out of combat; it may take a defense slot.
- Worst case at one point: 5 capturers + 3 support = 8 attackers.

Triggers **[decided]**:
- **Attack:** a bot arrives at the last hub before a flag (the next leg of its route ends at the flag hub) and that leg is
  longer than 100 yd. The request is for that flag (its hub is the flag hub). Last legs of 100 yd or less (SH1→S 23,
  SH2→S 54, BH1→B 72, FH2→F 31, ...) never ask. A neutral flag never gets a request (nobody defends it). If the team already has a live attack request, nothing is opened.
- **Defense:** a defender near its flag hub (within 60 yd) sees two or more enemy players within 40 yd of the hub. One
  enemy alone does not open a request. While two or more enemies are near the hub, or one is casting the capture spell
  (21651), the request is renewed with no interval. The request is made at the flag hub.

Implementation (`ABCoordinator::RequestSupport / TryAcceptSupport / OnSupportArrived`, `BattleBotAI::ABUpdateDefense`):
- A bot looks for requests only when it reaches a hub it was not at before (`m_abLastHub`), at its next decision.
- Chance and distance: "within 2 legs **or** 150 yd" of the flag hub is read as either condition being enough **[assumption]**;
  legs are counted along the shortest route between hubs (`GetRouteLegCount`), yards are straight line from the bot to the hub.
- A bot that already has the requested flag as its objective (any mode) does not roll, and the requester never answers itself.
- An answered request becomes the bot's objective with a `support` flag: it does not use the normal slots (so up to 8 attackers at
  a flag), it is not re-evaluated by a change of the flags on its way, and it keeps its flag after arriving unless the objective
  becomes invalid. A supporting defender that arrives takes a normal defense slot if one is free.
- A new attack request can only open after the previous one expired (fixed 20 s, so the old 10 s interval no longer matters).
- Radii (60 yd to watch, 40 yd for enemies) are not in the rules **[assumption]**. The number announced in the chat counts the
  enemies within 60 yd of the hub, to show if more are coming behind the ones that opened the request.
- `.battlebot abstatus` lists the live requests of each team (`Support requests: ATTACK Farm 1/3, 9s`) and marks supporters with `[support]`.

## 6. Capture and combat

Already implemented (`ABCaptureFlag`) **[done]**:
- A bot may capture if the node state is 0 (neutral) or `state % 2 == teamIndex` (Alliance=0, Horde=1), the same rule as
  `BattleGroundAB::EventPlayerClickedOnFlag`. States: 0 neutral, 1/2 contested Alliance/Horde, 3/4 held Alliance/Horde.
- Waits for a cast in progress; waits for an ally casting within 5 yd; takes the nearest banner within 30 yd; walks to it
  if farther than 5 yd (`INTERACTION_DISTANCE`); dismounts; `CastSpell(21651)` (`SPELL_CAPTURE_BANNER`).

Detecting an enemy capture (an enemy casting 21651 near the hub) is done in phase 2b, only to renew defense requests.

Phase 2c **[done, untested]**:
- A defender (not a healer) within 60 yd of its flag hub attacks an enemy that is casting the capture (21651) within 40 yd of
  the hub, before any other target and even if it is being attacked (damage interrupts the cast). `ABFindCapturer` /
  `ABPrioritizeCapturer`, and the same enemy comes first in `SelectAttackTarget`.
- An attacker (not a supporter on its way) within 25 yd of its flag hub captures instead of fighting, unless somebody is
  attacking it or an enemy player is within 10 yd (`ABPrioritizeCapture`). **[assumption: 10 yd]** If the 21651 cast is refused
  while the bot is in combat, see problem (C) in section 8.

## 7. Constants (ABCoordinator.cpp / BattleBotWaypoints.cpp)

| Constant | Value |
|---|---|
| `CAPTURE_SLOTS_PER_NODE` | 5 |
| `DEFENSE_SLOTS_AT_FULL_WEIGHT` | 5 |
| `PURGE_INTERVAL` | 30 s |
| `ROLE_SHARE` | {30, 30, 40} |
| `ROLE_TOLERANCE` | 1.0 |
| `AB_HUB_RADIUS` | 25 yd (nearest hub used to enter the graph) |
| Arrival tolerance | 6–9 yd (points), 6–8 yd (hubs) |
| Wander start / defender | 0–7 yd every 4–10 s / 0–15 yd every 5–12 s |
| `BB_UPDATE_INTERVAL` | 1000 ms |

## 8. Known problems and open items

- **Bot stuck at the Farm hub** (seen once in the first match). Cause unknown. Hypotheses: (A) active path with no
  movement (`UpdateWaypointMovement` returns while `m_currentPath` is set); (B) stuck in `ABCaptureFlag` (ally casting,
  banner farther than 5 yd with no path, banner not yet spawned); (C) the 21651 cast failing in a loop during the combat
  flag. Diagnosis: `.battlebot showpath` on the stuck bot + its state (idle/running/casting, ally nearby, flag state).
  A watchdog (5 s with a path and no movement → drop it and decide again) only goes in if the owner approves.
- WSG does not have the immediate post-combat resume (only AB has `IsCombatFlagStale`).
- The arrival radius differs from the rules (see section 2).
- `MyBranch` still exists on the remote (`origin`); it was renamed `radwar`. Delete it only if asked.

## 9. Phases

| Phase | Content | Status |
|---|---|---|
| 1 | Hub graph, routes, wander, radius, post-combat | done and tested |
| 2a | Coordinator: roles, objectives, slots | done, being tested |
| 2b | Support requests | done, untested |
| 2c | Active defense (switch target, defense request, attacker prioritizes the banner) | done, untested |
| 2d | Waiting for an ally on the last leg of an attack | todo |

## 10. WSG route map

Unlike AB, the WSG routes are written by hand in `BattleBotWaypoints.cpp` (`vPath_WSG_*`, listed in `vPaths_WS`), and that
file stays the source of truth. `contrib/battlebot_routes/wsg/wsg_routes.json` is a data snapshot of them: 24 routes,
332 points. Each route has `name`, `noReverse`, `comment` and `points` (`[x, y, z, callback]`, where the callback is the
C++ function run on arrival at the point, or empty).

- Regenerate after editing the C++ routes: `python3 contrib/battlebot_routes/wsg/extract_routes.py`.
- Check it is current: `python3 contrib/battlebot_routes/wsg/extract_routes.py --check` (exit code 1 if outdated).
- `noReverse` mirrors `vPaths_NoReverseAllowed` (graveyard exits and graveyard-jump routes, which cannot be walked backwards).
- Not captured in the snapshot: the base roof routes (`HordeTunnel_to_HordeBaseRoof`, `AllianceTunnel_to_AllianceBaseRoof`)
  are only chosen at random while our own flag is taken (`IsPathAllowedForRandomChoice`).
