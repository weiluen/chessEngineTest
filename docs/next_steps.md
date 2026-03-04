# Core Optimisation Roadmap

1. **Correctness Baseline**
   - Validate move generator via deep `perft` against known reference positions.
   - Add regression tests that cover castling, en-passant, promotions, and repetition detection once implemented.
2. **Search Enhancements**
   - Wire quiescence to use capture-only generation (avoid full legal generation) to cut overhead.
   - Add history / killer heuristics and simple SEE for pruning.
   - Implement aspiration windows and dynamic move ordering using history scores.
3. **Evaluation**
   - Replace material-only evaluation with tapered PST and mobility terms.
   - Introduce incremental evaluation updates hooked into `make_move` / `unmake_move`.
4. **Transposition Table**
   - Rework store logic to use multi-entry buckets and age-based replacement.
   - Add TT cut-nodes into move ordering and repetition detection.
5. **Profiling & Tooling**
   - Integrate benchmarking harness printing nodes-per-second.
   - Add sanitiser-enabled CI target plus perf counter instrumentation.
6. **Parallel Search (Later)**
   - Prototype lazy SMP with thread-local stacks and TT splitting once single-thread baselines beat regression suite.
