# Absolute Workbench frontend handoff

This document is the UI contract for the universal Absolute Workbench project. The
frontend lives there; this repository intentionally contains no renderer hook or ImGui
code.

## Navigation

Register three pages under **Ship Systems** / **Diagnostics**:

1. **Power Presets**
2. **Automation / Cheats**
3. **Power Diagnostics**

Use the normal Workbench fixed header and its draft/save/close behavior. The header must
show backend state, active preset, reactor output, allocated pips, and available pips
when a live snapshot exists.

## Power Presets page

### Layout

- Left rail: preset list with New, Duplicate, Rename, Delete, and startup-preset marker.
- Main panel: one row each for Weapon 1, Weapon 2, Weapon 3, Engines, Shields, and Grav
  Drive.
- Each row displays the current ship maximum and a contiguous pip strip.
- Pip colors use both color and a symbol/texture for accessibility:
  - Green / `1`: first tier
  - Yellow / `2`: second tier
  - Red / `3`: third tier
  - Hollow / `-`: unrequested
- An empty native system is visibly marked **Not installed**; do not invent a capacity
  from another ship or the HUD's empty column.

Clicking or keyboard-activating a pip sets the cumulative tier counts for that row. The
frontend must keep rows monotonic (`GGGYYRR--`). If a user attempts a priority inversion,
normalize the intervening pips and show a small explanatory status message.

Provide a reorderable **Within-tier order** list. Explain that it is a deterministic
round-robin tie breaker only when the reactor cannot finish the current tier; it does not
allow Yellow to outrank unfilled Green.

### Preview

Always show a non-mutating preview for the current live ship:

- target pips per system;
- clipped pips because a system is absent or smaller on this ship;
- unassigned reactor pips;
- the first incomplete tier, if any; and
- a compact `Green complete -> Yellow partial -> Red blocked` explanation.

Buttons:

- **Save Draft** writes only the custom configuration.
- **Save & Activate** atomically writes, reloads, then invokes the selected preset.
- **Activate Without Saving** applies the saved version and must not silently use draft
  values.

Disable activation when no pilot-ready snapshot exists, but keep editing available.

## Automation / Cheats page

Place a persistent amber/orange **CHANGES GAME BALANCE** banner above all controls:

> Automatic power reassignment removes part of Starfield's manual ship-power challenge.
> These rules are optional cheats and are disabled by default.

The global enable switch requires a one-time confirmation. Never enable it as a side
effect of enabling an individual rule. Show both gates on every row: global state and
rule state.

### Rule builder

Each rule edits:

- name and enabled state;
- trigger: Weapon Fired, Incoming Damage, Throttle Above, or Manual;
- source weapon for Weapon Fired, including Any Weapon;
- throttle threshold and hysteresis for Throttle Above;
- target system;
- target pip count or Max;
- event hold duration;
- numeric precedence; and
- a plain-language summary, for example: `For 1.2 s after Weapon 1 fires, demand W0 max
  before the Green tier (priority 200).`

The live panel must list currently active rules, their remaining duration, their demand,
which demand won any conflict, and which base-tier pips were displaced. Provide an
immediate **Disable All Automation** action that clears latched rules.

Do not use celebratory, progression, or achievement styling for this page. The label
should be candid and neutral rather than moralizing.

## Power Diagnostics page

Show:

- Workbench dependency/API version;
- Absolute Power backend version and runtime state;
- Starfield runtime compatibility gate;
- live component/pilot readiness;
- installed systems with current/max values;
- available and total reactor pips;
- last preset apply result and partial-change count;
- native seam state (`UnsupportedRuntime`, `NativeSeamUnavailable`, etc.); and
- copyable log/config paths.

The current alpha must present `NativeSeamUnavailable` as an expected implementation
status, not as a successful live connection.

## Persistence and error handling

Write `AbsolutePower_Custom.ini` through a temporary file plus atomic replace. Preserve
unknown sections and keys where practical. After save, call `reloadConfiguration`, read
the records back through API v1, and compare them to the draft before reporting success.

Every failed API call stays visible in the page status area. Never dismiss a failed
apply merely because the configuration save succeeded. If an apply stops after releasing
some systems, surface the completed/total change count and ask the user to retry only
after a fresh snapshot.

## Keyboard and controller accessibility

Every pip, preset, rule, reorder control, and confirmation must be reachable without a
mouse. Use one focus stop per row plus directional pip editing where possible to avoid a
six-by-dozens tab trap. Provide text labels for all colors and never encode tier state by
color alone.
