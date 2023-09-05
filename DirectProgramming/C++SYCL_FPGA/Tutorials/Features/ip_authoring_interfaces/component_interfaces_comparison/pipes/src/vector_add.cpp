#include <iostream>

// oneAPI headers
#include <sycl/sycl.hpp>
#include <sycl/ext/intel/fpga_extensions.hpp>
#include "exception_handler.hpp"

#define VECT_SIZE 256
// Forward declare the kernel name in the global scope. This is an FPGA best
// practice that reduces name mangling in the optimization reports.
class IDSimpleVAddPipes;
class IDPipeA;
class IDPipeB;
class IDPipeC;

using PipeProps = decltype(sycl::ext::oneapi::experimental::properties(
    sycl::ext::intel::experimental::ready_latency<0>));
using InputPipeA =
    sycl::ext::intel::experimental::pipe<IDPipeA, int, 0, PipeProps>;
using InputPipeB =
    sycl::ext::intel::experimental::pipe<IDPipeB, int, 0, PipeProps>;
using OutputPipeC =
    sycl::ext::intel::experimental::pipe<IDPipeC, int, 0, PipeProps>;

struct SimpleVAddKernelPipes {
  int len;

  void operator()() const {
    for (int idx = 0; idx < len; idx++) {
      int a_val = InputPipeA::read();
      int b_val = InputPipeB::read();
      int sum = a_val + b_val;
      OutputPipeC::write(sum);
    }
  }
};

int main() {
  try {
    // Use compile-time macros to select either:
    //  - the FPGA emulator device (CPU emulation of the FPGA)
    //  - the FPGA device (a real FPGA)
    //  - the simulator device
#if FPGA_SIMULATOR
    auto selector = sycl::ext::intel::fpga_simulator_selector_v;
#elif FPGA_HARDWARE
    auto selector = sycl::ext::intel::fpga_selector_v;
#else  // #if FPGA_EMULATOR
    auto selector = sycl::ext::intel::fpga_emulator_selector_v;
#endif

    // create the device queue
    sycl::queue q(selector, fpga_tools::exception_handler);

    auto device = q.get_device();

    std::cout << "Running on device: "
              << device.get_info<sycl::info::device::name>().c_str()
              << std::endl;

    int count = VECT_SIZE;  // pass array size by value

    // push data into pipes before invoking kernel
    int *a = new int[count];
    int *b = new int[count];
    for (int i = 0; i < count; i++) {
      a[i] = i;
      b[i] = (count - i);

      InputPipeA::write(q, a[i]);
      InputPipeB::write(q, b[i]);
    }

    std::cout << "Add two vectors of size " << count << std::endl;

    q.single_task<IDSimpleVAddPipes>(
         SimpleVAddKernelPipes{count});

    // verify that VC is correct
    bool passed = true;
    for (int i = 0; i < count; i++) {
      int expected = a[i] + b[i];
      int calc = OutputPipeC::read(q);
      if (calc != expected) {
        std::cout << "idx=" << i << ": result " << calc << ", expected ("
                  << expected << ") A=" << a[i] << " + B=" << b[i] << std::endl;
        passed = false;
      }
    }

    std::cout << (passed ? "PASSED" : "FAILED") << std::endl;

    delete[] a;
    delete[] b;

    return passed ? EXIT_SUCCESS : EXIT_FAILURE;

  } catch (sycl::exception const &e) {
    std::cerr << "Caught a synchronous SYCL exception: " << e.what()
              << std::endl;
    std::cerr << "   If you are targeting an FPGA hardware, "
                 "ensure that your system is plugged to an FPGA board that is "
                 "set up correctly"
              << std::endl;
    std::terminate();
  }
}