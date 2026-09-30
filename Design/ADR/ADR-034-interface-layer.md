# ADR-034 — The interface layer, and the commander's client

**Status:** accepted, 2026-09-29 · **Lands with:** phase 4 of [`Design/MvpPlan.md`](../MvpPlan.md), its tasks 2, 4, 5 and 6 and the last two of its tests · **Amends:** [ADR-010](ADR-010-canvas-text-overlay.md)'s canvas, which becomes one surface of several, and whose quad gains a kind and a segment and grows from 44 bytes to 64; [ADR-018](ADR-018-client.md)'s cameras and keys, which a skirmish trades for the commander's, its debug keys behind Alt; [ADR-030](ADR-030-skirmish.md)'s first view under `--skirmish`, which the strategic camera takes over; and [`AGENTS.md`](../../AGENTS.md) §2, whose `NeuronClient`, `GameLib` and `NeuronClientTests` rows gain the interface, the commander and their tests

## Context

Phase 4's second task asks for an ADR of the interface layer:
- the canvas's widgets, in `NeuronClient`: panel, label, button (with its hotkey, or the reason it is disabled), bar and list;
- input routing: the interface first, the world second;
- a world-anchored overlay of markers, rings, bars and lines, drawn with a sloped quad that has a CPU twin (R15);
- it amends ADR-010. D9 rules out third-party code, so the layer is the tree's own.

The phase's fourth to sixth tasks build the client on it:
- the strategic camera: the arrows and the screen's edges pan, the wheel zooms, the middle button turns, and Home frames the core;
- selection by click and by box, with Shift adding, and a pick on the CPU against each entity's projected box grown to at least 10 pixels;
- right-click moves, S stops and H holds; Ctrl with a digit sets a control group, and the digit recalls it;
- selection rings, and order markers drawn at once;
- the debug keys behind a modifier;
- the HUD's first panels: a top bar, a selection panel, and an order bar whose Attack and Mine buttons are disabled with their reason;
- the cursor from `InputState`.

The concept's §9 describes the camera, the selection and the HUD. [ADR-033](ADR-033-orders-and-flight.md) carries the orders to the server, and their states back in each snapshot.

ADR-010's canvas draws text and rectangles, each a quad on the pixel grid. It cannot draw a line at a slope, which rings, lines and markers need. Nothing that draws on it can be tested without a GPU.

## Decision

**Surfaces.** `NeuronClient::Surface` is what the interface and the overlay draw on:
- `FillRectangle`, `Print`, `Measure` and `DrawSegment`;
- in pixels of the target with y down, in linear color with straight alpha, over what was drawn before.

There are three surfaces:
- **The canvas** (amends ADR-010). `TextStyle` and `TextExtent` move to `Surface.h`. A style's weight becomes DirectWrite's number, 100 to 900, so that the header names no DirectWrite type.
- **`DeferredSurface`** measures text on its target at once and keeps everything else. When it replays, it draws what it kept on its target, in order, once.
- **A test's `RecordingSurface`** keeps what is drawn on it. It lays text out at 8 pixels a character and 16 a line, so that what draws on a surface is tested on the CPU.

**The segment** (amends ADR-010, under R15 and R16).
- **The quad.** A `CanvasQuad`'s fill flag becomes its kind: 0 a glyph, 1 a fill, 2 a segment. A segment's ends and half its width follow the alpha as five floats.
  - The quad is 64 bytes. The 16,384 quads a frame may draw fill 1 MiB of each frame's upload ring.
  - `CanvasQuad.hlsli` mirrors it, and the layout echo checks every member.
- **Its corners.** The vertex shader places a segment's four corners on the rectangle about it, sloped along it. The rectangle reaches half its width and a pixel more to either side, and as far past either end.
  - Bit 0 of the vertex picks the end over the start, and bit 1 the side.
  - A segment of no length lies along +x.
- **Its coverage.** The pixel shader covers a pixel by its centre's distance *d* from the segment: clamp(half width + ½ − *d*, 0, 1). So a segment has round ends and an edge antialiased over a pixel. The quad's extra pixel holds every pixel it covers.
- **The twins.** `CanvasSegmentCornerNdc` and `CanvasSegmentCoverage` are the C++ twins of the HLSL functions of the same names. They sit in `CanvasShading` beside ADR-010's, under R15's exception for the canvas.
- **Glyphs and fills** draw as ADR-010 has them.

**The widgets** (`NeuronClient::Interface`) are immediate.
- **Each frame** calls `Begin`, then each widget in the order it draws, then `End`. A widget draws at once, and says what the pointer and the keys did to it.
- **Their look.** An `InterfaceStyle` gives the text, the padding and the colors. A row, of a button or a list, is a line of text at 1.4 ems with the padding above and below it.

The five widgets:
- **Panel.** A translucent rectangle: room of the interface's.
- **Label.** A line of text.
- **Button.** Its text, and its hotkey's name at its right, dimmed.
  - It is clicked when a press of the left button and its release both land on it, in one frame or across several.
  - Its hotkey clicks it when pressed with neither Alt nor Ctrl held.
  - A button given a reason is disabled: dimmed, and never clicked. While the pointer hovers it, the reason is drawn beside the pointer, over everything.
- **Bar.** Its fraction, from its left, in its color, over the rest.
- **List.** A row for each line that fits, from the top. The chosen row is highlighted and the hovered one lit. It says which row a press and its release both landed on.

**Input routing:** the interface first, the world second.
- **A press** of any button is the interface's when it lands on a panel or widget drawn that frame, and the world's otherwise. It stays whose it is until its release has been read, wherever the pointer goes meanwhile. A press and its release within one frame are one press.
- **With no button held**, the pointer is the interface's while it is over a panel or widget, and the wheel with it.
- **A hotkey** is the interface's whenever its button is drawn, enabled or not. So a disabled button's key does nothing else. With Alt or Ctrl held a key is never a hotkey: those belong to the control groups and the debug keys.
- **The frame's order.**
  1. The HUD is laid out first, on a `DeferredSurface`, so that the frame knows whose the pointer is before the world reads it.
  2. The camera and the commander take what the interface left.
  3. The renderer's canvas draws the world's overlay, then the HUD replayed over it, then ADR-010's figures over both.

**The world's overlay** (`NeuronClient::Overlay`): segments and fills, drawn where a view shows the world. `ProjectPoint` is the inverse of `NeuronCore::PerspectiveRay`, and nothing on or behind the near plane projects.
- **A ring**: 48 segments around a level circle.
- **A line**: one segment, cut where it crosses the near plane, a thousandth of the plane's distance in front of it. A line wholly behind the plane is left out.
- **A marker**: a diamond, a size in pixels across, about where a point shows. It is as large at any distance.
- **A bar**: raised above where a point shows, its fraction in one color and the rest in another. Nothing draws one in this phase; phase 5's condition bars will.

The overlay has no depth test, so a mark shows through what stands in front of it. Each segment is its own quad, so where two meet, as around a ring or a marker, their shared pixels blend twice.

**The pick** (`NeuronClient::Pick`, the concept's §9), on the CPU.
- **An entity's box** is its composite's box, where the entity stands and turned as it is. Its eight corners project, and their bounds grow about their middle to at least 10 pixels each way (`MIN_PICK_PIXELS`). A box with a corner behind the near plane is not picked.
- **A click** picks, among the boxes that hold the pointer, the one whose middle is nearest the eye. Among those as near, it picks the first.
- **A box** selects the entities whose middles show within it, edges included.
- **Debris is never picked**, as the concept has it. A detonated entity drops out of both the click and the box, though a selection that already holds it keeps it.

**The input** (task 6).
- **`InputState`** gives:
  - the pointer's place in the client area;
  - whether the pointer is there;
  - each button's presses and releases since the last frame.
- **The window** hears when the pointer leaves the client area, so the screen's edges pan only while the pointer is in it. During a drag it keeps the pointer's messages beyond its edges, as it did.
- **Alt**, pressed alone, no longer opens the window's menu, which the window does not have, so the key after it is not swallowed. Alt+F4 still closes the window.
- **The modifier keys.** `SHIFT_KEY`, `CONTROL_KEY` and `ALT_KEY` spell the SDK's `VK_SHIFT`, `VK_CONTROL` and `VK_MENU` without its header. The window checks that they agree.

**The strategic camera** (`GameLib::StrategicCamera`, the concept's §9). It looks down at a focus on the plane y = 0 from a distance, along a heading about +Y, at a tilt below the horizon. It neither rolls nor looks up. Its field of view is the orbit camera's, 45°, and its near plane is at 0.5 units.
- **Pan.** The arrows pan the focus along the plane, and so does the pointer within 4 pixels of the screen's edge. It moves a screen's worth of its distance a second, ahead along its heading and across it.
  - WASD does not pan, so that S can stop (the owner, at phase 3's checkpoint).
  - While F3's tuning shows, the arrows and Home are the tuning's, as ADR-018 has them.
- **Zoom.** Each notch of the wheel takes 0.85 of the distance. The distance stays between 150 units, close enough to see a ship's modules, and 9,000, far enough to see the whole sector.
- **Turn.** A drag with the middle button turns the heading and the tilt by 0.005 radians a pixel. The tilt stays between 25° and 85° below the horizon.
- **Home** frames the side's core from 1,400 units, keeping the heading and the tilt.
- **The first view** (amends ADR-030). It looks at the side's core from 1,400 units, 55° below the horizon, toward the sector's middle, and targets none. For an observer it frames the whole layout, as ADR-030's first view does. A capture (ADR-031) shows it, with the HUD.

**The commander** (`GameLib::Commander`) keeps the player's selection, control groups and orders.
- **Selection.**
  - A click selects the entity picked, or nothing. With Shift, it adds that entity or takes it out.
  - A drag of 4 pixels or more selects what its box holds. With Shift, it adds.
  - Esc disarms a move if one is armed, and otherwise clears the selection.
- **Orders** go to the side's ships in the selection, at most 256 (ADR-033):
  - a right-click moves them, as a group, to the point of the plane under the pointer;
  - the order bar's Move, or M, arms a move. The next left click in the world gives it, and a right-click or Esc disarms it;
  - Stop, or S, stops them, and Hold, or H, holds them.

  Orders go to the server as the game's commands (ADR-033), whether the skirmish is paused or not.
- **Control groups**, 0 to 9. Ctrl with a digit makes the selection a group, and the digit alone selects what of the group can still be picked. Alt with a digit is a debug view's.
- **Marks, at once.** A move marks its point with a marker that grows and fades over a second. The mark starts on the frame the order is given, before the server has heard of it (ADR-018's delay).
- **Order states.** From the side's order states, each moving ship's destination has a marker, brighter while the ship is selected, and each selected ship a line to it.
- **Rings.** Each selected entity has a ring at 1.15 times its sphere: cyan for the side's own, red for another side's, and pale yellow for no side's.
- **An observer** selects, and orders nothing.

**The HUD's first panels** (`GameLib::DrawHud`), on the interface. The text is Segoe UI at 15 pixels on a 96 DPI monitor, and larger in proportion on a denser one, as ADR-010's figures are. The HUD's sizes are taken as the game starts.
- **The top bar**, across the top, shows the side, its whole ships, the world's time in minutes and seconds, which stops while the world is paused, and "paused" while it is.
- **The selection panel**, in the bottom-left corner, says how many are selected. It gives a row to each of the first 8: its composite's name and its id, then "idle", "moving" or "holding" for a ship of the side's, or "debris". With more than 8, the last row says how many more there are. A click on a row selects that entity alone.
- **The order bar**, centred along the bottom, has five buttons: Move (M), Stop (S), Hold (H), Attack (A) and Mine (G). G is Mine's key because M is Move's.
  - Move, Stop and Hold are disabled, with "Select ships of yours first", until the selection holds a ship of the side's.
  - Attack is disabled with "Attack comes with combat, in phase 5".
  - Mine is disabled with "Mining comes with the economy, in phase 6".
  - The bar is not drawn for an observer, nor while the orbit or chase camera is on.
- **ADR-010's figures** move below the top bar.

**The keys of a skirmish** (amends ADR-018). A world with sides is commanded. The sector keeps ADR-018's cameras and keys.
- **The keys:** the commander's and the camera's above, Space to pause or resume, and F1, whose key map lists them.
- **The debug keys, behind Alt:**
  - Alt+1 shows the lit image, and Alt+2 to Alt+6 the debug views;
  - Alt+O orbits the first selected entity, and Alt+C chases it, until Home;
  - Alt+F flies while orbiting;
  - Alt+E detonates every selected entity, and Alt+R restores it.
- **N, B, F, C and Tab** do nothing in a skirmish.
- **The brackets, V, F2 and F3** do what they did.

**Where it lives** (amends `AGENTS.md` §2):
- `NeuronClient`:
  - `Surface`, `DeferredSurface` and `Interface`;
  - `Overlay` and `Pick`;
  - the canvas's segment and its twins;
  - `InputState`'s pointer and buttons.
- `GameLib`: `StrategicCamera`, `Commander`, the HUD, and the game's frame that joins them.

## Tests

- **`NeuronClientTests`, `InterfaceTests`, 6** (the plan's widgets' hit tests and routing):
  - **Clicks.** A button is clicked by a press and its release on it, even within one frame. It is not clicked by a press alone, by a release off it, or by a press of the world's released on it.
  - **Routing.** The pointer goes to the interface over its room with no button held. A press is the interface's or the world's through its release frame, wherever it is dragged.
  - **Hotkeys.** A hotkey clicks its button, but not with Alt held. A disabled button takes its key, is never clicked, is dimmed, and says why while hovered and not after.
  - **Lists.** A list shows the rows that fit, highlights the chosen row, and says which row was clicked.
  - **Bars.** A bar fills its fraction.
  - **The deferred surface.** It measures at once, and draws in order, once, when it replays.
- **`NeuronClientTests`, `PickTests`, 3** (the plan's pick):
  - a point along a view's ray projects back onto its pixel's centre, and a point behind the eye nowhere;
  - of two boxes under the pointer, the nearer is picked;
  - a box far off is picked within the 10 pixels it is grown to, and not beyond, and empty space picks nothing;
  - a box selects the middles within it, from either pair of its corners.
- **`NeuronClientTests`, `OverlayTests`, 3:**
  - a ring's 48 segments lie on its circle, end to end and closed;
  - a line is cut at the near plane, and one wholly behind it is left out;
  - a marker's corners stand half its size from its point, and a bar is its full part and then the rest.
- **`NeuronClientTests`, `CanvasTests`, 3 more** (the plan's twin):
  - the twin places a sloped segment's corners at values worked by hand, and a segment of no length along +x;
  - its coverage inside, at and beyond a segment's edge and ends;
  - on WARP, segments of every slope, width and alpha, a dot, one that leaves the target, and segments over a fill and over one another, drawn by the GPU. Every pixel is compared with the twin's composite, within 10⁻⁵.
- **The layout echo** gains the kind and the segment's five members.

The commander, the HUD and the strategic camera are `GameLib`'s, which has no suite. A suite for it would be a new project and an ADR, which the plan does not name. The agent exercised them natively, in a program of its own that is not in the tree (see *Measured*). Otherwise, running the game is their test.

## Measured

Measured natively, with GCC 13.3 at `-O1` on a 2.1 GHz Xeon, against the repository's sources, in the tests and in programs of the agent's that are not in the tree. The C++ was also checked against MinGW-w64's Windows headers through clang 21, and clang-tidy 22.1.8 ran with the repository's `.clang-tidy`. No Windows build, GPU or DirectWrite was at hand: those are CI's, and the frame rate is the owner's.

- **The suites.** The client's portable suites pass natively, 40 tests, among them the 12 of `InterfaceTests`, `PickTests` and `OverlayTests`. The canvas's two new CPU tests need its Direct3D header, so a program of the agent's checked their values against the twin, and all of them agree exactly. The WARP test and the layout echo run in CI.
- **The commander's program.** It checks, against a strategic camera's view:
  - a click, Shift adding and taking out, and a click on nothing;
  - a box that holds the side's ships, an asteroid and an enemy;
  - a right-click that moves the side's ships alone, to the plane's point under the pointer: under the screen's middle pixel, 0.5 units from the camera's focus;
  - a group set with Ctrl, not recalled with Alt, and recalled without a member that detonated;
  - Stop, Hold, and a move armed, given, disarmed by a right-click and by Esc;
  - a press the interface owned, which the world never sees;
  - an observer, who selects but gives no orders.

  It also checks, on the HUD:
  - S stops only while the side's ships are selected;
  - A does nothing, and hovering Attack gives its reason;
  - a click on the selection panel's row selects that entity;
  - the camera's zoom limits, its pan along its heading, and the focus at the middle of the screen.

  All of it passes.
- **Costs**, each over 20,000 frames, the worst of three runs:
  - **A HUD frame** with 12 selected, 8 of them shown, costs 1.7 µs, and asks its target for 8 fills, 20 texts drawn and 30 measured. On the canvas each text drawn or measured is a DirectWrite layout, made anew: 50 a frame, whose cost on Windows was not measured.
  - **A pick** among 54 boxes, the skirmish's count, spread over the first view, costs 14.6 µs. **A box** over them costs 0.7 µs. Each runs once per click, not every frame.
  - **Eight selected entities' rings** cost 29.8 µs a frame, as 384 segments.

## What this forecloses

- **A retained interface.** Widgets are drawn and asked each frame. There is no widget tree, no layout engine and no animation: a panel's place is computed where it is drawn.
- **Keyboard focus and text entry.** No widget takes typing or focus.
- **Tooltips beyond a disabled button's reason.**
- **A widget's hotkey with a modifier.** Alt and Ctrl belong to the commander and the debug keys.
- **Overlay marks hidden by what stands before them.** The overlay has no depth.
- **Polylines as one shape.** A ring's and a marker's joints blend twice. That is invisible at the ring's alpha of 0.9, and faint at a marker's 0.4.
- **A pick by voxels.** The pick is by box. The exact pick of a module through the visibility buffer (the concept's §9) waits for inspecting and targeting modules.
- **A HUD whose sizes follow a change of DPI while the game runs.**
- **A layout cache.** The canvas lays text out anew for every draw and measure. If the frame rate calls for one, it is the first remedy.
