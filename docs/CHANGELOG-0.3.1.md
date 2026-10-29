# Absolute Power 0.3.1-alpha

- Preserves Starfield's canonical native weapon-part enumeration by default; no universal Weapon
  2/3 swap is applied.
- Adds the validated `sWeaponPoolOrder` compatibility permutation for loadouts whose populated
  native entries demonstrably disagree with HUD Weapon 1/2/3 order.
- Treats absent weapon groups as absent: sparse native tables map only their populated entries.
- Rejects invalid or duplicate mappings, duplicate resolved pools, and layouts with more than
  three weapon-bearing parts.
- Verifies that the equipment object and every resolved pool address are unchanged between the
  planning snapshot and native write.
- Logs the populated weapon-part count and active ordinal map on the first valid snapshot.
- Corrects zero-based Weapon 0/1/2 text in Control diagnostics and automation choices to the
  cockpit-facing Weapon 1/2/3 labels.
- Extends the shipped Balanced, Combat, and Travel plans to rank all 12 possible pips for every
  system. Stealth remains intentionally sparse.
