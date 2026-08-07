# Absolute suite integration

## Discovery and load order

`AbsolutePower.dll` exports:

```cpp
const AbsolutePowerApi::ApiV1* AbsolutePower_QueryApi(std::uint32_t requestedAbiVersion);
```

The API is pull-based. Absolute Workbench and AbsoluteHOTAS may load before or after
Absolute Power, locate the DLL with `GetModuleHandleW`, resolve the export with
`GetProcAddress`, and request ABI version `1`. Clients must check `structSize`,
`abiVersion`, every callback pointer they use, and every returned `Result`.

## Absolute Workbench

Absolute Workbench is required. At SFSE post-post-load, Absolute Power validates the
size-versioned `AbsoluteWorkbench_QueryHostApi(1)` table and requires host mode `Active`;
file presence alone never activates the backend. Workbench discovers
`AbsolutePower.workbench.json`, queries API
v1, renders the pages described in the frontend handoff, writes
`AbsolutePower_Custom.ini` atomically, writes `Deleted=true` tombstones for removed
shipped records, and calls `reloadConfiguration`. The backend never links to
ImGui, D3D12, DXGI, or Workbench implementation symbols.

## AbsoluteHOTAS

AbsoluteHOTAS is optional. It should enumerate `getCommandCount/getCommand` and expose
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

## Compatibility expectations

- Missing AbsoluteHOTAS has no effect on standalone Workbench operation.
- Missing Absolute Workbench makes Absolute Power inert and reports
  `WorkbenchMissing` through API v1.
- Unsupported Starfield builds fail closed.
- API v1 uses fixed-size POD outputs so allocator and configuration C++ types never cross
  the DLL boundary.
