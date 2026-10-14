# LICMAT material format

LIC Engine materials are versioned, human-readable `.licmat` text resources. They store surface values—not Vulkan handles—so materials are portable between scenes and projects. The current shader consumes the supported surface properties using a compact 96-byte per-object push-constant block.

```text
LICMAT 1
name "Voidbound Player"
base_color 0.25 0.92 0.98 1.0
metallic 0.45
roughness 0.22
emission 0.20
```

- `base_color`: linear red, green, blue and alpha channels, each from 0 to 1.
- `metallic`: 0 (non-metal) to 1 (metal).
- `roughness`: 0.04 (smooth) to 1 (rough).
- `emission`: 0 to 4; adds color-matched emissive output.

The editor inspector lets you tune these properties, save the selected object's surface as `.licmat`, or apply an existing material from `Assets/Materials`. Shipped defaults for the player, enemies, projectiles and XP shards are copied to a project without overwriting existing user files.

Scene files were upgraded to `LICSCENE 2` to preserve the new properties. `LICSCENE 1` remains supported and loads with default metallic/roughness/emission values.

## Current limitations

The active Vulkan test renderer currently draws primitive cubes. This material format is compatible with LIC Engine's current surface shader, but it is not yet a full glTF/PBR material system: texture maps, normal maps, light sources, shadows and a general material graph are future work.
