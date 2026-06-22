// Tiny link probe that proves the Emdawnwebgpu webgpu.h symbols are available.
#include <webgpu/webgpu.h>

// Returns null while forcing the linker to resolve the Emdawn WebGPU type.
extern "C" WGPUInstance ofg_emdawnwebgpu_probe_null_instance() {
    return nullptr;
}
