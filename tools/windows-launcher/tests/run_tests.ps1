# Integration tests for the Windows launcher. Runs real processes in temporary install roots, so it needs real Windows.
#   pwsh tools/windows-launcher/tests/run_tests.ps1 -BinDir build-launcher/Release [-Only name,name]
param(
    [Parameter(Mandatory = $true)][string]$BinDir,
    [string[]]$Only = @(),
    [string]$WorkDir = (Join-Path $env:SystemDrive 'qk4-launcher-tests')
)

$ErrorActionPreference = 'Stop'
$BinDir = (Resolve-Path $BinDir).Path
$Launcher = Join-Path $BinDir 'QK4.exe'
$PauseLauncher = Join-Path $BinDir 'QK4-testpause.exe'
$FakeApp = Join-Path $BinDir 'fakeapp.exe'
$QtApp = Join-Path $BinDir 'qtrelaunchapp.exe'

$V11 = '0.8.0-uj.11'
$V12 = '0.8.0-uj.12'
$V13 = '0.8.0-uj.13'
$V14 = '0.8.0-uj.14'
$V15 = '0.8.0-uj.15'
$V16 = '0.8.0-uj.16'

$EXIT_OK = 0
$EXIT_NO_VERSION = 1
$EXIT_ROLLED_BACK = 2
$EXIT_COMMIT_FAILED = 3

$script:Cases = [System.Collections.Generic.List[object]]::new()
function Add-Case([string]$Name, [scriptblock]$Body) { $script:Cases.Add([pscustomobject]@{ Name = $Name; Body = $Body }) }

# --- assertions ---------------------------------------------------------------------------------------------------

function Assert-True($Condition, [string]$Message) { if (-not $Condition) { throw "ASSERT: $Message" } }
function Assert-Eq($Expected, $Actual, [string]$Message) {
    if ($Expected -cne $Actual) { throw "ASSERT: $Message (expected '$Expected', got '$Actual')" }
}
function Wait-Until([scriptblock]$Condition, [int]$TimeoutMs, [string]$Message) {
    $clock = [Diagnostics.Stopwatch]::StartNew()
    while ($clock.ElapsedMilliseconds -lt $TimeoutMs) {
        if (& $Condition) { return }
        Start-Sleep -Milliseconds 50
    }
    throw "TIMEOUT after $TimeoutMs ms: $Message"
}

# --- install root ---------------------------------------------------------------------------------------------------

function New-Root([string]$Path = (Join-Path $WorkDir ([Guid]::NewGuid().ToString('N').Substring(0, 12)))) {
    New-Item -ItemType Directory -Force -Path $Path, (Join-Path $Path 'versions'), (Join-Path $Path 'fake-logs') | Out-Null
    Copy-Item $Launcher (Join-Path $Path 'QK4.exe')
    $script:Roots.Add($Path)
    return $Path
}

function Add-Version([string]$Root, [string]$Name, [string]$Exe = $FakeApp, [switch]$Healthy, [switch]$NoExe) {
    $folder = Join-Path $Root "versions\$Name"
    New-Item -ItemType Directory -Force -Path $folder | Out-Null
    if (-not $NoExe) { Copy-Item $Exe (Join-Path $folder 'QK4.exe') }
    if ($Healthy) { New-Item -ItemType File -Path (Join-Path $folder '.healthy') | Out-Null }
    return $folder
}

function Set-Current([string]$Root, [string]$Name) {
    [IO.File]::WriteAllText((Join-Path $Root 'current.txt'), "$Name`r`n")
}
function Get-Current([string]$Root) { ([IO.File]::ReadAllText((Join-Path $Root 'current.txt'))).Trim() }

function Get-FailureNote([string]$Root) {
    $path = Join-Path $Root 'update-failed.txt'
    if (Test-Path -LiteralPath $path) { return @([IO.File]::ReadAllLines($path) | Where-Object { $_ -ne '' }) }
    return @()
}

function Start-Exe([string]$Exe, [string]$Root, [string[]]$Arguments = @(), [hashtable]$Environment = @{}) {
    $psi = [Diagnostics.ProcessStartInfo]::new($Exe)
    $psi.UseShellExecute = $false
    foreach ($a in $Arguments) { $psi.ArgumentList.Add($a) }
    $psi.Environment['FAKE_LOG_DIR'] = (Join-Path $Root 'fake-logs')
    $psi.Environment.Remove('QK4_UPDATE_ATTEMPT') | Out-Null
    foreach ($k in $Environment.Keys) { $psi.Environment[$k] = $Environment[$k] }
    return [Diagnostics.Process]::Start($psi)
}

function Start-Launcher([string]$Root, [string[]]$Arguments = @(), [hashtable]$Environment = @{}) {
    return Start-Exe (Join-Path $Root 'QK4.exe') $Root $Arguments $Environment
}

function Start-Finish([string]$Root, [string]$Version, [string]$Mode, [int]$ClosingPid = 0, [string[]]$Original = @()) {
    return Start-Launcher $Root (@('--qk4-finish-update', $Version, "$ClosingPid", '--') + $Original) @{ FAKE_MODE = $Mode }
}

function Wait-Exit($Process, [int]$TimeoutMs = 150000) {
    if (-not $Process.WaitForExit($TimeoutMs)) { throw "TIMEOUT: process $($Process.Id) did not exit within $TimeoutMs ms" }
    $Process.WaitForExit()
    return $Process.ExitCode
}

function Test-Alive([int]$ProcessId) { return $null -ne (Get-Process -Id $ProcessId -ErrorAction SilentlyContinue) }

function Get-Starts([string]$Root, [string]$Version = '') {
    $records = @(foreach ($file in Get-ChildItem -LiteralPath (Join-Path $Root 'fake-logs') -Filter '*.log') {
        $record = [pscustomobject]@{
            Pid = [int]$file.BaseName; Exe = ''; Attempt = ''; Restarted = $false; Started = [long]0
            Args = [System.Collections.Generic.List[string]]::new()
        }
        foreach ($line in [IO.File]::ReadAllLines($file.FullName, [Text.Encoding]::UTF8)) {
            $at = $line.IndexOf('=')
            $key = $line.Substring(0, $at)
            $value = $line.Substring($at + 1)
            switch ($key) {
                'exe' { $record.Exe = $value }
                'attempt' { $record.Attempt = $value }
                'restarted' { $record.Restarted = $value -eq '1' }
                'started' { $record.Started = [long]$value }
                'arg' { $record.Args.Add($value) }
            }
        }
        $record
    })
    return @($records | Where-Object { $Version -eq '' -or $_.Exe -like "*\versions\$Version\QK4.exe" } | Sort-Object Started)
}

# The launcher exits as soon as it has created a process, so that process's record can follow a moment later.
function Wait-Starts([string]$Root, [string]$Version, [int]$Count = 1) {
    Wait-Until { @(Get-Starts $Root $Version).Count -ge $Count } 5000 "$Version started $Count time(s)"
}

function Assert-Args([string[]]$Expected, $Start, [string]$Message) {
    Assert-Eq $Expected.Count $Start.Args.Count "$Message argument count"
    for ($i = 0; $i -lt $Expected.Count; ++$i) { Assert-Eq $Expected[$i] $Start.Args[$i] "$Message argument $i" }
}

function Stop-RootProcesses([string]$Root) {
    Get-Process | Where-Object { $_.Path -and $_.Path.StartsWith("$Root\", [StringComparison]::OrdinalIgnoreCase) } |
        Stop-Process -Force -ErrorAction SilentlyContinue
}

function Remove-Root([string]$Root) {
    Stop-RootProcesses $Root
    for ($i = 0; $i -lt 50 -and (Test-Path -LiteralPath $Root); ++$i) {
        try { Remove-Item -LiteralPath $Root -Recurse -Force -ErrorAction Stop } catch { Start-Sleep -Milliseconds 200 }
    }
}

function Show-Root([string]$Root) {
    Write-Host "--- state of $Root"
    Get-ChildItem -LiteralPath $Root -Recurse -Force | ForEach-Object { Write-Host "  $($_.FullName)" }
    foreach ($s in Get-Starts $Root) {
        Write-Host "  start pid=$($s.Pid) alive=$(Test-Alive $s.Pid) restarted=$($s.Restarted) exe=$($s.Exe) args=$($s.Args -join '|')"
    }
    foreach ($f in 'current.txt', 'update-failed.txt') {
        $p = Join-Path $Root $f
        if (Test-Path -LiteralPath $p) { Write-Host "  ${f}: $([IO.File]::ReadAllText($p).Trim())" }
    }
}

# Asserts everything a successful update promises.
function Assert-Committed([string]$Root, [string]$Version, $Process, [string]$LeftoverAttempt = '') {
    Assert-Eq $EXIT_OK (Wait-Exit $Process) 'finisher exit code'
    Assert-Eq $Version (Get-Current $Root) 'current.txt'
    $folder = Join-Path $Root "versions\$Version"
    Assert-True (Test-Path -LiteralPath (Join-Path $folder '.healthy')) '.healthy written'
    $ready = @(Get-ChildItem -LiteralPath $folder -Force -Filter '.ready-*' | Where-Object Name -ne ".ready-$LeftoverAttempt")
    Assert-Eq 0 $ready.Count 'readiness file removed'
}

# Asserts everything a rollback promises when the attempt's folder had nothing else running from it.
function Assert-RolledBack([string]$Root, [string]$Version, [string]$Previous, [string[]]$Original, $Process) {
    Assert-Eq $EXIT_ROLLED_BACK (Wait-Exit $Process) 'finisher exit code'
    Assert-Eq $Previous (Get-Current $Root) 'current.txt unchanged'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $Root "versions\$Version"))) 'failed folder gone'
    Assert-Eq 0 @(Get-ChildItem -LiteralPath (Join-Path $Root 'versions') -Force -Filter '.trash-*').Count 'trash deleted'
    Assert-True ((Get-FailureNote $Root) -contains $Version) 'failure note names the version'
    $attempts = @(Get-Starts $Root $Version)
    Assert-True ($attempts.Count -ge 1) 'attempt really ran'
    foreach ($a in $attempts) { Assert-True (-not (Test-Alive $a.Pid)) "attempt process $($a.Pid) stopped" }
    Wait-Starts $Root $Previous
    $back = @(Get-Starts $Root $Previous)
    Assert-Eq 1 $back.Count 'previous version started once'
    Assert-Eq '' $back[0].Attempt 'previous version gets no attempt ID'
    Assert-True (Test-Alive $back[0].Pid) 'previous version running'
    Assert-Args $Original $back[0] 'previous version'
}

function Get-PlainRoot() {
    $root = New-Root
    Add-Version $root $V13 -Healthy | Out-Null
    Add-Version $root $V14 | Out-Null
    Set-Current $root $V13
    return $root
}

$HardArgs = @(
    '--connect', 'Radio With Spaces', ("S$([char]0xFC)d $([char]0x65E5)$([char]0x672C)$([char]0x8A9E)"),
    'C:\path with space\', 'trail\', 'two\\', 'quote"inside', ''
)

# --- install flow ---------------------------------------------------------------------------------------------------

Add-Case 'healthyCommits' {
    $root = Get-PlainRoot
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $finisher = Start-Finish $root $V14 'ready' 0 @('--connect', 'Radio A')
    Assert-Committed $root $V14 $finisher
    Assert-True ($clock.ElapsedMilliseconds -ge 9500) 'watched for 10 s after readiness'
    Assert-Eq 0 (Get-FailureNote $root).Count 'no failure note'
    $starts = @(Get-Starts $root $V14)
    Assert-Eq 1 $starts.Count 'one start'
    Assert-True ($starts[0].Attempt -match '^[0-9a-f]{16,}$') 'random attempt ID'
    Assert-True (Test-Alive $starts[0].Pid) 'left running'
    Assert-Args @('--connect', 'Radio A') $starts[0] 'new version'
    Assert-Eq 0 @(Get-Starts $root $V13).Count 'previous not started'
}

Add-Case 'cleanExitAfterReadyCommits' {
    $root = Get-PlainRoot
    Assert-Committed $root $V14 (Start-Finish $root $V14 'clean-exit-after-ready')
    Assert-Eq 0 @(Get-Starts $root $V13).Count 'previous not started'
}

Add-Case 'neverReadyRollsBack' {
    $root = Get-PlainRoot
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $finisher = Start-Finish $root $V14 'never-ready' 0 @('--connect', 'Radio A')
    Assert-RolledBack $root $V14 $V13 @('--connect', 'Radio A') $finisher
    Assert-True ($clock.ElapsedMilliseconds -ge 59000) 'waited the full 60 s'
}

Add-Case 'crashAfterReadyRollsBack' {
    $root = Get-PlainRoot
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $finisher = Start-Finish $root $V14 'crash-after-ready' 0 @('--connect', 'Radio A')
    Assert-RolledBack $root $V14 $V13 @('--connect', 'Radio A') $finisher
    Assert-True ($clock.ElapsedMilliseconds -lt 15000) 'caught as soon as the job emptied'
}

Add-Case 'startFailRollsBackAfterGrace' {
    $root = Get-PlainRoot
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $finisher = Start-Finish $root $V14 'start-fail'
    Assert-RolledBack $root $V14 $V13 @() $finisher
    Assert-True ($clock.ElapsedMilliseconds -ge 4500) 'empty job given its 5 s'
    Assert-True ($clock.ElapsedMilliseconds -lt 20000) 'failed early, not at 60 s'
}

Add-Case 'missingFolderRollsBack' {
    $root = Get-PlainRoot
    Remove-Item -LiteralPath (Join-Path $root "versions\$V14") -Recurse -Force
    $finisher = Start-Finish $root $V14 'ready' 0 @('--connect', 'Radio A')
    Assert-Eq $EXIT_ROLLED_BACK (Wait-Exit $finisher 20000) 'finisher exit code'
    Assert-True ((Get-FailureNote $root) -contains $V14) 'failure note names the version'
    Wait-Starts $root $V13
    Assert-Eq 1 @(Get-Starts $root $V13).Count 'previous version started'
}

Add-Case 'windowWithoutFilePasses' {
    $root = Get-PlainRoot
    $clock = [Diagnostics.Stopwatch]::StartNew()
    Assert-Committed $root $V14 (Start-Finish $root $V14 'no-file-but-window')
    Assert-True ($clock.ElapsedMilliseconds -ge 59000) 'waited the full 60 s'
    $starts = @(Get-Starts $root $V14)
    Assert-True (Test-Alive $starts[0].Pid) 'session kept running'
    Assert-Eq 0 @(Get-Starts $root $V13).Count 'previous not started'
}

Add-Case 'restartThenReadyPasses' {
    $root = Get-PlainRoot
    Assert-Committed $root $V14 (Start-Finish $root $V14 'restart-then-ready' 0 @('--connect', 'Radio A'))
    $starts = @(Get-Starts $root $V14)
    Assert-Eq 2 $starts.Count 'original and replacement'
    Assert-True (Test-Alive $starts[1].Pid) 'replacement left running'
    Assert-Args @('--connect', 'Radio A') $starts[1] 'replacement'
}

Add-Case 'qtRestartThenReadyPasses' {
    $root = New-Root
    Add-Version $root $V13 -Healthy | Out-Null
    Add-Version $root $V14 -Exe $QtApp | Out-Null
    Set-Current $root $V13
    Assert-Committed $root $V14 (Start-Finish $root $V14 'ready' 0 @('--connect', 'Radio A'))
    $starts = @(Get-Starts $root $V14)
    Assert-Eq 2 $starts.Count 'original and replacement'
    Assert-True (Test-Alive $starts[1].Pid) 'replacement left running'
    Assert-Eq $starts[0].Attempt $starts[1].Attempt 'replacement inherits the attempt ID'
}

# QProcess::startDetached, exactly as InterfaceScale::relaunch() calls it: if Qt broke away from the job the replacement
# would survive this and the assertion in Assert-RolledBack would fail.
Add-Case 'restartThenFailStops' {
    $root = New-Root
    Add-Version $root $V13 -Healthy | Out-Null
    Add-Version $root $V14 -Exe $QtApp | Out-Null
    Set-Current $root $V13
    $finisher = Start-Finish $root $V14 'never-ready'
    Wait-Until { @(Get-Starts $root $V14 | Where-Object Restarted).Count -eq 1 } 15000 'replacement started'
    Assert-RolledBack $root $V14 $V13 @() $finisher
    Assert-Eq 2 @(Get-Starts $root $V14).Count 'original and replacement both ran'
}

Add-Case 'staleHealthyDoesNotPass' {
    $root = Get-PlainRoot
    $folder = Join-Path $root "versions\$V14"
    New-Item -ItemType File -Path (Join-Path $folder '.healthy') | Out-Null
    New-Item -ItemType File -Path (Join-Path $folder '.ready-deadbeefdeadbeef') | Out-Null
    Assert-RolledBack $root $V14 $V13 @() (Start-Finish $root $V14 'never-ready')
}

Add-Case 'concurrentVersions' {
    $root = Get-PlainRoot
    Add-Version $root $V15 | Out-Null
    $higher = Start-Finish $root $V15 'ready'
    Start-Sleep -Seconds 1
    $lower = Start-Finish $root $V14 'ready'
    Assert-Eq $EXIT_OK (Wait-Exit $higher) 'higher finisher'
    Assert-Eq $V15 (Get-Current $root) 'higher committed first'
    Assert-Eq $EXIT_OK (Wait-Exit $lower) 'lower finisher'
    Assert-Eq $V15 (Get-Current $root) 'current.txt never moves backwards'
}

Add-Case 'oneFailsOneWorks' {
    $root = Get-PlainRoot
    $first = Start-Finish $root $V14 'ready'
    Wait-Until { @(Get-Starts $root $V14).Count -eq 1 } 5000 'first attempt started'
    $firstStart = (Get-Starts $root $V14)[0]
    $second = Start-Finish $root $V14 'start-fail'
    Assert-Eq $EXIT_ROLLED_BACK (Wait-Exit $second) 'second finisher'
    Assert-True (Test-Alive $firstStart.Pid) 'first attempt untouched'
    Assert-True (Test-Path -LiteralPath (Join-Path $root "versions\$V14\QK4.exe")) 'folder kept'
    Assert-True ((Get-FailureNote $root) -contains $V14) 'failure note written'
    Wait-Starts $root $V13
    Assert-Eq 1 @(Get-Starts $root $V13).Count 'previous version started'
    Assert-Eq $EXIT_OK (Wait-Exit $first) 'first finisher'
    Assert-Eq $V14 (Get-Current $root) 'first attempt committed'
    Assert-True (Test-Alive $firstStart.Pid) 'first attempt still running'
}

Add-Case 'failureAfterOtherSucceeded' {
    $root = Get-PlainRoot
    Set-Current $root $V14
    $finisher = Start-Finish $root $V14 'start-fail' 0 @('--connect', 'Radio A')
    Assert-Eq $EXIT_ROLLED_BACK (Wait-Exit $finisher) 'finisher exit code'
    Assert-Eq 0 (Get-FailureNote $root).Count 'no failure note'
    Assert-True (Test-Path -LiteralPath (Join-Path $root "versions\$V14\QK4.exe")) 'folder kept'
    Wait-Starts $root $V14 2
    $current = @(Get-Starts $root $V14 | Where-Object Attempt -eq '')
    Assert-Eq 1 $current.Count 'current version started'
    Assert-Args @('--connect', 'Radio A') $current[0] 'current version'
}

Add-Case 'failureNoteNotDuplicated' {
    $root = Get-PlainRoot
    [IO.File]::WriteAllText((Join-Path $root 'update-failed.txt'), "$V12`n$V14`n")
    Assert-RolledBack $root $V14 $V13 @() (Start-Finish $root $V14 'start-fail')
    $note = Get-FailureNote $root
    Assert-Eq 2 $note.Count 'note lines'
    Assert-Eq $V12 $note[0] 'earlier line kept'
}

Add-Case 'failureNoteAppends' {
    $root = Get-PlainRoot
    [IO.File]::WriteAllText((Join-Path $root 'update-failed.txt'), $V12) # no final newline
    Assert-RolledBack $root $V14 $V13 @() (Start-Finish $root $V14 'start-fail')
    $note = Get-FailureNote $root
    Assert-Eq 2 $note.Count 'one version per line'
    Assert-Eq $V12 $note[0] 'earlier line kept'
    Assert-Eq $V14 $note[1] 'new line added'
}

Add-Case 'launcherKilled' {
    $root = Get-PlainRoot
    $finisher = Start-Finish $root $V14 'ready'
    Wait-Until { @(Get-Starts $root $V14).Count -eq 1 } 5000 'attempt started'
    $orphan = (Get-Starts $root $V14)[0]
    Start-Sleep -Seconds 1
    Stop-Process -Id $finisher.Id -Force
    $finisher.WaitForExit()
    Assert-True (Test-Alive $orphan.Pid) 'no kill on close'
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $launch = Start-Launcher $root
    Assert-Eq $EXIT_OK (Wait-Exit $launch 5000) 'launch after kill'
    Assert-True ($clock.ElapsedMilliseconds -lt 3000) 'lock is free'
    Wait-Starts $root $V13
    Assert-Eq 1 @(Get-Starts $root $V13).Count 'old version until proven'
    # The killed attempt's own readiness file stays. It names that attempt's ID, so it proves nothing about this one.
    Assert-Committed $root $V14 (Start-Finish $root $V14 'ready') $orphan.Attempt
}

Add-Case 'closingCopyHangs' {
    $root = Get-PlainRoot
    $closing = Start-Launcher $root
    Wait-Until { @(Get-Starts $root $V13).Count -eq 1 } 5000 'closing copy started'
    $closingPid = (Get-Starts $root $V13)[0].Pid
    $begin = [DateTime]::UtcNow.ToFileTimeUtc()
    $finisher = Start-Finish $root $V14 'ready' $closingPid
    Wait-Until { @(Get-Starts $root $V14).Count -eq 1 } 40000 'new version started'
    $waited = ((Get-Starts $root $V14)[0].Started - $begin) / 10000
    Assert-True ($waited -ge 29000) "waited 30 s, got $waited ms"
    Assert-True ($waited -lt 36000) "stopped waiting at 30 s, got $waited ms"
    Assert-True (-not (Test-Alive $closingPid)) 'hung copy terminated'
    Assert-Committed $root $V14 $finisher
}

Add-Case 'waitsForClosingCopy' {
    $root = Get-PlainRoot
    $closing = Start-Launcher $root
    Wait-Until { @(Get-Starts $root $V13).Count -eq 1 } 5000 'closing copy started'
    $closingPid = (Get-Starts $root $V13)[0].Pid
    $finisher = Start-Finish $root $V14 'ready' $closingPid
    Start-Sleep -Seconds 3
    Assert-Eq 0 @(Get-Starts $root $V14).Count 'new version waits'
    $stopped = [DateTime]::UtcNow.ToFileTimeUtc()
    Stop-Process -Id $closingPid -Force
    Wait-Until { @(Get-Starts $root $V14).Count -eq 1 } 3000 'new version started once it closed'
    Assert-True ((Get-Starts $root $V14)[0].Started -ge ($stopped - 10000000)) 'started after the close'
    Assert-Committed $root $V14 $finisher
}

Add-Case 'closingPidReusedByStranger' {
    $root = Get-PlainRoot
    $stranger = Start-Process -FilePath 'ping.exe' -ArgumentList '-n', '120', '127.0.0.1' -PassThru -WindowStyle Hidden
    try {
        $finisher = Start-Finish $root $V14 'ready' $stranger.Id
        Wait-Until { @(Get-Starts $root $V14).Count -eq 1 } 5000 'no wait for a process outside the install'
        Assert-Committed $root $V14 $finisher
        Assert-True (Test-Alive $stranger.Id) 'stranger left alone'
    } finally { Stop-Process -Id $stranger.Id -Force -ErrorAction SilentlyContinue }
}

Add-Case 'badFinishArgumentsStillLaunch' {
    $root = Get-PlainRoot
    $launch = Start-Launcher $root @('--qk4-finish-update', '..\..\evil-uj.99', '0', '--', '--connect', 'Radio A')
    Wait-Exit $launch 20000 | Out-Null
    Wait-Starts $root $V13
    $starts = @(Get-Starts $root $V13)
    Assert-Eq 1 $starts.Count 'current version started'
    Assert-Args @('--connect', 'Radio A') $starts[0] 'current version'
}

# --- transient locks ---------------------------------------------------------------------------------------------

Add-Case 'commitRetriesPastBusyLock' {
    $root = Get-PlainRoot
    $finisher = Start-Finish $root $V14 'ready'
    Start-Sleep -Seconds 2
    $lock = [IO.File]::Open((Join-Path $root '.update.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
    try {
        Assert-True (-not $finisher.WaitForExit(13000)) 'cannot commit while the lock is held'
    } finally { $lock.Dispose() }
    Assert-Committed $root $V14 $finisher
}

Add-Case 'commitFailureKeepsNewVersionRunning' {
    $root = Get-PlainRoot
    $finisher = Start-Finish $root $V14 'ready'
    Start-Sleep -Seconds 2
    $lock = [IO.File]::Open((Join-Path $root '.update.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
    try {
        Assert-Eq $EXIT_COMMIT_FAILED (Wait-Exit $finisher 70000) 'finisher exit code'
    } finally { $lock.Dispose() }
    Assert-Eq $V13 (Get-Current $root) 'next launch gets the old version'
    Assert-True (Test-Alive (Get-Starts $root $V14)[0].Pid) 'new version keeps running'
    Assert-True (Test-Path -LiteralPath (Join-Path $root "versions\$V14\QK4.exe")) 'folder kept for the next offer'
    Assert-Eq 0 (Get-FailureNote $root).Count 'not a failure'
    Assert-Eq 0 @(Get-Starts $root $V13).Count 'previous not started'
}

Add-Case 'rollbackRenameRetries' {
    $root = Get-PlainRoot
    # Stands in for antivirus holding a file: the folder cannot be renamed until this closes.
    $held = [IO.File]::Open((Join-Path $root "versions\$V14\scan.bin"), 'Create', 'ReadWrite', 'None')
    $finisher = Start-Finish $root $V14 'crash-after-ready'
    try {
        Assert-True (-not $finisher.WaitForExit(6000)) 'rename keeps retrying'
    } finally { $held.Dispose() }
    Assert-RolledBack $root $V14 $V13 @() $finisher
}

Add-Case 'lockHeldLaunchesAnyway' {
    $root = Get-PlainRoot
    $lock = [IO.File]::Open((Join-Path $root '.update.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
    try {
        $clock = [Diagnostics.Stopwatch]::StartNew()
        Assert-Eq $EXIT_OK (Wait-Exit (Start-Launcher $root) 20000) 'launch exit code'
        Assert-True ($clock.ElapsedMilliseconds -ge 9500) 'waited 10 s for the lock'
        Assert-True ($clock.ElapsedMilliseconds -lt 14000) 'gave up at 10 s'
        Wait-Starts $root $V13
        Assert-Eq 1 @(Get-Starts $root $V13).Count 'launched anyway'
    } finally { $lock.Dispose() }
}

# A root the user cannot write to gives an immediate error, not a sharing violation. It must not cost every launch 10 s.
Add-Case 'unusableLockIsNotWaitedFor' {
    $root = Get-PlainRoot
    New-Item -ItemType Directory -Path (Join-Path $root '.update.lock') | Out-Null
    $clock = [Diagnostics.Stopwatch]::StartNew()
    Assert-Eq $EXIT_OK (Wait-Exit (Start-Launcher $root) 20000) 'launch exit code'
    Assert-True ($clock.ElapsedMilliseconds -lt 3000) 'no 10 s wait'
    Wait-Starts $root $V13
    Assert-Eq 1 @(Get-Starts $root $V13).Count 'launched'
}

# --- cleanup ---------------------------------------------------------------------------------------------------------

Add-Case 'cleanupInUse' {
    $root = New-Root
    foreach ($v in $V11, $V12, $V13, $V14, $V15, $V16) { Add-Version $root $v | Out-Null }
    $versions = Join-Path $root 'versions'
    $oldIncoming = New-Item -ItemType Directory -Path (Join-Path $versions '.incoming-old')
    $oldIncoming.LastWriteTimeUtc = [DateTime]::UtcNow.AddDays(-2)
    New-Item -ItemType Directory -Path (Join-Path $versions '.incoming-new') | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $versions '.trash-left\sub') | Out-Null
    Set-Current $root $V12
    Assert-Eq $EXIT_OK (Wait-Exit (Start-Launcher $root) 10000) 'launch of the old version'
    Wait-Until { @(Get-Starts $root $V12).Count -eq 1 } 5000 'old version running'
    $old = (Get-Starts $root $V12)[0]
    Set-Current $root $V14

    $cleanup = Start-Launcher $root @('--qk4-cleanup', $V14, '15')
    Assert-Eq $EXIT_OK (Wait-Exit $cleanup 40000) 'cleanup exit code'

    Assert-True (Test-Alive $old.Pid) 'old copy still running'
    Assert-True (Test-Path -LiteralPath (Join-Path $versions "$V12\QK4.exe")) 'folder in use stays'
    foreach ($kept in $V13, $V14, $V16, '.incoming-new') {
        Assert-True (Test-Path -LiteralPath (Join-Path $versions $kept)) "$kept kept"
    }
    foreach ($gone in $V11, $V15, '.incoming-old', '.trash-left') {
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $versions $gone))) "$gone removed"
    }
    Assert-Eq 0 @(Get-ChildItem -LiteralPath $versions -Force -Filter '.trash-*').Count 'no trash left'
}

Add-Case 'cleanupOnlyFromCurrentVersion' {
    $root = New-Root
    foreach ($v in $V11, $V12, $V13, $V14) { Add-Version $root $v | Out-Null }
    Set-Current $root $V14
    Assert-Eq $EXIT_OK (Wait-Exit (Start-Launcher $root @('--qk4-cleanup', $V13, '')) 20000) 'cleanup exit code'
    Assert-True (Test-Path -LiteralPath (Join-Path $root "versions\$V11\QK4.exe")) 'nothing cleaned'
}

# A copy stranded in a .trash-* folder: a launcher killed between rename and put-back, or a put-back that never worked.
# It starts from its real folder and the folder moves under it, because a copy started from the .trash-* path would
# report that path and so says nothing about which name Windows gives a moved image.
Add-Case 'cleanupSparesRunningTrashFolder' {
    $root = New-Root
    foreach ($v in $V11, $V13, $V14) { Add-Version $root $v | Out-Null }
    $versions = Join-Path $root 'versions'
    New-Item -ItemType File -Path (Join-Path $versions "$V11\plugin.dll") | Out-Null
    Set-Current $root $V11
    Assert-Eq $EXIT_OK (Wait-Exit (Start-Launcher $root) 10000) 'launch of the copy'
    Wait-Starts $root $V11
    $running = (Get-Starts $root $V11)[0]
    Set-Current $root $V14
    Rename-Item -LiteralPath (Join-Path $versions $V11) -NewName '.trash-stranded'
    $trash = Join-Path $versions '.trash-stranded'

    Assert-Eq $EXIT_OK (Wait-Exit (Start-Launcher $root @('--qk4-cleanup', $V14, '')) 40000) 'cleanup exit code'
    Assert-True (Test-Alive $running.Pid) 'running copy unharmed'
    Assert-True (Test-Path -LiteralPath (Join-Path $trash 'plugin.dll')) 'its other files kept'

    Stop-Process -Id $running.Pid -Force
    Wait-Until { -not (Test-Alive $running.Pid) } 5000 'copy stopped'
    Assert-Eq $EXIT_OK (Wait-Exit (Start-Launcher $root @('--qk4-cleanup', $V14, '')) 40000) 'later cleanup exit code'
    Assert-True (-not (Test-Path -LiteralPath $trash)) 'removed once nothing runs from it'
}

# Stands for a copy the launcher cannot query, such as an elevated one: no visible process, but the exe cannot be opened
# for writing. Holding the exe open read-only blocks writers the same way a mapped image does.
Add-Case 'cleanupHonoursExeProbe' {
    $root = New-Root
    foreach ($v in $V13, $V14) { Add-Version $root $v | Out-Null }
    $trash = Add-Version $root '.trash-held'
    New-Item -ItemType File -Path (Join-Path $trash 'plugin.dll') | Out-Null
    Set-Current $root $V14
    $held = [IO.File]::Open((Join-Path $trash 'QK4.exe'), 'Open', 'Read', 'Read')
    try {
        Assert-Eq $EXIT_OK (Wait-Exit (Start-Launcher $root @('--qk4-cleanup', $V14, '')) 40000) 'cleanup exit code'
        Assert-True (Test-Path -LiteralPath (Join-Path $trash 'plugin.dll')) 'nothing deleted around the exe'
    } finally { $held.Dispose() }
    Assert-Eq $EXIT_OK (Wait-Exit (Start-Launcher $root @('--qk4-cleanup', $V14, '')) 40000) 'later cleanup exit code'
    Assert-True (-not (Test-Path -LiteralPath $trash)) 'removed once released'
}

function Start-PausedCleanup([string]$Root, [string]$Own) {
    Copy-Item $PauseLauncher (Join-Path $Root 'QK4-testpause.exe')
    $signal = Join-Path $Root 'pause'
    $process = Start-Exe (Join-Path $Root 'QK4-testpause.exe') $Root @('--qk4-cleanup', $Own, '') @{ QK4_TEST_PAUSE_FILE = $signal }
    Wait-Until { Test-Path -LiteralPath "$signal.paused" } 10000 'cleanup reached its pause'
    return [pscustomobject]@{ Process = $process; Signal = $signal }
}

# The launch outlasts the lock wait and starts the doomed folder while cleanup is past its process check. The running
# copy is what must survive.
Add-Case 'cleanupPausedRace' {
    $root = New-Root
    foreach ($v in $V11, $V12, $V13, $V14) { Add-Version $root $v | Out-Null }
    # NTFS lets the folder of a running exe be renamed, and only the exe itself refuses deletion. This file stands for
    # the DLLs and plugins a running copy would lose.
    $plugin = Join-Path $root "versions\$V11\plugin.dll"
    New-Item -ItemType File -Path $plugin | Out-Null
    Set-Current $root $V14
    $paused = Start-PausedCleanup $root $V14
    Set-Current $root $V11
    Assert-Eq $EXIT_OK (Wait-Exit (Start-Launcher $root) 20000) 'launch exit code'
    Wait-Until { @(Get-Starts $root $V11).Count -eq 1 } 5000 'that version started'
    $running = (Get-Starts $root $V11)[0]
    New-Item -ItemType File -Path "$($paused.Signal).resume" | Out-Null
    Assert-Eq $EXIT_OK (Wait-Exit $paused.Process 60000) 'cleanup exit code'
    Assert-True (Test-Alive $running.Pid) 'running copy unharmed'
    Assert-True (Test-Path -LiteralPath (Join-Path $root "versions\$V11\QK4.exe")) 'its folder kept'
    Assert-True (Test-Path -LiteralPath $plugin) 'its other files kept'
    Assert-Eq 0 @(Get-ChildItem -LiteralPath (Join-Path $root 'versions') -Force -Filter '.trash-*').Count 'no trash left'
}

# The launch is waiting on the lock when cleanup resumes, so it selects after the rename and cannot pick a folder that
# has gone.
Add-Case 'cleanupPausedRaceWithinLockWait' {
    $root = New-Root
    foreach ($v in $V11, $V12, $V13, $V14) { Add-Version $root $v -Healthy | Out-Null }
    Set-Current $root $V14
    $paused = Start-PausedCleanup $root $V14
    Set-Current $root $V11
    $launch = Start-Launcher $root
    Start-Sleep -Seconds 1
    New-Item -ItemType File -Path "$($paused.Signal).resume" | Out-Null
    Assert-Eq $EXIT_OK (Wait-Exit $paused.Process 60000) 'cleanup exit code'
    Assert-Eq $EXIT_OK (Wait-Exit $launch 15000) 'launch exit code'
    Wait-Starts $root $V14
    Assert-Eq 0 @(Get-Starts $root $V11).Count 'renamed folder not started'
    Assert-Eq 1 @(Get-Starts $root $V14).Count 'fallback started instead'
}

# --- normal launch -----------------------------------------------------------------------------------------------------

Add-Case 'argsRoundTrip' {
    $root = Get-PlainRoot
    Assert-Eq $EXIT_OK (Wait-Exit (Start-Launcher $root $HardArgs) 10000) 'normal launch'
    Wait-Until { @(Get-Starts $root $V13).Count -eq 1 } 5000 'normal launch started'
    Assert-Args $HardArgs (Get-Starts $root $V13)[0] 'normal launch'
    Stop-RootProcesses $root
    Remove-Item -Path (Join-Path $root 'fake-logs\*')

    Assert-RolledBack $root $V14 $V13 $HardArgs (Start-Finish $root $V14 'crash-after-ready' 0 $HardArgs)

    $root = Get-PlainRoot
    Assert-Committed $root $V14 (Start-Finish $root $V14 'ready' 0 $HardArgs)
    Assert-Args $HardArgs (Get-Starts $root $V14)[0] 'after commit'
}

Add-Case 'missingCurrentTxt' {
    $root = New-Root
    Add-Version $root $V13 -Healthy | Out-Null
    Add-Version $root $V14 | Out-Null
    Add-Version $root $V15 -Healthy -NoExe | Out-Null
    Assert-Eq $EXIT_OK (Wait-Exit (Start-Launcher $root) 10000) 'missing file'
    Wait-Until { @(Get-Starts $root).Count -eq 1 } 5000 'fallback started'
    Assert-Eq 1 @(Get-Starts $root $V13).Count 'highest healthy preferred'

    [IO.File]::WriteAllBytes((Join-Path $root 'current.txt'), [byte[]](0, 255, 13, 200, 0, 7))
    Assert-Eq $EXIT_OK (Wait-Exit (Start-Launcher $root) 10000) 'damaged file'
    Wait-Until { @(Get-Starts $root).Count -eq 2 } 5000 'fallback started again'
    Assert-Eq 2 @(Get-Starts $root $V13).Count 'damaged pointer falls back'

    Stop-RootProcesses $root
    Remove-Item -Path (Join-Path $root 'versions\*') -Recurse -Force
    $launch = Start-Launcher $root
    Wait-Until { $launch.Refresh(); $launch.MainWindowHandle -ne [IntPtr]::Zero } 10000 'visible error shown'
    Assert-Eq 'QK4' $launch.MainWindowTitle 'error dialog title'
    Assert-True (-not $launch.WaitForExit(500)) 'waits for the person'
    $launch.CloseMainWindow() | Out-Null
    Assert-Eq $EXIT_NO_VERSION (Wait-Exit $launch 5000) 'exit code when nothing qualifies'
}

Add-Case 'currentTxtEditedInNotepad' {
    $root = Get-PlainRoot
    Set-Current $root $V14
    [IO.File]::WriteAllText((Join-Path $root 'current.txt'), "$V13`r`n", [Text.UnicodeEncoding]::new($false, $true))
    Assert-Eq $EXIT_OK (Wait-Exit (Start-Launcher $root) 10000) 'launch'
    Wait-Until { @(Get-Starts $root).Count -eq 1 } 5000 'started'
    Assert-Eq 1 @(Get-Starts $root $V13).Count 'UTF-16 pointer honoured'
}

Add-Case 'pathWithSpaces' {
    $path = "C:\tmp\QK4 S$([char]0xFC)d long folder name"
    Remove-Root $path
    $root = New-Root $path
    Add-Version $root $V13 -Healthy | Out-Null
    Add-Version $root $V14 | Out-Null
    Add-Version $root $V15 | Out-Null
    Set-Current $root $V13
    Assert-Eq $EXIT_OK (Wait-Exit (Start-Launcher $root @('--connect', 'A B')) 10000) 'normal launch'
    Wait-Until { @(Get-Starts $root $V13).Count -eq 1 } 5000 'normal launch started'
    Assert-Args @('--connect', 'A B') (Get-Starts $root $V13)[0] 'normal launch'
    Assert-Committed $root $V14 (Start-Finish $root $V14 'ready' 0 @('--connect', 'A B'))
    Assert-Eq $EXIT_ROLLED_BACK (Wait-Exit (Start-Finish $root $V15 'crash-after-ready') 30000) 'rollback'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $root "versions\$V15"))) 'failed folder gone'
    Assert-True ((Get-FailureNote $root) -contains $V15) 'note written'
}

# --- the binary itself --------------------------------------------------------------------------------------------

Add-Case 'binaryContract' {
    Assert-Eq 'QK4 Update Launcher' (Get-Item $Launcher).VersionInfo.FileDescription 'FileDescription'
    $text = [Text.Encoding]::Latin1.GetString([IO.File]::ReadAllBytes($Launcher))
    Assert-True ($text.Contains('asInvoker')) 'manifest never asks for elevation'
    Assert-True ($text.IndexOf('VCRUNTIME', [StringComparison]::OrdinalIgnoreCase) -lt 0) 'static C runtime'
    Assert-True ($text.IndexOf('MSVCP', [StringComparison]::OrdinalIgnoreCase) -lt 0) 'static C++ runtime'
    $pauseName = -join ('QK4_TEST_PAUSE_FILE'.ToCharArray() | ForEach-Object { "$_`0" })
    Assert-True (-not $text.Contains($pauseName)) 'test pause absent from the release build'
    $pauseText = [Text.Encoding]::Latin1.GetString([IO.File]::ReadAllBytes($PauseLauncher))
    Assert-True ($pauseText.Contains($pauseName)) 'test pause present in the test build'
}

# --- runner ---------------------------------------------------------------------------------------------------------

New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null
$results = [System.Collections.Generic.List[object]]::new()
foreach ($case in $script:Cases) {
    if ($Only.Count -gt 0 -and $Only -notcontains $case.Name) { continue }
    $script:Roots = [System.Collections.Generic.List[string]]::new()
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $failure = $null
    try {
        & $case.Body
    } catch {
        $failure = "$_ at $($_.InvocationInfo.ScriptLineNumber)"
        foreach ($r in $script:Roots) { try { Show-Root $r } catch { Write-Host "state unavailable: $_" } }
    }
    foreach ($r in $script:Roots) { Remove-Root $r }
    $seconds = [math]::Round($clock.Elapsed.TotalSeconds, 1)
    $results.Add([pscustomobject]@{ Name = $case.Name; Passed = $null -eq $failure; Seconds = $seconds; Failure = $failure })
    if ($failure) { Write-Host "FAIL $($case.Name) (${seconds}s): $failure" } else { Write-Host "PASS $($case.Name) (${seconds}s)" }
}

$failed = @($results | Where-Object { -not $_.Passed })
Write-Host "$($results.Count - $failed.Count) passed, $($failed.Count) failed, $($results.Count) total"
if ($results.Count -eq 0) { Write-Host 'no case matched'; exit 1 }
exit $(if ($failed.Count -eq 0) { 0 } else { 1 })
