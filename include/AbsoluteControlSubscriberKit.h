#pragma once

// Header-only conveniences for ordinary Absolute Control subscribers. This is
// an SDK implementation aid, not a second ABI: only AbsoluteControlPanelAPI POD
// records and callbacks cross the DLL boundary.

#include "AbsoluteControlPanelAPI.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <mutex>
#include <span>
#include <string_view>

namespace AbsoluteControlSubscriberKit
{
    namespace Api = AbsoluteControlPanelApi;

    template <std::size_t Capacity>
    [[nodiscard]] inline bool CopyText(
        char (&destination)[Capacity], std::string_view source) noexcept
    {
        static_assert(Capacity > 0);
        if (source.size() >= Capacity) return false;
        if (!source.empty()) {
            std::memcpy(destination, source.data(), source.size());
        }
        destination[source.size()] = '\0';
        return true;
    }

    [[nodiscard]] inline Api::ValueV1 Boolean(bool value) noexcept
    {
        Api::ValueV1 result;
        result.kind = Api::ValueKind::Boolean;
        result.booleanValue = value ? 1U : 0U;
        return result;
    }

    [[nodiscard]] inline Api::ValueV1 Integer(std::int64_t value) noexcept
    {
        Api::ValueV1 result;
        result.kind = Api::ValueKind::Integer;
        result.integerValue = value;
        return result;
    }

    [[nodiscard]] inline Api::ValueV1 Float(double value) noexcept
    {
        Api::ValueV1 result;
        result.kind = Api::ValueKind::Float;
        result.floatValue = value;
        return result;
    }

    [[nodiscard]] inline bool String(
        std::string_view value, Api::ValueV1& output) noexcept
    {
        output = {};
        output.kind = Api::ValueKind::String;
        return CopyText(output.stringValue, value);
    }

    [[nodiscard]] inline bool ReadBoolean(
        const Api::ValueV1& value, bool& output) noexcept
    {
        if (value.kind != Api::ValueKind::Boolean || value.booleanValue > 1U) {
            return false;
        }
        output = value.booleanValue != 0;
        return true;
    }

    [[nodiscard]] inline bool ReadInteger(
        const Api::ValueV1& value, std::int64_t minimum,
        std::int64_t maximum, std::int64_t step,
        std::int64_t& output) noexcept
    {
        if (value.kind != Api::ValueKind::Integer || minimum > maximum ||
            step <= 0 || value.integerValue < minimum ||
            value.integerValue > maximum ||
            (value.integerValue - minimum) % step != 0) {
            return false;
        }
        output = value.integerValue;
        return true;
    }

    [[nodiscard]] inline bool ReadFloat(
        const Api::ValueV1& value, double minimum, double maximum,
        double& output) noexcept
    {
        if (value.kind != Api::ValueKind::Float || !std::isfinite(minimum) ||
            !std::isfinite(maximum) || minimum > maximum ||
            !std::isfinite(value.floatValue) ||
            value.floatValue < minimum || value.floatValue > maximum) {
            return false;
        }
        output = value.floatValue;
        return true;
    }

    [[nodiscard]] inline bool ReadString(
        const Api::ValueV1& value, std::string_view& output) noexcept
    {
        if (value.kind != Api::ValueKind::String) return false;
        const auto end = std::find(std::begin(value.stringValue),
            std::end(value.stringValue), '\0');
        if (end == std::end(value.stringValue)) return false;
        output = std::string_view(value.stringValue,
            static_cast<std::size_t>(end - std::begin(value.stringValue)));
        return true;
    }

    [[nodiscard]] inline bool ValidHostApi(const Api::ApiV1* api) noexcept
    {
        constexpr auto required = static_cast<std::uint32_t>(
            offsetof(Api::ApiV1, registerModule) +
            sizeof(api->registerModule));
        return api && api->structSize >= required &&
            api->abiVersion == Api::kAbiVersion && api->moduleId &&
            std::string_view(api->moduleId) == Api::kModuleId &&
            api->registerPage && api->unregisterModule &&
            api->requestRefresh && api->registerModule;
    }

    [[nodiscard]] inline bool Supports(
        const Api::ApiV1* api, std::uint64_t capability) noexcept
    {
        constexpr auto required = static_cast<std::uint32_t>(
            offsetof(Api::ApiV1, capabilities) +
            sizeof(api->capabilities));
        return ValidHostApi(api) && api->structSize >= required &&
            (api->capabilities & capability) == capability;
    }

    [[nodiscard]] inline bool IsOpen(const Api::ApiV1* api) noexcept
    {
        constexpr auto required = static_cast<std::uint32_t>(
            offsetof(Api::ApiV1, isOpen) + sizeof(api->isOpen));
        return ValidHostApi(api) && api->structSize >= required &&
            api->isOpen && api->isOpen() != 0;
    }

    [[nodiscard]] inline bool IsInputCaptureActive(
        const Api::ApiV1* api) noexcept
    {
        constexpr auto required = static_cast<std::uint32_t>(
            offsetof(Api::ApiV1, isInputCaptureActive) +
            sizeof(api->isInputCaptureActive));
        return ValidHostApi(api) && api->structSize >= required &&
            api->isInputCaptureActive && api->isInputCaptureActive() != 0;
    }

    // Reusable transaction mechanics for the common provider model where a
    // draft is applied live, Apply persists it, and Cancel restores the value
    // observed immediately before the first accepted edit. Domain validation
    // and field selection stay in the caller-supplied mutator.
    template <class Configuration>
    class DraftTransaction final
    {
    public:
        using GetCallback = Configuration (*)();
        using ApplyCallback = bool (*)(const Configuration&);
        using SaveCallback = bool (*)();

        struct Access
        {
            GetCallback get{};
            ApplyCallback apply{};
            SaveCallback save{};
        };

        explicit DraftTransaction(Access access) noexcept : access_(access) {}

        template <class Mutator>
        [[nodiscard]] Api::Result Mutate(Mutator&& mutator) noexcept
        {
            if (!access_.get || !access_.apply || !access_.save) {
                return Api::Result::InvalidArgument;
            }
            try {
                std::lock_guard lock(mutex_);
                const Configuration current = access_.get();
                Configuration proposed = current;
                if (!mutator(proposed)) return Api::Result::InvalidArgument;
                // Capture rollback before live application. If copying the
                // configuration throws, provider state has not changed.
                if (!editing_) rollback_ = current;
                if (!access_.apply(proposed)) return Api::Result::WriteFailure;
                editing_ = true;
                return Api::Result::Ok;
            } catch (...) {
                return Api::Result::Rejected;
            }
        }

        [[nodiscard]] Api::Result Commit() noexcept
        {
            if (!access_.save) return Api::Result::InvalidArgument;
            try {
                std::lock_guard lock(mutex_);
                if (!access_.save()) return Api::Result::WriteFailure;
                editing_ = false;
                return Api::Result::Ok;
            } catch (...) {
                return Api::Result::WriteFailure;
            }
        }

        void Cancel() noexcept
        {
            if (!access_.apply) return;
            try {
                std::lock_guard lock(mutex_);
                if (editing_ && !access_.apply(rollback_)) return;
                editing_ = false;
            } catch (...) {
            }
        }

        [[nodiscard]] bool IsEditing() const noexcept
        {
            std::lock_guard lock(mutex_);
            return editing_;
        }

    private:
        Access access_{};
        mutable std::mutex mutex_;
        Configuration rollback_{};
        bool editing_{};
    };

    struct ModulePagesRegistrationResult
    {
        Api::Result result{ Api::Result::InvalidArgument };
        bool rollbackAttempted{};
        // Meaningful only when rollbackAttempted is true.
        Api::Result rollbackResult{ Api::Result::Ok };

        [[nodiscard]] bool RollbackSucceeded() const noexcept
        {
            return rollbackAttempted && rollbackResult == Api::Result::Ok;
        }

        [[nodiscard]] bool PartialRegistrationMayRemain() const noexcept
        {
            return rollbackAttempted && rollbackResult != Api::Result::Ok;
        }
    };

    namespace Detail
    {
        template <std::size_t Capacity>
        [[nodiscard]] inline bool BoundedTextView(
            const char (&text)[Capacity], std::string_view& output) noexcept
        {
            const auto end = std::find(std::begin(text), std::end(text), '\0');
            if (end == std::end(text)) return false;
            output = std::string_view(text,
                static_cast<std::size_t>(end - std::begin(text)));
            return true;
        }

        [[nodiscard]] inline bool ValidModulePages(
            const Api::ModuleDescriptorV1& module,
            std::span<const Api::PageDescriptorV1> pages) noexcept
        {
            std::string_view moduleId;
            if (pages.empty() || !BoundedTextView(module.moduleId, moduleId) ||
                moduleId.empty()) {
                return false;
            }

            for (std::size_t index = 0; index < pages.size(); ++index) {
                std::string_view pageModuleId;
                std::string_view pageId;
                if (!BoundedTextView(pages[index].moduleId, pageModuleId) ||
                    pageModuleId != moduleId ||
                    !BoundedTextView(pages[index].pageId, pageId) ||
                    pageId.empty()) {
                    return false;
                }
                for (std::size_t prior = 0; prior < index; ++prior) {
                    std::string_view priorPageId;
                    if (!BoundedTextView(
                            pages[prior].pageId, priorPageId) ||
                        priorPageId == pageId) {
                        return false;
                    }
                }
            }
            return true;
        }
    }

    // Registers one copied module descriptor followed by its copied pages.
    // Duplicate is always a failure: v1 has no ownership token with which to
    // prove that an existing module or page belongs to this caller. If a page
    // fails after this call registered the module, rollback is attempted and
    // its result is returned separately. This is not an atomic transaction.
    [[nodiscard]] inline ModulePagesRegistrationResult
    RegisterModulePagesDetailed(
        const Api::ApiV1* api, const Api::ModuleDescriptorV1& module,
        std::span<const Api::PageDescriptorV1> pages) noexcept
    {
        if (!ValidHostApi(api) || !Detail::ValidModulePages(module, pages)) {
            return { Api::Result::InvalidArgument, false, Api::Result::Ok };
        }
        try {
            const auto moduleResult = api->registerModule(&module);
            if (moduleResult != Api::Result::Ok) {
                return { moduleResult, false, Api::Result::Ok };
            }
            for (const auto& page : pages) {
                const auto pageResult = api->registerPage(&page);
                if (pageResult != Api::Result::Ok) {
                    const auto rollbackResult =
                        api->unregisterModule(module.moduleId);
                    return { pageResult, true, rollbackResult };
                }
            }
            return { Api::Result::Ok, false, Api::Result::Ok };
        } catch (...) {
            return { Api::Result::Rejected, false, Api::Result::Ok };
        }
    }

    // Compatibility wrapper. Call RegisterModulePagesDetailed when a caller
    // must distinguish successful rollback from possible partial registration.
    [[nodiscard]] inline Api::Result RegisterModulePages(
        const Api::ApiV1* api, const Api::ModuleDescriptorV1& module,
        std::span<const Api::PageDescriptorV1> pages) noexcept
    {
        return RegisterModulePagesDetailed(api, module, pages).result;
    }
}
