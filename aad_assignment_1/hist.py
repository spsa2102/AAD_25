import matplotlib.pyplot as plt
import subprocess
import sys
import re
import numpy as np

def get_kernel_times(source):
    """
    Parses execution times from a file or a subprocess output.
    Looks for lines formatted as: KERNEL_TIME: <float>
    """
    times = []
    pattern = re.compile(r"KERNEL_TIME:\s*(\d+\.?\d*)")
    
    # If source is a list of strings (from subprocess output)
    if isinstance(source, list):
        iterator = source
    # If source is a file path
    else:
        try:
            with open(source, 'r') as f:
                iterator = f.readlines()
        except FileNotFoundError:
            print(f"Error: File {source} not found.")
            return []

    for line in iterator:
        match = pattern.search(line)
        if match:
            try:
                val = float(match.group(1))
                times.append(val)
            except ValueError:
                continue
    return times

def run_cuda_executable(exec_path, args=[]):
    """
    Runs the CUDA executable and captures stdout.
    """
    cmd = [exec_path] + args
    print(f"Running: {' '.join(cmd)}")
    try:
        # Run process and capture output
        result = subprocess.run(
            cmd, 
            stdout=subprocess.PIPE, 
            stderr=subprocess.PIPE, 
            text=True,
            check=True
        )
        return result.stdout.splitlines()
    except subprocess.CalledProcessError as e:
        print(f"Error running executable: {e}")
        print("Stderr:", e.stderr)
        return []
    except FileNotFoundError:
        print(f"Executable not found at: {exec_path}")
        return []

def plot_histogram(times, output_file="kernel_time_histogram.png"):
    if not times:
        print("No timing data found to plot.")
        return

    plt.figure(figsize=(10, 6))
    
    # Create histogram
    # Using 'auto' bins to let matplotlib decide, or specify a number like 50
    counts, bins, patches = plt.hist(times, bins=50, color='skyblue', edgecolor='black', alpha=0.7)
    
    # Add labels and title
    plt.title(f'Distribution of CUDA Kernel Execution Times\n(N={len(times)} samples)')
    plt.xlabel('Execution Time (milliseconds)')
    plt.ylabel('Frequency')
    
    # Add mean/median lines
    mean_val = np.mean(times)
    median_val = np.median(times)
    plt.axvline(mean_val, color='red', linestyle='dashed', linewidth=1, label=f'Mean: {mean_val:.2f} ms')
    plt.axvline(median_val, color='green', linestyle='dashed', linewidth=1, label=f'Median: {median_val:.2f} ms')
    
    plt.legend()
    plt.grid(axis='y', alpha=0.5)
    
    plt.savefig(output_file)
    print(f"Histogram saved to {output_file}")
    plt.close()

if __name__ == "__main__":
    # CONFIGURATION
    # Set this to True if you want to run the binary directly from Python
    RUN_BINARY = False
    EXECUTABLE_PATH = "./search_cuda"
    EXECUTABLE_ARGS = ["-s", "TEST", "100"] # Example: search for "TEST", 100 batches
    
    # Set this to the path of a log file if you ran it manually: ./search_cuda > output.log
    LOG_FILE_PATH = "output.log"

    data = []

    if len(sys.argv) > 1:
        # If arguments provided, assume it's a log file
        data = get_kernel_times(sys.argv[1])
    elif RUN_BINARY:
        # Run the binary
        lines = run_cuda_executable(EXECUTABLE_PATH, EXECUTABLE_ARGS)
        data = get_kernel_times(lines)
    else:
        # Default: Try to read from default log file
        print(f"No arguments provided. Attempting to read from {LOG_FILE_PATH}...")
        data = get_kernel_times(LOG_FILE_PATH)
        
        # DEMO MODE: If no data found, generate dummy data for demonstration
        if not data:
            print("\n[Demo Mode] No data found. Generating dummy data to show plot capabilities...")
            # Simulate a normal distribution around 50ms with some outliers
            data = np.random.normal(loc=50.0, scale=2.0, size=1000).tolist()
            # Add some occasional slower runs (e.g., thermal throttling or system load)
            data.extend(np.random.normal(loc=60.0, scale=5.0, size=50).tolist())

    if data:
        print(f"Collected {len(data)} data points.")
        print(f"Min: {min(data):.2f} ms, Max: {max(data):.2f} ms")
        plot_histogram(data)
    else:
        print("Could not obtain data.")