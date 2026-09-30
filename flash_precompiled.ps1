# Flashes the binaries in precompiled/ to a Tab5.
#
# For anyone who needs firmware on a board without a working ESP-IDF build. The
# folder is gitignored build output, filled either by a local `idf.py build` (see
# the `precompiled` target in CMakeLists.txt) or by extracting a release zip into
# it -- CI attaches the same images to every release.
#
# Usage: .\flash_precompiled.ps1 [-Port COM6]
#
# The only prerequisite is esptool v5 or newer. It is found automatically if
# ESP-IDF is installed on this machine; otherwise `pip install esptool`.
#
# -Port is only needed when more than one serial device is attached (or when
# auto-detect picks the wrong one) - with a single board connected it is found
# on its own.

param(
    [string]$Port
)

$ErrorActionPreference = "Stop"

$Dir = Join-Path $PSScriptRoot "precompiled"
if (-not (Test-Path (Join-Path $Dir "flash_args"))) {
    throw "No binaries in $Dir - build the project once, or extract a release zip into it."
}

# esptool, in order of preference: whatever is on PATH, then the copy inside the
# ESP-IDF tools venv (present on any machine with ESP-IDF installed, and does not
# need the IDF environment to be loaded first), then a pip install into the
# system python.
$Esptool = $null
$EsptoolArgs = @()

$OnPath = Get-Command "esptool.exe", "esptool" -ErrorAction SilentlyContinue | Select-Object -First 1
if ($OnPath) {
    $Esptool = $OnPath.Source
} else {
    $Candidates = @()
    if ($env:IDF_PYTHON_ENV_PATH) {
        $Candidates += Join-Path $env:IDF_PYTHON_ENV_PATH "Scripts\esptool.exe"
    }
    # e.g. C:\Espressif\tools\python\v6.0\venv\Scripts\esptool.exe - newest first,
    # so a machine with several IDF versions installed uses the latest esptool.
    $Candidates += Get-ChildItem "C:\Espressif\tools\python\*\venv\Scripts\esptool.exe" -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | ForEach-Object { $_.FullName }
    $Esptool = $Candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $Esptool) {
    $Python = Get-Command "python.exe", "python" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($Python) {
        # find_spec rather than a plain `import esptool`, so a missing module says so
        # through the exit code instead of spilling a traceback that would have to be
        # redirected away (redirecting a native command's stderr under
        # $ErrorActionPreference = "Stop" throws in Windows PowerShell 5.1).
        & $Python.Source -c "import importlib.util, sys; sys.exit(0 if importlib.util.find_spec('esptool') else 1)"
        if ($LASTEXITCODE -eq 0) {
            $Esptool = $Python.Source
            $EsptoolArgs = @("-m", "esptool")
        }
    }
}
if (-not $Esptool) {
    throw "esptool not found. Install it with:  pip install esptool"
}

if (-not $Port) {
    # Win32_PnPEntity rather than [System.IO.Ports.SerialPort]::GetPortNames(),
    # because the same query gives the friendly device names needed below when the
    # choice is ambiguous.
    $Devices = @(Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match "\(COM\d+\)" })
    if ($Devices.Count -eq 1) {
        $Port = [regex]::Match($Devices[0].Name, "\((COM\d+)\)").Groups[1].Value
    } else {
        # Zero or several: name what is attached and let the operator pick, rather
        # than guessing and writing firmware to the wrong device.
        Write-Host "Could not pick a serial port automatically." -ForegroundColor Yellow
        $Devices | ForEach-Object { Write-Host "  $($_.Name)" }
        throw "Plug in one board, or name it: .\flash_precompiled.ps1 -Port COM6"
    }
}

Get-Content (Join-Path $Dir "manifest.txt") | Write-Host
Write-Host "flashing to $Port" -ForegroundColor Cyan

Push-Location $Dir
try {
    # 460800 is what idf.py flash uses; flash_args carries the mode/size/freq and
    # the three offsets, so nothing about the layout is repeated here.
    & $Esptool @EsptoolArgs --chip esp32p4 --port $Port --baud 460800 write-flash "@flash_args"
    $code = $LASTEXITCODE
} finally {
    Pop-Location
}

if ($code -ne 0) {
    throw "esptool failed with exit code $code"
}
Write-Host "done - the board should reboot into the new firmware." -ForegroundColor Green
