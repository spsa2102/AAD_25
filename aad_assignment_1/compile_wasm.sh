#!/bin/bash
set -e

SERVE=0; OPEN=0; PORT=8000; DO_BUILD=1
ARGCOUNT=$#
while [[ $# -gt 0 ]]; do
  case "$1" in
    --serve) SERVE=1 ;;
    --open) OPEN=1; SERVE=1 ;;
    --port) shift; PORT=${1:-8000} ;;
    --no-build) DO_BUILD=0 ;;
    -h|--help)
      echo "Usage: $0 [--serve] [--open] [--port N] [--no-build]"; exit 0 ;;
    *) echo "Unknown arg: $1"; exit 1 ;;
  esac; shift
done

# Interactive menu if no flags passed and running in a TTY
if [[ $ARGCOUNT -eq 0 && -t 0 ]]; then
  echo "Select mode:"; echo "  1) Build"; echo "  2) Build + Serve"; echo "  3) Build + Serve + Open"; echo "  4) Serve existing (no build)"; echo "  5) Quit";
  read -r -p "Choice [1-5]: " CHOICE
  case "$CHOICE" in
    1) ;; # defaults already
    2) SERVE=1 ;;
    3) SERVE=1; OPEN=1 ;;
    4) DO_BUILD=0; SERVE=1 ;;
    5) echo "Bye"; exit 0 ;;
    *) echo "Invalid choice"; exit 1 ;;
  esac
  if [[ $SERVE -eq 1 ]]; then
    read -r -p "Port [$PORT]: " PORT_IN
    [[ -n $PORT_IN ]] && PORT=$PORT_IN
  fi
fi

echo "DETI Coin WASM build (scalar + SIMD)"
echo "Mode: DO_BUILD=$DO_BUILD SERVE=$SERVE OPEN=$OPEN PORT=$PORT"

if [[ $DO_BUILD -eq 1 ]]; then
  if ! command -v emcc &>/dev/null; then
    if [[ -f "$HOME/emsdk/emsdk_env.sh" ]]; then
      # shellcheck disable=SC1090
      source "$HOME/emsdk/emsdk_env.sh"
    fi
  fi
  if ! command -v emcc &>/dev/null; then
    echo "Emscripten not found (install emsdk or source env)."; exit 1
  fi
  echo "emcc: $(emcc --version | head -n 1)"
fi

# Create output directory
mkdir -p WebAssembly

if [[ $DO_BUILD -eq 1 ]]; then
  mkdir -p WebAssembly
  # Clean export lists (previous corruption removed)
  SCALAR_EXPORTS='["_search_coins","_set_difficulty","_get_attempts","_get_coins_found","_get_first_coin_nonce","_get_first_coin_ptr","_get_first_coin_length","_get_coins_buffer_ptr","_get_coins_buffer_length","_get_random_nonce","_malloc","_free"]'
  SIMD_EXPORTS='["_search_coins_simd","_set_difficulty_simd","_get_attempts_simd","_get_coins_found_simd","_get_first_coin_nonce_simd","_get_first_coin_ptr_simd","_get_first_coin_length_simd","_get_coins_buffer_ptr_simd","_get_coins_buffer_length_simd","_malloc","_free"]'
  emcc -O3 wasm_search.c -o WebAssembly/wasm_search_scalar.js \
    -s EXPORTED_FUNCTIONS="$SCALAR_EXPORTS" \
    -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","HEAPU8"]' -s ALLOW_MEMORY_GROWTH=1 \
    -s MODULARIZE=1 -s EXPORT_NAME='WasmSearchScalar'
  emcc -O3 -msimd128 wasm_simd_search.c -o WebAssembly/wasm_search_simd.js \
    -s EXPORTED_FUNCTIONS="$SIMD_EXPORTS" \
    -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","HEAPU8"]' -s ALLOW_MEMORY_GROWTH=1 \
    -s MODULARIZE=1 -s EXPORT_NAME='WasmSearchSIMD'
else
  echo "Skip build (--no-build)"
fi

ls -1 WebAssembly | grep wasm_search_ >/dev/null 2>&1 || echo "(No wasm_search_* artifacts found)"
if [[ $SERVE -eq 1 ]]; then
  echo "Serving from WebAssembly/ directory"
  echo "URL: http://localhost:$PORT/benchmark.html"
else
  echo "Run with --serve to start a server (URL then: http://localhost:$PORT/benchmark.html)"
fi

if [[ $SERVE -eq 1 ]]; then
  PY_CMD="python3"; command -v python3 >/dev/null 2>&1 || PY_CMD="python"
  ( cd WebAssembly && $PY_CMD -m http.server "$PORT" ) & SERVER_PID=$!
  sleep 1
  if [[ $OPEN -eq 1 ]] && command -v xdg-open >/dev/null 2>&1; then
    xdg-open "http://localhost:$PORT/benchmark.html" >/dev/null 2>&1 || true
  fi
  wait $SERVER_PID
fi