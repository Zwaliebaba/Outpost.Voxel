# Outpost.Voxel — Game Concept Review

**Status:** a review for the owner. Nothing here changes `GameConcept.md` until the owner takes it into the concept · **Date:** 2026-09-29
**Reviews:** [`GameConcept.md`](GameConcept.md) as the owner accepted it on 2026-09-28, with G22 as he amended it the same day. It is read against [`SpaceScene.md`](SpaceScene.md), [`NeuronVoxelFormat.md`](NeuronVoxelFormat.md), [ADR-003](ADR/ADR-003-engine-and-game-layout.md), [ADR-014](ADR/ADR-014-placements.md), [ADR-015](ADR/ADR-015-client-server-boundary.md), [ADR-017](ADR/ADR-017-sector.md), [ADR-018](ADR/ADR-018-client.md), `AGENTS.md`, and the decision table of [`Archive/SampleRenderer.md`](Archive/SampleRenderer.md)

This document reviews the game concept and changes nothing. Part 1 gives the findings. Part 2 turns them into proposed amendments. Each amendment is marked either as requiring the owner's decision or as derived from a finding the owner can accept with the rest.

Six reviewers read the concept against its siblings, each with one mandate: the core loop and the player's fantasy (F1), the economy (F2), a min-maxer trying to break the rules (F3), command and readability at scale (F4), the opponent (F5), and scope and feasibility (F6). The lead added four findings from reading the siblings (L1 to L4), and the owner asked that they be carried as findings. The reviewers raised 49 findings. Merged where they said the same thing, they come to 36, and a merged finding keeps its strongest source's number and names the rest.

The fifteen of highest severity went to an advocate for the concept, who argued each against the concept's own reasoning, the owner's stated intent and counter-precedent. Within a severity, challenges to the owner's own decisions came first, then findings that two or more reviewers raised independently. The advocate weakened fourteen, let one stand and refuted none. The lead adjudicated all 36: the fifteen between reviewer and advocate, and the rest alone, by the advocate's two tests. Does the concept already answer it? Would the milestone's own design settle it at no extra cost?

The findings are ranked by severity × confidence × (cost of change later ÷ cost now):

| Factor | 3 | 2 | 1 |
|---|---|---|---|
| Severity | Critical | Major | Minor |
| Confidence | High | Medium | Low |
| Cost of change, later ÷ now | A protocol, data format or order fixed in G-M1 or G-M2 would have to be reopened | A milestone's design or tuning would be redone | Waiting costs nothing |

Ties go to the higher severity, then to the finding more reviewers raised, then to the lead's judgement. The main branch was pulled on 2026-09-29, bringing S-M5 and the amended G22. The next free ADR is therefore ADR-023, and the proposals number from there.

## Part 1 — Findings

### 1. Verdict

The concept is sound as a game in its bones: a Warzone 2100 commander whose units are the player's own sculpted hulls, one machinery of per-voxel removal that serves combat and the economy alike (G6, G19), and an opponent that plays through the same door as the player (G16). No finding breaks an owner decision outright. The one the reviewers pressed hardest, G5's split between library and match, survives with a mechanism for the owner to weigh (F1.2). What the concept lacks is the layer where it becomes a game: the rules that decide which hull wins. Its three largest risks follow from that. First, G18 is a principle without rules: no milestone says a ship flies on its own mass and thrust (F6.3), nothing says where a weapon aims or how a ship turns to bear (F1.4), and validation and exposure leave cheap exploits open (F3.1, F3.3, F3.5), so whatever the code happens to do becomes the meta. Second, the fog is written into the snapshots when it must be a property of protocol version 2, which G-M1 is about to fix without it (F6.6, F5.5). Third, the loop may not close: no rule ends a dry sector with both cores standing, and the economy's central number, what an ore voxel buys, is missing (F2.1, F2.2). The plan reaches the first risk in its third milestone, which would be in time. But no done-when asks the question and no gate carries a number, so that milestone can pass without answering it (F1.1, F6.2). The second and third risks meet a test only when G-M5 plays a skirmish. The review therefore proposes that G-M1 carry each design's profile and a per-side protocol, that the battle milestone's gate be the shape test, and that command and combat change places.

### 2. Ranked findings

| Rank | ID | Title | Target | Tier | Severity | Confidence | Advocate |
|---|---|---|---|---|---|---|---|
| 1 | F1.1 | No milestone asks whether shape decides a fight | §12.2, §14; G22 | Derived (G22) | Major | High | Weakened |
| 2 | F6.3 | Nothing says a ship flies on its own figures | G18, §5.4, §9, §12.2 | Execution | Major | High | Weakened |
| 3 | F6.6 | Protocol version 2 must carry the fog | G15, G21; §7.1, §8.2, §9, §11.1 | Derived (G15, G21) | Major | High | Weakened |
| 4 | F6.2 | No budget for the bench, and no batch for the match | §3, §8.4, §12.2, §14 | Execution | Major | High | Weakened |
| 5 | F1.4 | Where a weapon aims and how a ship bears pick the winning hull | §8.1–§8.3, §9; G14, G18 | Execution | Major | High | Stands |
| 6 | F1.2 | Research into materials and classes lands on the library | G5, G12, G13; §5.2, §7.2 | Owner decision (G5) | Major | High | Weakened |
| 7 | F5.5 | Commands are not checked against a side, and the set has no orders | §10, §11.1; G16, G27 | Derived (G16, G27) | Major | High | Not sent |
| 8 | F4.1 | The match needs named variants, not one fit per ship | §5.1, §5.6; G12 | Derived (G12) | Major | High | Not sent |
| 9 | F2.1 | No rule ends a dry sector with both cores standing | §3, §7.1; G10, G19, G20 | Owner decision (G10) | Major | High | Weakened |
| 10 | F4.2 | Nothing shows a ship's condition, or stands in for it at sector zoom | §9; G14, G24 | Execution | Major | High | Weakened |
| 11 | F3.1 | Overlapping parts and mounts buy compactness for nothing | §5.1, §5.5; G18 | Execution | Major | High | Weakened |
| 12 | F3.5 | Only a weapon pays for being buried | §5.4; G14, G18 | Derived (G14, G18) | Major | High | Not sent |
| 13 | F3.2 | A shot's contract is unwritten | §7.1, §8.2, §8.3; G15, G19 | Derived (G15, G19) | Major | High | Not sent |
| 14 | F3.3 | A class by bounding box gates neither mass nor load | §5.4, §6, §11.2 | Derived (G18) | Major | High | Not sent |
| 15 | F5.6 | A thread of its own breaks the repeatability §10 promises | §10, §14; ADR-015 | Execution | Major | High | Not sent |
| 16 | F2.2 | No exchange rate, and parity cannot pay for the scale | §5.4, §7.1, §11.2; G19 | Execution | Major | High | Not sent |
| 17 | L1 | The build order is not the file's order | §5.6, §13 | Execution | Minor | High | Not sent |
| 18 | F6.4 | §14's last fallback would break the voxel sculpted being the voxel shot | §8.4, §14; G6, G11 | Owner decision (G6, G11) | Major | Medium | Weakened |
| 19 | F6.7 | G23's reason covers only half the designer | §12.2; G23 | Derived (G23) | Major | Medium | Not sent |
| 20 | F3.8 | No rule says what a power shortfall stops | §5.4, §6; G26 | Derived (G26) | Major | Medium | Not sent |
| 21 | F3.7 | Keep-outs can hold a target beyond every weapon's range | §8.1, §9; G20 | Execution | Major | Medium | Not sent |
| 22 | F2.5 | Far fields pay less, and "richer" is undefined | §4, §7.1; G19 | Derived (G19) | Major | Medium | Not sent |
| 23 | F1.6 | §6 leaves the core's design unsaid, and §3 promises a repair nothing provides | §3, §6; G20 | Derived (G20) | Minor | High | Weakened |
| 24 | F2.3 | The start's bank, tier zero and first credit are unstated | §3, §7.2; G26 | Execution | Minor | High | Weakened |
| 25 | F2.6 | Module repair has no price | §5.6, §7.3 | Execution | Minor | High | Weakened |
| 26 | F2.4 | A refused trade leaks through the fog, and the stock has no per-side rule | §7.3; G21, G25 | Derived (G25) | Minor | High | Weakened |
| 27 | F5.2 | The opponent's five verbs lack the two its rules require | §10; G21, G25 | Execution | Minor | High | Not sent |
| 28 | F4.6 | Formations for four, clicks on one pixel, keys taken | §9; ADR-017 | Execution | Minor | High | Not sent |
| 29 | F5.7 | The opponent's library is frozen while the player's learns | §10, §12.2 | Execution | Major | Medium | Not sent |
| 30 | F4.3 | The bands' job depends on friendly fire, and their spacing is unset | §3, §8.2, §9; G7 | Owner decision (G7) | Minor | Medium | Weakened |
| 31 | F4.8 | The HUD has no schedule past G-M2 | §9, §12.2; D9 | Execution | Minor | Medium | Not sent |
| 32 | F6.5 | §11.2 sizes the first clash, not the match | §11.2, §8.3; S19 | Execution | Minor | High | Not sent |
| 33 | L2 | Separation steering meets ADR-017's foreclosure | §9, §13 | Execution | Minor | High | Not sent |
| 34 | L3 | The draw count assumes cascades the slice defers | §11.2, §14; G22 | Execution | Minor | High | Not sent |
| 35 | L4 | G-M1 lists work that is done | §5.3, §12.2 | Execution | Minor | High | Not sent |
| 36 | F2.7 | Twenty items and uncapped labs make research an order, not a choice | §6, §7.2 | Execution | Minor | Medium | Not sent |

"Not sent" marks a finding outside the fifteen, which the lead adjudicated alone.

### 3. Findings in full

Findings appear in rank order. Each ends with the advocate's verdict, where it was sent, and the lead's adjudication in one sentence. Where adjudication narrowed a finding, the fields below say what survives, and the claims it dropped are listed in §5. Milestone numbers are the concept's own. Where a proposal holds whichever way question 29 orders them, it names a milestone by its content: the command milestone, or the battle or combat milestone.

#### 1. F1.1: No milestone asks whether shape decides a fight

Merges F6.1 and F5.8.

- Target: §12.2 (the done-whens and the order), §14 ("The brick and the needle"), §5.4, §8.1; G22
- Tier: Derived decision (G22)
- Severity: Major (F1.1 raised it as Critical)
- Confidence: High
- Problem: The bet is that shape matters: "the test of them is whether designs of different shapes win in different situations" (§5.4), and §14 names G-M3 and G-M4 as where authored designs can show it. But no milestone's done-when asks the question. G-M3 is done when two fleets fight to the end, G-M5 when a skirmish ends, and G-M6 when one design is fielded (§12.2). A milestone can pass without testing the bet it exists to test. The plan also builds command (G-M2) before combat (G-M3), though ships choose their own targets (§8.1), so the question waits a milestone for nothing. And the bench that judges per-voxel damage sits inside the milestone whose cost it judges (G22).
- Evidence:
  - The concept: §5.4, §14, the done-whens of §12.2, §8.1 and G22.
  - SpaceScene §16 and NVF §10 record ten milestones built on 2026-09-28, N-M0 to N-M4 and S-M1 to S-M5, so code is not the scarce input. The owner's verdicts are.
  - Precedent: Gratuitous Space Battles fights player-designed fleets without orders once a battle starts, so a battle without command already tests a design. Besiege and Kerbal Space Program put a build-and-test loop of minutes at the center of construction.
- Proposal:
  1. G-M3's done-when gains the shape test: three authored hulls of equal cost, a brick, a needle and a spine, each beat another over 20 seeded engagements under F1.4's rules (Part 2, gate 5).
  2. Swap G-M2 and G-M3. Combat comes first, with a bench that fights library fleets choosing their own targets, which also gives the owner a proving ground for his MagicaVoxel designs. Command comes second. Repair moves to G-M4, which brings the shipyard it needs, and the opponent's project starts with combat as the scripted second side the bench needs anyway (F5.8).
  3. Bench whole placements before G-M1 (gate 1), so that their cost is known before protocol version 2 is fixed.

  F6.1's fuller reorder, a gray-box first match before the full economy, is left out: it re-plans more than the finding needs. The candidate this review was asked to weigh, a gray-box skirmish with coarse damage, is rejected. The renderer draws every surviving voxel whatever the damage cell, so it retires only the server's risk, and it would test a game in which the unit built is not the unit broken (F6.4).
- Cost of change: §12.2's table, the wording of G22, and SpaceScene §16's re-plan paragraph. None of G-M1's own work exists yet, so this is cheapest now. A gate written after its milestone gates nothing.
- Falsification: G-M3 as planned runs the shape test without a gate asking for it, and nothing in G-M2's command layer needs rework once ships fight.
- Advocate: Weakened. "No milestone's done-when asks whether hull shape decides a fight, so G-M3's battle bench, which §14 already names for the brick-and-needle test, should gate on it."
- Adjudication: Weakened, and the advocate's restatement is adopted. Authored hulls are shot apart in G-M3, the third of six milestones, so the bet is not tested last. But it is not tested at all unless a done-when asks, and the swap, which costs nothing, asks one milestone sooner.

#### 2. F6.3: Nothing says a ship flies on its own figures

Merges F5.3's profile and F4.7.

- Target: G18, §5.4, §9, §11.1, §12.2 (G-M1, G-M2), §14 ("The brick and the needle"); ADR-017
- Tier: Execution
- Severity: Major
- Confidence: High
- Problem: G18 makes performance follow from geometry: mass and inertia from voxels, acceleration from thrusters, turning from inertia (§5.4). No milestone says that flight takes its limits from those figures. §9 flies ships "with ADR-017's flight model", whose speeds, accelerations and turn rates are defaults per class. G-M1 is done when "the sector flies and draws fitted designs", and class defaults would satisfy that. Nothing says where the figures are shown either. Until G-M6, designs come from MagicaVoxel, which shows none, and §9's HUD lists none. If G-M2 tunes paths and formations on class defaults, G18 has to be retrofitted into tuned flight. §14's shape test then runs on silhouettes and arcs alone, and is judged by anecdote.
- Evidence: G18, §5.4, §9, §11.1 and §12.2. ADR-017's class table gives a frigate 60 units/s, 30 units/s² and 45°/s, and a capital ship 20 units/s, 5 units/s² and 8°/s. SpaceScene §5.3 calls these defaults. Precedent:
  - Kerbal Space Program derives thrust-to-weight from a craft's parts and shows it in the editor.
  - In Cosmoteer, where the thrusters sit and what the ship weighs decide how it accelerates and turns.
  - Starsector's refit screen shows ordnance points, flux and each slot's arc as a ship is fitted.
- Proposal:
  - G-M1 computes one profile per design and variant in the shared game library: mass, center of mass, inertia, thrust per axis, and each mount's clear arc, walked with `GridWalk`. For a fixed set of directions it also holds the silhouette and the armor over the command module, and it carries price, build time and size class.
  - Flight takes each ship's limits from its profile, in the ADR that lifts ADR-017's foreclosure (L2). A battle updates the profile as voxels and modules go.
  - A design card shows the profile in the library and in production, refit and inspection. A command-line tool prints it from G-M1, for designs authored in MagicaVoxel.
  - The opponent reads the same profiles, which ADR-003's rule already puts in the shared library.
- Cost of change: G-M1's and G-M2's designs, and one ADR amending ADR-017. Cheap before G-M2 tunes paths and formations, and dear after.
- Falsification: G-M3's shape test, run on class defaults, already splits wins by shape through arcs and silhouette alone.
- Advocate: Weakened. "No milestone says flight takes its limits from each design's mass, inertia and thrust, or where those figures are shown, so G-M1's or G-M2's design should say both."
- Adjudication: Weakened, and the restatement is adopted. §9 reuses ADR-017's mechanics rather than its class table, and ADR-003 already routes shared derivations to the shared library. But the concept says neither that flight uses a design's own figures nor where they are shown, and both must be said before G-M2 tunes flight.

#### 3. F6.6: Protocol version 2 must carry the fog

Merges F4.5 and F5.1.

- Target: G15, G21, G25; §7.1, §8.2, §8.3, §9, §11.1; ADR-015; R19
- Tier: Derived decision (G15, G21)
- Severity: Major
- Confidence: High
- Problem: §9 promises that "a side cannot see past its sensors however its client is changed", but the concept's mechanisms hand the fog's secrets to every client:
  - G21 filters snapshots only.
  - §8.2 sends every shot once to every client with its origin, which gives a hidden shooter away.
  - §7.1 sends mining to every client "as it happens".
  - Designs and fits cross once (§11.1), to whom and when unstated, and a client must hold a design to draw it.
  - A side that first sights an entity needs its mask, wrecks and module states, which "sent once" cannot give.
  - A refused trade reveals a hidden ship (G25).

  G-M1 writes protocol version 2 before G-M3 builds these events and G-M5 turns fog on.
- Evidence: §7.1; §8.2 ("nothing about it is sent twice"); §9; §11.1's two unrelated rows. ADR-015 sends the same bytes to every session and forecloses per-session snapshots. ADR-018 forgets an entity once it leaves the snapshots. SpaceScene §6.2's welcome names every model. R19. Precedent: Quake III Arena delta-encodes each client's snapshot against the last one that client acknowledged. VALORANT withholds an enemy from a client until the server judges it visible. StarCraft keeps enemy buildings as last seen. Warzone 2100's counter-battery sensors make firing reveal the firer, as a designed mechanic.
- Proposal: Protocol version 2 is designed per side in G-M1, with fog off until G-M5:
  - Each session's messages are encoded separately.
  - On first sight a side receives a baseline (design, variant, module states and mask), then the changes, on the ordered channel.
  - A shot reaches a side from the tick it enters that side's sensors, unless the owner makes firing reveal the shooter as a mechanic.
  - Damage and mining out of view are not sent. Structures stay as last-seen ghosts, and a trader's radius is a sensor for every side with a ship inside it.
  - Design internals are the owner's choice (question 25): public, shown by inspection, which keeps R19's parity; or secret, sent as the exterior only and revealed by damage.
  - §9 gains a table of who receives what, and when.
  - G-M3's bit-for-bit test runs with entities leaving and re-entering view.
- Cost of change: G-M1's design and an ADR amending ADR-015. Left until G-M5, it becomes a change to every event type and its tests.
- Falsification: G-M5 turns fog on by filtering per session with no new message, and G-M3's bit-for-bit test holds across sightings.
- Advocate: Weakened. "§7.1's mining and §8.2's shots are written for every client, so protocol version 2 should deliver designs, shots, damage and mining per side from G-M1, with a baseline on first sight, rather than wait for G-M5."
- Adjudication: Weakened on the claim that the concept means fog for snapshots alone, since §9's guarantee covers every message. The mechanism and its timing stand in full, because G-M1 fixes the protocol two milestones before the fog it must carry.

#### 4. F6.2: No budget for the bench, and no batch for the match

Merges F5.4 and F4.7's after-action record.

- Target: §3, §5.4, §8.4, §12.2's done-whens, §14
- Tier: Execution
- Severity: Major
- Confidence: High
- Problem: §14 says per-voxel damage may prove "too dear", but names no figure it would be too dear against. The concept states no frame budget, tick budget, resolution or reference machine, and G-M3 is done once its note is committed, whatever the note says. The slice's two open questions are the match's length, which §3 calls "not a figure anyone has measured", and whether "designs of different shapes win in different situations" (§5.4). Each needs many matches, a solo owner plays a few a week, and nothing says what the server or the opponent records to explain a result. The means already exist: two opponents stepped from one seed can play headless (§10, ADR-015's `Step`, ADR-018's lockstep).
- Evidence: §3, §8.4, §12.2 and §14. D11 fixes the bench at 1920 × 1080. SampleRendererPerformance.md measures the one GPU on record. AGENTS.md §6: CI times nothing. ADR-018: `--frigates` goes up to 100,000, the figures give per-pass GPU times, and the bench writes a CSV and a summary. Precedent: the AIIDE StarCraft AI Competition has measured its bots by thousands of bot-against-bot games a year since 2010.
- Proposal:
  - **Budgets.** The owner fixes them (question 26): 16.7 ms a frame at 1920 × 1080 on the Adreno X1-85 in Release ARM64, as the M5 note was measured, and 8 ms of the server's 33.3 ms tick.
  - **Gates.** Each milestone ends at a gate from Part 2's table: a figure, a threshold, and a change of course.
  - **The batch.** A headless batch of opponent against opponent, seeded and in lockstep, writes the telemetry:
    - the opponent's decisions, with their inputs;
    - the server's economy, minute by minute;
    - its engagements, with the designs, bearings, shots and hits in each;
    - every command refused;
    - an after-action record for each design;
    - a summary of match length, winner and cause, and wins by design pair and range band.
  - **G-M5's done-when** keeps the owner's verdict and adds two conditions: the opponent kills a passive side's core within 40 minutes on 10 seeds of 10, and mirror matches end within 20 to 40 minutes on 7 of 10.
- Cost of change: A paragraph in §12.2, a threshold on each §14 risk, and a headless runner over ADR-018's lockstep, built with combat. A gate written after its milestone gates nothing.
- Falsification: Every milestone through G-M5 lands within the budgets at its first measurement, and the owner's play settles every design question without a number.
- Advocate: Weakened. "The concept sets no frame or tick budget for G-M3's bench to decide against, and a headless opponent-against-opponent batch, which §10's stepping already allows, would measure match length and shape faster than the owner's play."
- Adjudication: Weakened only in tone. The inputs for gates exist, and G-M5's verdict is rightly the owner's. But neither the budget nor the batch exists, and those two are what turn the plan's checks into decisions.

#### 5. F1.4: Where a weapon aims and how a ship bears pick the winning hull

Merges F5.3's behavior for bringing arcs to bear.

- Target: §5.5, §8.1–§8.3, §9, §11.1; G14, G18; §14 ("The brick and the needle"); ADR-017
- Tier: Execution
- Severity: Major
- Confidence: High (F1.4 raised it at Medium)
- Problem: §8 never says where a weapon aims: the box's center, the center of mass, the nearest voxel or a module. Each design has one command module (§5.5), and losing it loses the ship (G14), so the aim rule picks the dominant hull:
  - Fire at the center drills toward the middle, so the command module belongs off center.
  - Fire spread over the surface rewards total armor, which favors the brick.
  - Fire at the nearest face rewards a thick prow.

  Nor does anything turn a ship to bring its arcs to bear. ADR-017 flies a ship nose along its path, and ships choose targets, not bearings (§8.1), so a broadside design charges nose first. Whatever the code does becomes the meta.
- Evidence: §8.1–§8.3; §5.1 and §5.5; §9, whose pick names the module; §11.1; ADR-017's flight. Precedent:
  - Homeworld 2 lets a player target a capital ship's subsystems.
  - FTL has the player choose which enemy system each weapon fires at.
  - Homeworld's tactics settings and Starsector's ship AI turn ships to bring their weapons to bear, the same way for every side.
- Proposal:
  - Choose the aim rule in §8.1 before combat is built, and name the hull it favors. One option is the center of mass with a spread per weapon, wide for the mass driver so that it erodes and narrow for the laser so that it drills. Each weapon then punishes a hull, and the refit has a reason.
  - Bringing arcs to bear is a ship behavior on the server, the same for every side. A nose-armed design charges. A broadside design orbits at weapon range with its covered side inward, and a stance overrides either.
  - §9 gains a targeting stance (engines, weapons, nearest) and "attack this module", with the command module left off the list so that no kill switch is one click away.
  - §14's test runs under the chosen rules.
- Cost of change: §8.1 and §9, the orders of the command milestone and the combat milestone's weapons. After combat is built, every authored design is shaped against whatever rule the code happened to take.
- Falsification: The battle bench shows one hull winning under every aim rule tried, and designs of different shapes winning in different situations with no behavior for bringing arcs to bear.
- Advocate: Stands.
- Adjudication: Stands, as the advocate conceded, and its confidence is raised to High. AGENTS.md's rule that design comes before code guarantees the rule is written, not that it is chosen with its hull in view, and that is what this finding asks.

#### 6. F1.2: Research into materials and classes lands on the library

Merges the research half of F3.3.

- Target: G5, G12, G13; §5.1, §5.2, §5.3, §5.6, §7.2
- Tier: Owner decision (G5)
- Severity: Major
- Confidence: High
- Problem: G5 fixes a design's hull, mounts and materials before the match (§5.2), and a refit swaps modules only (§5.6). So two of §7.2's five branches, materials and size classes, pay only through the library: a researched material arms no ship unless a library design names it, and a class builds nothing without a library design of its size. The order of research, which §7.2 means as a choice, is then set when the library is written, and each material wants its own version of every hull. §5.1's "Warzone's kind of choice" leaves out Warzone's body, which that game's in-match design screen picks. And §7.2's fielding sentence names only modules, though §5.3 and §5.4 gate materials and classes too.
- Evidence: §5.2; §5.3 ("which research unlocks"); §5.4; §5.6; §7.2 ("enough to make the order of research a choice"). This adds a mechanism to §14's "Sculpting inside a strategy game": research forces re-armoring whatever players want. Precedent: Warzone 2100's design screen works during the match, so a researched component is fielded at once, and its armor research (Composite Alloys) upgrades bodies already built. Stellaris's designer works in the match, armor is a slotted component, and fleets upgrade at a starbase.
- Proposal: This challenges G5. There are four options:
  - **(a) Recommended.** Each palette entry pairs a color with a material class, light or heavy. When the design is built or refitted, the side's best researched material of that class fills it. G5 stands, and G13 changes.
  - **(b)** The fit holds each entry's material, so a refit re-armors at a price per voxel. G5 becomes "refits their modules and armor", and G12 and G13 follow.
  - **(c)** Materials become upgrades to armor already fielded, as Warzone's alloys are. No decision changes.
  - **(d)** G5 stays as it is, and the library sets the research order, the cost §14's first risk already names.

  Whichever option is taken, §7.2's fielding sentence names the class, the materials and the modules.
- Cost of change: G-M1's game data and fit, G-M4's research, and §5.2, §5.3, §5.6 and §7.2. Cheapest before G-M1 fixes the format of the game data. After that, every authored design carries the old shape.
- Falsification: In G-M4 and G-M5, the owner's research order changes with what each match shows, and no researched material sits idle for want of a design.
- Advocate: Weakened. "Research into materials and size classes pays only through library designs that use them, which is G5's intended coupling, and §7.2's fielding sentence should name materials and classes, as §5.3 and §5.4 already gate them."
- Adjudication: Weakened. The claim that "a library hull in the best armor fields at minute one" misreads §5.3, §5.4 and §5.5, and shrinks to a one-line fix to §7.2. The mechanism stands, and it is the owner's to weigh against G5 (question 23).

#### 7. F5.5: Commands are not checked against a side, and the set has no orders

- Target: §10 ("it cannot cheat by accident"), §11.1; G16, G27; R19; ADR-015
- Tier: Derived decision (G16, G27)
- Severity: Major
- Confidence: High
- Problem: G16 lets the opponent do "what commands allow", so the command set is its whole action space, and the server's check of each command is all that holds it to the player's rules. Today the set is pause, resume, detonate and restore. `ServerHost::Apply` takes a command without its session, so any session may detonate any entity. §11.1 amends ADR-015's commands for designs alone. §9's orders, production, refit, research, trade and building appear nowhere as commands, and a side's own credits, queues and stock appear nowhere in its snapshots. Nothing binds a session to a side, so §10's "it cannot cheat by accident" is not yet true.
- Evidence: ADR-015's `Command` message and step 1 of its host. `NeuronServer/ServerHost.h`, whose `Apply(const NeuronCore::Command&)` has no session (the lead checked the code). SpaceScene §5.5 and §6.2, where detonate and restore exist "for testing". §9's orders and HUD, and §11.1's rows.
- Proposal: Protocol version 2 (G-M1) binds each session to a side when the host adds it. The server refuses, by name, any command that names another side's entity, structure, design or stock, and counts the refusal rather than closing the session. Detonate and restore become debug commands that a skirmish refuses. §11.1 gains two rows:
  - the whole command vocabulary, designed once for the HUD and the opponent alike;
  - a side's own credits, research, queues and stock in its snapshots.

  A test sends an order to another side's ship and checks that it is refused and changes nothing.
- Cost of change: Two rows of §11.1 now, and the protocol ADRs of G-M1 and the command milestone later. Cheapest while protocol version 2 is written.
- Falsification: The command milestone's orders land with per-side checks and a complete command set, and no later milestone has to add either.
- Adjudication: Not sent to the advocate. It stands: the code confirms that the host applies commands without their session, so the guarantee §10 and R19 rest on is written nowhere yet.

#### 8. F4.1: The match needs named variants, not one fit per ship

- Target: §5.1, §5.2, §5.6; G12; §14 ("Sculpting inside a strategy game")
- Tier: Derived decision (G12)
- Severity: Major
- Confidence: High
- Problem: §5.1 promises "which module, in which slot, in a few seconds". But a fit belongs to one ship (G12), §11.2 assumes eight modules a ship, the power check couples them (§5.4), and G11 fields fifty ships. Refitting a wing ship by ship takes minutes of screen time, and each refit is a trip to a shipyard (§5.6). At ADR-017's cruise speeds, a front 2,000 units away costs a frigate 67 s and a capital ship 200 s there and back, before the refit itself. So a refit is never a battle action. §14's first risk rests the split between library and match on something no one can do at this scale.
- Evidence: Warzone 2100's design screen makes a template (body, propulsion, weapon) that factories build. It never refits a built unit, and its research upgrades reach units already built. Starsector keeps named variants of each hull, with an autofit, and changes loadouts between battles, never during one. §5.2 already stores a "default fit", which is one template per design.
- Proposal:
  - A variant is a named fit of a design. The library keeps several per design, the default first, and the refit screen can make another in the match.
  - Production and refit orders name a variant, never one ship's fit. "Refit to variant" is an order to a selection: each ship goes to the nearest shipyard of its class, queues, refits and rejoins its group.
  - The refit screen edits a variant once and shows, as it changes, the power margin, mass, acceleration, turn rate, arcs, price and time (F6.3's card).
  - §5.1 reads: between engagements, the match asks which variant to build, and which to refit to.
- Cost of change: G-M1 (variants in the shared library and protocol version 2), G-M4 (the refit order and screen), §5.1, §5.2, §5.6 and G12's wording. Cheaper before protocol version 2 fixes what a fit is on the wire.
- Falsification: In G-M4 the owner refits ten ships one by one in under about 15 s of screen time, or G-M5's skirmishes show that he never wants one fit on many ships.
- Adjudication: Not sent to the advocate. It stands: the concept's own "Warzone's kind of choice" is a choice of template, and §5.2's default fit is already one, so the change names what the concept half has.

#### 9. F2.1: No rule ends a dry sector with both cores standing

Merges F1.5.

- Target: G10, G19, G20; §2, §3, §6, §7.1, §8.3; §12.2 (G-M5)
- Tier: Owner decision (G10), as F1.5 raised it. The fix the lead recommends is derived.
- Severity: Major (F2.1 raised it as Critical)
- Confidence: High
- Problem: The sector's credits only fall. Fields are finite (G19), wrecks are scenery, salvage is out of scope under G10 (§2, §8.3), and the core cannot be rebuilt (G20). Once the fields are dry and the banks spent, no one builds or repairs, yet turrets cost nothing to run (§6). Two sides with their fleets spent, each behind its turrets, then hold for ever, and nothing ends the match. §3's "so every match has an end" rests on a core that someone must still reach. When the fields run dry is tuning, since §3 calls the length a target. That they can run dry with both cores standing is a missing rule, and a stalled skirmish fails G-M5's done-when (§12.2).
- Evidence:
  - The concept: G19 and §7.1; §2; §8.3 and S19, which keep wrecks and debris for ever by default; §5.4 and §6, where turrets draw power, not credits; §10, where the opponent attacks "once its fleet is strong enough".
  - F2.2's model: the rich fields run dry at minute 37 with 25 miners a side, or at minute 79 with 10.
  - Precedent:
    - Warzone 2100's oil never runs out.
    - The deposits of Total Annihilation and Supreme Commander never run dry either, and their builders reclaim wrecks. Supreme Commander's commander also yields a small income of its own.
    - StarCraft's minerals do run out, but its maps hold enough expansions that a mined-out map is a rare late state.
- Proposal:
  - **Derived, and recommended.** Add a dry-sector rule beside G20 in §3: once every field is dry, each core's command module loses voxels at a fixed rate, so every match ends. G-M4 sizes the ore against the target length, about 300,000 voxels of rich field on F2.2's model, so that the rule rarely fires.
  - **Owner alternatives on G10.**
    - (a) A wreck's voxels count as ore, cut by the same mining laser at a lower yield. This reverses §2's exclusion of salvage and makes a failed attack pay the defender.
    - (b) The core pays a small, endless trickle of credits, as Supreme Commander's commander does. This keeps one resource, from a second source.
- Cost of change: §3's victory paragraph and §7.1, G-M4's field sizes, and a stall check in G-M5's done-when. One server rule, and no engine work. Cheapest before G-M4 tunes the fields.
- Falsification: In G-M5's batch the opponent plays itself over twenty seeds until the fields are dry, and every match ends in a core kill first.
- Advocate: Weakened. "The concept has no rule for a sector whose fields are dry while both cores stand, so §3's 'every match has an end' overclaims, and G-M4 should size the ore against the target and add a backstop."
- Adjudication: Weakened. The dry times rest on assumed field sizes, a contested middle is §4's intent, and a failed attack costs the attacker its fleet, so the snowball and the need for salvage are dismissed. The missing rule to end a dry sector stands, and the salvage alternatives remain the owner's (question 28).

#### 10. F4.2: Nothing shows a ship's condition, or stands in for it at sector zoom

Merges F4.4 and F1.3.

- Target: §9 (the camera and the HUD), §5.4, §8.3, §11.1; G6, G14, G24; SpaceScene §7.6, §11.1
- Tier: Execution
- Severity: Major
- Confidence: High
- Problem: A ship lives while its command module works (G14), and its modules sit inside the hull (G3), so its voxel count says little about how near it is to death. §9's HUD has no condition readout, so these look like a healthy ship:
  - a failed module;
  - a reactor below its draw;
  - a weapon silent for any of five reasons: range, arc, power, failure, or a target out of sight;
  - a cut-off wreck still in its side's colors.

  At sector zoom a voxel covers under half a pixel (SpaceScene §11.1, §7.6), and a frigate a few pixels. Yet §9 names no icons, and friend and foe differ by one palette entry that shots can remove (G24).
- Evidence:
  - A voxel covers a pixel at 1,375 units (SpaceScene §11.1). Framing the default sector's 4,000-unit disk of stations (ADR-017) makes a frigate 16 × 13 px.
  - The canvas draws only rectangles and text: `FillRectangle`, `Print` and `Measure`, capped at 16,384 quads a frame (ADR-010; the lead checked `NeuronClient/Canvas.h`).
  - Precedent:
    - Supreme Commander's strategic zoom turns units into icons, as do Homeworld's Sensors Manager and Sins of a Solar Empire.
    - Warzone 2100 and Homeworld draw health bars, and Homeworld 2 shows each subsystem's health.
    - Kerbal Space Program labels a severed stage "Debris".
- Proposal:
  - **The command milestone fixes three zoom levels:**
    - a battle zoom, where a voxel covers at least a pixel. It frames near 1,375 units, and fights default to it.
    - a sector zoom, with an icon over the coarse model of each ship and structure. The icons are symbol-font glyphs drawn through the canvas.
    - the minimap.

    Ownership shows through the overlay at every zoom. §11.1 gains a world-anchored overlay, with an ADR amending ADR-010 for a sloped quad.
  - **The combat milestone adds a condition readout:**
    - bars for the command module's integrity and for the hull;
    - four status markers (weapons, thrust, power, sensors), computed by the client from its missing voxels and the variant;
    - an inspect view that shows each module by state;
    - one small field on the wire saying why a weapon is silent;
    - a neutral palette and a "ship split" alert for wrecks;
    - a standing order to retreat for repair.
- Cost of change: The HUD of the command and combat milestones, §9 and §11.1. Cheap now.
- Falsification: The finding is wrong if, in the fifty-a-side battle, the owner picks out crippled ships and sends them for repair as reliably without a readout as with one, and at sector zoom tells friend from foe and class from coarse models and G24's palettes alone.
- Advocate: Weakened. "§9's HUD shows nothing of a ship's condition, which G14's buried command module and failing modules make necessary, and names nothing to stand in for voxels at a sector-wide zoom."
- Adjudication: Weakened. The camera zooms, and a battle framed near 1,375 units shows its bites and splits, so the claim that damage is invisible where the commander plays is dismissed. The condition readout that G14 makes necessary stands in full, and so does the stand-in at sector zoom.

#### 11. F3.1: Overlapping parts and mounts buy compactness for nothing

- Target: §5.1, §5.5; G18; NVF §4.3
- Tier: Execution
- Severity: Major (F3.1 raised it as Critical)
- Confidence: High
- Problem: §5.5 refuses a mount whose box holds a hull voxel, but not two mounts whose boxes overlap, and NVF forbids a repeated position only within a part (§4.3), not across parts. So an author can stack four weapon markers on one spot, or overlay two parts. Mass, power and price still count every module (§5.4), so the stack buys no free firepower. It buys compactness, which is G18's own currency: four guns in the silhouette of one, four modules behind the armor that one needs, and less inertia for the same mass. Nothing joins a mount's box to the hull either, so a module may hang free of it, and a design of modules alone passes "one piece" vacuously.
- Evidence:
  - The concept: §5.1 and §5.5.
  - NVF: §4.1 makes names unique but not positions; §4.3; §4.4 allows up to 1,024 parts; §6.2.
  - The authoring path: §5.2 and G23 keep MagicaVoxel until G-M6, and overlapping models are ordinary there.
  - Stacked armor gains nothing per hit, because a hit damages "the voxels at and around where it lands" (§8.3).
  - Precedent: Space Engineers and From the Depths hold one block per cell. Kerbal Space Program hides overlap behind an "allow part clipping" cheat, which its players use to cram in parts.
- Proposal: Add one sentence to §5.5. A design is refused when two of its parts put a voxel in one cell, when two mounts' boxes share a cell, or when a mount's box shares no face with a hull voxel. The same check holds between a station's structures. They share one grid, on whole positions and quarter turns (S18), so the check is exact.
- Cost of change: §5.5, and a G-M1 test for each clause. There is no format change (N8). Cheaper before the first library designs are authored.
- Falsification: A frigate with four weapon markers on one spot and two overlapping parts is already refused, or on the bench it gains nothing over one gun.
- Advocate: Weakened. "§5.5 should also refuse two parts' voxels in one cell and mount boxes that overlap, as §5.1's rule that a module's box holds no hull voxel already implies."
- Adjudication: Weakened from Critical to Major. The advocate is right that mass, power and price scale with a stack, and that one hit reaches every stacked layer. But compactness in silhouette, inertia and armor is exactly what G18 prices. The third clause, a module hanging free of the hull, went unanswered.

#### 12. F3.5: Only a weapon pays for being buried

- Target: §5.4 (arcs, mass), §8.3; G14, G18
- Tier: Derived decision (G14, G18)
- Severity: Major
- Confidence: High
- Problem: §5.4 prices exposure for weapons alone: "a gun buried in armor does not fire". The command module pays nothing for burial, though its loss alone ends a ship (G14), and neither do thrusters, reactors or sensors. So the winner is an armored ball. Its command module and thrusters sit at the centroid, and its guns sit on spines. For its mass it has the deepest core and the least inertia, and the spines give it a needle's arcs. The needle's one asset, a small silhouette end-on, needs a facing that pure pursuit along a route does not hold (ADR-017). The ball keeps full thrust until its last layer goes.
- Evidence:
  - §5.4 ("turning follows from its moment of inertia, which a long hull has more of"), G14 and §8.3.
  - NVF §4.1 makes a hardpoint's +Z "the way a weapon fires and an engine's exhaust leaves", so the line to test is already in the file.
  - Precedent: in Crossout the cabin is the one vital part, and builds bury it. In MechWarrior Online a 'Mech also dies when both its legs go, so mobility has a kill of its own.
- Proposal: Extend §5.4's clear-line test to every module whose effect leaves the hull. A thruster thrusts only while the line along its mount's +Z is clear of its own hull, and a sensor sees only along clear lines. A ball has the least surface for its mass, so it carries the fewest exposed thrusters and guns. F3.5 also proposes a further loss condition, offered as an option on G14: a ship is lost when it has neither a working thruster nor a working weapon.
- Cost of change: §5.4, §8.3, and G14's row if the option is taken. G-M1's validation reuses the arc test. Cheap now.
- Falsification: On the bench, a ball with buried thrusters does no better than the same voxels with the thrusters exposed.
- Adjudication: Not sent to the advocate. It stands for the clear-line rule, which uses a convention the file already carries. The loss condition is optional, since a husk with neither thrust nor guns is already harmless while it waits to be killed.

#### 13. F3.2: A shot's contract is unwritten

- Target: §7.1, §8.2, §8.3; G15, G19
- Tier: Derived decision (G15, G19)
- Severity: Major
- Confidence: High
- Problem: §8.2 sweeps a shell "through the grids of the designs in its way", and §8.3 damages "the voxels at and around where it lands", but neither says what a shot meets or spends:
  - **Spaced armor.** A one-voxel skin one reach off the armor stops a shell for a hole, not a crater: at a reach of 3, 29 voxels against 76, so it stops 2.6 times the shells.
  - **Own hulls.** A side that can hit its own hulls can cut a capital ship's one-voxel neck to shed a slab.
  - **Wrecks.** If wrecks stop shells, the slab trails as a shield.
  - **Ore.** If weapons remove ore, mass drivers erase finite fields (G19); if mining lasers cut hulls, miners are armed.
- Evidence: §8.3; §7.1 ("as a shot cuts them out of a hull"); S19. The 29 and the 76 count the voxel centers within 3 of the impact, in one layer and in a half-space; the lead checked both. Precedent: in World of Tanks, spaced armor defeats HE and HEAT shells. In Total Annihilation, weapons fire grinds wrecks to heaps and then to nothing.
- Proposal: The combat milestone writes one sweep contract:
  - A shot meets the enemy's hulls and asteroids, and its own side's hulls if the owner so decides (question 24).
  - It passes wrecks and traders.
  - It spends its damage voxel by voxel along its path, carries the rest on, and applies its reach where it stops.
  - A weapon's shot stops at an asteroid and removes nothing.
  - A mining laser meets asteroids only.
- Cost of change: §7.1, §8.2 and §8.3 in the combat milestone's design, plus G-M4's mining. Cheap now, because the sweep is written from scratch.
- Falsification: On the bench, a hull with a spaced skin lasts less than half again as long as the same voxels laid solid. In G-M4, a mass driver removes less ore than a laser collects.
- Adjudication: Not sent to the advocate. It stands, with one conflict between reviewers, which goes to the owner. F3.2's shots pass friendly hulls, but option (A) of F4.3 gives the bands a job only if friendly hulls stop fire (question 24).

#### 14. F3.3: A class by bounding box gates neither mass nor load

- Target: §5.4 (size classes), §6, §11.2
- Tier: Derived decision (G18)
- Severity: Major
- Confidence: High
- Problem: §5.4 sets a class by the bounding box. The frigate fills 11 % of its 33 × 27 × 12 box, so a solid brick in a frigate-sized class holds 10,692 voxels, 99.5 % of the capital ship's 10,747. That is a capital ship's mass in the first class, gated only by price. Nor does a box bound §11.2's load. A hundred bricks in the capital ship's box come to 7.3 million voxels, eight times §11.2's 900,000. Only the largest class binds a structure. The research half of this finding, a library hull in the best armor, belongs to F1.2.
- Evidence: SpaceScene §4 gives the ships' boxes and voxel counts, and a brick in the capital ship's box holds 72,765 voxels. The lead checked the arithmetic. Precedent:
  - Robocraft capped a robot's CPU, a cost every block carries, rather than its extent.
  - Space Engineers caps the blocks a player may place (PCU), for the engine's sake.
  - Starsector gives every hull a fixed budget of ordnance points for its fit.
- Proposal: A class is a voxel budget within a box, and every design has one, ship or structure.
- Cost of change: §5.4 and §6, and the class data in the shared game library. Cheaper before G-M1 puts designs there and G-M4 prices the classes.
- Falsification: In G-M4 a frigate-box brick is fielded against the capital ship at equal credits and research, and prices alone keep it out of the first class's window.
- Adjudication: Not sent to the advocate. It stands: the arithmetic is exact, and a voxel budget also gives §11.2's load the bound it lacks.

#### 15. F5.6: A thread of its own breaks the repeatability §10 promises

- Target: §10 ¶1; §14 ("Determinism holds within one build"); ADR-015, ADR-018
- Tier: Execution
- Severity: Major
- Confidence: High
- Problem: ADR-015 applies a session's commands "in the order they arrive, at the current world tick". In the game, the host runs its own thread and catches up by as many as four ticks a wake. An opponent on a thread of its own therefore lands each command on whichever tick its thread reaches first, and two runs of one seed diverge at its first command. Only stepping it with the server repeats. Even then, the player's commands arrive at wall-clock ticks, and an opponent that budgets its thinking in milliseconds decides differently on a busy machine. §10 offers the thread as an equal choice, and §14 calls determinism enough "for a repeatable opponent".
- Evidence: §10. ADR-015's host and thread (`MAX_TICKS_PER_WAKE`, 4). ADR-018's two loops. ADR-017's `SectorTests`. Precedent: Age of Empires, StarCraft and Supreme Commander record a match as its stream of commands, each stamped with its turn, and replay it through a deterministic simulation (Bettner and Terrano, "1500 Archers on a 28.8", 2001).
- Proposal: Strike "on a thread of its own" from §10.
  - Whatever owns the server's thread steps the opponent once a tick, at one fixed point.
  - The host logs every command it applies, with its world tick and its session. A skirmish is then its seed, build, libraries and log, and it replays.
  - The opponent draws randomness only from `PcgHash` of the seed, and budgets its thinking in work, never in time.
  - Two tests: two runs of the opponent against itself, from one seed, send the same bytes; and a logged run replays to the same bytes.
- Cost of change: One sentence of §10, and a command log amending ADR-015 in the command milestone. Dear later, because a bug that cannot be replayed cannot be handed to Claude Code.
- Falsification: An opponent on its own thread gives byte-identical AI-vs-AI streams across repeated runs of one seed, under load, on the owner's machines.
- Adjudication: Not sent to the advocate. It stands: the mechanism follows from ADR-015's own words, and every seeded gate of F6.2 depends on the fix.

#### 16. F2.2: No exchange rate, and parity cannot pay for the scale

- Target: §5.3, §5.4, §7.1, §11.2; G11, G19
- Tier: Execution
- Severity: Major
- Confidence: High
- Problem: The economy's central number is missing: how many ore voxels buy a hull voxel. G19 makes ore voxels and §5.4 prices hulls per voxel, which invites parity, and at parity the stated scale cannot be paid for. §11.2's own counts put 450,000 voxels on each side, 150,000 in the fleet and 300,000 in the station. So the fields would have to hold at least §11.2's whole 900,000 again, before modules, research or losses, and the fields are voxels the renderer draws. On the reviewer's fuller model, one side's build costs about 711,000 credits, the work of 51 near-field miners over 30 minutes: as many ships as G11 gives a whole side.
- Evidence:
  - §11.2's counts.
  - The reviewer's model assumes a laser that cuts one voxel a tick into a hold of 500, fields 1,000 and 4,000 units out, modules at 375 and twenty projects at 5,000. On those numbers a near miner earns 7.7 voxels a second and a rich one 3.0, and one ore voxel to five hull voxels funds the fleet in 13 minutes of ten miners.
  - The result is most sensitive to the exchange rate, then to the laser.
  - Precedent: Warzone 2100 prices a design as the sum of its body, propulsion and weapon.
- Proposal: G-M4's design fixes the exchange rate first, near one ore voxel to five hull voxels on this model. It commits a budget table as its tuning contract: income per miner, fleet and station prices, and the fields' totals and dry times. The concept states whether miners and constructors count toward G11's fifty.
- Cost of change: G-M4's design, with G-M1 carrying the materials' prices. Cheap now. After G-M4, the fields, the fleets and the renderer's budget are all tuned around whatever number emerged.
- Falsification: G-M4 fields a slice at parity with at most fifteen miners a side, and its fields stay inside what the battle bench shows the renderer can hold.
- Adjudication: Not sent to the advocate. It stands on arithmetic from §11.2's own counts, which needs none of the reviewer's assumptions. Its side claim, that the rational structure is a minimal shell "at any price", is dismissed (§5).

#### 17. L1: The build order is not the file's order

Adds F3.4's point on connectivity.

- Target: §5.6, §13
- Tier: Execution
- Severity: Minor
- Confidence: High
- Problem: §5.6 has a ship appear by drawing "a growing prefix" of hull records "stored in build order, outward from the command module's mount". That conflicts with the format in four ways:
  - NVF §4.3 and ADR-020 keep each part's records in the `.vox`'s order, N6 copies the voxels to the GPU "as it lies", and §13 says the format does not change.
  - If the game reorders records at load, every voxel id, mask bit and damage event is in an order the file does not hold.
  - "Outward from the mount" is undefined across parts.
  - Unless the order runs breadth-first through face neighbors, a prefix of a curled hull is in pieces, and §8.3's cut-off rule wrecks voxels already paid for (F3.4).
- Evidence: §5.6 and §13; NVF N6, §4.3 and §6.2; ADR-020 ("Each part keeps its records in the `.vox`'s order"); ADR-014 (an id is the placement's base plus the record's index).
- Proposal: §5.6 states that server and client derive the build order at load, by one integer rule: breadth-first through face neighbors from the command mount's cell, ties broken by record index, parts in part order. Record indices, ids, masks and damage events all use that order, while the file keeps the `.vox`'s.
- Cost of change: A sentence now, recorded by G-M1's ADR. Once masks and damage events are keyed by index, changing the order is a change to the protocol.
- Falsification: G-M1's tests derive the same order on server and client from one design, and nothing downstream reads records by file index.
- Adjudication: The lead's Phase 0 finding, which the owner asked to be carried as one. It stands.

#### 18. F6.4: §14's last fallback would break the voxel sculpted being the voxel shot

- Target: §8.4; §14 ("Per-voxel damage at scale"); G6, G11
- Tier: Owner decision (G6, G11)
- Severity: Major
- Confidence: Medium
- Problem: §14's fallbacks for per-voxel damage at scale are "fewer, larger hits, or damage in coarser cells". The second breaks the rule that build-and-break games keep, and that G3 and G6 promise here: the voxel sculpted is the voxel shot away. Armor sculpted one voxel thick would stop counting. Neither fallback touches the renderer, which draws every surviving voxel whatever the size of the damage cell. The renderer's exits are named elsewhere in the concept (S-M9's levers held on damaged placements, `ExecuteIndirect`, and S19's debris lifetime), and §14 does not rank them against the server's. If the bench fails, the owner decides under pressure which of G6 and G11 gives way.
- Evidence:
  - The concept: §8.4, §11.2, §14; SpaceScene §7.6 and §17.
  - Linear extrapolation from the M5 note puts the battle's 900,000 voxels at 12.6 ms of splats. Adding the note's 0.86 ms of lighting and tone map gives 13.5 ms, inside 16.7 ms. But ships draw oriented, and the sky and bloom have since joined the frame.
  - Precedent: in Cosmoteer, From the Depths and Space Engineers, the unit placed is the unit destroyed. Starsector's coarse armor grid sits on hulls its players do not build.
- Proposal: Decide §14's ladder now (question 27):
  1. lifetimes for debris and wrecks (F6.5);
  2. S-M9's levers, held on damaged placements;
  3. then either G11's count lowered toward thirty a side, or the slice's largest class capped below the capital ship;
  4. a coarser damage cell last, and only with an equally coarse build grid.

  The owner's alternatives are to keep fifty a side and coarsen both grids to 2³, or to keep G6 and G11 and accept 30 frames a second on the reference machine.
- Cost of change: One paragraph of §14. Cheapest before anything is measured.
- Falsification: The battle bench finds the renderer within budget with lifetimes and levers, and only the server over it.
- Advocate: Weakened. "Coarse damage cells would stop the voxel sculpted from being the voxel shot away, so §14 should rank them last and list the renderer's exits beside the server's."
- Adjudication: Weakened. The extrapolated frame fits the budget, and the concept already names renderer exits, so "the cost most likely to bind" is dismissed. What stands is that §14's last fallback breaks the fantasy and belongs last. Which of G11 and hull size gives way first is the owner's decision.

#### 19. F6.7: G23's reason covers only half the designer

- Target: G23, §12.2 (G-M6), §14 ("The designer lands last")
- Tier: Derived decision (G23)
- Severity: Major
- Confidence: Medium
- Problem: G23 puts the whole designer last "so that its rules … are the ones the skirmish has tested" (§12.2). That reason covers only half of the designer. Materials, validation, size classes and the profile depend on tuned rules. Placing, removing and painting voxels, and placing mounts, depend on no rule, only on the picking the command milestone builds (§9). So the half of the fantasy that §1 calls the game's difference waits on a reason that does not apply to it. The phase's largest hand-built UI (D9) also lands where slippage hurts most. And G-M6 is done when one design is fielded, with no bar set against the MagicaVoxel pipeline it replaces.
- Evidence: §1, §5.2, §9, §12.2, §14. SampleRenderer D7 (the visibility buffer holds the normal) and D9. NVF §10: the MagicaVoxel pipeline works, with `--check` in CI. Precedent: Teardown builds its levels and props in MagicaVoxel, so a MagicaVoxel pipeline has already carried a shipped voxel game's content.
- Proposal: Split G-M6.
  - **The sculpting core** (voxels, colors, mounts, undo and saving) comes after command, beside the first match.
  - **The rule-bound half** (materials, validation, size classes and the profile) stays last.
  - **Done when** the owner builds a new 1,000-voxel design, with mounts and materials, faster in the game than through MagicaVoxel and `NvfImport`.
  - **A time box:** past a date the owner sets, the slice ships with MagicaVoxel as its hull tool (question 31).
- Cost of change: G-M6's row and G23's wording. Cheaper before command's panels are shaped for the HUD alone.
- Falsification: The rules tuned in G-M4 and G-M5 force the sculpting core to be rewritten.
- Adjudication: Not sent to the advocate. It stands. The owner chose G23 for a reason that holds for the rules; the split keeps that reason and moves only what it does not cover.

#### 20. F3.8: No rule says what a power shortfall stops

- Target: §5.4 (power), §6; G26
- Tier: Derived decision (G26)
- Severity: Major
- Confidence: Medium
- Problem: §5.4 refuses a ship's fit that draws more than it supplies, but damage lowers the supply in battle. Neither §5.4 nor §6 says what stops when draw exceeds supply, on a ship or on a station. If everything stops, one reactor structure silences every turret, and the siege comes down to that one target. §6 also leaves the connectors' topology open. If spare connectors can form loops, no single loss cuts anything off, and G26 never bites. And with no build radius, a chain of connectors creeps turrets out to the rich fields, the trader or the enemy's door.
- Evidence: §5.4 defines power for ships only, and only at fitting. §6: "every structure draws from it", building happens "at a free connector", and there is no radius. Precedent:
  - In Command & Conquer: Red Alert, low power shuts down defenses and radar.
  - In StarCraft, Protoss buildings go dark when their pylon dies.
  - In Factorio, a shortfall slows every consumer in proportion.
- Proposal:
  - When draw exceeds supply, modules or structures stop in the reverse of an order the player sets, weapons last by default. Until that interface exists, all of them slow in proportion.
  - A connector joins only the two structures built at it, so a station is a tree rooted at the core.
  - The core has a fixed number of connectors (F1.6).
- Cost of change: §5.4, §6 and G26; the combat milestone for ships and G-M4 for stations. Cheap now.
- Falsification: In G-M4, destroying the only reactor of a station with four turrets leaves the turrets firing under the chosen rule, and no chain grows past two links in G-M5's skirmishes.
- Adjudication: Not sent to the advocate. It stands, and it covers ships too: F4.2 reads out a reactor below its draw, but only this rule says what that state does.

#### 21. F3.7: Keep-outs can hold a target beyond every weapon's range

- Target: §8.1, §9 (paths, bands), §3; G7, G20; ADR-017
- Tier: Execution
- Severity: Major
- Confidence: Medium
- Problem: §9 routes every path around every keep-out, and ADR-017 sizes a keep-out as the obstacle's sphere plus the ship's plus 50 units. So a muzzle outside the keep-out of a structure the size of MilitaryStation is at least 50 units from its box's corners, 122 from its faces and 249 from its center. A laser at "short range" (§8.1) may never touch it. A buried core's command module may lie beyond every weapon, so G20's end never comes. A one-voxel bar 255 long gets the keep-out of a 3.2-million-voxel cube, so bars on connectors fence a station.
- Evidence: ADR-017 gives the station's sphere as 199.11 and the frigate's and capital ship's keep-outs as 271.26 and 294.92. SpaceScene §4 gives the station as 205 × 227 × 255; the lead checked the arithmetic. §8.1 names no ranges. Precedent: in Warzone 2100 and StarCraft, an attack order moves a unit until its target is within its weapon's range. Obstacles shape the path, never whether a target can be reached.
- Proposal:
  - Range is measured in three dimensions, from the muzzle to the target's nearest voxel.
  - An attack order may enter its target's keep-out, and stops at range.
  - A weapon whose muzzle lies inside another entity's box does not fire.
  - Separation holds across bands (L2).
- Cost of change: §8.1 (a range for each weapon) and §9; the command and combat milestones. Cheap before the path rule is written.
- Falsification: The planner already closes lasers to range on a structure the size of a station, or the tuned laser reaches 250 units.
- Adjudication: Not sent to the advocate. It stands: keep-outs were designed for routes that never attack (ADR-017), and the concept reuses them for ships that must.

#### 22. F2.5: Far fields pay less, and "richer" is undefined

- Target: §3, §4, §7.1; G9, G10, G19
- Tier: Derived decision (G19)
- Severity: Major
- Confidence: Medium
- Problem: Every ore voxel is worth the same (G10, G19), so the "richer" fields of §3 can only be bigger ones. Structures grow only at the station's connectors (G9, §6), so no refinery can stand at a field. On F2.2's model, a miner hauling from the middle earns 0.39 of what one earns at home. Income is limited by the number of miners, not by the fields, so nothing pays for expanding before the near fields are stripped. The pull to the middle (§7.1) is really a push into worse income.
- Evidence: F2.2's model puts the ratio between 0.35 and 0.60 over the lasers tried. §4 says "expansion and fighting happen in the same place". §6's growth builds only at a free connector. Precedent:
  - Warzone 2100's derricks each pump at a fixed rate from an oil resource that never runs dry, so income grows by claiming ground.
  - Homeworld's Resource Controller and Homeworld 2's Mobile Refinery are ships that serve as distant drop-offs.
  - Space Engineers' asteroids hold veins of different ores.
- Proposal:
  - A refinery module for a ship's mount: a mobile drop-off, and a target worth hunting.
  - Ore graded by the asteroid's palette entry, as catalogue data, the way G13 pairs entries with materials. Rich fields become high-grade veins, and where to cut becomes a choice. It stays one resource (G10).
- Cost of change: G-M4's catalogue, the asteroids' generator (§11.1), and §7.1's text. Cheap now.
- Falsification: G-M4 measures hauls that are short beside mining time, or players in G-M5 go to the middle before their near fields are stripped.
- Adjudication: Not sent to the advocate. It stands at Medium, since it depends on hauling time, which G-M4 will measure. The grading also answers what "richer" means in §3, which the concept leaves open.

#### 23. F1.6: §6 leaves the core's design unsaid, and §3 promises a repair nothing provides

Merges the core halves of F2.3 and F3.3, and the structures' repair from F2.6 and F3.4.

- Target: §3, §5.4, §5.6, §6, §9; G17, G18, G20
- Tier: Derived decision (G20)
- Severity: Minor (F1.6 raised it as Major)
- Confidence: High
- Problem: G20 hangs the match on the station core, and the concept leaves two things about it unsaid. First, §6 has the core "placed by the skirmish" without saying whose design it is, how large it is, or what it costs. Read as a library entry, it would be the largest block of the toughest material allowed, placed free. Second, §3 says the core "can be … repaired", but §5.6 repairs only ships docked at a shipyard, and no order in §9 repairs a structure. Structures stand still (G17), so G18 charges their armor nothing in acceleration or turning. A structure's design therefore has one axis, armor against price, which only a class's voxel budget bounds (F3.3).
- Evidence: G17, G18 and G20; §3, §5.4, §5.5, §5.6, §6 and §9. Precedent: Warzone 2100's HQ can be rebuilt, and losing it costs the minimap and the design screen. Dota 2's Ancient and League of Legends' Nexus are static cores that cannot be rebuilt, and both games work.
- Proposal:
  - §6 says the core is the skirmish's own design, the same for both sides, with a fixed number of connectors.
  - §3 either strikes "repaired" or names the mechanism (question 30): constructors repair structures at a price per voxel and at a stated rate. The core's command module is the one exception, as F3.4 proposes, so damage to it only accumulates.
  - Every structure has a size class with a voxel budget (F3.3).
- Cost of change: §3 and §6; G-M4 and G-M5. A paragraph now.
- Falsification: The finding fails if G-M5's sieges bring cores down within 20 to 40 minutes and no repair outpaces a fleet's fire.
- Advocate: Weakened. "§3 says the core can be repaired but no order or mechanism repairs a structure (§5.6, §9), and §6 should say plainly that the core is the skirmish's own design."
- Adjudication: Weakened to Minor. "Placed by the skirmish", in a start both sides share, reads naturally as the skirmish's own core, and finite ore bounds any repair race, so the fortress and the endless siege are dismissed. What stands is that §6 should say so, and that §3 promises a repair no mechanism provides.

#### 24. F2.3: The start's bank, tier zero and first credit are unstated

Merges F1.7.

- Target: §3, §6, §7.1, §7.2; G26
- Tier: Execution
- Severity: Minor (both reviewers raised it as Major)
- Confidence: High
- Problem: A side starts with a core, two constructors, a small escort and no research (§3). The core's role is command alone (§6), so income needs a refinery, a shipyard and a miner, and the start has none of them. That is Warzone 2100's No Bases start, but Warzone's derricks earn where they stand. Here ore is hauled (§7.1), so the first credit waits on a miner's round trip. Three things are stated nowhere:
  - the bank the constructors pay from (§6: "paying as it builds");
  - whether the core supplies power, since G26 has reactors supply it;
  - a tier zero: the material and modules known before any research, from which the start's ships are fitted (§7.2).
- Evidence: §3, §6, §7.1 and §7.2; G26. Precedent: StarCraft's Command Center, Age of Empires' Town Center and Homeworld's Mothership are each a drop-off and a producer at once, so income starts at second zero. Warzone 2100 offers No Bases, Bases and Advanced Bases, and its derricks earn where they stand.
- Proposal:
  - §7.2 defines tier zero: one material, and the command module, reactor, thruster, one weapon, mining laser, cargo hold and constructor bay.
  - §3 states the bank, and whether the core carries a reactor.
  - Optionally, the start adds a refinery at the core's connectors and a miner.
  - §3 says whether the start's ships come from the library or from the skirmish.
  - G-M4's done-when measures the time to the first credit.
- Cost of change: §3, §6 and §7.2; G-M4 and the skirmish layout of G-M5. A paragraph now.
- Falsification: G-M4's loop, with §3's start as written, earns within a minute or two and offers a choice of field, first project or trader before minute three.
- Advocate: Weakened. "§3's start leaves the starting credits, the base catalogue and the core's power unstated, and because ore is hauled rather than pumped in place, G-M4 should check how soon a side first earns."
- Adjudication: Weakened to Minor. A start without income structures is Warzone's own, and §11.2's 20,000 voxels are an average, not a first refinery's size, so the forced chain is dismissed. What stands is the unstated bank, tier zero and power, and the delay that hauling adds, which G-M4 must measure.

#### 25. F2.6: Module repair has no price

Merges F3.4.

- Target: §5.4, §5.6, §7.3, §8.3; G4
- Tier: Execution
- Severity: Minor (both reviewers raised it as Major)
- Confidence: High
- Problem: §5.6 prices a refit by "the difference in price" and repair by "a price per voxel". That leaves three things unsaid:
  - Restoring a damaged or failed module has no price. One per-voxel rate cannot fit both hull voxels, priced by material, and modules, priced by the catalogue (§5.4).
  - A negative difference, a refit to a cheaper module, has no stated meaning. A refund would let a side bank modules as credits.
  - Nothing says whether a module works before its build completes. If it does, a turret built outward from its weapon fires before it is paid for.
- Evidence: §5.4, §5.6, §7.3 and §8.3. Precedent: Starsector's d-mods leave a bill for damage. Total Annihilation's nanoframes do nothing until they are finished. Warzone 2100 keeps two paths for repair: trucks for structures, and the Repair Facility for units.
- Proposal:
  - Repair restores hull voxels at their material's price, and a damaged module at its price times the share of its voxels lost.
  - A failed module is replaced by a refit, at full price.
  - A refit charges the incoming module in full. For the outgoing module, the two reviewers differ, and the owner chooses: refund a share of its price (F2.6), or refund nothing (F3.4).
  - A design under construction is inert until complete.
- Cost of change: §5.6. Repair is built with the shipyard in G-M4 (F1.1), and G-M4 prices it.
- Falsification: The tests refit a damaged module to a fresh one, and cycle one through a cheaper module, and each costs a new module's price.
- Advocate: Weakened. "§5.6 prices a refit by the difference in price and repair by one per-voxel rate, which leaves the restoration of a damaged or failed module unpriced, and G-M3's repair design must price it."
- Adjudication: Weakened to Minor. A like-for-like swap is repair, which §5.6 prices apart from refit, and restoring a ship's own bought module leaves G4's cap intact, so the credit pump and the bypass are dismissed. The missing prices stand.

#### 26. F2.4: A refused trade leaks through the fog, and the stock has no per-side rule

Merges F3.6.

- Target: §7.3, §9; G4, G21, G25
- Tier: Derived decision (G25)
- Severity: Minor (both reviewers raised it as Major)
- Confidence: High
- Problem: Under G25, a trader deals with a side only while that side alone has a ship within its radius. So a refused trade tells a side that an enemy ship it cannot see is inside the radius, which breaks §9's promise that a side "cannot see past its sensors". §7.3's stock "runs down as it sells", with no stock per side and no cap, so a side alone for a moment can buy out what the other side's shortcut depends on. Whether one cheap picket should be able to deny a trader is another question. Exclusivity is G25's intent, "which makes its ground worth holding" (§7.3), and it lets the side that lost the middle deny the winner.
- Evidence: G25, §7.3, G21 and §9. Precedent: Warcraft III's Goblin Merchant cannot be attacked, and sells a limited stock that restocks. In Overwatch and in Halo's King of the Hill, one player on the point contests it, a deliberate comeback tool.
- Proposal: A trader shows every ship within its radius to each side with a ship there, and sells each side from a stock of its own. G-M4 states the premium, the stock and the refill. The reviewers' presence rules are held in reserve, in case G-M5's matches show pickets decide trade: the greater armed hull mass (F3.6), or armed ships alone (F2.4).
- Cost of change: §7.3 and G25; G-M4, and G-M5's fog. Cheap now.
- Falsification: In G-M5 both sides trade in most matches, and parked pickets die within a minute.
- Advocate: Weakened. "A refused trade reveals an unseen ship through the fog, and §7.3 gives the trader's stock no per-side rule; exclusivity itself is the owner's G25."
- Adjudication: Weakened to Minor. Exclusivity is the owner's G25 and can serve a comeback, and "one step beyond" covers a branch not yet started, so the picket as a defect and the narrowing of G4 are dismissed. The fog leak and the stock rule stand.

#### 27. F5.2: The opponent's five verbs lack the two its rules require

- Target: §10 ¶2; G20, G21, G25; §14 ("An opponent is a project")
- Tier: Execution
- Severity: Minor (F5.2 raised it as Major)
- Confidence: High
- Problem: §10 gives the opponent five verbs: expand, keep its economy running, research toward its library, defend, and attack in waves "once its fleet is strong enough". None says how it chooses, and two of the concept's own rules have no verb at all. Under fog (G21), an enemy that leaves the opponent's sensors drops out of its snapshots, so the opponent must scout and remember. And G25 lets one parked ship veto a trade, but no verb clears a trader's radius. "Strong enough" has no measure either.
- Evidence: §10 and §9; ADR-018 ("one only in the earlier has gone"); G25 and §7.3. Precedent: Warzone 2100's skirmish AIs (NullBot, BoneCrusher, Cobra) are scripts with authored build orders and research paths. They compose each unit from ordered lists of the best components research allows. Warzone's components have stats but no position, so those AIs never weigh shape.
- Proposal: A behavior model with one level, deciding once a second from its own session:
  - **Memory:** every enemy it has seen, kept until the enemy is seen destroyed or goes stale.
  - **Build:** an authored order of structures.
  - **Mining:** the near fields first, then the middle under escort.
  - **Research:** whatever its next wanted design lacks.
  - **Production:** the fieldable design that best answers the remembered enemy, read from F6.3's profiles and autofitted within power.
  - **Trade:** it holds the nearest trader's radius with one armed ship.
  - **Scouting:** one cheap ship.
  - **Defense:** a home group.
  - **Attack:** it launches at *k* times the remembered enemy strength, or at a deadline, and retreats below half strength.
  - **Personalities:** rush and tech, drawn from the seed.
- Cost of change: A subsection of §10. G-M5's design sets the constants.
- Falsification: An opponent built from the five verbs alone holds the owner to contested matches across seeds, and the owner never wins by denying its trade or by hiding.
- Adjudication: Not sent to the advocate. The lead weakens it to Minor by the advocate's second test: G-M5's design would scope the opponent in any case. What the concept must say now is the two verbs its own rules demand, memory under fog and clearing a radius, because G-M1's protocol and the command milestone's orders must carry what those verbs read (F5.5).

#### 28. F4.6: Formations for four, clicks on one pixel, keys taken

- Target: §9 (selection, orders), §11.1; ADR-017; SpaceScene §13
- Tier: Execution
- Severity: Minor (F4.6 raised it as Major)
- Confidence: High
- Problem: §9 says "a group moves in formation, which ADR-017's slots already fly". They fit no group of fifty:
  - ADR-017 has three slots, a flight of four of one class.
  - It holds a wingman between half and 1.25 times its own cruise, so frigates cannot keep station on a leader at 20 units/s.
  - Its tail slot, (0, 0, −2.4 D), sits directly behind the leader at its height, and fires through it (§8.2).

  Picking fails too. A click must land on the exact pixel (§9), and a 16-pixel frigate is easy to miss. In battle the nearest voxel is often debris, kept for ever by default (S19). And control groups need the number keys that SpaceScene §13 gives to debug views.
- Evidence: ADR-017's slots and a wingman's range; ADR-018's keys (1 to 6, E, R, Space). Precedent: Warzone 2100's commanders lead the units assigned to them, and its control groups sit on Ctrl+number. Homeworld's formations arrange a selected group as one shape.
- Proposal:
  - The squadron is the unit of command: ADR-017's flight extended to staggered rows of about a dozen ships of one speed class, with no slot in another's line of fire.
  - Picking runs on the CPU against each entity's projected box, grown to at least about 10 px, and never picks debris or wrecks.
  - The debug keys move behind a modifier, and 0 to 9 become control groups.
  - The skirmish sets a lifetime for debris (F6.5).
- Cost of change: The command milestone, an amendment of ADR-017, and SpaceScene §13.
- Falsification: In the battle at fifty a side, ships given one move arrive together in three-slot flights, leaders take negligible hits from their own wingmen, and clicks land where the owner meant.
- Adjudication: Not sent to the advocate. The lead weakens it to Minor, because the command milestone's design would extend the flights in any case. What the concept must correct is §9's claim that ADR-017's slots already fly a group, since three slots and a tail in its leader's line of fire do not.

#### 29. F5.7: The opponent's library is frozen while the player's learns

Adds F1.1's point that one author makes both libraries.

- Target: §2, §10 ¶2, §12.2 (G-M6's note), §14 ("The brick and the needle"); G5
- Tier: Execution
- Severity: Major
- Confidence: Medium
- Problem: Within a match, the opponent's designs meet the player's as equals. Across matches they do not. The player's library learns, while the opponent's is whatever the owner shipped. The opponent can answer a new design only by choosing among its own designs and refitting, so a design that beats everything it fields beats it every match, the same way. The library's author is also its only opponent, and knows every hull in it. And G-M6's rules are to be "the ones the skirmish has tested" (§12.2): tested against one frozen library.
- Evidence: §10; §2; §5.2 (both libraries come from MagicaVoxel until G-M6); §12.2. Precedent: Starsector's autofit fills authored variants with the weapons on hand, and Stellaris's AI designs take up new components over authored sections. In both, the hulls an AI is given bound what it can answer with.
- Proposal:
  - The opponent fields any library named on the command line, its own or the player's. That makes F6.2's batch a test of designs apart from anyone's play.
  - At each size class, its own library holds at least three designs with distinct profiles, and the seed draws which it fields.
  - Claude Code authors that library, unseen by the owner.
  - A CI test checks that every design in it validates, fits within power, and can be reached by research or a trader.
- Cost of change: Two sentences of §10, and a parameter and a test in G-M5. Cheap now or later.
- Falsification: G-M5's batch and the owner's matches find no player design that beats every design the opponent can field.
- Adjudication: Not sent to the advocate. It stands at Medium, and ranks low because the fix is cheap at any time. Letting the opponent field the player's library is the cheapest test of shape the plan could have.

#### 30. F4.3: The bands' job depends on friendly fire, and their spacing is unset

Adds F3.7's notes on bands.

- Target: §3 ("Movement"), §5.4, §8.2, §9; G7
- Tier: Owner decision (G7)
- Severity: Minor (F4.3 raised it as Major)
- Confidence: Medium
- Problem: G7's bands have a job only by implication. §8.2's sweep includes a side's own ships, so a ship one band up fires over its own front rank, and §5.4's arcs make height decide which mounts bear. If the shot contract lets a side's shots pass its own hulls (F3.2), that job vanishes. The command design owns the bands' count and spacing (§3). It must set them against weapon ranges, since bands wider than a laser's reach dodge it, and against hull heights, since bands closer than a tall hull let hulls interpenetrate (F3.7). §9's drop line has had no ground to meet since S-M1, and from straight above the bands are 0 px apart.
- Evidence: G7, §3, §5.4, §8.2 and §9; ADR-017, which places stations up to 250 units off the middle; SpaceScene §3.1, which retired the ground. Precedent: Homeworld sets height with Shift on its movement disc. Elite's scanner stands contacts on stalks over a drawn ellipse, which reads because the plane is drawn. Star Wars: Empire at War keeps its space battles on one plane.
- Proposal: Three alternatives for the owner.
  - **(A) Keep G7 and give the bands a stated job.**
    - The job: firing over one's own ships, and bringing dorsal or ventral mounts to bear. This requires that a side's shots stop at its own hulls (question 24).
    - Three bands, spaced to clear the largest banked hull and kept below the shortest weapon's range.
    - A group's band is a standing setting, and a grid is drawn under the selection.
  - **(B) One plane for the slice.** Bands come after combat, if friendly ships block fire.
  - **(C) G7 as written.**
- Cost of change: §3 and §9; the command and combat milestones. Cheaper before command starts.
- Falsification: The battle bench shows fleets of fifty on one plane losing only a few percent of their shots to their own ships, and no design gaining from a band. Then (B) is right.
- Advocate: Weakened. "G-M2's command design, to which §3 defers the bands' count and spacing, should state the job §5.4 and §8.2 already give them and space them against weapon ranges and hull heights."
- Adjudication: Weakened. §8.2's sweep and §5.4's arcs already give the bands a job, and §3 defers their count and spacing on purpose, so G7 stands and (A) is recommended. What survives is that the job exists only while a side's shots stop at its own hulls, which is question 24.

#### 31. F4.8: The HUD has no schedule past G-M2

- Target: §9 (the HUD), §12.2; D9, D13; §14 ("The UI is the tree's own")
- Tier: Execution
- Severity: Minor (F4.8 raised it as Major)
- Confidence: Medium
- Problem: §9 lists the HUD as credits, the selection, the two queues, the minimap and the refit screen. G-M2 delivers "the HUD's first panels", and no later milestone names a panel. The slice's own rules need more:
  - station power (G26);
  - a trader's state (G25);
  - a way to choose among twenty research items (§7.2);
  - building at a connector, in quarter turns (G9);
  - alerts;
  - F4.2's overlay.

  Under D9, every one of these is built from the canvas's rectangles and text. So the list is also the specification of the owner's toolkit.
- Evidence: §12.2's table. ADR-010 and `NeuronClient/Canvas.h`: `FillRectangle`, `Print`, `Measure`. ADR-018: an order shows only after the next tick plus 100 ms, so the client must draw its own order marker at once. Precedent: Total War pauses a battle so that a single player can give orders, and ADR-015's pause already freezes the world while commands still apply.
- Proposal: The minimum HUD, milestone by milestone:
  - **Command:** the overlay, the selection panel, the control groups, the order bar and the minimap.
  - **Combat:** bars, markers, alerts and inspect.
  - **G-M4:** credits and income, power, the shipyard, the lab's list, the trader, build mode and the refit screen.
  - **G-M5:** fog on the minimap, and the end screen.

  The toolkit supplies:
  - a panel, a label, a list, a bar and a tooltip;
  - a button, with a hotkey or with the reason it is disabled;
  - a world marker and a line;
  - input routing.

  The skirmish also allows orders while paused.
- Cost of change: §9 and §12.2's rows.
- Falsification: The owner plays G-M5's skirmish to its end with §9's five items and never misses an unpowered structure, a lost trader or a finished project.
- Adjudication: Not sent to the advocate. The lead weakens it to Minor by the second test: the panels arrive with their milestones either way. What the finding adds is the specification and the schedule for §14's risk.

#### 32. F6.5: §11.2 sizes the first clash, not the match

Adds F2.2's list of omissions.

- Target: §3, §7.1, §8.3, §11.2; SpaceScene S19
- Tier: Execution
- Severity: Minor (F6.5 raised it as Major)
- Confidence: High
- Problem: §11.2 counts whole fleets and structures, and so sizes a battle's first clash, not a match. It leaves out:
  - the wrecks each cut adds, as new entities and placements (§8.3);
  - the debris of every loss, which S19 keeps for ever by default and SpaceScene §7.6 draws without the levers, making it "the heaviest workload" (SpaceScene §17);
  - the asteroid fields, whose mined asteroids are damaged placements and whose voxels at parity outnumber every ship's (F2.2);
  - the traders;
  - the shot, damage and mining events.

  Debris only accumulates, so the worst frame comes late. And "within the default space scene's 1,033,408" compares against a workload never measured on a GPU.
- Evidence: §3, §7.1, §8.3 and §11.2; SpaceScene S19, §5.5, §7.6, §7.7 and §17. ADR-017 sets no debris lifetime by default. SampleRendererPerformance.md is the one GPU measurement, and it covers one station.
- Proposal:
  - The skirmish sets a lifetime for debris, which S19 allows, and one for wrecks. Each starts at a minute and is tuned on the bench.
  - §11.2 gains a row for the match's worst frame.
  - The battle bench runs through the aftermath and reports its worst frame beside its median.
- Cost of change: A row in §11.2, a line in §8.3, and the bench's timeline. Free now.
- Falsification: The battle bench shows the aftermath's worst frame within budget with debris kept for ever.
- Adjudication: Not sent to the advocate. The lead weakens it to Minor, because the bench would find the late frame in any case. The lifetimes remain a skirmish setting the concept should name.

#### 33. L2: Separation steering meets ADR-017's foreclosure

Adds F3.7's point on bands.

- Target: §9 ("Paths"), §13
- Tier: Execution
- Severity: Minor
- Confidence: High
- Problem: §9 has ships find paths around keep-outs, fly them "with ADR-017's flight model", and "keep apart from one another by steering". But ADR-017 forecloses "a ship that steers across its route". Its keep-out guarantee rests on every ship aiming along its route within its slot's reach, and separation is steering off the route. §13 lists amendments to SpaceScene, NVF, ADR-015, ADR-003 and AGENTS.md, but not ADR-017. Separation must also hold across bands, or hulls in adjacent bands interpenetrate (F3.7).
- Evidence: §9 and §13; ADR-017's "What this forecloses" and "Why a wingman steers along its route". Before that rule a wingman went 7.1 units into a keep-out; after it, the closest was 14.6 units clear.
- Proposal: §13 gains ADR-017. The command milestone's ADR amends ADR-017's flight and lifts the foreclosure. It keeps the keep-out guarantee as a hard constraint on separation, holds separation in three dimensions across bands, and keeps ADR-017's ten-minute test as a regression bound.
- Cost of change: A sentence in §13 now. The ADR is needed either way.
- Falsification: Separation keeps every ship out of every keep-out through ADR-017's ten minutes of detonations.
- Adjudication: The lead's Phase 0 finding. It stands.

#### 34. L3: The draw count assumes cascades the slice defers

- Target: §11.2; §14 ("Placements, not voxels"); G22
- Tier: Execution
- Severity: Minor
- Confidence: High
- Problem: §11.2 reaches 3,960 draws a frame by counting "the camera and S-M7's three cascades", and §14 repeats the figure. G22, as amended on 2026-09-28, still defers S-M7 past the slice, and until then the scene keeps one shadow map (SpaceScene §10). So the slice's worst case is 990 × 2 = 1,980 draws, and the battle's bench would otherwise be judged against a frame the slice never draws.
- Evidence: §11.2, §14, G22 and §12.1. SpaceScene §10: "Until S-M7 the space scene keeps one map".
- Proposal: §11.2 reads "… up to 1,980 draws a frame with the one shadow map the slice keeps (G22), and 3,960 once S-M7's three cascades land."
- Cost of change: Text, now.
- Falsification: None is needed: it is arithmetic.
- Adjudication: The lead's Phase 0 finding. It stands.

#### 35. L4: G-M1 lists work that is done

- Target: §5.3, §12.1, §12.2 (G-M1)
- Tier: Execution
- Severity: Minor
- Confidence: High
- Problem: §5.3 says "N-M1 builds NVF's voxel record" and speaks of "once N-M1 has landed". G-M1 delivers "N-M1, N-M2 and loading `.nvf`". But NVF §10 records N-M1 and N-M2 as done on 2026-09-28 (ADR-019, ADR-020), and N-M3 and N-M4 as built. So G-M1 overstates what remains. What does remain, `Outpost.exe` loading `.nvf`, is "a separate design change to SampleRenderer §7" (NVF §10), and G-M1 does not name it.
- Evidence: §5.3, §12.1 and §12.2; NVF §10's table and its "later" row; ADR-019 and ADR-020.
- Proposal: §5.3 moves to the past tense. G-M1 opens with loading `.nvf` in `Outpost.exe`, and with that change to SampleRenderer §7 (NVF §10).
- Cost of change: Text, now.
- Falsification: None is needed: it is record-keeping.
- Adjudication: The lead's Phase 0 finding. It stands.

#### 36. F2.7: Twenty items and uncapped labs make research an order, not a choice

- Target: §2, §6 (the lab), §7.2, §10; G2
- Tier: Execution
- Severity: Minor (F2.7 raised it as Major)
- Confidence: Medium
- Problem: The number of labs is unstated (§6, §7.2), and a normal match researches all of the tree's twenty or so items (§2). With three labs and 60 to 120 s a project, the tree is done 7 to 13 minutes after the labs stand, in a match of 20 to 40 minutes. So the choice is when to research, not what, and a choice of when can be solved. A refinery-yield upgrade multiplies every credit still left in a finite stock (G19), so it is worth most at minute one. The library sets the rest (F1.2).
- Evidence: §2, §6, §7.2, §10 and G19. The lab count and the project times are the reviewer's assumptions. Precedent: Warzone 2100's four hundred or so technologies outlast a skirmish, so its research is a choice of lines, and its skirmish caps research facilities (the reviewer recalls five, unverified). Company of Heroes has each player pick one of three doctrines a match.
- Proposal:
  - Cap the labs at two or three.
  - Size the tree at 1.5 to 2 times what a side can finish in 30 minutes.
  - Price the industry upgrades so that they pay back only after about ten minutes.
- Cost of change: G-M4's research design. Cheap now or later.
- Falsification: Openings differ by library in G-M4 and G-M5, and no single first project wins most matches.
- Adjudication: Not sent to the advocate. The lead weakens it to Minor by the second test: G-M4 tunes the tree whatever is decided now. What lasts is the missing cap on labs.

### 4. Challenges to owner decisions

Four findings challenge the owner's own decisions, G1 to G11. Each is set out below with its alternatives and what each would cost, and none is applied here. Separately, Part 2 proposes changes to G20, G21, G22, G23, G25 and G26. Those are derived decisions, but the owner took them on 2026-09-28, so every such change is marked as requiring his decision.

| Decision | Finding | Alternative | What it would cost | The review's view |
|---|---|---|---|---|
| G5 | F1.2 | (a) Each palette entry pairs a color with a material class, which the side's best researched material fills | G13 changes. G-M1's game data holds classes, the catalogue maps materials to classes, and the designer paints classes. R14 and NVF are untouched. | Recommended |
| G5 | F1.2 | (b) A refit re-armors | G5, G12 and G13 change. A refit prices voxels by material, the refit screen gains materials, and a refit takes longer. | — |
| G5 | F1.2 | (c) Research upgrades armor already fielded, as Warzone's alloys do | No decision changes. §5.3 and §7.2 recast materials as lines of upgrades. Cheapest in code, but a material stops being a design choice. | — |
| G5 | F1.2 | (d) G5 as it stands | Nothing now. The library sets the research order, as §14's first risk already accepts. | — |
| G6, G11 | F6.4 | Decide the ladder now: lifetimes and levers first, then G11's count or the largest class, and a coarser damage cell last, only with a coarse build grid | One paragraph of §14 now. Fewer ships or smaller hulls if the battle's gate fails. | Recommended |
| G6, G11 | F6.4 | Keep fifty a side, and coarsen both grids to 2³ | Designs are sculpted in bricks of eight. Validation and the designer change, and the fine sculpting G3 promises is lost. | — |
| G6, G11 | F6.4 | Keep G6 and G11, and accept 30 frames a second | Nothing now. The slice plays at half the frame rate on the reference machine. | — |
| G7 | F4.3 | (A) Keep G7, and state the bands' job: firing over one's own ships, and bringing dorsal and ventral mounts to bear | Needs a side's shots to stop at its own hulls (question 24). The command milestone pays for bands in orders, paths and formations. | Recommended if friendly fire is on |
| G7 | F4.3 | (B) One plane for the slice | G7 is deferred, so command saves the bands' work, but the third dimension waits. | Recommended if friendly fire is off |
| G7 | F4.3 | (C) G7 as written | Command pays for bands whose use is unstated. | — |
| G10 | F2.1 | The dry-sector rule alone: the cores wear down once every field is dry | No change to G10. One rule is added beside G20. | Recommended |
| G10 | F2.1 | (a) Salvage: a wreck's voxels are low-grade ore | G10 reads "ore from asteroid fields and from wrecks", and §2's exclusion reverses. Combat's wrecks and G-M4's mining share one rule, and a failed attack pays the defender. | — |
| G10 | F2.1 | (b) The core yields a small, endless trickle of credits | G10's one currency gains a second source, which weakens the pull to the middle. | — |

No finding asks to reverse G1, G2, G3, G4, G8 or G9. F3.5's option of a further way to lose a ship touches G14, which is derived (§3, finding 12).

### 5. Dismissed findings

No finding was dismissed whole. These are the claims the adjudication dropped from findings that otherwise survive.

| Finding | Claim dismissed | Reason |
|---|---|---|
| F1.1 | The bet is tested last | The third of six milestones shoots authored hulls apart, and §14 names that milestone for the shape test. |
| F6.1 | Command carries the phase's least design risk | F4.2, F4.3 and F4.6 show that its three-dimensional parts are unproven. The swap stands on its own merit. |
| F2.1, F1.5 | The middle snowballs, with no way back | A contested middle is §4's intent. A failed attack costs the attacker its fleet, and G25 lets the loser deny the winner's trade. |
| F3.1 | Stacking multiplies everything, and is Critical | Mass, power and price scale with a stack, and one hit reaches every stacked layer. |
| F3.3 (in F1.2) | A library hull in the best armor fields at minute one | §5.3, §5.4 and §5.5 gate materials and classes by research. Only §7.2's sentence omits them. |
| F4.3 | Nothing uses the bands, and each order costs a second input | §8.2's sweep and §5.4's arcs give the bands a job, and a band modifier costs input only when a ship changes band. |
| F6.4 | The renderer is the cost most likely to bind, and has no exit | The extrapolated frame is 13.5 ms of a 16.7 ms budget. The levers, `ExecuteIndirect` and S19's lifetime are exits the concept names. |
| F2.3, F1.7 | Income waits on a forced chain of 20,000-voxel structures | 20,000 is §11.2's assumed average for its load arithmetic. A start without income structures is Warzone 2100's No Bases start. |
| F1.6 | A library core is a free fortress, and a siege can be endless | "Placed by the skirmish", in a start both sides share, reads as the skirmish's own design. Repair is paid from finite ore. |
| F2.6, F3.4 | Free module repair pumps credits and gets round G4's stock | A like-for-like swap is repair, which §5.6 prices apart from refit. Restoring a ship's own bought module adds none to play. |
| F4.2, F1.3 | Per-voxel damage is invisible where the commander plays | The camera zooms, and a battle framed near 1,375 units shows its damage. |
| F4.5, F5.1 | The concept means fog for snapshots alone | §9's guarantee covers every message. What fails is the wording of §7.1 and §8.2, and the mechanism. |
| F6.3 | The command milestone flies designs on ADR-017's class constants | §9 reuses ADR-017's mechanics, not its defaults. |
| F2.4, F3.6 | One picket switching a trader off is a defect, and §7.3 narrows G4 | Exclusivity is G25's intent, and it can serve a comeback. "One step beyond" covers a branch not yet started at its first mark. |
| F2.2 | At any price, the rational structure is a minimal shell | Protection is worth its price wherever a structure's loss matters. The defensible point, that a structure has one design axis, is carried by F1.6 and F3.3. |

### 6. Strengths to protect

| Strength | Why it must survive | Raised by |
|---|---|---|
| The opponent plays only through a session (G16, G27, R19) | Fairness comes from the structure itself. Two opponents stepped from a seed make the headless batch that every gate relies on. No fix may open a side door into `GameLogic` for paths or threats. | F5, F6, F3 |
| A seeded world in lockstep (S11; ADR-015's `Step`; ADR-018's bench) | Replays, the batch and every falsification test above depend on byte-identical runs. Nothing in the world or the opponent may read the wall clock. | F5, F6 |
| Sculpting off the clock (G5) | A voxel editor inside a running match would be unusable. Variants (F4.1) keep the match's choice a choice of template. No fix may reopen a hull mid-match. | F1, F4 |
| Performance from geometry (G18), with exactly one command mount (§5.1, §5.5) | This is what makes a hull a decision rather than a skin. The single mount closes the spare-core exploit that From the Depths builders use. A fix to burial adds a loss condition, never a spare. | F1, F3 |
| Connectivity carries damage's meaning (G6, G14, G26) | A severed neck and a cut connector read at any zoom, and give a siege targets besides the core. Any fallback for cost keeps face-to-face connectivity to the command module and to the core. | F1 |
| Function is catalogue data, and the server validates everything (N8, G12, G15, §5.5) | Every exploit fix in this review is a validation line or a number, with no change to R14 or NVF. Hit claims and map hacks cannot exist. | F3 |
| Mining is damage (G19); one currency (G10); a bought module is consumed and never taught (G4) | One machinery shrinks the fields on screen. One number fits a hand-built HUD. Research stays the only road to many ships. | F2 |
| The contested middle (§3, §4) | The economy creates the battles. Every economic fix must reward holding the middle, not replace it. | F1 |
| Designs before any editor (§5.2), and a plan that refuses to guess (§8.4, §11.2) | MagicaVoxel and `NvfImport`, checked in CI, feed the first battle. The plan measures before it optimizes, and the bit-for-bit test stays. | F6 |
| Few discrete bands, and the close cameras (G7; §3, §9; SpaceScene §7.3) | Discrete bands keep a third dimension commandable. The orbit and chase cameras, and the chain from a voxel's id to its module, are where per-voxel damage is seen. | F4 |

### 7. Questions for the owner

These continue §14's numbering. Each blocks an amendment in Part 2.

23. **Materials and research (F1.2).** Which should replace G5 as it stands: (a) material classes, filled by the best researched material; (b) refits that re-armor; (c) armor upgrades on the Warzone model? Or should G5 stay as it is? This blocks G13, §5.3, §7.2 and G-M1's game data.
24. **Friendly fire (F3.2, F4.3).** Do a side's shots stop at its own hulls? This blocks G39, the bands' job under G7, and the squadrons' rows (F4.6).
25. **Design internals (F6.6).** On first sighting an enemy ship, does a side receive its whole design and variant, shown by inspection? Or only its exterior, with the rest revealed as damage uncovers it? This blocks G31 and the delivery of designs in protocol version 2.
26. **Budgets (F6.2).** Is the slice's budget 16.7 ms a frame at 1920 × 1080 on the Adreno X1-85 in Release ARM64, and 8 ms of the server's tick? This blocks G32 and every gate.
27. **The ladder (F6.4).** When the battle's gate fails, what gives way first after lifetimes and levers: G11's count of ships, or the largest class? This blocks G44 and §14's rewrite.
28. **The dry sector (F2.1).** Should the rule that wears the cores down stand alone, or should salvage of wrecks or a trickle at the core come with it? This blocks G36 and §3's paragraph on victory. Salvage also reverses §2's exclusion.
29. **The order (F1.1).** Should command and combat swap, with whole placements benched before G-M1? This blocks §12.2's table and G22.
30. **Repairing structures (F1.6).** Should §3 lose "repaired", or should constructors repair structures, the core's command module excepted? This blocks G48, §3 and §6.
31. **The designer (F6.7).** Should G-M6 be split? If so, what time box should apply, after which the slice ships with MagicaVoxel as its hull tool? This blocks G45 and G23.

## Part 2 — Proposed amendments to GameConcept.md

These are proposals only, and none is applied. Each comes from a finding of Part 1 that survived adjudication, and each is marked either as requiring the owner's decision or as derived. A derived proposal follows from its finding, and the owner can accept it with the rest. Where a proposal waits on a question of Part 1 §7, it shows the choices in brackets.

### New decisions

| # | Decision | Source |
|---|---|---|
| G28 | Every milestone ends at a gate: a measured figure, a threshold, and the change of course it triggers (§12.2). The battle milestone's gate is the shape test: a brick, a needle and a spine of equal cost each beat another over seeded engagements. | Review, 2026-09-29 — proposed; derived (F1.1, F6.2) |
| G29 | A design's performance is one profile per design and variant, computed in the shared game library from its voxels, materials and modules. The profile holds mass, inertia, thrust, arcs, silhouette and the armor over the command module. Flight takes each ship's limits from it, the HUD shows it, the opponent reads it, and damage updates it. | Review, 2026-09-29 — proposed; derived (F6.3) |
| G30 | Protocol version 2 is per side. An entity reaches a side as a baseline when it first comes into that side's sight, and its changes follow. Shots, damage and mining out of a side's sensors are not sent to it. | Review, 2026-09-29 — proposed; derived (F6.6) |
| G31 | A seen enemy's design and variant reach a side [in full, shown by inspection \| as its exterior, with the rest revealed by damage]. | Review, 2026-09-29 — proposed; requires owner decision (question 25) |
| G32 | The slice's budgets are 16.7 ms a frame at 1920 × 1080 on the owner's Adreno X1-85, in Release ARM64, and 8 ms of the server's 33.3 ms tick. A headless batch of the opponent against itself measures match length and shape. | Review, 2026-09-29 — proposed; requires owner decision (question 26) |
| G33 | Where a weapon aims on its target, and how a ship turns to bring its arcs to bear, are rules of combat, the same for every side. The battle milestone's design chooses them and names the hull each favors. | Review, 2026-09-29 — proposed; derived (F1.4) |
| G34 | Each session is bound to a side. The server refuses, by name, any command that names another side's entity, structure, design or stock, and the command set holds every order the HUD can give. | Review, 2026-09-29 — proposed; derived (F5.5) |
| G35 | A variant is a named fit of a design. The library keeps several per design, the refit screen can make one in the match, and production and refit orders name a variant. | Review, 2026-09-29 — proposed; derived (F4.1) |
| G36 | Once every field is dry, each core's command module loses voxels at a fixed rate, so every match ends. The sector's ore is sized so that this rarely happens. | Review, 2026-09-29 — proposed; requires owner decision (question 28) |
| G37 | A design is refused when two of its parts put a voxel in one cell, when two mounts' boxes share a cell, or when a mount's box shares no face with a hull voxel. | Review, 2026-09-29 — proposed; derived (F3.1) |
| G38 | A weapon, a thruster or a sensor works only along lines clear of its own hull. | Review, 2026-09-29 — proposed; derived (F3.5) |
| G39 | A shot meets the enemy's hulls and asteroids [and its own side's hulls \| but passes its own side's hulls]. It passes wrecks and traders, and spends its damage along its path. A weapon's shot removes no ore, and a mining laser meets asteroids only. | Review, 2026-09-29 — proposed; requires owner decision (question 24) |
| G40 | A size class is a voxel budget within a box, for ships and structures alike. | Review, 2026-09-29 — proposed; derived (F3.3) |
| G41 | The opponent is stepped with the server once a tick, at one fixed point. The server logs every command it applies, so a skirmish replays from its seed, its build, its libraries and that log. | Review, 2026-09-29 — proposed; derived (F5.6) |
| G42 | An ore voxel buys a fixed number of hull voxels. G-M4 sets the rate, with a budget table of income, prices and the fields' totals. | Review, 2026-09-29 — proposed; derived (F2.2) |
| G43 | A design's build order is derived at load, breadth-first through face neighbors from its command mount's cell. Voxel ids, masks and damage events follow it, and the file keeps its own order. | Review, 2026-09-29 — proposed; derived (L1) |
| G44 | When a gate finds per-voxel damage too dear, lifetimes and levers give way first, then [G11's count \| the largest class]. A coarser damage cell comes only last, and only with an equally coarse build grid. | Review, 2026-09-29 — proposed; requires owner decision (question 27) |
| G45 | The designer comes in two halves: its sculpting core beside the first match, and its rule-bound half last. The slice ships with MagicaVoxel as its hull tool if the core misses [a date the owner sets]. | Review, 2026-09-29 — proposed; requires owner decision (question 31) |
| G46 | When power drawn exceeds power supplied, modules and structures stop in a set order, weapons last. A connector joins only the two structures built at it. | Review, 2026-09-29 — proposed; derived (F3.8) |
| G47 | An attack order may enter its target's keep-out, and stops at its weapon's range, measured to the target's nearest voxel. | Review, 2026-09-29 — proposed; derived (F3.7) |
| G48 | The station core is the skirmish's own design, the same for both sides, with a fixed number of connectors. [Constructors repair structures, the core's command module excepted \| Structures are not repaired.] | Review, 2026-09-29 — proposed; requires owner decision (question 30) |
| G49 | Every ship shows its command module's integrity and its modules' state. Beyond the battle zoom, ships and structures draw as icons. | Review, 2026-09-29 — proposed; derived (F4.2) |
| G50 | A skirmish sets lifetimes for debris and for wrecks. | Review, 2026-09-29 — proposed; derived (F6.5) |

### Changed decisions

G7 stays as it is (F4.3). Its job goes into §3 and §9, once question 24 is answered.

| # | Existing row | Proposed row | Needs |
|---|---|---|---|
| G5 | Designs are made outside the match, into a library the player keeps. In the match the player fields designs from it and refits their modules; research and traders decide which can be fielded. | Only if question 23 takes (b): Designs are made outside the match, into a library the player keeps. In the match the player fields designs from it and refits their modules and armor; research and traders decide which can be fielded. | Owner decision (question 23) |
| G10 | The economy runs on one resource: ore from asteroid fields, refined into one currency that pays for research, production, armor and trade. | Only if question 28 takes salvage: The economy runs on one resource: ore from asteroid fields and from wrecks, refined into one currency that pays for research, production, armor and trade. | Owner decision (question 28) |
| G11 | A battle is dozens of ships a side, up to about fifty. | A battle is dozens of ships a side, up to about fifty. If the battle's gate finds per-voxel damage too dear, the count gives way before the damage cell does (G44). | Owner decision (question 27) |
| G12 | A design is a hull and its mounts; a fit is the module at each mount. The hull and its mounts are made in the library, the fit in the match. | A design is a hull and its mounts; a fit is the module at each mount, and a variant is a named fit. The hull and its mounts are made in the library; variants are kept there and made in the match (G35). | Derived (F4.1) |
| G13 | Each of a design's sixteen palette entries pairs a color with a material, so R14's record and palette stay as they are. | If question 23 takes (a): Each of a design's sixteen palette entries pairs a color with a material class, which the side's best researched material of that class fills when the design is built or refitted, so R14's record and palette stay as they are. | Owner decision (question 23) |
| G20 | A side loses when its station core is destroyed. | A side loses when its station core is destroyed. The core is the skirmish's own design (G48), and once every field is dry it wears down (G36). | Owner decision (questions 28, 30) |
| G21 | Fog of war is in the slice: each side's snapshots hold only what its sensors reach, which amends ADR-015. | Fog of war is in the slice: every message a side receives holds only what its sensors reach (G30), which amends ADR-015 from protocol version 2. | Owner decision (the owner took G21) |
| G22 | SpaceScene's S-M6 and S-M7 wait until after the slice, and S-M5, which was to wait with them, is finished first; S-M8 and S-M9 move into G-M3, revised for a battle and for damage. | SpaceScene's S-M6 and S-M7 wait until after the slice, and S-M5, which was to wait with them, is finished first. S-M8's bench runs on whole placements before G-M1 and moves into the battle milestone, revised for damage, and S-M9's levers follow the battle's gates. | Owner decision (question 29) |
| G23 | The in-game designer is the phase's last milestone; until it lands, designs are authored in MagicaVoxel and imported. | The in-game designer comes in two halves (G45): its sculpting core beside the first match, and its rule-bound half last. Until the core beats MagicaVoxel, designs are authored there and imported. | Owner decision (question 31) |
| G25 | Traders cannot be attacked in the slice, and deal with a side only while it alone has a ship within their trade radius. | Traders cannot be attacked in the slice, and deal with a side only while it alone has a ship within their trade radius. A trader shows every ship within its radius to each side with a ship there, and sells each side from a stock of its own. | Owner decision (the owner took G25) |
| G26 | A station's structures share power through their connectors, and a structure cut off from the core is unpowered. | A station's structures share power through their connectors, which join two structures each, and a structure cut off from the core is unpowered. When draw exceeds supply, structures stop in a set order, turrets last (G46). | Owner decision (the owner took G26) |

### Text changes

| Section | Before | After |
|---|---|---|
| §3, the start | Each side starts with a station core, two constructor ships, a small escort and no research. | Each side starts with a station core, two constructor ships, a small escort, a stated bank of credits, and no research beyond tier zero (§7.2). |
| §3, victory | The core can be defended, repaired and surrounded by structures, but not rebuilt, so every match has an end. | The core is the skirmish's own design, the same for both sides (G48). It can be defended and surrounded by structures, and repaired by constructors if question 30 keeps repair, but not rebuilt. Once every field is dry, each core's command module wears down (G36), so every match has an end. |
| §5.1 | A **fit** is the module at each mount. | A **fit** is the module at each mount, and a **variant** is a named fit, of which the library keeps several for each design (G35). |
| §5.1 | …and the match asks only for Warzone's kind of choice: which module, in which slot, in a few seconds. | …and the match asks only for Warzone's kind of choice: which variant to build, and which to refit to. |
| §5.3 | …so N-M1 builds NVF's voxel record as NVF §4 has it. If sixteen pairs prove too tight, those bits remain the way out, but once N-M1 has landed they are a major version of NVF (NVF §4.6) as well as a change to R14. | …so N-M1 built NVF's voxel record as NVF §4 has it (ADR-019). If sixteen pairs prove too tight, those bits remain the way out, but they are now a major version of NVF (NVF §4.6) as well as a change to R14. |
| §5.3, if question 23 takes (a) | …each of a design's sixteen palette entries pairs a color with a material, a design has at most sixteen color–material pairs, and a voxel's material is its entry's. | …each of a design's sixteen palette entries pairs a color with a material class, light or heavy, and a voxel's material is the side's best researched material of its entry's class when the design is built or refitted (G13). |
| §5.4, arcs | **Arcs.** A weapon fires only where its line of fire is clear of its own hull, so a gun buried in armor does not fire, and one on a spine fires almost everywhere. | **Arcs.** A weapon fires, a thruster thrusts and a sensor sees only along lines clear of its own hull (G38), so a gun or an engine buried in armor does nothing, and one on a spine works almost everywhere. |
| §5.4, size classes | **Size classes.** A design's bounding box sets its class, which decides the shipyards that can build it; research unlocks the larger classes. | **Size classes.** A class is a voxel budget within a box (G40), which decides the shipyards that can build a design; research unlocks the larger classes. |
| §5.5 | …when a mount's box holds a hull voxel, or its rotation is not one of the cube's 24; | …when a mount's box holds a hull voxel, shares a cell with another mount's box or shares no face with a hull voxel, or its rotation is not one of the cube's 24; when two of its parts put a voxel in one cell (G37); |
| §5.6, the build | …its hull's records are stored in build order, outward from the command module's mount, and its placement draws a growing prefix of them… | …server and client derive its build order at load, breadth-first through face neighbors from the command module's mount (G43), and its placement draws a growing prefix of its records in that order… |
| §5.6, refit and repair | A refit swaps modules at a shipyard, for the difference in price and some time. A shipyard also repairs a ship docked at it, restoring its lost voxels at a price per voxel. | A refit swaps modules at a shipyard for some time, charging the incoming modules in full and refunding [a share of \| nothing for] the outgoing. A shipyard also repairs a ship docked at it, restoring its lost hull voxels at their material's price and a damaged module at its price times the share of it lost. A failed module is replaced by a refit, and a design under construction does nothing until it is complete. |
| §6, the core | The **station core** comes first, placed by the skirmish; | The **station core** comes first: the skirmish's own design, the same for both sides, with a fixed number of connectors (G48); |
| §6, power | Every reactor module in a structure connected to the core supplies the whole station, and every structure draws from it (G26). | Every reactor module in a structure connected to the core supplies the whole station, and every structure draws from it (G26). A connector joins only the two structures built at it, so a station is a tree rooted at the core, and when draw exceeds supply, structures stop in a set order, turrets last (G46). |
| §7.1, mining | What mining removes reaches the clients as it happens, as damage does (§8.2). | What mining removes reaches each side whose sensors see it, as damage does (§8.2, G30). |
| §7.1, grades (optional, F2.5) | (none) | Ore is graded by its asteroid's palette entry, as catalogue data, and the richer fields hold the higher grades; a refinery module lets a ship serve as a drop-off. |
| §7.2, labs | Labs research, one project each at a time, for credits and time. | Labs research, one project each at a time, for credits and time, and a side may build at most three. A side starts with tier zero: one material, and the command module, reactor, thruster, weapon, mining laser, cargo hold and constructor bay its start needs. |
| §7.2, fielding | A design whose fit needs a module the side has neither researched nor bought cannot be fielded; the library keeps it until it can be. | A design cannot be fielded until the side has researched its class and its materials, and researched or bought every module of its variant; the library keeps it until it can be. |
| §7.3 | In the slice traders cannot be attacked (G25). | In the slice traders cannot be attacked (G25). A trader shows every ship within its radius to each side with a ship there, and sells each side from a stock of its own. |
| §8.1 | Ships choose targets in range for themselves; the player's orders choose for them (§9). | Each weapon has a range, measured from its muzzle to the target's nearest voxel (G47). Ships choose targets in range for themselves; the player's orders choose for them (§9). Where a weapon aims, and how a ship turns to bring its arcs to bear, are rules the same for every side (G33). |
| §8.2, delivery | A shell's flight is a pure function of those and the time, so every client draws the same shell, a client that joins late included, and nothing about it is sent twice. | A shell's flight is a pure function of those and the time, so every side that sees it draws the same shell, from the tick it enters that side's sensors (G30). |
| §8.2, the sweep | It sweeps each shell, tick by tick, through the grids of the designs in its way, | It sweeps each shell, tick by tick, through the grids of the designs and asteroids in its way, by G39's contract, |
| §8.3 | …and they last as long as the world's debris lifetime allows (S19). | …and they last as long as the skirmish's lifetimes for debris and wrecks allow (S19, G50). |
| §9, the camera | **The camera** looks down on the sector's plane: it pans, zooms, turns and tilts. | **The camera** looks down on the sector's plane: it pans, zooms, turns and tilts, between a battle zoom, where a voxel covers at least a pixel, and a sector zoom, where ships and structures draw as icons (G49). |
| §9, orders | A group moves in formation, which ADR-017's slots already fly. | A group moves in formation, as squadrons of up to about a dozen ships of one speed class whose staggered rows extend ADR-017's slots. An attack order may enter its target's keep-out, and stops at range (G47). |
| §9, paths | Ships find paths around the keep-outs, steer along them with ADR-017's flight model (pure pursuit, bank and slot), and keep apart from one another by steering, not by collision. | Ships find paths around the keep-outs, steer along them with ADR-017's flight model (pure pursuit, bank and slot) within their own profiles' limits (G29), and keep apart from one another by steering, in three dimensions and never into a keep-out, which amends ADR-017 (§13). |
| §9, fog | Each side sees what its ships' and structures' sensors reach, and its snapshots hold only that (G21). | Each side sees what its ships' and structures' sensors reach, and every message it receives holds only that (G21, G30). |
| §9, fog | This is the one place the concept asks the snapshots to differ by session, which ADR-015 forecloses today (§11.1). | So protocol version 2 is encoded per session from G-M1, which ADR-015 forecloses today (§11.1). |
| §9, the HUD | **The HUD:** the side's credits, the selection, the queues of production and research, the minimap, and the refit screen. | **The HUD:** the side's credits and power, the selection with each ship's condition (G49), the control groups, the queues of production and research, the minimap, the traders' state, alerts, and the refit screen, each landing with the milestone that needs it (§12.2). |
| §10, the thread | It runs in the server's process, on a thread of its own, or stepped with the server as the bench steps it (ADR-018), which keeps a skirmish repeatable from its seed within one build. | It runs in the server's process, stepped with the server once a tick at one fixed point, as the bench steps it (ADR-018), and the server logs every command it applies, so a skirmish replays from its seed, its build, its libraries and that log (G41). |
| §10, its play | It fields an authored library, designs made as the player's are (§5.2), and researches and trades by the same rules to reach them. Its play in the slice is one level: expand to the fields, keep its economy running, research toward its library, defend, and attack in waves once its fleet is strong enough. | It fields any library the command line names, by default its own, which Claude Code authors unseen by the owner, and researches and trades by the same rules to reach its designs. Its play in the slice is one level: expand to the fields, keep its economy running, research toward its library, remember what it has seen, hold a trader's radius, defend, and attack in waves once its fleet outweighs the enemy it remembers. |
| §11.2, placements | Seen by the camera and S-M7's three cascades, that is up to 3,960 draws a frame, one `DrawIndexedInstanced` each as `SplatPass` issues them today. | Seen by the camera and the one shadow map the slice keeps (G22), that is up to 1,980 draws a frame, and 3,960 once S-M7's three cascades land, one `DrawIndexedInstanced` each as `SplatPass` issues them today. |
| §11.2, a new item | (none) | **The match's worst frame** comes late: live ships, structures and fields, the wrecks and debris their lifetimes keep (G50), and the events of each second. The battle's bench reports it beside its median. |
| §13 | **ADR-015** is amended by protocol version 2 and by snapshots that differ by side (G21). **ADR-003** is amended by the shared game library and the opponent's project, each with an ADR of its own. | **ADR-015** is amended by protocol version 2, encoded per side, with sessions bound to sides and a command log (G30, G34, G41). **ADR-003** is amended by the shared game library and the opponent's project, each with an ADR of its own. **ADR-017** is amended by flight on each design's profile and by separation (G29, §9). **ADR-010** is amended by a world-anchored overlay (G49). |
| §14, per-voxel damage | If G-M3's bench finds it too dear at fifty a side, the fallbacks are fewer, larger hits, or damage in coarser cells; module hit points are not one, since G6 rules them out. | If the battle's bench finds it too dear against G32's budgets, the fallbacks come in G44's order: lifetimes and levers, then fewer ships or smaller hulls, and damage in coarser cells only last, and only with an equally coarse build grid. Module hit points are not one, since G6 rules them out. |

§11.1 gains five rows, in its own format:

| Change | Why | What it amends |
|---|---|---|
| Each design's profile (mass, inertia, thrust, arcs, silhouette and armor), computed once in the shared game library | G18, G29 | ADR-017's class table |
| Protocol version 2 encoded per session: a baseline on first sight, then the changes; sessions bound to sides | G21, G30, G34 | ADR-015 |
| The command set: every order of §9, plus production, refit, repair, research, trade and building. A side's own credits, research, queues and stock go in its snapshots. | G16, G34 | ADR-015's commands |
| A log of every command the server applies, with its world tick and its session | G41 | ADR-015 |
| A world-anchored overlay: icons, bars, marks and lines | G1, G49 | ADR-010 |

### Milestones

The table assumes that question 29 swaps command and combat. If the owner keeps the order, the rows keep their contents: G-M2 and G-M3 swap back, and repair stays in G-M4, where its shipyard is.

| | Delivers | Done when |
|---|---|---|
| G-M0 | Unchanged | Unchanged |
| G-M1 | **Designs as data.** `Outpost.exe` loads `.nvf` (NVF §10's follow-up, with its change to SampleRenderer §7). The shared game library holds designs, mounts, variants, materials and the module catalogue. Each design has its profile (G29), and a command-line tool prints it. Validation adds G37, G38 and G40. Protocol version 2 is per side, with sessions bound to sides (G30, G34). The record buffer grows. The sector's ships are rebuilt as fitted designs authored in MagicaVoxel, flying on their profiles. | The sector flies and draws fitted designs on their own profiles. Each design crosses the loopback once. The design and protocol tests are green. Gates 1 and 2 are met. |
| G-M2 | **Combat** (formerly G-M3). Weapons have ranges, with G39's contract and G33's rules. Shots and damage are per-side events, with masks, module failure, the command module's loss and wrecks. Lifetimes are set (G50). The bench is revised to a battle (S-M8), with a headless runner. The opponent's project starts as the bench's scripted second side. | Two fleets fight to the end. In the tests, the client's missing voxels are the server's, bit for bit, with entities leaving and re-entering view. The battle's note is committed against G32's budgets. Gates 3, 4 and 5 are met. |
| G-M3 | **Command** (formerly G-M2). The strategic camera has its battle and sector zooms, with icons (G49). It brings picking and selection, control groups, orders on the plane and in bands, paths and separation (amending ADR-017), squadrons, and the condition readout. The HUD gets its first panels (F4.8). S-M9's levers on damaged placements come in if gate 4 calls for them. | The owner commands fleets of fifty around the sector, and gate 6 is met. |
| G-M4 | **Economy and stations.** Asteroids, mining and refineries, with credits at G42's rate. The station core (G48), and structures built at connectors with power (G46). Production, refit to variants, and repair. The research tree, with its cap on labs. Traders. | One side runs the loop from ore to a fitted design fielded in battle. The first credit arrives within a minute. The budget table is committed. Gate 7 is met. |
| G-M5 | **The skirmish.** The opponent plays through its session, with memory and radius-holding (F5.2), and is stepped and logged (G41). Fog of war, victory with G36's rule, the skirmish's layout from a seed, and the batch's telemetry. | The owner plays a skirmish against the opponent to its end. The opponent kills a passive side's core within 40 minutes on 10 seeds of 10. Mirror matches end within 20 to 40 minutes on 7 seeds of 10. A logged match replays to the same bytes. Gate 8 is met. |
| G-M6 | **The designer.** If question 31 splits it (G45), it comes in two halves: the sculpting core, built beside G-M4, and the rule-bound half, last. | The owner builds a new 1,000-voxel design, with mounts and materials, faster in the game than through MagicaVoxel and `NvfImport`, and fields it in a skirmish. |

The gates come from F6.2. Their thresholds are the reviewers' judgement until question 26 fixes the budgets:

| Gate | Where | Measured | Threshold | Change of course |
|---|---|---|---|---|
| 1 | Before G-M1 | Per-pass GPU times, on the default sector and on `--stations 1 --frigates 990 --capitals 0`: 991 placements, 1,394,238 voxels | The splats over 10 ms, or the frame over 16.7 ms | S-M9's levers for whole placements, and `ExecuteIndirect` if the CPU is over, move ahead of G-M1. If both are under, §14's "Placements, not voxels" is struck. |
| 2 | End of G-M1 | The profiles of a brick, a needle and a spine of equal cost | Any two within 10 % on acceleration, turn rate and arc cover | Retune G29's derivations or the materials before combat is built on them |
| 3 | End of combat | The server's tick at the 99th percentile, 50 ships a side for 5 minutes in lockstep | Over 8 ms after one pass of optimization | G44's ladder |
| 4 | End of combat | The worst frame of the aftermath, at peak wrecks and debris | Over 16.7 ms | G50's lifetimes, then S-M9's levers on damaged placements, then G44's ladder |
| 5 | End of combat | Each shape's win rate against the others, 20 seeds a pairing, headless, under G33's rules | One shape wins over 80 % against both others, or no engagement range reverses a pairing | Retune. After two failed retunes, the owner reopens G18 before command is built |
| 6 | End of command | Pixels per voxel at the default strategic zoom; whether the owner can name a failed module on recorded runs without the orbit camera | Under 1 px, or he cannot | G49's readout and icons, and a nearer default zoom |
| 7 | End of G-M4 | Time to the first credit; the median length of 10 matches of the opponent against itself; whether the owner refits in 3 matches | A first credit after one minute; a median outside 20 to 40 minutes; no refits at all | Tune income and prices. No refits means §14's first risk has fired, and G5's split is reopened before G-M5 |
| 8 | End of G-M5 | The opponent against a scripted rush and a scripted turtle, 10 seeds each | It wins fewer than half | A second pass on the opponent before the designer |

### §14 additions

| Risk | Text |
|---|---|
| The rules that pick the winning hull | Aim, bearing, the sweep contract, exposure and validation (G33, G37, G38, G39) are the battle milestone's to write. Until they are, the shape test measures the code's accidents, not the designs (F1.4, F3.2). |
| Friendly fire decides the bands' job | If a side's shots stop at its own hulls, bands and staggered rows matter and command is harder. If they do not, bands need another job or wait (question 24; F3.2, F4.3). |
| The fog is a property of the protocol | A message type added without its per-side rule leaks. The batch's test of the opponent with and without enemy internals is the check (G30, F6.6). |
| The economy's numbers decide whether a match ends in its target | The exchange rate, the ore budget and the cap on labs are G-M4's to set against a budget table, and the batch measures the length (G36, G42; F2.1, F2.2, F2.7). |
| The opponent's library is frozen | A design that beats the whole of it beats it every match. The opponent fields any library, and the batch pits the libraries against each other (F5.7). |

The open questions are 23 to 31 of Part 1 §7, until the owner answers them.

### Knock-on effects

**`SpaceScene.md`:** §16's re-plan paragraph records the swap, and S-M8's bench running on whole placements before G-M1 (G22 as changed). §13's keys give 0 to 9 to control groups and move the debug views behind a modifier (F4.6). S19's lifetime takes the skirmish's values (G50). §7.6's levers on damaged placements stay as §11.1 already amends them.

**`NeuronVoxelFormat.md`:** the format does not change. G37's validation, G40's classes and G43's build order are the game's, and they stay out of the file (N8). §5's marker names carry a mount's size class, which G-M1 fixes. If question 23 takes (a), materials become classes in the design's game data, still outside the file.

**ADR-003:** the shared game library holds the profile and the validation (G29, G37). The opponent's project lands with combat, as the bench's second side, with its ADR.

**ADR-010:** amended by a world-anchored overlay and a sloped quad, with the quad's CPU twin (R15, G49).

**ADR-015:** amended beyond per-side snapshots. Every message is encoded per session, with baselines on first sight, sessions bound to sides, the full command set and a command log (G30, G34, G41). What it forecloses changes with it: a message that differs between sessions becomes the rule.

**ADR-017:** amended four ways. Flight takes each ship's limits from its profile (G29). Separation lifts the foreclosure on steering across a route, and keeps the keep-out guarantee and its ten-minute test (L2). Squadrons extend the slots (F4.6). And an attack may enter a keep-out (G47).

**ADR-018:** the bench's timeline runs through a battle's aftermath (F6.5), and N and B give way to selection and control groups (F4.6).

**`AGENTS.md`:** R19's "what its side is sent" now means every message (G30), with no change to its words. A new rule, R21, comes from S11 and G41: the world and the opponent read no clock but the tick, and draw randomness only from `PcgHash` of the seed. R15's list of twins gains the overlay's sloped quad, if it draws on the GPU. §2's table gains the opponent's project when its ADR lands, one milestone earlier than planned.

The next free ADR is ADR-023, so the ADRs these amendments bring number from there.
