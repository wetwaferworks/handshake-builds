#!/usr/bin/env bash
# v5.3: prove prior builds and v5..v5.2 artifacts unchanged. usage: priors-v5.3.sh TAG
set -u
B=/mnt/c/Users/jdwetwork/.aaa.www.inc/handshake/builds
V5=$B/v5; O=$V5/v5.3/logs; TAG=$1
export TMPDIR=$V5/v5.3/tmp; mkdir -p "$TMPDIR" "$O"
bash "$V5/tools/prior-manifest.sh" "$O" "$TAG" >/dev/null
ref=""
for d in "$V5"/v5.2/logs "$V5"/v5.1/logs "$V5"/logs; do ls "$d"/prior-v4-full.*.sha256 >/dev/null 2>&1 && { ref=$d; break; }; done
echo "reference prior manifests: ${ref:-none}"
for d in v1-firstboot v2-bigtext v2-stable-sd v2-hc v3-play v3-audio v4-multi v4-full; do
  n=$(wc -l < "$O/prior-$d.$TAG.sha256")
  r=""
  if [ -n "$ref" ]; then f=$(ls "$ref"/prior-$d.*.sha256 2>/dev/null | tail -n 1); [ -n "$f" ] && { cmp -s "$f" "$O/prior-$d.$TAG.sha256" && r="IDENTICAL_TO $(basename "$f")" || r="DIFFERS_FROM $(basename "$f")"; }; fi
  echo "PRIOR $d files=$n $r"
done
# v5 / v5.1 / v5.2 artifacts by their own SHA256SUMS files
cd "$V5"
for s in SHA256SUMS SHA256SUMS-v5.1 SHA256SUMS-v5.2; do
  t=$(wc -l < "$s"); ok=$(sha256sum -c "$s" 2>/dev/null | grep -c ': OK$')
  echo "SUMS $s ok=$ok/$t"
done
# everything in builds/v5 that is not v5.3
find . \( -path ./v5.3 -o -name '*v5.3*' -o -name EVIDENCE.txt \) -prune -o -type f -print0 | sort -z | xargs -0 sha256sum > "$O/v5-nonv53.$TAG.sha256"
echo "V5_NON_V53 files=$(wc -l < "$O/v5-nonv53.$TAG.sha256") manifest=$(sha256sum "$O/v5-nonv53.$TAG.sha256" | cut -c1-16)"
