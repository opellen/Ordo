#pragma once

#include <string>
#include <typeinfo>
#include <vector>

#include <ordo/core/dispatcher.h>
#include <ordo/core/kernel.h>

// Two translation units each define an anonymous-namespace `Ping` with a
// different payload. The lane functions expose each one without naming the type.
// Every Ping starts with an int lane tag that handlers check before reading the
// rest, so a cross-delivery is reported instead of reading the wrong payload.

namespace lane_a {
void subscribe(ordo::core::Dispatcher& dispatcher, const void* owner, std::vector<std::string>& log);
void dispatch(ordo::core::Dispatcher& dispatcher);
void registerCommand(ordo::core::Kernel& kernel);
void removeCommand(ordo::core::Kernel& kernel);
void send(ordo::core::Kernel& kernel);
int& commandRuns();
const std::type_info& pingType();
}  // namespace lane_a

namespace lane_b {
void subscribe(ordo::core::Dispatcher& dispatcher, const void* owner, std::vector<std::string>& log);
void dispatch(ordo::core::Dispatcher& dispatcher);
void registerCommand(ordo::core::Kernel& kernel);
void removeCommand(ordo::core::Kernel& kernel);
void send(ordo::core::Kernel& kernel);
int& commandRuns();
const std::type_info& pingType();
}  // namespace lane_b
