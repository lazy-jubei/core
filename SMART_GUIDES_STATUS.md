# Draw smart guides — implementation status

Branch: `libreoffice-26-8-0-jubei`. The smart-guide changes from
`libreoffice-26-8-0-smart` at `951b3aa3ae9de0925b201f7ecba654f466c1dd72`
were rebased onto the 26.8.0-based VSDX branch at
`68ac8fc67b3d0093b3d7f9c42c506c1d9c5e3e53`. The upstream release base is
`libreoffice-26.8.0.3` (LibreOffice 26.8.0 final).

Existing VSDX source changes are preserved. Windows and macOS push builds
now target the Jubei branch; manual dispatch remains available. macOS app
bundles are development builds without Developer ID signing or notarization.

Jubei defaults to the Tabbed interface in Writer, Calc, Impress and Draw,
and Light application appearance. These are defaults for new profiles;
existing saved user choices continue to take precedence.

The verification results below were recorded on the original smart-guide
branch. They have not been rerun against this rebased branch. See
`JUBEI.md` for the rebase validation and remaining build checks.

## Implemented

- Live edge/center alignment while moving shapes, with independent axis matching.
- Resize alignment and equal-width/equal-height matching, with paired dimension marks.
- Pixel-scaled magnetic tolerance, deterministic candidate ordering and competition with existing snapping.
- References from the active object list (including entered-group context), excluding selected, invisible and hidden-layer objects. References use snap rectangles.
- Draw-only opt-in; other applications retain the shared default of disabled.
- Transient overlay rendering independent of the old full-page drag stripes, using existing drag-overlay cleanup.
- Final constrained-geometry validation, size-validity gating and redraw when modifiers change factors or guide state without changing the handle position.
- Temporary suppression follows Draw's existing snap modifier path, separately from feature opt-in. Candidate caching permits guides to return when the modifier is released during the same drag.

## Verified

- Full native `CppunitTest_svx_unit` passed **139/139**, including 23 smart-guide geometry cases and 11 actual drag-pipeline integration cases. Final run: `../artifacts/smart-guides/native-tests-5.log`; detailed results: `svx-unit-passed.log` in the same directory.
- Native Windows `Library_svxcore` and `Library_sd` builds completed, including required runtime dependencies. The rebuilt Draw application was launched from `instdir/program/soffice.exe` and tested with an isolated profile. This is a usable development build; no installer package was created.
- Actual mouse tests at 74% zoom with grid snapping disabled: center alignment, equal-width resize, equal-height resize, and resize undo/redo passed. Saved FODG confirms exact centimeter geometry and only the two intended shapes.
- Actual rebuilt-application ODG save/reopen and VSDX export passed. Shape counts and dimensions were checked; the native VSDX regression suite also passed **13/13**.
- 23 repository geometry tests compile and pass against real configured LibreOffice headers/libraries and CppUnit.
- The expanded suite also passes two independent manager regression cases: **25/25**. They reproduce exact width 110 from width 100 and the inclusive tolerance boundary that previously failed due to floating-point error.
- Focused MSVC syntax checks pass for `svddrgmt.cxx`, `svdsnpv.cxx`, `drawview.cxx` and `fudraw.cxx`.
- `git diff --check` passes.
- Existing Python VSDX reference suite: 33/33 pass. This is the reference exporter suite, not a native application export test.

Validation scripts/logs are in `../artifacts/smart-guides/`: `run-geometry-tests.ps1`, `syntax-check.ps1`, `geometry-final-review.log`, and `*-final-syntax.log`.

## Pending and limitations

The application build and basic mouse checks are complete. Live overlay appearance while the mouse is held remains unverified: the UI automation releases the button before its next screenshot. Final snapping behavior was verified independently through saved geometry.

- See `SMART_GUIDES_QA.md` for the pending manual test matrix.
- Rotated/sheared selected shapes and references are excluded from smart resizing. Moving still aligns snap bounds. Point and glue-point drags retain ordinary behavior.
- Fractional effective resize handles that cannot be represented by the integer helper suppress the relevant guide. Existing resize constraints remain authoritative.
- This patch has no saved preference or new menu toggle. Guides are enabled for Draw and temporarily suppressed by snap modifiers.
- Performance on very large pages, multiple zoom levels, accessibility/high-contrast appearance and the remaining modifier/group scenarios need further application testing. See the completed checks and remaining matrix in `SMART_GUIDES_QA.md`.
- Do not use `build_vsdx_export.sh` for this branch: it copies older reference filter sources over the current export implementation.

## Authorship and review

Codex led design and review. Qwen 3.8 authored the geometry helper, repository tests, initial drag/overlay integration and Draw opt-in, and corrected geometry/compiler findings. Codex added independent regression checks and targeted final integration corrections (final resize factors, candidate bounds, size validity, modifier suppression and existing-snap ties). Qwen reviewed the six blocking integration findings as addressed.

During native validation, Qwen added the 11 drag integration tests and corrected fixture setup. Codex diagnosed the remaining fixture crash with cdb and added the missing SfxApplication initialization. Build workarounds and results are recorded in `../artifacts/smart-guides/NATIVE_BUILD_NOTES.md`.

Recorded Qwen sessions remain available through the shared Qwen Desktop chat store:
- Geometry corrections: `017cd820-4ccb-4ccb-80ef-74231a325574`
- Main integration: `61bf428e-4775-44d2-b774-3c3f1e0c84f8`
- Final independent review: `3d6598b3-4fee-4d9f-8bcf-e6f36b99ab68`
- Native drag test implementation: `2f662692-67fd-4923-9f5f-aaf4f3b5de31`
