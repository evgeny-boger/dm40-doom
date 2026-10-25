#!/usr/bin/env bash
# Build, bake and publish a GitHub release.
#
#   tools/release.sh TAG OPENDM40_DIR        e.g. tools/release.sh v0.1 ../opendm40
#
# Tags HEAD as TAG (annotated) unless the tag exists, builds it in a clean
# worktree, makes DOOMMCU.WAD (tools/make-wad.sh), bakes the flash cache in
# the opendm40 emulator (tools/bake.py, which needs that checkout's flash
# dumps; this is why CI does not do it), then pushes the tag and creates the
# release with dm40doom.bin, dm40-doom.elf and SHA256SUMS. The files are also
# left in build/release/TAG.
set -euo pipefail

die() { echo "release.sh: $*" >&2; exit 1; }
(($# == 2)) || die "usage: tools/release.sh TAG OPENDM40_DIR"
TAG=$1
OPENDM40=$(realpath "$2")
HERE=$(cd "$(dirname "$0")/.." && pwd)
cd "$HERE"

[ -z "$(git status --porcelain)" ] || die "uncommitted changes"
[ -f "$OPENDM40/emu/run.py" ] || die "$OPENDM40: no emu/run.py"
git rev-parse -q --verify "refs/tags/$TAG" > /dev/null || git tag -a "$TAG" -m "$TAG"

WT=$(mktemp -d)/dm40-doom-$TAG
git worktree add --detach "$WT" "$TAG"
trap 'git worktree remove --force "$WT"' EXIT
(
    cd "$WT"
    make -j"$(nproc)"
    tools/make-wad.sh
    python3 tools/bake.py "$OPENDM40" build/DOOMMCU.WAD
)

OUT=$HERE/build/release/$TAG
rm -rf "$OUT"
mkdir -p "$OUT"
cp "$WT/build/dm40doom.bin" "$WT/build/dm40-doom.elf" "$OUT/"
(cd "$OUT" && sha256sum dm40doom.bin dm40-doom.elf > SHA256SUMS)
WAD_MD5=$(sed -n 's/^DOOMMCU_MD5=//p' tools/make-wad.sh)

cat > "$OUT/notes.md" <<EOF
Doom for the Alientek DM40, $TAG ($(git rev-parse --short "$TAG^{commit}")): E1M1 and E1M2.

- \`dm40doom.bin\`: the update file for the vendor updater.
- \`dm40-doom.elf\`: symbols for the fault screen (\`arm-none-eabi-addr2line -f -i -e dm40-doom.elf <PC>\`).
- \`DOOMMCU.WAD\` is not attached: make it with \`tools/make-wad.sh\` (downloads Debian's
  doom-wad-shareware, or takes your own shareware \`doom1.wad\` v1.9). This image needs
  exactly md5 \`$WAD_MD5\`.

Install: [README](https://github.com/evgeny-boger/dm40-doom/blob/$TAG/README.md#installing-on-a-meter).
EOF

git push origin "$TAG"
gh release create "$TAG" --title "$TAG" --notes-file "$OUT/notes.md" \
    "$OUT/dm40doom.bin" "$OUT/dm40-doom.elf" "$OUT/SHA256SUMS"
