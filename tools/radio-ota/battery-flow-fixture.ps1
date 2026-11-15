# SPDX-License-Identifier: GPL-2.0-or-later
# Offline acquisition-flow fixture. Every device boundary is replaced before execution.
#requires -Version 5.1
param([Parameter(Mandatory = $true)] [string] $OutputPath, [switch] $FailSecondSnapshot)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'radio-core.ps1')
. (Join-Path $PSScriptRoot 'ram-trial-core.ps1')
. (Join-Path $PSScriptRoot 'battery-core.ps1')
$source = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'read-battery.ps1'))
$imports = @($source -split '\r?\n' | Where-Object { $_ -match '^\. ' })
if ($imports.Length -ne 2) { throw 'Acquisition imports changed; refusing to run the fixture.' }
foreach ($name in @('ram-transport.ps1', 'battery-core.ps1')) {
    $line = ". (Join-Path `$PSScriptRoot '$name')"
    if ($imports -notcontains $line) { throw 'Unexpected acquisition import.' }
    $source = $source.Replace($line, '')
}
$script:opened = $false
$script:batteryReads = 0
$script:guard = $null
$script:trialSent = [Collections.Generic.List[object]]::new()
function Open-Gmk70TrialRadio {
    param([string] $BluetoothAddress, [switch] $ReadOnly, [scriptblock] $ReadGuard)
    if (-not $ReadOnly -or $null -eq $ReadGuard -or $BluetoothAddress -ne '') { throw 'Collector did not request bounded read-only discovery.' }
    $script:opened = $true
    $script:guard = $ReadGuard
}
function Read-Gmk70TrialBlock {
    param([uint32] $Address, [int] $Length, [switch] $Storage)
    if (-not $script:opened) { throw 'Read before read-only open.' }
    $packet = if ($Storage) { New-Gmk70ReadPacket -Offset $Address -Length $Length -RegionLength 0x80000 } else { New-Gmk70RamReadPacket -Address $Address -Length $Length }
    & $script:guard $packet
    $script:trialSent.Add(@{ opcode = [int] $packet[0]; address = $Address; length = $Length })
    $bytes = [byte[]]::new($Length)
    switch ($Address) {
        0x11003cfe {
            $script:batteryReads++
            if ($FailSecondSnapshot -and $script:batteryReads -eq 2) { throw [TimeoutException]::new('Synthetic timeout; never retry.') }
            $bytes[0] = 1; $bytes[1] = 1; $bytes[2] = 60; $bytes[7] = 1; $bytes[9] = 60
            [BitConverter]::GetBytes([uint16] (120 + $script:batteryReads)).CopyTo($bytes, 4)
        }
        0x11004ddc {
            [BitConverter]::GetBytes([uint32] 0x10011ec5).CopyTo($bytes, 8)
            [BitConverter]::GetBytes([uint16] 255).CopyTo($bytes, 16)
            [BitConverter]::GetBytes([uint32] 1000).CopyTo($bytes, 24)
            [BitConverter]::GetBytes([uint32] 0x10011f09).CopyTo($bytes, 28)
            [BitConverter]::GetBytes([uint16] 0xf02).CopyTo($bytes, 36)
        }
        0x11004e04 { $bytes[4] = 2 }
        0x11004d80 { $bytes[0] = 3 }
    }
    return ,$bytes
}
function Assert-Gmk70BatteryCode { param([byte[]] $Bytes, [string] $Expected) }
function Close-Gmk70TrialRadio {
    $script:opened = $false
    [IO.File]::WriteAllText(($OutputPath + '.closed'), 'closed')
}
function Start-Sleep([int] $Seconds) {
    if ($Seconds -ne 12) { throw 'Unexpected acquisition interval.' }
}
# Only the platform check is simulated. The real acquisition flow, decoding,
# fixed request guard, JSON output and failure/finally paths execute below.
$edition = $PSVersionTable.PSEdition
try {
    $PSVersionTable.PSEdition = 'Desktop'
    & ([scriptblock]::Create($source)) -OutputPath $OutputPath -Count 3 -IntervalSeconds 12
} finally { $PSVersionTable.PSEdition = $edition }
