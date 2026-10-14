# Changelog

## Unreleased

- Added versioned `.licmat` material resources with base color, metallic, roughness and emission settings.
- Updated scene serialization to v2 while retaining v1 scene loading.
- Connected metallic, roughness and emission values to the Vulkan surface shader and added material save/load controls in the inspector.
- Moved VOIDBOUND wave/enemy/XP/score/upgrade balance into the NEXUS `GameRules.nx` source.
- Replaced the website's decorative mock viewport/cards with a plain responsive project/download page.

- Added VOIDBOUND, a playable arena-survival action game using the LIC Engine world and Vulkan renderer.
- Added player movement, dash, pulse projectiles, enemy AI, scaling waves, score, health, experience pickups, level-ups and four upgrades.
- Added game HUD, main menu, pause/restart/game-over overlays and standalone `voidbound` launch targets for Vulkan builds.
- Added headless tests for movement, pause/resume, run startup and menu cleanup.

- Added a Dear ImGui editor shell with scene tree, transform/material inspector, FileSystem, Output and Console panels.
- Added editable mesh-node transforms/material colors, node create/duplicate/delete, viewport orbit/zoom and a temporary Play/Stop preview.
- Added versioned scene save/load and malformed-file validation tests.
- Added per-object push constants so scene transforms and primitive colors affect the Vulkan preview.

- Added a compact, responsive dark website with honest project status and platform downloads.
- Added GitHub Pages deployment for the `site/` directory.
- Made the complete Windows ZIP the supported download; legacy standalone EXE assets are removed because they omit required runtime files.
- Added a native Windows startup error dialog and `lic-engine-startup.log` diagnostics.
- Made the batch launcher display startup logs and pause after errors.
- Made Windows packages verify that `vulkan-1.dll` and both compiled shaders are included.
- Added an explanatory dialog when a Windows core-only build is opened without command-line arguments.
- Updated download instructions and packaging.
