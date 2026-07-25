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
        throw "Не найдена команда '$Name'."
    }

    return $Command
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory)][string] $Command,
        [Parameter(ValueFromRemainingArguments)][string[]] $Arguments
    )

    $RenderedCommand = "$Command $($Arguments -join ' ')".Trim()
    Write-Host "    Выполняется: $RenderedCommand"

    $Stopwatch = [System.Diagnostics.Stopwatch]::StartNew()

    & $Command @Arguments
    $ExitCode = $LASTEXITCODE

    $Stopwatch.Stop()

    if ($ExitCode -ne 0) {
        throw "'$RenderedCommand' завершилась с кодом $ExitCode."
    }

    Write-Host "    Завершено за $(Format-Duration $Stopwatch.Elapsed)."
}

function Resolve-RepositoryRoot {
    $Git = Get-Command git -ErrorAction Ignore

    if ($Git) {
        $Root = (& git rev-parse --show-toplevel 2>$null)
        if ($LASTEXITCODE -eq 0 -and $Root) {
            return [System.IO.Path]::GetFullPath($Root.Trim())
        }
    }

    # Скрипты предполагаются в <repo>/scripts/.
    return [System.IO.Path]::GetFullPath(
        (Join-Path $PSScriptRoot "..")
    )
}

function Get-RepositoryRelativeFiles {
    param([Parameter(Mandatory)][string] $RepositoryRoot)

    Require-Command git | Out-Null

    # В отличие от Get-ChildItem -Recurse, эта команда:
    # - не обходит .git;
    # - не заходит в ignored build/ и vcpkg_installed/;
    # - включает tracked и новые untracked файлы;
    # - работает пропорционально индексу Git, а не размеру build tree.
    $Files = @(
        & git `
            -C $RepositoryRoot `
            ls-files `
            --cached `
            --others `
            --exclude-standard
    )

    if ($LASTEXITCODE -ne 0) {
        throw "Не удалось получить список файлов репозитория через git ls-files."
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
        $Reasons.Add("compile_commands.json отсутствует")

        return [pscustomobject]@{
            NeedsRefresh  = $true
            Reasons       = $Reasons
            DatabaseCount = 0
        }
    }

    $DatabaseInfo = Get-Item $RootDatabase
    $DatabaseTime = $DatabaseInfo.LastWriteTimeUtc

    Write-Host "    Получение списка tracked/untracked файлов через Git..."
    $RepositoryFiles = @(
        Get-RepositoryRelativeFiles `
            -RepositoryRoot $RepositoryRoot
    )
    Write-Host "    Файлов в рабочем дереве: $($RepositoryFiles.Count)"

    Write-Host "    Проверка CMake/vcpkg-файлов..."
    foreach ($RelativePath in $RepositoryFiles) {
        if (-not (Test-IsBuildModelFile -RelativePath $RelativePath)) {
            continue
        }

        $FullPath = Join-Path $RepositoryRoot $RelativePath

        if (-not (Test-Path $FullPath)) {
            $Reasons.Add("удалён build-model файл: $RelativePath")
            continue
        }

        if ((Get-Item $FullPath).LastWriteTimeUtc -gt $DatabaseTime) {
            $Reasons.Add("новее compilation database: $RelativePath")
        }
    }

    Write-Host "    Чтение compile_commands.json..."
    try {
        $Database = @(
            Get-Content $RootDatabase -Raw |
                ConvertFrom-Json
        )
    }
    catch {
        $Reasons.Add("compile_commands.json повреждён или не является JSON")

        return [pscustomobject]@{
            NeedsRefresh  = $true
            Reasons       = $Reasons
            DatabaseCount = 0
        }
    }

    if ($Database.Count -eq 0) {
        $Reasons.Add("compile_commands.json пуст")

        return [pscustomobject]@{
            NeedsRefresh  = $true
            Reasons       = $Reasons
            DatabaseCount = 0
        }
    }

    Write-Host "    Translation units в базе: $($Database.Count)"
    Write-Host "    Построение множества индексированных исходников..."

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
            "база содержит удалённые исходники: " +
            $MissingDatabaseFileCount
        )
    }

    Write-Host "    Проверка новых .cpp/.cc/.cxx..."

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
            "обнаружены новые неиндексированные .cpp: " +
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
MSVC compiler 'cl.exe' не найден.
Запустите скрипт из Visual Studio Developer PowerShell
или предварительно импортируйте Launch-VsDevShell.ps1.
"@
    }

    if (-not (Get-Command ninja -ErrorAction Ignore)) {
        throw "Не найден ninja.exe."
    }

    if (-not $env:VCPKG_ROOT) {
        throw "Не задана переменная VCPKG_ROOT."
    }

    $Toolchain = Join-Path `
        $env:VCPKG_ROOT `
        "scripts\buildsystems\vcpkg.cmake"

    if (-not (Test-Path $Toolchain)) {
        throw "Не найден vcpkg toolchain: $Toolchain"
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

    Write-Step "Обновление CMake-модели и compilation database"

    Invoke-Checked cmake "--preset" $Preset

    if (-not (Test-Path $GeneratedDatabase)) {
        throw "CMake не создал compilation database: $GeneratedDatabase"
    }

    Copy-Item `
        $GeneratedDatabase `
        $RootDatabase `
        -Force

    $Database = @(
        Get-Content $RootDatabase -Raw |
            ConvertFrom-Json
    )

    Write-Host "    compile_commands.json обновлён."
    Write-Host "    Translation units: $($Database.Count)"
}

function Ensure-SerenaProject {
    param([Parameter(Mandatory)][string] $RepositoryRoot)

    Require-Command serena | Out-Null

    $ProjectFile = Join-Path $RepositoryRoot ".serena\project.yml"

    if (-not (Test-Path $ProjectFile)) {
        Write-Step "Создание Serena project"
        Invoke-Checked serena "project" "create" "--index"
        return
    }

    Write-Step "Обновление индекса Serena"
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
        Write-Step "Первичное построение Graphify-графа"
        Invoke-Checked graphify "extract" "." "--code-only"
        return
    }

    Write-Step "Обновление Graphify-графа"

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
        Write-Host "Инструкция агенту:"
        Write-Host @'
1. Сначала прочитай только релевантные Serena memories.
2. Для выбора подсистем и входных точек используй scoped Graphify queries.
3. Definitions, references, callers и overloads проверяй через Serena/clangd.
4. Читай тела только конкретных символов.
5. Используй rg только для строк, макросов, CMake/конфигов и fallback.
6. Не перечитывай неизменённые файлы без конкретного пробела в информации.
'@
    }
    else {
        Write-Host "Завершающая инструкция агенту:"
        Write-Host @'
Обнови Serena memories только если изменились устойчивые сведения:
архитектурные границы, публичные API, доменные инварианты,
стандартная сборка или соглашения проекта.
Не сохраняй текущий diff, историю отладки и временные детали реализации.
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
        Write-Step "Проверка актуальности compilation database"

        $Stopwatch = [System.Diagnostics.Stopwatch]::StartNew()

        $State = Get-CodeIntelState `
            -RepositoryRoot $RepositoryRoot `
            -RootDatabase $RootDatabase

        $Stopwatch.Stop()
        Write-Host "    Проверка завершена за $(Format-Duration $Stopwatch.Elapsed)."

        if ($ForceCodeIntel -or $State.NeedsRefresh) {
            if ($ForceCodeIntel) {
                Write-Host "    Причина обновления: принудительно."
            }

            foreach ($Reason in $State.Reasons) {
                Write-Host "    Причина обновления: $Reason"
            }

            Update-CodeIntel `
                -Preset $Preset `
                -GeneratedDatabase $GeneratedDatabase `
                -RootDatabase $RootDatabase

            Ensure-SerenaProject `
                -RepositoryRoot $RepositoryRoot
        }
        else {
            Write-Host "    Code-intel актуален."
            Write-Host "    CMake и полный Serena index не запускаются."
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

        Write-Step "Запуск Codex с профилем '$CodexProfile'"
        & codex "--profile" $CodexProfile
    }
}
finally {
    Pop-Location
}
