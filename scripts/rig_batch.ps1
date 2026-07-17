# rig_batch.ps1 - PowerShell wrapper that re-rigs every .glb in a folder
# through scripts/rig_quadruped.py running under Blender headless.
#
# Usage:
#   ./scripts/rig_batch.ps1 -InputDir assets/models/meshy_raw
#   ./scripts/rig_batch.ps1 -InputDir assets/models/meshy_raw -OutputDir assets/models -Species dog
#   ./scripts/rig_batch.ps1 -InputDir assets/models/meshy_raw -FlipForward
#
# Parameters:
#   -InputDir      Folder containing the raw .glb files fresh from Meshy.
#   -OutputDir     Destination for the re-rigged files. Defaults to
#                  "<InputDir>/rigged/".
#   -Species       "cat" or "dog". Adjusts leg/tail proportions. Default: cat.
#   -FlipForward   Pass when a model comes out facing backward after rigging.
#                  Reverses the head-end / tail-end assignment.
#   -BlenderPath   Override if Blender isn't at the default install path.

param(
    [Parameter(Mandatory = $true)] [string] $InputDir,
    [string] $OutputDir   = "",
    [string] $Species     = "cat",
    [switch] $FlipForward,
    [string] $BlenderPath = "C:\Program Files\Blender Foundation\Blender 4.4\blender.exe"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $BlenderPath)) {
    Write-Error "Blender not found at '$BlenderPath'. Pass -BlenderPath to override."
    exit 1
}

if (-not (Test-Path $InputDir)) {
    Write-Error "Input directory not found: $InputDir"
    exit 1
}

# Default output = <InputDir>/rigged/ - keeps the raw Meshy files alongside
# the re-rigged copies so the user can A/B compare if a rig looks off.
if (-not $OutputDir) {
    $OutputDir = Join-Path $InputDir "rigged"
}
if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir | Out-Null
}

$scriptPath = Join-Path $PSScriptRoot "rig_quadruped.py"
if (-not (Test-Path $scriptPath)) {
    Write-Error "rig_quadruped.py not found next to this script."
    exit 1
}

$glbFiles = Get-ChildItem -Path $InputDir -Filter "*.glb" -File
if ($glbFiles.Count -eq 0) {
    Write-Host "No .glb files found in $InputDir - nothing to do."
    exit 0
}

Write-Host ""
Write-Host "Re-rigging $($glbFiles.Count) file(s) as species '$Species'..."
Write-Host "  Blender : $BlenderPath"
Write-Host "  Input   : $InputDir"
Write-Host "  Output  : $OutputDir"
if ($FlipForward) { Write-Host "  Flipped forward direction" }
Write-Host ""

$successCount = 0
$failCount    = 0

foreach ($file in $glbFiles) {
    $outputFile = Join-Path $OutputDir $file.Name
    Write-Host "-> $($file.Name)"

    # --factory-startup is REQUIRED, not cosmetic. This machine has the
    # broken mpfb / MakeHuman addon installed (its register() dies with a
    # SyntaxError on Blender 4.4). A half-registered addon does not merely
    # print a stderr traceback: it corrupts Blender's bundled glTF importer,
    # so bpy.ops.import_scene.gltf silently returns a degenerate 42-vertex
    # stand-in instead of the real 100k-250k-vertex Meshy mesh. Everything
    # downstream then runs on garbage and the export aborts with an
    # un-skinned file. --factory-startup never loads the user addon, so the
    # importer stays intact (verified: 121,131 verts on dog_regular vs 42).
    # The bundled io_scene_gltf2 importer/exporter is enabled under factory
    # settings, so import + skinned export still work. rig_quadruped.py also
    # carries a best-effort in-script guard, but the mpfb corruption is
    # timing-dependent, so disabling the addon at the CLI is the only
    # reliable fix.
    $blenderArgs = @(
        "--background",
        "--factory-startup",
        "--python", $scriptPath,
        "--",
        $file.FullName,
        $outputFile,
        "--species", $Species
    )
    if ($FlipForward) { $blenderArgs += "--flip-forward" }

    # Blender writes a lot of init noise to stdout/stderr. We must still
    # capture Blender's output for log filtering, but in Windows PowerShell
    # 5.1 the
    # combination of '2>&1' + '$ErrorActionPreference = Stop' (set at the
    # top of this script) wraps each stderr line as a NativeCommandError
    # ErrorRecord and terminates the script mid-loop. Solution: locally
    # relax ErrorActionPreference while running the native exe, restore it
    # afterward. $LASTEXITCODE remains the authoritative success signal.
    $prevErrorAction = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $output = & $BlenderPath @blenderArgs 2>&1
    $exitCode = $LASTEXITCODE
    $ErrorActionPreference = $prevErrorAction

    foreach ($line in $output) {
        $text = "$line"
        if ($text -match "^\[rig_quadruped\]" -or
            $text -match "^Re-rigged:" -or
            $text -match "^ERROR") {
            Write-Host "   $text"
        }
    }

    if ($exitCode -eq 0 -and (Test-Path $outputFile)) {
        $successCount++
    } else {
        $failCount++
        Write-Host "   FAILED (exit $exitCode)"
    }
}

Write-Host ""
Write-Host "Rigging complete: $successCount succeeded, $failCount failed."
Write-Host "Output: $OutputDir"
