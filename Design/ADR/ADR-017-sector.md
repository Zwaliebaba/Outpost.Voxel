# ADR-017 — The sector: layout, routes, flight and destruction

**Status:** accepted, 2026-09-28 · **Lands with:** S-M3 of [`Design/Archive/SpaceScene.md`](../Archive/SpaceScene.md) (§5, §15, §16) · **Amends:** SpaceScene §5, with what §5 left to the world's ADR, and the name of its world · **Amended by:** [ADR-021](ADR-021-sky.md), which keeps the settings' placeholders as the sky's and the lighting's defaults; and [ADR-033](ADR-033-orders-and-flight.md), whose ships fly this flight on the plane with limits from their profiles, and which lifts the foreclosure of a ship that steers across its route for ships that have none

## Context

SpaceScene §5 sets out the world:
- stations laid out from a seed;
- flights of ships on routes that orbit them and cross between them, flown by pure pursuit;
- detonations, whose debris lasts until it is restored or its lifetime runs out.

It leaves the flight's defaults to this ADR, to be tuned by eye in S-M4, and it holds every ship to its keep-out sphere over ten simulated minutes (§5.3, §15).

The owner answered three questions while S-M3 was built, on 2026-09-28:
- **A leader that detonates** hands the lead to its first wingman, and the rest of the flight re-slots on it.
- **A restored ship** resumes where it blew up. It is whole again at its frozen transform, with its flight state as at the event, and pure pursuit picks its route up from there.
- **The name.** The world is a `Sector`. It is one bounded region, and `Universe` stays free for whatever holds several.

The figures below were measured in three ways:
- **`GameLogicTests`,** built natively by GCC 13.3 for x86-64 against a throwaway stand-in for the test framework:
  - at `-O1` without contraction;
  - with `-O2 -mfma -ffp-contract=fast`, which forces the contraction MSVC may do under `/arch:AVX2`.
- **Throwaway probes,** native programs built the same way, over many seeds and parameter blocks.
- **The heavy sector** is 8 stations, 40 capital ships and 200 frigates, the design's heavier choice (§17 question 13).

## Decision

**`GameLogic::Sector`** is `NeuronServer`'s `World` for the space scene ([ADR-015](ADR-015-client-server-boundary.md)). `Sector::Create` takes the parameters and the folder of the models. It returns the sector or a refusal.

**The parameters** (§5.2), with their defaults: the seed (1), stations (4), frigates (40), capital ships (8), the stations' spacing (1,000 units), debris's lifetime (0 s, for none) and the tick rate (30).

**Refusals, by name and with a detail,** before anything runs:
- `BadParameter`: a tick rate of 0, a spacing that is not positive, a lifetime below 0, or a value that is not finite.
- `ModelNotLoaded`: the model's file and why, such as `MilitaryStation.vox: FileNotFound`, or the reader's own refusal.
- `ShipsWithoutStations`: ships with no station for their routes to orbit.
- `SpacingTooSmall`: a spacing that leaves no orbit of 1.3 keep-outs clear of the next station.
- `DoesNotFit`: stations and orbits that would leave the world's bound of 16,384 units (S12), or a region that cannot hold the stations a spacing apart.

A sector of nothing is no refusal.

**The models.** The sector loads `MilitaryStation.vox`, `CapitalShip.vox` and `Frigate.vox`, in that order of the manifest.
- **Hashing.** It hashes each file's bytes with FNV-1a for the welcome (§6.2).
- **Measuring.** It measures each model's sphere: half the diagonal of its occupied box, about the box's center. The spheres are 199.11, 45.81 and 22.15 units, as §4 has them.
- **Keep-outs.** A ship's keep-out radius is the station's sphere, plus its own, plus 50 (§5.2). That makes 294.92 units for a capital ship and 271.26 for a frigate.

**Randomness** (§5.2). Every draw is `PcgHash`(*index* × 16 + *stream* + *seed* × 0x9E3779B9), over fourteen streams:
- the stations' coordinates and turns;
- the flights' sizes;
- the routes' lengths, orders and starts;
- each orbit's tilt, radius and laps, and each flight's direction;
- the tilt axis of a route through one station;
- and the detonations' seeds.

A draw of a unit number takes the top 24 bits. The same parameters give the same sector, tick for tick, within one build (§17).

**Stations** (§5.1, §5.2, S18).
- **Placing.** They are placed by rejection sampling in a disk of radius *spacing* × √*N*, *spacing*/4 above and below the middle, at least the spacing apart, with a thousand tries each.
- **Standing.** Each stands whole, upright, and turned about the vertical by a whole number of quarter turns, from a table of exact rotations.
- **Ids.** Ids run from 1: the stations, then the capital ships, then the frigates flight by flight, in rank order.

**Flights.**
- **Sizes.** Capital ships fly alone. Frigates fly in flights of two to four, drawn in turn: the last flight takes what is left, and a flight of one is left only when there is one frigate.
- **The route's stations.** Each flight's route goes through two to four distinct stations, never more than there are, in a seeded order and back to the first.
- **The start.** Each flight starts at a seeded point of its route, at cruise, banked for the route's curve there, with its wingmen in their slots.

**Orbits** (§5.2).
- **Radius.** An orbit's radius lies between 1.3 and 2 keep-outs. It is capped so that the orbit, and the flight's reach to either side of it, stays 25 units clear of the next station's keep-out. The reach is a frigate slot's width, 53.2 units, and 0 for a capital ship.
- **Laps.** An orbit is flown one or two whole laps before the route leaves it.
- **Direction.** All of a flight's orbits turn the same way about their normals, drawn once per flight. Then every transit is an outer tangent, and one exists between any two orbits, because orbits capped this way cannot nest.
- **The plane.** It holds the direction the route passes the station in: the directions from the station before and toward the next, averaged as lines. It is turned about that direction by a random share of the turn that tilts it 45° from the horizontal. A route through one station tilts its orbit up to 45° about a random horizontal axis.

**Why the plane holds the route's direction.** A transit joins an orbit at a corner as sharp as its angle out of the orbit's plane, and pure pursuit cuts every corner.

| Orbits | Worst corner, default parameters | Worst corner, heavy sector |
|---|---|---|
| Each turning its own way, tilted about a random horizontal axis | 98°, over 5 seeds | 150°, over 2 seeds |
| One way per flight, random tilt axis | 71°, over 30 seeds | 83°, over 12 seeds |
| One way per flight, plane holding the route's direction | 63°, over 30 seeds | 64°, over 12 seeds |

The default sector's worst corner is 54.9°.

**Routes** (§5.2). A route is a closed chain of arcs and straight lines, and it is evaluated exactly.
- **Transits.** Each transit runs from one orbit's exit to the next orbit's entry. Eight rounds of refinement make it nearly tangent to both.
- **Waypoints.** A transit that would pass within a station's keep-out, plus the reach, plus 25 units, bends at a waypoint pushed out to 1.25 times that radius. A transit bends at most four times.
- **Why exact.** A polyline sampled every 4 units made pure pursuit's turn alternate from tick to tick, 0.29° and then 0.31°. That is the beat between the samples and a frigate's 2-unit step. The bank amplified it into a roll that rocked a degree either way, and the slots swung with it. Exact arcs removed it.
- **Tracking.** A ship is tracked along its route within 200 units behind and ahead of where it was. That is past a frigate's last slot, 106 units behind its leader, and far short of the shortest lap, 2,215 units. So a ship never jumps to the next lap of an orbit.

On the default sector's seed:
- 22 flights, on routes of 14,565 to 40,117 units, of at most 9 pieces.
- Every piece ends within 0.0008 units of where the next begins.
- Every route, with its flight's reach, stays at least 27.72 units clear of every keep-out.

**Flight** (§5.3). One step a tick, of `1 / tick rate` seconds.
- **Aim.** The ship aims by pure pursuit at the point of its route 1.5 s of travel ahead, and never less than at half its cruise.
- **Turn.** Its heading turns toward that point by at most its turn rate. It takes the angle from the sine and the cosine together. The cosine alone loses precision at the 0.27° a capital ship turns in a tick, and one turned 0.14% past its rate.
- **Speed.** Its speed moves toward its target by at most its acceleration.
- **Attitude.** A single up vector rolls about the heading, by at most the bank rate, toward its target. The target is the route's reference up, which is the orbit's normal on an arc and the world's up on a line, banked by atan2(lateral acceleration, 10 units/s²) within the bank limit.
  - One roll covers both banking and levelling. Each with a rate of its own, the two together turned a frigate's up at 120°/s on entering an orbit, and swept its wingmen's slots faster than they could fly.
- **Move.** The ship moves along its new heading at its new speed.

| Class | Cruise | Wingman's range | Acceleration | Turn rate | Bank limit | Bank rate |
|---|---|---|---|---|---|---|
| Frigate | 60 units/s | 30 to 75 units/s | 30 units/s² | 45°/s | 45° | 60°/s |
| Capital ship | 20 units/s | 10 to 25 units/s | 5 units/s² | 8°/s | 15° | 10°/s |

These are §5.3's defaults, with a wingman's range of half its cruise to a quarter over it, and bank rates of this ADR's own. S-M4 was to tune them by eye and amend this table. The owner flew among the ships on 2026-09-28 and asked for no change, so they stand.

**Formation.**
- **Slots.** A flight's slots lie in its leader's frame, banked, in widths *D* of its ship, twice its sphere:
  - (+1.2 *D*, 0, −1.2 *D*), back to the right;
  - (−1.2 *D*, 0, −1.2 *D*), back to the left;
  - (0, 0, −2.4 *D*), behind.
- **Who takes which.** The whole ships other than the leader take the slots in rank order.
- **The wingman's aim.** A wingman flies the route as its leader does. It aims 1.5 s ahead of its own progress along the route, moved to its slot's side of the route in its leader's frame, turned to the route's heading there.
- **The wingman's speed.** It flies at its slot's own speed along the leader's heading, plus 0.5/s for every unit the slot lies ahead of it along the route, within its range. The slot's own speed is the one an outer slot needs to be faster through a turn and an inner one slower. In the first measurement, without it, most wingmen on an orbit sat 10 to 25 units from their slots.

**Why a wingman steers along its route.** A wingman that steered straight at its slot, or at a point ahead of it, cut across an orbit's chord whenever it was far from the slot, and entered a keep-out. That happens to a restored ship, or to a flight re-forming on a new leader.
- **Before.** Over 40 seeds of the default parameters with detonations and restores, a wingman went up to 7.1 units into a keep-out. Aimed ahead along its leader's heading instead of its slot's motion, it still went 3.3 units in over 60 seeds.
- **After.** Steering along the route, the least clearance over the same 60 seeds is 14.6 units.
- **The cost.** A steady offset from the slot of about 6.5 units while its leader banks on an orbit, where the slot on the route's side and the slot in the leader's frame part. It vanishes in straight flight.

**Leadership** (the owner's answer).
- **Succession.** A flight keeps its leader while the leader is whole. When it is not, the first whole ship by rank leads, and the others take the slots in rank order.
- **A restored ship** rejoins as a wingman, and catches up with its slot along the route. It leads only a flight with no other ship whole.
- **No whole ship.** A flight with none has no leader, and nothing of it flies until a ship is restored.

The owner's answer covered a leader that detonates, and this ADR extended it to a leader restored: it rejoins as a wingman, so that a restore never makes a whole flight turn back for a leader that reappears behind it. The owner confirmed the extension on 2026-09-28, while S-M4 was built ([ADR-018](ADR-018-client.md)).

**Detonation and restore** (§5.5, the owner's answer).
- **Detonation.** A detonation freezes the entity where it is, and records the event:
  - the entity;
  - a seed, the detonation stream's draw at a count of the sector's detonations, so that each detonation has one of its own;
  - the world tick;
  - its velocity, which is its heading times its speed for a ship and zero for a station.

  A detonated entity is described at its frozen transform, with zero velocity, together with its event. A second detonation of it, or a detonation of an entity the sector does not hold, is ignored, since a command can race a removal (ADR-015).
- **Restore.** A restore makes the entity whole at that transform, with its heading, up, speed and progress as at the event. A leader picks its route up by pure pursuit from there, and a wingman rejoins along the route.

**Debris's lifetime** (§5.5, S19).
- **None, the default.** The debris lasts until it is restored.
- **Otherwise** the lifetime is a whole number of ticks, at least one: the seconds times the tick rate, rounded. The entity and its event leave the snapshot on the tick the world tick reaches the event's plus the lifetime. They leave for good: a restore does nothing, and the id is never used again.

**The settings** (§6.2), placeholders until S-M5 tunes the sky and the lighting:
- the sun from `SunDirection` at 50° and 50°, with a radiance of 0.7 and an angular radius of 0.27°;
- an ambient of 0.05 above and below;
- the sky's seed is the sector's;
- the galactic plane is turned 60° about *x*.

**Tests** (`GameLogicTests`, [ADR-016](ADR-016-server-suites.md)).
- **`RouteTests`,** 5 tests:
  - every route closes, and stays clear of every keep-out with its flight's reach, on three seeds;
  - the route's direction is that of its chords;
  - a ship is tracked along it without jumping a lap;
  - the separation of two points is taken the shorter way round.
- **`SectorTests`,** 11 tests:
  - the default sector;
  - the stations' layout, on sixteen parameter blocks;
  - two runs of one seed send the same bytes over two minutes of detonations, and another seed differs;
  - ten minutes within the limits;
  - ten minutes of detonations and restores within them;
  - the rules of succession;
  - a ship restored where it blew up;
  - a detonation reaching every client and a late one;
  - debris's lifetime, and debris without one;
  - the refusals.
- **The ten-minute bounds:**
  - no ship enters a keep-out;
  - no tick uses more than its class's speed, acceleration or turn rate, nor turns its attitude faster than its turn and bank rates together, allowing a thousandth for the snapshot's rounding;
  - no wingman strays more than 2 widths from its slot, nor more than a quarter of a width on average.

**Measured.**
- **The default sector's ten minutes**, natively at `-O1`:
  - at least 27.72 units clear of every keep-out;
  - the most of any limit a tick used, the snapshot's rounding included:
    - speed, 1: wingmen reach their top speed;
    - acceleration, 1.000015;
    - turn rate, 1.00002;
    - attitude, 0.717 of the turn and bank rates together;
  - wingmen at most 0.998 widths from their slots, and 0.124 on average.
- **With detonations and restores,** at least 31.18 units clear.
- **Under forced contraction,** the same figures to three places: 27.72 units, 0.998 and 0.124 widths, and 31.18 units.
- **Over more seeds,** in the probes:
  - 30 seeds undisturbed: at least 27.7 units clear.
  - The worst slot transient over those seeds is 1.81 widths, in a flight's first two seconds. That is why the bound is 2 widths.
  - 60 seeds with detonations: at least 14.6 units clear.
  - The heavy sector, on 4 seeds: at least 28.1 units clear undisturbed and 15.0 with detonations.
- **In CI,** MSVC's Debug|x64 build under `/arch:AVX2`, on 2026-09-28, the same figures to four places:
  - 27.7175 units clear;
  - slots at most 0.9978 widths away, and 0.1238 on average;
  - 31.1820 units clear through the detonations.
- **The suite's time.**
  - 1.9 s at `-O1`;
  - 28.6 s in an unoptimized build with checked containers, which stood in for CI's Debug build;
  - 36 s in CI's Debug build itself, of which the two ten-minute runs take 7 s and 6 s.
- **A tick's cost,** natively at `-O1`:
  - 23 µs for the default sector's 48 ships, and 119 µs for the heavy sector's 240;
  - `Create`, which reads and measures the three models, 6.5 ms.

## What this forecloses

- **A ship that steers across its route.** Every ship aims along its route, at most its slot's reach to the side, so the route's clearance is the flight's.
- **Routes sampled into polylines.**
- **A flight whose orbits turn opposite ways.**
- **Leadership passing back to a restored ship** while another ship of its flight is whole.
- **A station that is not whole, upright and turned by quarter turns about the vertical.**
- **Debris that returns** after its lifetime has run out.
