#!/bin/sh

# MSYS2's default winsymlinks:deepcopy mode cannot create an archived symlink
# until its target exists. Some upstream archives list the symlink first.
# Retrying extraction after the first pass has created the target is the
# workaround documented by MSYS2.

is_extraction=
for argument in "$@"; do
    case "$argument" in
        --extract|--get)
            is_extraction=1
            ;;
        --*)
            ;;
        -*)
            case "${argument#-}" in
                *x*) is_extraction=1 ;;
            esac
            ;;
    esac
done

/usr/bin/tar "$@"
status=$?
if [ "$status" -eq 0 ] || [ -z "$is_extraction" ]; then
    exit "$status"
fi

echo "MSYS2 tar extraction failed once; retrying after targets were created" >&2
exec /usr/bin/tar "$@"
