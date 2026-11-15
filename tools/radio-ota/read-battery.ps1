# SPDX-License-Identifier: GPL-2.0-or-later
# Explicit local, read-only Bluetooth acquisition. Build/test commands never run it.
#requires -Version 5.1
[CmdletBinding()]
param(
    [string] $BluetoothAddress = '',
    [string] $OutputPath = '',
    [ValidateRange(2, 10)] [int] $Count = 3,
    [ValidateRange(11, 60)] [int] $IntervalSeconds = 12
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSEdition -ne 'Desktop') { throw 'Run run-battery.cmd using Windows PowerShell 5.1.' }
. (Join-Path $PSScriptRoot 'ram-transport.ps1')
. (Join-Path $PSScriptRoot 'battery-core.ps1')
if ($OutputPath -eq '') { $OutputPath = 'gmk70-battery-radio-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '.json' }
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
if (Test-Path -LiteralPath $OutputPath) { throw 'Refusing to overwrite an existing capture.' }
$report = [ordered]@{
    format = 1; status = 'incomplete'; started_utc = [DateTime]::UtcNow.ToString('o')
    reference_image_sha256 = '100e2ecdad6d253ea6d177d91579c2e22ea8d5968d3f09d691ed599f7fe7512c'
    bounded_code_verified = $false; samples = @(); requests = @()
    limitation = 'Bluetooth reads can wake the radio. Armed timers do not prove an ADC conversion completed. No voltage measurement is captured.'
}
$failed = $true
$stage = 'connect'
try {
    Open-Gmk70TrialRadio -BluetoothAddress $BluetoothAddress -ReadOnly -ReadGuard { param($Packet) Assert-Gmk70BatteryReadPacket $Packet }
    # Preserve the first battery state before code checks/extra Bluetooth traffic.
    $firstTime = [DateTime]::UtcNow.ToString('o')
    $stage = 'first battery read'
    [byte[]] $first = Read-Gmk70TrialBlock -Address 0x11003cfe -Length 32
    [byte[]] $firstTimers = Read-Gmk70TrialBlock -Address 0x11004ddc -Length 40
    $firstTimerTime = [DateTime]::UtcNow.ToString('o')
    $stage = 'verify captured code layout'
    foreach ($entry in Get-Gmk70BatteryCodePlan) {
        $stage = 'verify captured code ' + ('0x{0:X8}' -f $entry.address)
        [byte[]] $code = Read-Gmk70TrialBlock -Address $entry.address -Length $entry.length -Storage:$entry.storage
        Assert-Gmk70BatteryCode -Bytes $code -Expected $entry.sha256
    }
    $report.bounded_code_verified = $true
    for ($index = 0; $index -lt $Count; $index++) {
        $stage = 'read snapshot ' + ($index + 1)
        if ($index) { Start-Sleep -Seconds $IntervalSeconds }
        $sampleTime = if ($index) { [DateTime]::UtcNow.ToString('o') } else { $firstTime }
        [byte[]] $battery = if ($index) { Read-Gmk70TrialBlock -Address 0x11003cfe -Length 32 } else { $first }
        [byte[]] $timers = if ($index) { Read-Gmk70TrialBlock -Address 0x11004ddc -Length 40 } else { $firstTimers }
        $timerTime = if ($index) { [DateTime]::UtcNow.ToString('o') } else { $firstTimerTime }
        [byte[]] $mode = Read-Gmk70TrialBlock -Address 0x11004e04 -Length 16
        [byte[]] $connection = Read-Gmk70TrialBlock -Address 0x11004d80 -Length 16
        $sample = ConvertFrom-Gmk70BatterySnapshot -Battery $battery -Timers $timers -Mode $mode -Connection $connection
        $sample['battery_read_started_utc'] = $sampleTime
        $sample['timer_read_finished_utc'] = $timerTime
        $sample['other_fields_read_finished_utc'] = [DateTime]::UtcNow.ToString('o')
        $report.samples += $sample
        Write-Host ('Sample {0}/{1}: percent {2}; filter {3}/360; sampler {4}; charger {5}' -f
            ($index + 1), $Count, $sample.radio_percent, $sample.downward_filter_count, $sample.sampler.state, $sample.charging_state)
    }
    $report.status = 'complete'
    $failed = $false
} catch {
    $report.status = 'failed'
    # Do not include WinRT device paths, addresses, raw RAM or exception dumps.
    $report['error_type'] = $_.Exception.GetType().FullName
    $report['error_stage'] = $stage
    Write-Host 'Capture stopped. The partial JSON retains any decoded samples; no retry was issued.'
} finally {
    $report.requests = @($script:trialSent.ToArray())
    Close-Gmk70TrialRadio
    $report['finished_utc'] = [DateTime]::UtcNow.ToString('o')
    $json = ConvertTo-Json -InputObject $report -Depth 8
    $file = [IO.File]::Open($OutputPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try {
        $encoded = [Text.UTF8Encoding]::new($false).GetBytes($json)
        $file.Write($encoded, 0, $encoded.Length)
    } finally { $file.Dispose() }
    Write-Host ('Saved: {0}' -f $OutputPath)
}
if ($failed) { exit 1 }
