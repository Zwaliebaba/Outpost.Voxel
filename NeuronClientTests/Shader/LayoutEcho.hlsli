#pragma once

// The layout echo of Design/SampleRenderer.md §7.4 (R16): every field of every constant-buffer mirror, in declaration
// order, written back as the 32-bit words the C++ struct holds them in. A mirror that places a field anywhere else reads
// another field's word, and LayoutEchoTests sees it.

#include "InstanceConstants.hlsli"
#include "LightingConstants.hlsli"
#include "PaletteConstants.hlsli"
#include "ShadowViewConstants.hlsli"
#include "ViewConstants.hlsli"

ConstantBuffer<ViewConstants> g_view : register(b0);
ConstantBuffer<InstanceConstants> g_instance : register(b1);
ConstantBuffer<PaletteConstants> g_palette : register(b2);
ConstantBuffer<ShadowViewConstants> g_shadowView : register(b3);
ConstantBuffer<LightingConstants> g_lighting : register(b4);
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

  Echo3(word, asuint(g_instance.modelOrigin));
  Echo(word, g_instance.firstRecord);
  Echo(word, g_instance.recordCount);

  [unroll] for (uint i = 0u; i < PALETTE_ENTRY_COUNT; ++i)
  {
    Echo3(word, asuint(g_palette.materials[i].albedo));
    Echo(word, asuint(g_palette.materials[i].emissiveScale));
  }

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
  Echo3(word, asuint(g_lighting.groundAlbedo));
  Echo(word, g_lighting.groundVisible);
  Echo3(word, asuint(g_lighting.background));
}
