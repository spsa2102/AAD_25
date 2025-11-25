# DETI Coin WebAssembly Implementation

## Prerequisites

- Emscripten SDK:

Add to your `~/.bashrc` for automatic activation:
```bash
echo 'source ~/emsdk/emsdk_env.sh' >> ~/.bashrc
```

## Quick Start

**Option 1: Build, serve, and open in one command**
```bash
bash compile_wasm.sh --serve --open
```

**Option 2: Interactive menu**
```bash
bash compile_wasm.sh
```
Then select:
- `1` - Build only
- `2` - Build + Serve
- `3` - Build + Serve + Open in browser
- `4` - Serve existing build (skip compilation)

**Option 3: Manual steps**
```bash
bash compile_wasm.sh --build

bash compile_wasm.sh --serve --port 8080

firefox http://localhost:8000/benchmark.html
```

## What Gets Built

The script compiles:
- `wasm_search.c` → `WebAssembly/wasm_search_scalar.js` + `.wasm` (scalar SHA1)
- `wasm_simd_search.c` → `WebAssembly/wasm_search_simd.js` + `.wasm` (4-lane SIMD)

Both include:
- Adjustable difficulty (1-8 hex chars matching `0xAAD20250`)
- Multi-coin buffer exports (up to 128 coins)
- First coin capture for quick display

## Using the Benchmark Page

Once `benchmark.html` loads:

1. **Load Modules** - Click to initialize WASM
2. **Set Difficulty** - Lower = easier (5 recommended for quick tests, 8 = full match)
3. **Iterations** - Number of nonces to try per run (300,000 default)
4. **Run buttons:**
   - **Run Scalar** - Single-threaded search
   - **Run SIMD** - 4-lane parallel search
   - **Compare** - Run both and show speedup
   - **Until Coin (SIMD)** - Loop until coin found (stops at 10× expected attempts)

### Expected Performance

**Difficulty vs Attempts:**
- Difficulty 1: ~16 attempts avg
- Difficulty 2: ~256 attempts avg
- Difficulty 5: ~1,048,576 attempts avg (~1M)
- Difficulty 8: ~4,294,967,296 attempts avg (~4.3B) - **very slow!**

**Typical speed on modern CPU:**
- Scalar: 5-10 MH/s
- SIMD: 20-60 MH/s (2-6× speedup)

### Coins Display

Found coins appear in the **Coins** section as:
```
V05:DETI coin 2 <random_text>        <nonce>
```
Format: `Vdd:` = difficulty tag (e.g., V05 = 5 hex chars matched)

## Troubleshooting

**"Modules not loaded"**
- Click **Load Modules** button first
- Check browser console (F12) for errors

**No coins found**
- Lower difficulty (try 5 or 4)
- Increase iterations or use **Until Coin** button
- Expected attempts ≈ 16^difficulty

**Blank Coins section despite "coins=1"**
- Hard refresh (Ctrl+Shift+R) to reload WASM
- Rebuild: `bash compile_wasm.sh --build`

**Build errors**
- Ensure Emscripten is sourced: `source ~/emsdk/emsdk_env.sh`
- Check `emcc --version` works

## Files

| File | Purpose |
|------|---------|
| `wasm_search.c` | Scalar SHA1 coin search implementation |
| `wasm_simd_search.c` | SIMD (4-lane) SHA1 coin search |
| `compile_wasm.sh` | Build script with interactive options |
| `WebAssembly/benchmark.html` | Test UI (static, not generated) |
| `WebAssembly/*.js`, `*.wasm` | Compiled outputs (gitignored) |

## Difficulty System

The difficulty controls how many leading hex nibbles of `hash[0]` must match `0xAAD20250`:

- **Difficulty 1:** Match `0x???????0` (last nibble = 0)
- **Difficulty 5:** Match `0xAAD2?250` (5 hex chars)
- **Difficulty 8:** Match `0xAAD20250` (full 32-bit signature)

Lower difficulty = faster coin discovery for testing.

## Development

Edit C sources, then rebuild:
```bash
bash compile_wasm.sh --build
```

