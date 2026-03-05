#!/usr/bin/env python3
"""
Minimal UCI engine match runner using python-chess.
Plays N games between two UCI engines, alternating colours, and writes a PGN.
"""

from __future__ import annotations

import argparse
import chess
import chess.engine
import chess.pgn
import io
import sys
import time
from typing import Optional


def parse_tc(tc_str: str) -> tuple:
    """Parse time control string like '5+0.1' into (base_sec, inc_sec)."""
    parts = tc_str.split("+")
    base = float(parts[0])
    inc = float(parts[1]) if len(parts) > 1 else 0.0
    return base, inc


def play_game(
    engine1: chess.engine.SimpleEngine,
    engine2: chess.engine.SimpleEngine,
    name1: str,
    name2: str,
    tc: Optional[str],
    depth: Optional[int],
    game_num: int,
    white_is_engine1: bool,
) -> chess.pgn.Game:
    white_engine = engine1 if white_is_engine1 else engine2
    black_engine = engine2 if white_is_engine1 else engine1
    white_name = name1 if white_is_engine1 else name2
    black_name = name2 if white_is_engine1 else name1

    board = chess.Board()
    game = chess.pgn.Game()
    game.headers["Event"] = "Engine Match"
    game.headers["Round"] = str(game_num)
    game.headers["White"] = white_name
    game.headers["Black"] = black_name
    game.headers["Date"] = time.strftime("%Y.%m.%d")

    node = game

    if tc:
        base, inc = parse_tc(tc)
        clocks = {chess.WHITE: base, chess.BLACK: base}
    else:
        clocks = None

    move_count = 0
    while not board.is_game_over(claim_draw=True):
        engine = white_engine if board.turn == chess.WHITE else black_engine

        if depth is not None:
            limit = chess.engine.Limit(depth=depth)
        elif clocks is not None:
            side = board.turn
            limit = chess.engine.Limit(
                white_clock=clocks[chess.WHITE],
                black_clock=clocks[chess.BLACK],
                white_inc=inc if tc else 0,
                black_inc=inc if tc else 0,
            )
        else:
            limit = chess.engine.Limit(time=5.0)

        start = time.monotonic()
        try:
            result = engine.play(board, limit)
        except chess.engine.EngineTerminatedError:
            # Engine crashed — the other side wins
            game.headers["Result"] = "0-1" if board.turn == chess.WHITE else "1-0"
            game.headers["Termination"] = "engine crash"
            return game

        elapsed = time.monotonic() - start

        if clocks is not None:
            side = board.turn
            clocks[side] -= elapsed
            if tc:
                clocks[side] += inc
            if clocks[side] <= 0:
                game.headers["Result"] = "0-1" if side == chess.WHITE else "1-0"
                game.headers["Termination"] = "time forfeit"
                return game

        if result.move is None:
            break

        node = node.add_variation(result.move)
        board.push(result.move)
        move_count += 1

        # Safety valve
        if move_count > 500:
            game.headers["Result"] = "1/2-1/2"
            game.headers["Termination"] = "max moves"
            break

    outcome = board.outcome(claim_draw=True)
    if outcome is not None:
        game.headers["Result"] = outcome.result()
    elif "Result" not in game.headers or game.headers["Result"] == "*":
        game.headers["Result"] = "1/2-1/2"

    return game


def main():
    parser = argparse.ArgumentParser(description="Run a UCI engine match")
    parser.add_argument("--engine1", required=True, help="Path to first engine binary")
    parser.add_argument("--engine2", required=True, help="Path to second engine binary")
    parser.add_argument("--name1", default="Engine1")
    parser.add_argument("--name2", default="Engine2")
    parser.add_argument("--games", type=int, default=10)
    parser.add_argument("--tc", default=None, help="Time control e.g. '5+0.1'")
    parser.add_argument("--depth", type=int, default=None, help="Fixed depth per move")
    parser.add_argument("--pgn", default="match_results.pgn", help="Output PGN file")
    args = parser.parse_args()

    if args.depth is None and args.tc is None:
        args.tc = "5+0.1"

    print(f"Starting {args.games}-game match: {args.name1} vs {args.name2}")
    if args.depth:
        print(f"  Mode: fixed depth {args.depth}")
    else:
        print(f"  Time control: {args.tc}")
    print()

    engine1 = chess.engine.SimpleEngine.popen_uci(args.engine1)
    engine2 = chess.engine.SimpleEngine.popen_uci(args.engine2)

    scores = {args.name1: 0.0, args.name2: 0.0, "draws": 0}
    games = []

    try:
        for i in range(1, args.games + 1):
            white_is_engine1 = (i % 2 == 1)
            white_name = args.name1 if white_is_engine1 else args.name2
            black_name = args.name2 if white_is_engine1 else args.name1

            print(f"Game {i}/{args.games}: {white_name} (W) vs {black_name} (B) ... ", end="", flush=True)

            game = play_game(
                engine1, engine2,
                args.name1, args.name2,
                args.tc, args.depth,
                i, white_is_engine1,
            )
            games.append(game)

            result = game.headers.get("Result", "*")
            if result == "1-0":
                scores[white_name] += 1.0
                print(f"{result} ({white_name} wins)")
            elif result == "0-1":
                scores[black_name] += 1.0
                print(f"{result} ({black_name} wins)")
            else:
                scores[args.name1] += 0.5
                scores[args.name2] += 0.5
                scores["draws"] += 1
                print(f"{result} (draw)")
    except KeyboardInterrupt:
        print("\nMatch interrupted!")
    finally:
        engine1.quit()
        engine2.quit()

    # Write PGN
    with open(args.pgn, "w") as f:
        for game in games:
            print(game, file=f)
            print(file=f)

    # Summary
    print()
    print("=" * 50)
    print(f"  MATCH RESULT ({len(games)} games)")
    print("=" * 50)
    print(f"  {args.name1:20s}: {scores[args.name1]:5.1f}")
    print(f"  {args.name2:20s}: {scores[args.name2]:5.1f}")
    print(f"  {'Draws':20s}: {int(scores['draws']):5d}")
    print("=" * 50)
    print(f"  PGN saved to: {args.pgn}")


if __name__ == "__main__":
    main()
