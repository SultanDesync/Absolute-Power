# Architecture

## Product boundary

Absolute Power owns ship-power policy, persistence semantics, live Starfield power
access, standalone configuration execution, suite commands, and keyboard activation
shortcuts intrinsic to Power. Absolute Control is the primary supported native
keyboard/mouse editor and capture client. The earlier ImGui Workbench is transitional.
AbsoluteHOTAS owns the HOTAS frontend and
device-button/POV mappings because it owns that physical input runtime. Neither frontend
owns the allocator or native power state, and neither frontend activates or disables the
Power runtime.

The first AP vertical slice replaced Workbench's `[PowerPresetBindings]` store with
Power-owned `[KeyboardPresetBindings]`, additive get/set/clear API callbacks, and headless
edge execution through the existing validated game-update path. No public format migration
was required for the pre-release format. This follows the suite
[headless subscriber contract](<../../Absolute Workbench/docs/HEADLESS-SUBSCRIBER-CONTRACT.md>).

```text
Manual Custom INI    Imported INI packs    Optional interface adapters
          \                 |              /                 \
           \                |   Control (native)      HOTAS (HID)
            \               |           \             /
                         PowerRuntime / config
                              |
               Automation demands (opt-in cheat layer)
                              |
                Green -> Yellow -> Red allocator
                              |
                  releases-first change planner
                              |
               exact-gated NativePowerBackend
                              |
                    Starfield power component
```

## Preset semantics

A system plan stores counts, not individual free-floating colors. Its effective pip row
is therefore monotonic: `GGGYYRR`, never `GYG`. That mirrors the native scalar power
level—raising a system to pip 5 necessarily includes pips 1 through 4.

The allocator works in this order:

1. Validate that the snapshot is internally consistent.
2. Sort active automation demands by descending rule priority.
3. Allocate each demand as an emergency minimum, clipped to the installed maximum.
4. Allocate Green one pip per eligible system per pass using the preset order.
5. Enter Yellow only if all Green requests are satisfied.
6. Enter Red only if all Green and Yellow requests are satisfied.
7. Leave unused reactor output in the available pool.

When applying a result, every decrease is issued before any increase. That avoids
transient over-allocation and matches the native model's available-power accounting.

## Automation semantics

Automation is an overlay, not a second preset system. Active rules produce minimum-pip
demands. Multiple demands for one system collapse naturally to the greatest minimum;
competing systems use descending rule priority. After emergency demands, the active base
preset consumes whatever remains.

Event triggers (`WeaponFired`, `IncomingDamage`) remain active for `HoldMilliseconds`.
`ThrottleAbove` is a level trigger with explicit hysteresis. Manual triggers remain live
until released. The runtime services active demands against the active base preset on its
bounded settlement cadence, one validated pip mutation at a time. When the last event hold
expires, it restores the base preset; while no demand or restoration is active it does not
continuously enforce the preset or overwrite manual power changes. Disabling automation clears
every latched state and completes any required base restoration.

Weapon-fire research currently has two candidate producer paths. Standalone Power read-only hooks
the exact WeaponGroup ButtonEvent listener target, while a direct-native client may call the
optional `recordWeaponFire` API tail after a successful start. Multi-weapon testing showed that the
listener observation occurs too early to prove Starfield matched the specific group. The fixed
rule/settlement policy also failed a cross-weapon journey. Neither path is release-promoted until
group identity, available-first assignment, convergence lifetime, switching, and simultaneous use
are validated end to end.

## Safety invariants

- No presentation/input host is required for configured startup presets. Any redesigned On-Demand
  Power or combat-context source must preserve that same standalone-runtime boundary.
- Power-owned keyboard shortcuts load and execute without a presentation host. Control
  may capture and edit them but cannot be their persistence or execution dependency.
- Automation and every shipped rule are disabled by default.
- Runtime-specific functions and layouts are exact-gated to Starfield 1.16.244.0.
- The absolute setter is never called without a freshly resolved, identity-checked part
  owned by the live power-equipment component on the correct game thread.
- Empty display slots are not treated as native parts.
- A failed capture or setter aborts the plan; there is no Windows-input fallback.
- Control drafts never mutate live power until the user applies or activates a preset.
- A missing, suppressed, or ABI-incompatible frontend cannot disable configuration
  loading, Power-owned bindings, or the standalone executor. It can only remove that
  frontend's editing and capture surface.

## Configuration ownership

`AbsolutePower.ini` is shipped and may be overwritten on update.
`AbsolutePower_Imports/*.ini` contains deterministic filename-ordered preset/rule
overlays and may be supplied by separate mod packages.
`AbsolutePower_Custom.ini` is user-owned and is never shipped. Sections in the custom
file overlay same-ID preset, rule, and keyboard-binding records from both defaults and
imports. `None` masks a lower-precedence keyboard binding. Absolute Power owns this
hierarchy. The native Control adapter keeps shortcut and automation drafts inside Power. The
rich preset/rule editor uses `AbsolutePower_QueryFrontendApi(1)`, a backend-owned,
generation-stamped configuration transaction. Frontends submit bounded intent and render the
structured write/reload/read-back result; only Absolute Power computes minimal overrides,
tombstones, binding cleanup, and source-aware persistence. The transaction preserves unrelated
custom sections and keys and verifies a temporary overlay through the ordinary precedence loader
before atomic replacement.
