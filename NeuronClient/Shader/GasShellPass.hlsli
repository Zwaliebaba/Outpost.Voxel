#pragma once

// The gas shell pass (Design/ADR/ADR-025): a thread per pixel in 8 x 8 groups, after the sky and before bloom, that adds
// the frame's shells of hot gas to the HDR color, up to the depth the view splat wrote.

#include "GasShell.hlsli"

ConstantBuffer<ViewConstants> g_view : register(b0);
ConstantBuffer<GasShells> g_shells : register(b1);
Texture2D<float> g_depth : register(t0);
RWTexture2D<float4> g_color : register(u0);

// NeuronClient/GasShellPass.h relies on the same number.
static const uint GAS_SHELL_GROUP_PIXELS = 8;

// The input's semantic is on a struct member (Design/ADR/ADR-005).
struct GasShellThread
{
  uint3 pixel : SV_DispatchThreadID;
};

[numthreads(GAS_SHELL_GROUP_PIXELS, GAS_SHELL_GROUP_PIXELS, 1)] void AddGasShells(GasShellThread _thread)
{
  uint2 pixel = _thread.pixel.xy;
  if (pixel.x >= g_view.widthPixels || pixel.y >= g_view.heightPixels)
  {
    return;
  }
  float3 shells = GasShellPixel(g_view, float2(pixel) + 0.5, g_depth.Load(int3(pixel, 0)), g_shells);
  g_color[pixel] = g_color[pixel] + float4(shells, 0.0);
}
