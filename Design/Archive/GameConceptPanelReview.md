# Outpost.Voxel — Game Concept Panel Review

**Status:** a review for the owner, which the owner took into [`GameConcept.md`](../GameConcept.md) on 2026-09-29, and archived as the record of its reasoning. Its questions 38 to 56 are answered in the concept's §14, seventeen by the owner and two, 42 and 52, derived from its findings; G54 to G73 stand in the concept as those answers; and the concept, not this review, is what the code follows · **Date:** 2026-09-29
**Reviews:** [`GameConcept.md`](../GameConcept.md) as the owner amended it on 2026-09-29 from its first review, [`GameConceptReview.md`](GameConceptReview.md), read against Warzone 2100, its data files and its source (https://github.com/Warzone2100/warzone2100, master of 2026-09-28), and against the games that already let players build what they fight with

This document reviews the game concept and changes nothing. Four reviewers read it independently, each with one mandate: systems and combat (C), balance and the economy (B), pacing and retention (P), and the market, with Warzone 2100 as the benchmark (M). Each was barred from repeating the first review's 36 findings, which the owner had taken in as G28 to G53, unless the adopted answer fell short or made a new problem. The lead checked every claim against the concept and its siblings, re-ran the arithmetic, counted Warzone's figures in its own files, and adjudicated where the reviewers disagreed (§3). Assumptions are named where they are made. Section and decision numbers are the concept's as it stood before this review's amendments.

## 1. Verdict

The concept was viable as a design direction, and not yet a balanced real-time strategy game. Its engineering scaffolding was its strongest asset: 53 recorded decisions, a deterministic headless batch, budgets, and gates that name their change of course. The first review had closed its internal consistency. What it left was the layer where balance lives, and there the panel found holes in the rules rather than in the numbers, each raised by two reviewers independently or shown by arithmetic:

- nothing capped the fleet, and what size is worth at equal price swung about forty times on rules not yet written (B2);
- the rules that pick the winning hull, target retention, leading, the order of hits within a tick and time to kill, were unwritten, and the one candidate, aim at the center of mass, lost to a ring (C1, C2, C3, C4, C7);
- three rules leaked value: G13 made a refit a free re-armor (B5), G36 could be held off by the side it would kill (B3), and a wreck could repay more than its hull (B4);
- keep-out spheres let cheap bars wall the plane, and left docking, building and mining with no rule to enter them (C5);
- the gates ran before their inputs existed, on samples too small to decide their thresholds (B1, B6, P1).

The likeliest degenerate lines were a raid on the core followed by a turtle (a core command module that never heals, G36's hole, and salvage paying whoever holds the ground), swarms under drilling fire, hollow hulls against center-of-mass aim, and bar walls. A classic rush was the least likely: the start has no shipyard, and gate 8 already tests one.

Against Warzone 2100 the concept surpassed it on the authorship of units and the fidelity of damage, matched it on the shape of the loop, and lagged on tactical breadth, the scale of the tree, territory control and replay (§4).

The technical risks were the best-managed part of the plan. Netcode strain from per-voxel damage is lower than it looks: at an assumed battle peak of fifty lasers removing one or two voxels a tick and fifty shells a second removing about fifty each, some 4,850 voxels go each second, 10 to 15 KB at two or three bytes a voxel, against 187,200 bytes a second of snapshots without delta encoding (§11.2). An authoritative server keeps fog where no client can reach it, which a simulation synchronized on every client, as Warzone's is, cannot. The feasibility risk that mattered was scope: six milestones, an interface built from the canvas up (D9), an opponent the concept itself calls a project, and the designer, which is what sets the game apart, built last.

## 2. Findings

### Systems and combat

**C1. The candidate aim point could lie outside the target** (Major; raised as Critical). §8.1 offered "the center of mass, with a spread per weapon" as a candidate. A ring of radii 20 and 12, six voxels thick, is π(20² − 12²) × 6 ≈ 4,825 voxels in one piece (§5.5), fits the capital ship's 45 × 77 × 21 box (SpaceScene §4), and has its center of mass in the hole. Face-on, a laser with a spread of 4 voxels (assumed) lands exp(−12²/32) ≈ 1 % of its shots on it; fire spread over its outline would land (20² − 12²)/20² = 64 %. Brick, needle and spine are all convex, so gate 5 could not see it. Taken as G57, aim drawn from the silhouette, and as G28's ring and hollow shell.

**C2. G33 left out target choice and retention** (Major). Drilling k voxels of toughness T at q damage a tick needs kT/q consecutive ticks on one line of one target. A ship that re-picks the nearest target every tick, in a melee closing at up to 120 units/s (two of ADR-017's frigates), spreads its beam, drilling becomes erosion, and the brick wins. Taken as G71.

**C3. A tick's hits resolved one at a time, in no stated order** (Major). §8.3 searched connectivity "after each hit". Any fixed order, by entity id or by side, hands mutual kills to one side, and a section severed mid-tick is passed by later shots (G39); the mirror batch would report that bias as shape. Per-hit searches also cost up to 100 beams × 3,000 voxels = 300,000 visits a tick, against gate 3's 8 ms. Taken as G72.

**C4. A breached reactor left no window to retreat** (Major). G51 made any reactor failure a second instant loss after G49's readout was written, and the readout barred only the command module and the hull. G46's default order, weapons last, cuts thrust first, and a capital ship needs 180/8 = 22.5 s to turn away at ADR-017's 8°/s. So a crippled ship detonated before "retreat for repair" could save it. Taken as G59.

**C5. Keep-out spheres let cheap structures close off ground** (Major). §9 had paths avoid keep-out spheres, which ADR-017 sizes from half the occupied box's diagonal, and G47 exempted only an attack. A 1 × 1 × 255 bar costs 255 voxels, 22 % of a frigate, yet keeps a frigate 127.5 + 22.15 + 50 ≈ 200 units from its center; chained at connectors, bars wall the plane and creep 255 units a link. Mining, docking, building at a connector and unloading all happen inside keep-outs, with no rule to enter one, and G47's other clause, no fire from a muzzle inside another entity's box, would silence turrets beside their own structures and ships crossing wrecks. Taken as G61 and the amended G47.

**C6. Squadron rows cleared fire in one direction only** (Major). §9's staggered rows keep lines clear toward the front. A broadside squadron of twelve, in three rows of four, engaging a target on its beam fires with 3 of its ships, and orbiting instead dissolves it. Gate 5 was run on lone ships, a milestone before squadrons exist. Taken as G62, with gate 5 run again in squadrons at G-M3.

**C7. The one counter that is not geometry hung on an unwritten lead rule** (Major). Toughness counts the same against both weapons, so the mass driver's miss chance was the only counter that is not shape. Against a frigate orbiting 500 units out at 60 units/s, a 300 units/s shell (assumed) lands 100 units off unled, 4.5 times the frigate's 22-unit sphere, and 100²/(2 × 500) = 10 units off led. So the mass driver was either a longer-ranged laser or blind to frigates. Taken as G58 and G60.

### Balance and the economy

**B1. Gates 5 and 7 measured what did not exist yet** (Major). Gate 5 compared designs "of equal cost" at G-M2's end, but G-M4 commits the prices; gate 7 needed "10 matches of the opponent against itself" at G-M4's end, but the opponent plays the loop only in G-M5. A retune of prices in G-M4 could hand the brick the win with no threshold reading wins by design pair afterwards. Taken as G73: gate 5 runs again at the ends of G-M3 and G-M4 and whenever prices or rules change, and gate 7's length and refits move to G-M5, as gate 10.

**B2. No rule capped the count, and size's worth swung forty times** (Major). G11's fifty was a scale, not a rule; only labs were capped. Split a capital ship (10,747 voxels) into 10,747/1,181 = 9.1 frigates at equal price and equal firepower. Under Lanchester's square law, where every ship fires, the frigates are 9.1/9.1^⅓ = 4.4 times as strong under drilling fire, and 1.0 under erosion; under the linear law, where G39 lets only a front rank fire, 0.48 and 0.11. That is a forty-fold swing on rules then unwritten. §11.2's 150,000 fleet voxels would hold 127 frigates, 2.5 times gate 3's fifty, and G44 could not lower a count no rule set. Taken as G56, and as G28's small ships against one large.

**B3. G36 could be held off by the side it would kill, and a tie had no winner** (Major; the pacing reviewer found it independently). Only mining lasers remove ore (G39), so "once every field is dry" never comes while a side leaves one voxel of its home field unmined under its turrets, and the side with the more damaged core wants exactly that. Identical cores wearing down at one rate fail on the same tick, and G20 named no draw. And since the core's command module never heals, one early raid decided any match G36 ended. Taken as the amended G36 and G20: the wear also starts at minute 60, and a same-tick loss draws.

**B4. A wreck could repay more than its hull** (Major). A command kill that drills out 250 of a capital ship's 10,747 voxels leaves 97.7 % of it as a wreck. At one hull voxel per wreck voxel or better, salvage repaid about the whole hull; at half the first review's 1:5 rate, 2.5 × 0.977 = 2.4 times it. Whoever held the ground profited from every exchange, which revives the snowball the first review dismissed before salvage existed (its F2.1), and reactor sniping (G51) became the standard denial. Taken as G63.

**B5. G13 made a refit a free re-armor** (Major). The best researched material filled a design "when the design is built or refitted", and a refit "charges the incoming modules in full" and nothing else, so refitting a fleet to its own variant re-armored it for nothing. "Best" was undefined, and a denser heavy material would slow every heavy-class design with no opt-out, invalidating the profiles gate 2 checks. The owner took Warzone's way rather than pricing the refit: research toughens a class where it stands, with its density and price fixed (G55, the amended G13).

**B6. The gates' samples were too small, and a mirror batch cannot see what it does not play** (Major). Sixteen wins in twenty is consistent with a true rate from 58 % to 92 % (95 % Wilson interval). Telling 80 % from 65 % takes 56 games, at a one-sided significance of 0.05 and a power of 0.8. Gate 8's ten seeds pass an opponent that truly wins 40 % in 37 % of runs, for each scripted side. And a batch of the opponent against itself never plays the strategies above. Taken as G73: samples sized to decide, and scripted exploit sides, as gate 9.

**B7. Nothing made a seeded sector fair** (Major). §3 asked only for fields "near each" start, and ADR-017 places by rejection sampling in a disk. Unequal ore near the starts, or a lone trader nearer one of them, decides mirror matches, and G25's exclusivity amplifies it. Taken as G66: symmetry under a half turn, which keeps whole coordinates and quarter turns, the trader at the center, and every seed played from both starts.

**B8. Three labs did not make twenty items a choice** (Minor). §7.2 said "the cap on labs keeps the order of research a choice". For twenty items and three labs to last from minute 3 to minute 30, a project takes (30 − 3) × 3/20 ≈ 4 minutes, which nothing stated; at 90 s a project the tree is done by minute 3 + 20 × 1.5/3 = 13. A refinery-yield upgrade is worth most at minute one, a solved opening. The first review's F2.7 had proposed sizing the tree and pricing industry's payback, and only the cap was carried. Taken as G64.

### Pacing and retention

**P1. Gate 7 could not run where it was scheduled, and could not fail** (Major). Beyond B1's point that no opponent plays the loop at G-M4's end, its refit test passed on a single refit made to try the screen, and "no refits at all" would reopen G5 on confounded evidence: with two weapons and about a dozen modules, a thin catalogue is the likelier cause than the split between library and match. Taken as G73's gate 10, which counts only refits ordered within three minutes of first seeing an enemy variant, in 2 matches of 3.

**P2. Research and trade ran dry together** (Major). Traders sell "one step beyond what the side's research has reached" (§7.3), so at B8's minute 13 the lab list and the trader fall silent at once, and for 17 minutes of a 30-minute match the middle is held for ore alone. Taken as G64.

**P3. Clearing lines of fire was the player's job, and nothing priced it** (Major). ⌈50/12⌉ = 5 squadrons share 3 bands, so two broadside squadrons orbiting one target cross each other's lines every lap. No gate set a threshold on shots stopped by a side's own hulls, gate 6 judged readability on recordings without time pressure, and alerts have no audio (§2). Taken as G71's preference for clear lines and its hold of fire, and as gate 6's threshold.

**P4. The design loop gave the player no feedback** (Major). After-action records went to the batch, and G-M5's end screen had no stated content. A player learns one uninformed data point per 20-to-40-minute match, too slowly for the loop meant to bring them back. Taken as G69.

**P5. Fun was validated late, by one person** (Major). Every human-judged check, gates 6 and 7, G-M5's verdict and G-M6's test, was judged by the game's designer, and the first full match against an opponent that plays the loop was in the fifth of six milestones. Taken as G68.

**P6. Difficulty had no dial, and the opponent attacked at tick zero** (Minor). Warzone scales its AIs by income: its rules scripts set Hard at 150 to 170 % and Insane at 200 to 230 %. R19 rightly forbids handing the opponent anything its side is not sent, and one level with a frozen library is something a player outgrows. Read literally, "attack in waves once its fleet outweighs the enemy it remembers" fired at tick zero, when it remembers nothing. Taken as G67, and as §10's memory from the known start.

### The market, and Warzone 2100

**M1. The selling point is the combination, not construction** (Major). From the Depths has players design, produce and command voxel vehicles whose inertia updates as blocks go (per its store page); Cosmoteer destroys modules individually, splits ships and steers by thruster placement; Avorion has block ships that break apart, mining and salvage lasers and a fleet strategy mode; Starsector has hulls, mounts and variants. None pairs sculpted hulls with Warzone's symmetric match at thirty to fifty ships a side in three dimensions. The unique part is also the risky part: it holds only if gate 5 passes and per-voxel damage survives at thirty ships a side (G44). Taken as §1's restatement.

**M2. Outpost had no outposts** (Major). Warzone's map is made of claims: derricks on fixed oil points, and the defenses around them. Structures rose only at connectors of the core's tree (§6, G46), so territory was held by ships alone, the weakest row of the benchmark (§4). Taken as G65: two outposts a side, not fatal to lose, each rooting a station within its build radius (G61).

**M3. The shot event gave counter-battery away** (Major). §8.2's event carried "the tick it fired at, its origin", so any modified client could place an unseen shooter, against §9's "Firing does not give the shooter away"; for a straight-line shell, its position, velocity and fire tick give the same answer. The systems reviewer found it too. Taken as G54.

**M4. The best replay hook was almost built** (Major). Libraries are files known by their hash (§5.2), the opponent fields any library (§10), and a log replays (G41). "Beat the opponent flying another player's library, verified by replay" is asynchronous multiplayer with no network code, as Gratuitous Space Battles built its multiplayer on uploaded fleet challenges. Taken as G69.

**M5. Nothing in the slice faces a market** (Minor). Only G-M0 is done; menus, audio, saves and networked play are excluded by design (§2), and D9 makes every panel the tree's own. That is right for a slice. Had a release been planned, the Claude-authored opponent library would have needed Steam's AI-content disclosure, on the reviewer's reading of Steam's content survey, where the code assistance would not. The owner answered that no release is planned (G70).

## 3. Where the panel disagreed

**Swarm or capital.** B2's square law favors a frigate swarm 4.4 times; C6 and G39's pass-through damage push toward the linear law, which favors the capital ship. Both hold, and that is the finding: the rules as written did not fix the size meta, so the command budget (G56) and gate 5's pairing of small ships against one large must.

**Mechanics, five or six.** The systems reviewer scored the mechanics five. G33 openly handed aim and bearing to G-M2's design with a gate behind it, so deferring them was not the defect; the defects were that the one written candidate could be gamed, and that G33 omitted retention, leading and the order within a tick. The lead scored six.

**Outposts** (M2) cut against the connector tree the first review asked to protect (its §6). The owner took them, with a cap of two and a build radius (G61, G65).

**C1's severity** was lowered from Critical to Major, since the aim rule was a candidate; its lesson, that the test shapes must include adversarial topology, stands in G28.

## 4. Warzone 2100 compared

Warzone's figures are counted from its `data/mp/stats/*.json`, its `multiplay/script/rules/` and its `src/power.cpp`, `src/structuredef.h` and `data/mp/stats/brain.json`, on the master branch of 2026-09-28.

| | The concept, as reviewed | Warzone 2100 | Verdict |
|---|---|---|---|
| **Customization depth** | Hulls sculpted voxel by voxel, with a material class per palette entry; typed, sized mounts; named variants; mass, inertia, thrust, arcs and silhouette derived from shape (G18, G29, G35). Hulls are made only between matches. The slice has about a dozen modules, 2 weapons and 2 material classes. | An in-match designer: body, propulsion and turret, as stat vectors with no spatial layout. 14 bodies, 5 researchable propulsions and cyborg legs, 77 researchable weapons. | Surpasses in ceiling, since the shape is the design; lags in breadth, 2 weapons against 77, and in flexibility within the match. |
| **Tech tree scale** | About twenty items in five branches, at most three labs. Materials reached ships at their next build or refit; whether upgrades reached fielded modules was unstated. | 390 topics in multiplayer and 413 in the campaign; 206 of the 390 are stat upgrades, which apply to what is already built. 5 labs, each taking a research module. | Lags about twenty times, deliberately for a slice, and in how research reaches the field. |
| **Economy model** | One resource: finite, graded asteroid ore and low-grade wrecks, hauled to refinery structures or ships; traders sell unresearched modules; cores wear down once the fields are dry. | One resource: each derrick yields 1 power a second times a modifier, "FOREVER" in its own source's words; 4 derricks to a generator, at most 8 generators. | Surpasses in texture: depletion, logistics, salvage and trade. Lags in soundness: income followed miner count rather than ground held, and three rules leaked value. |
| **Combat tactics** | Per-voxel damage and locational kills (the command module, reactors, "attack this module"); arcs; own hulls block fire; 3 bands; shells against beams. No damage types, artillery, air layer, veterancy, or terrain beyond asteroid fields that shrink as they are mined. | An authored counter matrix: 6 weapon effects against 7 propulsion classes, from 20 to 150 %, and against 4 structure strengths, from 10 to 400 %; kinetic against thermal armor; artillery spotted by sensors, and counter-battery; VTOLs against anti-air; commanders leading 6 units plus 2 a rank, over 9 ranks; terrain and its chokepoints. | Surpasses in physical fidelity; lags in tactical variety. Its counters are emergent and unproven. |
| **Replay value** | One seeded skirmish against one opponent at one level; no multiplayer, campaign, saves, menus or audio. Hooks: the library, the seeds, and any library as the opponent. | Three campaigns; skirmish and multiplayer for up to 10 players; 7 bundled AI configurations (NullBot in 3, Cobra, BoneCrusher, Nexus, SemperFi); presets for bases, power and tech level; mods; free, and still developed. | Lags heavily in the slice. A library players could share would outlast anything Warzone has. |
| **Base building** | A tree of player-designed structures at the core's connectors; power flows through connectors and is shed in a set order; 7 roles. | Free placement on terrain; 124 defensive structure entries, and walls, gates and tank traps; factory, research and power modules. | Surpasses in authorship; lags in placement and in defensive variety. |
| **Territory control** | Fields and trader radii held by ships alone; nothing stood away from the station. | Derricks claim fixed oil points, and defenses stand around them. | Lags: the weakest row. |
| **Scale and caps** | About fifty ships a side, as a load estimate; only labs capped. | 150 units a player, 15 of them trucks and 10 commanders; 5 factories, 5 labs and 8 generators; all enforced. | Lags: Warzone's scale was a rule, the concept's a hope. |

## 5. Scorecard

The scores are for the concept as reviewed, before the owner's amendments.

| | Score | Why |
|---|---|---|
| **Mechanics** | 6 of 10 | The primitives are coherent and testable: G39's contract, G29's profile, one command mount, and validation on the server. The rules that decide fights were unwritten or could be gamed, and keep-outs broke every interaction but an attack. |
| **Balance potential** | 6 of 10 | The batch makes imbalance cheap to find. As specified it would have measured the wrong things: no cap on the count, three leaks of value, unfair seeds, and gates too early and too small. |
| **Depth** | 6 of 10 | The design layer is deep: performance from geometry, arcs, buried modules. The match layer was thin: 2 weapons, 2 materials, no damage types, a tree that ran out by mid-match, no claims on territory, one level of opponent. The full vision's ceiling is about 8, if gate 5 passes. |
| **Market readiness** | 1 of 10 | Nothing playable existed, and §2 excludes menus, audio, saves and networked play by design, which is right for pre-production and no criticism of the plan. Its potential, had a release been planned: 6 of 10, or 3 if gate 5 or G44's floor of thirty ships fails. |

## 6. Strengths to protect

| Strength | Why it must survive |
|---|---|
| G39's contract, G29's single profile, and exactly one command mount | Damage spent along the path defeats spaced armor and self-cutting and gives the bands a job; one profile feeds flight, the HUD, the opponent and damage alike; one mount closes the spare-core exploit. |
| The batch: seeded, headless, in lockstep and logged (G32, G41, G53) | Statistical power costs CPU, not the owner's time; every gate the panel sharpened depends on it. |
| Per-side trader stock and grades that rise toward the middle (G25, G52) | The economy creates the battles; every economic fix must reward holding the middle. |
| Orders while paused, retreat for repair as a standing order, a band as a standing setting (§3, §9) | The right answers to the load of fifty ships a side. |
| Designs known by their hash and fieldable from any library (§5.2, §10) | Content that can be shared with no network code, which G69 makes a mode. |
| One voxel machinery for combat and mining (G6, G19) | The fields shrink on screen for the same reason hulls do; nothing else in the comparables does both. |
| Fog kept by an authoritative server (G21, G30) | No client can reach what its side is not sent, which a simulation synchronized on every client cannot promise. |

## 7. Questions, and the owner's answers

These continued the concept's §14 from 38. For each, the options offered are listed with the review's recommendation first, and the one the owner took is named; 42 and 52 were not put to the owner, since they follow from the findings with no alternative worth a choice.

38. **What does a shot tell a side that did not see it fired?** Offered: bearing only; bearing, and a counter-battery sensor in the slice; firing reveals the shooter. **Taken: bearing only** (G54).
39. **What happens to armor when a side researches a better material?** Offered: priced re-armor, with one density a class and a refit charging the price difference; free upgrades, Warzone's way; armor frozen at build. **Taken: free upgrades, Warzone's way** (G55, G13), with each class's density and price per voxel fixed, so that research changes no profile.
40. **How is fleet size capped?** Offered: command points, every ship counting; points with miners and constructors apart; a flat cap on ships. **Taken: command points, every ship counting** (G56).
41. **Where does a weapon aim, and do shells lead?** Offered for aim: the silhouette; the center of mass with a spread, and a ring and a hollow shell in gate 5; the nearest face. **Taken: the silhouette** (G57). Offered for leading: shells lead and agility evades; perfect lead; no lead. **Taken: shells lead and agility evades** (G58).
42. **Is a tick's combat resolved at once?** Not put to the owner; derived from C3 (G72).
43. **How much warning and time does a ship get?** Offered: a retreat window; decisive fights; one loss condition, with a reactor failure disabling rather than detonating. **Taken: a retreat window** (G59).
44. **What makes one fleet counter another, and does a detonation damage its neighbors?** Offered for counters: geometry, stated; a 2 × 2 damage table; a matrix on Warzone's model. **Taken: geometry, stated** (G60). Offered for detonations: no damage in the slice; damage to every hull nearby; damage to enemy hulls only. **Taken: no damage in the slice** (G51).
45. **How do ships move around, and into, obstacles?** Offered: clearance by voxels, with docks; spheres, with every interaction exempted and a cap on thin structures; spheres with a build radius. **Taken: clearance by voxels, with docks** (G61, G47).
46. **What does a squadron do in combat?** Offered: a formation by bearing; breaking to bear; holding formation. **Taken: a formation by bearing** (G62).
47. **What should salvage be worth?** Offered: half its hull; a quarter; half, with the modules recovered. **Taken: half its hull** (G63).
48. **How long should research last, and when do upgrades reach the fleet?** Offered: half the tree, with upgrades at once; most of the tree; the whole tree, early. **Taken: half the tree** (G64, G55).
49. **May a side build away from its core?** Offered: outposts, capped; no outposts; outposts after the slice. **Taken: outposts, capped at two** (G65).
50. **How does a stalled match end?** Offered: a clock and a draw rule; a threshold of ore and a draw rule; a score at a time limit. **Taken: a clock at minute 60 and a draw rule** (G36, G20).
51. **Must a sector be fair to both starts?** Offered: symmetric sectors; asymmetric, with sides swapped in every measurement; asymmetric as they are. **Taken: symmetric sectors** (G66).
52. **Are the gates' samples sized to decide them?** Not put to the owner; derived from B1, B6 and P1 (G73).
53. **How is the opponent made harder or easier?** Offered: visible handicaps; its budget of work alone; both. **Taken: visible handicaps** (G67).
54. **Who plays the slice before G-M5's verdict?** Offered: three to five outside players; the owner and the batch; outside players from G-M6. **Taken: three to five outside players** (G68).
55. **Is fighting another player's library a mode?** Offered: the first mode after the slice; a mode in the slice; UDP first. **Taken: the first mode after the slice** (G69).
56. **Is a release a goal?** Offered: not yet, with a roadmap recorded; planning for one; no, the owner's project only. **Taken: no, the owner's project only** (G70).
