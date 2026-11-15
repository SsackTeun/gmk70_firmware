# Offline checks only: no Bluetooth discovery or device commands.
#requires -Version 5.1
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ram-transport.ps1')

function Assert-Equal([string] $Actual, [string] $Expected, [string] $Label) {
    if ($Actual -ne $Expected) { throw "$Label expected '$Expected', received '$Actual'." }
}

function Assert-NameRejected {
    param([string[]] $Names, [string] $MessagePrefix)
    try { [void] (Find-Gmk70RadioNameIndex -Names $Names) } catch {
        if ($_.Exception.Message.StartsWith($MessagePrefix)) { return }
        throw
    }
    throw 'Invalid name selection was accepted.'
}

Assert-NameRejected -Names @() -MessagePrefix 'No connected GMK70'
Assert-NameRejected -Names @('Other keyboard', 'GMK70-6', 'GMK70-2 extra', "GMK70-2`n") -MessagePrefix 'No connected GMK70'
Assert-NameRejected -Names @('GMK70-1', 'GMK70-5') -MessagePrefix 'Multiple connected GMK70'
Assert-NameRejected -Names @('GMK70-2', 'GMK70-2') -MessagePrefix 'Multiple connected GMK70'
Assert-Equal -Actual (Find-Gmk70RadioNameIndex -Names @('GMK70-2')) -Expected 0 -Label 'Single connected name'
Assert-Equal -Actual (Find-Gmk70RadioNameIndex -Names @('Other keyboard', 'GMK70-5')) -Expected 1 -Label 'Mixed device names'
Write-Information 'PASS: empty, single, mixed, duplicate and ambiguous GMK70 discovery names are handled explicitly.' -InformationAction Continue

# All values in this test are synthetic, including the address and paths.
[string[]] $privateText = @('00:11:22:33:44:55', '001122334455', '123456789012345',
    'BluetoothLE#synthetic-id', 'HID\synthetic-id', '\\?\synthetic-id',
    'C:\synthetic-user\private.txt', '\\synthetic-host\private\file', '/home/synthetic-user/private')
foreach ($text in $privateText) {
    [string] $sanitized = Protect-Gmk70RadioErrorText -Text ('Failure "' + $text + '"')
    Assert-Equal -Actual ($sanitized.Contains($text)) -Expected $false -Label 'Sensitive exception text redacted'
}
[Management.Automation.ErrorRecord] $failure = [Management.Automation.ErrorRecord]::new(
    [InvalidOperationException]::new('Outer failure', [TimeoutException]::new('Inner failure')),
    'SyntheticError', [Management.Automation.ErrorCategory]::ConnectionError, $null)
[Gmk70RadioFailureDetail] $details = Get-Gmk70RadioFailureDetail -Failure $failure
Assert-Equal -Actual $details.id -Expected 'SyntheticError' -Label 'Error identifier'
Assert-Equal -Actual $details.category -Expected 'ConnectionError' -Label 'Error category'
Assert-Equal -Actual $details.exceptions.Length -Expected 2 -Label 'Inner exception retained'
Assert-Equal -Actual $details.exceptions[1].hresult -Expected '0x80131505' -Label 'Inner HRESULT'
Assert-Equal -Actual $details.exceptions[1].message -Expected 'Inner failure' -Label 'Inner message'
Assert-Equal -Actual (Protect-Gmk70RadioErrorText -Text ('x' * 2000)).Length -Expected 1500 -Label 'Bounded message length'
Write-Information 'PASS: sanitized structured exceptions retain useful identifiers and inner HRESULTs.' -InformationAction Continue

# Collection cardinality must survive PowerShell's pipeline unrolling.
foreach ($count in @(0, 1, 2)) {
    $values = [Collections.Generic.List[string]]::new()
    for ($i = 0; $i -lt $count; $i++) { $values.Add('service-' + $i) }
    $items = ConvertTo-Gmk70WinRtArray -Value $values.AsReadOnly() -ItemType ([string])
    Assert-Equal $items.Length $count 'Projected collection length'
    for ($i = 0; $i -lt $count; $i++) {
        Assert-Equal $items[$i] ('service-' + $i) 'Projected collection item'
    }
}
Write-Host 'PASS: empty, single and multiple collection items retain array shape.'

if ($PSVersionTable.PSEdition -eq 'Desktop') {
    # Load the real transport types/bridge, without enumerating or opening Bluetooth.
    Initialize-Gmk70TrialWinRt
    [Windows.Storage.StorageFile, Windows.Storage, ContentType = WindowsRuntime] | Out-Null
    [string] $temporary = [IO.Path]::GetTempFileName()
    try {
        $file = Wait-Gmk70TrialOperation -Start {
            [Windows.Storage.StorageFile]::GetFileFromPathAsync($temporary)
        } -ResultType ([Windows.Storage.StorageFile]) -Label 'offline native file operation'
        Assert-Equal -Actual $file.Name -Expected ([IO.Path]::GetFileName($temporary)) -Label 'Actual WinRT async result'
    }
    finally { [IO.File]::Delete($temporary) }
    Write-Information 'PASS: transport WinRT type loading and async bridge complete an actual offline operation.' -InformationAction Continue
    [Windows.System.UserProfile.GlobalizationPreferences, Windows.System.UserProfile, ContentType = WindowsRuntime] | Out-Null
    $languages = ConvertTo-Gmk70WinRtArray -Value ([Windows.System.UserProfile.GlobalizationPreferences]::Languages) -ItemType ([string])
    if ($languages.Length -lt 1 -or [string]::IsNullOrEmpty($languages[0])) {
        throw 'A native WinRT vector must expose its items through the array adapter.'
    }
    Write-Host 'PASS: actual native WinRT COM vector supports counting and indexing.'

    [Windows.Security.Cryptography.CryptographicBuffer, Windows.Security.Cryptography, ContentType = WindowsRuntime] | Out-Null
    [Windows.Storage.Streams.DataReader, Windows.Storage.Streams, ContentType = WindowsRuntime] | Out-Null
    $buffer = [Windows.Security.Cryptography.CryptographicBuffer]::DecodeFromHexString('010203ff')
    $bytes = ConvertFrom-Gmk70WinRtBuffer -Buffer $buffer
    Assert-Equal ([BitConverter]::ToString($bytes)) '01-02-03-FF' 'Native buffer bytes'
    $empty = [Windows.Security.Cryptography.CryptographicBuffer]::CreateFromByteArray([byte[]]::new(0))
    Assert-Equal (ConvertFrom-Gmk70WinRtBuffer -Buffer $empty).Length 0 'Empty native buffer'
    Assert-Equal (ConvertFrom-Gmk70WinRtBuffer -Buffer $null).Length 0 'Absent native buffer'
    Write-Host 'PASS: actual native WinRT buffers retain exact bytes and empty array shape.'
}
