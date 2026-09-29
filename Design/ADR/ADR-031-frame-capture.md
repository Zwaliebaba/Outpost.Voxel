# ADR-031 — A frame as evidence: `--capture` through WIC

**Status:** accepted, 2026-09-29 · **Lands with:** phase 2 of [`Design/MvpPlan.md`](../MvpPlan.md), its task 5 · **Amends:** [`AGENTS.md`](../../AGENTS.md) §2, whose `NeuronClient` row gains the capture

## Context

The plan's §3.3 makes frames the evidence of every phase that changes the picture. From phase 2, `Outpost.exe --capture <file>.png --capture-at <seconds>` writes one frame through WIC, which the Windows SDK provides, and exits. That leaves two questions: which frame is written, and what of it.

A capture should show what the player sees, the figures included. It should not depend on how fast the machine draws: two runs of one build and one seed, on one machine, should capture the same world.

The flip-model swap chain (SampleRenderer §13) discards a buffer's contents when it presents it, so a frame cannot be read back after its present.

## Decision

**What is captured** is the frame as it is presented, at the swap chain's size:
- the tone-mapped image, or the debug view in its place;
- the canvas over it.

The renderer records the copy in the frame's own command list, after the canvas and before the back buffer's transition to the present state, into a readback buffer:
- `FrameSettings::capture` asks for the copy;
- `Renderer::TakeCapture` waits for that frame's fence and hands back its pixels.

The copy is `RecordTextureReadback`. `ReadTexture2D` now records it too, so every WARP test that reads a texture back runs the capture's copy.

**When.** `--capture-at <seconds>` is a time of the world's clock after its tick 0: tick seconds × the tick rate, a fraction of a tick if need be.
- The first frame whose render time reaches that tick is drawn at exactly that tick, rather than at its render time. So the world a capture shows depends on the world and the tick alone (R21), not on the frame rate.
- A time before the first frame's render time, such as 0, the default, is drawn at the earliest tick the client still holds. That is at most a second before its newest snapshot (SpaceScene §6.4).
- The camera is the first view. Nothing moves it unless someone touches the window.

**The file** is a PNG image written by WIC's encoder as 24-bit BGR, a format the encoder takes as it is:
- each pixel's red, green and blue as the buffer holds them, already encoded as sRGB by the view it was drawn through;
- not its alpha, which the window never shows.

The name must end in `.png`, and a file already there is replaced. The client then closes its window, and the process exits with 0.

A failure ends the run with the story and exit code 1, as any failure does. That covers a file that cannot be written, and a window closed before the capture's time came.

**COM.** WIC's objects are COM's. `NeuronClient::ComApartment` gives the calling thread COM for as long as it lives:
- it enters the thread into the multithreaded apartment, and leaves it when it goes;
- a thread in a single-threaded apartment already stays there, which WIC works in as well.

The writer holds one, so no thread has to start COM beforehand.

**Linking.** `Outpost` and `NeuronClientTests` link `windowscodecs.lib`, which defines WIC's class and format identifiers, in all four configurations. No project changes any other setting.

**The command line refuses:**
- `--capture-at` without `--capture`;
- `--capture` with `--bench`, which ends the run in its own way;
- a file whose name does not end in `.png`;
- a time beyond an hour.

**Tests.** `NeuronClientTests` gains `CapturedFrameTests`, 2 tests:
- what `WritePng` writes, WIC reads back as a PNG image of 8-bit RGB at the frame's size, with every pixel's red, green and blue as the frame held them;
- an empty frame, or pixels of another count than its size, is refused before anything is written, and a folder that does not exist is WIC's failure.

The renderer's copy of the back buffer runs only in the game, which has a swap chain, so the owner's captures test it.

## What this forecloses

- **A capture of anything but the presented frame through this path.** The HDR color, the visibility buffer or a debug target would each need a readback of its own.
- **A capture at the client's time, or at a count of frames.**
- **Alpha, channels wider than eight bits, or a format other than PNG.**
- **A capture from `--bench`.**
