# Imports the SquareLine Studio UI export into this example project.
#
# Mirrors the export directory into main/ui/, excluding SquareLine's own
# CMakeLists.txt (its add_library(ui ...) conflicts with the ESP-IDF component
# build), filelist.txt, project.info, and ui_events.cpp. Mirroring also deletes files for
# screens that were renamed/removed in SquareLine, which would otherwise stay
# behind and break the build (SRC_DIRS compiles everything in ui/).
#
# Usage: .\import_ui.ps1 [-Source <path-to-squareline-export>]
#
# By default the export is expected as a sibling of this repo:
#   <parent>\rammp-hmi-p4\import_ui.ps1   (this script)
#   <parent>\ui_c_files                (the SquareLine export)

param(
    [string]$Source
)

$ErrorActionPreference = "Stop"

$RepoRoot = $PSScriptRoot
if (-not $Source) {
    $Source = Join-Path (Split-Path -Parent $RepoRoot) "ui_c_files"
}
$Source = [System.IO.Path]::GetFullPath($Source)

$Dest = Join-Path $RepoRoot "main\ui"
# ui_events.cpp holds hand-written Call-function bodies; SquareLine regenerates
# it with empty stubs, so keep the repo copy and ignore the exported one.
$Excluded = @("CMakeLists.txt", "filelist.txt", "project.info", "ui_events.cpp")

# --- sanity checks on the export ---------------------------------------------
if (-not (Test-Path $Source -PathType Container)) {
    $repoName = Split-Path -Leaf $RepoRoot
    Write-Error @"
SquareLine export directory not found: $Source
Expected 'ui_c_files' to sit beside '$repoName' in $(Split-Path -Parent $RepoRoot).
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

# Snapshot the source file set so we can tell afterwards whether screens were
# added or removed (see the CMake re-configure step below).
$srcsBefore = @(Get-ChildItem $Dest -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Extension -in ".c", ".cpp" } |
    ForEach-Object { $_.FullName } | Sort-Object)

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

# --- force a CMake re-configure when screens were added or removed ------------
# SRC_DIRS expands its glob only when CMake configures. Delete a screen in
# SquareLine and ninja keeps a rule for the vanished file ("missing and no known
# rule to make it"); add one and it is silently never compiled. Touching the
# component's CMakeLists.txt makes ninja re-run CMake on the next build, since
# CMake emits a rule tying build.ninja to those files.
$srcsAfter = @(Get-ChildItem $Dest -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Extension -in ".c", ".cpp" } |
    ForEach-Object { $_.FullName } | Sort-Object)
if (Compare-Object $srcsBefore $srcsAfter) {
    $mainCMake = Join-Path $RepoRoot "main\CMakeLists.txt"
    if (Test-Path $mainCMake) {
        (Get-Item $mainCMake).LastWriteTime = Get-Date
        Write-Host ""
        Write-Host "Source file set changed - touched main/CMakeLists.txt so CMake re-configures."
    }
}

# --- warn about mirrored ui/ subdirs that the build does not compile ----------
# main/CMakeLists.txt lists ui subdirectories in SRC_DIRS explicitly. When
# SquareLine starts exporting a new folder of sources (e.g. ui/images for an
# added image asset), the mirror brings it in but nothing compiles it, and the
# only symptom is an "undefined reference to ui_img_*" at link time. Flag it
# here instead.
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
