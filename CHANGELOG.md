# Changelog

Changes since the repository was made public. Dates are when the change reached `main`.

## 2026-10-08 - First public release

### Added

- `LICENSE` explaining the split licence, copyright 12113775 Canada Inc.:
  - firmware, tools and git hook: GPL-3.0-or-later (partly derived from python-kasa);
  - stand design and docs: CC BY-NC-SA 4.0;
  - fonts: SIL OFL 1.1.

  Full texts are in `LICENSES/`, each source file has an SPDX and copyright line, and the README has a License section.
- `SECURITY.md`: only `main` is supported; report privately through GitHub's "Report a vulnerability"; scope; known limitations (credentials stored in flash in plain text, the legacy protocol is unencrypted, TPAP runs over plain HTTP).
- README section "Don't want to build it yourself?": printed stands and caps, individual parts and complete kits from 12113775 Canada Inc., requested through a GitHub issue.
- `CHANGELOG.md`.

### Changed

- Display navigation hints: pages with a swipe up/down choice (gauge, TASK, 7 DAYS) show a vertical dot column on the right with up/down arrows, and the page dot row at the bottom has left/right arrows. Pages without an up/down choice show no column.
- Gauge page:
  - The title is "LIVE - ALL" instead of "TOTAL", and swipe up/down steps through ALL and each plug.
  - ALL shows the sum of all plugs, the headroom left before `MAX_W`, and how many plugs are online.
  - A plug shows its watts, volts and amps, plus the total for comparison. An offline plug shows "--" and "offline".
  - The dots list ALL first, then each plug: green = online, red = offline.
- Real LAN IP addresses in the docs and a code comment are replaced with generic examples.

### Repository

- `main` requires a pull request; force-pushes and deletion are blocked.
- Secret scanning and push protection are enabled.
