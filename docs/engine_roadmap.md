# HyperSearch Strength Roadmap

This plan targets the four biggest levers for closing the gap to engines in the ~1600 Elo range: richer evaluation, improved move ordering, selective pruning, and a stronger transposition table. Each section below lists the intended behaviour, an implementation breakdown, and success metrics.

---

## 1. Evaluation Depth

**Goal:** Move from material+PST to a fast, incremental evaluation that understands structure, king safety, and phase-dependent terms.

### Milestones
1. **Incremental Evaluator Core**
   - Encapsulate evaluation state (`EvalState`) with cached material, piece-square sums, pawn hash, mobility counts.
   - Update `make_move` / `unmake_move` to maintain `EvalState` using delta computations.
   - Expose `int evaluate(const Position&, const EvalState&)` returning centipawns.
2. **Pawn Structure Table**
   - Build a pawn hash table keyed by pawn bitboards; store evaluated pawn terms (doubled, isolated, passed, backward, pawn shield/Storm).
   - Add side-to-move specific king-shield bonuses using precomputed tables.
3. **Mobility & Piece Activity**
   - Count legal/pseudo moves for knights, bishops, rooks, queens; scale by game phase.
   - Add rook/queen semi-open file bonuses, bishop pair, outpost detection (knight/bishop on protected squares).
4. **King Safety**
   - Track attacked squares around both kings.
   - Penalise exposed kings (open/half-open files, missing pawns), award defenders.
5. **Parameter Tuning Hooks**
   - Centralise all weights in a struct to support future Texel tuning or automated search.

**Success Metrics**
- Evaluation call cost stays within ~1.5× current material+PST time (benchmark with `bench`).
- Self-play (cutechess-cli) shows ≥+50 Elo over material-only baseline at 3+0.
- Perft correctness remains unchanged (no bugs introduced).

---

## 2. Move Ordering Enhancements

**Goal:** Improve node move ordering to maximise cutoffs and stabilise LMR/aspiration windows.

### Milestones
1. **Capture Sorting Upgrade**
   - Replace MVV-LVA heuristic with SEE (static exchange evaluation) to score captures.
   - Cache SEE values per move to reuse in quiescence.
2. **History & Butterfly Tables**
   - Split history into separate quiet/capture tables.
   - Implement decay per iteration; use 16-bit saturation to avoid overflow.
3. **Counter-Move / Follow-Up Tables**
   - Track the best reply to previous moves (per side). Prepend counter move in children ordering.
4. **Killer Rework**
   - Maintain two killer moves per ply for quiet and for tactical categories separately.
   - Reset selective killers on `ucinewgame`.
5. **Principal Variation Cache**
   - Store PV lines per depth to seed move ordering in future iterations.

**Success Metrics**
- Search logs: increased TT and history hits, reduced principal variation reshuffling.
- Depth-limited bench: ≥10% lower node count at depth 10 vs. baseline.
- No regression in perft or tactical test suites (mini test set).

---

## 3. Selective Pruning

**Goal:** Aggressive but safe pruning for quick time controls, minimising tactical oversights.

### Milestones
1. **Null-Move Verification**
   - Add verification search (re-search PV node with reduced depth) for high-null cutoffs.
   - Switch R parameter dynamically (R=2 default, R=3 in low-material endings).
2. **Late Move Reductions (LMR) Refinement**
   - Implement depth-/move-index-based reduction table.
   - Guard PV, capture, check, and SEE-positive moves from reduction.
3. **Futility / Razoring**
   - At depth ≤ 2, prune moves with stand-pat + margin < alpha (except checks).
   - Add razor at depth 1 with margin based on king safety/piece values.
4. **Beta Pruning Enhancements**
   - Introduce multi-cut pruning after several consecutive beta cutoffs.
   - Consider IID (internal iterative deepening) when TT miss and depth is high.
5. **Quiescence Improvements**
   - Integrate SEE to drop obviously losing captures.
   - Add delta pruning (ignore low-gain promotions/captures).

**Success Metrics**
- Bench: nodes/search time reduction ≥15% at depth 12 vs. baseline.
- Tactical regression suite (e.g., WAC, ECM small subset) shows equal or improved solve rate.
- No false mate scores (run detection during match testing).

---

## 4. Transposition Table Upgrades

**Goal:** Boost TT hit rate and stability, reducing wasted work and improving move ordering.

### Milestones
1. **Clustered Buckets**
   - Store 4 entries per bucket (fingerprint, move, depth, bound, generation).
   - Replace only lowest-value entry (age + depth heuristic).
2. **Generation & Aging**
   - Maintain search generation counter; age entries and deprioritise stale ones.
   - Clear table lazily by bumping generation instead of zeroing memory.
3. **TT Move Prefetch**
   - Prefetch bucket in search before iteration (hardware prefetch or manual pointer touches).
   - Return TT move early for ordering.
4. **Repetition / Draw Detection**
   - Use TT key stack to detect threefold repetition quickly.
   - Prevent storing repetition nodes as fails.
5. **Diagnostics**
   - Add TT stats output in `info string` (hits, collisions, replacements).
   - Provide CLI command to resize TT at runtime.

**Success Metrics**
- TT hit rate > 30% at depth 12 (bench).
- Memory footprint matches configured hash size; no uncontrolled growth.
- No performance regressions in short tests; `perft` unchanged.

---

## Implementation Order (Suggested)
1. Incremental evaluation infrastructure (EvalState) — makes later features easier.
2. TT cluster upgrade — safe foundation for deeper pruning.
3. History/ordering revamp — leverages new TT move + eval info.
4. Selective pruning adjustments — once ordering is stable.

Each milestone should be accompanied by:
- Unit/regression tests (perft, move generator).
- Benchmark snapshots (`bench` command capturing nodes/time).
- Match snippets (cutechess-cli 50-game blitz set).
