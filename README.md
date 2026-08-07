# Absolute Power

Absolute Power is a Starfield SFSE plugin for priority-based ship-power presets and
optional event-driven reallocation. It is part of the Absolute flight-control suite.

## Absolute suite

- [Absolute Workbench](https://github.com/SultanDesync/Absolute-Workbench) — required
  shared configuration UI for Absolute Power and optional frontend for other daughters.
- [AbsoluteHOTAS](https://github.com/SultanDesync/AbsoluteHOTAS) — optional HOTAS/HOSAS
  command client and the suite's primary mod page.
- [Absolute Head Tracking](https://github.com/SultanDesync/Absolute-Head-Tracking) —
  standalone OpenTrack cockpit camera control.
- [AbsoluteZero Ship Control](https://github.com/SultanDesync/AbsoluteZero-Ship-Control) —
  lightweight mouse-alignment assistance.

The base mod is standalone with respect to flight hardware: it does not require
AbsoluteHOTAS. It does require the universal [Absolute Workbench](https://github.com/SultanDesync/Absolute-Workbench)
mod for its user
interface. AbsoluteHOTAS is an optional command client that can bind preset activation
and automation commands through the exported suite API.

## Bootstrap status

This repository is a buildable `0.2.0-alpha` foundation. It currently includes:

- a deterministic Green -> Yellow -> Red pip allocator;
- strict tier barriers and round-robin tie breaking;
- releases-first native change planning;
- opt-in weapon, incoming-damage, throttle, and manual automation policy;
- shipped/default plus user-overlay configuration conventions;
- a C-compatible API for Absolute Workbench and AbsoluteHOTAS;
- a positive post-post-load Workbench ABI handshake instead of a file-presence check;
- exact Starfield 1.16.244.0 native power-setter signature gates; and
- standalone tests for allocation policy, automation timing, and ABI shape.

The live equipment-component lookup and shared-reference lifetime sequence recovered by
the research harness has not yet been promoted into production. The DLL therefore loads
and exposes configuration/API metadata, but power reads and writes fail closed with
`NativeSeamUnavailable`. No unverified pointer or setter call is made.

## Priority presets

Each installed ship system receives a number of pips in three tiers:

- **Green** — allocate first.
- **Yellow** — allocate only after every requested Green pip is populated.
- **Red** — allocate only after every requested Green and Yellow pip is populated.

When a reactor cannot complete a tier, the allocator shares that tier one pip at a time
in the preset's configured system order and stops. Uncolored capacity remains available.
Preset counts are clipped to the current ship's installed subsystem maximum, so one
preset can travel between ships and tolerate an empty weapon slot.

## Automation / Cheats

Automation can reserve emergency pips before the Green tier when rules fire. Example
rules can max the weapon being fired, raise shields after damage, or raise engines above
a throttle threshold. This bypasses vanilla manual power-management pressure and is
therefore labeled **Automation / Cheats**, globally disabled by default, and separately
disabled per rule.

## Build

```powershell
xmake
xmake test
```

The build stages the DLL, default INI, and Workbench manifest under
`contrib/PluginRelease/Data/SFSE/Plugins`. Set `xmake f --deploydir=...` or the
`ABSOLUTE_POWER_DEPLOY_DIR` environment variable for local deployment.

See [Architecture](docs/ARCHITECTURE.md), [research provenance](docs/RESEARCH-PROVENANCE.md),
[suite integration](docs/SUITE-INTEGRATION.md), and the
[Absolute Workbench frontend handoff](docs/WORKBENCH-HANDOFF.md).
