# Imports the SquareLine Studio UI export into this example project.
#
# Mirrors the export directory into main/ui/, excluding SquareLine's own
# CMakeLists.txt (its add_library(ui ...) conflicts with the ESP-IDF component
# build), filelist.txt, project.info, and ui_events.cpp. Mirroring also deletes files for
# screens that were renamed/removed in SquareLine, which would otherwise stay
# behind and break the build (SRC_DIRS compiles everything in ui/).
#
# After mirroring it converts any indexed image (I1/I2/I4/I8) in the export: to A8
# when the palette is a single ink, to RGB565A8 when it carries real colour.
# The sw renderer cannot draw indexed data, so those images fault or vanish as soon
# as they are scaled or rotated. SquareLine infers the format from the source PNG's
# colour count (256 or fewer distinct colours -> indexed) and cannot emit A8 at all,
# so a single-ink icon can only ever arrive indexed. The conversion is therefore
# unconditional rather than a prompt. Each rewritten file keeps a .c.orig beside it.
#
# Usage: .\import_ui.ps1 [-Source <path-to-squareline-export>] [-bfm]
#
# -bfm builds, flashes and monitors with idf.py once the import succeeds,
# loading the ESP-IDF PowerShell environment first if idf.py is not on PATH.
#
# By default the export is expected as a sibling of this repo, or of any
# directory above it:
#   <parent>\<this repo>\import_ui.ps1   (this script)
#   <parent>\ui_c_files                  (the SquareLine export)

param(
    [string]$Source,
    # -bfm: hand off to idf.py once the import is done.
    [Alias("bfm")]
    [switch]$BuildFlashMonitor
)

$ErrorActionPreference = "Stop"

$RepoRoot = $PSScriptRoot
if (-not $Source) {
    # The export normally sits beside this repo, but moving the repo down a
    # level (ATDev\<repo> -> ATDev\rammp\<repo>) silently broke a hard-coded
    # "..\ui_c_files". Walk up from the repo and take the first ui_c_files that
    # actually looks like an export.
    $probe = Split-Path -Parent $RepoRoot
    while ($probe) {
        $candidate = Join-Path $probe "ui_c_files"
        if (Test-Path (Join-Path $candidate "ui.c")) {
            $Source = $candidate
            break
        }
        $probe = Split-Path -Parent $probe
    }
    if (-not $Source) {
        $Source = Join-Path (Split-Path -Parent $RepoRoot) "ui_c_files"
    }
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
SquareLine export directory not found.
Looked for a 'ui_c_files' directory beside '$repoName' and beside each of its
parent directories, starting at $(Split-Path -Parent $RepoRoot).
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

# --- convert indexed images the renderer cannot draw --------------------------
# The sw renderer has no indexed support, so LVGL falls back to a line-by-line
# decoder that breaks under any transform: a scaled-up image wraps a uint32_t
# offset and panics, a scaled-down or rotated one silently draws nothing.
#
# There is no way to avoid this upstream. SquareLine infers the format from the
# source PNG's colour count -- 256 or fewer distinct colours exports indexed
# (measured: Lock 247 -> I8, turbo 1105 -> RGB565A8) -- and a single-ink alpha
# mask is one RGB times at most 256 alpha levels, so it is always under that
# bound. SquareLine cannot emit A8 at all. So convert unconditionally rather
# than asking a question whose answer is always yes, and just say what changed.
# A colourful palette (a photo that quantised under 256 colours) becomes
# RGB565A8 instead of A8, which triples its flash cost -- hence the per-file
# format and byte count in the report below.
$assetTool = Join-Path $RepoRoot "scripts\ui_assets.py"
if (-not (Test-Path $assetTool)) {
    # nothing to do
} elseif (-not (Get-Command python -ErrorAction SilentlyContinue)) {
    Write-Host ""
    Write-Host "NOTE: python not on PATH - skipped the image colour-format conversion."
} else {
    $assetReport = & python $assetTool check --fix
    $assetFailed = $LASTEXITCODE -ne 0
    $converted = @($assetReport | Where-Object { $_ -match '^\s+converted ' })

    if ($converted.Count -gt 0) {
        Write-Host ""
        Write-Host "WARNING: converted $($converted.Count) image(s) out of indexed colour formats." -ForegroundColor Yellow
        $converted | ForEach-Object { Write-Host $_ -ForegroundColor Yellow }
        Write-Host "Indexed images fault or draw nothing when scaled or rotated. This runs" -ForegroundColor Yellow
        Write-Host "on every import: SquareLine picks indexed for any source PNG with 256 or" -ForegroundColor Yellow
        Write-Host "fewer colours and cannot export A8. Originals kept as .c.orig." -ForegroundColor Yellow
    }

    # Anything the converter refused (a map the descriptor disagrees with, i.e. an
    # export this tool cannot parse) needs a person to look at it, so make it loud.
    $refused = @($assetReport | Where-Object { $_ -match 'CANNOT convert' })
    if ($refused.Count -gt 0) {
        Write-Host ""
        $refused | ForEach-Object { Write-Host $_ -ForegroundColor Red }
        Write-Host "Those images are still indexed and will fault or vanish when transformed." -ForegroundColor Red
    }

    # Everything else the tool reported (e.g. the set_scale advisory) prints plain.
    $assetReport |
        Where-Object { $_ -notmatch '^\s+converted |CANNOT convert|^Converting ' } |
        Where-Object { $_.Trim() -ne "" } |
        ForEach-Object { Write-Host $_ }

    if ($assetFailed -and $refused.Count -eq 0) {
        Write-Host ""
        Write-Host "NOTE: image check reported a problem it could not fix automatically."
    }
}

# --- optionally hand off to idf.py -------------------------------------------
if (-not $BuildFlashMonitor) {
    Write-Host ""
    Write-Host "Rebuild with: idf.py build flash monitor  (or re-run with -bfm)"
    exit 0
}

# idf.py is not on PATH by default; the ESP-IDF PowerShell profile defines it as
# a global alias. Skip loading when it already resolves - the profile prepends to
# PATH every time it runs, so re-sourcing it in an already-exported shell just
# grows PATH.
if (-not (Get-Command "idf.py" -ErrorAction SilentlyContinue)) {
    # The filename carries the IDF version, so a toolchain upgrade moves it.
    $idfProfile = "C:\Espressif\tools\Microsoft.v6.0.PowerShell_profile.ps1"
    if (-not (Test-Path $idfProfile)) {
        $idfProfile = Get-ChildItem "C:\Espressif\tools\Microsoft.*.PowerShell_profile.ps1" `
            -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending |
            Select-Object -First 1 -ExpandProperty FullName
    }
    if (-not $idfProfile) {
        Write-Error @"
-bfm needs idf.py, but neither idf.py nor an ESP-IDF PowerShell profile was found
under C:\Espressif\tools. Open an ESP-IDF shell and run 'idf.py build flash monitor'
by hand, or re-run the import without -bfm.
"@
        exit 1
    }

    Write-Host ""
    Write-Host "Loading ESP-IDF environment: $idfProfile"
    # The profile activates a venv and probes for optional tools; a stray
    # non-terminating error there must not abort the import script.
    $prevEAP = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & $idfProfile | Out-Null
    } finally {
        $ErrorActionPreference = $prevEAP
    }

    if (-not (Get-Command "idf.py" -ErrorAction SilentlyContinue)) {
        Write-Error "idf.py is still not available after loading $idfProfile."
        exit 1
    }
}

Write-Host ""
Write-Host "Running: idf.py build flash monitor"
Write-Host ""
# Run from the repo root: the import can be launched from anywhere, but idf.py
# resolves the project from the working directory.
Push-Location $RepoRoot
try {
    idf.py build flash monitor
    $idfExit = $LASTEXITCODE
} finally {
    Pop-Location
}
exit $idfExit
