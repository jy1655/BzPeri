#pragma once

#include <string>

namespace bzp::samples {

// Registers the built-in example services underneath the provided namespace node (for example "samples").
// The caller is responsible for clearing any existing configurators if they want to avoid duplicate registrations.
void registerSampleServices(const std::string& namespaceNode);

} // namespace bzp::samples
