set_project("AbsolutePower")
set_version("0.3.0")
set_languages("c++23")
set_warnings("all")

add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN", "WINVER=0x0A00", "_WIN32_WINNT=0x0A00")
add_rules("mode.debug", "mode.releasedbg")

option("deploydir")
    set_default("")
    set_showmenu(true)
    set_description("SFSE/Plugins folder used for live-test deployment")
option_end()

target("AbsolutePower")
    set_kind("shared")
    add_includedirs("include")
    add_files("src/**.cpp")
    set_pcxxheader("include/PCH.h")
    add_defines("ABSOLUTE_POWER_EXPORTS")
    add_defines(
        "PLUGIN_VERSION_MAJOR=0",
        "PLUGIN_VERSION_MINOR=3",
        "PLUGIN_VERSION_PATCH=0",
        "PLUGIN_VERSION_PRERELEASE=alpha"
    )
    add_syslinks("version", "user32")

    after_build(function (target)
        import("core.project.config")
        local layout = (config.get("mode") == "debug") and "PluginDebug" or "PluginRelease"
        local stage = path.join(os.projectdir(), "contrib", layout, "Data", "SFSE", "Plugins")
        local dllsrc = target:targetfile()
        local inisrc = path.join(os.projectdir(), "config", "AbsolutePower.ini")
        local manifestsrc = path.join(os.projectdir(), "config", "AbsolutePower.workbench.json")
        os.mkdir(stage)
        os.cp(dllsrc, path.join(stage, "AbsolutePower.dll"))
        os.cp(inisrc, path.join(stage, "AbsolutePower.ini"))
        os.cp(manifestsrc, path.join(stage, "AbsolutePower.workbench.json"))
        print("Staged AbsolutePower DLL, defaults, and Workbench manifest -> " .. stage)

        local destination = config.get("deploydir")
        if not destination or destination == "" then
            destination = os.getenv("ABSOLUTE_POWER_DEPLOY_DIR")
        end
        if destination and destination ~= "" then
            os.mkdir(destination)
            os.cp(dllsrc, path.join(destination, "AbsolutePower.dll"))
            os.cp(inisrc, path.join(destination, "AbsolutePower.ini"))
            os.cp(manifestsrc, path.join(destination, "AbsolutePower.workbench.json"))
            print("Deployed AbsolutePower -> " .. destination)
        end
    end)

target("power_allocator_test")
    set_kind("binary")
    set_default(false)
    add_tests("tiered_allocator")
    add_includedirs("include")
    add_files("tests/power_allocator_test.cpp", "src/PowerAllocator.cpp")

target("power_grid_editing_test")
    set_kind("binary")
    set_default(false)
    add_tests("positional_segment_normalization")
    add_includedirs("include")
    add_files("tests/power_grid_editing_test.cpp", "src/PowerGridEditing.cpp")

target("power_service_test")
    set_kind("binary")
    set_default(false)
    add_tests("settled_one_pip_transaction")
    add_includedirs("include")
    add_files("tests/power_service_test.cpp", "src/PowerService.cpp", "src/PowerAllocator.cpp")

target("automation_test")
    set_kind("binary")
    set_default(false)
    add_tests("automation_rules")
    add_includedirs("include")
    add_files("tests/automation_test.cpp", "src/Automation.cpp")

target("weapon_fire_event_test")
    set_kind("binary")
    set_default(false)
    add_tests("weapon_fire_event_decode")
    add_includedirs("include")
    add_files("tests/weapon_fire_event_test.cpp")

target("native_power_mapping_test")
    set_kind("binary")
    set_default(false)
    add_tests("all_native_power_pool_mappings")
    add_includedirs("include")
    add_files("tests/native_power_mapping_test.cpp")

target("suite_api_test")
    set_kind("binary")
    set_default(false)
    add_tests("abi_layout")
    add_includedirs("include")
    add_files("tests/suite_api_test.cpp")

target("keyboard_shortcut_test")
    set_kind("binary")
    set_default(false)
    add_tests("keyboard_shortcut_policy")
    add_includedirs("include")
    add_files("tests/keyboard_shortcut_test.cpp", "src/KeyboardShortcut.cpp")

target("joystick_shortcut_test")
    set_kind("binary")
    set_default(false)
    add_tests("joystick_shortcut_policy")
    add_includedirs("include")
    add_files("tests/joystick_shortcut_test.cpp", "src/JoystickShortcut.cpp", "src/InputBusClient.cpp", "src/RuntimePaths.cpp")

target("configuration_test")
    set_kind("binary")
    set_default(false)
    set_rundir(os.projectdir())
    add_tests("default_schema")
    add_includedirs("include")
    add_files("tests/configuration_test.cpp", "src/Configuration.cpp", "src/KeyboardShortcut.cpp", "src/JoystickShortcut.cpp")
    add_syslinks("user32")
