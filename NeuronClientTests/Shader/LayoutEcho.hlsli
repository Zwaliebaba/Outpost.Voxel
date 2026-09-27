#pragma once

// The layout echo of Design/SampleRenderer.md §7.4 (R16): every field of every constant-buffer mirror, in declaration
// order, written back as the 32-bit words the C++ struct holds them in. A mirror that places a field anywhere else reads
// another field's word, and LayoutEchoTests sees it.

#include "InstanceConstants.hlsli"
#include "PaletteConstants.hlsli"
#include "ViewConstants.hlsli"

ConstantBuffer<ViewConstants> g_view : register(b0);
ConstantBuffer<InstanceConstants> g_instance : register(b1);
ConstantBuffer<PaletteConstants> g_palette : register(b2);
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
}
