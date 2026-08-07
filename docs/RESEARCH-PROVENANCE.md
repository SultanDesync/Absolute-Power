# Native power research provenance

The bootstrap is based on the accepted artifacts in
`F:\AbsoluteHOTAS-ResearchData`, all validated against Starfield `1.16.244.0`.

## Accepted facts carried into production design

- The live power-equipment lookup returned type ID `260072` through descriptor global
  RVA `0x5F2FAF8`.
- The accepted component contained a bounded part table with installed weapons, shield,
  engine, and grav-drive objects. A ship with only two weapons returned five parts; the
  empty third HUD column was not a part object.
- Each accepted part exposed maximum power at `+0x60` and current power at `+0x64`.
- Native absolute setter RVA `0x21573A0`, add-one `0x2157440`, and remove-one
  `0x2157633` were observed and exact-byte-gated.
- Calling the absolute setter with reason `1` changed shield `6 -> 5`, made one reactor
  pip available, then restored `5 -> 6` and consumed that available pip.
- Fresh generic `Left`, `Right`, `Down`, and `Up` semantic events causally navigated and
  adjusted the ShipHUD power frontend without physical keys.

Primary evidence:

- `runs/20260805T060300.592Z-system-power-equipment-snapshot-v1`
- `runs/20260805T061343.154Z-system-power-one-pip-v1`
- `runs/20260806T062225.969Z-system-power-direct-navigation-v1`
- `deployment-snapshots/20260806T062225-final-ship-function-cartography-v1/`

## Production promotion gate still open

The research snapshot command used an exact registry lookup, bounded copy, identity
classification, and balanced shared-reference release on the observed game thread. The
public AbsoluteHOTAS tree intentionally excludes that research-only implementation.
Absolute Power preserves the confirmed constants and object model, but its backend
returns `SnapshotSeamUnavailable` until the complete ownership sequence is recovered or
promoted and then revalidated. Reconstructing it from isolated RVAs would discard the
most important safety evidence and is explicitly out of bounds.

The generic ShipHUD semantic route remains a separate future primitive for relative
navigation. It must not substitute for the absolute setter used by presets, and it must
receive its own pilot/menu context gate before production use.
