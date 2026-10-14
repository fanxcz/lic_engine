# Building LIC Engine

LIC Engine can be built in two modes:

- **Core mode** builds the engine core, command-line editor entry point, and tests. It does not require a graphics SDK.
- **Vulkan viewport mode** additionally builds the GLFW window, Vulkan presentation backend, depth buffer, and a minimal shader pipeline that renders a colored test cube. This proves the first geometry draw path; it is not yet a general-purpose mesh/material renderer.

## Requirements

- **Windows 11 local builds:** Visual Studio 2022 with the *Desktop development with C++* workload, CMake 3.25+, and Git. The Vulkan viewport additionally requires the LunarG Vulkan SDK.
- **Cross-platform build downloads:** install GitHub CLI (`gh`) and authenticate once with `gh auth login`. GitHub Actions builds for other operating systems on their native runners; Windows cannot produce a genuine macOS binary by compiling locally.

- CMake 3.25 or newer
- A C++23 compiler (MSVC 2022, GCC 13+, or Clang 16+ recommended)
- For the viewport: Vulkan SDK or system Vulkan development headers/loader, GLFW 3 development files, and the `glslc` shader compiler
- A desktop session and Vulkan-capable driver to run the viewport

## Windows 11: build the local executable

Open PowerShell in the repository root. The script configures a clean x64 build, compiles, runs CTest, checks the executable version, and creates a ZIP package:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\tools\build-windows.ps1
```

If the Vulkan SDK and its `glslc.exe` are not installed, the script still builds the core CLI executable and warns that the 3D viewport is disabled. To explicitly request a core-only build:

```powershell
.\tools\build-windows.ps1 -CoreOnly
```

Output: `dist\lic-engine-windows-x64.zip` and `dist\lic-engine-windows-x64\lic_editor.exe`. For a graphical viewport, install the [LunarG Vulkan SDK](https://vulkan.lunarg.com/sdk/home), reopen PowerShell so `VULKAN_SDK` is available, and run the script again. GLFW 3.4 is fetched by CMake only when the Vulkan SDK is detected.

## Windows 11: build and download all platform packages

Push the repository changes to GitHub first. Then, from the repository root, run:

```powershell
gh auth login
.\tools\build-all-platforms.ps1
```

The script dispatches the **Build platform binaries** workflow, waits for it to finish, and downloads its packages into `dist\platform-builds\`. Packages are also available on the repository's **Actions** page for 30 days after each run. Pushing a version tag such as `v0.1.0` creates a GitHub Release with the generated archives attached.

Current package scope:
- `lic-engine-windows-x64.zip`: Windows x64 executable; the workflow builds the Vulkan viewport and includes its SPIR-V shaders.
- `lic-engine-linux-x64.tar.gz`: Linux x64 executable with the Vulkan viewport and shaders.
- `lic-engine-macos-arm64-core.tar.gz`: Apple Silicon macOS core CLI executable only. The desktop Vulkan path has not been validated for macOS yet.

Android APKs and Web/WASM builds are **not available yet**: the engine does not have an Android activity/platform layer or a Web renderer target. Those need real platform backends; producing an empty APK or an unrelated Web stub would not be a usable engine build.

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
