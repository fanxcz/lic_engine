# LIC Engine Architecture

## Goals

LIC Engine is a native C++ game engine with a usable editor and distributable runtime. The editor must not be a dependency of shipped games. Platform APIs must not leak into gameplay code, and subsystems should be testable in isolation.

## Runtime boundaries

- **Core:** types, errors/results, logging, assertions, time, configuration, IDs, and memory utilities. No graphics or window-system dependency.
- **Platform:** window, file system, paths, input events, timers, and platform capability reporting behind interfaces.
- **Renderer:** GPU resources, swapchain, command submission, shaders, materials, meshes, cameras, lights, and render passes. Vulkan is the first backend; clients do not call Vulkan directly.
- **World:** entities, stable handles, components, transforms, hierarchy, scene lifecycle, and serialization.
- **Assets:** GUIDs, import settings, dependency records, source-to-runtime conversion, caching, and actionable diagnostics.
- **Physics:** fixed-step simulation, rigid bodies, collision shapes, layers, queries, and synchronization with the world. Jolt is wrapped behind an adapter.
- **Audio:** listener, emitters, playback, attenuation, and asset lifetime. Optional in headless tests.
- **Input:** physical events mapped to named actions and axes. Gameplay uses actions rather than platform key codes.
- **Animation:** skeletons, clips, blend evaluation, and root-motion interfaces, after import and transforms are stable.
- **Runtime:** application lifecycle, world loading, update/render scheduling, game module loading, and standalone execution.
- **Editor:** project management, viewport, hierarchy, inspector, asset browser, console, profiler, and play-in-editor. Runtime never depends on editor code.

## Dependency direction

    Editor -> Runtime -> Core
       |        |-----> Platform
       |        |-----> World
       |        |-----> Renderer
       |        |-----> Physics adapter
       |        |-----> Audio adapter
       |        |-----> Input
       +------> Assets <---- Runtime
    Standalone Runner -> Runtime

This is conceptual ownership, not permission for circular includes. Shared interfaces belong in small public headers. CMake targets should enforce dependency direction where practical.

## World and object model

The initial `LIC::World` module implements generation-safe entity handles, entity creation/destruction, typed component add/get/has/remove operations, mutable/const component iteration, and packed sparse-set storage. Destroying an entity removes its components; recycled entity indices receive a new generation so stale handles cannot access a new object. Automated tests cover duplicate components, missing components, stale handles, cleanup, index recycling, and dense-storage compaction.

The next world milestone is a transform component and hierarchy, followed by versioned scene serialization. A transform has local position, rotation, and scale; world transforms derive from the hierarchy. Define coordinate convention, handedness, angle unit, and world unit before implementing importers. Scene serialization must use stable IDs, never raw memory addresses or storage indices.

Do not build a universal reflection system before the first scene works. Start with explicit component metadata and serialization hooks, then expand to meet editor needs.

## Rendering strategy

1. Create a window and Vulkan instance/device with capability checks.
2. Render a frame with swapchain, command buffers, and depth buffer.
3. Add camera transforms, vertex/index buffers, shader compilation, and a textured mesh.
4. Add physically based material inputs, directional lighting, and shadow mapping.
5. Add asset-driven materials, batching/instancing, diagnostics, and GPU timing.
6. Introduce a render graph, asynchronous uploads, and advanced post-processing only after the basics are reliable.

Every GPU resource needs explicit ownership and a documented destruction order. Shader or device failures must produce readable diagnostics rather than silent black screens.

## Editor UX

- Large central viewport; hierarchy on the left; inspector on the right; bottom tabs for assets, console, problems, profiler, and build output.
- Clear selection, focus, gizmo modes, camera controls, snapping, undo/redo, and dirty-state indicators.
- Play, pause, single-step, and stop are distinct actions. Runtime changes must not silently overwrite the authored scene.
- Editor and runtime share serialization and asset IDs.
- Panels are resizable; shortcuts are discoverable; actions have visible feedback.
- Dark, restrained, information-dense design—not a marketing dashboard.

## Threading and frame scheduling

Start with a predictable main-thread loop and fixed-step physics accumulator. Do not add a job system until ownership and synchronization boundaries are clear. GPU work, asset loading, and editor UI need explicit thread-safety contracts. Background tasks must not mutate world internals arbitrarily.

## Errors and observability

Errors should identify the operation and relevant asset/resource path, and suggest a next step. Add structured log levels, console, CPU frame-time tracking, memory counters, and GPU timing when supported. Avoid catch-all error suppression.

## Build and dependency policy

- CMake targets represent modules and declare dependencies.
- Pin third-party versions or commits; record license and attribution.
- Keep generated files and build products out of source directories.
- Keep public headers self-contained and compile a header-smoke target.
- Unit-test core, serialization, IDs, transforms, and asset metadata without a GPU.
- Run renderer smoke tests on supported Vulkan drivers; report skipped tests explicitly.

## Platform strategy

Prove the complete path on Windows and Linux desktop first. Keep path, input, window, and graphics interfaces portable. Android later requires lifecycle handling, touch input, packaging, and mobile GPU validation; compiling the core alone does not mean Android is supported. Web export needs a separately planned graphics and packaging strategy.


## NEXUS and engine asset contracts

The NEXUS compiler and its standard library are shipped beside the graphical editor. `assets/scripts/GameRules.nx` is the checked-in source of gameplay balance; the editor copies it into the user's writable project without overwriting edits. The engine validates the script's printed configuration values before applying them to the simulation. NEXUS does not yet run in-process as a per-frame gameplay VM, so Vulkan, the OS/event loop and ECS storage are native modules; the intended next step is a supported host API for NEXUS entity/input/update callbacks.

Material resources are versioned `.licmat` text assets, independent of Vulkan handles. Each scene object's appearance stores linear base RGBA, metallic, roughness and emission strength. Scene format v2 serializes these properties, while the loader remains compatible with v1 scenes using default surface parameters. Presets are copied into a project only when a preset file is absent, so user-authored materials survive editor updates.


## glTF assets and NEXUS scene API

The asset module uses the vendored single-header `cgltf` parser (MIT licensed) to load glTF 2.0 JSON and GLB files into a CPU mesh representation. Triangle primitives preserve material boundaries and metallic-roughness factors; external buffers/images are copied into project-local `Assets/Models` during import. Scene files use `LICSCENE 3` to store project-relative model paths and remain able to read versions 1 and 2. The importer restricts file size, vertex/index counts and dependency paths; images are not yet sampled by the viewport shader.

The Vulkan renderer uploads indexed vertex and index buffers, caches imported models for the life of the device, and draws each primitive with its material factors. Imported model cache entries are released before destroying the Vulkan device. Current mesh buffers use host-visible/coherent memory for a simple portable path; a device-local staging uploader is a future performance improvement.

NEXUS scripts can emit validated `LICAPI` scene commands. The host supports spawn/edit/enable/destroy operations for script-owned mesh nodes and applies these commands to the editor scene when rules are tested, replaying them into the next VOIDBOUND run. This is a setup-time bridge, not a per-frame virtual machine. Input/update/collision callbacks remain a follow-up milestone requiring persistent embedded NEXUS execution.
