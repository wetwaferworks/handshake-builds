#!/usr/bin/env bash
# usage: prior-manifest.sh OUTDIR TAG -> sha256 of every file in every prior build (read-only)
B=/mnt/c/Users/jdwetwork/.aaa.www.inc/handshake/builds; O="$1"; T="$2"
for d in v1-firstboot v2-bigtext v2-stable-sd v2-hc v3-play v3-audio v4-multi v4-full; do
  (cd $B/$d && find . -path ./linux-7.3.5-audio -prune -o -type f -print0 | sort -z | xargs -0 sha256sum) > $O/prior-$d.$T.sha256
done
echo DONE
