"""A part's preview mesh: its surface, from Geometry.surface, colored from the palette (§7).

Each face carries its voxel's color as a byte color attribute on the face's corners, written in the palette's sRGB
bytes. Solid shading shows it with Color set to Attribute; the material, which reads the same attribute, shows it in
Material Preview and Rendered.
"""

import bpy

from . import Geometry

COLOR_ATTRIBUTE = 'nvf_color'
MATERIAL_NAME = 'NVF Preview'


def preview_material():
  """The one material every preview shares: the color attribute into a Principled BSDF's base color."""
  material = bpy.data.materials.get(MATERIAL_NAME)
  if material is not None:
    return material
  material = bpy.data.materials.new(MATERIAL_NAME)
  material.use_nodes = True
  nodes = material.node_tree.nodes
  shader = next(node for node in nodes if node.type == 'BSDF_PRINCIPLED')
  attribute = nodes.new('ShaderNodeVertexColor')
  attribute.layer_name = COLOR_ATTRIBUTE
  attribute.location = (shader.location.x - 300, shader.location.y)
  material.node_tree.links.new(attribute.outputs['Color'], shader.inputs['Base Color'])
  return material


def build_mesh(name, records, palette):
  """A mesh of the surface of `records`, one part's voxels, in Blender's axes and in the part's space."""
  vertices, faces, colors = Geometry.surface(records)
  mesh = bpy.data.meshes.new(name)
  mesh.from_pydata(vertices, [], faces)
  attribute = mesh.color_attributes.new(COLOR_ATTRIBUTE, 'BYTE_COLOR', 'CORNER')
  srgb = [tuple(channel / 255.0 for channel in (entry.red, entry.green, entry.blue, entry.alpha)) for entry in palette]
  corner_colors = []
  for color in colors:
    corner_colors.extend(srgb[color] * 4)
  attribute.data.foreach_set('color_srgb', corner_colors)
  mesh.color_attributes.active_color = attribute
  mesh.materials.append(preview_material())
  mesh.validate()
  mesh.update()
  return mesh
