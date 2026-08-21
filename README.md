# Absolute Power

Absolute Power is a Starfield SFSE plugin for priority-based ship-power presets and
optional event-driven reallocation. It is part of the Absolute flight-control suite.

## Absolute suite

- **Absolute Control** — the optional native PauseMenu configuration host. Power registers
  provider-owned Presets, an Automation / Cheats **Coming Soon** safety preview, and Diagnostics
  when it is present.
- [Absolute Workbench](https://github.com/SultanDesync/Absolute-Workbench) — the earlier ImGui
  frontend and UX prototype; it is not the production menu host.
- [AbsoluteHOTAS](https://github.com/SultanDesync/AbsoluteHOTAS) — HOTAS/HOSAS Power tab
  and device-button/POV command host.
- [Absolute Head Tracking](https://github.com/SultanDesync/Absolute-Head-Tracking) —
  standalone OpenTrack cockpit camera control.
- [AbsoluteZero Ship Control](https://github.com/SultanDesync/AbsoluteZero-Ship-Control) —
  lightweight mouse-alignment assistance.

Absolute Power is a standalone runtime. It loads and executes manually authored or
installed configuration without a presentation mod. Absolute Control adds the primary native
keyboard/mouse editor; AbsoluteHOTAS adds the HOTAS-native interface and device mappings.
The local AP build owns keyboard shortcut persistence and edge execution; Control only
captures and edits those records through the Power API. Established bindings therefore
remain usable without the shared UI. When both frontends are installed they share the same
Power runtime, presets, and exported commands; UI arbitration does not disable the backend.

The current Control surface is the compact reference for compound subscriber UI: semantic section
headers, inline profile actions, keyboard and Input Bus bindings, and a six-row 12-pip allocation
grid with 1/2/3 tier glyphs, direct four-state pip cycling, quick-step buttons, and explicitly
associated per-row priority Choices. Every edit remains in Power's ordinary Apply/Cancel draft.
Older hosts receive bounded fallbacks rather than inferred layout.

Configuration precedence is `AbsolutePower.ini`, then filename-sorted
`AbsolutePower_Imports\*.ini` preset packs, then `AbsolutePower_Custom.ini`. A pack can
therefore be installed as its own mod without replacing Power or the user's settings.

Absolute Power ships headless-friendly number-row bindings for its four default profiles:

```ini
[KeyboardPresetBindings]
Balanced=1
Combat=2
Travel=3
Stealth=4
```

Manual overrides use the user-owned custom file:

```ini
[KeyboardPresetBindings]
Balanced=Ctrl+Shift+F8
Travel=NumpadAdd
```

Supported names include letters, digits, `F1`–`F24`, common navigation keys, numpad keys,
and `VKxx` hexadecimal virtual-key notation. `None` masks a lower-precedence binding.

## Bootstrap status

This repository is a buildable `0.2.0-alpha` foundation. It currently includes:

- a deterministic Green -> Yellow -> Red pip allocator;
- strict tier barriers and round-robin tie breaking;
- releases-first native change planning;
- opt-in weapon, incoming-damage, throttle, and manual automation policy;
- shipped/default plus user-overlay configuration conventions;
- a C-compatible API for Absolute Workbench and AbsoluteHOTAS;
- a fail-optional Absolute Control provider adapter using the native host ABI;
- a fail-optional Absolute Input Bus client for Power-owned joystick/throttle button and POV
  preset shortcuts;
- load-order-independent discovery and ABI validation of the optional Workbench and
  AbsoluteHOTAS interfaces;
- exact Starfield 1.16.244.0 native power-setter signature gates; and
- standalone tests for allocation policy, automation timing, ABI shape, positional grid
  normalization, and shortcut capability/failure boundaries.

The live equipment-component lookup and shared-reference lifetime sequence recovered by
the research harness is promoted intact. Preset requests are queued from either input
host—or from configured standalone startup behavior—and executed by a
deduplicated one-shot SFSE game task. Every lookup, layout, identity, setter, and
final-release sequence remains exact-gated to Starfield 1.16.244.0. Promotion closes the
implementation gap; integrated in-game qualification remains a release gate.

The first automation research slice exists, but it is not release-qualified. Its successful
Weapon 1 path exposed unresolved cross-weapon identity, demand-settlement, and user-policy issues.
The early Control surface therefore withholds the rule editor and exposes only truthful status plus
an emergency Disable All action. Headless defaults remain disabled while the feature is redesigned
around On-Demand Power and possible Auto Combat Mode.

## Priority presets

Each installed ship system receives a number of pips in three tiers:

- **Green** — allocate first.
- **Yellow** — allocate only after every requested Green pip is populated.
- **Red** — allocate only after every requested Green and Yellow pip is populated.

When a reactor cannot complete a tier, the allocator shares that tier one pip at a time
in the preset's configured system order and stops. Uncolored capacity remains available.
Preset counts are clipped to the current ship's installed subsystem maximum, so one
preset can travel between ships and tolerate an empty weapon slot.

The shipped **Stealth** profile requests one Green pip for Engines and Shields and zero
for Weapons and Grav Drive. It leaves nearly all reactor power unassigned while retaining
minimal mobility and protection, and remains editable through the headless INI or Control.

## Automation / Cheats

The experimental backend can reserve emergency pips before the Green tier, but its general rule
surface is not part of the early supported release. The stable page ID remains registered as
**Automation / Cheats (Coming Soon)** with a read-only readiness explanation and an immediate
Disable All action. Do not treat the current INI rule schema as a frozen SDK contract.

## Build

```powershell
xmake
xmake test
```

The build stages the DLL, default INI, and legacy Workbench manifest under
`contrib/PluginRelease/Data/SFSE/Plugins`. Set `xmake f --deploydir=...` or the
`ABSOLUTE_POWER_DEPLOY_DIR` environment variable for local deployment.

See [Architecture](docs/ARCHITECTURE.md), [research provenance](docs/RESEARCH-PROVENANCE.md),
[suite integration](docs/SUITE-INTEGRATION.md), the
[Absolute Control integration checkpoint](docs/CONTROL-PANEL-INTEGRATION.md), and the
[Power frontend UX handoff](docs/WORKBENCH-HANDOFF.md).
