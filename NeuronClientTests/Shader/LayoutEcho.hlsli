#pragma once

// The layout echo of Design/Archive/SampleRenderer.md §7.4 (R16): every field of every constant-buffer mirror, in declaration
// order, then of two elements of each mirror the shaders read from a structured buffer, written back as the 32-bit words
// the C++ structs hold them in. A mirror that places a field anywhere else, or a structured buffer whose stride differs
// from the struct's size, reads another field's word, and LayoutEchoTests sees it.

#include "BloomConstants.hlsli"
#include "CanvasQuad.hlsli"
#include "ExplosionConstants.hlsli"
#include "LightingConstants.hlsli"
#include "PaletteConstants.hlsli"
#include "PlacementConstants.hlsli"
#include "ShadowViewConstants.hlsli"
#include "SkyConstants.hlsli"
#include "StarRecord.hlsli"
#include "ViewConstants.hlsli"

ConstantBuffer<ViewConstants> g_view : register(b0);
ConstantBuffer<ShadowViewConstants> g_shadowView : register(b1);
ConstantBuffer<LightingConstants> g_lighting : register(b2);
ConstantBuffer<ExplosionConstants> g_explosion : register(b3);
StructuredBuffer<PaletteConstants> g_palettes : register(t0);
StructuredBuffer<PlacementConstants> g_placements : register(t1);
StructuredBuffer<CanvasQuad> g_canvasQuads : register(t2);
ConstantBuffer<SkyConstants> g_sky : register(b4);
ConstantBuffer<BloomConstants> g_bloom : register(b5);
StructuredBuffer<StarRecord> g_stars : register(t3);
RWByteAddressBuffer g_echo : register(u0);

void Echo(inout uint _word, uint _value)
{
  g_echo.Store(_word * 4u, _value);
  _word += 1u;
}

void Echo3(inout uint _word, uint3 _value)
{
  Echo(_word, _value.x);
  Echo(_word, _value.y);
  Echo(_word, _value.z);
}

[numthreads(1, 1, 1)] void LayoutEcho()
{
  uint word = 0u;
  Echo3(word, asuint(g_view.position));
  Echo(word, asuint(g_view.tanHalfFovY));
  Echo3(word, asuint(g_view.right));
  Echo(word, asuint(g_view.aspect));
  Echo3(word, asuint(g_view.up));
  Echo(word, asuint(g_view.nearPlane));
  Echo3(word, asuint(g_view.forward));
  Echo(word, g_view.widthPixels);
  Echo(word, g_view.heightPixels);

  Echo3(word, asuint(g_shadowView.origin));
  Echo(word, asuint(g_shadowView.halfWidth));
  Echo3(word, asuint(g_shadowView.right));
  Echo(word, asuint(g_shadowView.halfHeight));
  Echo3(word, asuint(g_shadowView.up));
  Echo(word, asuint(g_shadowView.depthRange));
  Echo3(word, asuint(g_shadowView.forward));
  Echo(word, g_shadowView.widthPixels);
  Echo(word, g_shadowView.heightPixels);

  Echo3(word, asuint(g_lighting.toSun));
  Echo(word, asuint(g_lighting.shadowNormalOffset));
  Echo3(word, asuint(g_lighting.sunRadiance));
  Echo(word, asuint(g_lighting.emissiveGain));
  Echo3(word, asuint(g_lighting.skyColor));
  Echo(word, asuint(g_lighting.skyIntensity));
  Echo3(word, asuint(g_lighting.groundColor));
  Echo(word, g_lighting.placementCount);
  Echo3(word, asuint(g_lighting.background));

  Echo3(word, asuint(g_explosion.blastOrigin));
  Echo(word, asuint(g_explosion.timeSeconds));
  Echo3(word, asuint(g_explosion.inheritedVelocity));
  Echo(word, asuint(g_explosion.launchSpeed));
  Echo(word, asuint(g_explosion.falloffDistance));
  Echo(word, asuint(g_explosion.directionJitter));
  Echo(word, asuint(g_explosion.speedJitter));
  Echo(word, asuint(g_explosion.drag));
  Echo(word, g_explosion.maxQuarterTurns);
  Echo(word, g_explosion.seed);
  Echo(word, g_explosion.hashBase);

  Echo3(word, asuint(g_sky.toSun));
  Echo(word, asuint(g_sky.sunAngularRadiusRadians));
  Echo3(word, asuint(g_sky.sunRadiance));
  Echo(word, g_sky.seed);
  Echo3(word, asuint(g_sky.galaxyX));
  Echo(word, asuint(g_sky.galaxyGain));
  Echo3(word, asuint(g_sky.galaxyY));
  Echo(word, asuint(g_sky.starGain));
  Echo3(word, asuint(g_sky.galaxyZ));

  Echo(word, g_bloom.widthPixels);
  Echo(word, g_bloom.heightPixels);
  Echo(word, g_bloom.karis);
  Echo(word, asuint(g_bloom.belowShare));

  [unroll] for (uint palette = 0u; palette < 2u; ++palette)
  {
    [unroll] for (uint i = 0u; i < PALETTE_ENTRY_COUNT; ++i)
    {
      PaletteMaterial material = g_palettes[palette].materials[i];
      Echo3(word, asuint(material.albedo));
      Echo(word, asuint(material.emissiveScale));
    }
  }

  [unroll] for (uint placementIndex = 0u; placementIndex < 2u; ++placementIndex)
  {
    PlacementConstants placement = g_placements[placementIndex];
    Echo3(word, asuint(placement.axisX));
    Echo(word, placement.firstRecord);
    Echo3(word, asuint(placement.axisY));
    Echo(word, placement.recordCount);
    Echo3(word, asuint(placement.axisZ));
    Echo(word, placement.firstVoxel);
    Echo3(word, asuint(placement.translation));
    Echo(word, placement.paletteIndex);
  }

  [unroll] for (uint quad = 0u; quad < 2u; ++quad)
  {
    CanvasQuad canvasQuad = g_canvasQuads[quad];
    Echo(word, asuint(canvasQuad.pixelX));
    Echo(word, asuint(canvasQuad.pixelY));
    Echo(word, canvasQuad.widthPixels);
    Echo(word, canvasQuad.heightPixels);
    Echo(word, canvasQuad.atlasX);
    Echo(word, canvasQuad.atlasY);
    Echo(word, canvasQuad.fill);
    Echo3(word, asuint(canvasQuad.color));
    Echo(word, asuint(canvasQuad.alpha));
  }

  [unroll] for (uint star = 0u; star < 2u; ++star)
  {
    StarRecord record = g_stars[star];
    Echo3(word, asuint(record.direction));
    Echo(word, asuint(record.flux));
    Echo3(word, asuint(record.color));
  }
}
