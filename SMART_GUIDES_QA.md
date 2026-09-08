# Draw smart guides — application QA checklist

Status: **native Draw libraries built and application smoke testing performed on 2026-09-07**. Tests used this branch's `instdir/program/soffice.exe`, with an isolated profile. This is a development build, not an installer package.

## Completed application checks

- Full native `CppunitTest_svx_unit`: **139/139**, including all 23 geometry and 11 drag integration tests. Drag tests cover feature-off behavior, suppression, snap-off at drag start, selected-reference exclusion, cancellation, equal-size resizing and aspect-ratio constraints.
- At 74% zoom, with grid snapping disabled, dragged a 2 cm wide rectangle near the center of a 4 cm reference. The resulting x position was exactly 6 cm (reference x=5 cm), verified in the saved FODG.
- Dragged its right handle near the reference width: snapped to exactly 4 cm. Undo restored 2 cm and redo restored 4 cm.
- Dragged its bottom handle near the reference height: snapped to exactly 2 cm. Saved FODG confirms both shapes are 4 x 2 cm and contains only the two intended rectangles.
- Rebuilt application UNO smoke: saved and reopened ODG with exact shape geometry; exported VSDX and verified two shapes and their dimensions in page XML. This persistence test uses API edits, separately from the mouse tests above.
- Native VSDX regression suite passed: **13/13**.

Evidence: `../artifacts/smart-guides/smart-guides-ui.fodg`, `application-smoke-result.json`, `application-smoke.odg`, `application-smoke.vsdx`, and `native-visio-tests.log`. Full native test output is under `workdir/CppunitTest/`.

The UI automation releases the mouse before taking its next screenshot. Final snapping behavior was verified; live overlay appearance while the button is held has **not** been visually verified. The broader matrix below remains a follow-up checklist except for the specific checks recorded above.

Create a Draw document containing several separated rectangles with different widths/heights, one group, and one hidden layer. Repeat at low, normal and high zoom.

| Check | Expected result |
|---|---|
| Move a rectangle near another's edges and centers | Nearby horizontal/vertical features snap independently and a live guide spans the relevant shapes. Guides disappear outside magnetic tolerance. |
| Move near two competing references | Closest displacement wins deterministically; changing stacking order does not change geometric tie selection. |
| Resize side and corner handles | Edge/center alignments and equal dimensions can snap; equal dimensions show paired marks. |
| Exact width 110 from width 100; target at tolerance boundary | The equal-size match remains visible without flicker or missed boundary snapping. |
| Already exactly on an existing snap target | A more distant smart match does not displace the existing exact snap. |
| Shift/aspect-ratio constraints, centered resize, work-area limits | Normal constraints hold. Only matches true of the final shape geometry are shown. |
| Change Shift without moving the handle | Shape factors and guides update together; guides never describe a stale preview. |
| Hold/release Ctrl and Alt during movement | Verify both shared global snap-disable and Draw's application snap modifier behavior, preserving existing copy/center semantics. Guides clear immediately and can return in the same drag. |
| Start drag with snap suppression, then release it | Candidates are available and guides can appear without restarting the drag. |
| Invalid/minimum-size resize preview | Smart guide overlays are suppressed consistently with invalid shape geometry. |
| Multi-selection, unopened group, entered group | Aggregate selection and active-group references are used consistently. Selected objects never become their own references. |
| Invisible objects and hidden layers | They do not provide references. |
| Rotated/sheared shape resize; point/glue-point drag | Unsupported smart resize cases use ordinary behavior without misleading equal-size guides. |
| Escape, mouse-up, new drag, switch view/split view | Transient guides clear through the drag overlay lifecycle. |
| Undo/redo | Only the actual shape edit is undoable; no guide objects or extra document edits appear. |
| Save/reopen ODG and export/reopen VSDX | Shape changes persist; smart guide overlays never become document/export objects. |
| High contrast and large pages | Guides remain legible; mouse movement remains responsive. Record object count, zoom and latency for performance findings. |
| Open Impress/Writer/Calc | Shared smart guides remain off unless explicitly enabled by an application. |

Record build revision, exact reproduction steps, screenshots where useful, and pass/fail results before treating this source checkpoint as a validated application feature.
