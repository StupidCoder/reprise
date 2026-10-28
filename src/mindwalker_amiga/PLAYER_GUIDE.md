# Mind Walker player guide

The details screen's **How to play** section is a read-only SDL article with
six selectable pages: overview, landscape, trail troubleshooting, tunnel,
arena and psyche. It works without imported files. No gameplay rules change.
The text describes our reconstruction, not an original manual. The three
panels on each page are schematic instructions, not captures of original play.

## Evidence

Source: the owner's Mind Walker ADF (SHA256 recorded in
`.analysis/mindwalker_amiga/disk.md`). Recovery and original-execution evidence
are in `.analysis/mindwalker_amiga/game.md` and its harness recordings.
No external game-specific guides were consulted for this article.

- Landscape `cell_events`, `next_place`, `centre_map`, `may_mark`, `in_map`:
  A $209F34, $20ACC4, $205A36/$205ACA, $20596A. Kite-like creatures' places
  hold the current view until all have been touched (from progress 7).
  Beacon activation requires a neighbouring trail/centre; it marks the
  block and opens its ramp. Covered by existing `a_places`, `a_wayon`.
- Ordinary trail requires matching form/surface, a neighbouring visited cell
  or the centre, eligible terrain, and inclusion in the local window.
  The window is centre -32 through +31 on both axes, wrapping at 4096.
  Re-centring landmarks clear it. There is no trail-length counter in this
  rule. Boundary analysis is from the recovered implementation; no new
  original-execution boundary recording was made for the guide.
- Tunnel B $301186/$302842: steering, growing openings, catch when the
  rectangle's horizontal bounds span x=81..239, timeout returns to A.
  Existing `b_tunnel` validates timeout; arena recordings use a forced catch.
- Arena C $401D24/$4028C0/$402930/$402C34: target and exit, passable walls,
  deadly active pulse (ANY wall, not only the currently lit colour), aim,
  wind, temperature changes target after collection. `c_leave`, `c_win`,
  `c_die` are prior read-for-read checks, with injected state as documented.
- Psyche P $5021B8/$505852/$5059B2/$506796: seven tiles per win, 7x6 grid,
  mouse placement, hint timing and 1,000 bonus cost. `p_play` previously
  checked wrong placement, correction, five further placements, one hint,
  and the return to A; final completion follows the recovered progress branch.

## Validation and limits

Shell checks open all six entries without an import, including scrolling the
list, compare their real renderer output to the selected page, and check
sidebar and previous/last-page navigation. Optional captures:
`build/shell_checks --mindwalker-guide .analysis/mindwalker_amiga/guide`.

The user's earlier stopped-trail incident has no captured state. The guide
explains candidate intended causes; it does not diagnose that specific event
or claim that a reconstruction bug is impossible. Exact local-window boundary
behaviour has not received a new reference replay in this change.

Validation on 2026-10-28: `./build.sh` packaged the macOS app; the shell
navigation test passed, including all six uninstalled-guide routes. All six
960x640 captures were inspected for readable text and unclipped navigation.
This is renderer/input validation, not a new original-game mechanics replay
or a manual GUI playthrough.
