#include "AbsolutePowerAPI.h"

#include <cassert>
#include <cstddef>
#include <type_traits>

int main() {
    using namespace AbsolutePowerApi;
    static_assert(std::is_standard_layout_v<ApiV1>);
    static_assert(std::is_trivially_copyable_v<StatusV1>);
    static_assert(std::is_trivially_copyable_v<RuleV1>);
    static_assert(std::is_trivially_copyable_v<PreviewV1>);
    assert(kAbiVersion == 1);
    assert(kSystemCount == 6);
    assert(offsetof(ApiV1, getStatus) > offsetof(ApiV1, version));
    assert(offsetof(ApiV1, previewPreset) > offsetof(ApiV1, reloadConfiguration));
    assert(sizeof(SnapshotV1::systems) / sizeof(SystemStateV1) == kSystemCount);
    return 0;
}
