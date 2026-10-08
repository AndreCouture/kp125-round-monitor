# Changelog

Changes since the repository was made public. Dates are when the change reached `main`.

## Unreleased

### Fixed

- Touch: a quick swipe could be taken as a tap (next page). The touch chip is now read about every 10 ms instead of every 50-60 ms, a touch ends only after the finger has been gone for 80 ms, and the chip's own slide detection rules out a tap. A tap now moves at most 8 px and a swipe at least 15 px; anything between is ignored.
- Cap: the two feet on the right (seen from the back) are 0.5 mm shorter, so the cap seats flat (`foot_trim`), and the crush ribs grip harder (`cap_fit` 0.3 mm, was 0.2) because the cap was still a bit loose.

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
- Secret scanning, push protection and private vulnerability reporting are enabled.
