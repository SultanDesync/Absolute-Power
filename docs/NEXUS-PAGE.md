# Absolute Power — Nexus page kit

## Listing identity

- **Name:** Absolute Power
- **Version:** 0.3.0-alpha
- **Tagline:** Portable, priority-driven ship-power presets for Starfield.
- **Suggested summary:** Fixed power presets assume equal ships. Absolute Power applies universal Green, Yellow, and Red priorities, funding important systems before lower tiers and adapting the same plan to different reactors and loadouts.
- **Primary cover:** `docs/images/absolute-power-cover-1600x900-v2.png`
- **Nexus banner (1300×372):** `docs/images/absolute-power-banner-1300x372-v2.png`
- **Feature screenshot:** `docs/images/absolute-power-presets.png`
- **Main file:** `Absolute-Power-v0.3.0-alpha-Release.zip`

## Requirements

- Starfield 1.16.244
- SFSE 0.2.20 or later
- No Address Library required by Absolute Power itself
- Absolute Control — Standalone Menu Host 0.2.0-beta.1 required for the advertised in-game editor;
  the Power backend retains its INI and shortcut fallback without it
- Address Library for SFSE Plugins required by the separate Absolute Control host

## Suggested tags

- Gameplay
- Ships
- Utilities
- User Interface
- Controller
- Quality of Life

## Embedded image placeholders

Replace these reserved `.invalid` URLs in both copies of the Nexus description after the final
images have been uploaded to a public host:

| Placeholder URL | Local source | Description role |
| --- | --- | --- |
| `https://REPLACE-ME.invalid/absolute-power-guide-01-mod-options.png` | `docs/images/absolute-power-guide-01-mod-options.png` | Pause Menu entry and first access. |
| `https://REPLACE-ME.invalid/absolute-power-guide-02-priority-grid.png` | `docs/images/absolute-power-guide-02-priority-grid.png` | Tier requests and deterministic per-system order. |
| `https://REPLACE-ME.invalid/absolute-power-guide-03-profile-bindings.png` | `docs/images/absolute-power-guide-03-profile-bindings.png` | Profile identity, startup selection, and keyboard/HOTAS activation. |
| `https://REPLACE-ME.invalid/absolute-power-guide-04-profile-actions.png` | `docs/images/absolute-power-guide-04-profile-actions.png` | Create, duplicate, hide/delete, and revert actions. |

The canonical BBCode is `Nexus_Description.txt`; the release-folder copy should remain identical.

## Upload notes

Use `Nexus_Description.txt` as the BBCode description and
`releases/v0.2.0-alpha/CHANGELOG.txt` for the initial changelog. Upload the primary screenshot
without additional HDR conversion. Use the 1600×900 RGB artwork as the cover and the exact
1300×372 RGB artwork as the page banner. The supported public feature is priority presets; do not
market the unfinished Automation / Cheats backend as available.
