#pragma once

#include "phoneme/media/MediaAdapter.hpp"
#include "phoneme/vm/Value.hpp"

namespace phoneme::vm {

class Machine;

[[nodiscard]] Status dispatch_media_event(
    Machine& machine,
    ObjectRef player,
    const media::MediaEvent& event);

} // namespace phoneme::vm
