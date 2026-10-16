param([switch]$Test, [string]$RuntimeDirectory = "outputs\决构编辑器-v0.1.15")
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $projectRoot.StartsWith('D:\', [StringComparison]::OrdinalIgnoreCase)) { throw 'This project must remain on D:.' }
$toolBin = Join-Path $projectRoot 'tools\llvm-mingw-20260616-ucrt-x86_64\bin'
$premake = Join-Path $projectRoot 'tools\premake\premake5.exe'
$env:PATH = "$toolBin;$env:PATH"
$env:TEMP = Join-Path $projectRoot 'work'
$env:TMP = $env:TEMP
$eventConfig = Join-Path $projectRoot 'upstream\event\include\event2\event-config.h'
$content = Get-Content -LiteralPath $eventConfig -Raw
$content = $content.Replace('/* #undef EVENT__HAVE_STRTOK_R */', '#define EVENT__HAVE_STRTOK_R 1')
Set-Content -LiteralPath $eventConfig -Value $content -Encoding utf8
Push-Location (Join-Path $projectRoot 'upstream')
try {
    & $premake gmake --cc=clang --no-dxsdk --no-audio --use-simd=none
    if ($LASTEXITCODE -ne 0) { throw 'Project generation failed.' }
    & "$toolBin\mingw32-make.exe" -C build -j 6 config=release CC=clang CXX=clang++ AR=llvm-ar 'LDFLAGS=-municode -lopengl32 -lgdi32 -limm32 -lole32 -luuid'
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
    Copy-Item -LiteralPath 'bin\release\YGOPro.exe' -Destination (Join-Path (Join-Path $projectRoot $RuntimeDirectory) 'YGOPro.exe')
} finally { Pop-Location }
if ($Test) {
    & 'D:\Codex\tools\Python314\python.exe' (Join-Path $PSScriptRoot 'test_engine.py')
    if ($LASTEXITCODE -ne 0) { throw 'Engine verification failed.' }
    & 'D:\Codex\tools\Python314\python.exe' (Join-Path $PSScriptRoot 'test_gui.py') --sync
    if ($LASTEXITCODE -ne 0) { throw 'GUI verification failed.' }
    & 'D:\Codex\tools\Python314\python.exe' (Join-Path $PSScriptRoot 'test_gui.py') --perspective
    if ($LASTEXITCODE -ne 0) { throw 'Perspective verification failed.' }
}
