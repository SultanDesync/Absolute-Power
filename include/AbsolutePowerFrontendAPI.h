#pragma once

// Rich, provider-owned Absolute Power editor ABI. This contract is separate
// from AbsolutePowerApi::ApiV1 so command clients and the validated HOTAS game-
// thread bridge remain stable while the configuration workbench evolves.

#include "AbsolutePowerAPI.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace AbsolutePowerFrontendApi {

inline constexpr std::uint32_t kAbiVersion = 1;
inline constexpr std::size_t kPathCapacity = 1024;
inline constexpr std::size_t kDetailCapacity = 256;
inline constexpr std::uint32_t kMaximumRecords = 256;

enum class Result : std::uint32_t {
    Ok,
    NotReady,
    InvalidArgument,
    NotFound,
    CapacityExceeded,
    StaleGeneration,
    InvalidDraft,
    WriteFailure,
    ReloadFailure,
    VerificationMismatch,
    Rejected,
};

enum class SourceKind : std::uint32_t {
    BuiltIn,
    Defaults,
    Import,
    Custom,
};

struct RecordSourceV1 {
    std::uint32_t structSize{sizeof(RecordSourceV1)};
    SourceKind baseKind{SourceKind::BuiltIn};
    std::uint8_t userOverride{};
    std::uint8_t reserved[3]{};
    char baseLabel[AbsolutePowerApi::kLabelCapacity]{};
    char basePath[kPathCapacity]{};
};

struct ConfigurationInfoV1 {
    std::uint32_t structSize{sizeof(ConfigurationInfoV1)};
    std::uint64_t generation{};
    std::uint32_t presetCount{};
    std::uint32_t ruleCount{};
    std::uint8_t automationEnabled{};
    std::uint8_t reserved[3]{};
    char startupPreset[AbsolutePowerApi::kIdCapacity]{};
    char defaultsPath[kPathCapacity]{};
    char importsPath[kPathCapacity]{};
    char customPath[kPathCapacity]{};
};

struct PresetRecordV1 {
    std::uint32_t structSize{sizeof(PresetRecordV1)};
    AbsolutePowerApi::PresetV1 preset{};
    RecordSourceV1 source{};
    AbsolutePowerApi::KeyboardBindingV1 keyboardBinding{};
    std::uint8_t hasKeyboardBinding{};
    std::uint8_t reserved[7]{};
};

struct RuleRecordV1 {
    std::uint32_t structSize{sizeof(RuleRecordV1)};
    AbsolutePowerApi::RuleV1 rule{};
    RecordSourceV1 source{};
};

// Borrowed input is copied completely before saveConfiguration returns.
// Callers retain ownership and may release it immediately after the call.
struct ConfigurationDraftV1 {
    std::uint32_t structSize{sizeof(ConfigurationDraftV1)};
    std::uint64_t baseGeneration{};
    char startupPreset[AbsolutePowerApi::kIdCapacity]{};
    std::uint8_t automationEnabled{};
    std::uint8_t reserved[3]{};
    std::uint32_t presetCount{};
    const AbsolutePowerApi::PresetV1* presets{};
    std::uint32_t ruleCount{};
    const AbsolutePowerApi::RuleV1* rules{};
};

struct SaveReportV1 {
    std::uint32_t structSize{sizeof(SaveReportV1)};
    Result result{Result::InvalidDraft};
    std::uint64_t generation{};
    char detail[kDetailCapacity]{};
};

enum class ActivationState : std::uint32_t {
    Idle,
    Queued,
    Waiting,
    Settling,
    Converged,
    Failed,
};

struct ActivationStatusV1 {
    std::uint32_t structSize{sizeof(ActivationStatusV1)};
    ActivationState state{ActivationState::Idle};
    AbsolutePowerApi::Result lastResult{AbsolutePowerApi::Result::Ok};
    std::uint32_t totalChanges{};
    std::uint32_t completedChanges{};
    std::uint32_t remainingChanges{};
    std::uint32_t reserved{};
    std::uint64_t sequence{};
    char requestedPreset[AbsolutePowerApi::kIdCapacity]{};
    char activePreset[AbsolutePowerApi::kIdCapacity]{};
};

struct ApiV1 {
    std::uint32_t structSize{sizeof(ApiV1)};
    std::uint32_t abiVersion{kAbiVersion};
    const char* moduleId{};
    const char* version{};
    Result(__cdecl* getConfigurationInfo)(ConfigurationInfoV1*) noexcept{};
    Result(__cdecl* getPresetRecord)(std::uint64_t generation, std::uint32_t index,
                                    PresetRecordV1*) noexcept{};
    Result(__cdecl* getRuleRecord)(std::uint64_t generation, std::uint32_t index,
                                  RuleRecordV1*) noexcept{};
    Result(__cdecl* saveConfiguration)(const ConfigurationDraftV1*,
                                      SaveReportV1*) noexcept{};
    Result(__cdecl* getActivationStatus)(ActivationStatusV1*) noexcept{};
};

static_assert(std::is_standard_layout_v<ApiV1>);
static_assert(std::is_trivially_copyable_v<RecordSourceV1>);
static_assert(std::is_trivially_copyable_v<ConfigurationInfoV1>);
static_assert(std::is_trivially_copyable_v<PresetRecordV1>);
static_assert(std::is_trivially_copyable_v<RuleRecordV1>);
static_assert(std::is_trivially_copyable_v<ConfigurationDraftV1>);
static_assert(std::is_trivially_copyable_v<SaveReportV1>);
static_assert(std::is_trivially_copyable_v<ActivationStatusV1>);

} // namespace AbsolutePowerFrontendApi

#if defined(ABSOLUTE_POWER_EXPORTS)
#define ABSOLUTE_POWER_FRONTEND_API __declspec(dllexport)
#else
#define ABSOLUTE_POWER_FRONTEND_API __declspec(dllimport)
#endif

extern "C" ABSOLUTE_POWER_FRONTEND_API
const AbsolutePowerFrontendApi::ApiV1*
AbsolutePower_QueryFrontendApi(std::uint32_t requestedAbiVersion) noexcept;
