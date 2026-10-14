# Building LIC Engine

LIC Engine can be built in two modes:

- **Core mode** builds the engine core, command-line editor entry point, and tests. It does not require a graphics SDK.
- **Vulkan viewport mode** additionally builds the GLFW window, Vulkan presentation backend, depth buffer, and a minimal shader pipeline that renders a colored test cube. This proves the first geometry draw path; it is not yet a general-purpose mesh/material renderer.

## Requirements

- CMake 3.25 or newer
- A C++23 compiler (MSVC 2022, GCC 13+, or Clang 16+ recommended)
- For the viewport: Vulkan SDK or system Vulkan development headers/loader, GLFW 3 development files, and the `glslc` shader compiler
- A desktop session and Vulkan-capable driver to run the viewport

### Debian / Ubuntu

```sh
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build libvulkan-dev libglfw3-dev glslc mesa-vulkan-drivers
```

For proprietary GPU drivers, install and configure the driver supported by the machine. Mesa's software Vulkan driver is useful for CI and compatible systems, but may not be installed/configured on every distribution.

### Configure and build

From the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

CMake enables the Vulkan viewport automatically when both Vulkan and a supported GLFW CMake target are found. To force a core-only build:

```sh
cmake -S . -B build-core -DLIC_ENGINE_ENABLE_VULKAN=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-core --parallel
ctest --test-dir build-core --output-on-failure
```

## Run

```sh
./build/lic_editor --help
./build/lic_editor --version
./build/lic_editor --diagnostics
./build/lic_editor --window
```

On Windows, run the corresponding `lic_editor.exe` from the selected CMake configuration directory (often `build/Release` for multi-configuration generators).

If the graphics dependencies were not found during configuration, `--window` reports that the viewport is unavailable instead of failing with a linker error. Reconfigure after installing the SDK and GLFW.

## Known limitations of this milestone

- No asset-import pipeline, user-controlled camera, scene grid, general-purpose material system, or editor panels yet. The current cube is compiled directly into a small test shader.
- Window resizing rebuilds swapchain-dependent resources; handling is intentionally kept small and will receive dedicated tests as the renderer grows.
- Vulkan initialization is currently a runtime smoke-test. Headless CI builds and unit tests do not open a graphical window.
