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
#   .\scripts\run-tests-parallel.ps1                              # Default binary
#   .\scripts\run-tests-parallel.ps1 .\build\bin\core-tests.exe   # Explicit path
#
# Requirements:
#   - Tests must be isolated (no shared global state between tests)
#   - Tests must not write to fixed temp file paths
#

param(
    [string]$Binary = ".\build\bin\core-tests.exe",
    [int]$Shards = 4
)

# Validate binary exists
if (-not (Test-Path $Binary)) {
    Write-Error "Test binary not found: $Binary"
    Write-Host ""
    Write-Host "Build with:"
    Write-Host "  cmake --build build --target core-tests"
    exit 1
}

Write-Host "=========================================="
Write-Host "Running $Shards-way parallel tests"
Write-Host "Binary: $Binary"
Write-Host "=========================================="

# Launch all shards in parallel using Jobs
$jobs = @()
for ($i = 0; $i -lt $Shards; $i++) {
    Write-Host "Starting shard $i/$($Shards-1)..."
    $jobs += Start-Job -ScriptBlock {
        param($bin, $total, $index)
        $env:GTEST_TOTAL_SHARDS = $total
        $env:GTEST_SHARD_INDEX = $index
        & $bin
    } -ArgumentList $Binary, $Shards, $i
}

# Wait for all jobs to complete
$jobs | Wait-Job | Out-Null

# Collect and display results
foreach ($job in $jobs) {
    Receive-Job -Job $job
    Remove-Job -Job $job
}

Write-Host "=========================================="
Write-Host "All $Shards shards complete."
Write-Host "=========================================="
