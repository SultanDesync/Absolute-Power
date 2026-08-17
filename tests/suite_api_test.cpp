#include "AbsolutePowerAPI.h"
#include "AbsolutePowerFrontendAPI.h"

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
    assert(offsetof(ApiV1, getKeyboardBinding) > offsetof(ApiV1, processGameThread));
    assert(sizeof(SnapshotV1::systems) / sizeof(SystemStateV1) == kSystemCount);
    static_assert(std::is_trivially_copyable_v<KeyboardBindingV1>);
    static_assert(sizeof(void*) == 8);
    static_assert(sizeof(KeyboardBindingV1) == 72);
    static_assert(offsetof(ApiV1, processGameThread) == 128);
    static_assert(offsetof(ApiV1, getKeyboardBinding) == 136);
    static_assert(offsetof(ApiV1, clearKeyboardBinding) == 152);
    static_assert(offsetof(ApiV1, recordWeaponFire) == 160);
    static_assert(sizeof(ApiV1) == 168);
    assert(static_cast<std::uint32_t>(Result::Conflict) == 9);
    assert(static_cast<std::uint32_t>(Result::WriteFailure) == 10);

    using namespace AbsolutePowerFrontendApi;
    static_assert(std::is_standard_layout_v<AbsolutePowerFrontendApi::ApiV1>);
    static_assert(std::is_trivially_copyable_v<ConfigurationDraftV1>);
    static_assert(std::is_trivially_copyable_v<PresetRecordV1>);
    static_assert(std::is_trivially_copyable_v<RuleRecordV1>);
    static_assert(std::is_trivially_copyable_v<ActivationStatusV1>);
    static_assert(offsetof(AbsolutePowerFrontendApi::ApiV1, getConfigurationInfo) >
                  offsetof(AbsolutePowerFrontendApi::ApiV1, version));
    static_assert(offsetof(AbsolutePowerFrontendApi::ApiV1, saveConfiguration) >
                  offsetof(AbsolutePowerFrontendApi::ApiV1, getRuleRecord));
    static_assert(offsetof(AbsolutePowerFrontendApi::ApiV1, getActivationStatus) >
                  offsetof(AbsolutePowerFrontendApi::ApiV1, saveConfiguration));
    assert(AbsolutePowerFrontendApi::kAbiVersion == 1);
    assert(AbsolutePowerFrontendApi::kMaximumRecords == 256);
    assert(static_cast<std::uint32_t>(AbsolutePowerFrontendApi::Result::StaleGeneration) == 5);
    return 0;
}
