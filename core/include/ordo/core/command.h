#pragma once

#include <ordo/core/command_context.h>

namespace ordo::core {

// A transaction object created fresh per dispatched EventT (see
// Kernel::registerCommand). Pure interface: state and helpers live on the
// CommandContext parameter, so a Command is stateless by construction.
template <typename EventT>
class Command {
public:
    virtual void execute(const EventT& event, CommandContext& context) = 0;
    virtual ~Command() = default;
};

}  // namespace ordo::core
