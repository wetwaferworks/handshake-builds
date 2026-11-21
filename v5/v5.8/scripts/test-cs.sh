#!/usr/bin/env bash
set -eu
HS_ROOT="${HS_ROOT:-$(cd "$(dirname "$0")/../../../.." && pwd)}"
d=$HS_ROOT/builds/v5/v5.8/_cs_test
rm -rf "$d"
mkdir -p "$d"
fsutil.exe file setCaseSensitiveInfo "${HANDSHAKE_ROOT_WIN:-handshake\builds\v5\v5.8}\_cs_test" enable
echo set_rc=$?
fsutil.exe file queryCaseSensitiveInfo "${HANDSHAKE_ROOT_WIN:-handshake\builds\v5\v5.8}\_cs_test"
touch "$d/Foo"
touch "$d/foo"
echo 'listing:'
ls -l "$d"
n=$(ls "$d" | wc -l)
echo "entries=$n"
rm -rf "$d"
if [ "$n" -eq 2 ]; then
  echo CASE_SENSITIVE_OK
else
  echo CASE_SENSITIVE_FAIL
  exit 1
fi
