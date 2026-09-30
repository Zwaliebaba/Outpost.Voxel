# Outpost.Voxel — MVP Plan

**Status:** approved by the owner, 2026-09-29, with the scope the owner chose that day (§2.3); its phases run one pull request at a time, each approved at a checkpoint before the next begins (§3.5) · **Date:** 2026-09-29
**Builds on:** [`GameConcept.md`](GameConcept.md), whose G74 puts this MVP first; [`../AGENTS.md`](../AGENTS.md), which every phase follows; and the code as it stands on `main` · **Executed by:** an agent, Claude Code on the owner's Windows PC (§3.1)

This plan says how the next phase of work reaches a playable game: a skirmish of one human against the computer opponent, under fog of war, with limited research and ships. It is the technical design of its phases, as `AGENTS.md` asks of a milestone; each phase adds the ADRs its decisions need, in the commits that implement them. It changes no rule of `AGENTS.md` and no decision of the concept. Where the MVP builds less than a decision describes, §2.2 says so, and the decision stands for the milestones that follow the MVP (§7).

## 1. Purpose

The concept plans six milestones, G-M1 to G-M6, and the first match a human plays against an opponent that plays the whole loop comes in G-M5, the fifth of them; the panel review called that fun validated late (its P5). This plan builds a thin slice through G-M1 to G-M5 first: every layer of the game at the smallest breadth that still makes a match, so that the owner plays the game before its breadth is built, and the plan can change course while change is cheap.

The MVP is a subset of the concept, not a substitute for it. Each mechanism it builds is the concept's own mechanism at a smaller breadth: per-voxel damage with two weapons, not hit points; fog enforced by the server, not hidden by the client; an opponent that plays through a session, not one that reads the world. What it leaves out waits, whole, for the milestones after it (§7).

## 2. The MVP

### 2.1 The match

A skirmish starts from the command line: `Outpost.exe --skirmish --seed 7`. Side A is the human, side B the computer opponent; both play by the same rules, with the same library and the same core.

- **The sector** is small and symmetric under a half turn about its center (G66): two cores about 3,600 units apart, two small asteroid fields near each core, and two larger ones in the middle, out of both cores' sight.
- **The station** is the core alone: a skirmish-owned design, the same for both sides, which carries the shipyard, the lab, the refinery, a sensor array and four turrets (G48). Nothing is built onto it.
- **Ships** come from a library of four designs: a miner, two frigate-class gunships (one with mass drivers, one with lasers), and a capital-class cruiser. Each has one fit. A side starts with its core, two miners and two mass-driver gunships.
- **The economy** is mining: miners cut ore voxels out of asteroids with a mining laser, as a shot cuts a hull (G19), carry it home, and the core's refinery turns it into credits. One grade.
- **Production** at the core builds one ship at a time, for credits, within a budget of 20 command points a side (G56).
- **Research** at the core runs one of six projects at a time: two unlock designs, four toughen or upgrade what a side already fields, at once (G55).
- **Combat** is per voxel (G6). A weapon aims at its target's silhouette (G57), a shell leads (G58), a ship keeps its target (G71), a side's own hulls stop its fire (G39), and a tick's shots resolve at once (G72). A ship is lost when its command module fails and detonates when a reactor does.
- **Fog of war** is the server's (G21, G30): each side receives only what its sensors reach, and a shot only from where it enters them (G54). A minimap shows it.
- **The end** is the enemy core's destruction (G20). From minute 60 both cores wear down (G36), and cores that fail on one tick draw.
- **Difficulty** is a visible handicap: each side's starting bank and income multiplier, set on the command line and shown to both sides (G67).

### 2.2 What the MVP leaves out, and the subsets it builds

| Concept | In the MVP | After the MVP |
|---|---|---|
| G5, G12, G35: the player's library, variants and refit | Four designs in `GameData`, the same for both sides, one fit each | The library, variants, the refit screen and the module stock |
| G6, G14, G51, G63: wrecks and salvage | Every loss and every piece cut off detonates into debris | Wrecks, and salvage at half the hull's price |
| G7: altitude bands | One plane | Three bands |
| G9, G26, G46, G48, G65: structures, connectors and outposts | The core, which carries every role | Constructors, structures at connectors, outposts |
| G61: clearance by voxels | Spheres around the cores and asteroids, which no player shapes | Voxel clearances, once players build structures |
| G4, G25: traders | None | Traders |
| G10, G42, G52: grades and refinery ships | One grade; the core's refinery only | Grades, refinery ships |
| G29: the profile | Mass, inertia, thrust, turn rate, speed cap, sensor range, clear lines, silhouette, price, build time, command points | The rest: the armor over vital modules, arcs for the design card |
| G43: the build order | A ship appears whole when its build completes | Ships built voxel by voxel |
| The concept's §5.6: repair | None: a damaged ship stays damaged, and a retreat (G59) saves it for a later fight | Repair at the shipyard, and "retreat for repair" as a standing order |
| G49, G59: the condition readout | Bars for the weakest vital module and the hull | Status markers, the inspect view, the reason a weapon is silent |
| G62: squadrons by bearing | A group moves keeping its members' offsets | Formations by bearing, the "break and bear" stance |
| G64: the research tree | Six items, one lab | About twenty items, three labs |
| G69: the runner and the end screen | A headless match harness in the tests; an end screen with each design's figures | The two-library runner as a tool; the library challenge |
| G11, G32, G44, gates 1 to 10 | About 20 ships a side; the MVP's own gate (§6) | Fifty a side, and the concept's gates |
| G45, G-M6: the in-game designer | Designs written by a generator script (§5, phase 1) | The designer |
| Audio, menus, saves (§2 of the concept) | None; the command line starts a match | As the concept decides |

### 2.3 The owner's scope decisions

The owner chose the MVP's scope on 2026-09-29, from these options:

| Question | Chosen | Also offered |
|---|---|---|
| Where the agent runs | The owner's Windows PC: it builds, tests, runs the game and captures frames | Cloud sessions, with CI as the only build; a mix of the two |
| The economy | Mining, minimal | Income nodes that pay whoever holds them; passive income only |
| The station | The core does it all | A fixed layout of separate structures; constructors building at connectors |
| Who authors the designs | Generated by the agent's script | The owner, in MagicaVoxel; a mix |
| When the owner steers | At the end of every phase | At milestones of several phases; only at the MVP gate |
| The battle's size | About 20 ships a side | Fifty a side from the start; about 8 a side |
| Planes and fire | One plane, own hulls block fire | Three bands; one plane with shots passing friends |
| Extras | A minimap with fog; difficulty handicaps | Wrecks and salvage; variants and refit |

## 3. How an agent runs a phase

### 3.1 The environment

The agent is Claude Code on the owner's Windows PC, with Visual Studio 2026's C++ workload (toolset v145), its ARM64 or x64 build tools as the machine needs, Python 3.11 or later, and the repository checked out. It builds and tests as `AGENTS.md` §3 says, always through the solution:

```powershell
msbuild Outpost.Voxel.slnx /t:Restore /p:RestorePackagesConfig=true /nologo /v:minimal
msbuild Outpost.Voxel.slnx /p:Configuration=Debug /p:Platform=x64 /m /v:minimal /nologo
msbuild Outpost.Voxel.slnx /p:Configuration=Release /p:Platform=ARM64 /m /v:minimal /nologo   # on the reference machine
python Build\CheckFormat.py
python Build\CheckProjectFiles.py
python Build\RunClangTidy.py   # in a Developer PowerShell
```

Debug|x64 is what CI builds. The owner's reference machine is ARM64 with an Adreno X1-85, where G32's budgets are measured, so a phase that changes what is drawn or simulated is also built in Release on the machine's own platform and run there. Tests run through `vstest.console.exe` over every suite the build produced, and the Python suites with `python -m unittest discover` over their folders. CI takes 10 to 13 minutes a run; the agent does not wait on it to find its own failures.

**Preflight,** once, before phase 1: build Debug|x64 and the native Release, run every suite, launch `Outpost.exe` and capture a frame (§3.3), and report the machine, the toolchain and the timings. A failure here stops the plan until the owner has fixed the environment.

### 3.2 Starting a phase

1. Branch from an up-to-date `main`: `mvp/p<n>-<slug>`, for example `mvp/p1-designs`.
2. Read `AGENTS.md` whole, this plan's §3 and its phase, the concept's sections and decisions the phase cites, the ADRs it amends, and the checkpoint log's notes on the phase before (§10).
3. If the owner's notes at the last checkpoint change this phase, change the plan first, in the phase's first commit, and say so in the pull request.

The owner starts a phase with a message such as: *"Execute phase 3 of Design/MvpPlan.md. Follow AGENTS.md and the plan's §3. Stop at the phase's checkpoint."*

### 3.3 Doing the work

- **Decisions before code.** A phase writes its ADRs first, numbered from the next free number, ADR-026 for phase 1's first, since ADR-024 and ADR-025 went to the fragmented detonation and its light, and each ADR states its context, its decision and what it forecloses, as `AGENTS.md` §6 asks. Figures in an ADR are measured, and say how.
- **Small commits,** each building and passing its tests, with imperative subjects.
- **Tests with the code.** Game rules are tested headless, in the suites of the libraries that hold them, so that CI checks them without a GPU; what only the GPU runs has a C++ twin and a WARP test (R15, R16). Every seeded test is deterministic (R21); a flaky test is a defect.
- **Frames as evidence.** From phase 2, `Outpost.exe --capture <file>.png --capture-at <seconds>` writes one frame through WIC, which the Windows SDK provides, and exits; until then the agent captures the window with PowerShell. Every phase that changes the picture attaches captures to its pull request.
- **The checklist.** `AGENTS.md` §7 holds for every phase, in full.

### 3.4 Ending a phase

1. Run the three checkers, the Debug|x64 build and every suite, and for a phase that changes what is drawn or simulated, the native Release build and a run of the game against the phase's checkpoint list, with captures.
2. Record the phase's figures: test counts, the frame and tick times from the game's figures overlay (F1) at the phase's heaviest scene, and anything its done-when measures.
3. Open one pull request for the phase, whose description reports:
   - what the phase built, by task, and the concept's decisions it implements, with the subsets of §2.2 it relies on;
   - what was verified, with the commands, their results and the captures, and what was not;
   - every deviation from this plan or from `AGENTS.md`, and why;
   - the ADRs it adds;
   - the questions it could not answer, and what it proposes for the next phase.
4. Stop. The next phase starts only after the owner's checkpoint.

### 3.5 Checkpoints and changes of course

At every phase's end the owner reviews the pull request and, when the phase changed what can be seen, plays the build against the phase's checkpoint list. The owner then answers one of three ways:

- **Approve:** merge, and the next phase starts as planned.
- **Adjust:** merge, with notes that change the next phases; the next phase writes them into this plan first (§3.2).
- **Re-plan:** stop. The owner and the agent revise this plan, and the concept where a decision changes, before any more code.

The phases after the next are provisional. Four of them end at a decision point (§5), where the owner judges whether the approach holds before more is built on it. The agent records each checkpoint's outcome in §10 in the next phase's first commit.

### 3.6 When the agent stops and asks

The agent stops, reports and waits when:

- a design question arises that neither the concept nor this plan answers, and the answer changes what a player sees or does;
- the work would break a rule of `AGENTS.md`: a build setting, a silenced warning, third-party code (D9), a project this plan does not name, or a reference that crosses the layers;
- the phase needs to change a decision of the concept;
- a done-when still fails after two honest attempts to meet it;
- a test cannot be made deterministic;
- the frame exceeds 16.7 ms, or the tick 8 ms, at the MVP's scale in Release on the reference machine;
- the phase grows beyond its task list. Work found on the way is noted in the pull request for a later phase, not done.

## 4. The shape of the MVP's code

| Project | Kind | New | What the MVP adds to it |
|---|---|---|---|
| `NeuronCore` | static library | — | The protocol's envelope for version 2: sides as integers, entities as composite models, masks, events and an opaque game payload; the stepped-client interface; twins of new GPU algorithms |
| `GameCore` | static library | Phase 1 | The shared game library of ADR-003: the catalogue, material and size classes, designs and their validation, profiles, the game's messages and their encoding, the asteroid and layout generators |
| `NeuronServer` | static library | — | Sessions bound to sides, snapshots per session, commands checked per side, the command log, stepped clients |
| `GameLogic` | static library | — | `Skirmish`, the MVP's world, beside `Sector`: layout, orders and flight, combat, mining, production, research, fog, victory |
| `NeuronClient` | static library | — | Masks in the splat, the sloped quad and the UI toolkit on the canvas, the cursor in `InputState`, frame capture |
| `GameLib` | static library | — | The skirmish client: strategic camera, selection, orders, overlay, HUD, minimap, end screen |
| `Opponent` | static library | Phase 8 | The computer opponent, which references `NeuronCore` and `GameCore` alone (R19) |
| `Outpost` | application | — | `--skirmish` and its options; wires the host, the player's session and the opponent's |
| `GameCoreTests`, `OpponentTests` | test DLLs | Phases 1 and 8 | The shared library's suite; the opponent's, with the headless match harness |

**Engine below, game above** (R9). `NeuronCore`, `NeuronServer` and `NeuronClient` learn integer sides, composite models, masks and events, and never a design, an order or a credit. `GameCore` gives those meaning, and both sides of the boundary read it. Game messages travel inside the engine's envelope as a payload that `GameCore` encodes and decodes on both ends, so the engine's protocol changes rarely and the game's as often as it must. Phase 2's ADR fixes the mechanism.

**Determinism** (R21). `Skirmish` and `Opponent` read no clock but the tick and draw randomness only from `PcgHash` of the seed. A skirmish is its seed, its build, its library and its command log (G41), and the tests replay one.

**Fog lives on the server** (G30). The client draws what its session receives and nothing else; an observer session, which receives everything, exists for the tests and for development, and is never the opponent's (R19).

## 5. The phases

| Phase | Delivers | What the owner sees at the checkpoint | Decision point |
|---|---|---|---|
| 1 | Designs as data | The generated designs in MagicaVoxel; their profiles; the space scene unchanged from `.nvf` | Are generated designs good enough for the MVP? |
| 2 | The skirmish on screen | Two cores, the fields and each side's ships, fitted and in their colors | — |
| 3 | Fog of war | Only what the player's sensors reach; enemy structures as last seen | — |
| 4 | Command | The strategic camera, selection and moving groups under fog | — |
| 5 | Combat | Fights: voxels shot away, ships lost two ways, the condition bars | Is per-voxel combat readable and fun at this scale? |
| 6 | Economy and production | Mining, credits, production within the command budget | Does the economy pace the match? |
| 7 | Research | Six projects, designs unlocked, fleets upgraded at once | — |
| 8 | The opponent | A computer opponent that plays the whole loop | Does it play well enough to judge the game? |
| 9 | The match, whole | Victory, defeat and the clock; the minimap; the end screen | The MVP gate (§6) |

### Phase 1 — Designs as data

**Goal.** The MVP's hulls, modules and asteroids exist as `.nvf` files the game reads, and a shared game library turns them into validated designs with profiles.

**Concept.** G3, G12, G13, G18, G29 (§2.2's subset), G37, G38, G40; NVF §5 and §10's follow-up; R14, R20.

**Tasks.**
1. **ADR: `GameCore`.** The shared game library ADR-003 creates "when the first such type appears": a static library referencing `NeuronCore` alone, namespace `GameCore`, with its suite `GameCoreTests`. It updates `AGENTS.md` §2's table, `.clang-tidy`'s `HeaderFilterRegex` and the solution in the same commit. `GameCore` is this plan's name for it; the owner confirms it at the checkpoint, when a rename is still cheap.
2. **ADR: the design generator.** `Tools/DesignGenerator/`, Python beside the Blender extension, which ADR-023 amended records as a tool with no C++. It writes the MVP's `.vox` files into `GameData` deterministically, and its unittest suite joins CI's Linux job. The ADR also fixes how a mount's marker name carries its size class, within NVF §5's names, which the concept left to G-M1 (its §5.2).
3. **The generator** writes:
   - modules, as small models, one per kind and size: command module, reactor, thruster, mass driver, laser, mining laser, cargo hold, sensor, and the core's shipyard, lab, refinery and sensor array;
   - hulls with mount markers, each a single part: `Miner`, `Gunship`, `Lancer`, `Cruiser` and `StationCore`, within their size classes (§8.1);
   - three asteroids of distinct shapes.

   Each palette's sixteen entries pair colors with the light and heavy classes (G13), and one entry is the side's color (G24).
4. **Import.** `NvfImport` makes each `.nvf` from its `.vox`, and both are committed, so CI's model check holds them together. `Outpost`'s build copies the new files into `GameData` beside the executable.
5. **`GameCore`'s content.**
   - The catalogue as C++ tables: material classes with density, toughness and price (G13); module kinds with size, mass, power, and their stats; size classes with box, voxel budget, speed cap and command points (G40, G56).
   - The four designs and the core: each names its hull, the material class of each palette entry, and its one fit.
   - `Validate`, which refuses by name as §5.5 of the concept lists: no command mount, or two; voxels not in one piece; a mount's box holding a hull voxel, overlapping another's, or touching no hull voxel (G37); a module that does not match its mount; power drawn beyond supply; no size class that fits.
   - `Profile`, over §2.2's subset: mass, center of mass and inertia; thrust along each axis from thrusters whose +Z lines clear their own hull (G38); acceleration, turn rate and the class's speed cap; sensor range; each weapon mount's clear line; price, build time and command points. The silhouette joins it in phase 5.
6. **`Outpost.exe` reads `.nvf`.** `ClientSession` and `Sector` load `<name>.nvf` where they loaded `<name>.vox`, as NVF §10's follow-up and the concept's G-M1 ask. That is an ADR of its own, amending ADR-018's welcome and SampleRenderer §7. The space scene must draw the same frames it drew from `.vox`.
7. **Tests.**
   - Every MVP design validates, and one planted defect for each refusal is refused by name.
   - Each design's profile is pinned; the test prints the table the pull request quotes.
   - The generator's output is pinned by hash in its own suite.
   - The existing suites stay green on `.nvf`.

**Done when.** CI is green, the model check included; the profile table stands in the pull request and lands near §8.1's targets; the space scene runs unchanged from `.nvf`.

**Checkpoint.** Open `Gunship.vox`, `Cruiser.vox` and `StationCore.vox` in MagicaVoxel: shapes, mount markers and palettes. Read the profile table. Confirm the working names and `GameCore`. Launch `Outpost.exe` with no arguments: the space scene as before.

**Decision point.** Are generated designs good enough to judge the MVP? Replacing them later needs no change to the plan: a design authored in MagicaVoxel with the same mounts takes a generated one's place.

### Phase 2 — The skirmish on screen

**Goal.** `Outpost.exe --skirmish --seed <n>` starts the MVP's symmetric sector, with the two cores, the asteroid fields and each side's starting ships, drawn as fitted designs in their side's colors. There is no fog yet: every session sees everything.

**Concept.** G11, G20, G24, G48, G66; §3; ADR-015, whose protocol version 2 this phase begins; ADR-017's layout.

**Tasks.**
1. **ADR: protocol version 2, designs and sides.**
   - The welcome lists the composite models an entity may be, each a hull model and a module model at each mount, and the sides.
   - An entity record names its composite model and its side; asteroids are composites of one part.
   - The engine's envelope carries a game payload that `GameCore` encodes (§4).
   - It amends ADR-015's messages; ADR-015's rule of one snapshot for every session stands until phase 3.
2. **`GameLogic::Skirmish`,** a world beside `Sector`, built from its seed:
   - the half-turn layout of §8.4, from a generator in `GameCore` that both sides could run (the concept's §7.1);
   - the two cores, and six fields of asteroids at whole coordinates, turned by quarter turns;
   - each side's starting ships, holding station near their core.
3. **Placing a composite.** The client places an entity as its hull's part and each fitted module at its mount, turned by one of the cube's 24 rotations. So a core, which stands aligned, stays on the aligned splat (the concept's §5.1).
4. **The side's color.** Each side draws with its own variant of each palette (G24; ADR-014's palettes).
5. **The command line:** `--skirmish`, with `--seed`; the space scene stays the default until the MVP gate. `--capture` and `--capture-at` (§3.3).
6. **Tests.**
   - The layout is its own half turn, with sides exchanged, on 20 seeds.
   - One seed gives the same welcome and snapshot bytes twice.
   - The client places a composite's modules at its mounts, on the CPU.
   - Protocol version 2's encoding round-trips, and its refusals are refused by name.

**Done when.** CI is green; captures from two seeds stand in the pull request; the scene holds 60 frames a second in Release on the reference machine.

**Checkpoint.** Launch `--skirmish --seed 1` and `--seed 2`. Check each:
- the cores and fields where §8.4 puts them, and the layout symmetric;
- the ships fitted, their modules at their mounts;
- two distinct side colors.

### Phase 3 — Fog of war

**Goal.** Each side receives only what its sensors reach, and the player's client draws only that, remembering enemy structures as last seen. The server refuses a command that names another side's entity, and a logged skirmish replays.

**Concept.** G21, G30, G34, G41, G53; the concept's §9 table of who receives what; ADR-015, whose foreclosure of per-session snapshots this lifts; R19, R21.

**Tasks.**
1. **ADR: sides, sessions and fog.**
   - A session is bound to a side when the host adds it, or is an observer.
   - Snapshots are described and encoded per session.
   - An entity is visible to a side while within the sensor range of any of that side's entities, from their profiles.
   - An entity entering a side's sight arrives whole, as a baseline (G30).
   - A command the world refuses for its side is counted, not fatal to the session (G34).
   - The command log's format, and the replay.
2. **`NeuronServer`.** `AddSession` takes the session's side; the world describes itself per side, and judges each command for the side that sent it.
3. **`Skirmish`** computes each side's visibility every tick, by brute force at the MVP's counts.
4. **The client** draws what its snapshots hold. It keeps each enemy structure it has seen as last seen, drawn dimmed, until it is seen again. `--observe` gives the window an observer session, for development only.
5. **The command log.** `--log <file>` writes every command the server applies, with its world tick and its session. `--replay <file>` runs the logged skirmish headless and reports whether its snapshots match.
6. **Tests.**
   - On 20 seeds and every tick, no side receives an entity outside its sensors.
   - A command naming the other side's entity is refused and changes nothing.
   - Two runs of one seed send every session the same bytes.
   - A logged run replays to the same bytes.

**Done when.** CI is green; the tests above pass; captures show one seed with and without `--observe`.

**Checkpoint.** In `--skirmish`, the enemy core and ships are hidden at the start, the near fields are visible, and the middle is not. `--observe` shows everything.

### Phase 4 — Command

**Goal.** The player commands ships from a strategic camera: selects them, moves them, groups them, and sees their orders, through the first panels of the HUD.

**Concept.** G1, G7 (one plane in the MVP), G47's closing (for moves), G62 (§2.2's subset); the concept's §9 on the camera, selection, orders, paths and the HUD; ADR-017, whose flight takes its limits from profiles; ADR-010, amended by the overlay; ADR-018's keys.

**Tasks.**
1. **ADR: orders and flight.**
   - The orders: move, stop and hold.
   - A group's move keeps its members' offsets.
   - Ships steer on the plane within their profiles' acceleration, turn rate and speed cap, keep apart from one another, and keep out of the cores' and asteroids' spheres (§2.2 on G61).
   - Orders are commands in `GameCore`'s payload.
   - It amends ADR-017's flight and lifts its foreclosure on steering off a route.
2. **ADR: the interface layer.**
   - The canvas's widgets, in `NeuronClient`: panel, label, button (with its hotkey, or the reason it is disabled), bar and list.
   - Input routing: the interface first, the world second.
   - A world-anchored overlay of markers, rings, bars and lines, drawn with a sloped quad that has a CPU twin (R15).
   - It amends ADR-010. D9 rules out third-party code, so the layer is the tree's own.
3. **`Skirmish`** flies ships to their orders, and a group as one.
4. **The client.**
   - The strategic camera: pan with the arrows and the screen's edges, zoom with the wheel, turn with the middle button, and Home frames the core. WASD does not pan, so that S can stop (the owner, at phase 3's checkpoint).
   - Selection by click and by box, with Shift adding. The pick is on the CPU, against each entity's projected box grown to at least 10 px (the concept's §9).
   - Right-click moves, S stops, H holds.
   - Ctrl with a digit sets a control group, and the digit recalls it.
   - Selection rings, and order markers drawn at once (ADR-018's delay).
   - The debug keys move behind a modifier. Orders work while paused.
5. **The HUD's first panels:** a top bar, a selection panel, and an order bar whose Attack and Mine buttons are disabled with their reason until phases 5 and 6.
6. **`InputState`** gives the cursor's position, which it keeps and does not yet give.
7. **Tests.**
   - Over 20 seeds of random orders, ships arrive within tolerance and never enter a sphere.
   - A group's move keeps its spacing.
   - The pick, the widgets' hit tests and the input routing hold, on the CPU.
   - The sloped quad's twin agrees with its shader.

**Done when.** CI is green; captures show a selection, a group's move and its markers; the camera and selection hold 60 frames a second in Release on the reference machine.

**Checkpoint.** Select and move ships and groups for a few minutes. Does the camera feel right? Do the selection and orders read? Does the fog open and close as ships scout?

### Phase 5 — Combat

**Goal.** Ships and the cores' turrets fight by the concept's rules. Shots remove voxels, and a ship is lost when its command module fails or detonates when a reactor does.

**Concept.** G6, G14, G15, G33, G39, G47, G51 (§2.2's subset), G54, G57, G58, G59 (§2.2's subset), G60, G71, G72; the concept's §8.

**Tasks.**
1. **ADR: combat.**
   - **Weapons:** the mass driver's shells lead (G58), and the laser's beam hits at once.
   - **Aim:** a weapon aims at a voxel drawn from its target's silhouette, from the profile (G57). The profile gains the silhouette for a fixed set of directions.
   - **Targets and fire:** a ship keeps its target, and holds fire on a line its own side's hulls block (G71).
   - **Jinking:** a ship under fire jinks within its acceleration (G58).
   - **Resolution:** a tick's shots resolve at once, against the grids as the tick began (G72).
   - **Damage:** per voxel, up to its class's toughness. A module fails below a share of its voxels.
   - **Loss:** the command module's failure loses the ship; a reactor's detonates it; in the MVP both become debris, as does any piece cut off from the command module (§2.2).
   - **The attack order** closes to range, measured to the nearest voxel (G47).
   - **Events:** shots and damage are events per side. A shell reaches a side as its position and velocity where it enters that side's sensors (G54).
   - **Masks:** baselines carry the mask, and the splat skips a voxel its mask removes. That amends ADR-014, with the mask's twin and its layout (R15, R16).
2. **`Skirmish`** fires, flies shells, sweeps them and beams with `GridWalk`, keeps each voxel's damage, and searches the connectivity of each damaged entity once a tick. The cores' turrets fire as ships do.
3. **The client.**
   - Masks per placement.
   - Shells and beams drawn in the overlay.
   - The attack order on a right-click at an enemy, and attack-move on A with a click.
   - Bars for the weakest vital module and the hull over the selected and the damaged (G59).
   - Ownership at a distance: a mark in its side's color under every entity, at every zoom, with the bars (the owner, at phase 4's checkpoint, settling phase 2's note).
   - A box that holds any of the side's ships selects only those, and otherwise every entity whose center it holds (the owner, at phase 4's checkpoint; the concept's §9).
4. **Tests.**
   - A seeded duel runs the same twice.
   - A mirrored duel with its sides exchanged gives the mirrored result (G72).
   - A ship behind its own side's hull holds fire and damages nothing.
   - A shell leads a target flying straight, and hits it.
   - Over 40 seeds, jinking lowers the mass driver's hits on a gunship and not on a cruiser.
   - The client's mask equals the server's bit for bit, with entities leaving and re-entering sight.
   - A shot reaches a side only from its sensors' edge.
5. **A smoke check, not gate 5.** Headless duels at equal cost among the three combat designs, across three ranges, on 20 seeds and both starts each. The pull request tabulates them; no design winning more than 90 % against all others is the bar.

**Done when.** CI is green; the smoke table and captures of a fight stand in the pull request; the heaviest fight the phase can stage, both sides at 20 command points, holds 16.7 ms a frame and 8 ms a tick in Release on the reference machine.

**Checkpoint.** Stage fights with the existing orders. Can you see voxels go and read which ship is winning? Do both kinds of loss show? Do the bars help, and does ownership read at a distance?

**Decision point.** Is per-voxel combat readable and fun at this scale? If not, the owner chooses before more is built on it among:
- retuning aim, reach and toughness;
- G60's reserved table of damage by weapon and material class;
- aids to readability: hit flashes, a nearer default zoom, bigger bars.

### Phase 6 — Economy and production

**Goal.** Miners cut ore and haul it home, the core's refinery turns it into credits, and credits build ships at the core within each side's command budget. Handicaps set a side's bank and income.

**Concept.** G10, G19, G39 (the mining laser meets asteroids alone), G42, G52 (one grade), G56, G67; the concept's §7.1, and its §5.6 on production.

**Tasks.**
1. **ADR: mining, credits and production.**
   - **Mining:** the mining laser cuts asteroid voxels as a weapon cuts a hull, into a cargo hold.
   - **Hauling:** a standing order cycles a miner between a field and its core.
   - **The refinery** turns ore into credits at G42's rate.
   - **Prices:** a design costs its voxels by class and its modules.
   - **Production:** the core builds one ship at a time, and the ship appears whole when done (§2.2).
   - **The budget:** the server enforces command points (G56).
   - **Handicaps** set a side's bank and income multiplier (G67).
2. **`Skirmish`.**
   - Asteroids lose voxels as they are mined, as events per side.
   - Miners run the cycle.
   - The side's credits, command points and production queue live in `Skirmish` and reach that side alone, in its snapshots (G34).
3. **The client.**
   - The HUD shows credits with the income of the last minute, and the command points.
   - The production panel shows each design's price, points and availability, and the queue with its progress and a cancel.
   - A right-click on an asteroid with miners selected mines it, and the Mine button works.
4. **The command line:** `--handicap-bank <A>,<B>` and `--handicap-income <A>,<B>`, shown at the start.
5. **Tests.**
   - Mining removes voxels and yields credits at the rate.
   - A miner's cycle completes.
   - Production respects credits, the budget and the queue.
   - A handicap multiplies income.
   - A side never receives the other's credits or queue.

**Done when.** CI is green; the pull request tabulates §8.3's economy targets as measured headless; captures show the loop.

**Checkpoint.** Mine, build and hit the budget. Is the first minute busy enough? Does the loop read in the HUD?

**Decision point.** Does the economy pace the match? Before research and the opponent build on these numbers, the owner judges §8.3's targets: the first credit, a miner's payback, and the time to a full fleet.

### Phase 7 — Research

**Goal.** Six projects at the core's lab change what a side can field and how its fleet performs, at once.

**Concept.** G2, G13, G55, G64 (§2.2's subset); the concept's §7.2.

**Tasks.**
1. **ADR: research.**
   - The six projects of §8.2, with their prices and durations, run one at a time.
   - An armor mark raises its class's toughness on every hull of the side at once (G55).
   - An upgrade reaches every such module the side has fielded (G55).
   - A design can be built once its size class and every module of its fit are researched (the concept's §7.2).
2. **`Skirmish`** runs the lab, applies each project's effect and gates production. A side's research reaches that side alone.
3. **The client:** a research panel with each project's price, duration, prerequisites and progress; and the production panel shows why a design is locked.
4. **Tests.**
   - A project's effect reaches fielded ships at once.
   - A locked design cannot be built.
   - Prices and durations are applied.
   - A side never receives the other's research.

**Done when.** CI is green; captures show the panel and an unlocked cruiser in the field.

**Checkpoint.** Research toward the cruiser and field one; upgrade the fleet mid-fight. Are the choices real?

### Phase 8 — The opponent

**Goal.** The computer opponent plays the whole loop through its own session, under fog, stepped with the server. Whole matches run headless in the tests, and a logged match replays.

**Concept.** G16, G27, G41, G53, G67; the concept's §10; R19, R21.

**Tasks.**
1. **ADR: the opponent.**
   - The `Opponent` project references `NeuronCore` and `GameCore` alone; its namespace is `Opponent` and its suite `OpponentTests`.
   - The host steps it at one fixed point each tick, through an interface in `NeuronCore` that `NeuronServer` calls and `Outpost` wires (G41).
   - Its budget of work.
   - The headless match harness.
   - It updates `AGENTS.md` §2's table, the header filter and the solution.
2. **`Opponent`** reads its side's snapshots, remembers every enemy it has seen, and sends commands, by §8.5's script:
   - economy: miners, and fields near first, then in the middle;
   - production and research in order;
   - a scout;
   - defense of its core;
   - waves once its fleet outweighs the enemy it remembers.
3. **`Outpost`.** `--skirmish` gives side B to the opponent, with its own end of a loopback and nothing else (R19).
4. **The harness,** in `OpponentTests`, runs `Skirmish` on a `ServerHost` in lockstep, with no client. It pits two opponents, or the opponent against a scripted side, and writes a summary of each match: its length, winner and cause, each side's economy by the minute, and each design's figures.
5. **Tests.**
   - The opponent makes no refused command over a match.
   - It destroys a passive side's core within 30 minutes, on 10 seeds from both starts.
   - Its mirror matches end, by a core or by the clock.
   - A logged match replays to the same bytes.
   - The opponent's session is a side's, never an observer's.

**Done when.** CI is green; the harness's summaries of 10 seeds from both starts stand in the pull request.

**Checkpoint.** Play against it at even handicaps and at a handicap in your favor. Does it expand, defend, scout and attack? Can you beat it, and does it punish mistakes?

**Decision point.** Does the opponent play well enough to judge the game with? If not, the owner chooses between one more pass on it, in this phase, and continuing with handicaps as its difficulty.

### Phase 9 — The match, whole

**Goal.** A match starts, plays and ends: victory, defeat or a draw by the clock, with a minimap under fog and an end screen, and it lasts about as long as the concept intends. Then the owner plays it (§6).

**Concept.** G20, G36, G66, G67, G69 (§2.2's subset); the concept's §9 on the minimap.

**Tasks.**
1. **Victory and the clock.** A side loses when its core is destroyed (G20). From minute 60 each core's command module loses voxels at a fixed rate, and cores that fail on one tick draw (G36).
2. **The end screen:** the result and its cause, the duration, both sides' handicaps (G67), and each design's figures: built, lost, kills and damage dealt (G69's subset).
3. **The minimap:**
   - the sector, the player's own entities and their sensors' coverage;
   - the enemies seen now, and enemy structures as last seen;
   - the camera's footprint.
   
   A click moves the camera there.
4. **Tuning.** A pass with the harness: opponent against opponent on 10 seeds from both starts, tuning §8's values until the median match lasts 20 to 40 minutes and neither start wins more than 65 %. The pull request gives the tuned tables.
5. **Performance.** The heaviest battle of a match, both sides at 20 command points, measured in Release on the reference machine.
6. **The MVP's checklist** (§6.2), made ready for the owner.

**Done when.** §6.1's criteria hold, except the owner's own play, which is the gate.

## 6. The MVP gate

### 6.1 Criteria

- The owner has played at least five whole matches against the opponent, and gone through §6.2's checklist.
- The harness, opponent against opponent on 10 seeds from both starts:
  - every match ends by minute 70;
  - the median lasts 20 to 40 minutes;
  - neither start wins more than 65 %.
- The opponent destroys a passive side's core within 30 minutes in all 20 runs.
- A logged match replays to the same bytes.
- In Release on the reference machine, the heaviest battle holds 16.7 ms a frame, and the server 8 ms a tick.
- CI is green and the checkers clean.

Outside players (G68) are the owner's choice at this gate; the concept asks for them at G-M5's verdict.

### 6.2 The owner's checklist

1. A skirmish starts from the command line with the player's core, two miners and two gunships, and the enemy hidden.
2. Scouting reveals the middle and the enemy; enemy ships vanish when they leave sensor range, and the enemy core stays as last seen.
3. Miners mine and haul, credits rise, and a field visibly shrinks.
4. Production builds ships until the budget of 20 points stops it.
5. Research unlocks the lancer and the cruiser, and upgrades reach the fleet at once.
6. Ships fight on their own and by order; voxels are shot away; ships are lost both ways; the bars show the weakest vital module.
7. The opponent expands, scouts, defends and attacks in waves.
8. A match ends in victory, defeat or a draw by the clock, and the end screen shows each design's figures.
9. A handicap changes the opponent's strength noticeably.
10. The frame rate holds in the largest fight.

And five questions for the verdict: Was it fun? Did a hull's shape matter? What confused you? What would you add first? What would you cut?

### 6.3 The verdict

- **Go:** the milestones after the MVP (§7) are planned in detail, starting from what the matches taught.
- **Adjust:** a named set of changes is made to the MVP first, as extra phases of this plan.
- **Re-plan:** the concept is revisited where the MVP showed it wrong, before any further milestone is planned.

## 7. After the MVP

After the gate, the concept's milestones resume from the MVP's code, re-planned in the light of its matches. The concept's own order and gates are the starting point. Each widens a mechanism the MVP built narrow:

1. **Scale:** fifty ships a side, gates 1, 3 and 4, `ExecuteIndirect` and S-M9's levers as the gates call for them.
2. **Wrecks and salvage:** G51, G63.
3. **The station game:** constructors, structures at connectors, power by tree, outposts and voxel clearances (G9, G26, G46, G61, G65).
4. **Traders:** G4, G25.
5. **The library:** variants, refit, repair and the module stock (G5, G35).
6. **Altitude bands:** G7.
7. **The research tree:** at its full size, with three labs (G64).
8. **The opponent's own library,** the batch, and gates 5 and 8 to 10.
9. **The designer:** G45, G-M6.
10. **The library challenge:** G69.

## 8. Starting values

These are where the phases start, not measurements. The phase that introduces a value tunes it, and its pull request updates these tables with what it measured. Targets are stated as figures a profile or the harness can check, so that the constants behind them stay free.

### 8.1 Designs and classes

| Design | Class | Voxels, measured in phase 1 | Fit | Command points | Available |
|---|---|---|---|---|---|
| `Miner` | Frigate | 1,210 | Command module, reactor, 2 thrusters, mining laser, cargo hold, sensor | 1 | From the start |
| `Gunship` | Frigate | 1,155 | Command module, reactor, 2 thrusters, 2 mass drivers, sensor | 1 | From the start |
| `Lancer` | Frigate | 1,119 | Command module, reactor, 2 thrusters, 2 lasers, sensor | 1 | After Lasers |
| `Cruiser` | Capital | 9,017 | Command module, large reactor, 4 thrusters, 2 mass drivers, 2 lasers, sensor | 4 | After Capital hulls and Lasers |
| `StationCore` | Station, the skirmish's own | 28,955 | Command module, large reactor, shipyard, lab, refinery, sensor array, 4 mass drivers | — | Placed by the skirmish |

Phase 1 planned about 1,000 voxels for the miner, 1,500 for each combat frigate, 10,000 for the cruiser and 30,000 for the core. It kept the frigates near 1,150 and gave the cruiser 63 % heavy armor. That way two thrusters move a frigate, and four the cruiser, within a quarter of the class targets below. The cruiser and the core carry the large reactor, since their fits draw more than a small one supplies ([ADR-026](ADR/ADR-026-game-core.md)).

**Class targets.** The two classes aim at ADR-017's class figures, which the owner accepted by eye:
- a frigate: 60 units/s at most, about 30 units/s² of acceleration, and a 45°/s turn;
- a capital ship: 20 units/s, about 5 units/s², and an 8°/s turn.

**Class limits.** A frigate's box is about 32 × 16 × 48 with a budget of 2,000 voxels. A capital ship's is about 64 × 24 × 96 with a budget of 12,000. Profiles land within 25 % of the targets.

**Measured in phase 1** (ADR-026).
- **Acceleration.** The frigates accelerate at 25.1 to 34.9 units/s², and the cruiser at 5.7.
- **Half turns.** The frigates turn half a turn in 4.3 to 4.6 s, and the cruiser in 23.4 s, against 4.0 s and 22.5 s at their caps alone.
- **Prices.** A gunship costs 3,819 credits and builds in 25.5 s, and a cruiser costs 28,848 and builds in 192 s.

**Materials.** The light class has density 1 and toughness 10, and the heavy class density 3 and toughness 30. A heavy voxel costs four times a light one.

### 8.2 Research

| Project | Effect | Price, credits | Duration |
|---|---|---|---|
| Lasers | Unlocks the laser, and with it the lancer | 1,500 | 3 min |
| Capital hulls | Unlocks the capital class, and with Lasers the cruiser | 3,000 | 5 min |
| Armor II | Both classes' toughness × 1.3, on every hull at once | 2,500 | 4 min |
| Mass drivers II | Mass driver damage × 1.25, on every one fielded | 2,000 | 4 min |
| Thrusters II | Thrust × 1.2, on every one fielded | 2,000 | 4 min |
| Refinery II | Credits per ore × 1.25 | 2,000 | 4 min |

**Target:** a side finishes about three of the six by minute 30 (G64's half).

### 8.3 Weapons, sensors and the economy

**Weapons and sensors.**
- The mass driver fires 300 units/s shells out to 600 units, re-drawing its aim every shell.
- The laser reaches 250 units, and holds its line.
- A ship's sensor sees 700 units, and the core's sensor array 1,200.

**Time to kill** (G59). A duel at equal cost lasts at least three times the loser's half turn: 12 s between frigates, 67.5 s against a capital ship. The target is about 20 to 40 s between frigates.

**Economy.**
- An ore voxel buys about five light hull voxels (G42, the first review's model).
- A miner fills its hold in about 40 s at a near field.
- The first credit arrives within 60 s.
- A miner repays its price within 2 minutes of mining.
- Six miners pay for a full budget of 20 points in 12 to 15 minutes.

**The start.** Each side has its core, two miners and two gunships, and a bank of one miner and one gunship.

### 8.4 The sector

The cores stand at (−1,800, 0, 0) and (1,800, 0, 0).

- **Near fields:** two about 500 units from each core, for example at (−1,400, 0, ±500), and their half turns.
- **Middle fields:** two larger ones at (0, 0, ±800).

The half turn is (x, y, z) → (−x, y, −z), so each field has its image. No core sees the middle at the start: the nearest middle field lies 1,970 units from a core, beyond the array's 1,200.

**Phase 2's values** ([ADR-030](ADR/ADR-030-skirmish.md)). A near field holds 6 asteroids within 110 units of its center, and a middle field 10 within 180, 44 in all. Each stands within 20 units of the plane and at least 48 from its field's others. Each side's two miners and two gunships hold station in a line 120 units in front of its core. Side 1, the player's, is blue, and side 2 red.

### 8.5 The opponent's script

The opponent decides every 15 ticks. Its steps are counted as work, never timed (R21).

- **Economy:** it keeps four to six miners on the nearest field with fewer than three, and moves to the middle once the near fields run dry.
- **Production:** two miners and two gunships first. Then, within the budget, it aims at a mix of about half gunships, a quarter lancers and a quarter cruisers, once each is unlocked.
- **Research:** Lasers, Mass drivers II, Capital hulls, Armor II, Thrusters II, then Refinery II.
- **Scouting:** at minute 2 it sends one gunship through the middle fields toward the other start.
- **Memory:** every enemy ship it has seen, with its design and last position, until it sees that ship destroyed or two minutes pass. The enemy core is where the symmetric layout puts it.
- **Defense:** when an enemy is seen within 1,200 units of its core, its idle combat ships attack-move there.
- **Waves:** it attacks with every idle combat ship when its fleet's value is at least 1.3 times the enemy's it remembers, with six combat ships or more, or at minutes 15, 25 and 35 regardless. A wave falls back to the core below half its starting value.
- **Harassment:** it attacks enemy miners it sees.

## 9. Risks

- **The interface is the long pole.** D9 rules out a toolkit, so every panel is built on the canvas. Phase 4 keeps the widgets to five, and each later phase adds only the panels it needs.
- **Readability at the strategic zoom.** Per-voxel damage reads at the battle zoom (the concept's §9). Phase 5's decision point is where it is judged, with aids ready if it fails.
- **Generated designs look plain.** They are judged at phase 1's checkpoint. MagicaVoxel replacements need no change to the plan, since designs are data.
- **The opponent decides whether the MVP can be judged.** Phase 8 holds it to a passive side and to the owner's play, and handicaps cover what it lacks.
- **Starting values are guesses.** The harness makes tuning cheap; each phase's pull request says what it tuned, and phase 9 tunes the whole.
- **The MVP does not retire the scale risk.** Fifty ships a side, and per-voxel damage under that load, wait for the gates after the MVP (§7). Keeping the concept's mechanisms, only narrower, is what keeps that work additive.
- **The protocol changes in most phases.** A version bump is cheap within one process, and each phase's ADR records its version.
- **CI is slow.** The agent verifies on the owner's PC before it pushes, and CI confirms.

## 10. Checkpoint log

| Phase | Pull request | Date | Verdict | Notes |
|---|---|---|---|---|
| Plan | [Zwaliebaba/Outpost.Voxel#20](https://github.com/Zwaliebaba/Outpost.Voxel/pull/20) | 2026-09-29 | Approved | Repair stays out of the MVP, as §2.2 has it. Phase 1 may start. |
| Phase 1 | [Zwaliebaba/Outpost.Voxel#20](https://github.com/Zwaliebaba/Outpost.Voxel/pull/20) | 2026-09-29 | Approved | Vectored thrust stands (ADR-026), and the cruiser keeps its 63 % heavy armor. The build rate, the working names and `GameCore` drew no comment and stand. No run of `Outpost.exe` was reported. Phase 2 may start. |
| Phase 2 | [Zwaliebaba/Outpost.Voxel#20](https://github.com/Zwaliebaba/Outpost.Voxel/pull/20) | 2026-09-29 | Approved | Captures of two seeds at 2 s show the whole layout, each drawn at exactly tick 60 (ADR-031), and a close-up shows the core and the four ships fitted, their modules at their mounts. The owner measured 145 frames a second, against the 60 the phase asked for. The side's color reads only up close: it covers 0.6 % of the core's visible voxels and 0.7 % to 5.1 % of the ships', as measured over the models. The owner closed the phase without the repaint the agent proposed, so ownership at a distance waits for phase 4's overlay (the concept's §9). The build rate shows nowhere until production, so phase 6's decision point judges it. The first view and the sides' colors drew no comment and stand. Phase 3 may start. |
| Phase 3 | [Zwaliebaba/Outpost.Voxel#22](https://github.com/Zwaliebaba/Outpost.Voxel/pull/22) | 2026-09-29 | Approved | The owner's captures of one seed at tick 60 show the checkpoint: without `--observe` side 1 draws 17 entities, its core, its four ships and its near fields, and nothing of side 2 or the middle; with it, all 54. No frame time was reported. The fogged first view, pause from any session, and a refusal counted without a message back drew no comment and stand; phase 4's strategic camera, which Home frames on the core, takes over the first view. The owner settled a clash in phase 4's keys: the arrows and the screen's edges pan, S stops and H holds, and WASD does not pan. Phase 4 may start. |
| Phase 4 | [Zwaliebaba/Outpost.Voxel#23](https://github.com/Zwaliebaba/Outpost.Voxel/pull/23) | 2026-09-30 | Approved | The owner merged the phase and reported that the camera and selection held 60 frames a second in Release, as the done-when asks. No captures reached the agent. The owner answered the pull request's three questions. A box that holds any of the side's ships selects only those, and otherwise every entity whose center it holds; the concept's §9 says so now, and phase 5's client builds it. Ownership at a distance, a mark in its side's color under every entity at every zoom, lands in phase 5 with the bars, which settles phase 2's note. M arms a move, A attacks, G mines, and Alt is the debug keys' modifier, as built. The first view on the side's core, the order bar hidden while inspecting, and flight's choices in ADR-033 drew no comment and stand. Phase 5 may start. |
