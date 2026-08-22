# Native power research provenance

The bootstrap is based on the accepted artifacts in
`F:\AbsoluteHOTAS-ResearchData`, all validated against Starfield `1.16.244.0`.

## Accepted facts carried into production design

- The live power-equipment lookup returned type ID `260072` through descriptor global
  RVA `0x5F2FAF8` in the accepted research process. Production testing later proved
  this registry key is process-assigned; the exact-gated descriptor field must be
  read live rather than treating `260072` as a build-stable constant.
- The accepted component contained a bounded part table with installed weapons, shield,
  engine, and grav-drive objects. A ship with only two weapons returned five parts; the
  empty third HUD column was not a part object.
- The native pool-selector branches are identified by their embedded failure strings:
  selector `1` / identity gate `0x1F2FAC4` is Engines, selector `2` / `0x1F2FA82`
  is Shields, and selector `3` / `0x1F2FA40` is Grav Drive. Live three-weapon
  testing establishes that the power part table's weapon enumeration remains canonical:
  part order `0,1,2` maps to `Weapon0,Weapon1,Weapon2`. Capture and setter lookup
  share this single six-pool mapping contract.
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

## Production implementation promoted; release qualification open

The complete research sequence has been promoted into Absolute Power's
`NativePowerBackend`: live registry-key resolution, bounded component and part copies,
identity classification, exact layout and function gates, and balanced shared-reference
release all execute on a validated game-thread path. Preset requests settle one native
pip at a time, with a fresh snapshot on a later update before the next change. The
implementation remains exact-gated to Starfield `1.16.244.0` and fails closed when any
lookup, ownership, layout, identity, executor, or setter check fails.

This closes the implementation-promotion gate; it does not by itself qualify a release.
Integrated in-game testing must still cover repeated snapshot/release cycles, multi-frame
settlement, startup activation, Workbench and HOTAS command paths, missing pilot context,
and fail-closed behavior. `NativeSeamUnavailable` now means that the promoted path could
not produce a fresh validated snapshot or executor context. It is a visible diagnostic,
not the expected steady state of the implementation.

Reconstructing future support from isolated RVAs would discard the most important safety
evidence and remains explicitly out of bounds. A new Starfield runtime must repeat the
lookup, ownership, layout, identity, setter, and release validation before its gates are
accepted.

The generic ShipHUD semantic route remains a separate future primitive for relative
navigation. It must not substitute for the absolute setter used by presets, and it must
receive its own pilot/menu context gate before production use.
