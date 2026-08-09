#
# Videowall Pattern Cycling Script
# Cycles through all available patterns with configurable delay
#
# Usage: .\cycle_patterns.ps1 [[-Url] URL] [[-Delay] seconds] [[-Basepath] PATH]
# Default delay: 15 seconds
# Default URL: http://localhost:8090
#
# This script will:
# - Cycle through all 10 available patterns in order
# - Show progress and pattern descriptions
# - Sleep for specified delay between patterns
# - Loop forever until interrupted with Ctrl+C
#
# Requires: videowall with 48 instances running, pattern script in same directory
#

param(
    [string]$Url = "http://localhost:8090",
    [int]$Delay = 15,
    [string]$Basepath = ""
)

# Available patterns (must match the Pattern enum in test_load_pattern_snapshots.py)
$Patterns = @(
    "all_same",
    "columns",
    "rows",
    "diagonal_down",
    "diagonal_up",
    "circles",
    "spiral",
    "checkerboard",
    "waves_horizontal",
    "waves_vertical"
)

# Pattern descriptions (indexed by position in $Patterns array)
$PatternDescriptions = @(
    "All instances load the same snapshot",          # all_same
    "Same snapshot per column (6 instances each)",   # columns
    "Same snapshot per row (8 instances each)",      # rows
    "Diagonal lines (top-right to bottom-left)",     # diagonal_down
    "Diagonal lines (top-left to bottom-right)",     # diagonal_up
    "Concentric circles from center",                # circles
    "Spiral pattern from center outward",            # spiral
    "Alternating checkerboard pattern",              # checkerboard
    "Horizontal wave pattern",                       # waves_horizontal
    "Vertical wave pattern"                          # waves_vertical
)

function Get-Timestamp {
    return (Get-Date -Format "yyyy-MM-dd HH:mm:ss")
}

# Validate delay
if ($Delay -le 0) {
    Write-Host "ERROR: Delay must be a positive integer (seconds)"
    Write-Host "Usage: .\cycle_patterns.ps1 [-Url URL] [-Basepath PATH] [-Delay seconds]"
    exit 1
}

# Check if pattern script exists
if (-not (Test-Path ".\test_load_pattern_snapshots.py")) {
    Write-Host "ERROR: test_load_pattern_snapshots.py not found in current directory"
    exit 1
}

Write-Host "$(Get-Timestamp) - Starting videowall pattern cycling"
Write-Host "$(Get-Timestamp) - WebAPI URL: $Url"
if ($Basepath -ne "") {
    Write-Host "$(Get-Timestamp) - Remote basepath: $Basepath"
}
Write-Host "$(Get-Timestamp) - Delay between patterns: $Delay seconds"
Write-Host "$(Get-Timestamp) - Available patterns: $($Patterns.Count)"
Write-Host "$(Get-Timestamp) - Press Ctrl+C to stop"
Write-Host ""

$PatternCount = $Patterns.Count
$CycleCount = 0

try {
    while ($true) {
        $CycleCount++

        for ($i = 0; $i -lt $PatternCount; $i++) {
            $Pattern = $Patterns[$i]
            $Description = $PatternDescriptions[$i]

            Write-Host "$(Get-Timestamp) - [Cycle $CycleCount, Pattern $($i + 1)/$PatternCount]"
            Write-Host "$(Get-Timestamp) - Loading pattern: $Pattern"
            Write-Host "$(Get-Timestamp) - Description: $Description"

            # Build args list
            $Args = @(".\test_load_pattern_snapshots.py", "--url", $Url, "--pattern", $Pattern)
            if ($Basepath -ne "") {
                $Args += @("--basepath", $Basepath)
            }

            # Run the pattern loading script
            & python $Args
            if ($LASTEXITCODE -eq 0) {
                Write-Host "$(Get-Timestamp) - [OK] Pattern '$Pattern' loaded successfully"
            } else {
                Write-Host "$(Get-Timestamp) - [FAIL] Failed to load pattern '$Pattern'"
                Write-Host "$(Get-Timestamp) - Continuing with next pattern..."
            }

            # Sleep between patterns (but not after the last pattern in a cycle)
            if ($i -lt ($PatternCount - 1)) {
                Write-Host "$(Get-Timestamp) - Sleeping $Delay seconds before next pattern..."
                Start-Sleep -Seconds $Delay
            }

            Write-Host ""
        }

        Write-Host "$(Get-Timestamp) - Completed cycle $CycleCount, starting next cycle..."
        Write-Host ""
    }
}
finally {
    Write-Host ""
    Write-Host "$(Get-Timestamp) - Pattern cycling stopped by user"
}
