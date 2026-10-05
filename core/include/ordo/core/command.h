#pragma once

#include <ordo/core/command_context.h>

namespace ordo::core {

// Created fresh for each dispatched EventT (see Kernel::registerCommand).
// Keeps no state of its own; everything it needs comes through the context.
template <typename EventT>
class Command {
public:
    virtual void execute(const EventT& event, CommandContext& context) = 0;
    virtual ~Command() = default;
};

}  // namespace ordo::core
