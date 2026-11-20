#!/usr/bin/env bash
# usage: prior-manifest.sh OUTDIR TAG  -> manifests of every prior build (read-only)
B=/workspace/scratch/handshakeos/builds; O="$1"; T="$2"
for d in v1-firstboot v2-bigtext v2-stable-sd v2-hc v3-play v3-audio v4-multi; do
  (cd $B/$d && find . -path ./linux-7.3.5-audio -prune -o -type f -print0 | sort -z | xargs -0 sha256sum) > $O/prior-$d.$T.sha256
  # size+mtime listing of every file (kernel tree included), kept as a digest only
  echo "$( (cd $B/$d && find . -printf '%p %s %T@\n' | sort) | sha256sum | cut -d' ' -f1)  prior-$d.$T.stat" > $O/prior-$d.$T.statdigest
done
echo DONE
