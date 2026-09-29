# ADR-022 — Bloom

**Status:** accepted, 2026-09-28 · **Lands with:** S-M5 of [`Design/Archive/SpaceScene.md`](../Archive/SpaceScene.md) (§12.2, §15) · **Refines:** §12.2, on how the taps are read and how the levels come back up the chain; §15, on the constant image, which truncation can take a step off

## Context

§12.2 decided bloom: no threshold, and a share of every pixel's light spread over a wide kernel built as Jimenez built it for *Call of Duty: Advanced Warfare* (2014). The kernel halves the HDR image level by level with a 13-tap filter, with Karis's average in the first halving, and brings the levels back up with a 3 × 3 tent. §12.2 also argued that a CPU twin agrees with the GPU to rounding, because every tap lands where the sampler's bilinear weights are exact. Building it settled five things §12.2 and §15 left open or got slightly wrong: the levels' sizes, what Karis's average weights, how the levels come back up, how the taps are read, and how the GPU stores what it writes.

## Decision

**The chain.**
- **Levels.** Bloom halves the view while the next level's smaller dimension stays at least 16 texels, so it has at least one level and at most eight. That is six levels at 1080p, seven at 4K, five at 720p and eight at 8K, so the kernel keeps its size on the screen as the view grows.
- **Sizes.** Each level is half the one above, rounded up. At any size, odd ones included, a texel's centre then lies on the corner between four texels of the level above, and taps past an edge clamp to it.
- **Memory.** The levels are `R16G16B16A16_FLOAT`, like the HDR color. At 1080p they hold 691,110 texels, 5.53 MB by arithmetic.

**Down** (`BloomDownsample`):
- **Taps.** Thirteen taps on the corners at offsets of 0, ±1 and ±2 texels of the level above, taken as five boxes of four: the middle box weighs a half and the four others an eighth each.
- **Karis's average.** The first halving weights each tap within its box by 1 / (1 + its Rec. 709 luminance), and normalizes box by box. This is Jimenez's "partial" Karis average.
- **Why per tap.** The first cut weighted whole boxes instead. That cannot tame one brilliant texel: all five boxes share it, so their weights move together. Measured by `KarisAverageTamesABrilliantTexel`, one texel of 10,000 gives its level-one neighbour 625 with a plain average, 625 with weighted boxes, and 0.333 with weighted taps.

**Up** (`BloomUpsample`, `BloomTent`):
- **Blend in place.** From the second-smallest level up, each level is blended with the tent over the level below it: *level* ← lerp(*level*, tent, *n* / (*n* + 1)), where *n* counts the levels below. Each level then ends as the average of its own blur and every smaller one's.
- **The tent.** Nine taps a texel of the lower level apart, weighted 1, 2 and 1 along each axis. Each tap lies a quarter of the way between texel centres, where its bilinear weights are a quarter and three quarters.
- **Averaging, not adding.** §12.2 says the levels are added back up. They are averaged, which is the same sum divided by the number of levels, taken as the chain goes. The division matters: a sum could pass half precision's largest value, 65,504. The sun's disc is about 12,500 at the default sun, and six levels of it would add up to about 75,000. An average never exceeds the brightest texel in it.
- **Typed UAV loads.** Reading a level while writing it needs typed UAV loads of `R16G16B16A16_FLOAT`. Feature levels 12_0 and above require them, and the device is created at 12_1 (Microsoft's "Typed unordered access view loads"), so the chain holds one set of levels, not two.

**The taps are read with explicit loads, not through the sampler.**
- **Why not the sampler.** §12.2's argument about the sampler's 8-bit subtexel weights holds at these positions. But the sampler takes a normalized coordinate, and *p* / *W* rounded to single precision can land 2^-11 of a texel below the corner. Whether the fixed-point conversion then rounds or truncates is the hardware's choice, and truncation would move a weight by 1/256.
- **What the loads cost.** Each tap is four loads with its exact weights, so the GPU's sums agree with the twin's to single-precision rounding on any hardware, which a store can turn into one step (below). On the way down that is 36 distinct texels for 13 taps. Bloom is timed as a pass of its own (`GpuPass::Bloom`), and the bench's CSV and summary carry it.

**The tone map** mixes bloom in before the exposure and the ACES fit.
- It takes the tent over the chain's first level at every pixel of the view, and shows *color* + (*bloom* − *color*) × 0.04 (`BLOOM_SHARE`, §12.2's default share).
- Bloom and the sky run only for the lit image, and a debug view skips them.

**The twin** is `NeuronCore/Bloom.h`. It stores every texel as the GPU does, through `NeuronCore/Half.h`.
- **Stores truncate.** Direct3D converts a float to a narrower float by rounding toward zero, and what lies past the largest half, 65,504, becomes the largest half rather than infinity (Microsoft's "Data conversion rules"). `FloatToHalf` does the same, for the sky's twin as for bloom's.
- **How that was found.** The first cut rounded to the nearest, ties to even, and the first WARP run failed: the GPU's third level lay up to three steps under the twin's. A twin that truncates gives the failing texel bit for bit, and the sky's failing pixel too (ADR-021). It also agrees with the nearest-rounding twin on exactly the texels the GPU matched that twin on: 5,059 of 20,475 in the first level, and 17 of 5,194 in the second.
- **What truncation costs.** A store never rounds up, so each loses less than a step, at most a thousandth of the value in a half's normal range. Faint texels lose a larger share: a dim texel's light, spread over the chain, keeps 98.5 % of its sum. And Karis's average divides. Where the quotient for a constant lands a float's rounding under it, the first halving takes a step off: 3,160 of the 31,743 positive halves as grays, both with GCC and with MSVC in CI. Every level after the first keeps what the first holds. So §15's constant image comes back unchanged but for that one step, and the test says so.
- **CPU tests.** A constant image comes back from the chain as above, at an odd 257 × 129, and every positive half is tried as a gray at the first halving. A bright texel's spread mirrors with its mirror image and with its transpose. A dim texel's light keeps its centre, at column 45.497 for 45.5, and its sum within 2 %: 0.015396 of 0.015625, as measured by `SpreadsABrightTexelSymmetrically`.
- **WARP tests.** `BloomPassTests` compares every texel of every level against the twin, on the way down from an HDR color the test writes, and on the way up from the levels the GPU wrote going down. It then compares the tone map's output with the twin's mix of the first level the GPU holds. It logs, level by level, how many texels match the twin to the bit and the most halves any other strays, and the most the tone map strays.
- **Measured.** On the run of 75273a9, the first with the truncating twin, every texel of all three levels matched the twin to the bit, on the way down and on the way up, and the tone map's output matched the twin's exactly. The bounds, four steps and a floor near zero, stay as headroom for a GPU that sums a texel's taps in another order.
- **On hardware.** The owner has seen bloom on hardware, as S-M5's done-when asks (SpaceScene §16), and confirmed it on 2026-09-29. The share stays 4 %, to be judged with the rest of the look in S-M6 (§12.1).

## Consequences

- **No threshold.** There is no threshold to tune, and nothing pops on or off. The share is the one knob, a constant in both languages.
- **Resolution.** The kernel's size follows the screen, not the pixel: a 4K view gets one level more than 1080p.
- **The sampler.** Moving the taps back onto the sampler would need a new argument that the twin still agrees, and a measurement of what it saves.
- **Stores.** Every twin of a half-precision target stores as Direct3D does, toward zero. A twin of another narrow float format, such as an 11- or 10-bit one, needs the same rule: one that rounds to the nearest drifts from the GPU by up to a step a store.
