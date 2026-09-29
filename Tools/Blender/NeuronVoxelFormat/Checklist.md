# The extension, checked by hand

[`Design/Archive/NeuronVoxelFormat.md`](../../../Design/Archive/NeuronVoxelFormat.md) §9 called N-M4 done when this checklist had passed by hand on the frigate and on the capital ship, as it has ([ADR-020](../../../Design/ADR/ADR-020-nvf-import.md)). CI has no Blender. `Tests/ExtensionTests.py` drives the same operators headless, but it cannot look at a viewport, a panel or a dialog, and this checklist can. When you run it, record the date, the Blender version and anything that surprised you in ADR-020, beside N-M4's runs.

## Before you start

1. Build the extension with the Blender you will check with, from the repository root:
   `blender --command extension build --source-dir Tools/Blender/NeuronVoxelFormat --output-dir <a scratch folder>`
   It writes `neuron_voxel_format-1.0.0.zip`, without `Tests/` or this file.
2. In Blender, go to *Edit › Preferences › Get Extensions*, open the menu at the top right, choose *Install from Disk…*, and pick the zip. *Neuron Voxel Format* appears among the add-ons, enabled.
3. Copy `GameData/Frigate.vox` and `Frigate.nvf`, and `CapitalShip.vox` and `CapitalShip.nvf`, into a scratch folder, and work on the copies. The repository's assets stay as they are.

## The frigate, then the capital ship

Do each step for the frigate, then again for the capital ship.

1. **Import.** Choose *File › Import › Neuron Voxel (.nvf)* and pick the copied `.nvf`. A collection named after the file holds the ship and a small sphere, its pivot, at its middle. Set *Viewport Shading › Color* to *Attribute*: the ship shows the palette's colors. Clicking the hull does not select it, and its transform fields are locked.
2. **Add an engine.** Put the 3D cursor on the stern with Shift and a right-click. In *N › NVF*, press *Add Hardpoint*: the part is `main`, the type `engine`, the identifier `main`. An arrows empty appears at the cursor, with its Y arrow pointing forward. Press *Snap position to › Face*. Turn it half a turn about Z (*R Z 180*) so that its Y arrow points aft, and press *Snap Rotation to 90°*.
3. **Add a weapon.** Put the cursor on the bow and add a hardpoint of type `weapon` with identifier `left`. Press *Snap position to › Center*. Turn it a little about Z, and leave it unsnapped.
4. **Validate.** Press *Validate*: it says the model is valid.
5. **Export.** Choose *File › Export › Neuron Voxel (.nvf)*. It offers the file you imported; export over it. Then save the `.blend` in the scratch folder for step 9. A `.blend` is scratch, and git ignores it.
6. **Look at the file.** Run `NvfImport --dump <file>.nvf`: it lists `engine.main` and `weapon.left`, neither from the `.vox`. Run `NvfImport <file>.vox <file>.nvf --check`: the file is up to date, exit code 0.
7. **Re-import.** In a new file (*File › New › General*), import the `.nvf` again. Both hardpoints are where you put them, and their arrows point as they did.
8. **Change the voxels.** Open the copied `.vox` in MagicaVoxel, repaint or add a voxel, and save. `--check` now says the file is stale, exit code 1. Run `NvfImport <file>.vox <file>.nvf`, then `--dump`: both hardpoints are still listed.
9. **A stale export.** Open the `.blend` you saved in step 5, which still holds the old voxels, select the ship's weapon, and export over the file. The export is refused with a message saying that NvfImport has run since, and the file is unchanged.
10. **Re-import.** Import the `.nvf` once more: the voxel change shows, and so do both hardpoints.

## Also worth a look

- **The station.** Import `GameData/MilitaryStation.nvf`, which previews as 360,330 faces. Note how long the import takes and whether the viewport stays responsive (§11).
- **Refusals.** Clear a hardpoint's parent (*Alt P*) and press *Validate*: it says the hardpoint is not parented to a part. Undo that, give two hardpoints one name with *Rename*, and it refuses the name. Export refuses what Validate lists.
- **Two ships in one file.** Import the frigate twice. The second collection is named `Frigate.001` and its objects' names differ from the first's. Select an object of either ship: the panel and *Validate* speak for that ship alone.
