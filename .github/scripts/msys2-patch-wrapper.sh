#!/bin/sh

# MSYS2's patch needs --binary for CRLF targets, but that option breaks patches
# against LF-only files. LibreOffice's Windows external sources contain both,
# so select the mode per patch without modifying the tree during detection.

patch_input=$(mktemp "${TMPDIR:-/tmp}/lo-msys2-patch.XXXXXX") || exit 1
normalized_input=$(mktemp "${TMPDIR:-/tmp}/lo-msys2-patch-normalized.XXXXXX") || exit 1
trap 'rm -f "$patch_input" "$normalized_input"' EXIT HUP INT TERM

cat > "$patch_input" || exit 1

if /usr/bin/patch --dry-run "$@" < "$patch_input" >/dev/null 2>&1; then
    /usr/bin/patch "$@" < "$patch_input"
    exit $?
fi

if /usr/bin/patch --dry-run --binary "$@" < "$patch_input" >/dev/null 2>&1; then
    /usr/bin/patch --binary "$@" < "$patch_input"
    exit $?
fi

# A CRLF patch against an LF target fails in both modes above. Normalize only
# patch-file line endings, then retry without changing the extracted source.
sed 's/\r$//' "$patch_input" > "$normalized_input" || exit 1
if /usr/bin/patch --dry-run "$@" < "$normalized_input" >/dev/null 2>&1; then
    /usr/bin/patch "$@" < "$normalized_input"
    exit $?
fi

if /usr/bin/patch --dry-run --binary "$@" < "$normalized_input" >/dev/null 2>&1; then
    /usr/bin/patch --binary "$@" < "$normalized_input"
    exit $?
fi

# Repeat in normal mode without suppressing diagnostics so the build log shows
# the original failure and any reject files are generated as usual.
/usr/bin/patch "$@" < "$patch_input"
exit $?
