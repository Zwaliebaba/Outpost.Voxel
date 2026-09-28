#pragma once

// The packing of the voxel record and of the visibility buffer's normal (Design/Archive/SampleRenderer.md §7.1, §7.3). The C++
// twins are NeuronCore/VoxelRecord.h and NeuronCore/OctahedralNormal.cpp (R15).

// The visibility buffer's value for a pixel that no voxel covers, as NeuronCore/TraceHit.h names it.
static const uint NO_VOXEL = 0xFFFFFFFFu;

static const float SNORM16_SCALE = 32767.0;

struct VoxelRecord
{
  uint x; // model coordinates, in voxels
  uint y;
  uint z;
  uint color; // the palette entry minus one: 0 to 15
};

// Bits 0-23 hold x, y and z, bits 24-27 the color, and bits 28-31 are zero (R14).
uint PackVoxelRecord(VoxelRecord _record)
{
  return _record.x | (_record.y << 8u) | (_record.z << 16u) | ((_record.color & 0xFu) << 24u);
}

VoxelRecord UnpackVoxelRecord(uint _packed)
{
  VoxelRecord record;
  record.x = _packed & 0xFFu;
  record.y = (_packed >> 8u) & 0xFFu;
  record.z = (_packed >> 16u) & 0xFFu;
  record.color = (_packed >> 24u) & 0xFu;
  return record;
}

float SignNotZero(float _value)
{
  return _value >= 0.0 ? 1.0 : -1.0;
}

// round() rounds half to even, as std::nearbyint does in the twin.
uint PackSnorm16(float _value)
{
  return uint(int(round(clamp(_value, -1.0, 1.0) * SNORM16_SCALE))) & 0xFFFFu;
}

float UnpackSnorm16(uint _bits)
{
  int value = int(_bits << 16u) >> 16;
  return max(float(value) / SNORM16_SCALE, -1.0);
}

// An octahedral map: two 16-bit snorm values in one uint. The six axis directions survive the round trip exactly.
uint PackOctahedralNormal(float3 _normal)
{
  float manhattan = abs(_normal.x) + abs(_normal.y) + abs(_normal.z);
  float x = _normal.x / manhattan;
  float y = _normal.y / manhattan;
  if (_normal.z < 0.0)
  {
    float foldedX = (1.0 - abs(y)) * SignNotZero(x);
    float foldedY = (1.0 - abs(x)) * SignNotZero(y);
    x = foldedX;
    y = foldedY;
  }
  return PackSnorm16(x) | (PackSnorm16(y) << 16u);
}

float3 UnpackOctahedralNormal(uint _packed)
{
  float3 normal = float3(UnpackSnorm16(_packed), UnpackSnorm16(_packed >> 16u), 0.0);
  normal.z = 1.0 - abs(normal.x) - abs(normal.y);
  float fold = max(-normal.z, 0.0);
  normal.x += normal.x >= 0.0 ? -fold : fold;
  normal.y += normal.y >= 0.0 ? -fold : fold;
  return normalize(normal);
}
