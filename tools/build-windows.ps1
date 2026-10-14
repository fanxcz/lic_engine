[CmdletBinding()]
param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [switch]$CoreOnly
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "CMake was not found. Install CMake and the Visual Studio 2022 C++ build tools, then reopen PowerShell."
}

$vulkanSdk = $env:VULKAN_SDK
$glslc = if ($vulkanSdk) { Join-Path $vulkanSdk "Bin/glslc.exe" } else { $null }
$enableVulkan = (-not $CoreOnly) -and $vulkanSdk -and (Test-Path $glslc)

if ($enableVulkan) {
    Write-Host "Vulkan SDK found: $vulkanSdk" -ForegroundColor Green
    Write-Host "GLFW 3.4 will be fetched by CMake into the build directory."
} else {
    if (-not $CoreOnly) {
        Write-Warning "Vulkan SDK/glslc was not found. Building the portable core executable only. Install the Vulkan SDK to enable the 3D viewport."
    }
    $enableVulkan = $false
}

$buildDir = Join-Path $repoRoot "build/windows-x64"
$distRoot = Join-Path $repoRoot "dist"
$packageDir = Join-Path $distRoot "lic-engine-windows-x64"
$archive = Join-Path $distRoot "lic-engine-windows-x64.zip"

if (Test-Path $buildDir) { Remove-Item $buildDir -Recurse -Force }
if (Test-Path $packageDir) { Remove-Item $packageDir -Recurse -Force }
if (Test-Path $archive) { Remove-Item $archive -Force }
New-Item -ItemType Directory -Force -Path $distRoot, $packageDir | Out-Null

$cmakeVulkan = if ($enableVulkan) { "ON" } else { "OFF" }
$cmakeFetchGlfw = if ($enableVulkan) { "ON" } else { "OFF" }
$configureArgs = @(
    "-S", $repoRoot,
    "-B", $buildDir,
    "-G", "Visual Studio 17 2022",
    "-A", "x64",
    "-DLIC_ENGINE_ENABLE_VULKAN=$cmakeVulkan",
    "-DLIC_ENGINE_FETCH_GLFW=$cmakeFetchGlfw",
    "-DBUILD_TESTING=ON"
)

Write-Host ([Environment]::NewLine + "[1/4] Configuring Windows x64...") -ForegroundColor Cyan
& cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed with exit code $LASTEXITCODE." }

Write-Host ([Environment]::NewLine + "[2/4] Building $Configuration...") -ForegroundColor Cyan
& cmake --build $buildDir --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE." }

Write-Host ([Environment]::NewLine + "[3/4] Running tests...") -ForegroundColor Cyan
& ctest --test-dir $buildDir -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Tests failed with exit code $LASTEXITCODE." }

$executable = Join-Path $buildDir "$Configuration/lic_editor.exe"
if (-not (Test-Path $executable)) { throw "Expected executable was not produced: $executable" }
Copy-Item $executable (Join-Path $packageDir "lic_editor.exe") -Force
if ($enableVulkan) {
    $gameExecutable = Join-Path $buildDir "$Configuration/voidbound.exe"
    if (-not (Test-Path $gameExecutable)) { throw "Expected Voidbound executable was not produced: $gameExecutable" }
    Copy-Item $gameExecutable (Join-Path $packageDir "voidbound.exe") -Force
}
$nexusExecutable = Join-Path $buildDir "$Configuration/nexus.exe"
if (-not (Test-Path $nexusExecutable)) { throw "Expected NEXUS compiler was not produced: $nexusExecutable" }
Copy-Item $nexusExecutable (Join-Path $packageDir "nexus.exe") -Force
Copy-Item (Join-Path $buildDir "$Configuration/runtime") (Join-Path $packageDir "runtime") -Recurse -Force
Copy-Item (Join-Path $buildDir "$Configuration/stdlib") (Join-Path $packageDir "stdlib") -Recurse -Force
Copy-Item (Join-Path $buildDir "$Configuration/NEXUS-LICENSE") (Join-Path $packageDir "NEXUS-LICENSE") -Force
$shaderDir = Join-Path (Split-Path $executable -Parent) "shaders"
if ($enableVulkan) {
    foreach ($shader in @("viewport.vert.spv", "viewport.frag.spv")) {
        if (-not (Test-Path (Join-Path $shaderDir $shader))) {
            throw "Expected compiled Vulkan shader was not packaged: $(Join-Path $shaderDir $shader)"
        }
    }
}
if (Test-Path $shaderDir) {
    $packageShaders = Join-Path $packageDir "shaders"
    New-Item -ItemType Directory -Force -Path $packageShaders | Out-Null
    Copy-Item (Join-Path $shaderDir "*") $packageShaders -Recurse -Force
}
if ($enableVulkan) {
    $loaderCandidates = @()
    if ($vulkanSdk) {
        $loaderCandidates += Join-Path $vulkanSdk "Runtime/x64/vulkan-1.dll"
        $loaderCandidates += Join-Path $vulkanSdk "Bin/vulkan-1.dll"
    }
    if ($env:WINDIR) { $loaderCandidates += Join-Path $env:WINDIR "System32/vulkan-1.dll" }
    $vulkanLoader = $null
    foreach ($candidate in $loaderCandidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            $vulkanLoader = $candidate
            break
        }
    }
    if (-not $vulkanLoader) {
        throw "The Vulkan viewport was built, but vulkan-1.dll could not be packaged. Check the Vulkan SDK installation."
    }
    Copy-Item $vulkanLoader (Join-Path $packageDir "vulkan-1.dll") -Force
}
if ($enableVulkan) {
    Set-Content -Encoding ascii (Join-Path $packageDir "run-voidbound.bat") -Value @(
        "@echo off",
        "setlocal",
        'cd /d "%~dp0"',
        '"voidbound.exe" %*',
        'set "exitCode=%ERRORLEVEL%"',
        'if not "%exitCode%"=="0" pause',
        "exit /b %exitCode%"
    )
}
Set-Content -Encoding ascii (Join-Path $packageDir "run-lic-engine.bat") -Value @(
    "@echo off",
    "setlocal",
    'cd /d "%~dp0"',
    'if not exist "lic_editor.exe" (',
    "  echo LIC Engine executable is missing from this package.",
    "  pause",
    "  exit /b 2",
    ")",
    '"lic_editor.exe" %*',
    'set "exitCode=%ERRORLEVEL%"',
    'if not "%exitCode%"=="0" (',
    "  echo.",
    '  echo LIC Engine exited with code %exitCode%.',
    '  if exist "lic-engine-startup.log" (',
    "    echo Startup log:",
    '    type "lic-engine-startup.log"',
    "  )",
    "  pause",
    ")",
    "exit /b %exitCode%"
)
Copy-Item (Join-Path $repoRoot "README.md") (Join-Path $packageDir "README.md") -Force
Copy-Item (Join-Path $repoRoot "docs/BUILDING.md") (Join-Path $packageDir "BUILDING.md") -Force
$assetDir = Join-Path $packageDir "assets"
New-Item -ItemType Directory -Force -Path $assetDir | Out-Null
Copy-Item (Join-Path $repoRoot "assets/materials") (Join-Path $assetDir "materials") -Recurse -Force
Copy-Item (Join-Path $repoRoot "assets/scripts") (Join-Path $assetDir "scripts") -Recurse -Force
$examplesDir = Join-Path $packageDir "examples"
New-Item -ItemType Directory -Force -Path $examplesDir | Out-Null
Copy-Item (Join-Path $repoRoot "examples/nexus") (Join-Path $examplesDir "nexus") -Recurse -Force

Write-Host ([Environment]::NewLine + "[4/4] Checking executable and packing ZIP...") -ForegroundColor Cyan
& $executable --version
if ($LASTEXITCODE -ne 0) { throw "The generated executable failed its --version smoke test." }
& $nexusExecutable --version
if ($LASTEXITCODE -ne 0) { throw "Bundled NEXUS compiler failed its --version smoke test." }
Compress-Archive -Path (Join-Path $packageDir "*") -DestinationPath $archive -Force

Write-Host ([Environment]::NewLine + "Build complete.") -ForegroundColor Green
Write-Host "Executable: $executable"
Write-Host "Package:    $archive"
if ($enableVulkan) {
    Write-Host "Mode:       Vulkan editor + VOIDBOUND game (launch voidbound.exe)."
} else {
    Write-Host "Mode:       Core-only CLI. Use the GitHub platform workflow or install the Vulkan SDK for the 3D viewport."
}
