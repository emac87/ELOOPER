#!/usr/bin/env bash
# Syntax/type-check individual source files against the real JUCE build flags, without
# building the whole app. Safe to run in parallel.   Usage: tools/check.sh Source/UI/Foo.cpp [...]
set -euo pipefail
cd "$(dirname "$0")/.."
DB=build/compile_commands.json
[ -f "$DB" ] || { echo "Run: cmake -S . -B build -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ... first"; exit 2; }
status=0
for f in "$@"; do
  abs="$(realpath "$f")"
  # Use the ELooper target's flags (any app source works; files are globbed at configure time).
  cmd=$(python3 - "$DB" "$abs" <<'PY'
import json,sys,shlex
db=json.load(open(sys.argv[1])); target=sys.argv[2]
ent=[e for e in db if e['file']==target and '/ELooper.dir/' in e.get('output',e['command'])]
if not ent: ent=[e for e in db if '/ELooper.dir/' in e.get('output',e['command']) and e['file'].endswith('.cpp')]
e=ent[0]; args=shlex.split(e['command'])
out=[]; skip=False
for a in args:
    if skip: skip=False; continue
    if a in ('-o','-MF','-MT','-MQ'): skip=True; continue
    if a in ('-c','-MD','-MMD'): continue
    if a==e['file'] or a.endswith(e['file'].split('/')[-1]) and a.endswith('.cpp'): continue
    out.append(a)
print(e['directory']+'|||'+' '.join(shlex.quote(x) for x in out))
PY
)
  dir="${cmd%%|||*}"; flags="${cmd#*|||}"
  echo "== $f"
  (cd "$dir" && eval "$flags -fsyntax-only -Wno-unused-parameter \"$abs\"") || status=1
done
exit $status
