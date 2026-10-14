# LIC Engine

**A C++23 game engine focused on fast iteration, a clean editor, and a practical runtime.**

LIC Engine aims to combine the approachable scene workflow of Godot, the level-design and runtime mindset of Source, and the rendering ambition of Unreal—without copying their interfaces or architecture.

> **Project status:** early foundation. The native Vulkan viewport now opens a GLFW window, selects a Vulkan-capable device, builds a swapchain/render pass/depth buffer and draws a shader-based colored cube. This is the first renderer smoke scene, not a full editor: imported meshes, camera controls, scene hierarchy, material editing, physics, and runtime packaging remain future work.

## Download for Windows 11

**[Download LIC Engine for Windows — EXE](https://github.com/fanxcz/lic_engine/releases/download/continuous/lic_editor.exe)** · **[Download the complete Windows package (EXE + Vulkan shaders)](https://github.com/fanxcz/lic_engine/releases/download/continuous/lic-engine-windows-x64.zip)** · [All build results](https://github.com/fanxcz/lic_engine/actions/workflows/build-binaries.yml)

The standalone `lic_editor.exe` is provided for quick access, but the graphical viewport needs the adjacent `shaders\` directory and a Vulkan-capable graphics driver. For normal use, download and extract the complete ZIP, then double-click `run-lic-engine.bat`. The continuous download is refreshed by the **Build platform binaries** workflow; if the link is not available yet, run the workflow once using the steps in [Windows 11 build instructions](docs/BUILDING.md).

## Design principles

- **A real engine, not a demo:** editor, runtime, asset pipeline, and sample game evolve together.
- **C++23 + CMake:** readable native code and reproducible builds.
- **Modular core:** rendering, world, physics, audio, input, assets, and platform services have clear boundaries.
- **Desktop first, portable by design:** Windows and Linux first; Android after the platform and renderer interfaces are stable. Keep platform-specific APIs out of the core.
- **Useful editor UX:** dark, restrained interface; large scene viewport; hierarchy and inspector; visible asset browser; clear play/stop controls; useful diagnostics.
- **Measured performance:** frame-time and memory metrics, profiling hooks, and test scenes from the beginning.
- **Honest status:** document what works, what is experimental, and what is planned.

## Target editor layout

```text
┌──────────────────────────────────────────────────────────────────────────┐
│ LIC Engine   Project  Scene  Assets  Build             Play  Pause  Stop │
├──────────────┬──────────────────────────────────────┬────────────────────┤
│ Scene        │                                      │ Inspector          │
│ Hierarchy    │          3D Scene View               │ Transform          │
│              │     gizmos · grid · camera           │ Components         │
├──────────────┴──────────────────────────────────────┴────────────────────┤
│ Assets / Console / Problems / Profiler / Build output                    │
└──────────────────────────────────────────────────────────────────────────┘
```

The editor should be information-dense but calm: neutral dark surfaces, readable typography, restrained accent color, consistent spacing, and no decorative gradients or oversized dashboard cards. Panels should be resizable and remember their layout. The viewport must remain the visual focus.

## Planned architecture

```text
LIC Editor ───────────────┐
                          ├── LIC Runtime ── World / ECS / Scene / Events
Standalone Game Runner ───┘        │
                                   ├── Renderer (Vulkan first)
                                   ├── Physics (Jolt adapter)
                                   ├── Audio (miniaudio adapter)
                                   ├── Input
                                   ├── Asset database and import pipeline
                                   ├── Animation
                                   └── Platform abstraction
```

### Initial technology direction

| Area | Initial choice | Notes |
|---|---|---|
| Language/build | C++23, CMake | Keep compiler and dependency requirements documented |
| Graphics | Vulkan | Start with a small forward renderer; add render graph and advanced effects only after the basics are reliable |
| Editor UI | Dear ImGui with docking | Editor tooling, not the in-game UI system |
| Math | GLM | Centralize coordinate-system and unit conventions |
| World model | Entity-component-system with stable entity handles | Avoid exposing storage details throughout the engine |
| Physics | Jolt Physics behind an adapter | Fixed simulation step and explicit collision layers |
| Audio | miniaudio behind an adapter | Keep audio optional for headless tests |
| Model format | glTF 2.0 | Document supported materials, textures, skins, and animation features |
| Assets | GUID-based asset database | Stable references, import settings, dependency tracking, and useful error messages |
| Testing | CTest | Unit tests plus a small deterministic smoke-test scene |

These are initial choices, not reasons to couple every module permanently to one vendor. Third-party licenses must be reviewed and recorded before dependencies are added.

## Platform binaries

The repository now includes a build workflow for downloadable native executables:

| Package | Current contents |
|---|---|
| Windows x64 | Native executable with the Vulkan viewport and compiled shaders |
| Linux x64 | Native executable with the Vulkan viewport and compiled shaders |
| macOS arm64 | Core command-line executable; desktop Vulkan portability is not yet validated |

On Windows 11, run `tools/build-windows.ps1` to build and package the local executable. To dispatch native builds on GitHub Actions and download all platform packages, run `tools/build-all-platforms.ps1` from an authenticated GitHub CLI session. Full steps are in [docs/BUILDING.md](docs/BUILDING.md).

The manual platform workflow updates a rolling preview release tagged `continuous`, with direct links to the Windows executable and complete archive. Pushing a version tag beginning with `v` also publishes a versioned GitHub Release. Android APK and Web/WASM builds are not available yet: those targets need platform entry points and renderer integration first.

## Roadmap

1. **Foundation:** repository conventions, CMake targets, logging, assertions, configuration, platform/window layer, and CI build checks.
2. **First visible scene:** Vulkan initialization, swapchain, camera, depth buffer, a lit mesh, resize handling, and a reproducible sample scene.
3. **World and assets:** entity/component model, transforms, scene serialization, asset IDs, glTF import, and error reporting.
4. **Editor workflow:** viewport navigation, hierarchy, inspector, selection, transform gizmos, asset browser, console, and play/stop.
5. **Gameplay systems:** input mapping, physics, audio, animation, prefabs, and a documented C++ game-module interface.
6. **Shipping workflow:** standalone runtime, project packaging, profiling, automated tests, Windows/Linux release builds, then Android feasibility and support.
7. **Advanced rendering:** PBR materials, shadows, image-based lighting, post-processing, instancing, GPU profiling, and scalable quality settings.

The order is deliberate: each stage must build and run before the next adds complexity. Feature names alone do not count as completion.

## Repository structure target

```text
lic_engine/
├── CMakeLists.txt
├── CMakePresets.json
├── README.md
├── LICENSE
├── docs/
│   ├── ARCHITECTURE.md
│   ├── ROADMAP.md
│   └── CONTRIBUTING.md
├── engine/
│   ├── core/
│   ├── platform/
│   ├── renderer/
│   ├── world/
│   ├── assets/
│   ├── physics/
│   ├── audio/
│   └── runtime/
├── editor/
├── tools/
├── examples/
│   └── sandbox/
├── tests/
└── third_party/
```

Folders should be introduced with their first real implementation rather than filled with empty placeholders.

## Build and validation

Follow [docs/BUILDING.md](docs/BUILDING.md) for exact Windows 11, Linux, and CI artifact commands. CTest covers the core tests and CLI smoke tests. The platform workflow builds the Windows and Linux Vulkan viewport packages and a macOS core executable; it does not imply support for Android or Web yet. Report only builds and tests that were actually run, and distinguish the core CLI from the graphical renderer.

## Contributing

See [docs/CONTRIBUTING.md](docs/CONTRIBUTING.md), [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), and [docs/ROADMAP.md](docs/ROADMAP.md). Prefer small modules, clear ownership, explicit error handling, and tests for behavior that can regress.
