"""The Neuron Voxel Format in Blender: import an .nvf, author its hardpoints and pivots, and export it again
(Design/Archive/NeuronVoxelFormat.md §7).

Blender edits hardpoints and pivots only. The voxels, the colors and the part tree come from the file and go back to it
unchanged (N5). NvfFormat.py is the format, the Python twin of NeuronCore's (AGENTS.md R20); Geometry.py and
Rebuild.py hold what the extension decides without Blender; the Linux CI job tests all three. The rest is Blender's
side: the import and export operators, the preview and the sidebar panel.
"""

from . import ExportOperator, HardpointPanel, ImportOperator

MODULES = (ImportOperator, ExportOperator, HardpointPanel)


def register():
  for module in MODULES:
    module.register()


def unregister():
  for module in reversed(MODULES):
    module.unregister()
