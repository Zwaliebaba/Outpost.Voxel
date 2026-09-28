# ADR-010 — The canvas: DirectWrite text drawn straight by Direct3D 12

**Status:** accepted, 2026-09-28 · **Lands with:** the canvas, between M4 and M5 of [`Design/Archive/SampleRenderer.md`](../Archive/SampleRenderer.md) (§13, §15) · **Amended:** on 2026-09-28 the owner lifted four of its foreclosures: ClearType text; text scaled, rotated or placed off the pixel grid; evicting single glyphs; and color glyphs (D13). The canvas keeps its grayscale, hinted, pixel-aligned atlas until a HUD needs more, and each of the four lands with an ADR of its own; ClearType, for one, needs dual-source blending, which gives each subpixel its own alpha. Direct2D and Direct3D 11On12 stay out, as D13 has it.

## Context

§13 said the application has no in-window UI. The title carried the figures, and a UI library would have been a dependency and an ADR for no gain. Then the window became borderless fullscreen on the owner's request, which draws no title bar. While it has the screen, the figures show only in Alt+Tab or on a taskbar on another monitor.

On 2026-09-28 the owner asked for text on screen, and for it to be drawn by a class called the canvas, a 2D overlay that the HUD will draw on later. DirectWrite provides the text, and Direct3D 12 draws it directly, not through Direct2D. The same day the owner settled four choices:
- a glyph atlas rather than distance fields or outline geometry;
- DirectWrite's full text layout with a text renderer of our own, rather than glyph indices alone;
- the figures the title carries on a panel that F2 hides;
- the canvas's CPU twin in `NeuronClient` rather than `NeuronCore`.

The usual way to draw DirectWrite text on Direct3D 12 is Direct2D over Direct3D 11On12. That adds a second API to every frame, and synchronization between the two: the back buffer is wrapped for Direct3D 11, then flushed back. It gives ClearType. It also draws with code this repository cannot compare against a CPU twin on WARP, as R15 requires.

## Decision

**Where it lives.** `NeuronClient::Canvas` holds the overlay, and `NeuronClient::GlyphAtlas` its glyphs.
- The game decides what to draw, through `Renderer::Overlay()`.
- The renderer draws the canvas last, over the tone-mapped image or a debug view. It draws through the swap chain's sRGB view, so the blend runs in linear light and colors are given linear.

**Text.** DirectWrite lays text out and rasterizes glyphs on the CPU; the canvas uses nothing else of it.
- `IDWriteTextLayout` is created with GDI-compatible natural metrics at one pixel per DIP, so every advance is a whole pixel. Lines break at `\n` and nowhere else.
- The canvas's own `IDWriteTextRenderer`, written with C++/WinRT's `implements`, receives each glyph run with pixel snapping on. It turns every glyph into a quad over the glyph's bitmap.
- Underlines and strikethroughs become fills. Inline objects are not supported.

**Glyphs.** `IDWriteGlyphRunAnalysis` rasterizes each glyph once per font face, em size and glyph index, with the GDI natural rendering mode, grid fitting on and grayscale antialiasing.
- Glyphs are packed shelf by shelf, one texel apart, into a 1,024 × 1,024 `R8_UNORM` atlas of 1 MiB.
- The CPU keeps a copy. The rows that changed since the last upload reach the GPU in the command list of the frame that first draws them, through one upload buffer per frame in flight.
- A glyph that finds no room is left out of its frame, and the atlas starts over after that frame.
- The atlas is grayscale because ClearType's three coverages per pixel cannot be blended over a 3D image with one alpha. They would also be wrong under `--bench`'s stretched swap chain.

**Drawing.**
- A frame's quads reach the GPU through an upload ring, as a root SRV. Each quad is a `CanvasQuad`: position and size in pixels, the glyph's atlas texel, a fill flag, color and alpha, 44 bytes in all. Its layout is shared with HLSL under R16 and checked by the layout echo.
- One `DrawInstanced` draws four vertices per quad. The vertex shader places the corners on whole pixels.
- The pixel shader loads the glyph's texel by integer address, with no sampler, so a glyph's pixels are its bitmap texel for texel. It writes the color premultiplied by alpha times coverage, and the blend state lays that over the target.
- More than 16,384 quads in one frame are dropped.

**Twin.** R15 puts every twin in `NeuronCore`, which client and server share. The owner placed the canvas's twin, `CanvasShading`, in `NeuronClient`, beside the canvas, because only the client draws one. `AGENTS.md` R15 records the exception.
- The twin includes no Windows or Direct3D header.
- `NeuronCore` keeps its twins apart from the GPU code they check by the project boundary; here only review keeps them apart.
- A WARP test composites a frame's quads over the atlas's CPU copy with the twin and compares every pixel with what the GPU drew. That also shows the atlas reached the GPU as the CPU holds it.

**On screen.**
- The panel shows the figures the title carries, one per line: the adapter, the view, the frame time, the emissive scale, the explosion's time and scale, a pause, vsync off, and a missing debug layer.
- It sits on a translucent black panel in the top-left corner, in Consolas at 15 pixels on a 96 DPI monitor and larger in proportion on a denser one.
- It is on by default, and F2 hides and shows it.

## What this forecloses

- Direct2D and Direct3D 11On12.
- ClearType text.
- Text scaled, rotated or placed off the pixel grid. The atlas holds hinted bitmaps drawn texel for texel. A HUD that needs any of that needs another atlas mode, distance fields for instance, and an ADR.
- Evicting single glyphs: the atlas starts over when it is full.
- Color glyphs. `TranslateColorGlyphRun` is not used, so a color font draws in one color.
