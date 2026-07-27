$ErrorActionPreference = "Stop"

$projectRoot = $PSScriptRoot
$dependencyLockPath = Join-Path $projectRoot "dependencies.lock.json"
$dependencyLock = (Get-Content -LiteralPath $dependencyLockPath -Raw |
    ConvertFrom-Json).kotorPatchManager
$sourceLayoutPath = Join-Path $projectRoot "source-layout.txt"
$dependenciesRoot = Join-Path $projectRoot ".deps"
$upstreamRoot = Join-Path $dependenciesRoot "Kotor-Patch-Manager"

$layoutEntries = @(
    Get-Content -LiteralPath $sourceLayoutPath |
        ForEach-Object {
            $line = ($_ -split "#", 2)[0].Trim()
            if (-not $line) {
                return
            }

            $parts = @($line -split "\s+")
            $expectedParts = switch ($parts[0]) {
                "source" { 4 }
                "replace" { 3 }
                "remove" { 3 }
                "ignore" { 2 }
                default { throw "Unknown source-layout action '$($parts[0])'." }
            }
            if ($parts.Count -ne $expectedParts) {
                throw "Invalid source-layout entry: $line"
            }

            [pscustomobject]@{
                Action = $parts[0]
                Values = @($parts[1..($parts.Count - 1)])
            }
        }
)

$sourceEntries = @($layoutEntries | Where-Object Action -eq "source")
if ($sourceEntries.Count -eq 0) {
    throw "source-layout.txt does not define any source trees."
}

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw "Git is required to download the pinned Kotor Patch Manager source."
}

if (-not (Test-Path -LiteralPath (Join-Path $upstreamRoot ".git"))) {
    New-Item -ItemType Directory -Path $dependenciesRoot -Force | Out-Null

    & git init $upstreamRoot
    if ($LASTEXITCODE -ne 0) { throw "Could not initialize the upstream checkout." }
    & git -C $upstreamRoot remote add origin $dependencyLock.repository
    if ($LASTEXITCODE -ne 0) { throw "Could not configure the upstream checkout." }
    & git -C $upstreamRoot sparse-checkout init --cone
    if ($LASTEXITCODE -ne 0) { throw "Could not initialize sparse checkout." }
}
else {
    $configuredRepository = & git -C $upstreamRoot remote get-url origin
    if ($LASTEXITCODE -ne 0 -or
        $configuredRepository.Trim() -ne $dependencyLock.repository) {
        throw "The cached upstream checkout has an unexpected origin. Remove '.deps' and rebuild."
    }
}

$currentRevision = & git -C $upstreamRoot rev-list -n 1 --all
if (-not $currentRevision -or
    $currentRevision.Trim() -ne $dependencyLock.revision) {
    & git -C $upstreamRoot fetch --depth 1 origin $dependencyLock.revision
    if ($LASTEXITCODE -ne 0) { throw "Could not fetch the pinned upstream revision." }
    & git -C $upstreamRoot checkout --detach $dependencyLock.revision
    if ($LASTEXITCODE -ne 0) { throw "Could not check out the pinned upstream revision." }
}

$sparsePaths = @($sourceEntries | ForEach-Object { $_.Values[1] })
& git -C $upstreamRoot sparse-checkout set @sparsePaths
if ($LASTEXITCODE -ne 0) {
    throw "Could not select the source trees listed in source-layout.txt."
}

$ignoredEntries = @($layoutEntries | Where-Object Action -eq "ignore")
foreach ($entry in $ignoredEntries) {
    $ignoredPath = Join-Path $upstreamRoot ($entry.Values[0] -replace "/", "\")
    if (Test-Path -LiteralPath $ignoredPath) {
        throw "Ignored upstream folder was materialized by sparse checkout: $($entry.Values[0])"
    }
}

$projectPrefix = [System.IO.Path]::GetFullPath($projectRoot).TrimEnd("\") + "\"
$generatedRoots = @{}

foreach ($entry in $sourceEntries) {
    $name = $entry.Values[0]
    if ($generatedRoots.ContainsKey($name)) {
        throw "Duplicate source-layout source name: $name"
    }

    $upstreamSource = Join-Path $upstreamRoot ($entry.Values[1] -replace "/", "\")
    if (-not (Test-Path -LiteralPath $upstreamSource -PathType Container)) {
        throw "The pinned checkout did not contain '$($entry.Values[1])'."
    }

    $generatedRoot = [System.IO.Path]::GetFullPath(
        (Join-Path $projectRoot ($entry.Values[2] -replace "/", "\")))
    if (-not $generatedRoot.StartsWith(
            $projectPrefix,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to generate source outside the project: $generatedRoot"
    }

    if (Test-Path -LiteralPath $generatedRoot) {
        Remove-Item -LiteralPath $generatedRoot -Recurse -Force
    }
    Copy-Item -LiteralPath $upstreamSource -Destination $generatedRoot -Recurse
    $generatedRoots[$name] = $generatedRoot
}

$replacementEntries = @($layoutEntries | Where-Object Action -eq "replace")
$listedOverrides = @(
    $replacementEntries |
        ForEach-Object { "$($_.Values[0])/$($_.Values[1])" }
)
$overridesRoot = Join-Path $projectRoot "overrides"
Get-ChildItem -LiteralPath $overridesRoot -Recurse -File | ForEach-Object {
    $relativeOverride = $_.FullName.Substring($overridesRoot.Length + 1).
        Replace("\", "/")
    if ($listedOverrides -notcontains $relativeOverride) {
        throw "Override file is not listed in source-layout.txt: $relativeOverride"
    }
}

foreach ($entry in $replacementEntries) {
    $name = $entry.Values[0]
    if (-not $generatedRoots.ContainsKey($name)) {
        throw "Replacement refers to unknown source '$name'."
    }

    $relativePath = $entry.Values[1] -replace "/", "\"
    $replacement = Join-Path $projectRoot "overrides\$name\$relativePath"
    if (-not (Test-Path -LiteralPath $replacement -PathType Leaf)) {
        throw "Replacement file is missing: overrides/$name/$($entry.Values[1])"
    }

    $generatedRoot = $generatedRoots[$name]
    $generatedPrefix = $generatedRoot.TrimEnd("\") + "\"
    $destination = [System.IO.Path]::GetFullPath(
        (Join-Path $generatedRoot $relativePath))
    if (-not $destination.StartsWith(
            $generatedPrefix,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to replace a path outside generated source '$name': $destination"
    }

    $destinationDirectory = Split-Path -Parent $destination
    New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
    Copy-Item -LiteralPath $replacement -Destination $destination -Force
}

foreach ($entry in @($layoutEntries | Where-Object Action -eq "remove")) {
    $name = $entry.Values[0]
    if (-not $generatedRoots.ContainsKey($name)) {
        throw "Removal refers to unknown source '$name'."
    }

    $generatedRoot = $generatedRoots[$name]
    $generatedPrefix = $generatedRoot.TrimEnd("\") + "\"
    $target = [System.IO.Path]::GetFullPath(
        (Join-Path $generatedRoot ($entry.Values[1] -replace "/", "\")))
    if (-not $target.StartsWith(
            $generatedPrefix,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove a path outside generated source '$name': $target"
    }
    if (Test-Path -LiteralPath $target) {
        Remove-Item -LiteralPath $target -Recurse -Force
    }
}

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "Visual Studio Installer's vswhere.exe was not found."
}

$msbuild = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -find "MSBuild\**\Bin\MSBuild.exe" |
    Select-Object -First 1
if (-not $msbuild) {
    throw "Visual Studio C++ build tools were not found."
}

& $msbuild `
    (Join-Path $projectRoot "SpiderManPatcher.Runtime\KotorPatcher.vcxproj") `
    /p:Configuration=Release /p:Platform=Win32 /m
if ($LASTEXITCODE -ne 0) {
    throw "The native runtime build failed."
}

& $msbuild `
    (Join-Path $projectRoot "Patches\RendererD3D11\SpiderManRenderer.vcxproj") `
    /p:Configuration=Release /p:Platform=Win32 /m
if ($LASTEXITCODE -ne 0) {
    throw "The D3D11 renderer build failed."
}

& $msbuild `
    (Join-Path $projectRoot "Patches\Presentation60\Presentation60.vcxproj") `
    /p:Configuration=Release /p:Platform=Win32 /m
if ($LASTEXITCODE -ne 0) {
    throw "The experimental 60 Hz presentation patch build failed."
}

& $msbuild `
    (Join-Path $projectRoot "Patches\TextConfig\TextConfig.vcxproj") `
    /p:Configuration=Release /p:Platform=Win32 /m
if ($LASTEXITCODE -ne 0) {
    throw "The plain-text configuration patch build failed."
}

& $msbuild `
    (Join-Path $projectRoot "Patches\ResolutionConfigurable\ResolutionConfigurable.vcxproj") `
    /p:Configuration=Release /p:Platform=Win32 /m
if ($LASTEXITCODE -ne 0) {
    throw "The configurable resolution patch build failed."
}

& $msbuild `
    (Join-Path $projectRoot "Patches\CameraAspect\CameraAspect.vcxproj") `
    /p:Configuration=Release /p:Platform=Win32 /m
if ($LASTEXITCODE -ne 0) {
    throw "The aspect-correct camera patch build failed."
}

& $msbuild `
    (Join-Path $projectRoot "Patches\AudioBalance\AudioBalance.vcxproj") `
    /p:Configuration=Release /p:Platform=Win32 /m
if ($LASTEXITCODE -ne 0) {
    throw "The authored SFX volume patch build failed."
}

& $msbuild `
    (Join-Path $projectRoot "Patches\HudAspect\HudAspect.vcxproj") `
    /p:Configuration=Release /p:Platform=Win32 /m
if ($LASTEXITCODE -ne 0) {
    throw "The aspect-correct HUD patch build failed."
}

& $msbuild `
    (Join-Path $projectRoot "Patches\DrawDistance\DrawDistance.vcxproj") `
    /p:Configuration=Release /p:Platform=Win32 /m
if ($LASTEXITCODE -ne 0) {
    throw "The extended draw-distance patch build failed."
}

& $msbuild `
    (Join-Path $projectRoot "Patches\FogForce\FogForce.vcxproj") `
    /p:Configuration=Release /p:Platform=Win32 /m
if ($LASTEXITCODE -ne 0) {
    throw "The restored fog patch build failed."
}

$dist = Join-Path $projectRoot "dist"
if (Test-Path -LiteralPath $dist) {
    $resolvedDist = [System.IO.Path]::GetFullPath($dist)
    $expectedDist = [System.IO.Path]::GetFullPath((Join-Path $projectRoot "dist"))
    if ($resolvedDist -ne $expectedDist) {
        throw "Refusing to clean unexpected output directory: $resolvedDist"
    }
    Remove-Item -LiteralPath $resolvedDist -Recurse -Force
}

dotnet publish `
    (Join-Path $projectRoot "SpiderManPatcher\SpiderManPatcher.csproj") `
    --configuration Release `
    --runtime win-x86 `
    --self-contained true `
    -p:PublishSingleFile=true `
    -p:IncludeNativeLibrariesForSelfExtract=true `
    -p:DebugType=None `
    -p:DebugSymbols=false `
    --output $dist
if ($LASTEXITCODE -ne 0) {
    throw "The launcher publish failed."
}

$patchesDirectory = Join-Path $dist "patches"
New-Item -ItemType Directory -Path $patchesDirectory -Force | Out-Null

$noCdSource = Join-Path $projectRoot "Patches\NoCD"
$noCdZip = Join-Path $patchesDirectory "no-cd.zip"
$noCdPatch = Join-Path $patchesDirectory "no-cd.kpatch"
$noCdFiles = @(
    Join-Path $noCdSource "manifest.toml"
) + @(
    Get-ChildItem -LiteralPath $noCdSource -Filter "*hooks.toml" -File |
        Sort-Object Name |
        Select-Object -ExpandProperty FullName
)
Compress-Archive `
    -LiteralPath $noCdFiles `
    -DestinationPath $noCdZip `
    -Force
if (Test-Path -LiteralPath $noCdPatch) {
    Remove-Item -LiteralPath $noCdPatch -Force
}
Move-Item -LiteralPath $noCdZip -Destination $noCdPatch

$resolutionSource = Join-Path $projectRoot "Patches\ResolutionConfigurable"
$resolutionBinary = Join-Path $resolutionSource "bin\Release\windows_x86.dll"
if (-not (Test-Path -LiteralPath $resolutionBinary)) {
    throw "The configurable resolution patch binary was not produced: $resolutionBinary"
}

$resolutionStage = Join-Path $resolutionSource "package"
if (Test-Path -LiteralPath $resolutionStage) {
    Remove-Item -LiteralPath $resolutionStage -Recurse -Force
}
$resolutionStageBinaries = Join-Path $resolutionStage "binaries"
New-Item -ItemType Directory -Path $resolutionStageBinaries -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $resolutionSource "manifest.toml") -Destination $resolutionStage
Copy-Item -LiteralPath (Join-Path $resolutionSource "resolution-configurable.hooks.toml") -Destination $resolutionStage
Copy-Item -LiteralPath $resolutionBinary -Destination $resolutionStageBinaries

$resolutionZip = Join-Path $patchesDirectory "resolution-configurable.zip"
$resolutionPatch = Join-Path $patchesDirectory "resolution-configurable.kpatch"
Compress-Archive `
    -Path (Join-Path $resolutionStage "*") `
    -DestinationPath $resolutionZip `
    -Force
if (Test-Path -LiteralPath $resolutionPatch) {
    Remove-Item -LiteralPath $resolutionPatch -Force
}
Move-Item -LiteralPath $resolutionZip -Destination $resolutionPatch
Remove-Item -LiteralPath $resolutionStage -Recurse -Force

$cameraSource = Join-Path $projectRoot "Patches\CameraAspect"
$cameraBinary = Join-Path $cameraSource "bin\Release\windows_x86.dll"
if (-not (Test-Path -LiteralPath $cameraBinary)) {
    throw "The aspect-correct camera patch binary was not produced: $cameraBinary"
}

$cameraStage = Join-Path $cameraSource "package"
if (Test-Path -LiteralPath $cameraStage) {
    Remove-Item -LiteralPath $cameraStage -Recurse -Force
}
$cameraStageBinaries = Join-Path $cameraStage "binaries"
New-Item -ItemType Directory -Path $cameraStageBinaries -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $cameraSource "manifest.toml") -Destination $cameraStage
Copy-Item -LiteralPath (Join-Path $cameraSource "camera-aspect.hooks.toml") -Destination $cameraStage
Copy-Item -LiteralPath $cameraBinary -Destination $cameraStageBinaries

$cameraZip = Join-Path $patchesDirectory "camera-aspect-correct.zip"
$cameraPatch = Join-Path $patchesDirectory "camera-aspect-correct.kpatch"
Compress-Archive `
    -Path (Join-Path $cameraStage "*") `
    -DestinationPath $cameraZip `
    -Force
if (Test-Path -LiteralPath $cameraPatch) {
    Remove-Item -LiteralPath $cameraPatch -Force
}
Move-Item -LiteralPath $cameraZip -Destination $cameraPatch
Remove-Item -LiteralPath $cameraStage -Recurse -Force

$windowedSource = Join-Path $projectRoot "Patches\WindowedMode"
$windowedZip = Join-Path $patchesDirectory "windowed-mode.zip"
$windowedPatch = Join-Path $patchesDirectory "windowed-mode.kpatch"
$windowedFiles = @(
    Join-Path $windowedSource "manifest.toml"
    Join-Path $windowedSource "windowed-mode.hooks.toml"
)
Compress-Archive `
    -LiteralPath $windowedFiles `
    -DestinationPath $windowedZip `
    -Force
if (Test-Path -LiteralPath $windowedPatch) {
    Remove-Item -LiteralPath $windowedPatch -Force
}
Move-Item -LiteralPath $windowedZip -Destination $windowedPatch

$inputFocusSource = Join-Path $projectRoot "Patches\InputFocusFix"
$inputFocusZip = Join-Path $patchesDirectory "input-focus-fix.zip"
$inputFocusPatch = Join-Path $patchesDirectory "input-focus-fix.kpatch"
$inputFocusFiles = @(
    Join-Path $inputFocusSource "manifest.toml"
    Join-Path $inputFocusSource "input-focus-fix.hooks.toml"
)
Compress-Archive `
    -LiteralPath $inputFocusFiles `
    -DestinationPath $inputFocusZip `
    -Force
if (Test-Path -LiteralPath $inputFocusPatch) {
    Remove-Item -LiteralPath $inputFocusPatch -Force
}
Move-Item -LiteralPath $inputFocusZip -Destination $inputFocusPatch

$gameUpdateSource = Join-Path $projectRoot "Patches\GameUpdate30Hz"
$gameUpdateZip = Join-Path $patchesDirectory "game-update-30hz.zip"
$gameUpdatePatch = Join-Path $patchesDirectory "game-update-30hz.kpatch"
$gameUpdateFiles = @(
    Join-Path $gameUpdateSource "manifest.toml"
    Join-Path $gameUpdateSource "game-update-30hz.hooks.toml"
)
Compress-Archive `
    -LiteralPath $gameUpdateFiles `
    -DestinationPath $gameUpdateZip `
    -Force
if (Test-Path -LiteralPath $gameUpdatePatch) {
    Remove-Item -LiteralPath $gameUpdatePatch -Force
}
Move-Item -LiteralPath $gameUpdateZip -Destination $gameUpdatePatch

$presentationSource = Join-Path $projectRoot "Patches\Presentation60"
$presentationBinary = Join-Path $presentationSource "bin\Release\windows_x86.dll"
if (-not (Test-Path -LiteralPath $presentationBinary)) {
    throw "The experimental presentation patch binary was not produced: $presentationBinary"
}

$presentationStage = Join-Path $presentationSource "package"
if (Test-Path -LiteralPath $presentationStage) {
    Remove-Item -LiteralPath $presentationStage -Recurse -Force
}
$presentationStageBinaries = Join-Path $presentationStage "binaries"
New-Item -ItemType Directory -Path $presentationStageBinaries -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $presentationSource "manifest.toml") -Destination $presentationStage
Copy-Item -LiteralPath (Join-Path $presentationSource "presentation-60.hooks.toml") -Destination $presentationStage
Copy-Item -LiteralPath $presentationBinary -Destination $presentationStageBinaries

$presentationZip = Join-Path $patchesDirectory "presentation-60-interpolated.zip"
$presentationPatch = Join-Path $patchesDirectory "presentation-60-interpolated.kpatch"
Compress-Archive -Path (Join-Path $presentationStage "*") -DestinationPath $presentationZip -Force
if (Test-Path -LiteralPath $presentationPatch) {
    Remove-Item -LiteralPath $presentationPatch -Force
}
Move-Item -LiteralPath $presentationZip -Destination $presentationPatch
Remove-Item -LiteralPath $presentationStage -Recurse -Force

$textConfigSource = Join-Path $projectRoot "Patches\TextConfig"
$textConfigBinary = Join-Path $textConfigSource "bin\Release\windows_x86.dll"
if (-not (Test-Path -LiteralPath $textConfigBinary)) {
    throw "The plain-text configuration patch binary was not produced: $textConfigBinary"
}

$textConfigStage = Join-Path $textConfigSource "package"
if (Test-Path -LiteralPath $textConfigStage) {
    Remove-Item -LiteralPath $textConfigStage -Recurse -Force
}
$textConfigStageBinaries = Join-Path $textConfigStage "binaries"
New-Item -ItemType Directory -Path $textConfigStageBinaries -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $textConfigSource "manifest.toml") -Destination $textConfigStage
Copy-Item -LiteralPath (Join-Path $textConfigSource "text-config.hooks.toml") -Destination $textConfigStage
Copy-Item -LiteralPath $textConfigBinary -Destination $textConfigStageBinaries

$textConfigZip = Join-Path $patchesDirectory "text-config.zip"
$textConfigPatch = Join-Path $patchesDirectory "text-config.kpatch"
Compress-Archive -Path (Join-Path $textConfigStage "*") -DestinationPath $textConfigZip -Force
if (Test-Path -LiteralPath $textConfigPatch) {
    Remove-Item -LiteralPath $textConfigPatch -Force
}
Move-Item -LiteralPath $textConfigZip -Destination $textConfigPatch
Remove-Item -LiteralPath $textConfigStage -Recurse -Force

$rendererSource = Join-Path $projectRoot "Patches\RendererD3D11"
$rendererBinary = Join-Path $rendererSource "bin\Release\windows_x86.dll"
if (-not (Test-Path -LiteralPath $rendererBinary)) {
    throw "The D3D11 renderer binary was not produced: $rendererBinary"
}

$rendererStage = Join-Path $rendererSource "package"
if (Test-Path -LiteralPath $rendererStage) {
    $resolvedRendererStage = [System.IO.Path]::GetFullPath($rendererStage)
    $expectedRendererStage = [System.IO.Path]::GetFullPath(
        (Join-Path $rendererSource "package"))
    if ($resolvedRendererStage -ne $expectedRendererStage) {
        throw "Refusing to clean unexpected renderer package directory: $resolvedRendererStage"
    }
    Remove-Item -LiteralPath $resolvedRendererStage -Recurse -Force
}

$rendererStageBinaries = Join-Path $rendererStage "binaries"
New-Item -ItemType Directory -Path $rendererStageBinaries -Force | Out-Null
Copy-Item `
    -LiteralPath (Join-Path $rendererSource "manifest.toml") `
    -Destination $rendererStage
Copy-Item `
    -LiteralPath (Join-Path $rendererSource "renderer-d3d11.hooks.toml") `
    -Destination $rendererStage
Copy-Item `
    -LiteralPath $rendererBinary `
    -Destination $rendererStageBinaries

$rendererZip = Join-Path $patchesDirectory "renderer-d3d11.zip"
$rendererPatch = Join-Path $patchesDirectory "renderer-d3d11.kpatch"
Compress-Archive `
    -Path (Join-Path $rendererStage "*") `
    -DestinationPath $rendererZip `
    -Force
if (Test-Path -LiteralPath $rendererPatch) {
    Remove-Item -LiteralPath $rendererPatch -Force
}
Move-Item -LiteralPath $rendererZip -Destination $rendererPatch
Remove-Item -LiteralPath $rendererStage -Recurse -Force

$audioBalanceSource = Join-Path $projectRoot "Patches\AudioBalance"
$audioBalanceBinary = Join-Path $audioBalanceSource "bin\Release\windows_x86.dll"
if (-not (Test-Path -LiteralPath $audioBalanceBinary)) {
    throw "The authored SFX volume patch binary was not produced: $audioBalanceBinary"
}

$audioBalanceStage = Join-Path $audioBalanceSource "package"
if (Test-Path -LiteralPath $audioBalanceStage) {
    Remove-Item -LiteralPath $audioBalanceStage -Recurse -Force
}
$audioBalanceStageBinaries = Join-Path $audioBalanceStage "binaries"
New-Item -ItemType Directory -Path $audioBalanceStageBinaries -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $audioBalanceSource "manifest.toml") -Destination $audioBalanceStage
Copy-Item -LiteralPath (Join-Path $audioBalanceSource "audio-balance.hooks.toml") -Destination $audioBalanceStage
Copy-Item -LiteralPath $audioBalanceBinary -Destination $audioBalanceStageBinaries

$audioBalanceZip = Join-Path $patchesDirectory "authored-sfx-volume.zip"
$audioBalancePatch = Join-Path $patchesDirectory "authored-sfx-volume.kpatch"
Compress-Archive -Path (Join-Path $audioBalanceStage "*") -DestinationPath $audioBalanceZip -Force
if (Test-Path -LiteralPath $audioBalancePatch) {
    Remove-Item -LiteralPath $audioBalancePatch -Force
}
Move-Item -LiteralPath $audioBalanceZip -Destination $audioBalancePatch
Remove-Item -LiteralPath $audioBalanceStage -Recurse -Force

$hudAspectSource = Join-Path $projectRoot "Patches\HudAspect"
$hudAspectBinary = Join-Path $hudAspectSource "bin\Release\windows_x86.dll"
if (-not (Test-Path -LiteralPath $hudAspectBinary)) {
    throw "The aspect-correct HUD patch binary was not produced: $hudAspectBinary"
}

$hudAspectStage = Join-Path $hudAspectSource "package"
if (Test-Path -LiteralPath $hudAspectStage) {
    Remove-Item -LiteralPath $hudAspectStage -Recurse -Force
}
$hudAspectStageBinaries = Join-Path $hudAspectStage "binaries"
New-Item -ItemType Directory -Path $hudAspectStageBinaries -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $hudAspectSource "manifest.toml") -Destination $hudAspectStage
Copy-Item -LiteralPath (Join-Path $hudAspectSource "hud-aspect.hooks.toml") -Destination $hudAspectStage
Copy-Item -LiteralPath $hudAspectBinary -Destination $hudAspectStageBinaries

$hudAspectZip = Join-Path $patchesDirectory "hud-aspect-correct.zip"
$hudAspectPatch = Join-Path $patchesDirectory "hud-aspect-correct.kpatch"
Compress-Archive -Path (Join-Path $hudAspectStage "*") -DestinationPath $hudAspectZip -Force
if (Test-Path -LiteralPath $hudAspectPatch) {
    Remove-Item -LiteralPath $hudAspectPatch -Force
}
Move-Item -LiteralPath $hudAspectZip -Destination $hudAspectPatch
Remove-Item -LiteralPath $hudAspectStage -Recurse -Force

$drawDistanceSource = Join-Path $projectRoot "Patches\DrawDistance"
$drawDistanceBinary = Join-Path $drawDistanceSource "bin\Release\windows_x86.dll"
if (-not (Test-Path -LiteralPath $drawDistanceBinary)) {
    throw "The extended draw-distance patch binary was not produced: $drawDistanceBinary"
}

$drawDistanceStage = Join-Path $drawDistanceSource "package"
if (Test-Path -LiteralPath $drawDistanceStage) {
    Remove-Item -LiteralPath $drawDistanceStage -Recurse -Force
}
$drawDistanceStageBinaries = Join-Path $drawDistanceStage "binaries"
New-Item -ItemType Directory -Path $drawDistanceStageBinaries -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $drawDistanceSource "manifest.toml") -Destination $drawDistanceStage
Copy-Item -LiteralPath (Join-Path $drawDistanceSource "draw-distance.hooks.toml") -Destination $drawDistanceStage
Copy-Item -LiteralPath $drawDistanceBinary -Destination $drawDistanceStageBinaries

$drawDistanceZip = Join-Path $patchesDirectory "draw-distance-extended.zip"
$drawDistancePatch = Join-Path $patchesDirectory "draw-distance-extended.kpatch"
Compress-Archive -Path (Join-Path $drawDistanceStage "*") -DestinationPath $drawDistanceZip -Force
if (Test-Path -LiteralPath $drawDistancePatch) {
    Remove-Item -LiteralPath $drawDistancePatch -Force
}
Move-Item -LiteralPath $drawDistanceZip -Destination $drawDistancePatch
Remove-Item -LiteralPath $drawDistanceStage -Recurse -Force

$fogForceSource = Join-Path $projectRoot "Patches\FogForce"
$fogForceBinary = Join-Path $fogForceSource "bin\Release\windows_x86.dll"
if (-not (Test-Path -LiteralPath $fogForceBinary)) {
    throw "The restored fog patch binary was not produced: $fogForceBinary"
}

$fogForceStage = Join-Path $fogForceSource "package"
if (Test-Path -LiteralPath $fogForceStage) {
    Remove-Item -LiteralPath $fogForceStage -Recurse -Force
}
$fogForceStageBinaries = Join-Path $fogForceStage "binaries"
New-Item -ItemType Directory -Path $fogForceStageBinaries -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $fogForceSource "manifest.toml") -Destination $fogForceStage
Copy-Item -LiteralPath (Join-Path $fogForceSource "fog-force.hooks.toml") -Destination $fogForceStage
Copy-Item -LiteralPath $fogForceBinary -Destination $fogForceStageBinaries

$fogForceZip = Join-Path $patchesDirectory "fog-restored.zip"
$fogForcePatch = Join-Path $patchesDirectory "fog-restored.kpatch"
Compress-Archive -Path (Join-Path $fogForceStage "*") -DestinationPath $fogForceZip -Force
if (Test-Path -LiteralPath $fogForcePatch) {
    Remove-Item -LiteralPath $fogForcePatch -Force
}
Move-Item -LiteralPath $fogForceZip -Destination $fogForcePatch
Remove-Item -LiteralPath $fogForceStage -Recurse -Force

Write-Host "Built patcher: $dist"
