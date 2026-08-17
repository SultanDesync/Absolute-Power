# Absolute Control integration checkpoint

## Boundary

Absolute Control is a fail-optional presentation host. Absolute Power remains fully usable with
no Control DLL: it owns configuration precedence, persistence, command execution, keyboard
shortcut polling, allocation, automation policy, native validation, and all gameplay state.

Power discovers the host dynamically and registers copied descriptors through
`AbsoluteControlPanel_QueryApi(1)`. It never links to the host, requires no host import library,
and treats absence, incompatibility, and registration failure as non-fatal.

## Implemented native slice

Module `absolute.power` currently registers these stable pages:

| Page ID | Visible page | Current controls |
|---|---|---|
| `power-presets` | Presets | Fixed-size selected-preset workbench: one authoritative source/startup-aware profile selector, explicit rename, create/duplicate/delete-or-hide/revert/startup actions, Power-owned binding, allocator preview, activation lifecycle, tie-break ordering, 18 exact tier sliders, and the six-system segmented grid |
| `power-automation` | Automation / Cheats (Coming Soon) | Early-release three-control safety preview: read-only release status, a candid explanation of the unresolved policy, and immediate Disable All Automation Now. The implemented 21-control rule editor is deliberately withheld. |
| `power-diagnostics` | Diagnostics | Fixed 18-control read-only surface grouped around compatibility, executor/snapshot/activation, live ship power, configuration/automation runtime, frontend ownership, compact support state, and Power-owned paths |

Control owns rendering, navigation, capture, and generic Apply/Cancel orchestration. Power owns
the draft values behind every callback. Apply validates and atomically writes Power's custom
configuration; Cancel restores the opening values. Activate calls the saved preset command and
does not pretend that queue acceptance is native convergence.

The richer editor boundary is now available through
`AbsolutePower_QueryFrontendApi(1)`. It exposes a coherent generation-stamped configuration
snapshot, per-record defaults/import/custom provenance, Power-owned keyboard binding metadata,
and a bounded borrowed preset/rule draft. Power rejects stale generations and invalid drafts,
writes only sparse known-key overrides or tombstones into a copied custom overlay, preserves
unrelated sections and keys, verifies the temporary file through the normal precedence loader,
replaces atomically, reloads, and reports the resulting generation. The stable command/HOTAS
API remains `AbsolutePower_QueryApi(1)`.

The host capture codec is translated at the provider boundary into Power's keyboard shortcut
model. Configured shortcuts continue to execute through Power when Control is missing or closed.

The Presets route now also registers `preset-grid`, a bounded six-column × 32-segment compound
channel. Power publishes immutable low-rate frames through a three-slot reader-safe mailbox. Each
frame carries draft tier classification, live powered pips, installed maximum, and allocator
preview target. The graph labels Green as first, Yellow as after Green, and Red as last; it also
explains the cyan live outline, gold preview tick, hollow capacity, and per-system G/Y/R counts.
Pointer tier selection plus per-system add/trim events mutate the Power-owned draft; the host
attaches the ordinary page transaction before the callback, so grid and keyboard-binding changes
share Apply, Cancel, unregister pinning, and teardown rollback. Power throttles gameplay-driven
publication to 10 Hz while accepted edits publish immediately.

The current labeled-choice surface is deliberately constant at 35 controls (36 through the
previous/next compatibility fallback for an older host). It does not emit controls per preset or
truncate the backend's 256-record configuration envelope. One populated selector chooses the
provider-owned record and identifies its source and startup status; lifecycle actions, binding, numeric tier values,
tie-break order, preview, and the compound grid operate on that selection. Draft-mutating actions
carry an explicit host transaction flag, so create/delete/revert/startup/order changes pin provider
lifetime and roll back on Cancel, Close, callback failure, or abnormal session teardown exactly like
slider and grid writes. The bounded name editor writes through that same draft, while Save & Activate
uses a separate host ordering flag that persists the pinned draft before Power receives activation.

The provider still contains a constant-size selected-record editor for Power's bounded 256-rule
research envelope, but the early release publishes only its first three safety controls. The stable
page ID remains available for future redesign while users see **Coming Soon**, the exact reason it
is not supported, and **Disable All Automation Now**. The emergency action atomically persists the
global permission as off and clears runtime latches. Defaults remain disabled. This boundary avoids
freezing the current rule schema or presenting one successful Weapon 1 path as complete automation.

Diagnostics reads coherent Power-owned runtime/configuration state, weapon-source event counts,
active demand/settlement/restoration state, and host-selection state without acquiring gameplay
ownership. It does not expose raw addresses. A dedicated clipboard action and richer active-rule,
winner, and displacement telemetry remain future schema/backend work.

## Verification checkpoint

- Power builds and all seven native test executables pass.
- Configuration tests cover batch shortcut persistence, the saved automation gate, record
  provenance, the shipped Stealth allocation and 1-4 bindings, collision-free preset/rule allocation, sparse override removal, tombstones,
  unrelated-content preservation, captured-key atomic read-back, and invalid-draft rejection.
- Shortcut tests cover the Control keyboard-capture encoding.
- Host contract tests cover bounded text capture and both Save & Activate orderings: apply failure
  suppresses activation and preserves the draft; apply success cleans the transaction before invoke.
- Fresh ResearchDev Starfield 1.16.244 run `ap-native-smoke-20260815-074145` registered the fixed
  37-preset/23-automation/17-diagnostic surfaces and a ready segmented grid from Power artifact
  SHA256 `9EAF799165D208F5F18C41BFB2DB7D16DA3B2489FA8A4C5CC04D30BFF549B889`. Native input selected
  Power and traversed Presets, Automation, and Diagnostics while the bridge published the
  three-page model after each transition.
- In that run Power's exact-gated executor acquired a fresh native ship snapshot, settled the
  startup preset through 22 one-pip/next-frame-confirmed steps, and reported convergence. The menu
  then closed normally, Starfield stayed responsive, exited normally, and produced no new dump.
- A follow-up real-menu binding regression found that the Mod Organizer `overwrite` target accepted
  a verified same-directory move but rejected `ReplaceFileW`. Power now tries the metadata-preserving
  replace first and falls back to the same write-through `MoveFileExW` atomic replacement already
  used for a new overlay. The direct headless/legacy shortcut and automation writers use the same
  fallback, so this is not a Control-only persistence exception. Run
  `ap-native-smoke-20260815-081600`, using Power SHA256
  `8E23F0BF504BE6483490059B7AC666AC4B538224721D177E86EF926CEA6AB829`, captured `W`, accepted Apply,
  committed configuration generation 1 -> 2, read back `Balanced=W`, exited normally, and produced
  no dump. The test profile's original custom INI was restored after verification.
- Targeted Mod Organizer run `ap-native-smoke-20260815-110205` loaded Power SHA256
  `C7C0B3EA84294222BE888341A3258E7962E410DEDBEEA91D0C2ADD47E4BD2073` with ResearchDev SHA256
  `32944C2B9759788ACE9C6FE55A3FC2F4AEFEE7F428E10991A41F5EAED77F7A24` and SWF SHA256
  `C65598476D3729B7EBB0274307692DC2334AAC03D01F74AD615E609954746326`. The harness hit its known
  guarded-W title-selection false negative, but Starfield continued into the save and auto-opened
  Control. Power logged `35 preset, 21 automation, 17 diagnostic controls; grid=ready`; live visual
  inspection confirmed the revised Automation editor rendered without Previous/Next or the redundant
  selected-rule summary, with a clean Apply state. Popover selection was left to the user when live
  input was detected, so this is registration/render evidence rather than full dropdown interaction
  qualification.
- The 2026-08-15 weapon-automation smoke installed the candidate native WeaponGroup listener,
  registered 35 Preset, 21 Automation, and 18 Diagnostic controls, visibly reported **Weapon Fire
  Ready**, returned cleanly from Control to gameplay, and observed live Weapon 1/2 input without a
  CTD. A subsequent user journey enabled the shipped global and selected-rule gates, applied the
  transaction, fired Weapon 1, and confirmed emergency allocation from the available pool. A later
  multi-weapon journey showed that another weapon could drain Weapon 1 without charging its own
  system. Review found an over-broad pre-match listener observation, only one shipped fixed-target
  weapon rule, zero-based UI terminology, releases-before-assignments behavior, and a hold that can
  expire before convergence. The earlier evidence therefore qualifies only the Weapon 1 proof of
  concept, not the feature. The optional HOTAS bridge also remains unqualified.

This establishes mechanical exposure of Presets and Diagnostics plus a truthful Automation safety
preview; it is not final human UX acceptance or completion of Automation. Exact pointer-grid
traversal, scalar/grid/rename/rule
Apply/Cancel persistence, Save & Activate interaction, Disable All interaction, and Control-absent
in-game shortcut execution remain explicit qualification work.

## Next Control slice

Presets and Diagnostics are the supported early Power workbenches. Automation is deferred while its
user contract is redesigned around On-Demand Power and possible Auto Combat Mode; its current rule
builder and API are not SDK promises. The next shared-menu priority is the fail-optional
AbsoluteHOTAS subscriber integration described in that repository's handoff. Power still needs
preset pointer/numeric/text Apply/Cancel qualification, clipboard support, and final section/layout
metadata before the public SDK freezes.
