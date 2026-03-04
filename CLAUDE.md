# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

HyperSearchChess — a C++20 chess engine focused on raw search speed. Single-threaded, designed with future SMP in mind. Supports the UCI protocol for GUI integration.

## Build Commands

```bash
# Configure (out-of-source build, default RelWithDebInfo)
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo

# Build all targets
cmake --build build -j$(sysctl -n hw.ncpu)

# Debug build (enables ASan/UBSan)
cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug -j$(sysctl -n hw.ncpu)

# Release build (-Ofast -march=native)
cmake -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j$(sysctl -n hw.ncpu)
```

## Running

```bash
# UCI mode (default) — connects to any UCI-compatible GUI
./build/hyperchess

# Direct CLI mode (bypasses UCI, useful for quick testing)
./build/hyperchess --cli --depth 7
./build/hyperchess --cli --fen "r1bqkbnr/pppppppp/2n5/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 1 2" --depth 6
./build/hyperchess --cli --movetime 5000
./build/hyperchess --cli --wtime 60000 --btime 60000 --winc 1000 --binc 1000
./build/hyperchess --cli --infinite

# Perft (move generation correctness validation)
./build/perft --depth 6
./build/perft --fen "..." --depth 5

# Benchmark (NPS across 4 test positions)
./build/bench

# Engine-vs-engine match (builds both engines, requires FollyChess in parent dir)
./compete.sh                    # 10 games, 5s+0.1s TC
GAMES=20 DEPTH=8 ./compete.sh   # fixed depth, 20 games
```

## Architecture

All code lives in the `chess` namespace. Headers in `include/chess/`, implementations in `src/`.

**Core data flow:** `Position` (board state + move gen) → `Search` (iterative deepening negamax) → `evaluate(pos, eval_state)` → score.

### Key modules

- **types.hpp** — Foundational types: `Bitboard` (uint64), `Move` struct, `Color`, `Piece`, `SearchLimits`. All other modules depend on this.
- **bitboard.hpp/cpp** — Square enum (A1=0..H8=63), precomputed attack tables. `init_attack_tables()` must be called before any position work. Uses classical sliding approach (not magic bitboards yet).
- **eval_state.hpp** — `EvalState` struct with per-color MG/EG accumulators for material, PST, pawn structure, mobility, and king safety. Dirty flags (`pawn_dirty`, `mobility_dirty`, `king_dirty`) trigger lazy recomputation. Stored inside `StateInfo` for automatic make/unmake tracking.
- **evaluation.hpp/cpp** — Incremental tapered evaluation. `init_evaluation()` must be called at startup. `eval_on_piece_add()`/`eval_on_piece_remove()` update EvalState deltas on make/unmake. Full eval includes: material + PST (MG/EG), pawn structure (doubled/isolated/backward/passed + shield/storm via `PawnTable` hash cache), piece mobility (N/B/R/Q pseudo-legal move counts). Phase blend: `(mg * phase + eg * (24 - phase)) / 24`. Tempo bonus +10cp.
- **position.hpp/cpp** — Board representation using per-color, per-piece bitboards (`pieces_[2][6]`). FEN parsing, legal move generation, make/unmake with incremental Zobrist and EvalState updates. `StateInfo` stack stores zobrist, castling, EP, fifty-move counter, and `EvalState` per ply. `eval_state()` accessor for the current ply's eval.
- **search.hpp/cpp** — Iterative deepening negamax with: null-move pruning (R=2 + verification), LMR (depth/move-index table), razoring (depth ≤ 1), futility pruning (depth ≤ 2), delta pruning in qsearch, simplified SEE for move pruning/ordering. Move ordering: TT move → captures (SEE-scored) → promotions → killers (2/ply) → counter-move → history (`history_[2][64][64]`). PV tracking via `pv_table_`. `TimeManager` handles soft/hard time limits. Public `stop()` for UCI abort.
- **transposition_table.hpp/cpp** — Clustered 4-entry buckets (`TTBucket`) with generation + depth replacement policy. Hardware prefetch via `__builtin_prefetch`. Stats tracking (lookups/hits/stores/replacements). 128MB default in UCI mode.
- **move.hpp/cpp** — Move flag constants (bitmask), null move factory, UCI string conversion.
- **uci.hpp/cpp** — UCI protocol implementation. Runs a stdin command loop via `uci::loop()`. Internal `Engine` class owns `Position`, `TranspositionTable` (128MB), and `Search`. Search runs on a separate `std::thread`; supports `stop` command for async abort. `setoption name Hash` for configurable TT size (1–4096 MB).

### Important invariants

- `init_attack_tables()` and `init_evaluation()` must be called once at startup before creating any `Position`.
- `Position::make_move()` returns false if the move leaves the king in check (pseudo-legal filtering). Always check the return value.
- `add_piece()`/`remove_piece()` in Position call `eval_on_piece_add()`/`eval_on_piece_remove()` to keep EvalState in sync. Don't modify bitboards without going through these.
- Move flags are bitmasks (e.g., `MoveFlagCapture | MoveFlagPromotion` for capture-promotions).
- Scores near `CheckmateScore` (31000) encode mate distance: `CheckmateScore - ply`. TT stores/retrieves scores adjusted for ply distance via `to_tt_score`/`from_tt_score`.
- UCI search runs on a background thread — `Search::stop()` sets an atomic flag checked at every node.
- `negamax` takes a `prev_move` parameter used by the counter-move heuristic.

### Build targets

- `hyperchess` — Main executable; UCI mode by default, `--cli` for direct search (`src/main.cpp`)
- `perft` — Move generation correctness tool (`src/perft.cpp`)
- `bench` — NPS benchmark across 4 test positions (`src/bench.cpp`)
- `chess_core` — Static library linked by all executables

### Compiler settings

Strict warnings: `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual -Wnull-dereference`. GCC-only flags (`-Wduplicated-cond`, `-Wduplicated-branches`, `-Wlogical-op`) gated behind `$<$<CXX_COMPILER_ID:GNU>:...>`. Debug builds enable ASan+UBSan via `HYPERCHESS_ENABLE_SANITIZERS` (ON by default).

## Current Status

See `docs/engine_roadmap.md` for the strength improvement plan. Key gaps: no repetition/fifty-move detection, no capture-only move generation (qsearch uses full `generate_legal_moves()`), no magic bitboards, tests directory is empty, no multi-threading (SMP).
