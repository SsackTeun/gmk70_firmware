# SPDX-License-Identifier: GPL-2.0-or-later
# Hardware-free tests; PowerShell 7 on Linux or Windows PowerShell 5.1.
#requires -Version 5.1
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ram-transport.ps1')
. (Join-Path $PSScriptRoot 'battery-core.ps1')
function Assert-Equal($Actual, $Expected, [string] $Label) {
    if ($Actual -ne $Expected) { throw "$Label expected '$Expected', received '$Actual'." }
}
function Assert-Rejected([scriptblock] $Action, [string] $Label) {
    $rejected = $false
    try { & $Action | Out-Null } catch { $rejected = $true }
    if (-not $rejected) { throw "$Label was accepted." }
}
function Packet($Entry) {
    if ($Entry.storage) { return ,(New-Gmk70ReadPacket -Offset $Entry.address -Length $Entry.length -RegionLength 0x80000) }
    return ,(New-Gmk70RamReadPacket -Address $Entry.address -Length $Entry.length)
}
foreach ($entry in @((Get-Gmk70BatteryCodePlan)) + @((Get-Gmk70BatteryReadPlan))) {
    [byte[]] $packet = Packet $entry
    Assert-Gmk70BatteryReadPacket $packet
    Assert-Gmk70TrialPacket $packet
    $changed = [byte[]] $packet.Clone()
    $changed[3] = $changed[3] -bxor 1
    Assert-Rejected { Assert-Gmk70BatteryReadPacket $changed } 'Adjacent memory'
    $changed = [byte[]] $packet.Clone()
    $changed[7]++
    Assert-Rejected { Assert-Gmk70BatteryReadPacket $changed } 'Extra read byte'
}
for ($opcode = 0; $opcode -le 255; $opcode++) {
    if ($opcode -in @(6, 8)) { continue }
    $packet = [byte[]] (8, 6, 0, 0xfe, 0x3c, 0, 0x11, 32, 0)
    $packet[0] = $opcode
    Assert-Rejected { Assert-Gmk70BatteryReadPacket $packet } 'Non-read opcode'
}
$script:trialReadOnly = $true
$script:trialWriteRanges = @(@{ start = [uint32] 0x11001af4; length = 4 })
$write = New-Gmk70RamWritePacket -Address 0x11001af4 -Bytes ([byte[]] (0, 0x20, 0x70, 0x47))
Assert-Rejected { Send-Gmk70TrialPacket $write } 'Read-only transport with a live RAM write scope'
$script:trialReadOnly = $false
$script:trialWriteRanges = @()
Assert-Rejected { Send-Gmk70TrialPacket $write } 'Ordinary trial without a write scope'
Write-Host 'PASS: fixed battery/code read allowlist and transport reject writes and adjacent/oversized reads.'

$battery = [byte[]]::new(32)
$battery[0] = 1; $battery[1] = 1; $battery[2] = 60; $battery[7] = 1; $battery[8] = 0; $battery[9] = 60
[BitConverter]::GetBytes([uint16] 120).CopyTo($battery, 4)
[BitConverter]::GetBytes([uint16] 4).CopyTo($battery, 16)
$timers = [byte[]]::new(40)
[BitConverter]::GetBytes([uint32] 0x10011ec5).CopyTo($timers, 8)
[BitConverter]::GetBytes([uint16] 255).CopyTo($timers, 16)
[BitConverter]::GetBytes([uint32] 1000).CopyTo($timers, 24)
[BitConverter]::GetBytes([uint32] 0x10011f09).CopyTo($timers, 28)
[BitConverter]::GetBytes([uint16] 0xf02).CopyTo($timers, 36)
$mode = [byte[]]::new(16); $mode[4] = 2
$connection = [byte[]]::new(16); $connection[0] = 3
$sample = ConvertFrom-Gmk70BatterySnapshot $battery $timers $mode $connection
Assert-Equal $sample.radio_percent 60 'Returned percentage'
Assert-Equal $sample.retained_percent 60 'Filter-held percentage'
Assert-Equal $sample.downward_filter_count 120 'Downward counter'
Assert-Equal $sample.sampler.state 'armed' 'Sampler'
Assert-Equal $sample.sampler.period_ms 10000 'Native 10-ms timer units'
Assert-Equal $sample.adc_read.state 'unarmed' 'ADC conversion-read timer'
Assert-Equal $sample.charging_state 'discharging' 'Charging latch'
Assert-Equal $sample.output 'bluetooth' 'Output'
Assert-Equal $sample.profile 2 'Profile'
Assert-Equal $sample.connection_state 3 'Connection'
Assert-Equal $sample.uart_idle_count 4 'Idle counter'
Assert-Equal $sample.external_percent_override $false 'External override'
$before = ConvertTo-Json $sample -Depth 5 -Compress
# Unused bytes may contain addresses/pointers. They must never enter the report.
foreach ($offset in @(0, 1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15)) {
    if ($offset -ne 4) { $mode[$offset] = 238 }
    if ($offset -ne 0) { $connection[$offset] = 238 }
}
foreach ($offset in @(3, 10, 11, 12, 13, 14, 15, 18, 19, 20, 21, 23, 24, 25, 26, 27, 28, 29, 30, 31)) { $battery[$offset] = 238 }
$after = ConvertTo-Json (ConvertFrom-Gmk70BatterySnapshot $battery $timers $mode $connection) -Depth 5 -Compress
Assert-Equal $after $before 'No raw RAM/addresses in decoded output'
[BitConverter]::GetBytes([uint16] 255).CopyTo($timers, 36)
Assert-Equal (ConvertFrom-Gmk70BatterySnapshot $battery $timers $mode $connection).sampler.state 'expired-or-pending' 'Periodic timer awaiting dispatch'
[BitConverter]::GetBytes([uint32] 0).CopyTo($timers, 24)
Assert-Equal (ConvertFrom-Gmk70BatterySnapshot $battery $timers $mode $connection).sampler.state 'unarmed' 'Unarmed sampler'
$battery[6] = 1; $battery[8] = 3
$sample = ConvertFrom-Gmk70BatterySnapshot $battery $timers $mode $connection
Assert-Equal $sample.external_percent_override $true 'Externally supplied percentage latch'
Assert-Equal $sample.charging_state 'charging' 'Charging command latch'
$battery[8] = 4
Assert-Equal (ConvertFrom-Gmk70BatterySnapshot $battery $timers $mode $connection).charging_state 'full' 'Full command latch'
[BitConverter]::GetBytes([uint16] 361).CopyTo($battery, 4)
Assert-Rejected { ConvertFrom-Gmk70BatterySnapshot $battery $timers $mode $connection } 'Invalid filter count'
[BitConverter]::GetBytes([uint16] 120).CopyTo($battery, 4)
$timers[28] = 0
Assert-Rejected { ConvertFrom-Gmk70BatterySnapshot $battery $timers $mode $connection } 'Foreign timer callback'
Write-Host 'PASS: held percentage, sampler/ADC timer states, charging/override latches, and private-byte exclusion.'

$known = [byte[]] (0..31)
Assert-Gmk70BatteryCode $known '630dcd2966c4336691125448bbb25b4ff412a49c732db2c8abc1b8581bd710dd'
$known[0] = 255
Assert-Rejected { Assert-Gmk70BatteryCode $known '630dcd2966c4336691125448bbb25b4ff412a49c732db2c8abc1b8581bd710dd' } 'Mismatched code'
foreach ($name in @('read-battery.ps1', 'battery-core.ps1', 'ram-transport.ps1')) {
    $tokens = $null; $errors = $null
    [void] [Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot $name), [ref] $tokens, [ref] $errors)
    if ($errors) { throw "Invalid PowerShell: $name" }
}
Write-Host 'PASS: code mismatch stops decoding; acquisition and transport parse without accessing hardware.'

$temporary = Join-Path ([IO.Path]::GetTempPath()) ('gmk70-battery-tests-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($temporary) | Out-Null
$hostExe = (Get-Process -Id $PID).Path
$fixture = Join-Path $PSScriptRoot 'battery-flow-fixture.ps1'
try {
    $output = Join-Path $temporary 'success.json'
    & $hostExe -NoLogo -NoProfile -File $fixture -OutputPath $output | Out-Null
    Assert-Equal $LASTEXITCODE 0 'Complete acquisition'
    $report = [IO.File]::ReadAllText($output) | ConvertFrom-Json
    Assert-Equal $report.status 'complete' 'Acquisition status'
    Assert-Equal $report.samples.Length 3 'Snapshot count'
    Assert-Equal $report.samples[0].downward_filter_count 121 'First battery block retained before code checks'
    Assert-Equal $report.samples[2].downward_filter_count 123 'Later battery snapshot'
    Assert-Equal $report.requests.Length 16 'Bounded read count'
    Assert-Equal (Test-Path ($output + '.closed')) $true 'Transport closed'
    $original = [IO.File]::ReadAllText($output)
    # Desktop PowerShell promotes redirected native stderr to ErrorRecords.
    # This invocation intentionally fails; verify its exit and preserved file.
    try {
        $ErrorActionPreference = 'Continue'
        & $hostExe -NoLogo -NoProfile -File $fixture -OutputPath $output 2>&1 | Out-Null
    }
    finally { $ErrorActionPreference = 'Stop' }
    Assert-Equal ($LASTEXITCODE -ne 0) $true 'Existing output rejected'
    Assert-Equal ([IO.File]::ReadAllText($output)) $original 'Existing capture preserved'
    $output = Join-Path $temporary 'timeout.json'
    & $hostExe -NoLogo -NoProfile -File $fixture -OutputPath $output -FailSecondSnapshot | Out-Null
    Assert-Equal $LASTEXITCODE 1 'Timeout exit'
    $report = [IO.File]::ReadAllText($output) | ConvertFrom-Json
    Assert-Equal $report.status 'failed' 'Partial capture status'
    Assert-Equal $report.samples.Length 1 'Completed snapshot preserved'
    Assert-Equal $report.requests.Length 9 'No retry after timeout'
    Assert-Equal $report.error_stage 'read snapshot 2' 'Failed operation recorded'
    Assert-Equal -Actual $report.error_details.exceptions[0].message -Expected 'Synthetic timeout; never retry.' -Label 'Useful failure message'
    Assert-Equal -Actual $report.error_details.exceptions[0].hresult -Expected '0x80131505' -Label 'Timeout HRESULT'
    Assert-Equal (Test-Path ($output + '.closed')) $true 'Timeout closes transport'
    $output = Join-Path $temporary 'connect.json'
    & $hostExe -NoLogo -NoProfile -File $fixture -OutputPath $output -FailConnect | Out-Null
    Assert-Equal -Actual $LASTEXITCODE -Expected 1 -Label 'Connection failure exit'
    $report = [IO.File]::ReadAllText($output) | ConvertFrom-Json
    Assert-Equal -Actual $report.error_stage -Expected 'connect' -Label 'Connection acquisition stage'
    Assert-Equal -Actual $report.error_connect_stage -Expected 'discovery-name-selection' -Label 'Precise connection substage'
    Assert-Equal -Actual ($report.error_details.exceptions[0].message -match '^No connected GMK70') -Expected $true -Label 'Empty discovery explanation'
    Assert-Equal -Actual ([string]::IsNullOrEmpty($report.error_details.id)) -Expected $false -Label 'Useful PowerShell error identifier'
    Assert-Equal -Actual $report.error_details.source_file -Expected 'radio-core.ps1' -Label 'Source basename only'
    Assert-Equal -Actual $report.requests.Length -Expected 0 -Label 'No requests on connection failure'
    Assert-Equal -Actual $report.samples.Length -Expected 0 -Label 'No simulated diagnosis on connection failure'
    Assert-Equal -Actual $report.bounded_code_verified -Expected $false -Label 'No code verification without connection'
    Assert-Equal -Actual (Test-Path ($output + '.closed')) -Expected $true -Label 'Connection failure closes transport'
} finally { Remove-Item -LiteralPath $temporary -Recurse -Force }
Write-Host 'PASS: mocked acquisition writes complete/partial JSON, closes transport, preserves existing output and never retries a timeout.'
