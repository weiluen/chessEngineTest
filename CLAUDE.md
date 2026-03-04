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
```

## Architecture

All code lives in the `chess` namespace. Headers in `include/chess/`, implementations in `src/`.

**Core data flow:** `Position` (board state + move gen) → `Search` (iterative deepening negamax) → `evaluate()` → score.

### Key modules

- **types.hpp** — Foundational types: `Bitboard` (uint64), `Move` struct, `Color`, `Piece`, `SearchLimits`. All other modules depend on this.
- **bitboard.hpp/cpp** — Square enum (A1=0..H8=63), precomputed attack tables. `init_attack_tables()` must be called before any position work. Uses classical approach for sliding pieces (not magic bitboards yet).
- **position.hpp/cpp** — Board representation using per-color, per-piece bitboards (`pieces_[2][6]`). FEN parsing, legal move generation, make/unmake with incremental Zobrist updates. `StateInfo` stack for undo history.
- **search.hpp/cpp** — Iterative deepening with PVS, null-move pruning (R=2), LMR, quiescence search. Move ordering via TT move, killer heuristic, and history tables (`history_[2][64][64]`). `TimeManager` handles soft/hard time limits. Public `stop()` method for external abort (used by UCI).
- **transposition_table.hpp/cpp** — Single-bucket TT with generation-based replacement. 128MB default in UCI mode.
- **evaluation.hpp/cpp** — Material + piece-square table evaluation with tapered scoring (midgame/endgame phase blend based on non-pawn material). +10 tempo bonus for side to move.
- **move.hpp/cpp** — Move flag constants (bitmask), null move factory, UCI string conversion.
- **uci.hpp/cpp** — UCI protocol implementation. Runs a stdin command loop via `uci::loop()`. Internal `Engine` class owns `Position`, `TranspositionTable` (128MB), and `Search`. Search runs on a separate `std::thread`; supports `stop` command for async abort. Supports `setoption name Hash` for configurable TT size.

### Important invariants

- `init_attack_tables()` must be called once at startup before creating any `Position`.
- `Position::make_move()` returns false if the move leaves the king in check (pseudo-legal filtering). Always check the return value.
- Move flags are bitmasks (e.g., `MoveFlagCapture | MoveFlagPromotion` for capture-promotions).
- Scores near `CheckmateScore` (31000) encode mate distance: `CheckmateScore - ply`. TT stores/retrieves scores adjusted for ply distance via `to_tt_score`/`from_tt_score`.
- UCI search runs on a background thread — `Search::stop()` sets an atomic flag checked at every node.

### Build targets

- `hyperchess` — Main executable; UCI mode by default, `--cli` for direct search (`src/main.cpp`)
- `perft` — Move generation correctness tool (`src/perft.cpp`)
- `chess_core` — Static library linked by both executables (includes all modules including UCI)

### Compiler settings

Strict warnings enabled: `-Wall -Wextra -Wpedantic -Wshadow -Wconversion` and more. Debug builds enable ASan+UBSan via `HYPERCHESS_ENABLE_SANITIZERS` (ON by default).

## Current Status

See `docs/next_steps.md` for the optimization roadmap. Key gaps: no repetition/fifty-move detection, no opening book, no NNUE, tests directory is empty, no multi-threading (SMP).
