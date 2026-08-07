set_project("AbsolutePower")
set_version("0.2.0")
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
        "PLUGIN_VERSION_MINOR=2",
        "PLUGIN_VERSION_PATCH=0",
        "PLUGIN_VERSION_PRERELEASE=alpha"
    )
    add_syslinks("version")

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

target("automation_test")
    set_kind("binary")
    set_default(false)
    add_tests("automation_rules")
    add_includedirs("include")
    add_files("tests/automation_test.cpp", "src/Automation.cpp")

target("suite_api_test")
    set_kind("binary")
    set_default(false)
    add_tests("abi_layout")
    add_includedirs("include")
    add_files("tests/suite_api_test.cpp")

target("configuration_test")
    set_kind("binary")
    set_default(false)
    set_rundir(os.projectdir())
    add_tests("default_schema")
    add_includedirs("include")
    add_files("tests/configuration_test.cpp", "src/Configuration.cpp")
    add_syslinks("user32")
