[CmdletBinding()]
param(
    [string]$Port = "COM4",
    [ValidateRange(5000, 90000)]
    [int]$PositionMillidegrees = 15000,
    [ValidateRange(5, 120)]
    [int]$TestSpeedRpm = 30,
    [ValidateRange(100, 3000)]
    [int]$AccelerationRpmPerSecond = 500,
    [switch]$ConfirmUserPresent,
    [switch]$ConfirmMechanismSuspended,
    [switch]$ConfirmCurrentLimitedSupply,
    [switch]$ConfirmPhysicalDisconnectReady
)

$ErrorActionPreference = "Stop"

if (-not $ConfirmUserPresent -or
    -not $ConfirmMechanismSuspended -or
    -not $ConfirmCurrentLimitedSupply -or
    -not $ConfirmPhysicalDisconnectReady) {
    throw "Acceptance test requires all four physical safety confirmations."
}

$commandTool = Join-Path $PSScriptRoot "zdt_backup_command.ps1"
$safety = @{
    ConfirmUserPresent = $true
    ConfirmMechanismSuspended = $true
    ConfirmCurrentLimitedSupply = $true
    ConfirmPhysicalDisconnectReady = $true
}
$results = [System.Collections.Generic.List[object]]::new()

function Invoke-ZdtStage {
    param(
        [string]$Stage,
        [string]$Action,
        [hashtable]$Extra = @{}
    )

    $arguments = @{
        Port = $Port
        Action = $Action
        Axis = "Gen2"
        CaptureSeconds = 0.5
        PassThru = $true
    }
    if ($Action -in @("Enable", "Speed", "Position")) {
        foreach ($key in $safety.Keys) {
            $arguments[$key] = $safety[$key]
        }
    }
    foreach ($key in $Extra.Keys) {
        $arguments[$key] = $Extra[$key]
    }

    $result = & $commandTool @arguments
    $result | Add-Member -NotePropertyName Stage -NotePropertyValue $Stage
    $results.Add($result)
    Write-Host ("{0,-22} online={1} enabled={2} motion={3} speed={4,5} rpm position={5,9} mdeg status=0x{6:X2}" -f `
        $Stage, $result.Gen2Online, $result.Gen2Enabled,
        $result.Gen2MotionActive, $result.Gen2SpeedRpm,
        $result.Gen2PositionMillidegrees, $result.Gen2MotorStatusFlags)
    if ($result.Status -ne 0) {
        throw "Stage '$Stage' was rejected with status $($result.Status)."
    }
    return $result
}

function Assert-Healthy {
    param([object]$Result, [string]$Stage)

    if (-not $Result.Gen2Online) {
        throw "Gen2 went offline at '$Stage'."
    }
    if ($Result.Gen2Stalled -or $Result.Gen2StallProtected) {
        throw "Stall status was reported at '$Stage'."
    }
}

try {
    Invoke-ZdtStage -Stage "select" -Action "Select" | Out-Null
    Start-Sleep -Milliseconds 500
    $baseline = Invoke-ZdtStage -Stage "baseline" -Action "Status"
    Assert-Healthy $baseline "baseline"

    Invoke-ZdtStage -Stage "enable" -Action "Enable" | Out-Null
    Start-Sleep -Milliseconds 500
    $enabled = Invoke-ZdtStage -Stage "enabled-status" -Action "Status"
    Assert-Healthy $enabled "enabled-status"
    if (-not $enabled.Gen2Enabled) {
        throw "Gen2 did not enter the enabled state."
    }

    Invoke-ZdtStage -Stage "position-forward" -Action "Position" -Extra @{
        PositionMillidegrees = $PositionMillidegrees
        PositionSpeedRpm = $TestSpeedRpm
        AccelerationRpmPerSecond = $AccelerationRpmPerSecond
        PositionMode = "RelativeCurrent"
    } | Out-Null
    Start-Sleep -Milliseconds 1200
    $forward = Invoke-ZdtStage -Stage "forward-status" -Action "Status"
    Assert-Healthy $forward "forward-status"

    Invoke-ZdtStage -Stage "position-return" -Action "Position" -Extra @{
        PositionMillidegrees = -$PositionMillidegrees
        PositionSpeedRpm = $TestSpeedRpm
        AccelerationRpmPerSecond = $AccelerationRpmPerSecond
        PositionMode = "RelativeCurrent"
    } | Out-Null
    Start-Sleep -Milliseconds 1200
    $returned = Invoke-ZdtStage -Stage "return-status" -Action "Status"
    Assert-Healthy $returned "return-status"

    Invoke-ZdtStage -Stage "speed-forward" -Action "Speed" -Extra @{
        SpeedRpm = $TestSpeedRpm
        AccelerationRpmPerSecond = $AccelerationRpmPerSecond
    } | Out-Null
    $speedForward = Invoke-ZdtStage -Stage "speed-forward-status" -Action "Status"
    Assert-Healthy $speedForward "speed-forward-status"
    Invoke-ZdtStage -Stage "speed-forward-stop" -Action "Stop" | Out-Null
    Start-Sleep -Milliseconds 500
    $forwardStopped = Invoke-ZdtStage -Stage "forward-stop-status" -Action "Status"

    Invoke-ZdtStage -Stage "speed-reverse" -Action "Speed" -Extra @{
        SpeedRpm = -$TestSpeedRpm
        AccelerationRpmPerSecond = $AccelerationRpmPerSecond
    } | Out-Null
    $speedReverse = Invoke-ZdtStage -Stage "speed-reverse-status" -Action "Status"
    Assert-Healthy $speedReverse "speed-reverse-status"
    Invoke-ZdtStage -Stage "speed-reverse-stop" -Action "Stop" | Out-Null
    Start-Sleep -Milliseconds 500
    $reverseStopped = Invoke-ZdtStage -Stage "reverse-stop-status" -Action "Status"

    $leaseCountBefore = $reverseStopped.Gen2SpeedLeaseExpiredCount
    Invoke-ZdtStage -Stage "speed-lease-start" -Action "Speed" -Extra @{
        SpeedRpm = $TestSpeedRpm
        AccelerationRpmPerSecond = $AccelerationRpmPerSecond
    } | Out-Null
    Start-Sleep -Milliseconds 1800
    $leaseStopped = Invoke-ZdtStage -Stage "speed-lease-status" -Action "Status"
    Assert-Healthy $leaseStopped "speed-lease-status"

    if (($forward.Gen2PositionMillidegrees -
            $baseline.Gen2PositionMillidegrees) -lt 3000) {
        throw "Forward position movement was too small."
    }
    if ([Math]::Abs($returned.Gen2PositionMillidegrees -
            $baseline.Gen2PositionMillidegrees) -gt 5000) {
        throw "Return position error exceeded 5 degrees."
    }
    if ($speedForward.Gen2SpeedRpm -lt 5) {
        throw "Forward speed feedback did not become positive."
    }
    if ($speedReverse.Gen2SpeedRpm -gt -5) {
        throw "Reverse speed feedback did not become negative."
    }
    if ([Math]::Abs($forwardStopped.Gen2SpeedRpm) -gt 2 -or
        [Math]::Abs($reverseStopped.Gen2SpeedRpm) -gt 2) {
        throw "Speed feedback did not return to zero after Stop."
    }
    if ($leaseStopped.Gen2MotionActive -or
        [Math]::Abs($leaseStopped.Gen2SpeedRpm) -gt 2 -or
        $leaseStopped.Gen2SpeedLeaseExpiredCount -ne
            ($leaseCountBefore + 1)) {
        throw "The firmware speed lease did not stop the motor."
    }
    if ($reverseStopped.Gen2InvalidResponseCount -ne
        $baseline.Gen2InvalidResponseCount) {
        throw "Invalid UART response count increased during the test."
    }
}
finally {
    try {
        Invoke-ZdtStage -Stage "final-stop" -Action "Stop" | Out-Null
        Invoke-ZdtStage -Stage "final-disable" -Action "Disable" | Out-Null
        Start-Sleep -Milliseconds 500
        Invoke-ZdtStage -Stage "final-status" -Action "Status" | Out-Null
        Invoke-ZdtStage -Stage "deselect" -Action "Deselect" | Out-Null
    } catch {
        Write-Warning "Final stop/disable sequence failed: $_"
    }
}

$results | Select-Object Stage,Gen2Online,Gen2Enabled,Gen2MotionActive,
    Gen2SpeedRpm,Gen2PositionMillidegrees,Gen2MotorStatusFlags,
    Gen2LastFunction,Gen2LastReplyStatus,Gen2InvalidResponseCount,
    Gen2ResponseCount,Gen2TimeoutCount,Gen2SpeedLeaseExpiredCount |
    Format-Table -AutoSize
