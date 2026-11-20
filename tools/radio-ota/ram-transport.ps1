# Explicit physical-device transport. Importing it does not connect or send.
#requires -Version 5.1
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'radio-core.ps1')
. (Join-Path $PSScriptRoot 'ram-trial-core.ps1')
$script:trialDevice = $null
$script:trialService = $null
$script:trialCommand = $null
$script:trialData = $null
$script:trialTaskBridge = $null
$script:trialWriteRanges = @()
$script:trialReadOnly = $false
$script:trialReadGuard = $null
$script:trialSent = [Collections.Generic.List[object]]::new()
[string] $script:trialConnectStage = 'not-started'

function Wait-Gmk70TrialOperation {
    param([scriptblock] $Start, [type] $ResultType, [string] $Label)
    $operation = & $Start
    $task = $script:trialTaskBridge.MakeGenericMethod($ResultType).Invoke($null, @($operation))
    if (-not $task.Wait(20000)) {
        try { $operation.Cancel() } catch { }
        throw [TimeoutException]::new("Timed out during $Label; no retry or additional write is permitted.")
    }
    return , $task.Result
}

function Assert-Gmk70TrialGatt([object] $Result, [string] $Label) {
    if ($Result.Status.ToString() -ne 'Success') { throw "$Label returned $($Result.Status)." }
}

function Open-Gmk70TrialRadio {
    param([string] $BluetoothAddress = '', [switch] $ReadOnly,
        [scriptblock] $ReadGuard = $null)
    $script:trialConnectStage = 'host-check'
    if ($PSVersionTable.PSEdition -ne 'Desktop') { throw 'Use Windows PowerShell 5.1.' }
    if (-not $ReadOnly) { throw 'This archived trial has no public device binding.' }
    $script:trialReadOnly = [bool] $ReadOnly
    $script:trialReadGuard = $ReadGuard
    $script:trialWriteRanges = @()
    $script:trialCommand = $null
    $script:trialData = $null
    Initialize-Gmk70TrialWinRt
    if ($ReadOnly -and $BluetoothAddress -eq '') {
        $script:trialConnectStage = 'discovery-type-loading'
        [Windows.Devices.Enumeration.DeviceInformation, Windows.Devices.Enumeration, ContentType = WindowsRuntime] | Out-Null
        [Windows.Devices.Enumeration.DeviceInformationCollection, Windows.Devices.Enumeration, ContentType = WindowsRuntime] | Out-Null
        [Windows.Devices.Bluetooth.BluetoothConnectionStatus, Windows.Devices.Bluetooth, ContentType = WindowsRuntime] | Out-Null
        $script:trialConnectStage = 'discovery-selector'
        $selector = [Windows.Devices.Bluetooth.BluetoothLEDevice]::GetDeviceSelectorFromConnectionStatus(
            [Windows.Devices.Bluetooth.BluetoothConnectionStatus]::Connected)
        $script:trialConnectStage = 'discovery-query'
        $devices = Wait-Gmk70TrialOperation -Start {
            [Windows.Devices.Enumeration.DeviceInformation]::FindAllAsync($selector)
        } -ResultType ([Windows.Devices.Enumeration.DeviceInformationCollection]) -Label 'find connected keyboard'
        $script:trialConnectStage = 'discovery-collection-conversion'
        $devices = ConvertTo-Gmk70WinRtArray -Value $devices -ItemType ([Windows.Devices.Enumeration.DeviceInformation])
        $script:trialConnectStage = 'discovery-device-names'
        [string[]] $names = [string[]]::new($devices.Length)
        # The adapter deliberately preserves an array as one pipeline value.
        # Enumerate its elements explicitly, including zero/one/many results.
        for ([int] $index = 0; $index -lt $devices.Length; $index++) { $names[$index] = $devices[$index].Name }
        $script:trialConnectStage = 'discovery-name-selection'
        [int] $candidate = Find-Gmk70RadioNameIndex -Names $names
        $script:trialConnectStage = 'device-open-id'
        $script:trialDevice = Wait-Gmk70TrialOperation -Start {
            [Windows.Devices.Bluetooth.BluetoothLEDevice]::FromIdAsync($devices[$candidate].Id)
        } -ResultType ([Windows.Devices.Bluetooth.BluetoothLEDevice]) -Label 'open connected radio'
    }
    else {
        $script:trialConnectStage = 'address-conversion'
        $address = [Convert]::ToUInt64($BluetoothAddress.Replace(':', ''), 16)
        $script:trialConnectStage = 'device-open-address'
        $script:trialDevice = Wait-Gmk70TrialOperation -Start {
            [Windows.Devices.Bluetooth.BluetoothLEDevice]::FromBluetoothAddressAsync($address)
        } -ResultType ([Windows.Devices.Bluetooth.BluetoothLEDevice]) -Label 'open radio'
    }
    $script:trialConnectStage = 'device-validation'
    if ($null -eq $script:trialDevice -or
        (-not $ReadOnly -and $script:trialDevice.Name -ne 'GMK70-2') -or
        ($ReadOnly -and $script:trialDevice.Name -notmatch '\AGMK70-[1-5]\z')) { throw 'Expected the GMK70 keyboard.' }
    Write-Host ('GMK70 Bluetooth connection: {0}' -f $script:trialDevice.ConnectionStatus)
    $uncached = [Windows.Devices.Bluetooth.BluetoothCacheMode]::Uncached
    $uuid = [Guid] '02f00000-0000-0000-0000-00000000fe00'
    $script:trialConnectStage = 'service-query'
    $result = Wait-Gmk70TrialOperation -Start { $script:trialDevice.GetGattServicesForUuidAsync($uuid, $uncached) } `
        -ResultType ([Windows.Devices.Bluetooth.GenericAttributeProfile.GattDeviceServicesResult]) -Label 'discover OTA service'
    Assert-Gmk70TrialGatt $result 'Service discovery'
    $script:trialConnectStage = 'service-collection-conversion'
    $services = ConvertTo-Gmk70WinRtArray -Value $result.Services `
        -ItemType ([Windows.Devices.Bluetooth.GenericAttributeProfile.GattDeviceService])
    if ($services.Length -ne 1) { throw 'Expected exactly one OTA service.' }
    $script:trialService = $services[0]
    $script:trialConnectStage = 'service-access'
    $access = Wait-Gmk70TrialOperation -Start { $script:trialService.RequestAccessAsync() } `
        -ResultType ([Windows.Devices.Enumeration.DeviceAccessStatus]) -Label 'service access'
    if ($access.ToString() -ne 'Allowed') { throw 'OTA service access denied.' }
    $script:trialConnectStage = 'characteristic-query'
    $result = Wait-Gmk70TrialOperation -Start { $script:trialService.GetCharacteristicsAsync($uncached) } `
        -ResultType ([Windows.Devices.Bluetooth.GenericAttributeProfile.GattCharacteristicsResult]) -Label 'characteristics'
    Assert-Gmk70TrialGatt $result 'Characteristic discovery'
    $script:trialConnectStage = 'characteristic-collection-conversion'
    $characters = ConvertTo-Gmk70WinRtArray -Value $result.Characteristics `
        -ItemType ([Windows.Devices.Bluetooth.GenericAttributeProfile.GattCharacteristic])
    $script:trialConnectStage = 'characteristic-selection'
    foreach ($char in $characters) {
        if ($char.Uuid -eq [Guid] '02f00000-0000-0000-0000-00000000ff01') { $script:trialCommand = $char }
        if ($char.Uuid -eq [Guid] '02f00000-0000-0000-0000-00000000ff00') { $script:trialData = $char }
    }
    if ($null -eq $script:trialCommand -or $null -eq $script:trialData) { throw 'Missing OTA characteristics.' }
    $script:trialConnectStage = 'ready'
}

function Initialize-Gmk70TrialWinRt {
    $script:trialConnectStage = 'type-loading'
    Add-Type -AssemblyName System.Runtime.WindowsRuntime
    [Windows.Devices.Bluetooth.BluetoothLEDevice, Windows.Devices.Bluetooth, ContentType = WindowsRuntime] | Out-Null
    [Windows.Devices.Bluetooth.BluetoothCacheMode, Windows.Devices.Bluetooth, ContentType = WindowsRuntime] | Out-Null
    [Windows.Devices.Enumeration.DeviceAccessStatus, Windows.Devices.Enumeration, ContentType = WindowsRuntime] | Out-Null
    [Windows.Devices.Bluetooth.GenericAttributeProfile.GattDeviceServicesResult, Windows.Devices.Bluetooth, ContentType = WindowsRuntime] | Out-Null
    [Windows.Devices.Bluetooth.GenericAttributeProfile.GattDeviceService, Windows.Devices.Bluetooth, ContentType = WindowsRuntime] | Out-Null
    [Windows.Devices.Bluetooth.GenericAttributeProfile.GattCharacteristicsResult, Windows.Devices.Bluetooth, ContentType = WindowsRuntime] | Out-Null
    [Windows.Devices.Bluetooth.GenericAttributeProfile.GattCharacteristic, Windows.Devices.Bluetooth, ContentType = WindowsRuntime] | Out-Null
    [Windows.Devices.Bluetooth.GenericAttributeProfile.GattWriteResult, Windows.Devices.Bluetooth, ContentType = WindowsRuntime] | Out-Null
    [Windows.Devices.Bluetooth.GenericAttributeProfile.GattReadResult, Windows.Devices.Bluetooth, ContentType = WindowsRuntime] | Out-Null
    [Windows.Devices.Bluetooth.GenericAttributeProfile.GattWriteOption, Windows.Devices.Bluetooth, ContentType = WindowsRuntime] | Out-Null
    [Windows.Storage.Streams.DataWriter, Windows.Storage.Streams, ContentType = WindowsRuntime] | Out-Null
    [Windows.Storage.Streams.DataReader, Windows.Storage.Streams, ContentType = WindowsRuntime] | Out-Null
    [Windows.Storage.Streams.IBuffer, Windows.Storage.Streams, ContentType = WindowsRuntime] | Out-Null
    $script:trialConnectStage = 'async-bridge'
    $methods = @([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
            $_.Name -eq 'AsTask' -and $_.IsGenericMethodDefinition -and
            $_.GetGenericArguments().Length -eq 1 -and $_.GetParameters().Length -eq 1 -and
            $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'
        })
    if ($methods.Length -ne 1) { throw 'Cannot find the WinRT task bridge.' }
    $script:trialTaskBridge = $methods[0]
}

function Close-Gmk70TrialRadio {
    $script:trialWriteRanges = @()
    if ($null -ne $script:trialService) { try { $script:trialService.Dispose() } catch { } }
    if ($null -ne $script:trialDevice) { try { $script:trialDevice.Dispose() } catch { } }
    $script:trialReadOnly = $false
    $script:trialReadGuard = $null
}

function Send-Gmk70TrialPacket([byte[]] $Packet) {
    # This check runs at the last boundary before any GATT write.
    if ($script:trialReadOnly -and ($null -eq $Packet -or $Packet.Length -lt 1 -or $Packet[0] -notin @(6, 8))) {
        throw 'Read-only radio access permits only storage/memory read requests.'
    }
    Assert-Gmk70TrialPacket -Packet $Packet -WriteRanges $script:trialWriteRanges
    if ($script:trialReadOnly -and $null -ne $script:trialReadGuard) { & $script:trialReadGuard $Packet }
    $writer = [Windows.Storage.Streams.DataWriter]::new()
    try {
        $writer.WriteBytes($Packet)
        $buffer = $writer.DetachBuffer()
        $method = [Windows.Devices.Bluetooth.GenericAttributeProfile.GattCharacteristic].GetMethod(
            'WriteValueWithResultAsync', [type[]] @([Windows.Storage.Streams.IBuffer],
                [Windows.Devices.Bluetooth.GenericAttributeProfile.GattWriteOption]))
        $arguments = [object[]] @($buffer,
            [Windows.Devices.Bluetooth.GenericAttributeProfile.GattWriteOption]::WriteWithResponse)
        $script:trialSent.Add(@{ opcode = [int] $Packet[0]; address = [BitConverter]::ToUInt32($Packet, 3);
            length = [int] [BitConverter]::ToUInt16($Packet, 7) })
        $result = Wait-Gmk70TrialOperation { $method.Invoke($script:trialCommand, $arguments) } `
            ([Windows.Devices.Bluetooth.GenericAttributeProfile.GattWriteResult]) 'bounded trial request'
        Assert-Gmk70TrialGatt $result 'Trial request'
    }
    finally { $writer.Dispose() }
}

function Read-Gmk70TrialBlock {
    param([uint32] $Address, [int] $Length, [switch] $Storage)
    $packet = if ($Storage) {
        New-Gmk70ReadPacket -Offset $Address -Length $Length -RegionLength 0x80000
    } else { New-Gmk70RamReadPacket -Address $Address -Length $Length }
    Send-Gmk70TrialPacket $packet
    $result = Wait-Gmk70TrialOperation {
        $script:trialData.ReadValueAsync([Windows.Devices.Bluetooth.BluetoothCacheMode]::Uncached)
    } ([Windows.Devices.Bluetooth.GenericAttributeProfile.GattReadResult]) 'consume trial response'
    Assert-Gmk70TrialGatt $result 'Trial read'
    [byte[]] $bytes = ConvertFrom-Gmk70WinRtBuffer -Buffer $result.Value
    if ($bytes.Length -ne $Length) { throw "Expected $Length bytes; received $($bytes.Length)." }
    return ,$bytes
}

function Read-Gmk70TrialRam {
    param([uint32] $Address, [int] $Length)
    Assert-Gmk70RamRange $Address $Length
    $bytes = [byte[]]::new($Length)
    for ($offset = 0; $offset -lt $Length; $offset += 128) {
        $count = [Math]::Min(128, $Length - $offset)
        [byte[]] $part = Read-Gmk70TrialBlock -Address ($Address + $offset) -Length ([Math]::Max(16, $count))
        [Array]::Copy($part, 0, $bytes, $offset, $count)
    }
    return ,$bytes
}
