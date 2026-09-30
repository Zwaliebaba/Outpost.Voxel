# ADR-035 — Combat: weapons, aim, damage and losses

**Status:** accepted, 2026-09-30 · **Lands with:** phase 5 of [`Design/MvpPlan.md`](../MvpPlan.md), its tasks 1 to 5 · **Amends:** [ADR-014](ADR-014-placements.md)'s placements, which carry a mask the splat honors, their constants growing from 64 bytes to 68 and the splat's root signature by the frame's mask words; [ADR-018](ADR-018-client.md)'s snapshot buffer, which carries each entity's mask and the game's payload to the render time; [ADR-025](ADR-025-detonation-light.md)'s blast, which a detonation that has lost voxels sizes by what it has left; [ADR-029](ADR-029-protocol-composites-and-sides.md)'s snapshot, which carries each entity's mask, at layout version 5; [ADR-030](ADR-030-skirmish.md)'s skirmish, whose ships and turrets fight, whose debris expires, and whose parameters gain the battle; [ADR-032](ADR-032-sides-sessions-and-fog.md)'s command log, whose skirmish parameters are version 2, and its replay, which times the host's steps; [ADR-033](ADR-033-orders-and-flight.md)'s orders and order states, version 2, with the attack and the attack-move, and its flight, which bears, closes and jinks; [ADR-034](ADR-034-interface-layer.md)'s commander and HUD; [`AGENTS.md`](../../AGENTS.md) §2, whose `NeuronCore`, `NeuronClient`, `GameCore`, `GameLogic` and `GameLib` rows gain combat, and the rows of `GameCoreTests` and `GameLogicTests` its tests; and [`MvpPlan.md`](../MvpPlan.md) §8.3, which gains phase 5's values

## Context

Phase 5 lets ships and the cores' turrets fight by the concept's rules (§8; G6, G14, G15, G33, G39, G47, G51, G54, G57, G58, G59, G60, G71, G72). Its first task asks for an ADR of combat:
- **weapons:** the mass driver's shells lead (G58), and the laser's beam hits at once;
- **aim:** a weapon aims at a voxel drawn from its target's silhouette, from the profile (G57);
- **targets and fire:** a ship keeps its target, and holds fire on a line its own side's hulls block (G71);
- **jinking:** a ship under fire jinks within its acceleration (G58);
- **resolution:** a tick's shots resolve at once, against the grids as the tick began (G72);
- **damage:** per voxel, up to its class's toughness, and a module fails below a share of its voxels;
- **loss:** the command module's failure loses a ship and a reactor's detonates it; in the MVP both become debris, as does any piece cut off from the command module (§2.2);
- **the attack order** closes to range, measured to the nearest voxel (G47);
- **events:** shots and damage are events per side, and a shell reaches a side as its position and velocity where it enters that side's sensors (G54);
- **masks:** baselines carry the mask, and the splat skips a voxel its mask removes, which amends ADR-014, with the mask's twin and its layout (R15, R16).

The plan's §8.3 gives the weapons' ranges, the shell's speed and the sensors', and G59's time to kill. It leaves damage, reach and toughness to this phase, which the smoke check of task 5 tunes.

## Decision

**The weapons** (`GameCore::WeaponSpec`, in the catalogue). A weapon is a module at a weapon mount, a ship's or a structure's alike; the cores' four mass drivers are their turrets.

| Weapon | Shot | Range | Damage | Reach | Rate | Shell speed |
|---|---|---|---|---|---|---|
| `MassDriver` | a shell | 600 units | 200 a shell | 1.5 voxels | a shell a second | 300 units/s |
| `Laser` | a beam | 250 units | 400 a second, spent each tick | 1 voxel | every tick | — |

- **The muzzle** is the middle of the mount box's front face: the mount's center cell's center, moved along its facing by half the box's depth and half a voxel.
- **The arc** is the half space the mount faces: a weapon bears on a target whose middle lies ahead of its muzzle, and fires a shot whose direction does.
- **A shell** is fired from the muzzle at the shell speed along its lead, and flies straight at that velocity, the shooter's own velocity not added, for its range's worth of time: 2 s, 60 ticks at 30 a second.
- **A beam** is a line from the muzzle through its aim point, out to its range, resolved at once on each tick it fires. Its damage a tick is its damage a second over the tick rate.

**The combat profile** (`GameCore::CombatProfile`), computed once for each design from its composite:
- **its voxels**, in the order of an entity's mask (`NeuronCore::CompositeVoxels`): component after component, part after part, record after record, each with its whole cell in the composite's space;
- **its components**, the hull and then the module at each mount, each with its first voxel and its count, and each module with its kind;
- **each voxel's class**: a hull voxel's is its palette entry's, light or heavy (ADR-027), and a module's voxels are light;
- **its weapons**, each with its spec, its component, its muzzle and its facing;
- **its bearing** (below);
- **its silhouettes** (G57): for each of 16 level directions, 22.5° apart, the voxel nearest a shooter looking along it in each column a voxel wide and a voxel high square to it, a tie going to the lower index. `SilhouetteDirection` takes a direction to the nearest of the 16 by its angle about the vertical. A shooter above or below its target aims as from the level direction.

**Toughness** (§8.3 of the concept). A voxel takes damage up to its class's toughness: 10 for a light voxel, 30 for a heavy one (the plan's §8.1 makes heavy armor three times light). A voxel goes once the damage it has taken reaches its toughness.

**The tick** (G72). `Skirmish::Advance` runs, in this order:
1. **Debris expires** 60 s of the world's time after its detonation (`DEBRIS_SECONDS`, the concept's "about a minute"): it leaves the world, and no snapshot holds it again.
2. **Every intact entity is taken as the tick begins** (`SweptEntity`): where it stands and how it is turned, its velocity, its mask, each voxel's damage and toughness. Debris is left out, so a shot passes it.
3. **Targets**, from what each side sees as the tick begins (below).
4. **Fire.** Each working weapon with a target and, for a mass driver, its interval spent, aims and fires, or holds fire.
5. **Every shot is swept** against the entities as the tick began: the beams, then the shells in the order they were fired. What each spends is kept apart.
6. **The damage is summed and applied.** A voxel goes when a shot spent the last of it, whatever the rounding of the sum, or when its summed damage reaches its toughness.
7. **Each entity that lost a voxel suffers once** (below): a loss, pieces cut off, modules failed.
8. **Flight** (ADR-033, and below).

No side's shots resolve before another's, so a mutual kill is mutual, and a duel with its sides exchanged gives the same result exchanged.

**A shot's sweep** (G39, `GameLogic::SweepShot`).
- **What it meets.** The voxels of every swept entity it enters, in the order it enters them, each entity's by the shot's motion relative to it over the same span: a shell's displacement in the tick less the entity's velocity times the tick, and a beam's line as it is. The cells come from `NeuronCore::SegmentCells`, `GridWalk` with a slab test, which counts a cell once the segment has spent more than a thousandth of a voxel inside it (`SEGMENT_INSIDE_CELLS`), so that a muzzle a rounding inside its own barrel's tip does not block it. Ties of entry go to the lower entity and then the lower voxel.
- **What it passes and where it stops.** It passes its shooter and debris. It stops without damage at its own side's voxels and at an asteroid's.
- **What it spends.** It spends its damage on an enemy's voxels one by one along its line, each taking what it has left, and carries the rest on. At the first voxel it cannot take, it stops, and what it has left spreads evenly over that voxel and the others whose centers lie within its reach of it: 19 of them for the mass driver's 1.5, 7 for the laser's 1. Each takes up to what it has left, and the rest of its share is lost. So a spaced skin stops no more than its voxels are worth.
- **A shell** that stops ends there. One that meets nothing in its tick flies on, and ends when its time is spent. **A beam** stops where its sweep stops, and is drawn to there.

**Aim** (G57). A weapon aims at a voxel of its target's silhouette for the direction from its muzzle to the target's middle, turned into the target's frame. The voxel is drawn with `PcgHash` of the seed, `AIM_STREAM`, the shooter's id, the weapon and the count of its shots (R21). The mass driver draws a voxel for every shell, so it erodes. The laser keeps its voxel while its beam damages its target, and draws anew for a new target or after a tick its beam spent nothing on the target, so it drills.

**The lead** (G58, `LeadShot`). A shell is fired at where its aim voxel would be if its target held its velocity: the least positive time at which a shell from the muzzle can meet it, from the quadratic. A weapon holds fire when there is none, or when the lead falls outside its arc.

**Targets** (G71). A **fightable** entity is a design's, whole, no piece of one, of a side. Each entity with a weapon, a ship or a turret alike, keeps a target of its own and one for each weapon:
- **The entity's target.** An attack's target, while the attacker's side sees it; the attack ends when it cannot. Otherwise the target it has, while it is fightable, of another side, seen by its side, and in a working weapon's reach. Otherwise the nearest such entity in reach, one that a working weapon's line from its muzzle to the entity's middle leaves clear of its own side's hulls first, and a tie by `PcgHash` of the seed, `TARGET_STREAM`, its id and the candidate's.
- **Each weapon's target.** The entity's target when the weapon bears on it: working, in its arc and in its range. Otherwise the weapon's own target while it bears, and otherwise the nearest target that bears, a tie drawn as above with the weapon.
- **Under fire.** An entity some enemy weapon targets is under fire, which is what makes a ship jink.
- **An order** clears the targets of its ships, which find theirs afresh.

**Range** (G47) is measured from a weapon's muzzle to the center of its target's nearest voxel that remains. Every center lies within the composite's sphere, so the sphere settles most cases, and `VoxelBody::NearestVoxel` the rest, over bricks of 8 voxels a side.

**Holding fire** (G71). A weapon holds fire when the line from its muzzle to where its shot goes, a shell's intercept point or a beam's aim point, meets a voxel of its own side's, its own hull's included.

**Jinking** (G58). A ship under fire slides across its heading, keeping its weapons where they bear:
- **Its side speed** is drawn every second (`JINK_SECONDS`) from `PcgHash` of the seed, `JINK_STREAM`, its id and the second, evenly from −20 to 20 units/s (`JINK_SPEED`).
- **It gets there within its side thrust**: its profile's acceleration times `VECTORED_THRUST_SHARE`, a quarter, times its share of working engines. So its agility decides how far it strays from a shell's lead: a gunship's 25.1 units/s² gives 6.3 units/s² across, and a cruiser's 5.7 gives 1.4.
- **While it bears on a target**, it slides back toward where it began to bear once it is 30 units off it (`JINK_LEASH`).
- `SkirmishParameters::jinking` turns it off, for a measure alone.

**Bearing** (§2.2's subset of G33 and G62). A design's bearing is the angle off its bow at which the most of its weapons bear: the middle of the widest run of whole degrees in which they do, the nearer the bow on a tie, and to starboard on a tie again. The gunship and the lancer bear dead ahead; the cruiser, whose fore mass driver and starboard laser both bear there, bears 45° to starboard. A ship bearing on a target halts and turns to hold it at its bearing, and jinks about where it began.

**The attack order** (G47). `Attack` names an entity to fight:
- **It closes** along the clearances (ADR-033) toward the target's nearest reachable point, its way found anew every 0.5 s (`REPATH_SECONDS`) as the target moves, until every working weapon's muzzle lies within 0.9 of its range of the target's nearest voxel (`ATTACK_RANGE_SHARE`). The rest is a margin, so that a target drifting off does not take it out of range at once. A ship with no working weapon closes no further.
- **In range, it bears.**
- **It ends**, and the ship idles, once its target is lost, becomes a piece, or leaves its side's sight.
- **`AttackMove`** moves as a move does, fights what comes into reach on its way as an attack does, and takes its way anew once that is done.
- **An idle or holding ship** bears on its target where it stands, turning without moving, and jinks under fire.
- **Refusals.** An attack naming an entity that is not fightable, of its own side, or not held is `NotOrderable`.

**Modules** (G6, G14). A module fails once less than half its voxels remain (`FAIL_SHARE`):
- a failed weapon holds its fire;
- failed engines take their thrust with them: a ship's acceleration and turn rate are scaled by its share of working engines;
- **a sensor keeps sensing while its ship lives.** A ship blinded with nothing to say so would read as a fault, and the status markers that would say so come after the MVP (the plan's §2.2 on G49). Before this rule, a 13-voxel sensor shot off first stalled duels between ships that could no longer see each other.

**Losses** (G51, the plan's §2.2).
- **A failed command module or reactor loses its entity at once.** The loss is recorded, by the reactor when both fail, and the entity detonates with its velocity, its remaining voxels its debris. In the MVP both kinds become debris; the concept's wreck, which a command loss leaves for salvage, comes after the MVP (the plan's §2.2). A structure is lost the same way.
- **What is cut off.** After the damage, each entity that lost voxels and survives finds what still connects, face to face, to its command module. A flood fill from the command module runs only when `CutsNothing` cannot show that the voxels beside the removed ones still connect to one another within a search of 512 voxels. What is cut off leaves the entity. A piece of at least 16 voxels (`PIECE_MIN_VOXELS`) becomes debris of its own at once: an entity of the same composite, side and place, masked to the piece, detonated with its parent's velocity. A smaller chip goes as the voxels a shot removes do, so that erosion does not strew the sector with entities of a voxel or two.
- **A detonation damages nothing near it** (G51).
- **`Restore`** makes an entity whole again, its mask, damage, modules and thrust; a piece, or debris that has expired, has no whole to return to.

**What a side receives** (G54). Each snapshot is the side's own, as ADR-032's are.
- **Masks.** Every entity the snapshot holds that has lost a voxel carries its mask, whole, in every snapshot, so that an entity coming into sight has its baseline at once and one in sight never drifts. Damage is not a message of its own: the mask is its record.
- **Shells.** A shell is in the side's payload while it is the side's own, or while an intact sensor of the side covers it, as its position and velocity at the snapshot's tick. So a shell flying into a side's sensors first reaches it at their edge, and never says where it was fired.
- **Beams.** A beam is in its own side's payload whole, and in another's as the parts of it within the side's sensors' spheres, merged where they overlap. The observer receives every shell and beam whole.

**The protocol** (amends ADR-029). The snapshot's header gains a `u32` count of masks and a `u32` of their bytes, between the detonations' count and the payload's bytes, and the masks follow the detonations. A mask is its entity's `u32` id, its `u32` count of voxels, then that count's bits in bytes, bit *i* in bit *i* mod 8 of byte *i* / 8, set for a voxel gone. The reader refuses as `MalformedMessage` masks that do not fill their bytes exactly, a count of 0, a spare bit set, a mask with no bit set, and two masks of one entity; and as `UnknownEntity` a mask of an entity the snapshot does not hold. That is layout version 5; the protocol stays 2. The client refuses as `BadMessage` a mask whose count is not its entity's composite's voxels, as its own models count them.

**The game's payloads** (amends ADR-033), little-endian:
- **The order**, version 2: kinds 1 move, 2 stop, 3 hold, 4 attack, 5 attack-move. An attack-move carries its point on the plane as a move does. An attack carries the `u32` id of the entity attacked after the ships, never 0 and none of them.
- **The snapshot's payload**, version 2: a `u16` count of order states, each 20 bytes: the ship's `u32` id, its `u8` state (1 moving, 2 holding, 3 attacking, 4 attack-moving), three reserved bytes of 0, an attack's `u32` target, and a move's or an attack-move's destination as `f32` x and z. Then a `u16` count of shells, each its position and velocity as three `f32` each, its `u8` side and three reserved bytes; then a `u16` count of beams, each its two ends as three `f32` each, its `u8` side and three reserved bytes. `DecodeSnapshotPayload` refuses as `DecodeOrder` does: `Truncated`, then `UnsupportedVersion`, then `MalformedOrder` for a state it does not know, a side of 0, a value that is not finite, a reserved byte that is not 0, or bytes to spare.
- **The skirmish's parameters** in its command log (amends ADR-032), version 2: the `u8` version, the `u32` seed, the `u32` tick rate, and a `u8` of flags: 1 for the battle and 2 for no jinking. A flag it does not know is refused.

**The battle** (`--skirmish --battle`, `GameCore::MakeBattleLayout`). Each side's whole command budget of 20 points, two cruisers, six gunships and six lancers, stands in place of its starting ships in a block about 450 units from the sector's center, facing the other side. The nearest two stand 780 units apart, out of each other's sensors, so that the fight starts when an order starts it. It is the heaviest fight the phase can stage, which its done-when measures. **To measure its tick,** `--replay` reports how long the host's steps took, on average and at worst, besides whether they matched: a battle played with `--log` gives the server's tick in the build that replays it.

**The client.**
- **Masks per placement** (amends ADR-014, ADR-018). The snapshot buffer gives each sampled entity the later snapshot's mask, as it gives what is present. `SceneModels` slices it for each part (`NeuronCore::PlacementMask`), and places no part that has lost every voxel. A placement's mask is a word for each 32 of its records, bit *i* mod 32 of word *i* / 32 set for a voxel gone, or nothing while it draws every voxel. The renderer refuses a mask of any other length.
- **The splat** (R15, R16). `PlacementConstants` gains `firstMaskWord`, where the placement's words start in the frame's buffer of mask words, or `NO_MASK`, 0xFFFFFFFF; it grows from 64 bytes to 68, and its mirror with it. `PushSplatPlacements` pushes the frame's mask words, never fewer than one so that there is always a buffer to bind, and the splat's root signature binds them as a root SRV at `t4`. Every permutation of the splat, view and shadow, aligned and oriented, leaves out a voxel its mask removes, through `IsVoxelGone` in `Shader/Placement.hlsli`, whose twin is `NeuronCore::IsVoxelGone`. The scene tracer, the reference, leaves out the same voxels.
- **A detonation that has lost voxels** (amends ADR-025) blasts from the mean of the voxels it has left, its light and heat sized by the box around them, as a composite of those voxels alone would be. So a piece cut off a ship flashes as a piece, not as the whole ship.
- **The game's payload at the render time.** A sample carries the later snapshot's payload, and how far before that snapshot the render time lies on the world's clock, which stands still while the world is paused. The order states, the shells and the beams are read from it, so that they agree with the entities drawn; a shell is drawn where its velocity has taken it by the render time.
- **The fight in the world's overlay** (amends ADR-034), under the commander's marks:
  - **ownership at a distance** (the owner, at phase 4's checkpoint): a disc 6 pixels wide at 96 DPI in its side's color just below every entity of a side, as wide at every zoom, and a dimmer one below each structure remembered out of sight;
  - **the condition readout's bars** (G59) over each entity that is selected or damaged: its weakest vital module, the command module or any reactor, against the share at which it fails, green above a half, amber to a quarter and red below; and its hull, the share of its hull's voxels that remain;
  - **shots:** each shell as a streak of its last 0.04 s of flight, and each beam as it fired, in its side's color taken halfway to white.
- **The commander** (amends ADR-034):
  - a right-click at an enemy, a whole entity of another side, attacks it with the selected ships of the side's, and elsewhere moves them;
  - A, the order bar's Attack, arms an attack: the next click attacks the enemy under it, or attack-moves to the point of the plane under it;
  - a click while a move is armed moves, over an enemy or not;
  - a box that holds any of the side's ships selects only those, and otherwise every entity whose middle it holds (the owner, at phase 4's checkpoint; the concept's §9);
  - an attack-move's destination is marked as a move's is, in the attack's color, and a selected ship attacking has a line to its target.

## Tests

- **`NeuronServerTests`.** A replay times each of its steps.
- **`NeuronCoreTests`.** The snapshot's golden bytes at layout 5 with masks, and every refusal of a mask by name. `CompositeVoxels` lists the voxels in the order of a mask; `PlacementMask` slices an entity's mask for each placement; `SegmentCells` walks a segment through the cells it enters, in order. The scene tracer leaves out the voxels a mask removes, against brute force on random scenes.
- **`NeuronClientTests`.**
  - **On WARP,** placements with masks, one left whole, draw what the scene tracer sees, in the view and in the sun's map; the same through either permutation, and detonated at time 0 as whole. The whole scene shows voxels the masks remove, and the masked scene none of them. The layout echo reads `firstMaskWord` where the struct puts it.
  - **On the CPU,** the snapshot buffer takes each entity's mask from the later snapshot and carries its payload; the session refuses a mask of another count; `SceneModels` slices the mask for each part and places no part that has lost every voxel; and a detonation that has lost voxels blasts from what it has left.
- **`GameCoreTests`.** The order and the snapshot's payload at version 2, golden bytes and refusals by name. Each design's combat profile: its composite's voxels by component, its weapons armed, its bearing, its silhouettes by the area it shows, the nearest direction, and a condition read from a mask. The battle's layout spends each side's whole budget.
- **`GameLogicTests`, `CombatTests`** (the plan's tests among them):
  - a shot spends its damage along its line, carries the rest, spreads what it has left over its reach where it stops, passes its shooter, and stops at its own side's voxels and an asteroid's without damage;
  - a shell is swept relative to what it meets; a lead meets a point moving straight; a removal that cuts nothing is known as one, and a rod cut in two is not;
  - **a seeded duel runs the same twice**, to the byte, tick by tick;
  - **a duel with its sides exchanged gives the exchanged result**, on three seeds: the same entity lost at the same tick the same way, and the same masks;
  - **a ship behind its own side's hull holds fire** and damages nothing, and fires once the hull is gone from the line;
  - **a shell leads a target flying straight, and hits it**;
  - **over 40 seeds, jinking lowers the mass driver's hits on a gunship** by at least a fifth, **and not on a cruiser**, by less than a twentieth;
  - **what a side receives of an entity's voxels is the server's**, bit for bit, through the host, while the enemy stays in sight, leaves it, is restored whole out of it and comes back;
  - **a shell reaches a side only from its sensors' edge**;
  - pieces cut off become debris of their own, a loss detonates its entity with its mask, and debris leaves after its time;
  - attacks on what combat cannot fight are refused, and a side learns its ships' attacks and attack-moves.
- **`GameLogicTests`, `SmokeCheckTests`.** The plan's smoke check, with its table: one seed in CI, to keep the harness working, and the plan's 20 with `OUTPOST_SMOKE_SEEDS=20`, which hold the 90 % bar.
- **`GameLogicTests`, `SkirmishTests`.** The parameters' bytes at version 2, each flag round trip, and their refusals.

## Measured

Natively, with GCC 13.3, on an Intel Xeon at 2.80 GHz with 4 virtual CPUs, against the repository's sources and GameData: the tests at `-O1`, and at `-O2` programs of the agent's that are not in the tree. The done-when's frame and tick are the owner's to measure in Release on the reference machine.

- **The designs** (by `ComputeCombatProfile`). Voxels: miner 1,451, gunship 1,372, lancer 1,336, cruiser 9,546, core 29,844. A command module holds 27 voxels, a reactor 45 and a large one 225, a thruster 45, a mass driver and a laser 21 each, a sensor 13, the sensor array 43. Silhouettes, along the bow axis and across it: gunship 78 and 164 voxels, lancer 60 and 215, cruiser 218 and 638, miner 88 and 182, core 894 and 828. A cruiser's mask is 1,194 bytes, by arithmetic from its voxels.
- **The smoke check**, the plan's task 5, over 20 seeds, both starts and three ranges, 240 s at most a duel. The cruiser is priced at 7.6 gunships or 7.9 lancers, and meets eight of either.

  | Duel | From | First wins | Second wins | Neither | Mean length | Losses: command, reactor |
  |---|---|---|---|---|---|---|
  | 1 × Gunship against 1 × Lancer | 200 | 14 | 25 | 1 | 37.2 s | 34, 5 |
  | 1 × Gunship against 1 × Lancer | 400 | 25 | 14 | 1 | 44.8 s | 29, 10 |
  | 1 × Gunship against 1 × Lancer | 600 | 22 | 18 | 0 | 39.7 s | 28, 12 |
  | 1 × Cruiser against 8 × Gunship | 200 | 0 | 40 | 0 | 36.6 s | 24, 21 |
  | 1 × Cruiser against 8 × Gunship | 400 | 0 | 39 | 1 | 62.9 s | 24, 16 |
  | 1 × Cruiser against 8 × Gunship | 600 | 0 | 40 | 0 | 55.8 s | 25, 15 |
  | 1 × Cruiser against 8 × Lancer | 200 | 0 | 40 | 0 | 20.1 s | 35, 19 |
  | 1 × Cruiser against 8 × Lancer | 400 | 0 | 40 | 0 | 22.2 s | 36, 19 |
  | 1 × Cruiser against 8 × Lancer | 600 | 0 | 40 | 0 | 24.1 s | 38, 20 |

  The gunship wins 180 of its 240 duels, 75 %; the lancer 177, 74 %; the cruiser none. The bar, no design winning more than 90 % against all the others, holds. Between the frigates, the gunship wins 61 and the lancer 57, with 2 draws, and a duel lasts 37 to 45 s on average: above G59's 12 s, and a little above the plan's 20 to 40 s at 400 units. **The cruiser loses every duel against eight frigates**, in 20 to 63 s on average, below G59's 67.5 s. It fights eight command modules with one, each the same 27 voxels, and eight hulls' fire concentrates on it while its own is spread.
- **The cruiser, further.** Against seven frigates, the equal cost rounded down, it lost all 120 of its duels over 10 seeds. Seven changes, one at a time, each over 5 seeds, both starts, the three ranges and both pairings, 60 duels each, gave it no win in 420: modules heavy, heavy armor doubled to 60, the laser's damage down to 250 a second, the mass driver's down to 120 a shell, modules failing only below a quarter of their voxels, the mass driver's reach widened to 2.5 and the laser's to 2. They moved how long it lasted: heavy armor doubled kept it 84 to 163 s on average against gunships, and every change left it 17 to 40 s against lancers. So the balance between a capital ship and frigates at equal cost is not a number this phase tunes, and it is the phase's decision point (the plan's phase 5 and §3.6).
- **The tuning** that chose damage 200 and reach 1.5 for the mass driver, and 400 a second and reach 1 for the laser, ran the same duels over 10 to 20 seeds for each candidate. Before the rule that a shot spends its damage along its line, a rule that cratered where a shot first landed shot weapons off before hulls, and duels stalled.
- **Jinking**, in `JinksShellsAsideWithinItsAgility` over 40 seeds: a gunship is hit by 0.362 of the shells aimed at it jinking and 0.823 holding still; a cruiser by 0.883 and 0.906.
- **The battle**, both fleets of `--battle` attack-moving into each other from the start, 150 s on each of seeds 1 to 5:
  - **the host's tick**, the skirmish's tick and the snapshots of three sessions, both sides and the observer, encoded: 0.25 to 0.29 ms on average, 0.78 to 1.03 ms at the 99th percentile, and at worst 1.0 to 6.3 ms, a single tick on a shared machine;
  - **the skirmish's tick alone:** 0.18 to 0.22 ms on average, 0.70 to 0.93 ms at the 99th percentile;
  - **a side's snapshot:** 22 to 37 KB at worst, with 39 to 51 masks and 61 to 64 shells;
  - **losses:** 21 to 23 a battle, 8 to 16 of them by the command module and 6 to 13 by a reactor.
- **Pieces.** Before `PIECE_MIN_VOXELS`, a minute of the battle left 220 pieces in the world, the median of 2 voxels, and a snapshot of 259 KB with 300 masks. With it, 8 to 10 pieces remained after 150 s, their median 20 to 32 voxels.
- **Connectivity.** Before `CutsNothing` and the face-neighbor table, the flood fill took about 60 % of the battle's tick.
- **The mirror.** Exchanged sides gave the exchanged result, exactly, on each of 20 seeds of a gunship against a gunship, a gunship against a lancer and a lancer against a lancer: 60 duels of 60.
- **Under FMA contraction.** `GameLogicTests` built at `-O2 -mfma -ffp-contract=fast`, which stands in for MSVC's contraction under `/arch:AVX2`, passes all 54 tests, with the same jinking figures.

## What this forecloses

- **Damage as a message of its own.** The mask is sent whole in every snapshot for every entity that has lost a voxel. A delta, or events of damage for effects such as sparks, waits until the snapshot's size needs it.
- **Wrecks and salvage.** A command loss becomes debris, as a reactor's does, until after the MVP (G51, the plan's §2.2).
- **Damage by a detonation** to what stands near it (G51).
- **Sensors that fail**, until the status markers (G49).
- **A module whose output falls with its voxels.** A module works fully until it fails.
- **Aim from above or below.** The silhouettes are level, 16 about the vertical.
- **A targeting stance and "attack this module"** (§9 of the concept), until after the MVP.
- **The broadside orbit** (G62). A ship bears where it stands; the cruiser holds its target 45° to starboard and does not orbit it.
- **The shooter's velocity in a shell's.**
