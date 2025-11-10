//
// OpenCL DETI coin search
// Arquiteturas de Alto Desempenho 2025/2026
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#ifdef __APPLE__
#include <OpenCL/opencl.h>
#else
#define CL_TARGET_OPENCL_VERSION 120
#include <CL/cl.h>
#endif

#include "aad_data_types.h"
#include "aad_utilities.h"
#include "aad_sha1_cpu.h"
#include "aad_vault.h"

static volatile sig_atomic_t stop_requested = 0;

static void handle_sigint(int sig)
{
  (void)sig;
  stop_requested = 1;
}

// Error checking helper
static void check_opencl_error(cl_int err, const char *operation)
{
  if(err != CL_SUCCESS)
  {
    fprintf(stderr, "OpenCL error during %s: %d\n", operation, err);
    exit(EXIT_FAILURE);
  }
}

// Load kernel source from file
static char* load_kernel_source(const char *filename, size_t *size)
{
  FILE *fp = fopen(filename, "r");
  if(!fp)
  {
    fprintf(stderr, "Failed to open kernel file: %s\n", filename);
    exit(EXIT_FAILURE);
  }
  
  fseek(fp, 0, SEEK_END);
  size_t file_size = (size_t)ftell(fp);
  rewind(fp);
  
  char *source = (char*)malloc(file_size + 1);
  if(!source)
  {
    fprintf(stderr, "Failed to allocate memory for kernel source\n");
    fclose(fp);
    exit(EXIT_FAILURE);
  }
  
  size_t read_size = fread(source, 1, file_size, fp);
  source[read_size] = '\0';
  fclose(fp);
  
  if(size) *size = file_size;
  return source;
}

int main(int argc, char **argv)
{
  unsigned long long n_batches = 0ULL;
  if(argc > 1)
    n_batches = strtoull(argv[1], NULL, 10);
  
  (void)signal(SIGINT, handle_sigint);
  
  // OpenCL setup
  cl_platform_id platform;
  cl_device_id device;
  cl_context context;
  cl_command_queue queue;
  cl_program program;
  cl_kernel kernel;
  cl_int err;
  
  // Get platform
  err = clGetPlatformIDs(1, &platform, NULL);
  check_opencl_error(err, "clGetPlatformIDs");
  
  // Get device (prefer GPU, fallback to CPU)
  err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device, NULL);
  if(err != CL_SUCCESS)
  {
    fprintf(stderr, "No GPU found, using CPU\n");
    err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_CPU, 1, &device, NULL);
    check_opencl_error(err, "clGetDeviceIDs");
  }
  
  // Print device info
  char device_name[128];
  clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(device_name), device_name, NULL);
  fprintf(stderr, "Using OpenCL device: %s\n", device_name);
  
  // Create context
  context = clCreateContext(NULL, 1, &device, NULL, NULL, &err);
  check_opencl_error(err, "clCreateContext");
  
  // Create command queue
#ifdef CL_VERSION_2_0
  queue = clCreateCommandQueueWithProperties(context, device, NULL, &err);
#else
  queue = clCreateCommandQueue(context, device, 0, &err);
#endif
  check_opencl_error(err, "clCreateCommandQueue");
  
  // Load and compile kernel
  size_t kernel_size;
  char *kernel_source = load_kernel_source("opencl_search_kernel.cl", &kernel_size);
  
  program = clCreateProgramWithSource(context, 1, (const char**)&kernel_source, &kernel_size, &err);
  check_opencl_error(err, "clCreateProgramWithSource");
  
  err = clBuildProgram(program, 1, &device, NULL, NULL, NULL);
  if(err != CL_SUCCESS)
  {
    size_t log_size;
    clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, NULL, &log_size);
    char *log = (char*)malloc(log_size);
    clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, log_size, log, NULL);
    fprintf(stderr, "Kernel compilation error:\n%s\n", log);
    free(log);
    exit(EXIT_FAILURE);
  }
  
  kernel = clCreateKernel(program, "search_coins_kernel", &err);
  check_opencl_error(err, "clCreateKernel");
  
  free(kernel_source);
  
  // Configuration
  const size_t local_work_size = 256;
  const size_t coins_per_batch = 1048576; // 1M coins per batch
  const size_t global_work_size = ((coins_per_batch + local_work_size - 1) / local_work_size) * local_work_size;
  const int max_found = 1024;
  
  // Allocate device buffers
  cl_mem d_found_coins = clCreateBuffer(context, CL_MEM_WRITE_ONLY, 
                                        max_found * 14 * sizeof(u32_t), NULL, &err);
  check_opencl_error(err, "clCreateBuffer found_coins");
  
  cl_mem d_found_count = clCreateBuffer(context, CL_MEM_READ_WRITE, 
                                        sizeof(int), NULL, &err);
  check_opencl_error(err, "clCreateBuffer found_count");
  
  // Host buffers
  u32_t *h_found_coins = (u32_t*)malloc(max_found * 14 * sizeof(u32_t));
  int h_found_count;
  
  // Initialize nonce with random value
  srand((unsigned int)time(NULL));
  unsigned long long base_nonce = ((unsigned long long)rand() << 32) | (unsigned long long)rand();
  unsigned long long batches_done = 0ULL;
  unsigned long long report_interval = 1000ULL;
  double total_elapsed_time = 0.0;
  
  // Initialize time measurement
  time_measurement();
  
  fprintf(stderr, "Starting OpenCL search with %zu coins per batch, local work size %zu\n",
          coins_per_batch, local_work_size);
  
  while((n_batches == 0ULL || batches_done < n_batches) && !stop_requested)
  {
    // Reset found count
    h_found_count = 0;
    err = clEnqueueWriteBuffer(queue, d_found_count, CL_FALSE, 0, 
                               sizeof(int), &h_found_count, 0, NULL, NULL);
    check_opencl_error(err, "clEnqueueWriteBuffer found_count");
    
    // Set kernel arguments
    cl_ulong cl_base_nonce = (cl_ulong)base_nonce;
    cl_ulong cl_num_coins = (cl_ulong)coins_per_batch;
    
    err = clSetKernelArg(kernel, 0, sizeof(cl_ulong), &cl_base_nonce);
    check_opencl_error(err, "clSetKernelArg 0");
    err = clSetKernelArg(kernel, 1, sizeof(cl_ulong), &cl_num_coins);
    check_opencl_error(err, "clSetKernelArg 1");
    err = clSetKernelArg(kernel, 2, sizeof(cl_mem), &d_found_coins);
    check_opencl_error(err, "clSetKernelArg 2");
    err = clSetKernelArg(kernel, 3, sizeof(cl_mem), &d_found_count);
    check_opencl_error(err, "clSetKernelArg 3");
    err = clSetKernelArg(kernel, 4, sizeof(int), &max_found);
    check_opencl_error(err, "clSetKernelArg 4");
    
    // Launch kernel
    err = clEnqueueNDRangeKernel(queue, kernel, 1, NULL, &global_work_size, 
                                 &local_work_size, 0, NULL, NULL);
    check_opencl_error(err, "clEnqueueNDRangeKernel");
    
    // Read back results
    err = clEnqueueReadBuffer(queue, d_found_count, CL_TRUE, 0, 
                              sizeof(int), &h_found_count, 0, NULL, NULL);
    check_opencl_error(err, "clEnqueueReadBuffer found_count");
    
    if(h_found_count > 0)
    {
      if(h_found_count > max_found)
        h_found_count = max_found;
      
      err = clEnqueueReadBuffer(queue, d_found_coins, CL_TRUE, 0, 
                                h_found_count * 14 * sizeof(u32_t), 
                                h_found_coins, 0, NULL, NULL);
      check_opencl_error(err, "clEnqueueReadBuffer found_coins");
      
      // Process found coins
      for(int i = 0; i < h_found_count; i++)
      {
        u32_t *coin = &h_found_coins[i * 14];
        u08_t *coin_bytes = (u08_t*)coin;
        
        // Recompute hash to verify and count leading zeros
        u32_t hash[5];
        sha1(coin, hash);
        
        // Count leading zeros in hash
        unsigned int zeros = 0u;
        for(zeros = 0u; zeros < 128u; ++zeros)
          if(((hash[1u + zeros / 32u] >> (31u - zeros % 32u)) & 1u) != 0u)
            break;
        if(zeros > 99u) zeros = 99u;
        
        printf("Found DETI coin (OpenCL): batch=%llu zeros=%u\n", batches_done, zeros);
        printf("coin: \"");
        for(int b = 0; b < 55; b++)
        {
          unsigned char ch = coin_bytes[b ^ 3];
          if(ch >= 32 && ch <= 126) putchar((int)ch);
          else putchar('?');
        }
        printf("\"\n");
        
        // Print SHA-1
        printf("sha1: ");
        for(int h = 0; h < 20; ++h) printf("%02x", ((unsigned char *)hash)[h ^ 3]);
        printf("\n");
        
        // Save coin
        save_coin(coin);
      }
      
      save_coin(NULL); // Flush
    }
    
    // Advance
    base_nonce += coins_per_batch;
    batches_done++;
    
    if((batches_done % report_interval) == 0ULL)
    {
      time_measurement();
      double delta_time = wall_time_delta();
      total_elapsed_time += delta_time;
      
      double batches_per_second = (double)report_interval / delta_time;
      double iterations_per_second = (double)(report_interval * coins_per_batch) / delta_time;
      
      fprintf(stderr, "batches=%llu nonce=%llu time=%.1f batch_per_sec=%.0f iter_per_sec=%.0f\n",
              batches_done, base_nonce, total_elapsed_time, 
              batches_per_second, iterations_per_second);
    }
  }
  
  // Cleanup
  save_coin(NULL);
  free(h_found_coins);
  clReleaseMemObject(d_found_coins);
  clReleaseMemObject(d_found_count);
  clReleaseKernel(kernel);
  clReleaseProgram(program);
  clReleaseCommandQueue(queue);
  clReleaseContext(context);
  
  return 0;
}
