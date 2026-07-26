#requires -Version 7.0

[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet("Before", "After")]
    [string] $Phase,

    [string] $Preset = "code-intel",

    [string] $CodexProfile = "repo",

    [switch] $ForceCodeIntel,

    [switch] $ForceGraph,

    [switch] $NoLaunch,

    [switch] $SkipGraph,

    [switch] $SkipSerena
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Format-Duration {
    param([Parameter(Mandatory)][TimeSpan] $Elapsed)

    if ($Elapsed.TotalMinutes -ge 1) {
        return "{0:N1} min" -f $Elapsed.TotalMinutes
    }

    return "{0:N1} s" -f $Elapsed.TotalSeconds
}

function Write-Step {
    param([Parameter(Mandatory)][string] $Message)

    Write-Host ""
    Write-Host "==> $Message"
}

function Require-Command {
    param([Parameter(Mandatory)][string] $Name)

    $Command = Get-Command $Name -ErrorAction Ignore
    if (-not $Command) {
        throw "Required command '$Name' was not found."
    }

    return $Command
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory)][string] $Command,
        [Parameter(ValueFromRemainingArguments)][string[]] $Arguments
    )

    $RenderedCommand = "$Command $($Arguments -join ' ')".Trim()
    Write-Host "    Running: $RenderedCommand"

    $Stopwatch = [System.Diagnostics.Stopwatch]::StartNew()

    & $Command @Arguments
    $ExitCode = $LASTEXITCODE

    $Stopwatch.Stop()

    if ($ExitCode -ne 0) {
        throw "'$RenderedCommand' exited with code $ExitCode."
    }

    Write-Host "    Completed in $(Format-Duration $Stopwatch.Elapsed)."
}

function Resolve-RepositoryRoot {
    $Git = Get-Command git -ErrorAction Ignore

    if ($Git) {
        $Root = (& git rev-parse --show-toplevel 2>$null)
        if ($LASTEXITCODE -eq 0 -and $Root) {
            return [System.IO.Path]::GetFullPath($Root.Trim())
        }
    }

    # Scripts are expected to live under <repo>/scripts/.
    return [System.IO.Path]::GetFullPath(
        (Join-Path $PSScriptRoot "..")
    )
}

function Get-RepositoryRelativeFiles {
    param([Parameter(Mandatory)][string] $RepositoryRoot)

    Require-Command git | Out-Null

    # Unlike Get-ChildItem -Recurse, this command:
    # - does not traverse .git;
    # - does not enter ignored build/ and vcpkg_installed/ directories;
    # - includes tracked files and new untracked files;
    # - scales with the Git index rather than the build tree size.
    $Files = @(
        & git `
            -C $RepositoryRoot `
            ls-files `
            --cached `
            --others `
            --exclude-standard
    )

    if ($LASTEXITCODE -ne 0) {
        throw "Failed to list repository files via git ls-files."
    }

    @(
        $Files |
            Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
            ForEach-Object { $_.Trim() }
    )
}

function Test-IsBuildModelFile {
    param([Parameter(Mandatory)][string] $RelativePath)

    $Path = $RelativePath.Replace("\", "/")
    $Name = [System.IO.Path]::GetFileName($Path)

    if ($Name -eq "CMakeLists.txt") {
        return $true
    }

    if ([System.IO.Path]::GetExtension($Path) -eq ".cmake") {
        return $true
    }

    return $Path -in @(
        "CMakePresets.json",
        "CMakeUserPresets.json",
        "vcpkg.json",
        "vcpkg-configuration.json"
    )
}

function Test-IsProjectSourceFile {
    param([Parameter(Mandatory)][string] $RelativePath)

    $Path = $RelativePath.Replace("\", "/")
    $Extension = [System.IO.Path]::GetExtension($Path).ToLowerInvariant()

    if ($Extension -notin @(".cpp", ".cc", ".cxx")) {
        return $false
    }

    return (
        $Path.StartsWith("src/") -or
        $Path.StartsWith("tests/") -or
        $Path.StartsWith("libs/")
    )
}

function Resolve-DatabaseFilePath {
    param([Parameter(Mandatory)] $Entry)

    $File = [string] $Entry.file

    if ([System.IO.Path]::IsPathRooted($File)) {
        return [System.IO.Path]::GetFullPath($File)
    }

    return [System.IO.Path]::GetFullPath(
        (Join-Path ([string] $Entry.directory) $File)
    )
}

function Get-CodeIntelState {
    param(
        [Parameter(Mandatory)][string] $RepositoryRoot,
        [Parameter(Mandatory)][string] $RootDatabase
    )

    $Reasons = [System.Collections.Generic.List[string]]::new()

    if (-not (Test-Path $RootDatabase)) {
        $Reasons.Add("compile_commands.json is missing")

        return [pscustomobject]@{
            NeedsRefresh  = $true
            Reasons       = $Reasons
            DatabaseCount = 0
        }
    }

    $DatabaseInfo = Get-Item $RootDatabase
    $DatabaseTime = $DatabaseInfo.LastWriteTimeUtc

    Write-Host "    Getting tracked/untracked files via Git..."
    $RepositoryFiles = @(
        Get-RepositoryRelativeFiles `
            -RepositoryRoot $RepositoryRoot
    )
    Write-Host "    Files in working tree: $($RepositoryFiles.Count)"

    Write-Host "    Checking CMake/vcpkg files..."
    foreach ($RelativePath in $RepositoryFiles) {
        if (-not (Test-IsBuildModelFile -RelativePath $RelativePath)) {
            continue
        }

        $FullPath = Join-Path $RepositoryRoot $RelativePath

        if (-not (Test-Path $FullPath)) {
            $Reasons.Add("deleted build-model file: $RelativePath")
            continue
        }

        if ((Get-Item $FullPath).LastWriteTimeUtc -gt $DatabaseTime) {
            $Reasons.Add("newer than compilation database: $RelativePath")
        }
    }

    Write-Host "    Reading compile_commands.json..."
    try {
        $Database = @(
            Get-Content $RootDatabase -Raw |
                ConvertFrom-Json
        )
    }
    catch {
        $Reasons.Add("compile_commands.json is corrupted or is not JSON")

        return [pscustomobject]@{
            NeedsRefresh  = $true
            Reasons       = $Reasons
            DatabaseCount = 0
        }
    }

    if ($Database.Count -eq 0) {
        $Reasons.Add("compile_commands.json is empty")

        return [pscustomobject]@{
            NeedsRefresh  = $true
            Reasons       = $Reasons
            DatabaseCount = 0
        }
    }

    Write-Host "    Translation units in database: $($Database.Count)"
    Write-Host "    Building indexed source set..."

    $DatabaseFileSet =
        [System.Collections.Generic.HashSet[string]]::new(
            [System.StringComparer]::OrdinalIgnoreCase
        )

    $MissingDatabaseFileCount = 0

    foreach ($Entry in $Database) {
        $FullPath = Resolve-DatabaseFilePath -Entry $Entry
        [void] $DatabaseFileSet.Add($FullPath)

        if (-not (Test-Path $FullPath)) {
            $MissingDatabaseFileCount++
        }
    }

    if ($MissingDatabaseFileCount -gt 0) {
        $Reasons.Add(
            "database contains deleted source files: " +
            $MissingDatabaseFileCount
        )
    }

    Write-Host "    Checking new .cpp/.cc/.cxx files..."

    $NewUnindexedSourceCount = 0

    foreach ($RelativePath in $RepositoryFiles) {
        if (-not (Test-IsProjectSourceFile -RelativePath $RelativePath)) {
            continue
        }

        $FullPath = [System.IO.Path]::GetFullPath(
            (Join-Path $RepositoryRoot $RelativePath)
        )

        if (-not (Test-Path $FullPath)) {
            continue
        }

        if (
            -not $DatabaseFileSet.Contains($FullPath) -and
            (Get-Item $FullPath).LastWriteTimeUtc -gt $DatabaseTime
        ) {
            $NewUnindexedSourceCount++
        }
    }

    if ($NewUnindexedSourceCount -gt 0) {
        $Reasons.Add(
            "new unindexed .cpp files found: " +
            $NewUnindexedSourceCount
        )
    }

    [pscustomobject]@{
        NeedsRefresh  = $Reasons.Count -gt 0
        Reasons       = $Reasons
        DatabaseCount = $Database.Count
    }
}

function Assert-MsvcEnvironment {
    if (-not (Get-Command cl -ErrorAction Ignore)) {
        throw @"
MSVC compiler 'cl.exe' was not found.
Run this script from Visual Studio Developer PowerShell
or import Launch-VsDevShell.ps1 before running it.
"@
    }

    if (-not (Get-Command ninja -ErrorAction Ignore)) {
        throw "ninja.exe was not found."
    }

    if (-not $env:VCPKG_ROOT) {
        throw "VCPKG_ROOT is not set."
    }

    $Toolchain = Join-Path `
        $env:VCPKG_ROOT `
        "scripts\buildsystems\vcpkg.cmake"

    if (-not (Test-Path $Toolchain)) {
        throw "vcpkg toolchain not found: $Toolchain"
    }
}

function Update-CodeIntel {
    param(
        [Parameter(Mandatory)][string] $Preset,
        [Parameter(Mandatory)][string] $GeneratedDatabase,
        [Parameter(Mandatory)][string] $RootDatabase
    )

    Require-Command cmake | Out-Null
    Assert-MsvcEnvironment

    Write-Step "Updating CMake model and compilation database"

    Invoke-Checked cmake "--preset" $Preset

    if (-not (Test-Path $GeneratedDatabase)) {
        throw "CMake did not create compilation database: $GeneratedDatabase"
    }

    Copy-Item `
        $GeneratedDatabase `
        $RootDatabase `
        -Force

    $Database = @(
        Get-Content $RootDatabase -Raw |
            ConvertFrom-Json
    )

    Write-Host "    compile_commands.json updated."
    Write-Host "    Translation units: $($Database.Count)"
}

function Ensure-SerenaProject {
    param([Parameter(Mandatory)][string] $RepositoryRoot)

    Require-Command serena | Out-Null

    $ProjectFile = Join-Path $RepositoryRoot ".serena\project.yml"

    if (-not (Test-Path $ProjectFile)) {
        Write-Step "Creating Serena project"
        Invoke-Checked serena "project" "create" "--index"
        return
    }

    Write-Step "Updating Serena index"
    Invoke-Checked serena "project" "index"
}

function Update-Graphify {
    param(
        [Parameter(Mandatory)][string] $RepositoryRoot,
        [switch] $Force
    )

    Require-Command graphify | Out-Null

    $GraphFile = Join-Path $RepositoryRoot "graphify-out\graph.json"

    if (-not (Test-Path $GraphFile)) {
        Write-Step "Initial Graphify graph build"
        Invoke-Checked graphify "extract" "." "--code-only"
        return
    }

    Write-Step "Updating Graphify graph"

    if ($Force) {
        Invoke-Checked graphify "update" "." "--force"
    }
    else {
        Invoke-Checked graphify "update" "."
    }
}

function Show-AgentPrompt {
    param([Parameter(Mandatory)][string] $Phase)

    Write-Host ""

    if ($Phase -eq "Before") {
        Write-Host "Agent instruction:"
        Write-Host @'
1. First read only the relevant Serena memories.
2. Use scoped Graphify queries to choose subsystems and entry points.
3. Verify definitions, references, callers, and overloads through Serena/clangd.
4. Read bodies only for specific symbols.
5. Use rg only for strings, macros, CMake/config files, and fallback searches.
6. Do not reread unchanged files unless there is a concrete information gap.
'@
    }
    else {
        Write-Host "Final agent instruction:"
        Write-Host @'
Update Serena memories only when stable knowledge has changed:
architecture boundaries, public APIs, domain invariants,
standard build commands, or project conventions.
Do not save the current diff, debugging history, or temporary implementation details.
'@
    }
}

$RepositoryRoot = Resolve-RepositoryRoot
$BuildDirectory = Join-Path $RepositoryRoot "build\code-intel"
$GeneratedDatabase = Join-Path `
    $BuildDirectory `
    "compile_commands.json"
$RootDatabase = Join-Path `
    $RepositoryRoot `
    "compile_commands.json"

Push-Location $RepositoryRoot

try {
    Write-Host "Repository: $RepositoryRoot"
    Write-Host "Phase: $Phase"

    if (-not $SkipSerena) {
        Write-Step "Checking compilation database freshness"

        $Stopwatch = [System.Diagnostics.Stopwatch]::StartNew()

        $State = Get-CodeIntelState `
            -RepositoryRoot $RepositoryRoot `
            -RootDatabase $RootDatabase

        $Stopwatch.Stop()
        Write-Host "    Check completed in $(Format-Duration $Stopwatch.Elapsed)."

        if ($ForceCodeIntel -or $State.NeedsRefresh) {
            if ($ForceCodeIntel) {
                Write-Host "    Update reason: forced."
            }

            foreach ($Reason in $State.Reasons) {
                Write-Host "    Update reason: $Reason"
            }

            Update-CodeIntel `
                -Preset $Preset `
                -GeneratedDatabase $GeneratedDatabase `
                -RootDatabase $RootDatabase

            Ensure-SerenaProject `
                -RepositoryRoot $RepositoryRoot
        }
        else {
            Write-Host "    Code-intel is up to date."
            Write-Host "    CMake and full Serena index will not run."
            Write-Host "    Translation units: $($State.DatabaseCount)"
        }
    }

    if (-not $SkipGraph) {
        Update-Graphify `
            -RepositoryRoot $RepositoryRoot `
            -Force:$ForceGraph
    }

    Show-AgentPrompt -Phase $Phase

    if ($Phase -eq "Before" -and -not $NoLaunch) {
        Require-Command codex | Out-Null

        Write-Step "Starting Codex with profile '$CodexProfile'"
        & codex "--profile" $CodexProfile
    }
}
finally {
    Pop-Location
}
