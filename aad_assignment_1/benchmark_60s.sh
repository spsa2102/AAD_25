#!/bin/bash

#
# 60-Second Benchmark Suite - AAD Assignment 1
# Runs all implementations for 60 seconds and compares performance
#

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

# Create timestamp
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
RESULTS_DIR="benchmark_results_${TIMESTAMP}"
mkdir -p "$RESULTS_DIR"

echo -e "${BLUE}"
echo "════════════════════════════════════════════════════════"
echo "   60-Second Benchmark - AAD Assignment 1"
echo "════════════════════════════════════════════════════════"
echo -e "${NC}"
echo "Results will be saved to: $RESULTS_DIR"
echo ""

# Array to store results
declare -A results
declare -A times

# Function to run a benchmark
run_benchmark() {
    local name=$1
    local binary=$2
    local args=$3
    local duration=60
    
    if [ ! -f "$binary" ]; then
        echo -e "${RED}✗ $binary not found${NC}"
        results[$name]="NOT_FOUND"
        return 1
    fi
    
    echo -e "${CYAN}Testing: $name${NC}"
    echo "Command: ./$binary $args"
    
    local logfile="$RESULTS_DIR/${name}.log"
    local start_time=$(date +%s%N)
    
    # Run for 60 seconds, then send SIGINT
    timeout 61 ./$binary $args > "$logfile" 2>&1 || true
    
    local end_time=$(date +%s%N)
    local elapsed=$((($end_time - $start_time) / 1000000))  # Convert to milliseconds
    times[$name]=$elapsed
    
    # Parse results from log
    local hash_rate=$(grep -oP '(?<=Speed: )[\d.]+(?= MH)' "$logfile" | tail -1)
    local attempts=$(grep -oP '(?<=attempts=)\d+' "$logfile" | tail -1)
    local coins=$(grep -oP '(?<=coins=)\d+' "$logfile" | tail -1)
    
    if [ -z "$hash_rate" ]; then
        hash_rate=$(grep -oP '[\d.]+(?= MH/s)' "$logfile" | tail -1)
    fi
    
    if [ -z "$attempts" ]; then
        attempts=$(grep -oP '[\d.]+(?= M/min)' "$logfile" | head -1 | xargs -I {} python3 -c "print(int({} * 1000000 / 60))")
    fi
    
    # Store result
    results[$name]="hash_rate=$hash_rate;attempts=$attempts;coins=$coins;elapsed=$elapsed"
    
    echo -e "${GREEN}✓ Completed${NC}"
    if [ -n "$hash_rate" ]; then
        echo "  Speed: $hash_rate MH/s"
    fi
    if [ -n "$coins" ]; then
        echo "  Coins found: $coins"
    fi
    echo "  Time: ${elapsed}ms"
    echo ""
}

# Run all benchmarks
echo -e "${YELLOW}Starting benchmarks (60 seconds each)...${NC}\n"

run_benchmark "CPU_Search" "cpu_search" "1000000000"
run_benchmark "AVX_Search" "avx_search" "1000000000"
run_benchmark "AVX2_Search" "avx2_search" "1000000000"
run_benchmark "SIMD_OpenMP" "simd_openmp_search" "1000000000"

# CUDA only if available
if command -v nvidia-smi &> /dev/null; then
    run_benchmark "CUDA_Search" "cuda_search" "1000000000"
else
    echo -e "${YELLOW}⊘ CUDA not available (nvidia-smi not found)${NC}\n"
    results["CUDA_Search"]="NOT_AVAILABLE"
fi

# OpenCL with 60 second timeout
run_benchmark "OpenCL_Search" "opencl_search" "1000000000"

# Generate comparison report
echo -e "${BLUE}"
echo "════════════════════════════════════════════════════════"
echo "                    RESULTS SUMMARY"
echo "════════════════════════════════════════════════════════"
echo -e "${NC}\n"

# Create comparison table
echo "Implementation Performance Comparison:"
echo "────────────────────────────────────────────────────────────"
printf "%-20s %-15s %-12s %-12s\n" "Implementation" "Speed (MH/s)" "Coins Found" "Time (s)"
echo "────────────────────────────────────────────────────────────"

declare -A speeds
max_speed=0

for name in "CPU_Search" "AVX_Search" "AVX2_Search" "SIMD_OpenMP" "CUDA_Search" "OpenCL_Search"; do
    result=${results[$name]}
    
    if [ "$result" = "NOT_FOUND" ] || [ "$result" = "NOT_AVAILABLE" ]; then
        printf "%-20s %-15s %-12s %-12s\n" "$name" "$result" "-" "-"
    else
        # Parse the result string
        hash_rate=$(echo "$result" | grep -oP '(?<=hash_rate=)[^;]+')
        coins=$(echo "$result" | grep -oP '(?<=coins=)[^;]+')
        elapsed=$(echo "$result" | grep -oP '(?<=elapsed=)\d+')
        
        # Convert ms to seconds for display
        elapsed_sec=$(echo "scale=2; $elapsed / 1000" | bc)
        
        if [ -z "$hash_rate" ] || [ "$hash_rate" = "None" ]; then
            printf "%-20s %-15s %-12s %-12s\n" "$name" "N/A" "$coins" "$elapsed_sec"
        else
            speeds[$name]=$hash_rate
            printf "%-20s %-15s %-12s %-12s\n" "$name" "$hash_rate" "$coins" "$elapsed_sec"
            
            # Track max speed
            max_speed_cmp=$(echo "$hash_rate > $max_speed" | bc)
            if [ "$max_speed_cmp" = "1" ]; then
                max_speed=$hash_rate
                fastest=$name
            fi
        fi
    fi
done

echo "────────────────────────────────────────────────────────────"
echo ""

# Summary statistics
if [ -n "$fastest" ] && [ "$max_speed" != "0" ]; then
    echo -e "${GREEN}Fastest Implementation:${NC} $fastest ($max_speed MH/s)"
    echo ""
    echo "Relative Performance (vs Fastest):"
    echo "────────────────────────────────────"
    for name in "CPU_Search" "AVX_Search" "AVX2_Search" "SIMD_OpenMP" "CUDA_Search" "OpenCL_Search"; do
        if [ -n "${speeds[$name]}" ]; then
            speedup=$(echo "scale=2; ${speeds[$name]} / $max_speed" | bc)
            printf "%-20s : %.2fx\n" "$name" "$speedup"
        fi
    done
    echo ""
fi

# Save summary to file
{
    echo "60-Second Benchmark Results - $TIMESTAMP"
    echo "=================================================="
    echo ""
    echo "Detailed Results:"
    for name in "CPU_Search" "AVX_Search" "AVX2_Search" "SIMD_OpenMP" "CUDA_Search" "OpenCL_Search"; do
        echo ""
        echo "[$name]"
        echo "${results[$name]}"
    done
    echo ""
    echo "Log files:"
    ls -lh "$RESULTS_DIR"/*.log 2>/dev/null || echo "No log files found"
} | tee "$RESULTS_DIR/summary.txt"

echo ""
echo -e "${GREEN}✓ Benchmark complete. Results saved to: $RESULTS_DIR${NC}"
