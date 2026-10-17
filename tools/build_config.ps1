function Get-BuildSetting([string]$Name, [string]$Default = '') {
    $value = [Environment]::GetEnvironmentVariable('DUEL_EDITOR_' + $Name.ToUpperInvariant())
    $configFile = Join-Path (Split-Path -Parent $PSScriptRoot) 'build-config.local.json'
    if (-not $value -and (Test-Path -LiteralPath $configFile)) {
        $config = Get-Content -LiteralPath $configFile -Raw -Encoding UTF8 | ConvertFrom-Json
        $property = $config.PSObject.Properties[$Name]
        if ($property) { $value = [string]$property.Value }
    }
    if (-not $value) { $value = $Default }
    if (-not $value) { throw "Configure DUEL_EDITOR_$($Name.ToUpperInvariant()) or '$Name' in build-config.local.json." }
    return [Environment]::ExpandEnvironmentVariables($value)
}
function Get-BuildPath([string]$Name, [string]$Default = '') {
    $value = Get-BuildSetting $Name $Default
    if (-not [IO.Path]::IsPathRooted($value)) { $value = Join-Path (Split-Path -Parent $PSScriptRoot) $value }
    return [IO.Path]::GetFullPath($value)
}
function Get-UnityData {
    $hub = Join-Path $env:ProgramFiles 'Unity/Hub/Editor'
    $candidate = Get-ChildItem -LiteralPath $hub -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending | ForEach-Object { Join-Path $_.FullName 'Editor/Data' } | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    return Get-BuildPath 'unity_data' $candidate
}
