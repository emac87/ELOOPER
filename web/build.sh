#!/usr/bin/env bash
# Builds the web version of Chimera into one self-contained page: web/dist/chimera.html
# Needs Emscripten (emcc/em++ on PATH, or EMSDK pointing at an emsdk checkout).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
if ! command -v em++ >/dev/null 2>&1; then
    # shellcheck disable=SC1091
    source "${EMSDK:-$HOME/emsdk}/emsdk_env.sh" >/dev/null
fi
mkdir -p "$here/build" "$here/dist"

em++ -std=c++17 -O3 -fwasm-exceptions -I "$root/Source" \
    "$here/src/chimera_wasm.cpp" "$root"/Source/Synth/dsp/*.cpp \
    -o "$here/build/chimera.js" \
    -sMODULARIZE=1 -sEXPORT_NAME=createChimera -sENVIRONMENT=shell \
    -sFILESYSTEM=0 -sTEXTDECODER=1 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=64MB \
    -sEXPORTED_FUNCTIONS=_malloc,_free \
    -sEXPORTED_RUNTIME_METHODS=UTF8ToString,stringToNewUTF8,HEAPU8,HEAPF32 \
    -sDYNAMIC_EXECUTION=0 -sASSERTIONS=0

python3 - "$here" <<'PY'
import base64, sys, pathlib
here = pathlib.Path(sys.argv[1])
src, build = here / "src", here / "build"
def js(text):   # safe inside a <script> element
    return text.replace("</script", "<\\/script").replace("<!--", "<\\!--")
html = (src / "index.html").read_text()
css = (src / "style.css").read_text()
glue = (build / "chimera.js").read_text()
worklet = glue + "\n" + (src / "worklet.js").read_text()
wasm = base64.b64encode((build / "chimera.wasm").read_bytes()).decode()
link = '<link rel="stylesheet" href="style.css">'
tag = '<script src="app.js"></script>'
assert link in html and tag in html, "index.html must contain the exact style.css link and app.js script tags"
html = html.replace(link, "<style>\n" + css + "\n</style>")
scripts = (
    '<script type="text/plain" id="chimera-wasm">' + wasm + "</script>\n"
    '<script type="text/plain" id="chimera-worklet-src">' + js(worklet) + "</script>\n"
    "<script>\n" + js((src / "loader.js").read_text()) + "\n</script>\n"
    "<script>\n" + js((src / "app.js").read_text()) + "\n</script>"
)
html = html.replace(tag, scripts)
out = here / "dist" / "chimera.html"
out.write_text(html)
print(f"{out}  {out.stat().st_size / 1e6:.2f} MB")
PY
