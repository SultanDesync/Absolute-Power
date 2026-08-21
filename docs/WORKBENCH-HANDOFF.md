# Absolute Control frontend UX handoff

This document is the target UX contract for the optional native Absolute Control frontend.
Absolute Power contains only its provider adapter and provider-owned state; the menu renderer,
navigation, and input-capture machinery live in the Control host. Power continues to load
configuration and execute through its exact-gated standalone runtime when Control is absent,
incompatible, rejected, or suppressed.

The earlier Absolute Workbench ImGui implementation remains a useful interaction prototype, but
it is not the production host or the owner of this contract.

The detailed [AP menu integration plan](<../../Absolute Workbench/docs/AP-MENU-INTEGRATION-PLAN.md>)
supersedes the bootstrap's direct frontend writer with a backend-owned transactional
save contract while preserving the user-visible behavior defined here. The suite
[headless subscriber contract](<../../Absolute Workbench/docs/HEADLESS-SUBSCRIBER-CONTRACT.md>)
also supersedes Workbench ownership of Power keyboard shortcuts: Control captures and
edits them, while Power persists and executes them without requiring the frontend.

## Current implementation checkpoint

On a fully capable host, the native provider exposes a constant-size 19-descriptor Presets
workbench around the segmented grid, a three-control **Automation / Cheats (Coming Soon)** safety
preview, and 19 read-only Diagnostics controls. Presets uses four group headers, compact inline
lifecycle actions, keyboard and Input Bus bindings, and six priority Choices attached explicitly to
their grid rows. Capability fallbacks remove headers/inline layout, substitute Previous/Next for a
missing labeled selector, omit unsupported provider capture, and leave priority Choices in the
ordinary list when row associations are unavailable. Power retains its experimental 21-control
rule editor in source, but does not publish it in the early release. The preview explains the
deferral and retains an immediate persisted Disable All action.

The Weapon 1 research path executed, but a later cross-weapon journey exposed incomplete identity,
policy, and settlement behavior. Automation is therefore deferred rather than promoted. A future
handoff should redesign it around On-Demand Power and possible Auto Combat Mode before restoring an
editable Control surface.

## Navigation

Register three pages under the **Absolute Power** module:

1. **Power Presets**
2. **Automation / Cheats (Coming Soon)**
3. **Power Diagnostics**

Use the normal Absolute Control header and its draft/apply/cancel/close behavior. Live reactor and
allocation state belongs in the grid and Diagnostics, not in subscriber-specific header geometry.

## Power Presets page

### Layout

- **Profile Identity**: one transient populated selector, rename field, and startup toggle.
- **Hardware Bindings**: keyboard chord and optional Input Bus button/POV capture.
- **Profile Actions**: Create/Duplicate and Delete/Revert in compact inline rows.
- Allocation grid: one row each for Weapon 0, Weapon 1, Weapon 2, Engines, Shields, and Grav
  Drive, followed by the row's priority Choice when supported.
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

Each row's priority Choice edits the same deterministic tie-break ordering. Explain that the order
applies only when the reactor cannot finish the current tier; it does not allow Yellow to outrank
unfilled Green. Pointer or keyboard/controller activation of the focused row must open the same
bounded Choice popup.

### Preview

The grid carries the non-mutating preview for the current live ship:

- target pips per system;
- clipped pips because a system is absent or smaller on this ship;
- unassigned reactor pips;
- the first incomplete tier, if any; and
- a compact `Green complete -> Yellow partial -> Red blocked` explanation.

The host footer's **Apply** writes the custom configuration; **Cancel** restores the opening draft.
The compact page does not currently publish a manual activation action. Startup, keyboard, and
Input Bus shortcuts activate saved profiles. Before release, decide explicitly whether the menu
also needs an activate-saved-profile action; changing the transient selector must never activate a
profile as a hidden side effect. A missing pilot-ready snapshot disables live execution/preview but
does not prevent editing.

## Automation / Cheats page

**Early-release treatment:** publish only **Automation / Cheats (Coming Soon)** with a read-only
explanation and immediate Disable All action. The detailed rule-builder contract below is retained
as research history, not the current release target. Revisit it only after the simpler On-Demand
Power behavior and any Auto Combat Mode are specified and validated.

Place a persistent amber/orange **CHANGES GAME BALANCE** banner above all controls:

> Automatic power reassignment removes part of Starfield's manual ship-power challenge.
> These rules are optional cheats and are disabled by default.

Never enable the global switch as a side effect of enabling an individual rule. Present an
explicit activation checklist above the editor, number the two required switches as the global
gate and selected-rule gate, put the selected-rule gate directly beside the selector/meaning, and
state when **Apply** is required. The checklist must resolve to a concrete `BLOCKED`, `PENDING`, or
`ARMED` state rather than expecting the user to infer readiness from two distant controls.

### Rule builder

Use one populated rule selector as the authoritative current-record control. Each option identifies
the rule's display name, source provenance, and current ON/OFF state. Changing selection is transient
view navigation and must not create a pending transaction. Do not duplicate it with Previous/Next
actions or a second selected-rule summary on hosts that support labeled choices.

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

- Absolute Control host/API version and frontend state;
- Absolute Power backend version and runtime state;
- Starfield runtime compatibility gate;
- live component/pilot readiness;
- installed systems with current/max values;
- available and total reactor pips;
- last preset apply result and partial-change count;
- native state (`UnsupportedRuntime`, `NativeSeamUnavailable`, `PilotNotReady`,
  `Rejected`, etc.); and
- copyable log/config paths.

`NativeSeamUnavailable` means the promoted native path could not obtain a fresh,
fully validated snapshot or game-thread executor context. Present it as a visible
fail-closed diagnostic, not as the expected baseline state and never as a successful
live connection. `PilotNotReady` is ordinary contextual unavailability and should not be
styled like an unsupported-runtime or ownership failure.

## Persistence and error handling

Submit the bounded draft and its opening generation through the AP configuration
transaction. Absolute Power writes `AbsolutePower_Custom.ini` through a temporary file
plus atomic replace, preserves unknown sections and keys where practical, reloads, and
compares the effective records to the submitted intent before returning success. The
bootstrap direct writer must retain those same safeguards until the transaction replaces
it, but new source-aware behavior must not be duplicated in each frontend.

Every failed API call stays visible in the page status area. Never dismiss a failed
apply merely because the configuration save succeeded. If an apply stops after releasing
some systems, surface the completed/total change count and ask the user to retry only
after a fresh snapshot.

## Keyboard and controller accessibility

Every pip, preset, rule, reorder control, and confirmation must be reachable without a
mouse. Use one focus stop per row plus directional pip editing where possible to avoid a
six-by-dozens tab trap. Provide text labels for all colors and never encode tier state by
color alone.
