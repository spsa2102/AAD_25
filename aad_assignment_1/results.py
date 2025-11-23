#!/usr/bin/env python3
"""
Simple histogram and statistics generator - NO matplotlib required!
Generates text-based histograms and CSV summaries
"""

import csv
import sys
import math
import os

def read_cuda_histogram(filename):
    """Read CUDA histogram CSV file"""
    kernel_times = []
    coins_found = []
    
    if not os.path.exists(filename):
        print(f"Warning: {filename} not found")
        return None, None
    
    try:
        with open(filename, 'r') as f:
            reader = csv.DictReader(f)
            for row in reader:
                try:
                    kernel_times.append(float(row['kernel_time_ms']))
                    coins_found.append(int(row['coins_found']))
                except (ValueError, KeyError):
                    continue
    except Exception as e:
        print(f"Error reading {filename}: {e}")
        return None, None
    
    return kernel_times if kernel_times else None, coins_found if coins_found else None

def read_benchmark_results(filename):
    """Read benchmark results CSV"""
    results = {}
    
    if not os.path.exists(filename):
        print(f"Warning: {filename} not found")
        return None
    
    try:
        with open(filename, 'r') as f:
            reader = csv.DictReader(f)
            for row in reader:
                impl = row['Implementation']
                try:
                    results[impl] = {
                        'attempts': float(row['Attempts']),
                        'per_sec': float(row['Attempts/Second']),
                        'per_min': float(row['Attempts/Minute'])
                    }
                except (ValueError, KeyError):
                    continue
    except Exception as e:
        print(f"Error reading {filename}: {e}")
        return None
    
    return results if results else None

def compute_stats(data):
    """Compute basic statistics"""
    if not data:
        return None
    
    data = sorted(data)
    n = len(data)
    
    stats = {
        'min': min(data),
        'max': max(data),
        'mean': sum(data) / n,
        'median': data[n // 2],
        'count': n
    }
    
    # Standard deviation
    variance = sum((x - stats['mean']) ** 2 for x in data) / n
    stats['stddev'] = math.sqrt(variance)
    
    # Percentiles
    stats['p25'] = data[n // 4]
    stats['p75'] = data[3 * n // 4]
    stats['p95'] = data[int(0.95 * n)]
    
    return stats

def print_stats(name, stats):
    """Print statistics in table format"""
    print(f"\n{name}:")
    print(f"  {'Count':<15} {stats['count']}")
    print(f"  {'Min':<15} {stats['min']:.6f}")
    print(f"  {'Max':<15} {stats['max']:.6f}")
    print(f"  {'Mean':<15} {stats['mean']:.6f}")
    print(f"  {'Median':<15} {stats['median']:.6f}")
    print(f"  {'StdDev':<15} {stats['stddev']:.6f}")
    print(f"  {'P25':<15} {stats['p25']:.6f}")
    print(f"  {'P75':<15} {stats['p75']:.6f}")
    print(f"  {'P95':<15} {stats['p95']:.6f}")

def text_histogram(data, bins=20, width=50):
    """Generate ASCII histogram"""
    if not data:
        return
    
    min_val = min(data)
    max_val = max(data)
    range_val = max_val - min_val
    
    if range_val == 0:
        print(f"All values are {min_val}")
        return
    
    # Create bins
    bin_width = range_val / bins
    histogram = [0] * bins
    
    for val in data:
        if val == max_val:
            idx = bins - 1
        else:
            idx = int((val - min_val) / bin_width)
        histogram[idx] += 1
    
    # Find max count for scaling
    max_count = max(histogram) if histogram else 1
    
    # Print histogram
    print("\nHistogram:")
    for i, count in enumerate(histogram):
        bin_start = min_val + i * bin_width
        bin_end = bin_start + bin_width
        
        # Create bar
        bar_width = int((count / max_count) * width)
        bar = '█' * bar_width
        
        print(f"  {bin_start:8.4f}-{bin_end:8.4f} │{bar:<{width}} {count:4d}")

def benchmark_comparison_text(results):
    """Print benchmark comparison as text table"""
    if not results:
        return
    
    print("\n" + "="*70)
    print("BENCHMARK COMPARISON")
    print("="*70)
    print(f"\n{'Implementation':<20} {'Attempts/Sec':<15} {'Attempts/Min':<20}")
    print("-"*70)
    
    # Sort by performance
    sorted_results = sorted(results.items(), key=lambda x: x[1]['per_sec'], reverse=True)
    
    # Find baseline (CPU)
    baseline_per_sec = None
    for name, data in results.items():
        if 'CPU' in name:
            baseline_per_sec = data['per_sec']
            break
    
    for name, data in sorted_results:
        per_sec = data['per_sec']
        per_min = data['per_min']
        
        # Calculate speedup if baseline exists
        speedup = ""
        if baseline_per_sec and baseline_per_sec > 0:
            factor = per_sec / baseline_per_sec
            speedup = f" ({factor:.2f}x)" if factor > 1.0 else f" ({factor:.2f}x)"
        
        print(f"{name:<20} {per_sec:<15.2e} {per_min:<20.2e}{speedup}")
    
    print("="*70)

def main():
    print("\n" + "="*70)
    print("DETI Coin Search - Results Analysis")
    print("="*70)
    
    # Read benchmark results
    print("\n1. BENCHMARK RESULTS")
    print("-"*70)
    results = read_benchmark_results('benchmark_results.csv')
    
    if results:
        benchmark_comparison_text(results)
        print("\n✓ Benchmark data loaded")
    else:
        print("⚠ No benchmark results found")
        print("  Run: ./benchmark_all")
    
    # Read CUDA histogram
    print("\n2. CUDA HISTOGRAM ANALYSIS")
    print("-"*70)
    kernel_times, coins_found = read_cuda_histogram('cuda_histogram.csv')
    
    if kernel_times:
        time_stats = compute_stats(kernel_times)
        print_stats("Kernel Execution Times (ms)", time_stats)
        text_histogram(kernel_times, bins=20, width=40)
        print(f"\n✓ Kernel timing data loaded ({len(kernel_times)} samples)")
    else:
        print("⚠ No CUDA histogram found")
        print("  Run: ./cuda_histogram")
    
    if coins_found:
        coins_stats = compute_stats(coins_found)
        print_stats("Coins Found Per Run", coins_stats)
        text_histogram(coins_found, bins=max(max(coins_found)-min(coins_found), 1), width=40)
        print(f"\n✓ Coins data loaded ({len(coins_found)} runs)")
    else:
        print("⚠ No coins histogram found")
    
    # Summary statistics
    print("\n" + "="*70)
    print("SUMMARY")
    print("="*70)
    
    if results:
        # Find fastest implementation
        fastest = max(results.items(), key=lambda x: x[1]['per_sec'])
        slowest = min(results.items(), key=lambda x: x[1]['per_sec'])
        
        print(f"\nBenchmark:")
        print(f"  Fastest:  {fastest[0]} ({fastest[1]['per_sec']:.2e} attempts/sec)")
        print(f"  Slowest:  {slowest[0]} ({slowest[1]['per_sec']:.2e} attempts/sec)")
        print(f"  Speedup:  {fastest[1]['per_sec']/slowest[1]['per_sec']:.2f}x")
    
    if kernel_times and coins_found:
        total_coins = sum(coins_found)
        print(f"\nCUDA Analysis ({len(kernel_times)} runs):")
        print(f"  Total coins found: {total_coins}")
        print(f"  Avg coins/run: {total_coins/len(coins_found):.2f}")
        print(f"  Avg kernel time: {time_stats['mean']:.6f} ms")
        print(f"  Kernel time range: {time_stats['min']:.6f} - {time_stats['max']:.6f} ms")
    
    print("\n" + "="*70)
    print("✓ Analysis complete")
    print("="*70 + "\n")

if __name__ == '__main__':
    main()