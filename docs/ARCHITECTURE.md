# Architecture

## Product boundary

Absolute Power owns ship-power policy, persistence semantics, live Starfield power
access, and suite commands. Absolute Workbench owns all rendering, input capture, draft
editing, confirmation, and save UX. AbsoluteHOTAS is optional and may invoke commands;
it does not own the allocator or native power state.

```text
Absolute Workbench (required UI)       AbsoluteHOTAS (optional bindings)
                 \                       /
                  AbsolutePower_QueryApi v1
                              |
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
until released. Disabling automation clears every latched state.

## Safety invariants

- Absolute Workbench is a required dependency; without it the plugin is inert.
- Automation and every shipped rule are disabled by default.
- Runtime-specific functions and layouts are exact-gated to Starfield 1.16.244.0.
- The absolute setter is never called without a freshly resolved, identity-checked part
  owned by the live power-equipment component on the correct game thread.
- Empty display slots are not treated as native parts.
- A failed capture or setter aborts the plan; there is no Windows-input fallback.
- Workbench drafts never mutate live power until the user applies or activates a preset.

## Configuration ownership

`AbsolutePower.ini` is shipped and may be overwritten on update.
`AbsolutePower_Custom.ini` is user-owned and is never shipped. Sections in the custom
file overlay same-ID preset and rule sections from the default file. Absolute Workbench
must write the custom file atomically and then call `reloadConfiguration` through API v1.
