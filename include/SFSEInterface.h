#pragma once

// Minimal SFSE load/version ABI, kept byte-compatible with the standalone
// interface used by AbsoluteHOTAS. AbsolutePower does not require CommonLibSF.

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string_view>

namespace REL {
class Version {
public:
    using value_type = std::uint16_t;

    constexpr Version() noexcept = default;
    constexpr Version(value_type major, value_type minor = 0, value_type patch = 0,
                      value_type build = 0) noexcept
        : values_{major, minor, patch, build} {}

    [[nodiscard]] constexpr std::uint32_t pack() const noexcept {
        return static_cast<std::uint32_t>((values_[0] & 0x0FFu) << 24 |
                                          (values_[1] & 0x0FFu) << 16 |
                                          (values_[2] & 0xFFFu) << 4 |
                                          (values_[3] & 0x00Fu));
    }

private:
    std::array<value_type, 4> values_{};
};
} // namespace REL

namespace SFSE {
using PluginHandle = std::uint32_t;

namespace Impl {
struct SFSEInterface {
    std::uint32_t sfseVersion;
    std::uint32_t runtimeVersion;
    std::uint32_t interfaceVersion;
    void* (*queryInterface)(std::uint32_t);
    PluginHandle (*getPluginHandle)();
    const void* (*getPluginInfo)(const char*);
};
struct SFSEMessagingInterface {
    std::uint32_t interfaceVersion;
    bool (*registerListener)(PluginHandle, const char*, void*);
    bool (*dispatch)(PluginHandle, std::uint32_t, void*, std::uint32_t, const char*);
};
struct SFSETaskInterface {
    std::uint32_t interfaceVersion;
    void (*addTask)(void*);
    void (*addPermanentTask)(void*);
};
} // namespace Impl

class ITaskDelegate {
public:
    virtual void Run() = 0;
    virtual void Destroy() = 0;
};

class TaskInterface {
public:
    [[nodiscard]] std::uint32_t Version() const noexcept { return GetProxy().interfaceVersion; }
    void AddTask(ITaskDelegate* task) const noexcept {
        if (task && GetProxy().addTask) {
            GetProxy().addTask(task);
        }
    }
    void AddPermanentTask(ITaskDelegate* task) const noexcept {
        if (task && GetProxy().addPermanentTask) {
            GetProxy().addPermanentTask(task);
        }
    }

private:
    [[nodiscard]] const Impl::SFSETaskInterface& GetProxy() const noexcept {
        return reinterpret_cast<const Impl::SFSETaskInterface&>(*this);
    }
};

class MessagingInterface {
public:
    enum MessageType : std::uint32_t {
        kPostLoad,
        kPostPostLoad,
        kPostDataLoad,
        kPostPostDataLoad,
    };
    struct Message {
        const char* sender;
        std::uint32_t type;
        std::uint32_t dataLength;
        void* data;
    };
    using EventCallback = void (*)(Message*);
    [[nodiscard]] std::uint32_t Version() const noexcept { return GetProxy().interfaceVersion; }
    bool RegisterListener(PluginHandle handle, EventCallback callback) const noexcept {
        return callback && GetProxy().registerListener &&
               GetProxy().registerListener(handle, "SFSE", reinterpret_cast<void*>(callback));
    }
private:
    [[nodiscard]] const Impl::SFSEMessagingInterface& GetProxy() const noexcept {
        return reinterpret_cast<const Impl::SFSEMessagingInterface&>(*this);
    }
};

class LoadInterface {
public:
    [[nodiscard]] PluginHandle GetPluginHandle() const noexcept {
        return GetProxy().getPluginHandle ? GetProxy().getPluginHandle() : 0;
    }
    [[nodiscard]] const MessagingInterface* GetMessagingInterface() const noexcept {
        return GetProxy().queryInterface
                   ? static_cast<const MessagingInterface*>(GetProxy().queryInterface(1))
                   : nullptr;
    }
    [[nodiscard]] const TaskInterface* GetTaskInterface() const noexcept {
        return GetProxy().queryInterface
                   ? static_cast<const TaskInterface*>(GetProxy().queryInterface(4))
                   : nullptr;
    }
private:
    [[nodiscard]] const Impl::SFSEInterface& GetProxy() const noexcept {
        return reinterpret_cast<const Impl::SFSEInterface&>(*this);
    }
};

struct PluginVersionData {
    enum Version : std::uint32_t { kVersion = 1 };

    constexpr void PluginVersion(REL::Version value) noexcept { pluginVersion = value.pack(); }
    constexpr void PluginName(std::string_view value) noexcept {
        SetCharBuffer(value, std::span{pluginName});
    }
    constexpr void AuthorName(std::string_view value) noexcept {
        SetCharBuffer(value, std::span{author});
    }
    constexpr void UsesSigScanning(bool value) noexcept {
        SetOrClearBit(addressIndependence, 1u << 0, value);
    }
    constexpr void UsesAddressLibrary(bool value) noexcept {
        SetOrClearBit(addressIndependence, 1u << 2, value);
    }
    constexpr void HasNoStructUse(bool value) noexcept {
        SetOrClearBit(structureCompatibility, 1u << 0, value);
    }
    constexpr void IsLayoutDependent(bool value) noexcept {
        SetOrClearBit(structureCompatibility, 1u << 3, value);
    }
    constexpr void CompatibleVersions(std::initializer_list<REL::Version> versions) noexcept {
        assert(versions.size() < std::size(compatibleVersions) - 1);
        std::ranges::transform(versions, std::begin(compatibleVersions),
                               [](const REL::Version& value) { return value.pack(); });
    }
    constexpr void MinimumRequiredXSEVersion(REL::Version value) noexcept {
        xseMinimum = value.pack();
    }

    const std::uint32_t dataVersion{kVersion};
    std::uint32_t pluginVersion{};
    char pluginName[256]{};
    char author[256]{};
    std::uint32_t addressIndependence{};
    std::uint32_t structureCompatibility{};
    std::uint32_t compatibleVersions[16]{};
    std::uint32_t xseMinimum{};
    const std::uint32_t reservedNonBreaking{};
    const std::uint32_t reservedBreaking{};

private:
    static constexpr void SetCharBuffer(std::string_view source, std::span<char> destination) {
        assert(source.size() < destination.size());
        std::ranges::fill(destination, '\0');
        std::ranges::copy(source, destination.begin());
    }

    static constexpr void SetOrClearBit(std::uint32_t& data, std::uint32_t bit, bool set) {
        if (set) {
            data |= bit;
        } else {
            data &= ~bit;
        }
    }
};

static_assert(offsetof(PluginVersionData, dataVersion) == 0x000);
static_assert(offsetof(PluginVersionData, pluginVersion) == 0x004);
static_assert(offsetof(PluginVersionData, pluginName) == 0x008);
static_assert(offsetof(PluginVersionData, author) == 0x108);
static_assert(offsetof(PluginVersionData, addressIndependence) == 0x208);
static_assert(offsetof(PluginVersionData, structureCompatibility) == 0x20C);
static_assert(offsetof(PluginVersionData, compatibleVersions) == 0x210);
static_assert(offsetof(PluginVersionData, xseMinimum) == 0x250);
static_assert(sizeof(PluginVersionData) == 0x25C);
} // namespace SFSE

#define SFSE_EXPORT extern "C" [[maybe_unused]] __declspec(dllexport)
#define SFSE_PLUGIN_LOAD(...) SFSE_EXPORT bool SFSEPlugin_Load(__VA_ARGS__)
#define SFSE_PLUGIN_VERSION SFSE_EXPORT constinit SFSE::PluginVersionData SFSEPlugin_Version
