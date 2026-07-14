# Imports the SquareLine Studio UI export into this example project.
#
# Mirrors the export directory into main/ui/, excluding SquareLine's own
# CMakeLists.txt (its add_library(ui ...) conflicts with the ESP-IDF component
# build), filelist.txt, and project.info. Mirroring also deletes files for
# screens that were renamed/removed in SquareLine, which would otherwise stay
# behind and break the build (SRC_DIRS compiles everything in ui/).
#
# Usage: .\import_ui.ps1 [-Source <path-to-squareline-export>]

param(
    [string]$Source = "C:\Users\halai\Offline_Documents\ATDev\ui_standalone"
)

$ErrorActionPreference = "Stop"

$Dest = Join-Path $PSScriptRoot "main\ui"
$Excluded = @("CMakeLists.txt", "filelist.txt", "project.info")

# --- sanity checks on the export ---------------------------------------------
if (-not (Test-Path $Source -PathType Container)) {
    Write-Error "Source directory not found: $Source"
    exit 1
}
foreach ($required in @("ui.c", "ui.h")) {
    if (-not (Test-Path (Join-Path $Source $required))) {
        Write-Error "Source does not look like a SquareLine export: missing $required in $Source"
        exit 1
    }
}

Write-Host "Importing SquareLine UI"
Write-Host "  from: $Source"
Write-Host "  to:   $Dest"
Write-Host ""

# --- mirror copy, excluding SquareLine's build/project files ------------------
# /MIR mirrors (copies new/changed, deletes stale), /XF excludes files by name.
# /NJH /NJS /NDL trim the output to just the per-file lines.
robocopy $Source $Dest /MIR /XF $Excluded /NJH /NJS /NDL
$rc = $LASTEXITCODE

# Robocopy exit codes 0-7 are success (bitmask: 1=copied, 2=extra deleted, 4=mismatch).
if ($rc -ge 8) {
    Write-Error "robocopy failed with exit code $rc"
    exit $rc
}
if ($rc -eq 0) {
    Write-Host "Already up to date - no files changed."
} else {
    Write-Host ""
    Write-Host "Import complete (robocopy code $rc)."
}

# --- remind about SquareLine events that need an implementation ---------------
$eventsHeader = Join-Path $Dest "ui_events.h"
if (Test-Path $eventsHeader) {
    $eventDecls = Select-String -Path $eventsHeader -Pattern '^\s*void\s+\w+\s*\(' |
        ForEach-Object { $_.Line.Trim() }
    if ($eventDecls) {
        Write-Host ""
        Write-Host "NOTE: ui_events.h declares event functions that must be implemented"
        Write-Host "in the app (e.g. in main/) or linking will fail:"
        $eventDecls | ForEach-Object { Write-Host "  $_" }
    }
}

Write-Host ""
Write-Host "Rebuild with: idf.py build flash monitor"
exit 0
