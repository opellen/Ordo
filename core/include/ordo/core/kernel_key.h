#pragma once

namespace ordo::core {

// Passkey minted only by Kernel: methods taking a KernelKey are publicly
// visible but callable only from Kernel code. Confines the friend surface
// to this empty token instead of opening a whole class to the kernel.
class KernelKey {
    friend class Kernel;
    KernelKey() = default;

public:
    KernelKey(const KernelKey&) = delete;
    KernelKey& operator=(const KernelKey&) = delete;
};

}  // namespace ordo::core
