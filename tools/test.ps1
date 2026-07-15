param(
    [ValidateSet("Debug", "Release")]
    [string] $Configuration = "Debug",

    [string] $ConfigurePreset = "vcpkg",

    [string] $BuildPreset,

    [string] $TestPreset,

    [string] $TestRegex,

    [string] $VsDevCmdPath,

    [switch] $SkipConfigure,

    [switch] $SkipBuild,

    [switch] $ReuseConfigure
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-VsDevCmdPath {
    param(
        [string] $ExplicitPath
    )

    if ($ExplicitPath) {
        if (-not (Test-Path -LiteralPath $ExplicitPath -PathType Leaf)) {
            throw "VsDevCmd.bat was not found at '$ExplicitPath'."
        }
        return (Resolve-Path -LiteralPath $ExplicitPath).Path
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $installPath = & $vswhere `
            -latest `
            -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath

        if ($LASTEXITCODE -eq 0 -and $installPath) {
            $candidate = Join-Path $installPath "Common7\Tools\VsDevCmd.bat"
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                return $candidate
            }
        }
    }

    $fallbackCandidates = Get-ChildItem `
        -Path "${env:ProgramFiles}\Microsoft Visual Studio\*\*\Common7\Tools\VsDevCmd.bat" `
        -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending

    if ($fallbackCandidates) {
        return $fallbackCandidates[0].FullName
    }

    throw "Could not locate VsDevCmd.bat. Install Visual Studio C++ tools or pass -VsDevCmdPath."
}

function Import-VsDevEnvironment {
    param(
        [string] $DevCmdPath
    )

    $command = "`"$DevCmdPath`" -arch=x64 -host_arch=x64 >nul && set"
    $environment = & cmd.exe /d /s /c $command
    if ($LASTEXITCODE -ne 0) {
        throw "VsDevCmd.bat failed with exit code $LASTEXITCODE."
    }

    foreach ($line in $environment) {
        if ($line -match "^(.*?)=(.*)$") {
            [Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process")
        }
    }
}

function Invoke-Checked {
    param(
        [string] $Executable,
        [string[]] $Arguments
    )

    Write-Host "> $Executable $($Arguments -join ' ')"
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "'$Executable $($Arguments -join ' ')' failed with exit code $LASTEXITCODE."
    }
}

function Test-CMakeSupportsFreshConfigure {
    $help = & cmake --help
    if ($LASTEXITCODE -ne 0) {
        return $false
    }

    return [bool]($help | Select-String -SimpleMatch "--fresh")
}

function Test-DirectoryWritable {
    param(
        [string] $Directory
    )

    if (-not (Test-Path -LiteralPath $Directory -PathType Container)) {
        New-Item -ItemType Directory -Path $Directory | Out-Null
    }

    $probe = Join-Path $Directory ".timetable-write-probe-$PID.tmp"
    try {
        Set-Content -LiteralPath $probe -Value "probe" -NoNewline
        Remove-Item -LiteralPath $probe -Force
        return $true
    } catch {
        if (Test-Path -LiteralPath $probe) {
            Remove-Item -LiteralPath $probe -Force -ErrorAction SilentlyContinue
        }
        return $false
    }
}

if (-not $BuildPreset) {
    $BuildPreset = if ($Configuration -eq "Debug") {
        "windows-msvc-debug"
    } else {
        "windows-msvc-release"
    }
}

if (-not $TestPreset) {
    $TestPreset = if ($Configuration -eq "Debug") {
        "windows-msvc-debug"
    } else {
        "windows-msvc-release"
    }
}

if (-not $env:VCPKG_ROOT) {
    throw "VCPKG_ROOT is not set. Set it to the vcpkg checkout used by the CMake preset."
}

$vcpkgRoot = (Resolve-Path -LiteralPath $env:VCPKG_ROOT).Path
$toolchainFile = Join-Path $vcpkgRoot "scripts\buildsystems\vcpkg.cmake"
if (-not (Test-Path -LiteralPath $toolchainFile -PathType Leaf)) {
    throw "The vcpkg toolchain file was not found at '$toolchainFile'."
}

$vcpkgBuildtrees = Join-Path $vcpkgRoot "buildtrees"
if (-not (Test-DirectoryWritable -Directory $vcpkgBuildtrees)) {
    throw "VCPKG_ROOT/buildtrees is not writable at '$vcpkgBuildtrees'. Fix the vcpkg directory permissions or set VCPKG_ROOT to a user-writable vcpkg checkout."
}

$devCmd = Resolve-VsDevCmdPath -ExplicitPath $VsDevCmdPath
Import-VsDevEnvironment -DevCmdPath $devCmd
$env:VCPKG_ROOT = $vcpkgRoot

if (-not $SkipConfigure) {
    $configureArguments = @("--preset", $ConfigurePreset)
    if (-not $ReuseConfigure) {
        if (Test-CMakeSupportsFreshConfigure) {
            $configureArguments = @("--fresh") + $configureArguments
        } else {
            Write-Warning "This CMake version does not support '--fresh'; reusing the existing configure cache."
        }
    }

    Invoke-Checked -Executable "cmake" -Arguments $configureArguments
}

if (-not $SkipBuild) {
    Invoke-Checked -Executable "cmake" -Arguments @("--build", "--preset", $BuildPreset)
}

$ctestArguments = @("--preset", $TestPreset)
if ($TestRegex) {
    $ctestArguments += @("-R", $TestRegex)
}

Invoke-Checked -Executable "ctest" -Arguments $ctestArguments
