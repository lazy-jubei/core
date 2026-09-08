# Draw smart guides: design and review contract

Design owner: Codex. Implementation owner: Qwen 3.8.

## Interaction

During an ordinary shape move, compare the selection's left, horizontal center,
right, top, vertical center, and bottom with nearby eligible shapes. Align each
axis independently within a screen-pixel tolerance. Show only guides that are
true of the final constrained geometry. During resize, recognize matching width
and height; show paired dimension marks on the resized selection and reference
shape. A matching width must not silently change height unless the existing
aspect-ratio constraint requires it.

Use a small, deterministic set of guides, with endpoints spanning the compared
objects rather than an entire page. Guides are temporary drawing overlays, not
document objects. They must never enter undo history, saved files, printing, or
VSDX export. Releasing or cancelling a drag removes every guide.

## Integration boundaries

- Shared svx support defaults off. Enable it for Draw documents only.
- Existing snap-disable modifiers suppress both attraction and guide display.
- Work-area limits, axis locks, aspect-ratio locks, center-based resizing, and
  minimum valid sizes remain authoritative. Suppress a proposed guide if those
  constraints prevent its exact match; do not show an approximate match as exact.
- Do not apply whole-shape logic to glue-point or polygon-point dragging.
- Multi-selection uses its aggregate snap rectangle. Selected objects and their
  descendants must not become reference candidates. Treat unentered groups as
  units; use the active object list when editing a group.
- Candidate collection excludes invisible objects and invisible layers, and does
  not search other pages. Keep candidate storage independent of object lifetimes
  during drag, or invalidate safely on model changes.
- Use the existing magnetic snap distance converted from pixels to model units.
  Rank matches deterministically by displacement and proximity. Avoid arbitrary
  first-N selection that makes guides depend on stacking order.
- Resolve new and existing snapping once per axis. Do not stack two offsets or
  claim smart alignment after another snap/constraint moved the object away.

## Implementation structure

Separate pure candidate/match geometry from SdrDragMethod integration and overlay
rendering. The geometry result should identify the chosen reference and match
kind, enabling both constraint verification and meaningful automated tests.
Keep model scanning out of repeated overlay paint calls. Reuse the existing
overlay lifecycle wherever possible, including split views and cancellation.

For an initial implementation, if a complex transform cannot be handled
correctly, retain normal drag behavior and suppress smart snapping for that
case. Explicitly document such limitations; do not approximate rotated intrinsic
shape dimensions using an unexplained bounding-box size.

## Acceptance checks

Automated geometry tests must cover edge/center matches, nearest-candidate
selection, tolerance boundaries and misses, negative coordinates, independent
axes, equal dimensions, side versus corner resize, fixed/center anchors,
degenerate dimensions, and constrained final geometry. Integration tests should
exercise opt-in, disabled snapping, reference exclusion, and drag cancellation
where the current test harness makes this practical.

Manual checks: move and resize two separated rectangles at multiple zooms;
repeat with snap disabled, Shift constraints, multi-selection, entered groups,
hidden layers, and Escape. Check undo/redo and verify no guide objects persist in
ODG or VSDX. Distinguish source tests, compiled tests, and actual UI validation in
the status report.

Preserve the VSDX branch changes and existing untracked build scripts. Do not run
build_vsdx_export.sh: it copies old reference filter sources over the repository
and reruns configure. Prefer the existing configured build and focused targets.
