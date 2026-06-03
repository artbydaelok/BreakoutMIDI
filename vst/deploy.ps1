# Deploys the freshly built Release VST3 to the per-user VST3 folder.
# The DAW reaches it via a junction (Program Files\VSTPlugins\BreakoutMIDI.vst3
# -> this per-user folder), so updating here updates what the DAW loads.
#
# Close the DAW first, or it will hold the plugin file locked and the copy fails.

$ErrorActionPreference = "Stop"

$config = if ($args.Count -ge 1) { $args[0] } else { "Release" }
$src = "$PSScriptRoot\build\BreakoutMIDI_artefacts\$config\VST3\BreakoutMIDI.vst3"
$dst = "$env:LOCALAPPDATA\Programs\Common\VST3\BreakoutMIDI.vst3"

if (-not (Test-Path $src)) { throw "Build artifact not found: $src  (build the $config config first)" }

New-Item -ItemType Directory -Force -Path (Split-Path $dst) | Out-Null
if (Test-Path $dst) { Remove-Item -LiteralPath $dst -Recurse -Force }
Copy-Item -LiteralPath $src -Destination $dst -Recurse -Force

$bin = Join-Path $dst "Contents\x86_64-win\BreakoutMIDI.vst3"
Write-Host "Deployed $config -> $dst"
Write-Host ("  binary: {0:N0} bytes, {1}" -f (Get-Item $bin).Length, (Get-Item $bin).LastWriteTime)
