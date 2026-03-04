# Chess Engine Core Search Design

## Goals
- Deliver a world-class search core prioritising raw speed over ancillary features.
- Build a modular foundation that can evolve (UCI protocol, book, NNUE) without impacting the search hot path.
- Use modern C++ for broad ecosystem support while keeping the code generation predictable for low-level optimisation.

## Guiding Principles
- **Cache locality first**: favour fixed-size arenas, struct-of-arrays where practical, and avoid virtual dispatch on hot paths.
- **Measure constantly**: integrate lightweight profiling hooks and per-node counters to guide optimisation.
- **Fail fast on correctness**: include ironclad move-validation and debug asserts compiled out in release builds.

## Architecture Overview

### Core Modules
| Module | Responsibilities | Key Performance Considerations |
| --- | --- | --- |
| `Bitboards` | Board representation, attack masks, incremental state updates. | Precompute sliding attacks with magic bitboards; ensure updates are branchless where possible. |
| `MoveGen` | Generate legal / pseudo-legal moves. | Split quiet vs. tactical, incremental move ordering data, use SIMD-friendly layouts. |
| `Evaluator` | Lightweight material + piece-square tables baseline; hook for future NNUE. | Keep fast path branch-free; compute deltas on `make`/`unmake`. |
| `Search` | Iterative deepening negamax with PVS, aspiration, quiescence. | Flatten call stacks, inline critical functions, exploit LMR and null-move while keeping cutoffs predictable. |
| `TranspositionTable` | Store exact/upper/lower bounds with Zobrist keys. | 64-bit keys, cluster buckets to cut cache misses, avoid locks by using per-thread segments. |
| `TimeManager` | Turn remaining time + increments into search depth targets. | Use monotonic clock; limit overhead to outside deepest nodes. |

### Threading Model
- Initial milestone targets single-threaded search to eliminate concurrency noise.
- Design TT and node accounting with thread-safety in mind (separate buckets or lock-free scheme) for future SMP.

### Search Enhancements Roadmap
1. **Baseline**: Negamax + alpha-beta + quiescence + TT + iterative deepening.
2. **Move ordering**: Principal variation cache, transposition table move, killers, history heuristics.
3. **Pruning/reductions**: Null-move pruning, late move reductions, razoring/futility in selective nodes.
4. **Extensions**: Checks, recaptures, mate threat detection.
5. **Advanced heuristics**: Counter-move heuristics, SEE-based pruning, internal iterative deepening.

### Data Structures
- **Position State**: `struct Position { Bitboards bb; uint64_t zobrist; PackedState stateStack[MaxPly]; ... }`.
- **Move Encoding**: 32-bit packed moves (`from`, `to`, `piece`, `promotions`, `flags`) tuned for fast extraction.
- **TT Entry**: 128-bit payload (key xor, packed score, best move, node type, depth, generation).

### Tooling & Build
- Use CMake targeting C++20, with `-Ofast -march=native` for release builds.
- Integrate sanitizers in debug builds (`-fsanitize=address,undefined`) to catch correctness issues early.
- Provide `bench` command for deterministic per-node benchmarking.

## Initial Milestone Deliverables
1. C++ skeleton with core headers in `include/` and implementations in `src/`.
2. Basic FEN loader and move generator sanity tests.
3. Search loop performing iterative deepening with TT placeholder.

## Next Steps
- Flesh out bitboard helper tables, generation masks.
- Implement make/unmake with incremental evaluation.
- Connect to minimal UCI driver to enable testing from external GUIs once search stabilises.
