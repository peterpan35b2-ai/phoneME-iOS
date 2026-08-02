#include "phoneme/vm/BuiltinClassRegistry.hpp"

#include <algorithm>
#include <exception>
#include <utility>

namespace phoneme::vm {

void BuiltinClassRegistry::add_factory(Factory factory) {
    if (factory == nullptr) {
        std::terminate();
    }
    if (std::find(factories_.begin(), factories_.end(), factory) !=
        factories_.end()) {
        std::terminate();
    }
    factories_.push_back(factory);
}

BuiltinClassRegistry::ClassPtr BuiltinClassRegistry::find(
    std::string_view internal_name) const {
    ClassPtr match;
    for (Factory factory : factories_) {
        auto built = factory(internal_name);
        if (built == nullptr) {
            continue;
        }
        if (match != nullptr) {
            // Package ownership must be exclusive. A duplicate match means two
            // independently maintained modules declared the same Java class.
            std::terminate();
        }
        match = std::move(built);
    }
    return match;
}

} // namespace phoneme::vm
