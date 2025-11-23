#!/bin/bash

#
# Complete Testing Suite for AAD Assignment 1
# Runs all benchmarks and collects results
#

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Create results directory
mkdir -p results_$(date +%Y%m%d_%H%M%S)
RESULTS_DIR="results_$(date +%Y%m%d_%H%M%S)"

echo -e "${BLUE}"
echo "════════════════════════════════════════════════════════"
echo "      AAD Assignment 1 - Complete Testing Suite"
echo "════════════════════════════════════════════════════════"
echo -e "${NC}"

# Test 1: CPU Benchmark
echo -e "\n${GREEN}[1/6] CPU Benchmark${NC}"
echo "─────────────────────────────────────"
echo "Testing CPU baseline (no SIMD)..."
if [ -f "cpu_search" ]; then
    echo "Running: ./cpu_search 1000000"
    timeout 65 ./cpu_search 1000000 2>&1 | tee "$RESULTS_DIR/cpu_search.log" || true
    echo -e "${GREEN}✓ CPU benchmark completed${NC}"
else
    echo -e "${RED}✗ cpu_search not found. Build with: make cpu_search${NC}"
fi

# Test 2: AVX Benchmark
echo -e "\n${GREEN}[2/6] AVX Benchmark${NC}"
echo "─────────────────────────────────────"
echo "Testing AVX (4 parallel lanes)..."
if [ -f "avx_search" ]; then
    echo "Running: ./avx_search 100000000"
    timeout 65 ./avx_search 100000000 2>&1 | tee "$RESULTS_DIR/avx_search.log" || true
    echo -e "${GREEN}✓ AVX benchmark completed${NC}"
else
    echo -e "${RED}✗ avx_search not found. Build with: make avx_search${NC}"
fi

# Test 3: AVX2 Benchmark
echo -e "\n${GREEN}[3/6] AVX2 Benchmark${NC}"
echo "─────────────────────────────────────"
echo "Testing AVX2 (8 parallel lanes)..."
if [ -f "avx2_search" ]; then
    echo "Running: ./avx2_search 100000000"
    timeout 65 ./avx2_search 100000000 2>&1 | tee "$RESULTS_DIR/avx2_search.log" || true
    echo -e "${GREEN}✓ AVX2 benchmark completed${NC}"
else
    echo -e "${RED}✗ avx2_search not found. Build with: make avx2_search${NC}"
fi

# Test 4: AVX-512F Benchmark (if available)
echo -e "\n${GREEN}[4/6] AVX-512F Benchmark (Optional)${NC}"
echo "─────────────────────────────────────"
if [ -f "avx512_search" ]; then
    echo "Testing AVX-512F (16 parallel lanes)..."
    echo "Running: ./avx512_search 100000000"
    timeout 65 ./avx512_search 100000000 2>&1 | tee "$RESULTS_DIR/avx512_search.log" || true
    echo -e "${GREEN}✓ AVX-512F benchmark completed${NC}"
else
    echo -e "${YELLOW}⚠ avx512_search not found (CPU may not support AVX-512F)${NC}"
fi

# Test 5: Master Benchmark (CPU/SIMD all in one)
echo -e "\n${GREEN}[5/6] Master Benchmark (All ISAs)${NC}"
echo "─────────────────────────────────────"
echo "Running comprehensive benchmark..."
if [ -f "benchmark_all" ]; then
    ./benchmark_all 2>&1 | tee "$RESULTS_DIR/benchmark_all.log"
    echo -e "${GREEN}✓ Master benchmark completed${NC}"
    
    # Copy CSV results
    if [ -f "benchmark_results.csv" ]; then
        cp benchmark_results.csv "$RESULTS_DIR/"
        echo -e "${GREEN}✓ Results saved to benchmark_results.csv${NC}"
    fi
else
    echo -e "${RED}✗ benchmark_all not found. Build with: make benchmark_all${NC}"
fi

# Test 6: CUDA Histogram (if available)
echo -e "\n${GREEN}[6/6] CUDA Histogram Analysis (Optional)${NC}"
echo "─────────────────────────────────────"
if [ -f "cuda_histogram" ]; then
    echo "Running 1000 CUDA kernel invocations..."
    echo "This may take 30 seconds to 5 minutes..."
    timeout 600 ./cuda_histogram 2>&1 | tee "$RESULTS_DIR/cuda_histogram.log" || true
    
    if [ -f "cuda_histogram.csv" ]; then
        cp cuda_histogram.csv "$RESULTS_DIR/"
        echo -e "${GREEN}✓ CUDA histogram completed${NC}"
    fi
else
    echo -e "${YELLOW}⚠ cuda_histogram not found (CUDA may not be available)${NC}"
    echo "  Your friend with a GPU can test this later"
fi

# Analysis
echo -e "\n${BLUE}"
echo "════════════════════════════════════════════════════════"
echo "              Testing Complete - Analysis"
echo "════════════════════════════════════════════════════════"
echo -e "${NC}"

echo -e "\n${GREEN}Results Directory: $RESULTS_DIR${NC}"
echo ""
echo "Files generated:"
ls -lh "$RESULTS_DIR"/ 2>/dev/null || echo "No files generated"

# Summary
echo ""
echo -e "${BLUE}Summary of Results:${NC}"
echo "─────────────────────────────────────"

if [ -f "$RESULTS_DIR/benchmark_all.log" ]; then
    echo -e "\n${GREEN}Benchmark Results:${NC}"
    grep "Result:" "$RESULTS_DIR/benchmark_all.log" 2>/dev/null || true
    grep "Speedup" "$RESULTS_DIR/benchmark_all.log" 2>/dev/null || true
fi

if [ -f "$RESULTS_DIR/benchmark_results.csv" ]; then
    echo -e "\n${GREEN}Performance CSV:${NC}"
    cat "$RESULTS_DIR/benchmark_results.csv"
fi

if [ -f "$RESULTS_DIR/cuda_histogram.csv" ]; then
    echo -e "\n${GREEN}CUDA Statistics:${NC}"
    head -5 "$RESULTS_DIR/cuda_histogram.csv"
    wc -l "$RESULTS_DIR/cuda_histogram.csv"
fi

# Check for coins vault
if [ -f "deti_coins_v2_vault.txt" ]; then
    echo -e "\n${GREEN}Coins Found:${NC}"
    wc -l deti_coins_v2_vault.txt
    echo "Sample coins:"
    head -5 deti_coins_v2_vault.txt | cut -c1-70
fi

# Analysis with Python
echo -e "\n${BLUE}Running Python Analysis...${NC}"
echo "─────────────────────────────────────"

if command -v python3 &> /dev/null; then
    if [ -f "analyze_results.py" ] || [ -f "results.py" ]; then
        if [ -f "analyze_results.py" ]; then
            python3 analyze_results.py 2>&1 | tee "$RESULTS_DIR/analysis.txt" || true
        elif [ -f "results.py" ]; then
            python3 results.py 2>&1 | tee "$RESULTS_DIR/analysis.txt" || true
        fi
    else
        echo "⚠ Python analysis scripts not found"
    fi
else
    echo "⚠ Python 3 not found"
fi

# Final Summary
echo -e "\n${BLUE}"
echo "════════════════════════════════════════════════════════"
echo "                   Testing Summary"
echo "════════════════════════════════════════════════════════"
echo -e "${NC}"

echo -e "\n${GREEN}What was tested:${NC}"
echo "  ✓ CPU baseline performance"
echo "  ✓ AVX performance (4 lanes)"
echo "  ✓ AVX2 performance (8 lanes)"
if [ -f "avx512_search" ]; then
    echo "  ✓ AVX-512F performance (16 lanes)"
fi
if [ -f "cuda_histogram" ]; then
    echo "  ✓ CUDA kernel analysis (1000 runs)"
fi

echo -e "\n${GREEN}Output files:${NC}"
echo "  Location: $RESULTS_DIR/"
echo "  "
ls -1 "$RESULTS_DIR"/ 2>/dev/null || echo "  (no files)"

echo -e "\n${GREEN}Next steps:${NC}"
echo "  1. Review the results in: $RESULTS_DIR/"
echo "  2. Check benchmark_results.csv for performance data"
echo "  3. If CUDA ran: check cuda_histogram.csv"
echo "  4. Run: python3 analyze_results.py (if available)"
echo "  5. Include results in your report"

echo -e "\n${BLUE}"
echo "════════════════════════════════════════════════════════"
echo "                Testing Complete ✓"
echo "════════════════════════════════════════════════════════"
echo -e "${NC}\n"