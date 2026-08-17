# Absolute suite integration

## Discovery and load order

`AbsolutePower.dll` exports its suite command API:

```cpp
const AbsolutePowerApi::ApiV1* AbsolutePower_QueryApi(std::uint32_t requestedAbiVersion);
```

The API is pull-based. Legacy Absolute Workbench and AbsoluteHOTAS may load before or after
Absolute Power, locate the DLL with `GetModuleHandleW`, resolve the export with
`GetProcAddress`, and request ABI version `1`. Clients must check `structSize`,
`abiVersion`, every callback pointer they use, and every returned `Result`.

## Optional Control provider

After Power has initialized its standalone runtime, it dynamically probes
`AbsoluteControlPanel.dll` and the local-development `AbsoluteControlPanelResearchDev.dll` for
`AbsoluteControlPanel_QueryApi(1)`. A valid host receives module `absolute.power` with the stable
pages `power-presets`, `power-automation`, and `power-diagnostics`. Host absence, `NotReady`, ABI
rejection, or rendering failure is logged and remains non-fatal. Registration is retried after
the SFSE load boundary when appropriate.

The host copies presentation descriptors. Reads, draft writes, Apply, Cancel, action execution,
validation, persistence, and refresh state stay inside Absolute Power. The current Presets route
uses labeled choices, bounded text, ordinary controls, and the experimental segmented-grid
extension to expose the fixed-size provider-owned workbench. Diagnostics remains read-only. A
complete selected-rule Automation editor also exists behind the same ownership boundary, but it is
deliberately not published: cross-weapon testing did not qualify its behavior. The stable route now
contains only the Coming Soon explanation and a persisted Disable All safety action. No Power state
moves into the host as a shortcut.

See [the native integration checkpoint](CONTROL-PANEL-INTEGRATION.md).

## Optional legacy interface and input hosts

Absolute Power initializes its configuration and backend with no interface host.
At SFSE post-post-load it additionally discovers and validates either a compatible active
`AbsoluteWorkbench_QueryHostApi(1)` table or a compatible `AbsoluteHOTAS_QueryApi(1)`
table. This selects optional presentation, input, and game-thread bridge behavior; it
does not activate or gate the Power runtime. File presence alone is never treated as a
successful interface handshake. Workbench discovers `AbsolutePower.workbench.json`,
queries API v1, renders the pages described in the frontend handoff, writes
`AbsolutePower_Custom.ini` atomically, writes `Deleted=true` tombstones for removed
shipped records, and calls `reloadConfiguration`. The backend never links to
ImGui, D3D12, DXGI, or Workbench implementation symbols.

That direct writer is the legacy bootstrap contract only. Rich frontends now use
`AbsolutePower_QueryFrontendApi(1)`, which preserves source metadata, writes only minimal user
overrides, rejects stale generations, and returns separate validation, write, reload, and
verification outcomes. `AbsolutePower_QueryApi(1)` remains available to existing command and
game-thread clients.

The same rule applies to intrinsic keyboard activation shortcuts. The first AP slice
replaced Workbench's `[PowerPresetBindings]` store and polling loop with Power-owned
`[KeyboardPresetBindings]`, additive binding callbacks, and headless execution. Control
remains the preferred capture/editor client. This preserves
the suite [headless subscriber contract](<../../Absolute Workbench/docs/HEADLESS-SUBSCRIBER-CONTRACT.md>).
HOTAS button/POV mappings remain HOTAS-owned because they depend on its device profiles
and polling runtime.

The legacy manifest's `requiredHost.required=true` describes only the relationship of its
Workbench-rendered pages. It is not a runtime dependency declaration for `AbsolutePower.dll`
and does not describe native Absolute Control registration.

## AbsoluteHOTAS

AbsoluteHOTAS enumerates `getCommandCount/getCommand` and exposes
those entries in its binding catalog. Dynamic preset IDs are returned as:

```text
preset:<PresetId>
```

The stable non-preset command is:

```text
automation:toggle
```

On a HOTAS press edge, call `invokeCommand`. Do not retain raw pointers to command
records, synthesize arrow keys, or duplicate power policy in AbsoluteHOTAS. A future
hold-style command must define explicit press/release ownership in a new ABI revision.

API v1 also has an experimental additive, size-gated `recordWeaponFire(groupIndex)` tail.
AbsoluteHOTAS calls it only after its direct native Weapon 1/2/3 start leaf succeeds. Older Power
builds remain compatible because HOTAS tests `structSize` and the callback pointer. This tail and
Power's standalone listener remain research seams, not a release-qualified Automation contract;
the final On-Demand Power design may replace their level/lifetime semantics.

## Compatibility expectations

- Control-only, legacy Workbench-only, and AbsoluteHOTAS-only installations can present and
  command the same independently initialized Absolute Power runtime.
- With neither host installed, Power runs configured startup presets through its own
  exact-gated executor and executes its configured keyboard shortcuts, but has no shared in-game
  editor. Experimental automation remains disabled by default and is not release-qualified. Any
  future On-Demand Power or combat-context implementation must use the same standalone executor.
  The
  `WorkbenchMissing` API v1 value is retained for binary compatibility but is no longer
  emitted by the runtime.
- Configuration precedence is shipped defaults, filename-sorted
  `AbsolutePower_Imports/*.ini`, then the user-owned `AbsolutePower_Custom.ini`.
- Unsupported Starfield builds fail closed.
- API v1 uses fixed-size POD outputs so allocator and configuration C++ types never cross
  the DLL boundary.
