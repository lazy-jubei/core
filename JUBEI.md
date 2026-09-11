# LibreOffice Jubei

Branch: `libreoffice-26-8-0-jubei`

## Source lineage

This branch carries the Draw smart-guide work from
`libreoffice-26-8-0-smart` (`951b3aa3ae9de0925b201f7ecba654f466c1dd72`)
onto the existing 26.8.0 VSDX branch
(`68ac8fc67b3d0093b3d7f9c42c506c1d9c5e3e53`). Its upstream release base is
`libreoffice-26.8.0.3`, the final LibreOffice 26.8.0 source tag.

The newer 27.2 development-base commits are not included. The HarfBuzz
MSVC environment fix and VSDX media-descriptor include fix were already
present in the 26.8.0 VSDX base and were not duplicated.

## Defaults

- Tabbed interface in Writer, Calc, Impress and Draw.
- Light application appearance, independently of the operating system theme.
- Existing saved profile settings remain user-controlled and take precedence.
- Smart guides and VSDX export are retained.

## Validation

The upstream release ancestry, source version, XML configuration, notebookbar
mode mappings, preservation of the smart-guide implementation/tests, and
patch whitespace were checked after rebasing. Windows/macOS push triggers
have been updated to this branch.

A full native build and the smart-guide C++ tests have not been run for this
branch. Historical results in SMART_GUIDES_STATUS.md apply to the original
smart-guide branch, not this rebase. Validate a fresh profile on a completed
build: open Writer, Calc, Impress and Draw; confirm Tabbed and Light defaults;
then exercise Draw alignment, equal-size resizing, undo/redo and VSDX export.
