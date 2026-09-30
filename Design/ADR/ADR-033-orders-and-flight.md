# ADR-033 — Orders and flight on the plane

**Status:** accepted, 2026-09-29 · **Lands with:** phase 4 of [`Design/MvpPlan.md`](../MvpPlan.md), its tasks 1 and 3 and the first two of its tests · **Amends:** [ADR-015](ADR-015-client-server-boundary.md)'s commands, which gain the game's own command and its payload; [ADR-017](ADR-017-sector.md)'s flight, which ships on the plane fly with limits from their profiles, and its foreclosure of a ship that steers across its route, lifted for ships that have no route; [ADR-030](ADR-030-skirmish.md)'s skirmish, whose ships now move; [ADR-032](ADR-032-sides-sessions-and-fog.md)'s command log, whose records carry the payload; and [`AGENTS.md`](../../AGENTS.md) §2, whose `GameCore` and `GameLogic` rows gain the orders and the flight · **Amended by:** [ADR-035](ADR-035-combat.md), whose orders and order states are version 2, with the attack and the attack-move, and whose ships bear, close and jink

## Context

Phase 4 lets the player command ships. The plan's first task asks for an ADR of orders and flight:
- the orders: move, stop and hold;
- a group's move keeps its members' offsets (§2.2's subset of G62);
- ships steer on the plane within their profiles' acceleration, turn rate and speed cap (G7's one plane, G29), keep apart from one another, and keep out of the cores' and asteroids' spheres (§2.2's subset of G61);
- orders are commands in `GameCore`'s payload;
- it amends ADR-017's flight and lifts its foreclosure on steering off a route.

The concept's §9 sets out the rest. Ships find paths around the obstacles and steer along them with ADR-017's flight model. The skirmish takes orders while it is paused. The client draws its own marker for an order at once, since the order shows in a snapshot only after the next tick and the interpolation delay (ADR-018).

ADR-015's commands are the engine's own: pause, resume, detonate and restore. An order is the game's, and the engine must carry it without knowing what it says (R9).

## Decision

**The game's command** (amends ADR-015).
- **A fifth kind of command, `Game`,** carries a payload the engine does not read. It names no entity, and its payload is never empty. The engine's four kinds carry no payload.
- **The layout.** A command is its `u32` kind, its `u32` entity, a `u32` size, then that many bytes of payload. That is layout version 4. The protocol stays 2: the orders are the command set G16 and §9 give protocol version 2.
- **Its refusal.** A message that breaks those rules is `MalformedMessage`. `NeuronServer::CommandRefusal` gains two names: `MalformedCommand`, for a payload the world cannot read, and `NotOrderable`, for an order that names an entity it cannot apply to. A world without commands of its own, such as the sector, refuses every game command as `MalformedCommand`.
- **How it reaches the world.** The host passes a game command it does not refuse to `World::ApplyGameCommand`, with the side of the session that sent it. That happens while the world is paused as well: the host applies every command as it arrives, and pausing stops only `Advance`.
- **The command log** (amends ADR-032). It is version 2, and a command record ends with the payload's `u32` size and its bytes. The decoder refuses as `MalformedLog` an engine command with a payload, and a game command without one or naming an entity.

**The orders** (`GameCore/Orders.h`), little-endian:
- a `u8` version, 1, and a `u8` kind: 1 for a move, 2 for a stop, 3 for a hold;
- a `u16` count of ships, 1 to 256, then each ship's `u32` id, never 0 and each once;
- for a move alone, the target on the plane as its `f32` x and z, both finite.

`DecodeOrder` refuses by name: `Truncated` before `UnsupportedVersion`, and then `MalformedOrder` for everything else, including bytes to spare. `EncodeOrder` throws `std::invalid_argument` for an order its decoder would refuse.

**What each order does.**
- **A move** sends each whole ship it names to the target plus the ship's offset from the middle of those ships, as a group (below).
- **A stop** halts each ship and forgets its move at once. The ship brakes to a halt along its heading, and then idles.
- **A hold** halts each ship the same way and holds it there. In this phase a hold moves a ship as a stop does. What differs is its state, and phase 5's rules of engagement will give it more.
- **Whole ships only.** An order leaves out a ship that has detonated since it was sent, since a command can race a detonation (ADR-015). A restored ship is idle, and brakes to a halt from the speed it had.

**Refusals** (`Skirmish::Refuses`):
- `MalformedCommand`: a payload that does not decode as an order;
- `NotOrderable`: an order naming an entity that is no ship, such as a core or an asteroid, or an id the skirmish does not hold;
- `OtherSidesEntity`: an order naming another side's ship, and every order from an observer.

A refusal is counted and forgotten, as ADR-032's are. The first entity that fails decides the name.

**What a side learns of its orders.** Each snapshot's payload holds the order states of the side's own ships: a `u8` version, 1, and a `u16` count. Then each ship that moves or holds takes 12 bytes: its `u32` id, its `u8` state (1 moving, 2 holding), three reserved bytes of zero, and a move's destination as `f32` x and z (0 while holding). A ship that does neither is left out. The observer learns every side's. The payload is written every tick, three bytes with nothing in it, and `DecodeOrderStates` refuses as `DecodeOrder` does.

**The obstacles and the clearances** (`GameLogic::Clearances`, §2.2 on G61).
- **The obstacles** are the cores and the asteroids. Each is its sphere: half its composite's box's diagonal, about its middle, as ADR-017 measures a model's. They never move, and a detonated one stays an obstacle while detonation is only a test's.
- **The keep-out.** A ship keeps its middle out of a circle on the plane about each obstacle's middle. The circle's radius is the obstacle's sphere, the ship's and a margin of 10 units (`CLEARANCE_MARGIN`). The circle is the whole sphere's, however far off the plane the obstacle's middle stands, so the ship's sphere never enters the obstacle's.
- **The path circle** is the keep-out with 12 units more (`PATH_SLACK`). Those 12 units are the room a ship's corners take (below).
- **The graph.** Every path circle has the regular octagon that circumscribes it, 0.05 units wider. The octagon's corners are at 22.5° and every 45° after, spelled out rather than computed, so that no build's cosine moves one. A corner that lies within another path circle is dropped. Two corners are joined when the segment between them clears every path circle. The graph is built once for each ship design, when the skirmish is made, since obstacles never move. An octagon's corner stands 8.2 % of the radius past its circle, so a way is a little longer than the one around the circles themselves. A way is a few straight legs between exact corners, which a ship flies corner to corner rather than by pure pursuit along it.
- **Where a move goes** (`Reachable`). A target outside every path circle is where the move goes. A target within one goes to the nearest point outside them all, 0.05 units out. That point lies where a ray from the target outward meets a circle, or where two circles meet, so the search tries both and keeps the nearest one outside every circle.
- **The way there** (`Path`). If the straight line to the destination clears every path circle, it is the whole way. Otherwise Dijkstra's search runs over the corners, with the start and the destination as two nodes more. It settles the nearest node first, and breaks ties by the order of the nodes, so every build finds the same way.
  - **A ship already in a path circle's slack** judges that circle by its keep-out on the legs from where it stands, so that it can find its way out.
  - **An enclosed destination.** A destination that no way reaches, one enclosed by overlapping circles, gives the way to the corner reached nearest to it.
- **The hard rule** (`KeptOut`). After each tick's flight, a ship whose middle lies within a keep-out goes to the nearest point outside every keep-out, 0.05 units out, by the same search. So no ship ever enters an obstacle's sphere, whatever its steering does. Steering alone keeps out of all but a few ticks in a million (see *Measured*).

**The flight on the plane** (amends ADR-017).
- **Limits.** A ship flies with ADR-017's flight step (`FlyToward`): turning, accelerating, banking and moving. Its limits come from its design's profile (G29): the speed cap, the forward acceleration, used either way, and the turn rate. Its bank limit and bank rate are ADR-017's for its size class: 45° and 60°/s for a frigate, 15° and 10°/s for a capital ship.
- **On the plane.** A ship's aim and its reference up are always level, so its heading and its position stay on the plane at its own height. A ship facing exactly away from its aim turns to its right, where `TurnToward` would turn about a level axis, out of the plane.
- **Steering along a path** (`FlyPath`). A path is the point the ship was ordered from, the corners, then the destination.
  - **The aim.** The ship aims straight at the next corner, turned by its avoidance of other ships (below).
  - **The next corner.** It takes the corner after once it is within 6 units of the one it aims at (`CORNER_REACHED`), or past the line through it that halves its turn. So it cuts a corner by at most 6 units. It swings outward from a corner's turn, away from the obstacle the corner wraps.
  - **Corner speed.** Its speed at a corner is held to the one whose turning circle, at its turn rate, swings it 12 units wide of a corner's turn (`CORNER_SWING`). That is ω × 12 / (1 − cos θ) for a turn of θ.
  - **Its speed** is the least of:
    - its pace (below);
    - the speed from which half its acceleration (`BRAKING_SHARE`) still slows it to each corner's speed;
    - the speed from which it still slows to a halt at its destination.

    It is then scaled by the square of the cosine between its heading and its aim, and it is zero while it faces more than 90° away. So a ship turns before it runs, and a turning ship slows, which tightens its turning circle.
- **Arrival.** A move ends once its path has at most 2 units left (`ARRIVAL_RADIUS`). The ship then brakes to a halt along its heading, and levels out. Halted within a thousandth of a radian of level, its up is the world's exactly. A ship at rest and level is left exactly as it stands, so a ship never ordered stays on the whole grid, as ADR-030 set it.

**A group** (§2.2's subset of G62). The whole ships of one move are a group.
- **Its limits.** A group flies at its slowest member's speed cap, acceleration and turn rate, so that its members turn, speed up and slow down alike. Each member still brakes to a halt with its own acceleration once its move ends.
- **Where each member goes.** Each member's destination is the target plus its offset from the group's middle, the mean of the members' positions, and then `Reachable` of that.
- **Its pace.** A member's pace is the group's cap in proportion to the length of its own path against the longest member's, so that the group arrives together. A group moved across open water keeps its spacing exactly, to rounding. A group whose paths differ, one member going around an obstacle, spreads out while it goes around, and gathers at the destinations.

**Keeping apart** (§9).
- **The avoidance.** A moving ship steers away from every other whole ship, of any side, moving or not, within a reach:
  - the reach is their two spheres and `CLEARANCE_MARGIN`, plus how far they close in a second;
  - the ship steers away from the other, and, when the other lies ahead on its course toward its next corner, also to the side away from it, to the right when the other lies dead ahead;
  - the push grows to 1.5 times its aim's weight as they meet.

  Ahead and the sides are the course's, not the heading's, so that a ship turning where it stands does not flip sides from tick to tick. A first version that took them from the heading deadlocked a ship beside an idle one. Ships that close head-on both turn right. A group's members, flying alike at their spacing, do not push one another.
- **A destination taken.** A moving ship halts once its path has no more left than their spacing and `CLEARANCE_MARGIN`, when a halted ship lies within their spacing of its destination. That is how two moves to one point end: side by side.
- **The avoidance is steering, not a rule.** Ships in a crowd can touch; only the keep-outs are hard.

**Replays** (R21). The flight is single-precision arithmetic in a fixed order, with no clock and no draw. A logged skirmish with orders replays to the same bytes within one build, as ADR-032's does.

**`Create`** builds the ship designs' clearances once, at about 1 ms each. It measures each composite's sphere, and gives each ship its flight. A ship starts idle and at rest, heading the way its design's front faces.

## Tests

- **`NeuronCoreTests`.** The command's golden bytes at layout 4, with and without a payload; every command kind's payload rule, refused as `MalformedMessage`; and a game command carried through encode and decode.
- **`NeuronServerTests`.** The command log's golden bytes at version 2, with a game command's payload, and its refusals. The host hands the world each game command with the side of the session that sent it, and counts the one the world refuses.
- **`GameCoreTests`, `OrdersTests`.** An order and the order states as this ADR spells them, both encoded and decoded, each refusal by name, and an encoder that writes nothing its decoder refuses.
- **`GameLogicTests`, `ClearancesTests`.**
  - A move taken out along its ray, or to where two circles meet.
  - The way around an obstacle is within 2 % of the shortest way around its path circle.
  - On 20 seeds of fields of 30 obstacles, 400 ways keep every leg clear of every path circle.
  - The way to a goal a ring of obstacles encloses leads to a corner outside the ring, and a ship in a path's slack finds its way out.
  - The hard rule takes a point to the nearest point outside every keep-out, also where two keep-outs overlap.
- **`GameLogicTests`, `SkirmishOrdersTests`** (the plan's):
  - **Random orders.** On 20 seeds, three rounds each of random orders to both sides' ships: moves to anywhere in the sector and a little beyond, eight times in ten, and otherwise stops and holds. On every tick, every ship keeps within its profile's speed cap, acceleration and turn rate, allowing a thousandth for rounding, and keeps its sphere out of every core's and asteroid's. Every move ends, and every ship comes to rest within 3 units of its destination, or beside a ship halted there first.
  - **A group's spacing.** Side 1's four ships, moved as one across open water, keep their spacing within a hundredth of a unit, and each comes to rest at its own place moved by the order.
  - **Through the host.** A side's order is taken while the skirmish is paused, and shows in its snapshots at once while its ship stays still. The ship flies once the skirmish resumes. The other side's order, one naming the core, the observer's and a malformed one are refused and counted. Side 2 learns nothing of side 1's orders, and the observer learns them all.
  - Every refusal by name.
  - A stop halts a ship and forgets its move at once; a hold holds; a move replaces a hold, and a hold a move.
  - A logged run of orders replays with no difference.
- **`GameLogicTests`, `SectorTests`.** The sector refuses the game's command as `MalformedCommand`, and none of the engine's.

## Measured

Measured natively, with GCC 13.3 at `-O1` on a 2.1 GHz Xeon, against the repository's sources and GameData, in the tests and in programs of the agent's that are not in the tree:

- **Random orders, the plan's test.** 185 arrivals, the worst 1.97 units from its destination; no destination taken; the longest round 86.5 s; every ship's sphere at least 10.01 units clear of every obstacle's. A group across open water kept its spacing within 0.00095 units over its 16.6 s move.
- **The same over 100 seeds and five rounds each, 666,000 ticks.**
  - Arrivals: 1,644, the worst 1.99 units from its destination. Five ships halted beside a ship on their destination, the farthest 35.9 units from it.
  - Every round ended, the longest in 101.4 s.
  - The hard rule acted on 345 of the 5.3 million ticks of a ship, by at most 1.67 units. Without the avoidance, it acted on 62 of them, by at most 0.70 units. Those are ships braking straight ahead after a stop or a hold beside an obstacle.
  - No tick used more than 1.00034 of a turn rate, 0.99978 of an acceleration, or 1 of a speed cap.
- **The graph.** Over seeds 1 to 20, the clearances hold 209 corners for a miner, 201 for a gunship, 183 for a lancer and 148 for a cruiser, on average. Each is built in 0.8 to 1.5 ms, at most 2.1 ms. `Skirmish::Create` takes 11.1 ms in all.
- **Costs.**
  - A gunship's way across the sector takes 6.3 µs to find on average, and 104 µs at most, over 1,000 ways.
  - An order costs 45 µs on average and at most 476 µs, over 1,000 random orders.
  - A tick with all eight ships moving costs 4.4 µs, against a tick of 33 ms.
- **In an unoptimized build with checked containers** (`-O0` with libstdc++'s debug mode), the random orders test takes 16 s, and the suite's sector tests, which were already there, 55 s.

## What this forecloses

- **Paths planned only when an order is given.** The obstacles never move, so a way stays good. A ship pushed off its way re-aims at its corner and never replans. A world whose obstacles move needs to replan.
- **Obstacles as spheres.** A clearance by voxels (G61) waits for players' structures, after the MVP.
- **A hard rule between ships.** Ships keep apart by steering alone.
- **Turning without spin-up.** Flight turns at the profile's rate from the first tick and ignores its turn acceleration, as ADR-017's does. It slows with its forward acceleration, whatever its reverse thrust.
- **More than 256 ships in one order.**
- **Formations by bearing** (G62). A group keeps its members' offsets and nothing more, until after the MVP.
