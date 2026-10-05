#pragma once

namespace ordo::core {

// Passkey: only Kernel can construct one, so a public method that takes a
// KernelKey is callable only from Kernel.
class KernelKey {
    friend class Kernel;
    KernelKey() = default;

public:
    KernelKey(const KernelKey&) = delete;
    KernelKey& operator=(const KernelKey&) = delete;
};

}  // namespace ordo::core
