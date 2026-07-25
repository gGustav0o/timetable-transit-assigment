[CmdletBinding()]
param(
    [switch] $Clean,
    [switch] $SkipBuild,
    [switch] $SkipTests,
    [switch] $SkipSerenaIndex
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$RepositoryRoot = Split-Path -Parent $PSScriptRoot
$BuildDirectory = Join-Path $RepositoryRoot "build\code-intel"
$GeneratedDatabase = Join-Path `
    $BuildDirectory `
    "compile_commands.json"
$RootDatabase = Join-Path `
    $RepositoryRoot `
    "compile_commands.json"

function Require-Command {
    param([Parameter(Mandatory)][string] $Name)

    if (-not (Get-Command $Name -ErrorAction Ignore)) {
        throw "Не найдена команда: $Name"
    }
}

Require-Command cmake
Require-Command ninja
Require-Command cl
Require-Command link
Require-Command vcpkg

if (-not $env:VCPKG_ROOT) {
    throw "Переменная VCPKG_ROOT не задана."
}

$VcpkgToolchain = Join-Path `
    $env:VCPKG_ROOT `
    "scripts\buildsystems\vcpkg.cmake"

if (-not (Test-Path $VcpkgToolchain)) {
    throw "Не найден vcpkg toolchain: $VcpkgToolchain"
}

Push-Location $RepositoryRoot

try {
    if ($Clean) {
        Remove-Item `
            $BuildDirectory `
            -Recurse `
            -Force `
            -ErrorAction Ignore
    }

    cmake --preset code-intel

    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed: $LASTEXITCODE"
    }

    if (-not $SkipBuild) {
        cmake `
            --build `
            --preset code-intel `
            --parallel

        if ($LASTEXITCODE -ne 0) {
            throw "Build failed: $LASTEXITCODE"
        }
    }

    if (-not $SkipTests) {
        ctest --preset code-intel

        if ($LASTEXITCODE -ne 0) {
            throw "Tests failed: $LASTEXITCODE"
        }
    }

    if (-not (Test-Path $GeneratedDatabase)) {
        throw "Не создана compilation database: $GeneratedDatabase"
    }

    Copy-Item `
        $GeneratedDatabase `
        $RootDatabase `
        -Force

    $Database = Get-Content `
        $RootDatabase `
        -Raw |
        ConvertFrom-Json

    if ($Database.Count -eq 0) {
        throw "Compilation database пуста."
    }

    Write-Host "Compilation database обновлена:"
    Write-Host "  $RootDatabase"
    Write-Host "Translation units: $($Database.Count)"

    if (-not $SkipSerenaIndex) {
        Require-Command serena

        serena project index

        if ($LASTEXITCODE -ne 0) {
            throw "Serena indexing failed: $LASTEXITCODE"
        }
    }
}
finally {
    Pop-Location
}
