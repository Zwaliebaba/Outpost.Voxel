# Outpost.Voxel — Game Concept

**Status:** accepted by the owner, 2026-09-28; G1 to G11 are the owner's answers of that day and G20 to G27 the proposals of §13 and §14 the owner took, G12 to G19 are derived here from those answers, and one question, the setting, is deferred to G-M4 (§14); G-M0 is done (§12.2) · **Date:** 2026-09-28
**Builds on:** [`SpaceScene.md`](SpaceScene.md), the world, the client/server boundary and the renderer; [`NeuronVoxelFormat.md`](NeuronVoxelFormat.md), parts and hardpoints; [ADR-003](ADR/ADR-003-engine-and-game-layout.md), [ADR-014](ADR/ADR-014-placements.md), [ADR-015](ADR/ADR-015-client-server-boundary.md), [ADR-017](ADR/ADR-017-sector.md) and [ADR-018](ADR/ADR-018-client.md) · **Reference game:** Warzone 2100

This document says what the game is: the player's role, the match, construction, stations, the economy, combat, command and the opponent, and in what order the next phase builds them (§12). It is the parent of the technical designs that follow, one for each of its milestones; each of those says how its part is built, and `AGENTS.md` says how the code is written. It changes no code. Where it departs from `SpaceScene.md` or `NeuronVoxelFormat.md` it says so (§13), and since the owner accepted it, those documents say so too.

## 1. Summary

Outpost is a real-time strategy game in space, in the mold of Warzone 2100. The player commands a station and the ships it builds, from a strategic camera, in a sector shared with one opponent and a few neutral traders. What sets it apart is construction. Outside the match, the player sculpts the hulls of ships and station structures voxel by voxel, places the mounts that will hold their modules, and keeps the results in a library of designs. In the match, the player mines ore, grows the station structure by structure, researches, fields designs from the library, refits their modules as the match demands, and fights. Research unlocks modules and armor materials; traders sell modules not yet researched, at a premium. Damage is per voxel: a shot removes the voxels it hits, a module fails as its voxels go, and a ship whose command module fails is lost.

The engine keeps what `SpaceScene.md` built: the authoritative server on a thread of its own, the transport, the snapshots, the placements and the splat. The game changes it in four places (§11). Designs travel as data, not as files both sides already hold. Snapshots carry shots and damage, and differ by side for fog of war. The renderer's record buffer grows during a match, and its placements mask the voxels shot away. And the levers of SpaceScene §7.6 must survive damage, because in a battle every ship is damaged. The load also moves: a battle's voxels stay within what the space scene draws today, but its placements grow about nineteenfold (§11.2).

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
| G10 | The economy runs on one resource: ore from asteroid fields, refined into one currency that pays for research, production, armor and trade. | Owner, 2026-09-28 |
| G11 | A battle is dozens of ships a side, up to about fifty. | Owner, 2026-09-28 |
| G12 | A design is a hull and its mounts; a fit is the module at each mount. The hull and its mounts are made in the library, the fit in the match. | §5.1 |
| G13 | Each of a design's sixteen palette entries pairs a color with a material, so R14's record and palette stay as they are. | §5.3; owner, 2026-09-28 |
| G14 | A ship lives while its command module works. What damage cuts off from the command module becomes a wreck. | §8.3 |
| G15 | A shot is a world event, as a detonation is. Every client draws the same shot, and the server alone decides what it hits. | §8.2 |
| G16 | The opponent plays through a session, as a client does: it sees what its side sees and does what commands allow. | §10 |
| G17 | Each structure of a station is an entity of its own, standing still and upright, turned by quarter turns about the vertical (S18). | §6 |
| G18 | A design's performance follows from its geometry: mass and inertia from its voxels, thrust from its thrusters, and weapon arcs that its own hull can block. | §5.4 |
| G19 | Ore is the voxels of asteroids: mining removes them as damage removes a hull's, so fields are finite and shrink as they are mined. | §7.1 |
| G20 | A side loses when its station core is destroyed. | Owner, 2026-09-28; §3 |
| G21 | Fog of war is in the slice: each side's snapshots hold only what its sensors reach, which amends ADR-015. | Owner, 2026-09-28; §9 |
| G22 | SpaceScene's S-M5, S-M6 and S-M7 wait until after the slice; S-M8 and S-M9 move into G-M3, revised for a battle and for damage. | Owner, 2026-09-28; §12.1 |
| G23 | The in-game designer is the phase's last milestone; until it lands, designs are authored in MagicaVoxel and imported. | Owner, 2026-09-28; §12.2 |
| G24 | One palette entry of every design and every module shows the side's color. | Owner, 2026-09-28; §5.3 |
| G25 | Traders cannot be attacked in the slice, and deal with a side only while it alone has a ship within their trade radius. | Owner, 2026-09-28; §7.3 |
| G26 | A station's structures share power through their connectors, and a structure cut off from the core is unpowered. | Owner, 2026-09-28; §6 |
| G27 | G16 is a conformance rule, `AGENTS.md` R19: the opponent plays through a session and nowhere else. | Owner, 2026-09-28; §13 |

G12 to G19 are this document's own, each derived from the owner's answers in the section it names; accepting the document accepts them. G20 to G27 began as this document's proposals, and the owner took them on 2026-09-28.

## 2. Scope

**In scope of this concept:** the player and the match (§3); the loop (§4); construction (§5); stations (§6); the economy, research and trade (§7); combat and damage (§8); command and control (§9); the opponent (§10); what the engine must change (§11); and the next phase's slice and milestones (§12).

**The slice** is what the next phase builds: a skirmish in one seeded sector, the player against one opponent, with the whole loop in it at a small size: a research tree of about twenty items, a catalogue of about a dozen modules, two weapons and one resource. It is started from the command line, as the space scene is today.

**Out of scope, and why:**

- **Multiplayer between machines** (G8). The UDP transport, its reliable channel and `Server.exe` wait for a skirmish worth playing; SpaceScene §6.1 already says what they add. Delta encoding of snapshots comes with them.
- **A campaign,** and the mission scripting it needs.
- **Several resources, tiered materials and salvage as income** (G10). Wrecks are scenery in the slice (§8.3).
- **Shields,** which G3 names among the modules. They change what a shot does before it reaches a voxel, which combat designs once per-voxel damage is measured (§8.4).
- **Fighters, carriers, boarding, crews, diplomacy, more than two sides, and several sectors.** `Universe` stays free for the last (ADR-017).
- **Designs the opponent invents.** It fields an authored library (§10); a generator is a design of its own.
- **Audio.** The game needs it and `NeuronClient` has none; it is a design and an ADR of its own.
- **Menus, and saving and loading a match.**

## 3. The player and the match

**The player is a commander (G1).** They never fly a ship. They select ships and structures and give orders, from a camera over the sector's plane (§9); the orbit and chase cameras of SpaceScene §13 stay, for looking at one ship.

**A skirmish (G8)** is one sector built from a seed, as `Sector` is today (ADR-017): two starting positions far apart, asteroid fields near each and richer ones between them, and one or two trader stations on the ground between. Each side starts with a station core, two constructor ships, a small escort and no research. The world runs at 30 ticks a second, as now.

**Victory.** A side loses when its station core is destroyed (G20). The core can be defended, repaired and surrounded by structures, but not rebuilt, so every match has an end.

**Scale (G11).** Dozens of ships a side, up to about fifty, and perhaps fifteen structures. Ships are the size of the two ship assets and between them: from the frigate's 1,181 voxels to the capital ship's 10,747 (SpaceScene §4). A match is meant to last twenty to forty minutes. That is a target for tuning the economy and the tree, not a figure anyone has measured.

**Movement (G7).** Ships move on the sector's main plane, where the stations already lie in a flattened disc (ADR-017), and change between a few altitude bands above and below it. Orders are given on the plane, and the band is a modifier. The number and spacing of the bands are the command design's (§9).

## 4. The loop

```
 asteroid fields ──mine──► refinery ──► credits ──┬──► structures: the station grows (§6)
        ▲                                         ├──► research: modules, materials, size classes (§7.2)
        │                                         ├──► production and refit: library designs, fitted (§5.6)
        │                                         └──► traders: modules not yet researched (§7.3)
        │
        └── fleets hold the fields and the traders' ground ──► the enemy's station core (§3)
```

Ore is the one resource (G10), and credits are what a refinery makes of it. Every choice spends credits: a structure, a research project, a ship, a refit, a module bought. The fields near a start are small and safe; the rich ones lie between the starts, and so do the traders, so expansion and fighting happen in the same place. Ore and credits are working names (§14, question 20).

## 5. Construction

### 5.1 Designs, mounts and fits (G3, G5, G12)

A **design** is a hull and its mounts. The hull is voxels, sculpted one at a time, in one or more parts of at most 256³ each (R14, N2). A **mount** is a place for a module: a named hardpoint (NVF §4.1) whose type says what kind of module it holds (`engine`, `weapon`, `mining` and so on) and whose size class says how large. The box a mount's largest module fills holds no hull voxel. Every design has exactly one mount for a command module.

A **module** is an authored voxel model, an `.nvf` of its own, with a palette of its own (S8): reactor, thruster, weapon, mining laser, cargo hold, constructor bay, sensor, and the station's roles (§6). What it does, its thrust, its draw on the reactors, its damage, is gameplay data in a catalogue and never in its file (N8).

A **fit** is the module at each mount. The hull and its mounts are made in the library, outside the match; the fit is chosen in the match, on the refit screen, from the modules the side has researched or bought (G5). So the slow, creative work of sculpting happens where no clock runs, and the match asks only for Warzone's kind of choice: which module, in which slot, in a few seconds.

A fitted design draws as one placement for each part of its hull and one for each module at its mount (SpaceScene §7.1). A mount is turned by one of the cube's 24 rotations in its part's space, so a design's placements are cube-symmetric to one another, and a structure, which stands upright and quarter-turned, keeps every placement on the aligned splat (§6).

### 5.2 The library

The library is the player's own collection of designs, kept between matches. Each design is an `.nvf` file for its hull and mounts, with a small file of game data beside it for its name, its materials (§5.3) and its default fit. A design is known by the hash of its content, as a model is known by its file's hash today (SpaceScene §6.2), so a library entry cannot be confused with an edited copy of itself.

Until the in-game designer (G-M6), designs are authored in MagicaVoxel and converted by `NvfImport` (NVF §6): the hull as parts, and the mounts as markers named `<part>@<type>.<name>` (NVF §5), which MagicaVoxel turns by exactly the cube's 24 rotations. That keeps N1, since the game never reads `.vox`, and it means the first designs, the player's and the opponent's alike, exist before any editor does. How a mount's name carries its size class is G-M1's to fix. The in-game designer then writes the same files.

### 5.3 Materials and color (G13)

The hull is built from armor materials (G3), which research unlocks (§7.2). Each has a density, a toughness (the damage a voxel of it takes before it goes) and a price per voxel. R14 fixes a voxel's record at 32 bits with a four-bit color, and the owner fixed the palette at sixteen entries. This concept keeps both: each of a design's sixteen palette entries pairs a color with a material, a design has at most sixteen color–material pairs, and a voxel's material is its entry's. The pairing is gameplay data, kept in the design's game data rather than its `.nvf` (N8), so neither the record nor the renderer changes.

Sixteen pairs is room for four materials in four colors each, or one material in every color. The owner chose the pairing over the alternative, the record's four zero bits, 28 to 31, as a material of its own (G13), so N-M1 builds NVF's voxel record as NVF §4 has it. If sixteen pairs prove too tight, those bits remain the way out, but once N-M1 has landed they are a major version of NVF (NVF §4.6) as well as a change to R14.

**The side's color.** One entry of every design's and every module's palette shows the side's color, and the renderer gives each side its own variant of each palette. Placements already name their palette (ADR-014), so a side's color costs a palette per model and side, and nothing per voxel. It also costs the player one of the sixteen entries (G24).

### 5.4 Performance follows from geometry (G18)

Freedom invites the solid brick and the needle. A design's performance therefore comes from its voxels and its fit, so that shape matters:

- **Mass** is the sum of its voxels', by material, and its modules'. **Acceleration** is its thrusters' thrust over its mass, and **turning** follows from its moment of inertia, which a long hull has more of.
- **Power.** Reactors supply it and every other module draws it. A fit that draws more than it supplies is refused.
- **Arcs.** A weapon fires only where its line of fire is clear of its own hull, so a gun buried in armor does not fire, and one on a spine fires almost everywhere.
- **Silhouette.** Shots hit real geometry (§8), so a large hull is a large target.
- **Cost and time.** A design costs its voxels by material, plus its modules, and takes a shipyard a time that grows with both.
- **Size classes.** A design's bounding box sets its class, which decides the shipyards that can build it; research unlocks the larger classes.

These are principles. The numbers are tuned in play, and the test of them is whether designs of different shapes win in different situations (§14, risks).

### 5.5 Validation

The server validates a design whenever one enters a match, since it never trusts a client, and the designer validates it as it is saved. A design is refused, by name as the readers refuse (SpaceScene §6.2), when it has no command-module mount or more than one; when its voxels are not one piece, face to face, around that mount; when a mount's box holds a hull voxel, or its rotation is not one of the cube's 24; when a palette entry names a material the catalogue lacks; or when it is larger than the largest size class. A fit is refused when a module does not match its mount's type and size, or when its modules draw more power than its reactors supply. Whether a side may field a valid design, given its research and what it has bought, is a separate question the match asks (§7).

### 5.6 Production, refit and repair in the match

A shipyard builds one design at a time, paying for it as it goes. The ship appears voxel by voxel: its hull's records are stored in build order, outward from the command module's mount, and its placement draws a growing prefix of them, which a placement's record range already allows (ADR-014). A refit swaps modules at a shipyard, for the difference in price and some time. A shipyard also repairs a ship docked at it, restoring its lost voxels at a price per voxel. A ship fitted with a bought module consumes it when it is built or refitted (§7.3).

## 6. Stations (G9, G17)

A station is a graph of **structures**, each a design like a ship's: a hull with mounts, fitted with the module of its role. A structure lives while its role module works, as a ship lives while its command module does (§8.3). The **station core** comes first, placed by the skirmish; it carries the station's first connectors and the side's command, and its loss ends the match (§3). The roles in the slice:

| Structure | Its role module | What it does |
|---|---|---|
| Station core | Command | Holds the side; its connectors start the station |
| Refinery | Refinery | Turns the ore delivered to it into credits |
| Shipyard | Shipyard | Builds, refits and repairs designs up to its size class (§5.6) |
| Lab | Lab | Runs one research project at a time (§7.2) |
| Reactor | Reactor | Powers the station |
| Turret | Weapon | Defends: the ships' weapon modules, on a structure |
| Sensor array | Sensor | Sees far (§9) |

**Power.** A ship's reactors power its own modules (§5.4); a station's structures share theirs. Every reactor module in a structure connected to the core supplies the whole station, and every structure draws from it (G26).

**Growth.** A constructor ship builds a structure from the library at a free connector of the station, paying as it builds, and the structure appears voxel by voxel as a ship does (§5.6). Connectors are hardpoints of the structures' hulls, with a type of their own. A structure stands upright, turned by quarter turns about the vertical, as S18 has every station stand, so every structure draws with the aligned splat and every voxel center is exact (SpaceScene §7.2).

**Destruction.** Each structure is an entity of its own, standing still (G17): it takes damage, fails and detonates on its own. A destroyed structure takes its connectors with it but not the structures beyond them, which stay where they are, unpowered while no connector joins them to the core.

## 7. Economy, research and trade

### 7.1 Ore and credits (G10, G19)

Ore is the voxels of asteroids. Mining ships, with a mining laser and a cargo hold in their fit, cut voxels out of an asteroid as a shot cuts them out of a hull (§8), carry the ore to a refinery, and the refinery turns it into credits. So the machinery of per-voxel damage serves both, and a field visibly shrinks as it is mined. Fields are finite, which pushes both sides toward the rich ones between them.

Asteroids come from the sector's seed, as the stars come from the sky's (SpaceScene §11.2): a few authored asteroid models, placed and turned by quarter turns, so that they draw with the aligned splat and cost the snapshots nothing. What mining removes reaches the clients as it happens, as damage does (§8.2). Since both sides must build the same asteroids from the seed, the generator is shared code, and it places them at whole coordinates, which no build rounds differently.

### 7.2 Research (G2)

Labs research, one project each at a time, for credits and time. The tree unlocks, in branches:

- **materials,** the armor of §5.3;
- **modules,** each in marks: a thruster, then a better thruster;
- **upgrades** to modules already known, as Warzone's research improves what a side already builds;
- **size classes,** and the shipyards that build them;
- **industry:** a refinery's yield and a shipyard's speed.

Warzone 2100's tree has over four hundred technologies. The slice's has about twenty, enough to make the order of research a choice. A design whose fit needs a module the side has neither researched nor bought cannot be fielded; the library keeps it until it can be.

### 7.3 Traders (G4)

A trader is a neutral station that the sector's seed places on the ground between the starts. It sells modules one step beyond what the side's research has reached, from a fixed catalogue, at a premium, with a stock that runs down as it sells and refills slowly; it is not a simulated market. A module bought joins the side's stock and is consumed when a ship fitted with it is built or refitted. Buying never teaches the side the module. So buying is the shortcut to a few strong ships early, and research the way to field many.

A trader deals with a side only while that side has a ship within its trade radius and the other has none, which makes its ground worth holding. In the slice traders cannot be attacked (G25).

## 8. Combat and damage

### 8.1 Weapons

The slice has two: a mass driver, whose shells fly at a finite speed and can miss a ship that moves, and a laser, a beam that hits at once, at short range, for as long as it fires. Each is a module at a weapon mount, on a ship or on a turret, and fires within its arc (§5.4). Ships choose targets in range for themselves; the player's orders choose for them (§9).

### 8.2 Shots are events (G15)

A shot is a world event, as a detonation is (SpaceScene §5.5): the weapon, the tick it fired at, its origin, its direction and its speed. A shell's flight is a pure function of those and the time, so every client draws the same shell, a client that joins late included, and nothing about it is sent twice. The server alone decides what it hits. It sweeps each shell, tick by tick, through the grids of the designs in its way, with the walk `NeuronCore` already has (`GridWalk`), and sends what it removed as a damage event: the entity, the tick and the voxels gone. A beam is resolved by the server every tick it fires, and sent as the same events.

### 8.3 Damage (G6, G14)

Every voxel has hit points from its material (§5.3). A hit damages the voxels at and around where it lands, by the weapon's damage and reach, and removes those it exhausts. The server keeps each entity's damage per voxel; a client keeps only which voxels are gone, one bit each: 1,344 bytes for a ship the size of the capital ship, by arithmetic.

- **Modules.** A module's voxels are hit as the hull's are. Its output falls as they go, and below a share of them it fails.
- **Cut off.** After each hit the server finds what is still connected, face to face, to the command module. What is not becomes a **wreck**: an entity of its own, with the voxels it took, drifting on with the velocity it had, as SpaceScene §5.5 has a chunk that breaks off. A ship cut in two is thus a ship and a wreck (G6).
- **Lost.** A ship whose command module fails is lost, and detonates: its remaining voxels become debris, as a detonation's do today.
- **Wrecks and debris** are scenery. Ships fly through them as they fly through debris now, and they last as long as the world's debris lifetime allows (S19).

A structure takes damage the same way, with its role module as its command (§6).

### 8.4 What the numbers must show

Per-voxel damage at fifty ships a side is the concept's largest cost, in three places at once: the server's sweeps and connectivity searches, the damage events on the wire, and the renderer's masks, with the levers of SpaceScene §7.6 held on damaged placements (§11.1). G-M3 measures all three on a battle before anything is optimized.

## 9. Command and control

**The camera** looks down on the sector's plane: it pans, zooms, turns and tilts. Each ship stands on a line dropped to the plane, so its band reads at a glance. The orbit and chase cameras of SpaceScene §13 stay, for looking at one ship.

**Selection** is by click and by box. A click is exact: the visibility buffer's voxel id names the placement (SpaceScene §7.3), the placement names the entity and the module, and a readback of that one pixel delivers it within a couple of frames. A box selects the entities whose centers it holds. Selections go into control groups.

**Orders:** move, on the plane and into a band; attack; attack-move; stop; hold; guard; mine; build a structure; refit and repair at a shipyard. A group moves in formation, which ADR-017's slots already fly.

**Paths.** The plane is open but not empty: stations, structures and asteroid fields are obstacles, each within its keep-out sphere, as the sector's routes already avoid them (ADR-017). Ships find paths around the keep-outs, steer along them with ADR-017's flight model (pure pursuit, bank and slot), and keep apart from one another by steering, not by collision. They no longer pass through one another, but they still pass through wrecks and debris.

**Fog of war.** Each side sees what its ships' and structures' sensors reach, and its snapshots hold only that (G21). The server already decides what reaches each client, so a side cannot see past its sensors however its client is changed. This is the one place the concept asks the snapshots to differ by session, which ADR-015 forecloses today (§11.1).

**The HUD:** the side's credits, the selection, the queues of production and research, the minimap, and the refit screen. The canvas draws text and quads (ADR-010); the HUD needs a layer of panels, lists and buttons over it, and D9's "no third-party code" means that layer is the tree's own.

## 10. The opponent (G16)

The opponent plays the same game through the same door: a session over a loopback, as the client has. It receives its side's snapshots, fog included, and sends commands; it never touches the world's objects. So it cannot cheat by accident, every rule the player meets it meets, and it exercises the protocol as a second client would. It runs in the server's process, on a thread of its own, or stepped with the server as the bench steps it (ADR-018), which keeps a skirmish repeatable from its seed within one build.

It fields an authored library, designs made as the player's are (§5.2), and researches and trades by the same rules to reach them. Its play in the slice is one level: expand to the fields, keep its economy running, research toward its library, defend, and attack in waves once its fleet is strong enough. A stronger opponent is a design of its own.

It is game code that reads snapshots and writes commands, so it belongs neither in `GameLogic`, which holds the world, nor in `GameLib`, which draws it. A project of its own, referencing only `NeuronCore` and the shared game library (§11.1), makes that boundary one the compiler keeps. Like every new project, it is an ADR (`AGENTS.md` §2). What the compiler cannot keep, `AGENTS.md` R19 does (G27).

## 11. What the engine must change

### 11.1 The changes

| Change | Why | What it amends |
|---|---|---|
| Designs on the wire: a design, its game data and its fit, sent once and known by its content hash; an entity names its design, its side and its state; protocol version 2 | G3, G5 | S13, and ADR-015's messages |
| Designs from the client: the server receives a side's designs, validates them (§5.5) and gives them ids | G5 | ADR-015's commands |
| Events for shots, damage and mining; wrecks as entities | G6, G15, G19 | SpaceScene §5.5, ADR-015 |
| Snapshots that differ by side, for fog of war | G21 | ADR-015, which sends the same bytes to every session |
| A shared game library: the module catalogue, the materials, the designs' game data and the asteroids' generator, which both sides read | G3, G19 | ADR-003, which creates it when the first such type appears |
| A record buffer that grows as designs arrive, instead of one static buffer | G3 | SpaceScene §7.1 |
| A mask per placement, one bit per voxel, which the splat skips | G6 | SpaceScene §5.5, ADR-014 |
| The levers kept on damaged placements: the reachable voxels updated as voxels go, and the coarse model drawn for a damaged placement far away | G6, G11 | SpaceScene §7.6, S-M9 |
| Many more draws: `ExecuteIndirect`, once the bench says the draw calls cost (§11.2) | G11 | SpaceScene §7.4; D1 does not forbid it |
| Picking through the visibility buffer | G1 | — |
| Paths, separation, orders and the strategic camera | G1, G7 | ADR-017's flight, SpaceScene §13 |
| The opponent's project | G16 | ADR-003 |
| A layer of panels, lists and buttons on the canvas | G1 | ADR-010 |

### 11.2 The load, by arithmetic

These figures are arithmetic on assumed counts, not measurements: fifty ships a side, each a hull of one part and eight modules; fifteen structures a side, each a hull and two modules; ships of 3,000 voxels and structures of 20,000 on average.

- **Placements:** 2 × (50 × 9 + 15 × 3) = 990, where the default space scene has 52 (SpaceScene §5.2). Seen by the camera and S-M7's three cascades, that is up to 3,960 draws a frame, one `DrawIndexedInstanced` each as `SplatPass` issues them today.
- **Voxels:** 2 × (50 × 3,000 + 15 × 20,000) = 900,000, within the default space scene's 1,033,408.
- **Snapshots:** 130 ships and structures at today's 48 bytes each are 6,240 bytes a tick, and 187,200 bytes a second at 30 ticks. Over a loopback that is nothing; over a network it wants the delta encoding the UDP design adds.
- **Designs** cross once each: 42,988 bytes for one the size of the capital ship, at R14's four bytes a voxel.

So the load moves from voxels to placements and events. The renderer was built for the first; the second is what G-M3's bench must measure.

## 12. The next phase

### 12.1 What happens to the plans that run

**`SpaceScene.md`.** S-M4 is merged and waits for the owner's check (SpaceScene §16). S-M5, S-M6 and S-M7 (the sky, bloom, temporal anti-aliasing and the cascades) are the look, and the slice plays without them, so they wait until after it (G22). S-M8's bench and S-M9's levers are what a strategic camera needs most, since a view of a sector puts most of its voxels under a pixel (SpaceScene §17). So they move into G-M3, revised: the bench measures a battle, and the levers hold on damaged placements (§11.1).

**`NeuronVoxelFormat.md`.** Designs and modules are `.nvf` files, so N-M1, the format, and N-M2, the importer and the converted assets, open G-M1, with the follow-up that has `Outpost.exe` load `.nvf` instead of `.vox` (NVF §10). N-M3 and N-M4, the Python twin and the Blender extension, wait: mounts on the grid are what MagicaVoxel's markers already express (§5.2).

**Question 25** of SpaceScene §17, how N and B find the flights' leaders, is answered by the concept rather than by a flag: selection replaces the cycling, and an entity's side crosses the wire in protocol version 2 (§11.1).

### 12.2 Milestones

Each milestone gets a technical design before its code, as `AGENTS.md` asks, and its ADRs land with its commits. The next free number is ADR-021: the owner reserved ADR-019 and ADR-020 for NVF's N-M1 and N-M2 (NeuronVoxelFormat.md §10).

| | Delivers | Done when |
|---|---|---|
| G-M0 | This concept accepted; the running plans re-planned as §12.1 says | Done on 2026-09-28: the owner accepted it, with §14's questions answered and the setting deferred, and `SpaceScene.md`, `NeuronVoxelFormat.md` and `AGENTS.md` record it (§13) |
| G-M1 | **Designs as data.** N-M1, N-M2 and loading `.nvf`; designs, mounts, fits, materials and the module catalogue in the shared game library; protocol version 2, with designs on the wire; the growing record buffer; the sector's ships rebuilt as fitted designs authored in MagicaVoxel | The sector flies and draws fitted designs; each design crosses the loopback once; the design and protocol tests green |
| G-M2 | **Command.** The strategic camera, picking and selection, orders, paths on the plane and its bands, separation and formations; the HUD's first panels | The owner commands fleets around the sector |
| G-M3 | **Combat.** Weapons, shots and damage as events, masks, module failure, the command module's loss, wrecks and repair; the bench revised to a battle (S-M8), and the levers held on damaged placements (S-M9) | Two fleets fight to the end; in the tests, the client's missing voxels are the server's, bit for bit; the battle's performance note is committed |
| G-M4 | **Economy and stations.** Asteroids, mining, refineries and credits; the station core, and structures built at connectors; production, refit and power; the research tree; traders | One side runs the loop from ore to a fitted design fielded in battle |
| G-M5 | **The skirmish.** The opponent through its session, fog of war, victory, and the skirmish's layout from a seed | The owner plays a skirmish against the opponent to its end |
| G-M6 | **The designer.** Sculpting a hull voxel by voxel in the game; mounts, materials and colors; validation; the library | The owner builds a design in the game and fields it in a skirmish |

G-M6 comes last so that its rules, the size classes, the materials and what makes a design strong, are the ones the skirmish has tested; until then designs are authored in MagicaVoxel (§5.2). The owner chose that order (G23), and §14 says what it risks.

## 13. What changes in the other documents

The owner accepted this concept on 2026-09-28. G-M0 recorded it where it changes a running plan, and the rest follows with the milestones that implement it:

- **`SpaceScene.md`:** its status line and §16 record §12.1's plan (G22), and §17 records question 25 as answered. S13 is superseded in part by designs on the wire, and §7.1's static buffer and §7.6's rule for damaged placements are amended, by the ADRs of G-M1 and G-M3.
- **`NeuronVoxelFormat.md`:** its status line and §10 record §12.1's plan. Mounts and connectors are hardpoint types, and what they mean stays out of the file (N8), so the format does not change (G13).
- **ADR-015** is amended by protocol version 2 and by snapshots that differ by side (G21). **ADR-003** is amended by the shared game library and the opponent's project, each with an ADR of its own.
- **`AGENTS.md`:** the paragraph that names the designs names this one, and §2's table gains the new projects as their ADRs land. R19 makes G16 a rule beside R18, that the opponent plays through a session and nowhere else (G27), and R14's paragraph counts this document among the designs a rule may come from.

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
15. **S-M5, S-M6 and S-M7 wait until after the slice, and S-M8 and S-M9 move into G-M3 (G22, §12.1).**
16. **The designer comes last in the phase (G23, §12.2),** with designs authored in MagicaVoxel until then.
17. **Materials pair with palette entries (G13, §5.3),** sixteen pairs a design, and the record's spare bits stay zero.
18. **The side's color takes one of a design's sixteen palette entries (G24, §5.3).**
19. **Traders cannot be attacked in the slice (G25, §7.3),** and deal with a side only while it alone holds their radius.
20. **The setting waits for G-M4 (§4).** Ore, credits, station core, constructor and the modules' names stay working names until G-M4's HUD needs the setting's own; "Outpost" suggests a frontier.
21. **A station's structures share power through their connectors (G26, §6),** and a structure cut off from the core is unpowered.
22. **G16 is a conformance rule, `AGENTS.md` R19 (G27, §13).**

**Risks:**

- **Sculpting inside a strategy game.** The library keeps the clock away from the sculpting (G5), which works only if a refit is enough to answer what happens in a match. If players want to reshape hulls mid-match, the split fails, and G-M5 and G-M6 are where that shows.
- **The designer lands last** (G23). What sets the game apart is the last thing the phase builds, so if the phase runs long, it is the feature at risk. MagicaVoxel keeps designs coming until then, but sculpting in MagicaVoxel is not the game's construction.
- **The brick and the needle.** §5.4's principles stay principles until tuned, and balance is iteration, not design. The test is whether authored designs of different shapes win in different situations, which G-M3 and G-M4 can run before G-M6 opens design to the player.
- **Per-voxel damage at scale** is the largest technical cost (§8.4). If G-M3's bench finds it too dear at fifty a side, the fallbacks are fewer, larger hits, or damage in coarser cells; module hit points are not one, since G6 rules them out.
- **Placements, not voxels** (§11.2): nineteen times the placements, and up to 3,960 draws a frame. `ExecuteIndirect` is the expected answer, and the bench decides.
- **An opponent is a project.** One that plays convincingly is a large piece of work; the slice's plays at one level, and a weak opponent makes the skirmish a test bed rather than a game.
- **The UI is the tree's own.** D9 rules out third-party code, so the HUD, the refit screen and the designer are built on the canvas, from the panels up. A toolkit would need the owner to revise D9.
- **Determinism holds within one build** (SpaceScene §17). That is enough for a skirmish in one process and for a repeatable opponent. Multiplayer across x64 and ARM64 stays authoritative, as the engine already is, rather than lockstep.
- **The phase is long:** six milestones before a skirmish is whole. The slice keeps its content small, about twenty research items, a dozen modules and two weapons, so that the whole loop fits.

## 15. References

- Warzone 2100, Pumpkin Studios, 1999; open source since 2004: https://wz2100.net
- [`SpaceScene.md`](SpaceScene.md): the world, the boundary and the renderer this concept builds on.
- [`NeuronVoxelFormat.md`](NeuronVoxelFormat.md): parts, hardpoints and the importer that designs and modules use.
- [`Archive/SampleRenderer.md`](Archive/SampleRenderer.md): D1, D5, D9 and D13, which constrain the draws, the palette, the UI and the canvas.
