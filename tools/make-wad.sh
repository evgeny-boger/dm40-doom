#!/usr/bin/env bash
# Make DOOMMCU.WAD from the shareware doom1.wad (v1.9).
#
#   tools/make-wad.sh [doom1.wad]      -> build/DOOMMCU.WAD
#
# Without an argument doom1.wad is taken from Debian's doom-wad-shareware
# package (downloaded into build/wad/, checked against the sha256 below).
# The result is checked against DOOMMCU_MD5, so every build of the WAD is the
# same file (the converter is deterministic; a different WAD only makes Doom
# rebuild its flash cache at start).
set -euo pipefail

DEB_URL=http://ftp.debian.org/debian/pool/non-free/d/doom-wad-shareware/doom-wad-shareware_1.9.fixed-5_all.deb
DEB_SHA256=5802f176c0303e228095b5312def53de602781cf4c53e79842257484a0d9e938
DOOM1_MD5=f0cefca49926d00903cf57551d901abe
DOOMMCU_MD5=e351817f0afec143e640fcb250f72395

HERE=$(cd "$(dirname "$0")/.." && pwd)
OUT=$HERE/build
mkdir -p "$OUT/wad"
die() { echo "make-wad.sh: $*" >&2; exit 1; }

if (($#)); then
    WAD=$(realpath "$1")
else
    DEB=$OUT/wad/doom-wad-shareware.deb
    [ -f "$DEB" ] || curl -fsSL -o "$DEB" "$DEB_URL"
    echo "$DEB_SHA256  $DEB" | sha256sum -c --quiet || die "$DEB: sha256 differs"
    dpkg-deb -x "$DEB" "$OUT/wad/x"
    WAD=$OUT/wad/x/usr/share/games/doom/doom1.wad
fi
[ "$(md5sum < "$WAD" | cut -d' ' -f1)" = "$DOOM1_MD5" ] || die "$WAD is not the shareware doom1.wad v1.9 (md5 $DOOM1_MD5)"

cc -std=gnu99 -O2 -w -Wno-error=incompatible-pointer-types -o "$HERE/wadutil/mcudoomwadutil" "$HERE"/wadutil/*.c
# mcudoomwadutil reads mcudoom_0_4.wad from the current directory.
(cd "$HERE/wadutil" && ./mcudoomwadutil "$WAD" "$OUT/DOOMMCU.WAD" > "$OUT/wad/convert.log")
[ "$(md5sum < "$OUT/DOOMMCU.WAD" | cut -d' ' -f1)" = "$DOOMMCU_MD5" ] || die "build/DOOMMCU.WAD: md5 differs from $DOOMMCU_MD5"
echo "build/DOOMMCU.WAD (md5 $DOOMMCU_MD5)"
