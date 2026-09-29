# Outpost.Voxel — Game Concept

**Status:** accepted by the owner, 2026-09-28, and amended by the owner on 2026-09-29 from its review, [`Archive/GameConceptReview.md`](Archive/GameConceptReview.md); G1 to G11 are the owner's answers of 2026-09-28 and G20 to G27 the proposals the owner took that day, G28 to G53 are the review's proposals and the answers to its questions, which the owner took on 2026-09-29, and G12 to G19 are derived here; the setting is deferred to G-M4 (§14); G-M0 is done (§12.2) · **Date:** 2026-09-28
**Builds on:** [`SpaceScene.md`](SpaceScene.md), the world, the client/server boundary and the renderer; [`NeuronVoxelFormat.md`](NeuronVoxelFormat.md), parts and hardpoints; [ADR-003](ADR/ADR-003-engine-and-game-layout.md), [ADR-014](ADR/ADR-014-placements.md), [ADR-015](ADR/ADR-015-client-server-boundary.md), [ADR-017](ADR/ADR-017-sector.md) and [ADR-018](ADR/ADR-018-client.md) · **Reference game:** Warzone 2100

This document says what the game is: the player's role, the match, construction, stations, the economy, combat, command and the opponent, and in what order the next phase builds them (§12). It is the parent of the technical designs that follow, one for each of its milestones; each of those says how its part is built, and `AGENTS.md` says how the code is written. It changes no code. Where it departs from `SpaceScene.md` or `NeuronVoxelFormat.md` it says so (§13), and since the owner accepted it, those documents say so too. The review that amended it on 2026-09-29 is archived as [`Archive/GameConceptReview.md`](Archive/GameConceptReview.md), and it gives each amendment's reasoning, finding by finding.

## 1. Summary

Outpost is a real-time strategy game in space, in the mold of Warzone 2100. The player commands a station and the ships it builds, from a strategic camera, in a sector shared with one opponent and a few neutral traders. What sets it apart is construction. Outside the match, the player sculpts the hulls of ships and station structures voxel by voxel, places the mounts that will hold their modules, and keeps the results in a library of designs, each with its named variants. In the match, the player mines ore from asteroids and wrecks, grows the station structure by structure, researches, fields designs from the library, refits them to other variants as the match demands, and fights. Research unlocks modules, armor materials and size classes; traders sell modules not yet researched, at a premium. Damage is per voxel: a shot removes the voxels it hits, a module fails as its voxels go, a ship whose command module fails is lost and left as a wreck to salvage, and one whose reactor fails detonates.

The engine keeps what `SpaceScene.md` built: the authoritative server on a thread of its own, the transport, the snapshots, the placements and the splat. The game changes it in four places (§11). Designs travel as data, not as files both sides already hold. Every message is encoded for one side, so that fog of war holds for shots, damage and mining as well as for what a snapshot shows. The renderer's record buffer grows during a match, and its placements mask the voxels shot away. And the levers of SpaceScene §7.6 must survive damage, because in a battle every ship is damaged. The load also moves: a battle's voxels stay within what the space scene draws today, but its placements grow about nineteenfold (§11.2).

What decides a fight is written down before combat is built. A design's figures come from its geometry, as one profile that flight, the HUD and the opponent all read (G29), and where a weapon aims, how a ship turns to bear and what a shot meets are rules the same for every side (G33, G39). Each milestone then ends at a gate: a measured figure against a threshold, with the change of course it triggers (G28), measured against the budgets of G32. The battle milestone's gate is the shape test, which asks whether designs of different shapes win in different situations.

| # | Decision | Source |
|---|---|---|
| G1 | The player is a commander, as in Warzone 2100: they design ships, shipyards build them, and they command fleets and a station from a strategic camera. "Their own ship" means their own designs. | Owner, 2026-09-28 |
| G2 | Progress is a research tree: labs spend credits and time to unlock modules, materials and upgrades. | Owner, 2026-09-28 |
| G3 | Construction is voxel by voxel. The hull is sculpted from armor materials, one voxel at a time; engines, weapons, reactors and shields are authored modules placed inside it, researched or bought. | Owner, 2026-09-28 |
| G4 | Neutral trader stations sell modules a side has not researched, at a premium, from a fixed catalogue with limited stock. Buying is the shortcut; research is the long road. | Owner, 2026-09-28 |
| G5 | Designs are made outside the match, into a library the player keeps. In the match the player fields designs from it and refits their modules; research and traders decide which can be fielded. | Owner, 2026-09-28 |
| G6 | Damage is per voxel. Shots remove voxels, a module fails as its voxels go, and a ship cut in two becomes two. | Owner, 2026-09-28 |
| G7 | Ships move on a main plane, with a few altitude bands above and below it. | Owner, 2026-09-28 |
| G8 | The first mode is a skirmish against one computer opponent in a seeded sector. Multiplayer comes later, over the boundary the engine already has; UDP and `Server.exe` stay out of the next phase. | Owner, 2026-09-28 |
| G9 | Stations grow by designed structures: the shipyard, lab, reactor, refinery and defenses are designs like ships, which constructor ships build onto the station at its connectors, in quarter turns. | Owner, 2026-09-28 |
| G10 | The economy runs on one resource: ore from asteroid fields and from wrecks, refined into one currency that pays for research, production, armor and trade. | Owner, 2026-09-28; wrecks added by the owner, 2026-09-29 (§14, question 28) |
| G11 | A battle is dozens of ships a side, up to about fifty. If the battle's gate finds per-voxel damage too dear, the count gives way before the damage cell does (G44). | Owner, 2026-09-28; amended by the owner, 2026-09-29 (§14, question 27) |
| G12 | A design is a hull and its mounts; a fit is the module at each mount, and a variant is a named fit. The hull and its mounts are made in the library; variants are kept there and made in the match (G35). | §5.1; amended 2026-09-29 (review F4.1) |
| G13 | Each of a design's sixteen palette entries pairs a color with a material class, which the side's best researched material of that class fills when the design is built or refitted, so R14's record and palette stay as they are. | §5.3; owner, 2026-09-28; classes by the owner, 2026-09-29 (§14, question 23) |
| G14 | A ship lives while its command module and its reactors work. What damage cuts off from the command module becomes a wreck. | §8.3; amended 2026-09-29 (G51) |
| G15 | A shot is a world event, as a detonation is. Every client draws the same shot, and the server alone decides what it hits. | §8.2 |
| G16 | The opponent plays through a session, as a client does: it sees what its side sees and does what commands allow. | §10 |
| G17 | Each structure of a station is an entity of its own, standing still and upright, turned by quarter turns about the vertical (S18). | §6 |
| G18 | A design's performance follows from its geometry: mass and inertia from its voxels, thrust from its thrusters, and weapon arcs that its own hull can block. | §5.4 |
| G19 | Ore is the voxels of asteroids, and of wrecks at a low grade: mining removes them as damage removes a hull's, so fields are finite and shrink as they are mined. | §7.1; wrecks added 2026-09-29 (§14, question 28) |
| G20 | A side loses when its station core is destroyed. The core is the skirmish's own design (G48), and once every field is dry it wears down (G36). | Owner, 2026-09-28; amended by the owner, 2026-09-29 (§14, questions 28 and 30); §3 |
| G21 | Fog of war is in the slice: every message a side receives holds only what its sensors reach (G30), which amends ADR-015 from protocol version 2. | Owner, 2026-09-28; amended by the owner, 2026-09-29 (review F6.6); §9 |
| G22 | SpaceScene's S-M6 and S-M7 wait until after the slice, and S-M5, which was to wait with them, is finished first. S-M8's bench runs on whole placements before G-M1 and moves into the battle milestone, revised for damage, and S-M9's levers follow the battle's gates. | Owner, 2026-09-28, amended the same day for S-M5 and on 2026-09-29 (§14, question 29); §12.1 |
| G23 | The in-game designer comes in two halves (G45): its sculpting core after command, beside G-M4, and its rule-bound half last. Until the core beats MagicaVoxel, designs are authored there and imported. | Owner, 2026-09-28; amended by the owner, 2026-09-29 (§14, question 31); §12.2 |
| G24 | One palette entry of every design and every module shows the side's color. | Owner, 2026-09-28; §5.3 |
| G25 | Traders cannot be attacked in the slice, and deal with a side only while it alone has a ship within their trade radius. A trader shows every ship within its radius to each side with a ship there, and sells each side from a stock of its own. | Owner, 2026-09-28; amended by the owner, 2026-09-29 (review F2.4); §7.3 |
| G26 | A station's structures share power through their connectors, which join two structures each, and a structure cut off from the core is unpowered. When draw exceeds supply, structures stop in a set order, turrets last (G46). | Owner, 2026-09-28; amended by the owner, 2026-09-29 (review F3.8); §6 |
| G27 | G16 is a conformance rule, `AGENTS.md` R19: the opponent plays through a session and nowhere else. | Owner, 2026-09-28; §13 |
| G28 | Every milestone ends at a gate: a measured figure, a threshold, and the change of course it triggers (§12.2). The battle milestone's gate is the shape test: a brick, a needle and a spine of equal cost each beat another over seeded engagements. | Owner, 2026-09-29; review F1.1, F6.2; §12.2 |
| G29 | A design's performance is one profile per design and variant, computed in the shared game library from its voxels, materials and modules. The profile holds mass, inertia, thrust, arcs, silhouette and the armor over the command module. Flight takes each ship's limits from it, the HUD shows it, the opponent reads it, and damage updates it. | Owner, 2026-09-29; review F6.3; §5.4 |
| G30 | Protocol version 2 is per side. An entity reaches a side as a baseline when it first comes into that side's sight, and its changes follow. Shots, damage and mining out of a side's sensors are not sent to it. | Owner, 2026-09-29; review F6.6; §9 |
| G31 | A seen enemy's design and variant reach a side in full, shown by inspection. | Owner, 2026-09-29 (§14, question 25); §9 |
| G32 | The slice's budgets are 16.7 ms a frame at 1920 × 1080 on the owner's Adreno X1-85, in Release ARM64, and 8 ms of the server's 33.3 ms tick. A headless batch of the opponent against itself measures match length and shape. | Owner, 2026-09-29 (§14, question 26); §12.2 |
| G33 | Where a weapon aims on its target, and how a ship turns to bring its arcs to bear, are rules of combat, the same for every side. The battle milestone's design chooses them and names the hull each favors. | Owner, 2026-09-29; review F1.4; §8.1 |
| G34 | Each session is bound to a side. The server refuses, by name, any command that names another side's entity, structure, design or stock, and the command set holds every order the HUD can give. | Owner, 2026-09-29; review F5.5; §10, §11.1 |
| G35 | A variant is a named fit of a design. The library keeps several per design, the refit screen can make one in the match, and production and refit orders name a variant. | Owner, 2026-09-29; review F4.1; §5.1 |
| G36 | Once every field is dry, each core's command module loses voxels at a fixed rate, so every match ends. The sector's ore is sized so that this rarely happens. | Owner, 2026-09-29 (§14, question 28); §3 |
| G37 | A design is refused when two of its parts put a voxel in one cell, when two mounts' boxes share a cell, or when a mount's box shares no face with a hull voxel. | Owner, 2026-09-29; review F3.1; §5.5 |
| G38 | A weapon, a thruster or a sensor works only along lines clear of its own hull. | Owner, 2026-09-29; review F3.5; §5.4 |
| G39 | A shot meets the enemy's hulls and asteroids, and stops at its own side's hulls without damaging them. It passes wrecks and traders, and spends its damage along its path. A weapon's shot removes no ore, and a mining laser meets asteroids and wrecks only. | Owner, 2026-09-29 (§14, questions 24 and 28); review F3.2; §8.2 |
| G40 | A size class is a voxel budget within a box, for ships and structures alike. | Owner, 2026-09-29; review F3.3; §5.4 |
| G41 | The opponent is stepped with the server once a tick, at one fixed point. The server logs every command it applies, so a skirmish replays from its seed, its build, its libraries and that log. | Owner, 2026-09-29; review F5.6; §10 |
| G42 | An ore voxel buys a fixed number of hull voxels, by its grade. G-M4 sets the rate, with a budget table of income, prices and the fields' totals. | Owner, 2026-09-29; review F2.2; §7.1 |
| G43 | A design's build order is derived at load, breadth-first through face neighbors from its command mount's cell. Voxel ids, masks and damage events follow it, and the file keeps its own order. | Owner, 2026-09-29; review L1; §5.6 |
| G44 | When a gate finds per-voxel damage too dear, lifetimes and levers give way first, then G11's count, toward thirty ships a side. A coarser damage cell comes only last, and only with an equally coarse build grid. | Owner, 2026-09-29 (§14, question 27); §14 |
| G45 | The designer comes in two halves: its sculpting core after command, beside G-M4, and its rule-bound half last. The slice ships with MagicaVoxel as its hull tool if the core is not in by the end of G-M5. | Owner, 2026-09-29 (§14, question 31); §12.2 |
| G46 | When power drawn exceeds power supplied, modules and structures stop in a set order, weapons last. A connector joins only the two structures built at it. | Owner, 2026-09-29; review F3.8; §5.4, §6 |
| G47 | An attack order may enter its target's keep-out, and stops at its weapon's range, measured to the target's nearest voxel. | Owner, 2026-09-29; review F3.7; §8.1, §9 |
| G48 | The station core is the skirmish's own design, the same for both sides, with a fixed number of connectors. Constructors repair structures, the core's command module excepted. | Owner, 2026-09-29 (§14, question 30); §6 |
| G49 | Every ship shows its command module's integrity and its modules' state. Beyond the battle zoom, ships and structures draw as icons. | Owner, 2026-09-29; review F4.2; §9 |
| G50 | A skirmish sets lifetimes for debris and for wrecks. | Owner, 2026-09-29; review F6.5; §8.3 |
| G51 | A ship whose command module fails becomes a wreck, which miners can salvage; a ship detonates, into debris, only when one of its reactor modules fails. | Owner, 2026-09-29 (§14, question 32); §8.3 |
| G52 | Ore is graded by its asteroid's palette entry, as catalogue data: the richer fields hold the higher grades, and a wreck's voxels are low grade. A refinery module lets a ship serve as a drop-off. | Owner, 2026-09-29 (§14, question 33); review F2.5; §7.1 |
| G53 | S11 and G41 are a conformance rule, `AGENTS.md` R21: the world and the opponent read no clock but the tick, and draw randomness only from `PcgHash` of the seed. | Owner, 2026-09-29 (§14, question 34); §13 |

G12 to G19 are this document's own, each derived from the owner's answers in the section it names; accepting the document accepts them. G20 to G27 began as this document's proposals, and the owner took them on 2026-09-28. G28 to G50 are the review's proposals, and G51 to G53 come from questions its answers raised; the owner took them all on 2026-09-29, with the choices §14 records (questions 23 to 35).

## 2. Scope

**In scope of this concept:** the player and the match (§3); the loop (§4); construction (§5); stations (§6); the economy, research and trade (§7); combat and damage (§8); command and control (§9); the opponent (§10); what the engine must change (§11); and the next phase's slice, milestones and gates (§12).

**The slice** is what the next phase builds: a skirmish in one seeded sector, the player against one opponent, with the whole loop in it at a small size: a research tree of about twenty items, with at most three labs a side; a catalogue of about a dozen modules; two weapons; and one resource. It is started from the command line, as the space scene is today.

**Out of scope, and why:**

- **Multiplayer between machines** (G8). The UDP transport, its reliable channel and `Server.exe` wait for a skirmish worth playing; SpaceScene §6.1 already says what they add. Delta encoding of snapshots comes with them.
- **A campaign,** and the mission scripting it needs.
- **Several resources, and ores that gate materials** (G10). Ore has grades, which set what it is worth and never what it builds (G52), and salvage is in: a wreck's voxels are low-grade ore (G10).
- **Shields,** which G3 names among the modules. They change what a shot does before it reaches a voxel, which combat designs once per-voxel damage is measured (§8.4).
- **Fighters, carriers, boarding, crews, diplomacy, more than two sides, and several sectors.** `Universe` stays free for the last (ADR-017).
- **Designs the opponent invents.** It fields an authored library, or any library the command line names (§10); a generator is a design of its own.
- **Audio.** The game needs it and `NeuronClient` has none; it is a design and an ADR of its own.
- **Menus, and saving and loading a match.** A match replays from its command log (G41), which is not a save.

## 3. The player and the match

**The player is a commander (G1).** They never fly a ship. They select ships and structures and give orders, from a camera over the sector's plane (§9); the orbit and chase cameras of SpaceScene §13 stay, for looking at one ship.

**A skirmish (G8)** is one sector built from a seed, as `Sector` is today (ADR-017): two starting positions far apart, asteroid fields near each and richer ones between them, and one or two trader stations on the ground between. Each side starts with a station core, two constructor ships, a small escort, a stated bank of credits, and no research beyond tier zero (§7.2). The bank, and whether the core alone powers the station at the start, are G-M4's to set, and G-M4 measures how soon a side earns its first credit (§12.2). The world runs at 30 ticks a second, as now.

**Victory.** A side loses when its station core is destroyed (G20). The core is the skirmish's own design, the same for both sides (G48). It can be defended and surrounded by structures, and constructors repair it, except its command module, whose damage only accumulates; it cannot be rebuilt. Once every field is dry, each core's command module wears down at a fixed rate (G36), so every match has an end.

**Scale (G11).** Dozens of ships a side, up to about fifty, and perhaps fifteen structures. Ships are the size of the two ship assets and between them: from the frigate's 1,181 voxels to the capital ship's 10,747 (SpaceScene §4), each within its class's voxel budget (G40). If the battle's gate finds per-voxel damage too dear, the count gives way first, toward thirty a side, after the lifetimes and the levers and before any coarser damage cell (G44). A match is meant to last twenty to forty minutes. That is a target for tuning the economy and the tree, not a figure anyone has measured, and G-M5's batch measures it (§12.2).

**Movement (G7).** Ships move on the sector's main plane, where the stations already lie in a flattened disc (ADR-017), and change between three altitude bands: the plane, one above it and one below. The bands have a job, because a side's own hulls block its shots (G39): a ship a band up fires over its own front rank, and a band decides which of a design's dorsal or ventral mounts bear (§5.4). The bands are spaced to clear the largest banked hull and kept closer than the shortest weapon's range. Orders are given on the plane, a group's band is a standing setting, and a grid is drawn under the selection (§9).

## 4. The loop

```
 asteroid fields ──┐
                   ├─mine──► refinery ──► credits ──┬──► structures: the station grows (§6)
 wrecks ───────────┘                                ├──► research: modules, materials, size classes (§7.2)
    ▲                                               ├──► production and refit: library designs, as variants (§5.6)
    │                                               └──► traders: modules not yet researched (§7.3)
    │
    └── fleets fight over the fields and the traders' ground ──► the enemy's station core (§3)
```

Ore is the one resource (G10): the voxels of asteroids and, at a low grade, of wrecks (G52), and credits are what a refinery makes of it. Every choice spends credits: a structure, a research project, a ship, a refit, a module bought. The fields near a start are small, safe and low in grade; the higher grades lie between the starts, and so do the traders, so expansion and fighting happen in the same place, and the wrecks a fight leaves are salvage for whoever holds the ground. Ore and credits are working names (§14, question 20).

## 5. Construction

### 5.1 Designs, mounts, fits and variants (G3, G5, G12, G35)

A **design** is a hull and its mounts. The hull is voxels, sculpted one at a time, in one or more parts of at most 256³ each (R14, N2). A **mount** is a place for a module: a named hardpoint (NVF §4.1) whose type says what kind of module it holds (`engine`, `weapon`, `mining` and so on) and whose size class says how large. The box a mount's largest module fills holds no hull voxel, and it touches the hull (§5.5). Every design has exactly one mount for a command module.

A **module** is an authored voxel model, an `.nvf` of its own, with a palette of its own (S8): reactor, thruster, weapon, mining laser, cargo hold, refinery, constructor bay, sensor, and the station's roles (§6). What it does, its thrust, its draw on the reactors, its damage, is gameplay data in a catalogue and never in its file (N8).

A **fit** is the module at each mount, and a **variant** is a named fit, of which the library keeps several for each design, the default first (G35). The hull and its mounts are made in the library, outside the match. Variants are kept there too, and the refit screen can make another in the match, from the modules the side has researched or bought (G5). So the slow, creative work of sculpting happens where no clock runs, and the match asks only for Warzone's kind of choice: which variant to build, and which to refit to.

A fitted design draws as one placement for each part of its hull and one for each module at its mount (SpaceScene §7.1). A mount is turned by one of the cube's 24 rotations in its part's space, so a design's placements are cube-symmetric to one another, and a structure, which stands upright and quarter-turned, keeps every placement on the aligned splat (§6).

### 5.2 The library

The library is the player's own collection of designs, kept between matches. Each design is an `.nvf` file for its hull and mounts, with a small file of game data beside it for its name, its material classes (§5.3) and its variants. A design is known by the hash of its content, as a model is known by its file's hash today (SpaceScene §6.2), so a library entry cannot be confused with an edited copy of itself.

Until the in-game designer's sculpting core (G-M6, G45), designs are authored in MagicaVoxel and converted by `NvfImport` (NVF §6): the hull as parts, and the mounts as markers named `<part>@<type>.<name>` (NVF §5), which MagicaVoxel turns by exactly the cube's 24 rotations. That keeps N1, since the game never reads `.vox`, and it means the first designs, the player's and the opponent's alike, exist before any editor does. How a mount's name carries its size class is G-M1's to fix, and from G-M1 a command-line tool prints each design's profile (§5.4), so that a design authored in MagicaVoxel shows its figures. The in-game designer then writes the same files.

### 5.3 Materials and color (G13, G24)

The hull is built from armor materials (G3), which research unlocks (§7.2). Each has a density, a toughness (the damage a voxel of it takes before it goes) and a price per voxel, and each belongs to one of two classes, light or heavy. R14 fixes a voxel's record at 32 bits with a four-bit color, and the owner fixed the palette at sixteen entries. This concept keeps both: each of a design's sixteen palette entries pairs a color with a material class, and a voxel's material is the side's best researched material of its entry's class when the design is built or refitted (G13). So a new material arms every design at its next build or refit, with no new version of each hull, and what the design decides is where its heavy armor goes. The pairing is gameplay data, kept in the design's game data rather than its `.nvf` (N8), so neither the record nor the renderer changes.

The owner chose the pairing over the alternative, the record's four zero bits, 28 to 31, as a material of its own (G13), and N-M1 built NVF's voxel record as NVF §4 has it ([ADR-019](ADR/ADR-019-nvf-format.md)). If sixteen entries prove too few, those bits remain the way out, but they are now a major version of NVF (NVF §4.6) as well as a change to R14.

**The side's color.** One entry of every design's and every module's palette shows the side's color, and the renderer gives each side its own variant of each palette. Placements already name their palette (ADR-014), so a side's color costs a palette per model and side, and nothing per voxel. It also costs the player one of the sixteen entries (G24).

### 5.4 Performance follows from geometry (G18, G29)

Freedom invites the solid brick and the needle. A design's performance therefore comes from its voxels and its variant, so that shape matters:

- **Mass** is the sum of its voxels', by material, and its modules'. **Acceleration** is its thrusters' thrust over its mass, and **turning** follows from its moment of inertia, which a long hull has more of.
- **Power.** Reactors supply it and every other module draws it. A variant that draws more than it supplies is refused. When damage leaves a ship short, its modules stop in the reverse of an order the player sets, weapons last by default, and until that order has an interface, all of them slow in proportion (G46).
- **Arcs.** A weapon fires, a thruster thrusts and a sensor sees only along lines clear of its own hull (G38), so a gun or an engine buried in armor does nothing, and one on a spine works almost everywhere. The line runs along the mount's +Z, the way NVF's hardpoint frame already points (NVF §4.1).
- **Silhouette.** Shots hit real geometry (§8), so a large hull is a large target.
- **Cost and time.** A design costs its voxels by material, plus its modules, and takes a shipyard a time that grows with both.
- **Size classes.** A class is a voxel budget within a box, for ships and structures alike (G40), and it decides the shipyards that can build a design; research unlocks the larger classes. A box alone would not do: a solid brick in the frigate's box holds 99.5 % of the capital ship's voxels, by arithmetic from SpaceScene §4.
- **The profile** (G29). The shared game library computes one profile for each design and variant: its mass, center of mass, inertia and thrust per axis; each mount's clear arc, walked with `GridWalk`; for a fixed set of directions, its silhouette and the armor over its command module; and its price, build time and size class. Flight takes each ship's limits from it, a design card shows it in the library and in production, refit and inspection, the opponent reads it, and a battle updates it as voxels and modules go.

These are principles. The numbers are tuned in play, and the test of them is whether designs of different shapes win in different situations, which the battle milestone's gate asks (G28, §12.2).

### 5.5 Validation

The server validates a design whenever one enters a match, since it never trusts a client, and the designer validates it as it is saved. A design is refused, by name as the readers refuse (SpaceScene §6.2), when it has no command-module mount or more than one; when its voxels are not one piece, face to face, around that mount; when two of its parts put a voxel in one cell (G37); when a mount's box holds a hull voxel, shares a cell with another mount's box or shares no face with a hull voxel, or its rotation is not one of the cube's 24; when a palette entry names a material class the catalogue lacks; or when it fits no size class's box and voxel budget (G40). The same checks hold between a station's structures, which share one grid on whole positions and quarter turns (S18), so they are exact. A variant is refused when a module does not match its mount's type and size, or when its modules draw more power than its reactors supply. Whether a side may field a valid design, given its research and what it has bought, is a separate question the match asks (§7.2).

### 5.6 Production, refit and repair in the match

A shipyard builds one variant at a time, paying for it as it goes, and a design under construction does nothing until it is complete. The ship appears voxel by voxel: server and client derive its build order at load, breadth-first through face neighbors from the command module's mount, ties broken by record index and parts taken in part order (G43), and its placement draws a growing prefix of its records in that order, which a placement's record range already allows (ADR-014). Voxel ids, masks and damage events follow that order, while the file keeps the `.vox`'s (NVF §4.3).

Production and refit orders name a variant (G35). A refit to a variant is an order to a selection: each ship goes to the nearest shipyard of its class, queues, refits and rejoins its group. The refit screen edits a variant once and shows, as it changes, the power margin, mass, acceleration, turn rate, arcs, price and time (§5.4). A refit charges the incoming modules in full. The outgoing ones go into the side's stock, repaired first at repair's price if damaged, and the next ship that needs one takes it from there; a failed one is scrapped, and a module is never turned back into credits (§14, question 35).

A shipyard also repairs a ship docked at it: its lost hull voxels at their material's price, and a damaged module at the module's price times the share of it lost. A failed module is replaced by a refit. A ship fitted with a bought module consumes it when it is built or refitted (§7.3).

## 6. Stations (G9, G17)

A station is a tree of **structures**, each a design like a ship's: a hull with mounts, fitted with the module of its role. A structure lives while its role module works, as a ship lives while its command module does (§8.3). The **station core** comes first: the skirmish's own design, the same for both sides, with a fixed number of connectors (G48). It carries the side's command, and its loss ends the match (§3). The roles in the slice:

| Structure | Its role module | What it does |
|---|---|---|
| Station core | Command | Holds the side; its connectors start the station |
| Refinery | Refinery | Turns the ore delivered to it into credits |
| Shipyard | Shipyard | Builds, refits and repairs designs up to its size class (§5.6) |
| Lab | Lab | Runs one research project at a time (§7.2) |
| Reactor | Reactor | Powers the station |
| Turret | Weapon | Defends: the ships' weapon modules, on a structure |
| Sensor array | Sensor | Sees far (§9) |

**Power.** A ship's reactors power its own modules (§5.4); a station's structures share theirs. A connector joins only the two structures built at it, so a station is a tree rooted at the core (G46). Every reactor module in a structure connected to the core supplies the whole station, and every structure draws from it (G26). When draw exceeds supply, structures stop in a set order, turrets last (G46).

**Growth.** A constructor ship builds a structure from the library at a free connector of the station, paying as it builds, and the structure appears voxel by voxel as a ship does (§5.6). Connectors are hardpoints of the structures' hulls, with a type of their own, and a structure's size class bounds it as a ship's does (G40). A structure stands upright, turned by quarter turns about the vertical, as S18 has every station stand, so every structure draws with the aligned splat and every voxel center is exact (SpaceScene §7.2).

**Repair.** Constructors repair structures at a price per voxel and at a rate that G-M4 sets, except the core's command module, whose damage only accumulates (G48).

**Destruction.** Each structure is an entity of its own, standing still (G17): it takes damage and fails on its own, leaving a wreck, or detonates if a reactor of its own fails (§8.3). A destroyed structure takes its connectors with it but not the structures beyond them, which stay where they are, unpowered while no connector joins them to the core.

## 7. Economy, research and trade

### 7.1 Ore and credits (G10, G19, G42, G52)

Ore is the voxels of asteroids, and of wrecks. Mining ships, with a mining laser and a cargo hold in their fit, cut voxels out of an asteroid or a wreck as a shot cuts them out of a hull (§8), and carry the ore to a refinery, which turns it into credits. So the machinery of per-voxel damage serves both, and a field visibly shrinks as it is mined. Fields are finite, which pushes both sides toward the rich ones between them.

**Grades** (G52). Ore is graded by its asteroid's palette entry, as catalogue data. The richer fields between the starts hold the higher grades, so where to cut is a choice, and a wreck's voxels are low grade. It stays one resource (G10). A refinery module lets a ship serve as a drop-off, so that a far field need not be hauled home from, and that ship is a target worth hunting. Without one, a miner hauling from the middle would earn about 0.39 of what one earns at home, on the review's model (F2.5).

**The exchange rate** (G42). An ore voxel buys a fixed number of hull voxels, by its grade. G-M4 fixes the rate first, and commits a budget table as its tuning contract: income per miner, the prices of fleets and stations, the fields' totals and when they run dry, and whether miners and constructors count toward G11's fifty. On the review's model the rate lies near one ore voxel to five hull voxels, since at parity the stated scale cannot be paid for (F2.2). The fields are sized so that the dry-sector rule rarely fires (G36).

Asteroids come from the sector's seed, as the stars come from the sky's (SpaceScene §11.2): a few authored asteroid models, placed and turned by quarter turns, so that they draw with the aligned splat and cost the snapshots nothing. What mining removes reaches each side whose sensors see it, as damage does (§8.2, G30). Since both sides must build the same asteroids from the seed, the generator is shared code, and it places them at whole coordinates, which no build rounds differently.

### 7.2 Research (G2)

Labs research, one project each at a time, for credits and time, and a side may build at most three. A side starts with tier zero: one material of each class, and the command module, reactor, thruster, weapon, mining laser, cargo hold and constructor bay its start needs. The tree unlocks, in branches:

- **materials,** the armor of §5.3, which arm every design at its next build or refit (G13);
- **modules,** each in marks: a thruster, then a better thruster;
- **upgrades** to modules already known, as Warzone's research improves what a side already builds;
- **size classes,** and the shipyards that build them;
- **industry:** a refinery's yield and a shipyard's speed.

Warzone 2100's tree has over four hundred technologies. The slice's has about twenty, and the cap on labs keeps the order of research a choice. A design cannot be fielded until the side has researched its size class, and researched or bought every module of its variant; its materials are the best the side has researched in each class (§5.3). The library keeps it until it can be fielded.

### 7.3 Traders (G4, G25)

A trader is a neutral station that the sector's seed places on the ground between the starts. It sells modules one step beyond what the side's research has reached, from a fixed catalogue, at a premium, with a stock that runs down as it sells and refills slowly; it is not a simulated market. A module bought joins the side's stock and is consumed when a ship fitted with it is built or refitted. Buying never teaches the side the module. So buying is the shortcut to a few strong ships early, and research the way to field many.

A trader deals with a side only while that side has a ship within its trade radius and the other has none, which makes its ground worth holding. It shows every ship within its radius to each side with a ship there, so that a refused trade gives nothing away through the fog, and it sells each side from a stock of its own (G25). G-M4 states the premium, the stock and the refill. In the slice traders cannot be attacked (G25).

## 8. Combat and damage

### 8.1 Weapons, range and aim (G33, G47)

The slice has two: a mass driver, whose shells fly at a finite speed and can miss a ship that moves, and a laser, a beam that hits at once, at short range, for as long as it fires. Each is a module at a weapon mount, on a ship or on a turret, and fires within its arc (§5.4). Each has a range, measured in three dimensions from its muzzle to the target's nearest voxel, and a weapon whose muzzle lies inside another entity's box does not fire (G47). Ships choose targets in range for themselves; the player's orders choose for them (§9).

**Aim and bearing** (G33). Where a weapon aims on its target, and how a ship turns to bring its arcs to bear, are rules of combat, the same for every side, and they pick the winning hull. Fire at the center drills toward the middle, fire spread over the surface rewards total armor, and fire at the nearest face rewards a thick prow. The battle milestone's design chooses the rules and names the hull each favors. One candidate is the center of mass, with a spread per weapon, wide for the mass driver so that it erodes and narrow for the laser so that it drills. Bringing arcs to bear is a ship behavior on the server: a nose-armed design charges, a broadside design orbits at weapon range with its covered side inward, and a stance overrides either (§9).

### 8.2 Shots are events (G15, G30, G39)

A shot is a world event, as a detonation is (SpaceScene §5.5): the weapon, the tick it fired at, its origin, its direction and its speed. A shell's flight is a pure function of those and the time, so every side that sees it draws the same shell, from the tick it enters that side's sensors (G30). The server alone decides what it hits. It sweeps each shell, tick by tick, through the grids of the designs and asteroids in its way, by G39's contract, with the walk `NeuronCore` already has (`GridWalk`), and sends what it removed as a damage event to each side that sees it: the entity, the tick and the voxels gone. A beam is resolved by the server every tick it fires, and sent as the same events.

**The contract** (G39). A shot meets the enemy's hulls and asteroids. It stops at its own side's hulls without damaging them, so a ship cannot fire through its own front rank but can fire over it from another band (§3), and no side can cut its own hull. It passes wrecks and traders. It spends its damage voxel by voxel along its path, carries the rest on, and applies its reach where it stops, so that a spaced skin of armor stops no more than its voxels are worth. A weapon's shot stops at an asteroid and removes nothing, and a mining laser meets asteroids and wrecks only.

### 8.3 Damage (G6, G14, G51)

Every voxel has hit points from its material (§5.3). A hit damages the voxels at and around where it lands, by the weapon's damage and reach, and removes those it exhausts. The server keeps each entity's damage per voxel; a client keeps only which voxels are gone, one bit each: 1,344 bytes for a ship the size of the capital ship, by arithmetic.

- **Modules.** A module's voxels are hit as the hull's are. Its output falls as they go, and below a share of them it fails.
- **Cut off.** After each hit the server finds what is still connected, face to face, to the command module. What is not becomes a **wreck**: an entity of its own, with the voxels it took, drifting on with the velocity it had, as SpaceScene §5.5 has a chunk that breaks off. A ship cut in two is thus a ship and a wreck (G6).
- **Lost.** A ship whose command module fails is lost, and becomes a wreck with the voxels it has left, for miners to salvage (G51). A ship whose reactor module fails detonates instead: it is lost at once, and its remaining voxels become debris, as a detonation's do today, which nobody can salvage. Aiming for a reactor therefore denies the salvage (§9).
- **Wrecks** are salvage: a mining laser cuts them as it cuts an asteroid, at a low grade (G52). **Debris** is scenery. Ships fly through both, as they fly through debris now. Debris lasts as long as the skirmish's debris lifetime, which starts at about a minute and is tuned on the bench; a wreck lasts until it is mined out or its own, longer lifetime runs out, which G-M4 tunes for salvage and gate 4 holds to the frame (S19, G50).

A structure takes damage the same way, with its role module as its command (§6): it leaves a wreck when that fails, and detonates when a reactor of its own does.

### 8.4 What the numbers must show

Per-voxel damage at fifty ships a side is the concept's largest cost, in three places at once: the server's sweeps and connectivity searches, the damage events on the wire, and the renderer's masks, with the levers of SpaceScene §7.6 held on damaged placements (§11.1). G-M2, the battle milestone, measures all three on a battle against G32's budgets before anything is optimized: the tick at gate 3, and the aftermath's worst frame at gate 4 (§12.2). If they fail, G44 says what gives way, in order.

## 9. Command and control

**The camera** looks down on the sector's plane: it pans, zooms, turns and tilts, between a battle zoom, where a voxel covers at least a pixel, and a sector zoom, where ships and structures draw as icons over their coarse models (G49), with the minimap beside both. A voxel covers a pixel at about 1,375 units, by arithmetic for a 45° view over 1,080 pixels (SpaceScene §11.1), and fights frame near that by default. Ownership shows through the overlay at every zoom. Each ship stands on a line dropped to the plane, and a grid is drawn under the selection, so its band reads at a glance. The orbit and chase cameras of SpaceScene §13 stay, for looking at one ship.

**Selection** is by click and by box. A click picks the nearest entity whose projected box, grown to at least about 10 pixels, holds the cursor, and never picks debris or a wreck; a box selects the entities whose centers it holds. Inspecting and targeting a module use the exact pick: the visibility buffer's voxel id names the placement (SpaceScene §7.3), the placement names the entity and the module, and a readback of that one pixel delivers it within a couple of frames. Selections go into control groups on the number keys, and the debug views move behind a modifier (SpaceScene §13).

**Orders:** move, on the plane and into a band; attack, which may enter its target's keep-out and stops at range (G47); attack-move; stop; hold; guard; retreat for repair, as a standing order; mine; build a structure; produce, refit to a variant and repair; research; trade. A targeting stance (engines, weapons, nearest) and "attack this module" choose what a ship aims for, with the command module left off the list, so that no salvage-keeping kill is one click away (G33). A group moves in formation, as squadrons of up to about a dozen ships of one speed class, whose staggered rows extend ADR-017's slots with no slot in another's line of fire, since a side's own hulls block its shots (G39). The skirmish allows orders while it is paused, and the client draws its own marker for an order at once, since the order shows in a snapshot only after the next tick and the interpolation delay (ADR-018).

**Paths.** The plane is open but not empty: stations, structures and asteroid fields are obstacles, each within its keep-out sphere, as the sector's routes already avoid them (ADR-017). Ships find paths around the keep-outs, steer along them with ADR-017's flight model (pure pursuit, bank and slot) within their own profiles' limits (G29), and keep apart from one another by steering, in three dimensions and across bands, never into a keep-out, which amends ADR-017 (§13). They no longer pass through one another, but they still pass through wrecks and debris.

**Fog of war.** Each side sees what its ships' and structures' sensors reach, and every message it receives holds only that (G21, G30). The server already decides what reaches each client, so a side cannot see past its sensors however its client is changed. So protocol version 2 is encoded per session from G-M1, which ADR-015 forecloses today (§11.1), and fog is switched on in G-M5. Firing does not give the shooter away: a shot reaches a side only once it enters that side's sensors.

| What | Who receives it | When |
|---|---|---|
| An entity's baseline: its design and variant in full (G31), its module states and its mask | Each side that sees it | When it comes into that side's sight, and again whenever it returns to it |
| An entity's changes: its transform, damage and module states | Each side that sees it | Every tick it stays in sight |
| A shot | Each side that sees it | From the tick it enters that side's sensors |
| Damage and mining | Each side that sees them | As they happen |
| An enemy structure | The other side | As last seen, until it is seen again |
| A side's own credits, research, queues and stock | That side alone | In every snapshot (G34) |
| The ships within a trader's radius | Each side with a ship inside it | While that ship is inside (G25) |

**The HUD:** the side's credits and power, the selection with each ship's condition (G49), the control groups, the queues of production and research, the minimap, the traders' state, alerts, and the refit screen. It lands with the milestones that need it: in G-M3, the overlay, the selection panel with the condition readout, the control groups, the order bar and the minimap; in G-M4, credits and income, power, the shipyard, the lab's list, the trader, building at a connector in quarter turns, and the refit screen; in G-M5, fog on the minimap and the end screen.

**The condition readout** (G49) is bars for the command module's integrity and for the hull; four status markers, for weapons, thrust, power and sensors, which the client computes from its missing voxels and the variant; an inspect view that shows each module by state; one small field on the wire that says why a weapon is silent; and a neutral palette and a "ship split" alert for wrecks.

The canvas draws text and quads (ADR-010). The HUD needs panels, labels, lists, bars, tooltips, buttons with a hotkey or the reason they are disabled, world markers and lines, and the input to route among them, and D9's "no third-party code" means that layer is the tree's own.

## 10. The opponent (G16)

The opponent plays the same game through the same door: a session over a loopback, as the client has. It receives its side's messages, fog included, and sends commands; it never touches the world's objects, and the server refuses any command that names another side's entity, structure, design or stock (G34). So it cannot cheat by accident, every rule the player meets it meets, and it exercises the protocol as a second client would. It runs in the server's process, stepped with the server once a tick at one fixed point, as the bench steps it (ADR-018), and the server logs every command it applies, with its world tick and its session, so that a skirmish replays from its seed, its build, its libraries and that log (G41). It budgets its thinking in work, never in time, and draws randomness only from `PcgHash` of the seed (G53).

It fields any library the command line names, by default its own, which Claude Code authors unseen by the owner, so that the libraries' author is not also their only opponent. At each size class its own library holds at least three designs with distinct profiles, the seed draws which it fields, and a CI test checks that every one of them validates, fits within power, and can be reached by research or a trader. It researches and trades by the same rules as the player to reach its designs. Its play in the slice is one level: expand to the fields, keep its economy running, research toward its library, remember what it has seen, hold a trader's radius, defend, and attack in waves once its fleet outweighs the enemy it remembers. G-M5's design sets the rest, from the review's model (F5.2). A stronger opponent is a design of its own.

It is game code that reads messages and writes commands, so it belongs neither in `GameLogic`, which holds the world, nor in `GameLib`, which draws it. A project of its own, referencing only `NeuronCore` and the shared game library (§11.1), makes that boundary one the compiler keeps. It starts in G-M2, as the battle bench's scripted second side, and like every new project it is an ADR (`AGENTS.md` §2). What the compiler cannot keep, `AGENTS.md` R19 does (G27), and R21 holds it to the tick and the seed, as it holds the world (G53).

## 11. What the engine must change

### 11.1 The changes

| Change | Why | What it amends |
|---|---|---|
| Designs on the wire: a design, its game data and its variants, sent once to each side and known by its content hash; an entity names its design, its variant, its side and its state; protocol version 2 | G3, G5, G31, G35 | S13, and ADR-015's messages |
| Designs from the client: the server receives a side's designs, validates them (§5.5) and gives them ids | G5 | ADR-015's commands |
| Protocol version 2 encoded per session: a baseline on first sight, then the changes; sessions bound to sides | G21, G30, G34 | ADR-015, which sends the same bytes to every session |
| The command set: every order of §9, plus production, refit, repair, research, trade and building; a side's own credits, research, queues and stock in its snapshots | G16, G34 | ADR-015's commands |
| A log of every command the server applies, with its world tick and its session | G41 | ADR-015 |
| Events for shots, damage and mining, sent per side; wrecks as entities | G6, G15, G19, G30 | SpaceScene §5.5, ADR-015 |
| A shared game library: the module catalogue, the material classes, the designs' game data and variants, their profiles and validation, and the asteroids' generator, which both sides read | G3, G19, G29, G37 | ADR-003, which creates it when the first such type appears |
| Each design's profile (mass, inertia, thrust, arcs, silhouette and armor), computed once in the shared game library, from which flight takes its limits | G18, G29 | ADR-017's class table |
| A record buffer that grows as designs arrive, instead of one static buffer | G3 | SpaceScene §7.1 |
| A mask per placement, one bit per voxel, which the splat skips | G6 | SpaceScene §5.5, ADR-014 |
| The levers kept on damaged placements: the reachable voxels updated as voxels go, and the coarse model drawn for a damaged placement far away | G6, G11 | SpaceScene §7.6, S-M9 |
| Many more draws: `ExecuteIndirect`, once the bench says the draw calls cost (§11.2) | G11 | SpaceScene §7.4; D1 does not forbid it |
| Picking: through the visibility buffer for a module, and on the CPU against projected boxes for an entity | G1 | — |
| Paths, separation in three dimensions, squadrons, orders and the strategic camera | G1, G7, G47 | ADR-017's flight and its foreclosure, SpaceScene §13 |
| The opponent's project | G16 | ADR-003 |
| A world-anchored overlay: icons, bars, marks and lines, with a sloped quad | G1, G49 | ADR-010 |
| A layer of panels, lists and buttons on the canvas | G1 | ADR-010 |

### 11.2 The load, by arithmetic

These figures are arithmetic on assumed counts, not measurements: fifty ships a side, each a hull of one part and eight modules; fifteen structures a side, each a hull and two modules; ships of 3,000 voxels and structures of 20,000 on average.

- **Placements:** 2 × (50 × 9 + 15 × 3) = 990, where the default space scene has 52 (SpaceScene §5.2). Seen by the camera and the one shadow map the slice keeps (G22), that is up to 1,980 draws a frame, and 3,960 once S-M7's three cascades land, one `DrawIndexedInstanced` each as `SplatPass` issues them today.
- **Voxels:** 2 × (50 × 3,000 + 15 × 20,000) = 900,000, within the default space scene's 1,033,408. The classes' voxel budgets (G40) are what hold a fleet to such counts: in the capital ship's box alone, a hundred solid bricks would hold 7.3 million voxels.
- **Snapshots:** 130 ships and structures at today's 48 bytes each are 6,240 bytes a tick for a side that sees them all, and 187,200 bytes a second at 30 ticks. Over a loopback that is nothing; over a network it wants the delta encoding the UDP design adds.
- **Designs** cross once each: 42,988 bytes for one the size of the capital ship, at R14's four bytes a voxel.
- **The match's worst frame** comes late: the live ships, structures and fields, the wrecks and debris their lifetimes keep (G50), and each second's events. The battle's bench reports it beside its median (gate 4).

So the load moves from voxels to placements and events. The renderer was built for the first; the second is what G-M2's bench must measure, and gate 1 measures whole placements before G-M1.

## 12. The next phase

### 12.1 What happens to the plans that run

**`SpaceScene.md`.** S-M4 is merged, and the owner checked it on 2026-09-28 (SpaceScene §16). S-M5, S-M6 and S-M7 (the sky, bloom, temporal anti-aliasing and the cascades) are the look, and the slice plays without them, so they were to wait until after it (G22). The owner chose the same day, while S-M5 was being built, to finish it first ([ADR-021](ADR/ADR-021-sky.md), [ADR-022](ADR/ADR-022-bloom.md)); S-M6 and S-M7 still wait. S-M8's bench and S-M9's levers are what a strategic camera needs most, since a view of a sector puts most of its voxels under a pixel (SpaceScene §17). So S-M8's bench first measures whole placements, before G-M1 fixes protocol version 2 (gate 1), which needs `--bench` to take the world's options it refuses today (ADR-018). It then moves into G-M2, the battle milestone, revised for damage, and S-M9's levers follow the battle's gates (G22, §12.2).

**`NeuronVoxelFormat.md`.** Designs and modules are `.nvf` files. N-M0 to N-M3 are done, and N-M4, the Blender extension, awaits the owner's checklist (NVF §10; [ADR-019](ADR/ADR-019-nvf-format.md), [ADR-020](ADR/ADR-020-nvf-import.md)). N-M3 and N-M4 were to wait, and the owner kept them with N-M1 and N-M2 (NeuronVoxelFormat.md §11, question 12). What this concept still needs of NVF is the follow-up that has `Outpost.exe` load `.nvf` instead of `.vox`, with its change to SampleRenderer §7 (NVF §10), and G-M1 opens with it.

**Question 25** of SpaceScene §17, how N and B find the flights' leaders, is answered by the concept rather than by a flag: selection and control groups replace the cycling in G-M3, and an entity's side crosses the wire in protocol version 2 (§11.1).

### 12.2 Milestones and gates

Each milestone gets a technical design before its code, as `AGENTS.md` asks, and its ADRs land with its commits. The next free number is ADR-023: ADR-019 and ADR-020 went to NVF's N-M1 and N-M2, for which the owner reserved them (NeuronVoxelFormat.md §10), and ADR-021 and ADR-022 to SpaceScene's S-M5. Each milestone also ends at a gate (G28): a measured figure, a threshold, and the change of course it triggers, with the budgets of G32 behind the thresholds.

Combat comes before command, as the owner decided on 2026-09-29 (§14, question 29). Ships choose their own targets (§8.1), so fleets fight on the bench before anyone commands them, the bench is a proving ground for the designs authored in MagicaVoxel, and the shape test comes a milestone sooner.

| | Delivers | Done when |
|---|---|---|
| G-M0 | This concept accepted; the running plans re-planned as §12.1 says | Done on 2026-09-28: the owner accepted it, with §14's questions answered and the setting deferred, and `SpaceScene.md`, `NeuronVoxelFormat.md` and `AGENTS.md` record it (§13) |
| G-M1 | **Designs as data.** `Outpost.exe` loads `.nvf` (NVF §10's follow-up, with its change to SampleRenderer §7). The shared game library holds designs, mounts, variants, material classes and the module catalogue. Each design has its profile (G29), and a command-line tool prints it. Validation adds G37, G38 and G40. Protocol version 2 is per side, with sessions bound to sides (G30, G34), and fog off until G-M5. The record buffer grows. The sector's ships are rebuilt as fitted designs authored in MagicaVoxel, flying on their profiles. | The sector flies and draws fitted designs on their own profiles. Each design crosses the loopback once. The design and protocol tests are green. Gates 1 and 2 are met. |
| G-M2 | **Combat.** Weapons with ranges (G47), G39's contract, and G33's rules of aim and bearing. Shots and damage as per-side events, with masks, module failure, the command module's loss, wrecks and a reactor's detonation (G51). Lifetimes for debris and wrecks (G50). The bench revised to a battle (S-M8), with a headless runner. The opponent's project, as the bench's scripted second side. | Two fleets fight to the end. In the tests, the client's missing voxels are the server's, bit for bit, with entities leaving and re-entering view. The battle's note is committed against G32's budgets. Gates 3, 4 and 5 are met. |
| G-M3 | **Command.** The strategic camera, with its battle and sector zooms and icons (G49); picking and selection; control groups; orders on the plane and in bands; paths and separation, amending ADR-017; squadrons; the condition readout; and the HUD's first panels (§9). S-M9's levers on damaged placements, if gate 4 calls for them. | The owner commands fleets of fifty around the sector, and gate 6 is met. |
| G-M4 | **Economy and stations.** Asteroids with graded ore, mining and salvage, and refineries on stations and ships (G52), with credits at G42's rate. The station core (G48), and structures built at connectors, powered (G46) and repaired by constructors. Production, refit to variants, and repair. The research tree, with its cap on labs. Traders. | One side runs the loop from ore to a fitted design fielded in battle. The first credit arrives within a minute. The budget table is committed. Gate 7 is met. |
| G-M5 | **The skirmish.** The opponent plays through its session, remembering what it has seen and holding a trader's radius, stepped and logged (G41). Fog of war, victory with G36's rule, the skirmish's layout from a seed, and the batch's telemetry. | The owner plays a skirmish against the opponent to its end. The opponent kills a passive side's core within 40 minutes on 10 seeds of 10. Mirror matches end within 20 to 40 minutes on 7 seeds of 10. A logged match replays to the same bytes. Gate 8 is met. |
| G-M6 | **The designer,** in two halves (G45). Its sculpting core (voxels, colors, mounts, undo and saving) is built after command, beside G-M4; its rule-bound half (materials, validation, size classes and the profile's card) comes last. | The owner builds a new 1,000-voxel design, with mounts and material classes, faster in the game than through MagicaVoxel and `NvfImport`, and fields it in a skirmish. |

The gates come from the review (F6.2). Their budgets are G32's, and the rest of their thresholds are the review's judgement, which the owner took with them:

| Gate | Where | Measured | Threshold | Change of course |
|---|---|---|---|---|
| 1 | Before G-M1 | Per-pass GPU times, on the default sector and on `--stations 1 --frigates 990 --capitals 0`: 991 placements, 1,394,238 voxels | The splats over 10 ms, or the frame over 16.7 ms | S-M9's levers for whole placements, and `ExecuteIndirect` if the CPU is over, move ahead of G-M1. If both are under, §14's "Placements, not voxels" is struck. |
| 2 | End of G-M1 | The profiles of a brick, a needle and a spine of equal cost | Any two within 10 % on acceleration, turn rate and arc cover | Retune G29's derivations or the materials before combat is built on them |
| 3 | End of G-M2 | The server's tick at the 99th percentile, 50 ships a side for 5 minutes in lockstep | Over 8 ms after one pass of optimization | G44's ladder |
| 4 | End of G-M2 | The worst frame of the aftermath, at peak wrecks and debris | Over 16.7 ms | G50's lifetimes, then S-M9's levers on damaged placements, then G44's ladder |
| 5 | End of G-M2 | Each shape's win rate against the others, 20 seeds a pairing, headless, under G33's rules | One shape wins over 80 % against both others, or no engagement range reverses a pairing | Retune. After two failed retunes, the owner reopens G18 before command is built. |
| 6 | End of G-M3 | Pixels per voxel at the default strategic zoom; whether the owner can name a failed module on recorded runs without the orbit camera | Under 1 px, or the owner cannot | G49's readout and icons, and a nearer default zoom |
| 7 | End of G-M4 | Time to the first credit; the median length of 10 matches of the opponent against itself; whether the owner refits in 3 matches | A first credit after one minute; a median outside 20 to 40 minutes; no refits at all | Tune income and prices. No refits means §14's first risk has fired, and G5's split is reopened before G-M5. |
| 8 | End of G-M5 | The opponent against a scripted rush and a scripted turtle, 10 seeds each | It wins fewer than half | A second pass on the opponent before the designer's rule-bound half |

**The batch** (G32). A headless batch of the opponent against itself, seeded and in lockstep, runs from G-M2 on and writes the telemetry that the gates and §14's risks read: the opponent's decisions, with their inputs; the server's economy, minute by minute; its engagements, with the designs, bearings, shots and hits in each; every command refused; an after-action record for each design; and a summary of match length, winner and cause, and wins by design pair and range band.

**The designer** comes in two halves (G45). Its sculpting core needs only the picking that command builds, so it comes after command, beside G-M4. Its rule-bound half stays last, so that the materials, the validation, the size classes and the profile's card are the ones the skirmish has tested. Until the core beats MagicaVoxel, designs are authored there (§5.2), and if it is not in by G-M5's end, the slice ships with MagicaVoxel as its hull tool.

## 13. What changes in the other documents

The owner accepted this concept on 2026-09-28 and amended it from its review on 2026-09-29. G-M0 recorded the acceptance where it changes a running plan, the amendment records its own, and the rest follows with the milestones that implement it:

- **`SpaceScene.md`:** its status line and §16 record §12.1's plan (G22): combat before command, and S-M8's bench on whole placements before G-M1. §17 records question 25 as answered, with N and B giving way in G-M3. S13 is superseded in part by designs on the wire, and §7.1's static buffer and §7.6's rule for damaged placements are amended, by the ADRs of G-M1 and G-M2. §13's keys give 0 to 9 to control groups and move the debug views behind a modifier when G-M3 builds them, and S19's lifetime takes the skirmish's values (G50) when G-M2 does.
- **`NeuronVoxelFormat.md`:** its status line and §10 record §12.1's plan. The format does not change. Mounts and connectors are hardpoint types, and G37's validation, G40's classes, G43's build order and the material classes are the game's, so they stay out of the file (N8). §5's marker names carry a mount's size class, which G-M1 fixes.
- **ADR-015** is amended by protocol version 2, encoded per side, with baselines on first sight, sessions bound to sides, the full command set and a command log (G30, G34, G41). What it forecloses changes with it: a message that differs between sessions becomes the rule.
- **ADR-003** is amended by the shared game library, which holds the profiles and the validation (G29, G37), and by the opponent's project, which lands with G-M2 as the bench's second side, each with an ADR of its own.
- **ADR-010** is amended by a world-anchored overlay and a sloped quad, with the quad's CPU twin (R15, G49).
- **ADR-017** is amended four ways: flight takes each ship's limits from its profile (G29); separation lifts the foreclosure on steering across a route, keeping the keep-out guarantee and its ten-minute test; squadrons extend the slots; and an attack may enter a keep-out (G47).
- **ADR-018:** the bench takes the world's options (gate 1), its timeline runs through a battle's aftermath, and N and B give way to selection and control groups.
- **`AGENTS.md`:** the paragraph that names the designs names this one, and §2's table gains the new projects as their ADRs land, the opponent's with G-M2. R19 makes G16 a rule beside R18 (G27); what its side is sent now means every message it receives (G30), with no change to its words. R21 makes S11 and G41 a rule (G53), and R14's paragraph counts this document among the designs a rule may come from.

## 14. Risks and open questions

**Answered by the owner on 2026-09-28:**

1. **The player is a commander (G1),** and "their own ship" means their own designs.
2. **Learning is a research tree (G2).**
3. **Construction is voxel by voxel (G3).**
4. **Neutral traders sell modules (G4),** from a fixed catalogue with limited stock, at a premium.
5. **The voxels are the hull, and modules give function (G3).**
6. **Designs are made in a library, and refitted in the match (G5).**
7. **Damage is per voxel (G6).**
8. **Ships move on a plane with altitude bands (G7).**
9. **The first mode is a skirmish against one computer opponent (G8).**
10. **Stations grow by designed structures (G9).**
11. **The economy runs on one resource (G10).**
12. **A battle is dozens of ships a side (G11).**
13. **Victory comes when a side's station core is destroyed (G20, §3),** rather than when every structure and every ship is gone.
14. **Fog of war is in the slice (G21, §9),** through snapshots that differ by side, which amends ADR-015.
15. **S-M5, S-M6 and S-M7 wait until after the slice, and S-M8 and S-M9 move into G-M3 (G22, §12.1).** The owner took S-M5 out of the wait the same day, and it is finished first. Since question 29, the battle milestone that takes S-M8 and S-M9 is G-M2.
16. **The designer comes last in the phase (G23, §12.2),** with designs authored in MagicaVoxel until then.
17. **Materials pair with palette entries (G13, §5.3),** sixteen pairs a design, and the record's spare bits stay zero.
18. **The side's color takes one of a design's sixteen palette entries (G24, §5.3).**
19. **Traders cannot be attacked in the slice (G25, §7.3),** and deal with a side only while it alone holds their radius.
20. **The setting waits for G-M4 (§4).** Ore, credits, station core, constructor and the modules' names stay working names until G-M4's HUD needs the setting's own; "Outpost" suggests a frontier.
21. **A station's structures share power through their connectors (G26, §6),** and a structure cut off from the core is unpowered.
22. **G16 is a conformance rule, `AGENTS.md` R19 (G27, §13).**

**Answered by the owner on 2026-09-29,** from the review's questions (23 to 31) and from four that its answers raised (32 to 35). Where one amends an answer above, its decision's row says so:

23. **Research fills material classes (G13, §5.3):** a palette entry pairs a color with a class, light or heavy, and the side's best researched material of that class fills it. G5 stands.
24. **A side's own hulls block its shots, and take no damage from them (G39, §8.2).** So G7's bands have a job (§3), squadrons keep their rows out of one another's line of fire (§9), and no side can cut its own hull.
25. **A seen enemy's design reaches a side in full, shown by inspection (G31, §9).**
26. **The budgets are 16.7 ms a frame and 8 ms of the tick, on the owner's Adreno X1-85 in Release ARM64 (G32).**
27. **Fewer ships give way first (G44, G11),** toward thirty a side, after the lifetimes and the levers and before any coarser damage cell.
28. **Once every field is dry, the cores wear down (G36), and salvage is in (G10, G19):** a wreck's voxels are low-grade ore, which reverses the exclusion of salvage §2 had.
29. **Combat comes before command, and whole placements are benched before G-M1 (G22, §12.2).**
30. **Constructors repair structures, the core's command module excepted (G48, §6).**
31. **The designer comes in two halves, time-boxed to G-M5's end (G45, G23, §12.2).**
32. **A lost ship leaves a wreck, and a failed reactor detonates it (G51, §8.3).** Asked because salvage would otherwise pay little: a ship lost to its command module detonated into debris nobody can salvage.
33. **Ore is graded, and a ship can carry a refinery (G52, §7.1),** the review's optional F2.5.
34. **S11 and G41 are a conformance rule, `AGENTS.md` R21 (G53, §13).**
35. **A refit's outgoing modules go into the side's stock (§5.6),** and a module is never turned back into credits; the two reviewers of F2.6 had offered a refund of a share, or none.

**Risks:**

- **Sculpting inside a strategy game.** The library keeps the clock away from the sculpting (G5), which works only if a refit to a variant is enough to answer what happens in a match. Gate 7 asks whether the owner refits at all, and G-M5 and G-M6 show the rest.
- **The designer comes late** (G45). Its sculpting core arrives beside G-M4 and its rules last, so what sets the game apart is still among the last things built. The time box decides it: past G-M5's end, the slice ships with MagicaVoxel as its hull tool, which works, but is not the game's construction.
- **The rules that pick the winning hull.** Aim, bearing, the sweep's contract, exposure and validation (G33, G37, G38, G39) are the battle milestone's to write. Until they are, the shape test measures the code's accidents, not the designs.
- **The brick and the needle.** §5.4's principles stay principles until tuned, and balance is iteration, not design. Gate 5, the shape test at the end of G-M2, is the test, and after two failed retunes the owner reopens G18 before command is built.
- **Per-voxel damage at scale** is the largest technical cost (§8.4). If the battle's bench finds it too dear against G32's budgets, the fallbacks come in G44's order: lifetimes and levers, then fewer ships, and damage in coarser cells only last, and only with an equally coarse build grid. Module hit points are not one, since G6 rules them out.
- **Placements, not voxels** (§11.2): nineteen times the placements, and up to 1,980 draws a frame with the slice's one shadow map, or 3,960 with S-M7's cascades. Gate 1 measures whole placements before G-M1; `ExecuteIndirect` is the expected answer, and the bench decides.
- **Own hulls block fire** (G39). So formations, bands and the opponent must all keep lines of fire clear, and command is harder than in a game where shots pass friends. Gate 6 and the batch's refused and wasted shots show what it costs.
- **The fog is a property of the protocol** (G30). A message type added without its per-side rule leaks. The batch runs the opponent with and without its enemy's internals, which is the check.
- **The economy's numbers decide whether a match ends in its target.** The exchange rate, the ore budget and the cap on labs are G-M4's to set against its budget table, and the batch measures the length (G36, G42).
- **Salvage rewards the defender.** A failed attack now pays the side it attacked (G10, G51), which favors defense; the dry-sector rule still ends a stalemate (G36), and the batch measures how often the defender wins.
- **A reactor is a kill switch the player can aim for** (G51). "Attack this module" can pick one, since only the command module is off its list (§9). If that proves one click too close, reactors leave the list as well (G33).
- **An opponent is a project.** One that plays convincingly is a large piece of work; the slice's plays at one level, and a weak opponent makes the skirmish a test bed rather than a game. Gate 8 holds it to beating a scripted rush and a scripted turtle.
- **The opponent's library is frozen.** A design that beats the whole of it beats it every match. The opponent fields any library, and the batch pits the libraries against each other (§10).
- **The UI is the tree's own.** D9 rules out third-party code, so the HUD, the refit screen and the designer are built on the canvas, from the panels up, and §9's schedule is the toolkit's specification. A toolkit would need the owner to revise D9.
- **Determinism holds within one build** (SpaceScene §17). That is enough for a skirmish in one process, and G41's stepping and command log make a skirmish replay, while R21 keeps the world and the opponent off the wall clock. Multiplayer across x64 and ARM64 stays authoritative, as the engine already is, rather than lockstep.
- **The phase is long:** six milestones before a skirmish is whole. The slice keeps its content small, about twenty research items, a dozen modules and two weapons, so that the whole loop fits.

## 15. References

- Warzone 2100, Pumpkin Studios, 1999; open source since 2004: https://wz2100.net
- [`SpaceScene.md`](SpaceScene.md): the world, the boundary and the renderer this concept builds on.
- [`NeuronVoxelFormat.md`](NeuronVoxelFormat.md): parts, hardpoints and the importer that designs and modules use.
- [`Archive/GameConceptReview.md`](Archive/GameConceptReview.md): the review of 2026-09-29, its 36 findings, and the reasoning behind G28 to G50.
- [`Archive/SampleRenderer.md`](Archive/SampleRenderer.md): D1, D5, D9 and D13, which constrain the draws, the palette, the UI and the canvas.
