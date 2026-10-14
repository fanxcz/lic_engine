[CmdletBinding()]
param(
    [string]$Ref = "main"
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$gh = Get-Command gh -ErrorAction SilentlyContinue
if (-not $gh) {
    throw "GitHub CLI (gh) was not found. Install it from https://cli.github.com/ and reopen PowerShell."
}

Push-Location $repoRoot
try {
    & gh auth status
    if ($LASTEXITCODE -ne 0) {
        throw "GitHub CLI is not authenticated. Run: gh auth login"
    }

    Write-Host "Starting Windows/Linux/macOS builds on GitHub Actions for ref '$Ref'..." -ForegroundColor Cyan
    & gh workflow run build-binaries.yml --ref $Ref
    if ($LASTEXITCODE -ne 0) {
        throw "Could not start the workflow. Ensure the workflow has been pushed to GitHub and Actions are enabled."
    }

    $runId = $null
    for ($attempt = 0; $attempt -lt 18; $attempt++) {
        Start-Sleep -Seconds 5
        $raw = & gh run list --workflow build-binaries.yml --branch $Ref --event workflow_dispatch --limit 1 --json databaseId,status,conclusion
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to query GitHub Actions runs."
        }
        $runs = @($raw | ConvertFrom-Json)
        if ($runs.Count -gt 0 -and $runs[0].databaseId) {
            $runId = [string]$runs[0].databaseId
            break
        }
    }

    if (-not $runId) {
        throw "GitHub Actions run was not found. Open the repository Actions page and check whether the workflow started."
    }

    Write-Host "Watching workflow run $runId. The script will stop if a platform build fails." -ForegroundColor Cyan
    & gh run watch $runId --exit-status
    if ($LASTEXITCODE -ne 0) {
        throw "At least one platform build failed. Inspect the run with: gh run view $runId --web"
    }

    $downloadRoot = Join-Path $repoRoot "dist/platform-builds"
    if (Test-Path $downloadRoot) { Remove-Item $downloadRoot -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $downloadRoot | Out-Null

    & gh run download $runId --dir $downloadRoot
    if ($LASTEXITCODE -ne 0) {
        throw "The workflow completed, but artifact download failed. Try: gh run download $runId --dir dist/platform-builds"
    }

    $expected = @(
        @{ Name = "lic-engine-windows-x64"; File = "lic-engine-windows-x64.zip" },
        @{ Name = "lic-engine-linux-x64"; File = "lic-engine-linux-x64.tar.gz" },
        @{ Name = "lic-engine-macos-arm64-core"; File = "lic-engine-macos-arm64-core.tar.gz" }
    )
    foreach ($item in $expected) {
        $source = Join-Path (Join-Path $downloadRoot $item.Name) $item.File
        if (Test-Path $source) {
            Copy-Item $source (Join-Path $downloadRoot $item.File) -Force
        } else {
            Write-Warning "Artifact archive not found where expected: $source"
        }
    }

    Write-Host ""
    Write-Host "Platform packages downloaded to: $downloadRoot" -ForegroundColor Green
    Get-ChildItem $downloadRoot -Recurse -File |
        Where-Object { $_.Extension -in @(".zip", ".gz") } |
        Select-Object FullName, Length |
        Format-Table -AutoSize
    Write-Host "Note: macOS package is currently a core CLI build; Linux and Windows packages target the Vulkan viewport."
}
finally {
    Pop-Location
}
