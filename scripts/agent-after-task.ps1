#requires -Version 7.0

[CmdletBinding()]
param(
    [string] $Preset = "code-intel",
    [switch] $ForceCodeIntel,
    [switch] $ForceGraph,
    [switch] $SkipGraph,
    [switch] $SkipSerena
)

& "$PSScriptRoot\AgentContext.ps1" `
    -Phase After `
    -Preset $Preset `
    -ForceCodeIntel:$ForceCodeIntel `
    -ForceGraph:$ForceGraph `
    -NoLaunch `
    -SkipGraph:$SkipGraph `
    -SkipSerena:$SkipSerena
