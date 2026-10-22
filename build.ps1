$ErrorActionPreference = "Stop"

$projectRoot = $PSScriptRoot
$buildRoot = Join-Path $projectRoot "build"
$dependencyLockPath = Join-Path $projectRoot "dependencies.lock.json"
$dependencyLock = (Get-Content -LiteralPath $dependencyLockPath -Raw |
    ConvertFrom-Json).kotorPatchManager
$sourceLayoutPath = Join-Path $projectRoot "source-layout.txt"
$dependenciesRoot = Join-Path $buildRoot ".deps"
$upstreamRoot = Join-Path $dependenciesRoot "Kotor-Patch-Manager"

function Copy-SourceTree {
    param(
        [Parameter(Mandatory)] [string] $Source,
        [Parameter(Mandatory)] [string] $Destination,
        [string[]] $AdditionalExcludedDirectories = @()
    )

    $excludedDirectories = @(
        ".vs", "bin", "build", "obj", "package", "temp_package", "Win32", "x64"
    ) + $AdditionalExcludedDirectories
    $excludedExtensions = @(".dll", ".exe", ".exp", ".iobj", ".ipdb", ".kpatch", ".lib", ".pdb", ".zip")

    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    Get-ChildItem -LiteralPath $Source -Recurse -File | ForEach-Object {
        $relativePath = $_.FullName.Substring($Source.Length + 1)
        $pathParts = @($relativePath -split "[\\/]")
        if ($pathParts | Where-Object { $excludedDirectories -contains $_ }) {
            return
        }
        if ($_.Name -eq "build.log" -or $excludedExtensions -contains $_.Extension) {
            return
        }

        $destinationPath = Join-Path $Destination $relativePath
        New-Item -ItemType Directory -Path (Split-Path -Parent $destinationPath) -Force |
            Out-Null
        Copy-Item -LiteralPath $_.FullName -Destination $destinationPath
    }
}

New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null

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
        throw "The cached upstream checkout has an unexpected origin. Remove 'build/.deps' and rebuild."
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

$buildPrefix = [System.IO.Path]::GetFullPath($buildRoot).TrimEnd("\") + "\"
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
        (Join-Path $buildRoot ($entry.Values[2] -replace "/", "\")))
    if (-not $generatedRoot.StartsWith(
            $buildPrefix,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to generate source outside the build directory: $generatedRoot"
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

$binaryOutput = Join-Path $buildRoot "bin"
if (Test-Path -LiteralPath $binaryOutput) {
    Remove-Item -LiteralPath $binaryOutput -Recurse -Force
}

& $msbuild `
    (Join-Path $buildRoot "SpiderManPatcher.Runtime\KotorPatcher.vcxproj") `
    /p:Configuration=Release /p:Platform=Win32 /m
if ($LASTEXITCODE -ne 0) {
    throw "The native runtime build failed."
}

$launcherSource = Join-Path $projectRoot "SpiderManPatcher"
$launcherBuild = Join-Path $buildRoot "SpiderManPatcher"
if (Test-Path -LiteralPath $launcherBuild) {
    Remove-Item -LiteralPath $launcherBuild -Recurse -Force
}
Copy-SourceTree -Source $launcherSource -Destination $launcherBuild

$dist = Join-Path $buildRoot "dist"
if (Test-Path -LiteralPath $dist) {
    $resolvedDist = [System.IO.Path]::GetFullPath($dist)
    $expectedDist = [System.IO.Path]::GetFullPath((Join-Path $buildRoot "dist"))
    if ($resolvedDist -ne $expectedDist) {
        throw "Refusing to clean unexpected output directory: $resolvedDist"
    }
    Remove-Item -LiteralPath $resolvedDist -Recurse -Force
}

dotnet publish `
    (Join-Path $launcherBuild "SpiderManPatcher.csproj") `
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

$patchSourceRoot = Join-Path $projectRoot "Patches"
$patchesRoot = Join-Path $buildRoot "Patches"
$sourcePatchBuilder = Join-Path $patchSourceRoot "create-patch.bat"
$patchBuilder = Join-Path $patchesRoot "create-patch.bat"
$patchPublisher = Join-Path $upstreamRoot "publish-patches.ps1"
if (-not (Test-Path -LiteralPath $sourcePatchBuilder -PathType Leaf)) {
    throw "The patch builder was not found: $sourcePatchBuilder"
}
if (-not (Test-Path -LiteralPath $patchPublisher -PathType Leaf)) {
    throw "The upstream patch publisher was not found: $patchPublisher"
}

$patchSources = @(
    Get-ChildItem -LiteralPath $patchSourceRoot -Directory |
        Where-Object {
            Test-Path -LiteralPath (Join-Path $_.FullName "manifest.toml")
        } |
        Sort-Object Name
)
if ($patchSources.Count -eq 0) {
    throw "No patch source directories were found in: $patchSourceRoot"
}

if (Test-Path -LiteralPath $patchesRoot) {
    Remove-Item -LiteralPath $patchesRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $patchesRoot | Out-Null
foreach ($supportFile in Get-ChildItem -LiteralPath $patchSourceRoot -File) {
    Copy-Item -LiteralPath $supportFile.FullName -Destination $patchesRoot
}
Copy-SourceTree `
    -Source $generatedRoots["KPatchCommon"] `
    -Destination (Join-Path $patchesRoot "Common")
foreach ($patchSource in $patchSources) {
    Copy-SourceTree `
        -Source $patchSource.FullName `
        -Destination (Join-Path $patchesRoot $patchSource.Name) `
        -AdditionalExcludedDirectories @("tests")
}

& $patchPublisher -PatchesDir $patchesRoot -OutDir $patchesDirectory

$builtPatches = @(
    Get-ChildItem -LiteralPath $patchesDirectory -Filter "*.kpatch" -File
)
if ($builtPatches.Count -ne $patchSources.Count) {
    throw "Patch publishing produced $($builtPatches.Count) of $($patchSources.Count) expected packages."
}

Write-Host "Built patcher: $dist"
