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
| `power-presets` | Presets | On a fully capable host, 19 descriptors: four semantic section headers; one authoritative source/startup-aware profile selector; rename and startup fields; Power-owned keyboard and Input Bus bindings; four inline create/duplicate/delete-or-hide/revert actions; six priority Choices explicitly associated with the six-system 12-pip grid |
| `power-automation` | Automation / Cheats (Coming Soon) | Early-release three-control safety preview: read-only release status, a candid explanation of the unresolved policy, and immediate Disable All Automation Now. The implemented 21-control rule editor is deliberately withheld. |
| `power-diagnostics` | Diagnostics | Fixed 19-control read-only surface covering compatibility, executor/snapshot/activation, live ship power, configuration/automation runtime, frontend and Input Bus state, compact support state, and Power-owned paths |

Control owns rendering, navigation, capture, and generic Apply/Cancel orchestration. Power owns
the draft values behind every callback. Apply validates and atomically writes Power's custom
configuration; Cancel restores the opening values. The compact page does not currently publish a
manual activation action; startup, keyboard, and Input Bus shortcuts remain Power-owned execution
paths. Whether to add an explicit activate-saved-profile action is a product decision before final
Power UX acceptance, not an implicit effect of changing the transient selector.

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

The Presets route also registers `preset-grid`, a bounded six-column compound channel. Each column
declares `maximumSegments=12`; the shared protocol retains a 32-segment upper bound without
reserving 32 visual slots. Power publishes immutable low-rate frames through a three-slot
reader-safe mailbox. Each
frame carries draft tier classification, live powered pips, installed maximum, and allocator
preview target. Green, Yellow, and Red pips carry 1, 2, and 3 glyphs; hollow pips remain unallocated.
Direct pip cycling and the +G/+Y/+R/− quick steps mutate the Power-owned draft. Positional edits
normalize intervening pips into canonical Green→Yellow→Red→Hollow order while guaranteeing that
the activated pip acquires the requested tier. The host
attaches the ordinary page transaction before the callback, so grid and keyboard-binding changes
share Apply, Cancel, unregister pinning, and teardown rollback. Power throttles gameplay-driven
publication to 10 Hz while accepted edits publish immediately.

The current surface is deliberately constant across the backend's 256-record configuration
envelope. With labeled choices, provider capture, structured layout, and live row associations it
declares 19 Presets descriptors. Missing structured layout omits headers and expands inline actions
to ordinary rows; missing labeled choices substitutes Previous/Next; missing provider capture omits
the Input Bus binding; missing live association support leaves all six tie-break Choices in the
ordinary list. A populated transient selector chooses the provider-owned record and identifies its
source and startup status. Draft-mutating actions
carry an explicit host transaction flag, so create/delete/revert/startup/order changes pin provider
lifetime and roll back on Cancel, Close, callback failure, or abnormal session teardown exactly like
grid writes. The bounded name editor and both binding controls write through that same draft. Grid
row associations are capability-gated records, not inferred from the `order-` ID prefix.

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

- Power builds and all nine native test executables pass on the current development tree.
- Configuration tests cover batch shortcut persistence, the saved automation gate, record
  provenance, the shipped Stealth allocation and 1-4 bindings, collision-free preset/rule allocation, sparse override removal, tombstones,
  unrelated-content preservation, captured-key atomic read-back, and invalid-draft rejection.
- Shortcut tests cover the Control keyboard-capture encoding.
- Host contract tests retain generic apply-before-invoke ordering coverage. That is host capability
  evidence, not a claim that the compact current Power page publishes Save & Activate.
- Positional grid tests cover interior Green→Yellow, Yellow→Red, Red→Hollow, distant
  Hollow→Green, full-row clearing, invalid plans, and bounds.
- Fresh ResearchDev Starfield 1.16.244 run `ap-native-smoke-20260815-074145` registered the fixed
  37-preset/23-automation/17-diagnostic surfaces and a ready segmented grid from Power artifact
  SHA256 `9EAF799165D208F5F18C41BFB2DB7D16DA3B2489FA8A4C5CC04D30BFF549B889`. Native input selected
  Power and traversed Presets, Automation, and Diagnostics while the bridge published the
  three-page model after each transition.
- In that run Power's exact-gated executor acquired a fresh native ship snapshot, settled the
  startup preset through 22 one-pip/next-frame-confirmed steps, and reported convergence. The menu
  then closed normally, Starfield stayed responsive, exited normally, and produced no new dump.
- Follow-up real-menu regressions found that the Mod Organizer `overwrite` target accepts a
  verified same-directory write-through `MoveFileExW` replacement, while `ReplaceFileW` may either
  reject the operation or falsely report success before the virtual path exposes the new file.
  Power therefore uses the validated move replacement directly for configuration transactions.
  The direct headless/legacy shortcut and automation writers retain equivalent atomic replacement
  behavior, so this is not a Control-only persistence exception. Run
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

The retained runtime records establish earlier mechanical exposure of Presets and Diagnostics plus
a truthful Automation safety preview; they are not final acceptance of the current 19-descriptor
layout or completion of Automation. Exact pointer/controller grid traversal, row-Choice activation,
pip-position normalization, rename/binding/grid Apply/Cancel/read-back, long device-name inspection,
Disable All interaction, and Control-absent in-game shortcut execution remain explicit
qualification work.

## Next Control slice

Presets and Diagnostics are the supported early Power workbenches. Automation is deferred while its
user contract is redesigned around On-Demand Power and possible Auto Combat Mode; its current rule
builder and API are not SDK promises. Power next needs current-artifact pointer/keyboard/controller
acceptance, binding-conflict and long-name coverage, Apply/Cancel/read-back, a decision on explicit
manual activation, and compound frame-time measurement. Any generic gap belongs in the shared SDK;
Power-specific coordinates or renderer branches do not.
