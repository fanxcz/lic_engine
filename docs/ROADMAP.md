# LIC Engine Roadmap

A milestone is not complete because directories or placeholder classes exist. Each milestone has observable exit criteria.

## M0 — Repository foundation

- [ ] C++23 and documented minimum CMake version.
- [ ] Real CMake targets for modules as they are implemented.
- [ ] Compiler warning policy and consistent formatting.
- [ ] CI configure/build/test workflow on Windows and Linux.
- [ ] Logging, assertions, result/error type, and configuration loading.
- [ ] Dependency inventory with versions, licenses, and update policy.

**Exit criteria:** clean configure and build from a fresh checkout; CTest passes; CI reports real results.

## M1 — First real 3D frame

- [ ] Window creation and resize events.
- [ ] Vulkan instance/device selection with useful failure diagnostics.
- [ ] Swapchain recreation and correct shutdown.
- [ ] Vertex/index buffers, depth testing, camera, and a lit mesh.
- [ ] Frame-time display and renderer smoke test.
- [ ] Sample scene launchable with a documented command.

**Exit criteria:** sample visibly renders on a supported desktop GPU, survives resize, exits cleanly, and reports unsupported setups clearly.

## M2 — World and saved scenes

- [x] Stable entity handles and core components (initial generational ECS world and component storage are implemented).
- [ ] Transform hierarchy and documented coordinate convention.
- [ ] Scene save/load with versioned serialization.
- [ ] Round-trip tests and malformed-file diagnostics.
- [ ] glTF static mesh and material import.
- [ ] GUID-based asset references and import metadata.

**Exit criteria:** a scene can be saved, restarted, loaded, and rendered with the same transforms and asset references.

## M3 — Editor essentials

- [ ] Dockable editor shell with viewport, hierarchy, inspector, and console.
- [ ] Orbit/fly camera and focus-selection behavior.
- [ ] Select, create, duplicate, rename, and delete entities.
- [ ] Transform gizmos and snapping.
- [ ] Edit component values and save scenes.
- [ ] Dirty-state indicator and basic undo/redo.
- [ ] Play/stop using the runtime scene format.

**Exit criteria:** create a small scene without editing source, save, play, stop, and reopen it.

## M4 — Gameplay loop

- [ ] Action-based input mappings.
- [ ] Fixed-step update and frame-rate-independent timing.
- [ ] Jolt adapter for rigid bodies, collision shapes, and ray queries.
- [ ] miniaudio adapter for playback and spatial attenuation.
- [ ] Prefabs and reusable scene objects.
- [ ] Documented C++ game-module example.

**Exit criteria:** an interactive sample uses input, physics, and audio through engine APIs.

## M5 — Asset workflow and standalone games

- [ ] Asset browser with filtering and import errors.
- [ ] Dependency tracking and safe asset reimport.
- [ ] Standalone runtime separate from editor.
- [ ] Project build/package command and reproducible output layout.
- [ ] Sample game packaged without editor dependencies.
- [ ] Release build and smoke tests on Windows and Linux.

**Exit criteria:** a clean package runs on a supported machine without the editor or developer build tree.

## M6 — Performance and visual quality

- [ ] CPU frame-time breakdown and memory counters.
- [ ] GPU timings where supported.
- [ ] PBR materials and directional shadows.
- [ ] Instancing/batching and visibility culling when measurements justify them.
- [ ] Scalable quality settings and benchmark scene.
- [ ] Profiling-driven optimization, not unmeasured complexity.

**Exit criteria:** benchmark results and target hardware are documented; no performance claims without reproducible measurements.

## M7 — Additional platforms and advanced systems

- [ ] Android lifecycle, touch input, packaging, and real-device validation.
- [ ] Mobile renderer capability tiers and memory budgets.
- [ ] Animation blending, skeletal import, and animation editor tools.
- [ ] Navigation and gameplay systems driven by sample-game needs.
- [ ] Evaluate web export after selecting a viable graphics and packaging path.

**Exit criteria:** each claimed platform has a documented build, launch, input, and smoke-test path. Experimental support is labelled honestly.

## Release gates

1. Configure and build from a clean directory.
2. Run automated tests and explain skipped tests.
3. Launch/smoke-test where a display or GPU is required.
4. Review warnings, errors, resource cleanup, and regressions.
5. Update docs and list verified versus unverified behavior.
