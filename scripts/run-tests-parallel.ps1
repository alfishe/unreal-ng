#
# run-tests-parallel.ps1 - Execute GTest tests with parallel sharding (Windows)
#
# Uses GTest's built-in sharding mechanism to split tests across multiple
# processes, achieving ~3x speedup on 4-core machines (37s -> 12s).
#
# How it works:
#   - GTEST_TOTAL_SHARDS tells GTest the total number of parallel runners
#   - GTEST_SHARD_INDEX tells each runner which subset of tests to run
#   - GTest automatically distributes tests evenly across shards
#
# Usage:
#   .\scripts\run-tests-parallel.ps1                              # Default binary, auto shards
#   .\scripts\run-tests-parallel.ps1 .\build\bin\core-tests.exe   # Explicit path, auto shards
#   .\scripts\run-tests-parallel.ps1 -Shards 16                   # Explicit shard count
#
# Shards default to auto-detection: the logical processor count reported by
# Windows (NUMBER_OF_PROCESSORS). Overshooting slightly is harmless - the OS
# timeslices the extra shards - so no efficiency-class weighting is applied.
#
# Requirements:
#   - Tests must be isolated (no shared global state between tests)
#   - Tests must not write to fixed temp file paths
#

param(
    [string]$Binary = ".\build\bin\core-tests.exe",
    [int]$Shards = 0
)

# Auto-detect shard count from the logical processor count when not specified
if ($Shards -le 0) {
    $Shards = [int]$env:NUMBER_OF_PROCESSORS
    if ($Shards -lt 1) { $Shards = 1 }
    Write-Host "Auto-detected $Shards shards from logical processor count"
}

# Validate binary exists
if (-not (Test-Path $Binary)) {
    Write-Error "Test binary not found: $Binary"
    Write-Host ""
    Write-Host "Build with:"
    Write-Host "  cmake --build build --target core-tests"
    exit 1
}

# Resolve to a fully-qualified, dot-free path. .NET Process.Start derives the
# child's executable path by string concatenation, so a "./"-containing -Binary
# would leak "." segments into _NSGetExecutablePath/argv[0]-based test path
# helpers on any platform.
[Environment]::CurrentDirectory = (Get-Location).ProviderPath
$Binary = [System.IO.Path]::GetFullPath($Binary)

Write-Host "=========================================="
Write-Host "Running $Shards-way parallel tests"
Write-Host "Binary: $Binary"
Write-Host "=========================================="

# Launch all shards in parallel as direct child processes (no Start-Job:
# jobs spawn a whole powershell.exe per shard, buffer output and lose exit codes).
# Each process gets its own GTEST_SHARD_INDEX; console output is inherited and
# live, exactly like the bash variant.
$shardInfo = @()
for ($i = 0; $i -lt $Shards; $i++) {
    Write-Host "Starting shard $i/$($Shards-1)..."
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $Binary
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.EnvironmentVariables['GTEST_TOTAL_SHARDS'] = "$Shards"
    $psi.EnvironmentVariables['GTEST_SHARD_INDEX'] = "$i"

    try {
        $proc = [System.Diagnostics.Process]::Start($psi)
    } catch {
        Write-Error "Failed to launch shard ${i}: $($_.Exception.Message)"
        exit 1
    }
    $shardInfo += New-Object PSObject -Property @{ Index = $i; Proc = $proc }
}

# Wait for every shard and track its exit code (nonzero = failed or crashed)
$failedShards = @()
foreach ($shard in $shardInfo) {
    $shard.Proc.WaitForExit()
    if ($shard.Proc.ExitCode -ne 0) {
        $failedShards += $shard.Index
        Write-Host "Shard $($shard.Index) FAILED with exit code $($shard.Proc.ExitCode)"
    }
    $shard.Proc.Dispose()
}

Write-Host "=========================================="
if ($failedShards.Count -gt 0) {
    Write-Host "$($failedShards.Count) of $Shards shards FAILED (indices: $($failedShards -join ', '))"
    Write-Host "=========================================="
    exit 1
}
Write-Host "All $Shards shards complete."
Write-Host "=========================================="
exit 0
