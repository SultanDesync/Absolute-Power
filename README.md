# Absolute Power v0.2.0-alpha

Conventional power presets reproduce a fixed distribution, but Starfield ships are not equal. A
layout made for one reactor and loadout may be impossible—or simply wrong—on a ship with less
power, different weapons, or different subsystem limits.

Absolute Power stores a priority plan instead of a snapshot. Essential requests are funded first;
preferred and discretionary systems receive power only after every higher-priority request the
current ship can satisfy has been handled. The same Combat, Travel, or Stealth policy therefore
scales down gracefully on a weaker ship and continues filling out on a stronger one.

The current standalone release is feature-stable for its supported preset workflow. The optional
event-driven Automation / Cheats backend remains disabled and is not part of the supported public
surface yet.

## Features

- Four shipped profiles: **Balanced**, **Combat**, **Travel**, and **Stealth**.
- Three priority tiers per ship system: Green, Yellow, and Red.
- Deterministic allocation with strict tier barriers and round-robin tie breaking.
- Portable presets that clip safely to the current ship's installed subsystem limits.
- Native number-row shortcuts for headless operation without a configuration frontend.
- Optional keyboard or HOTAS/POV preset bindings owned and persisted by Power.
- A full native **Absolute Control** menu with profile selection, startup behavior, bindings,
  direct 12-pip grid editing, per-system tie-break priorities, Apply/Cancel, and diagnostics.
- Fail-optional integration: presets and established shortcuts continue to work when Absolute
  Control, AbsoluteHOTAS, or any legacy frontend is absent.

## Requirements

- Starfield `1.16.244`
- [SFSE](https://sfse.silverlock.org/) `0.2.20` or later
- [Absolute Control](https://github.com/SultanDesync/Absolute-Control) standalone is required for
  the advertised native in-game editor, but is not a gameplay dependency.
- Address Library is **not** required by Absolute Power itself. The separate Absolute Control menu
  host currently requires Address Library for SFSE Plugins.

Native power application is exact-gated to the supported Starfield runtime. If validation fails,
Power leaves the affected operation disabled instead of attempting an unsafe fallback.

## Installation

Install the archive with MO2 or Vortex, or place these files manually:

```text
Data\SFSE\Plugins\AbsolutePower.dll
Data\SFSE\Plugins\AbsolutePower.ini
```

Launch Starfield through SFSE. With Absolute Control installed, open the Pause Menu, select
**MOD OPTIONS**, and choose **Absolute Power**.

## Priority presets

Every ship system can request pips in three tiers:

- **Green** — allocated first.
- **Yellow** — allocated after all requested Green pips.
- **Red** — allocated after all requested Green and Yellow pips.

When a reactor cannot complete a tier, the allocator shares that tier one pip at a time in the
preset's configured system order. Uncolored capacity remains unassigned. This makes one preset
usable across ships without assuming identical reactors or equipment.

The shipped **Stealth** profile requests one Green pip for Engines and Shields and leaves Weapons
and Grav Drive unpowered. It provides minimal mobility and protection while leaving most reactor
power unassigned.

## Default controls

Power owns four headless-friendly number-row shortcuts:

```ini
[KeyboardPresetBindings]
Balanced=1
Combat=2
Travel=3
Stealth=4
```

These are true single-keystroke activations and remain active without a configuration frontend.
Starfield leaves number-row keys `1`–`4` unused by default while piloting, so the shipped layout
provides immediate preset switching without displacing vanilla flight controls.

Change or clear them in Absolute Control, or override them in `AbsolutePower_Custom.ini`:

```ini
[KeyboardPresetBindings]
Balanced=Ctrl+Shift+F8
Travel=NumpadAdd
Stealth=None
```

Bindings may be a single key or a combination using Ctrl, Alt, and Shift. Supported names include
letters, digits, `F1`–`F24`, navigation keys, numpad keys, and `VKxx` hexadecimal virtual-key
notation. When AbsoluteHOTAS is installed and supplies the Absolute Input Bus, the native menu can
instead capture a stable joystick, throttle, button-box, or POV input from a flight-control device.

## Configuration and safe updates

Configuration precedence is:

1. `AbsolutePower.ini` — shipped defaults; replaced by updates.
2. `AbsolutePower_Imports\*.ini` — filename-sorted preset packs.
3. `AbsolutePower_Custom.ini` — user-owned settings and menu changes.

Do not place personal changes in the shipped default INI. Updates and preset packs must not replace
`AbsolutePower_Custom.ini`.

Preset packs can be installed as separate mods by placing sparse INI overlays in
`Data\SFSE\Plugins\AbsolutePower_Imports\`. They can add or override presets without replacing the
plugin or the user's custom file.

## Automation / Cheats

The repository contains an experimental event-driven backend, but it is not release-qualified.
Cross-weapon identity, demand settlement, and player policy still require redesign. Automation is
disabled by default, and Absolute Control intentionally presents a **Coming Soon** explanation plus
an emergency **Disable All** action instead of exposing the unfinished rule editor.

The current example rule schema is not a frozen public API.

## Absolute suite

- [Absolute Control](https://github.com/SultanDesync/Absolute-Control) — shared native PauseMenu
  configuration host and the recommended Power editor.
- [AbsoluteHOTAS](https://www.nexusmods.com/starfield/mods/16668) — HOTAS/HOSAS flight control and
  the optional Absolute Input Bus provider.
- [Absolute Head Tracking](https://www.nexusmods.com/starfield/mods/17872) — OpenTrack-compatible
  rotational cockpit tracking.
- [AbsoluteZero](https://www.nexusmods.com/starfield/mods/17460) — mouse alignment and locked-reticule
  steering.
## Building

Install xmake, then run:

```powershell
xmake f -m releasedbg
xmake build AbsolutePower
```

The release build stages an MO2-shaped layout under
`contrib/PluginRelease/Data/SFSE/Plugins`.

Technical documentation is available in [Architecture](docs/ARCHITECTURE.md),
[Suite Integration](docs/SUITE-INTEGRATION.md), and
[Absolute Control Integration](docs/CONTROL-PANEL-INTEGRATION.md).
