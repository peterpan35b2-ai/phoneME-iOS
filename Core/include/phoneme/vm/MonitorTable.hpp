#pragma once

#include <mutex>
#include <unordered_map>
#include <vector>

#include "phoneme/vm/Value.hpp"

namespace phoneme::vm {

using JavaThreadId = u32;

enum class MonitorEnterResult : u8 {
    acquired,
    would_block,
};

struct MonitorSnapshot final {
    JavaThreadId owner {0};
    u32 recursion {0};
    u32 waiters {0};
};

class MonitorTable final {
public:
    [[nodiscard]] Result<MonitorEnterResult> enter(ObjectRef object,
                                                   JavaThreadId thread_id);
    [[nodiscard]] Status exit(ObjectRef object, JavaThreadId thread_id);
    [[nodiscard]] Result<MonitorSnapshot> snapshot(ObjectRef object) const;
    void append_reference_roots(std::vector<ObjectRef>& roots) const;
    void release_all(JavaThreadId thread_id) noexcept;
    void clear() noexcept;

private:
    struct Monitor final {
        JavaThreadId owner {0};
        u32 recursion {0};
        u32 waiters {0};
    };

    mutable std::mutex mutex_;
    std::unordered_map<u64, Monitor> monitors_;
};

} // namespace phoneme::vm
