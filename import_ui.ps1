# Imports the SquareLine Studio UI export into this example project.
#
# Mirrors the export directory into main/ui/, excluding SquareLine's own
# CMakeLists.txt (its add_library(ui ...) conflicts with the ESP-IDF component
# build), filelist.txt, and project.info. Mirroring also deletes files for
# screens that were renamed/removed in SquareLine, which would otherwise stay
# behind and break the build (SRC_DIRS compiles everything in ui/).
#
# Usage: .\import_ui.ps1 [-Source <path-to-squareline-export>]
#
# By default the export is expected as a sibling of this repo:
#   <parent>\rammp-hmi-p4\import_ui.ps1   (this script)
#   <parent>\ui_standalone                (the SquareLine export)

param(
    [string]$Source
)

$ErrorActionPreference = "Stop"

$RepoRoot = $PSScriptRoot
if (-not $Source) {
    $Source = Join-Path (Split-Path -Parent $RepoRoot) "ui_standalone"
}
$Source = [System.IO.Path]::GetFullPath($Source)

$Dest = Join-Path $RepoRoot "main\ui"
$Excluded = @("CMakeLists.txt", "filelist.txt", "project.info")

# --- sanity checks on the export ---------------------------------------------
if (-not (Test-Path $Source -PathType Container)) {
    $repoName = Split-Path -Leaf $RepoRoot
    Write-Error @"
SquareLine export directory not found: $Source
Expected 'ui_standalone' to sit beside '$repoName' in $(Split-Path -Parent $RepoRoot).
Either export/move the UI there, or pass the path explicitly:
  .\import_ui.ps1 -Source <path-to-squareline-export>
"@
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

# --- drop SquareLine's duplicate generated font sources -----------------------
# SquareLine writes each generated font .c into BOTH assets/ and fonts/. Only the
# fonts/ copy is compiled (ui/assets is deliberately not in SRC_DIRS), and these
# run to megabytes each - enough to OOM clang-format in the pre-commit hook. Keep
# the real source assets (.ttf/.fcfg/.bin/.svg) and drop the duplicate .c. Done
# after the mirror because robocopy /XF rejects an absolute wildcard path.
$dupFonts = @(Get-ChildItem (Join-Path $Dest "assets") -Filter *.c -File -ErrorAction SilentlyContinue)
if ($dupFonts) {
    $dupFonts | Remove-Item -Force
    Write-Host ""
    Write-Host "Dropped duplicate generated font source from ui/assets (ui/fonts copy is the one built):"
    $dupFonts | ForEach-Object { Write-Host "  assets/$($_.Name)" }
}

# --- warn about mirrored ui/ subdirs that the build does not compile ----------
# main/CMakeLists.txt lists ui subdirectories in SRC_DIRS explicitly. When
# SquareLine starts exporting a new folder of sources (e.g. ui/images for an
# added image asset), the mirror brings it in but nothing compiles it, and the
# only symptom is an "undefined reference to ui_img_*" at link time. Flag it
# here instead.
#
# ui/assets is deliberately not compiled: SquareLine puts the raw source assets
# there (.ttf/.svg/.fcfg) plus a duplicate copy of the generated font .c that
# also lands in ui/fonts - compiling both gives duplicate symbols.
$NotCompiled = @("assets")

$MainCMake = Join-Path $RepoRoot "main\CMakeLists.txt"
if (Test-Path $MainCMake) {
    $cmakeText = Get-Content $MainCMake -Raw
    $srcDirs = @()
    if ($cmakeText -match '(?s)SRC_DIRS\s+((?:"[^"]*"\s*)+)') {
        $srcDirs = [regex]::Matches($Matches[1], '"([^"]*)"') |
            ForEach-Object { $_.Groups[1].Value.Replace("\", "/").TrimEnd("/") }
    }

    if (-not $srcDirs) {
        Write-Host ""
        Write-Host "WARNING: could not parse SRC_DIRS from $MainCMake - skipping source-dir check."
    } else {
        $missing = Get-ChildItem $Dest -Directory |
            Where-Object { $NotCompiled -notcontains $_.Name } |
            Where-Object { Get-ChildItem $_.FullName -Filter *.c -File } |
            Where-Object { $srcDirs -notcontains "ui/$($_.Name)" } |
            ForEach-Object { $_.Name }

        if ($missing) {
            Write-Host ""
            Write-Host "WARNING: these ui/ subdirectories contain .c files but are not in"
            Write-Host "SRC_DIRS in main/CMakeLists.txt, so they will not be compiled"
            Write-Host "(expect 'undefined reference' errors at link time):"
            $missing | ForEach-Object { Write-Host "  ui/$_" }
            Write-Host ""
            Write-Host "Add them to SRC_DIRS in main/CMakeLists.txt to fix."
        }
    }
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
