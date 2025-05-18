#define CL_TARGET_OPENCL_VERSION 220 // Or 120, 200, 210, etc., depending on your OpenCL headers and target
#include <CL/cl.h>
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <stdexcept> // For runtime_error
#include <cmath>     // For fabs, sin, cos

// Note: The "[unknown]" engine in intel_gpu_top for OpenCL compute workloads is common.
// This load generator aims to stress the GPU's execution units.
// The key is whether gpu_monitor.cpp can measure this activity via EuActive/GpuBusy.

const char* kernelSource = R"(
__kernel void load_kernel(__global float* data, const int count) {
    int id = get_global_id(0);
    if (id < count) {
        float val = data[id];
        // Perform a series of calculations to keep the EUs busy
        // The exact nature of these operations is less critical than them being computationally intensive.
        for (int i = 0; i < 1000; ++i) { // Adjust iteration count for more/less load per work-item
            val = val * sin((float)id * 0.01f + (float)i * 0.001f) + cos((float)id * 0.02f - (float)i * 0.002f);
            val = val / (1.0001f + fabs(val)); // Helps keep values bounded and avoid NaNs/denormals
        }
        data[id] = val;
    }
}
)";

void check_cl_error(cl_int err, const char* operation) {
    if (err != CL_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed with error code " + std::to_string(err));
    }
}

void run_load_on_device(cl_platform_id platform, cl_device_id device, int device_index) {
    cl_int err;
    char deviceName[128];
    clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(deviceName), deviceName, NULL);
    std::cout << "Starting load on Device " << device_index << ": " << deviceName << std::endl;

    cl_context_properties props[] = {CL_CONTEXT_PLATFORM, (cl_context_properties)platform, 0};
    cl_context context = clCreateContext(props, 1, &device, nullptr, nullptr, &err);
    check_cl_error(err, "clCreateContext");

    // Create a command queue. clCreateCommandQueue is deprecated in OpenCL 2.0+,
    // but often still available. clCreateCommandQueueWithProperties is preferred.
    cl_command_queue queue = clCreateCommandQueueWithProperties(context, device, 0, &err);
    if (err != CL_SUCCESS) { // Fallback for older OpenCL versions if WithProperties fails
        std::cout << "Device " << device_index << ": clCreateCommandQueueWithProperties failed (" << err << "), trying clCreateCommandQueue." << std::endl;
        queue = clCreateCommandQueue(context, device, 0, &err); // Deprecated in OpenCL 2.0
    }
    check_cl_error(err, "clCreateCommandQueue(WithProperties)");


    cl_program program = clCreateProgramWithSource(context, 1, &kernelSource, nullptr, &err);
    check_cl_error(err, "clCreateProgramWithSource");

    // Try to build for OpenCL 1.2, which is very common.
    // If you need features from newer versions and your hardware/driver supports it, you can change this.
    err = clBuildProgram(program, 1, &device, "-cl-std=CL1.2", nullptr, nullptr);
    if (err != CL_SUCCESS) {
        size_t log_size;
        clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &log_size);
        std::vector<char> log(log_size);
        clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, log_size, log.data(), nullptr);
        std::cerr << "Device " << device_index << " Kernel build log:\n" << log.data() << std::endl;
        check_cl_error(err, "clBuildProgram");
    }

    cl_kernel kernel = clCreateKernel(program, "load_kernel", &err);
    check_cl_error(err, "clCreateKernel");

    // Adjust dataSize based on GPU memory and desired parallelism
    // Larger dataSize means more work items if global_work_size is tied to it.
    const size_t dataSizeElements = 1024 * 1024 * 8; // 8M floats -> 32MB
    std::vector<float> host_data(dataSizeElements);
    for(size_t i = 0; i < dataSizeElements; ++i) host_data[i] = static_cast<float>(i % 100) + 0.1f; // Simple initial data

    cl_mem buffer = clCreateBuffer(context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                                   sizeof(float) * dataSizeElements, host_data.data(), &err);
    check_cl_error(err, "clCreateBuffer");

    err = clSetKernelArg(kernel, 0, sizeof(cl_mem), &buffer);
    check_cl_error(err, "clSetKernelArg(buffer)");
    int count_arg = static_cast<int>(dataSizeElements);
    err = clSetKernelArg(kernel, 1, sizeof(int), &count_arg);
    check_cl_error(err, "clSetKernelArg(count)");

    size_t global_work_size = dataSizeElements;
    // Local work size can be tuned. Query CL_KERNEL_WORK_GROUP_SIZE for optimal values or pass NULL.
    // size_t local_work_size = 256;

    std::cout << "Device " << device_index << ": Entering continuous kernel execution loop..." << std::endl;
    while (true) {
        err = clEnqueueNDRangeKernel(queue, kernel, 1, nullptr, &global_work_size, nullptr /* or &local_work_size */, 0, nullptr, nullptr);
        if (err != CL_SUCCESS) {
            std::cerr << "Device " << device_index << ": clEnqueueNDRangeKernel failed: " << err << std::endl;
            // Consider whether to break or retry. For a continuous load, breaking might be okay if error is persistent.
            break;
        }
        // clFinish ensures the kernel completes before the C++ loop re-enqueues it.
        // This makes the load more "serial" in terms of C++ loop iterations,
        // but the GPU is kept busy during each kernel's execution.
        err = clFinish(queue);
        if (err != CL_SUCCESS) {
            std::cerr << "Device " << device_index << ": clFinish failed: " << err << std::endl;
            break;
        }
        // No sleep needed if you want to keep the GPU as busy as possible by immediately re-queueing.
        // std::this_thread::sleep_for(std::chrono::milliseconds(1)); // Optional small delay
    }

    std::cout << "Device " << device_index << ": Exited kernel execution loop." << std::endl;
    clReleaseMemObject(buffer);
    clReleaseKernel(kernel);
    clReleaseProgram(program);
    clReleaseCommandQueue(queue);
    clReleaseContext(context);
    std::cout << "Finished load and cleaned up for Device " << device_index << std::endl;
}

int main() {
    try {
        cl_uint num_platforms;
        cl_int err = clGetPlatformIDs(0, nullptr, &num_platforms);
        if (err != CL_SUCCESS || num_platforms == 0) {
            std::cerr << "Failed to find any OpenCL platforms or no platforms reported." << std::endl;
            return 1;
        }

        std::vector<cl_platform_id> platforms(num_platforms);
        err = clGetPlatformIDs(num_platforms, platforms.data(), nullptr);
        check_cl_error(err, "clGetPlatformIDs");

        std::vector<std::pair<cl_platform_id, cl_device_id>> intel_gpus_with_platforms;
        for (cl_platform_id platform : platforms) {
            char platformVendor[128]; // Increased size for vendor name
            clGetPlatformInfo(platform, CL_PLATFORM_VENDOR, sizeof(platformVendor), platformVendor, nullptr);

            // Check for "Intel" in vendor string, case-insensitively or be specific
            if (std::string(platformVendor).find("Intel") != std::string::npos ||
                std::string(platformVendor).find("intel") != std::string::npos) {
                cl_uint num_devices;
                err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 0, nullptr, &num_devices);
                if (err == CL_DEVICE_NOT_FOUND) {
                    continue; // No GPU devices on this Intel platform
                }
                // Allow CL_SUCCESS or CL_DEVICE_NOT_FOUND (handled), any other error is problematic
                if (err != CL_SUCCESS) {
                     std::cerr << "Warning: clGetDeviceIDs (count) for platform " << platformVendor << " returned " << err << std::endl;
                     continue;
                }


                if (num_devices > 0) {
                    std::vector<cl_device_id> devices(num_devices);
                    err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, num_devices, devices.data(), nullptr);
                    check_cl_error(err, "clGetDeviceIDs (list)");
                    for (cl_device_id dev : devices) {
                        intel_gpus_with_platforms.push_back({platform, dev});
                    }
                }
            }
        }

        if (intel_gpus_with_platforms.empty()) {
            std::cerr << "No Intel GPUs found via OpenCL." << std::endl;
            return 1;
        }

        std::cout << "Found " << intel_gpus_with_platforms.size() << " Intel GPU(s) via OpenCL." << std::endl;

        std::vector<std::thread> threads;
        int device_idx_counter = 0;
        for (const auto& pair : intel_gpus_with_platforms) {
            threads.emplace_back(run_load_on_device, pair.first, pair.second, device_idx_counter++);
            if (intel_gpus_with_platforms.size() > 1) { // Only stagger if multiple GPUs
                 std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Stagger starts slightly
            }
        }

        for (auto& t : threads) {
            if (t.joinable()) {
                t.join();
            }
        }
    } catch (const std::runtime_error& e) {
        std::cerr << "OpenCL Runtime Error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "An unknown error occurred." << std::endl;
        return 1;
    }

    return 0;
}