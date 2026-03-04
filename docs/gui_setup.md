# Playing Against FollyChess

These steps assume you already cloned both projects on the same machine. The workflow below uses **Cute Chess** as a desktop GUI, but any UCI-compatible frontend (Arena, Banksia, etc.) will work.

## 1. Build HyperSearch (this project)
```bash
cmake -S . -B build
cmake --build build --config Release
```
The UCI binary is produced at `build/hyperchess` (or `build/Release/hyperchess` on Windows/MSVC).

## 2. Build FollyChess
Follow the instructions in the FollyChess repository, e.g.
```bash
cmake -S path/to/follychess -B folly-build
cmake --build folly-build --config Release
```
The resulting engine binary (often named `follychess`) is the one you will register inside the GUI.

## 3. Install Cute Chess
- macOS: `brew install cutechess`
- Linux: use your package manager or download from <https://cutechess.com/>
- Windows: download the latest build from the same site.

## 4. Register Both Engines
1. Launch Cute Chess GUI.
2. Open *Engines → Manage... → Add*.
   - Choose the `hyperchess` binary built in step 1. Set the protocol to **UCI**.
   - Add the FollyChess binary from step 2 the same way.
3. (Optional) Under *Engine Options* you can adjust `Hash` for both engines; HyperSearch understands the standard UCI *Hash* option.

## 5. Create a Match
1. Go to *Tournaments → New...* and select **Match**.
2. Add both engines, give each the desired number of games (e.g., 50) and color alternation.
3. Set the time control to **3 minutes + 0 seconds** (or your preferred rapid control).
4. Disable opening books if you want pure engine strength comparison, or point both engines to the same book file.
5. Start the match and monitor results in the GUI. Cute Chess records PGNs and statistics automatically; export them for later analysis via *File → Save Games*.

## 6. Command-Line Alternative
For automation/regression, Cute Chess also provides `cutechess-cli`. Example (adjust paths as needed):
```bash
cutechess-cli \
  -engine cmd="./build/hyperchess" name=HyperSearch \
  -engine cmd="../folly-build/follychess" name=FollyChess \
  -each tc=3+0 proto=uci \
  -rounds 50 -openings file=noob.pgn format=pgn order=random \
  -resign movecount=3 score=400 \
  -draw movenumber=80 movecount=10 score=5 \
  -pgnout matches.pgn
```

## 7. Optional: CLI Benchmark Mode
The engine still supports the previous single-search command-line mode using
```bash
./build/hyperchess --cli --fen "<FEN>" --depth 10
```
but GUIs should run the binary without `--cli` so it stays in UCI mode.
