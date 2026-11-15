# SPDX-License-Identifier: GPL-2.0-or-later
# Pure decoding and read allowlist. Loading this file does not access a device.
#requires -Version 5.1
Set-StrictMode -Version Latest

function Get-Gmk70BatteryCodePlan {
    # Exact bounded code fingerprints from the captured stock radio application.
    @(
        @{ storage = $true; address = [uint32] 0x70d4; length = 32; sha256 = 'b2e5ea5d7bf94374b725cf651cf118d5433478025f59ee61ef8999531f0e38b7' }
        @{ storage = $true; address = [uint32] 0x171d8; length = 32; sha256 = 'fb81b3e9e3784b06f0e9e372f2f5f7f3aa1bb89b15f99bd2941c9e4b31b68896' }
        @{ storage = $false; address = [uint32] 0x110017ee; length = 32; sha256 = '49420582bd402cf868cef22cba623774e50bc82fd99c071f4bc95b6678215182' }
        @{ storage = $false; address = [uint32] 0x1100184e; length = 32; sha256 = '326d751ed39c0af6da62999b6024d9ff92bdcd08e0d845f4564662bf7526dbfa' }
    )
}

function Get-Gmk70BatteryReadPlan {
    @(
        @{ storage = $false; address = [uint32] 0x11003cfe; length = 32 } # Battery, idle counter, radio power phase.
        @{ storage = $false; address = [uint32] 0x11004ddc; length = 40 } # ADC-read and periodic sampler timer objects.
        @{ storage = $false; address = [uint32] 0x11004e04; length = 16 } # Decode only selected output; discard address bytes.
        @{ storage = $false; address = [uint32] 0x11004d80; length = 16 } # Decode only connection-state byte.
    )
}

function Assert-Gmk70BatteryReadPacket([byte[]] $Packet) {
    if ($null -eq $Packet -or $Packet.Length -ne 9 -or $Packet[1] -ne 6 -or $Packet[2] -ne 0 -or $Packet[0] -notin @(6, 8)) {
        throw 'Battery capture permits only its fixed read requests.'
    }
    $address = [BitConverter]::ToUInt32($Packet, 3)
    $length = [BitConverter]::ToUInt16($Packet, 7)
    foreach ($entry in @((Get-Gmk70BatteryCodePlan)) + @((Get-Gmk70BatteryReadPlan))) {
        $opcode = if ($entry.storage) { 6 } else { 8 }
        if ($Packet[0] -eq $opcode -and $address -eq $entry.address -and $length -eq $entry.length) { return }
    }
    throw 'Battery capture rejected a read outside its code/sensor allowlist.'
}

function Assert-Gmk70BatteryCode([byte[]] $Bytes, [string] $Expected) {
    if ($Bytes.Length -ne 32) { throw 'Truncated radio code fingerprint.' }
    $hash = [Security.Cryptography.SHA256]::Create()
    try { $actual = [BitConverter]::ToString($hash.ComputeHash($Bytes)).Replace('-', '').ToLowerInvariant() }
    finally { $hash.Dispose() }
    if ($actual -ne $Expected) { throw 'Radio code differs from the captured version; refusing to interpret its RAM.' }
}

function ConvertFrom-Gmk70BatteryTimer([byte[]] $Bytes, [uint32] $Callback) {
    if ($Bytes.Length -ne 20) { throw 'Truncated battery timer.' }
    $function = [BitConverter]::ToUInt32($Bytes, 8)
    $id = [BitConverter]::ToUInt16($Bytes, 16)
    $period = 10 * [uint64] [BitConverter]::ToUInt32($Bytes, 4)
    if ($function -eq 0) { $state = 'uninitialized' }
    elseif ($function -ne $Callback) { throw 'Unexpected battery timer callback; cannot interpret this radio state.' }
    elseif ($id -eq 255) { $state = if ($period) { 'expired-or-pending' } else { 'unarmed' } }
    elseif ($id -ge 0xf02 -and $id -le 0x1000) { $state = 'armed' }
    else { throw 'Unexpected battery timer ID.' }
    return [ordered]@{ state = $state; period_ms = $period }
}

function ConvertFrom-Gmk70BatterySnapshot {
    param([byte[]] $Battery, [byte[]] $Timers, [byte[]] $Mode, [byte[]] $Connection)
    if ($Battery.Length -ne 32 -or $Timers.Length -ne 40 -or $Mode.Length -ne 16 -or $Connection.Length -ne 16) {
        throw 'Incomplete battery snapshot.'
    }
    $counter = [BitConverter]::ToUInt16($Battery, 4)
    if ($Battery[0] -gt 1 -or $Battery[1] -gt 1 -or $Battery[2] -gt 100 -or
        $Battery[6] -gt 1 -or $Battery[7] -gt 1 -or $Battery[8] -gt 4 -or $Battery[9] -gt 100 -or
        $Battery[22] -gt 2 -or $counter -gt 360 -or $Mode[4] -gt 6) { throw 'Unexpected battery-state values.' }
    $charging = switch ($Battery[8]) { 0 { 'discharging' } 1 { 'discharging-1' } 3 { 'charging' } 4 { 'full' } default { 'unknown' } }
    $output = if ($Mode[4] -eq 0) { 'dongle' } elseif ($Mode[4] -eq 6) { 'usb' } else { 'bluetooth' }
    return [ordered]@{
        radio_percent = [int] $Battery[9]
        retained_percent = [int] $Battery[2]
        retained_valid = [bool] $Battery[1]
        downward_filter_count = [int] $counter
        charging_state = $charging
        external_percent_override = [bool] $Battery[6]
        bas_enabled = [bool] $Battery[7]
        sampler_initialized = [bool] $Battery[0]
        sampler = ConvertFrom-Gmk70BatteryTimer -Bytes $Timers[20..39] -Callback 0x10011f09
        adc_read = ConvertFrom-Gmk70BatteryTimer -Bytes $Timers[0..19] -Callback 0x10011ec5
        radio_power_phase = [int] $Battery[22]
        uart_idle_count = [int] [BitConverter]::ToUInt16($Battery, 16)
        output = $output
        profile = [int] $Mode[4]
        connection_state = [int] $Connection[0]
    }
}
