#requires -Version 7.0

[CmdletBinding()]
param(
    [string] $Preset = "code-intel",
    [string] $CodexProfile = "repo",
    [switch] $ForceCodeIntel,
    [switch] $ForceGraph,
    [switch] $NoLaunch,
    [switch] $SkipGraph,
    [switch] $SkipSerena
)

& "$PSScriptRoot\AgentContext.ps1" `
    -Phase Before `
    -Preset $Preset `
    -CodexProfile $CodexProfile `
    -ForceCodeIntel:$ForceCodeIntel `
    -ForceGraph:$ForceGraph `
    -NoLaunch:$NoLaunch `
    -SkipGraph:$SkipGraph `
    -SkipSerena:$SkipSerena
