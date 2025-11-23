#!/bin/bash

#
# Test Attempts Per Minute - 60 seconds each
# Much simpler: just run benchmark_all which already does this
#

echo ""
echo "════════════════════════════════════════════════════════"
echo "     AAD Assignment 1 - Attempts Per Minute Test"
echo "════════════════════════════════════════════════════════"
echo ""
echo "This will test each implementation for 60 seconds"
echo "and measure attempts per minute."
echo ""

# Check if benchmark_all exists
if [ ! -f "./benchmark_all" ]; then
    echo "Error: benchmark_all not found"
    echo "Build it with: make benchmark_all"
    exit 1
fi

# Run the benchmark
echo "Starting benchmark (takes ~5 minutes)..."
echo "─────────────────────────────────────"
echo ""

./benchmark_all

# Show results
echo ""
echo "════════════════════════════════════════════════════════"
echo "                    Results"
echo "════════════════════════════════════════════════════════"
echo ""

if [ -f "benchmark_results.csv" ]; then
    echo "CSV Results:"
    echo "─────────────────────────────────────"
    cat benchmark_results.csv
    echo ""
    
    echo "Pretty Format:"
    echo "─────────────────────────────────────"
    column -t -s, benchmark_results.csv
    echo ""
    
    # Extract and show attempts per minute
    echo "Attempts Per Minute (Summary):"
    echo "─────────────────────────────────────"
    awk -F',' 'NR>1 {printf "%-20s %12s\n", $1, $4}' benchmark_results.csv
    echo ""
fi

# Check for coins
echo "Coins Found:"
echo "─────────────────────────────────────"
if [ -f "deti_coins_v2_vault.txt" ]; then
    wc -l deti_coins_v2_vault.txt
    echo ""
    echo "Sample coins:"
    head -3 deti_coins_v2_vault.txt | cut -c1-80
else
    echo "No coins found yet (expected - rare events)"
fi

echo ""
echo "════════════════════════════════════════════════════════"
echo "Done! Results are in benchmark_results.csv"
echo "════════════════════════════════════════════════════════"
echo ""