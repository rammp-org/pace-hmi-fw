# Windows Mobile Hotspot (tethering) status and restart, through WinRT.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File hotspot.ps1 status
#   powershell -NoProfile -ExecutionPolicy Bypass -File hotspot.ps1 restart
#
# Prints one line: `STATE <On|Off|InTransition|Unknown> CLIENTS <n>` (status), or
# `STOP <result> START <result>` then the status line (restart). Exit 0 when the
# hotspot ends On. Needs Windows PowerShell 5.1 (WinRT projection).
# The bench allows one restart per run (plan §6); run_bench.py enforces that.
param([Parameter(Mandatory = $true)][ValidateSet('status', 'restart')][string]$Action)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Runtime.WindowsRuntime
$asTask = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
        $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
        $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' })[0]
function Await($op, [Type]$type) {
    $task = $asTask.MakeGenericMethod($type).Invoke($null, @($op))
    if (-not $task.Wait(30000)) { throw "WinRT operation timed out" }
    $task.Result
}

$null = [Windows.Networking.Connectivity.NetworkInformation, Windows.Networking.Connectivity, ContentType = WindowsRuntime]
$null = [Windows.Networking.NetworkOperators.NetworkOperatorTetheringManager, Windows.Networking.NetworkOperators, ContentType = WindowsRuntime]
$resultType = [Windows.Networking.NetworkOperators.NetworkOperatorTetheringOperationResult, Windows.Networking.NetworkOperators, ContentType = WindowsRuntime]

$connProfile = [Windows.Networking.Connectivity.NetworkInformation]::GetInternetConnectionProfile()
if ($null -eq $connProfile) {
    $connProfile = [Windows.Networking.Connectivity.NetworkInformation]::GetConnectionProfiles() |
        Where-Object { $_.GetNetworkConnectivityLevel() -ne 'None' } | Select-Object -First 1
}
if ($null -eq $connProfile) { Write-Output 'STATE Unknown CLIENTS 0 (no connection profile)'; exit 2 }
$mgr = [Windows.Networking.NetworkOperators.NetworkOperatorTetheringManager]::CreateFromConnectionProfile($connProfile)

function Show() {
    Write-Output ("STATE {0} CLIENTS {1}" -f $mgr.TetheringOperationalState, $mgr.ClientCount)
}

if ($Action -eq 'restart') {
    $stop = 'skipped'
    if ($mgr.TetheringOperationalState -eq 'On') {
        $stop = (Await ($mgr.StopTetheringAsync()) $resultType).Status
    }
    # Wait for Off before starting again: a condition with a timeout, not a fixed sleep.
    $deadline = (Get-Date).AddSeconds(20)
    while ($mgr.TetheringOperationalState -ne 'Off' -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 250 }
    $start = (Await ($mgr.StartTetheringAsync()) $resultType).Status
    Write-Output "STOP $stop START $start"
}
Show
if ($mgr.TetheringOperationalState -eq 'On') { exit 0 } else { exit 1 }
