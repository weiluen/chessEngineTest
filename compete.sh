#!/usr/bin/env bash
set -euo pipefail

# ── Configuration ────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
HYPER_DIR="$SCRIPT_DIR"
FOLLY_DIR="$(dirname "$SCRIPT_DIR")/FollyChess"

GAMES=${GAMES:-10}
TC=${TC:-"5+0.1"}        # 5 seconds + 0.1s increment per move
DEPTH=${DEPTH:-""}        # if set, uses fixed depth instead of time control

# ── Colours ──────────────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

info()  { echo -e "${CYAN}[INFO]${NC} $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; exit 1; }
ok()    { echo -e "${GREEN}[OK]${NC} $*"; }

# ── Dependency checks & installation ─────────────────────────────────────────
check_brew() {
    command -v brew &>/dev/null || error "Homebrew is required. Install from https://brew.sh"
}

ensure_cmd() {
    local cmd="$1" pkg="${2:-$1}"
    if ! command -v "$cmd" &>/dev/null; then
        info "Installing $pkg via Homebrew..."
        brew install "$pkg"
    fi
}

check_brew

info "Checking dependencies..."
ensure_cmd cmake cmake
ensure_cmd bazel bazelisk

# Check for cutechess-cli; if not found, set up python-chess in a venv
export PATH="$HOME/bin:$PATH"
VENV_DIR="$HYPER_DIR/.venv"
PYTHON="python3"
USE_CUTECHESS=false
if command -v cutechess-cli &>/dev/null; then
    USE_CUTECHESS=true
    ok "cutechess-cli found"
else
    info "cutechess-cli not found; will use python-chess match runner"
    if [ -d "$VENV_DIR" ] && "$VENV_DIR/bin/python3" -c "import chess" &>/dev/null; then
        PYTHON="$VENV_DIR/bin/python3"
        ok "python-chess available (existing venv)"
    else
        info "Creating venv and installing python-chess..."
        python3 -m venv "$VENV_DIR"
        "$VENV_DIR/bin/pip" install --quiet chess
        PYTHON="$VENV_DIR/bin/python3"
        ok "python-chess installed in .venv"
    fi
fi

# ── Build HyperSearchChess ───────────────────────────────────────────────────
info "Building HyperSearchChess..."
cd "$HYPER_DIR"
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ 2>&1 | tail -3
cmake --build build -j"$(sysctl -n hw.ncpu)" 2>&1 | tail -3
HYPER_BIN="$HYPER_DIR/build/hyperchess"
[ -x "$HYPER_BIN" ] || error "HyperSearchChess build failed — binary not found"
ok "HyperSearchChess built: $HYPER_BIN"

# ── Build FollyChess ─────────────────────────────────────────────────────────
info "Building FollyChess..."
[ -d "$FOLLY_DIR" ] || error "FollyChess not found at $FOLLY_DIR — clone it to the parent directory first"
cd "$FOLLY_DIR"
bazel build --compilation_mode=opt \
    --repo_env='BAZEL_CXXOPTS=-std=c++23' \
    //cli:follychess 2>&1 | tail -5
FOLLY_BIN="$FOLLY_DIR/bazel-bin/cli/follychess"
[ -x "$FOLLY_BIN" ] || error "FollyChess build failed — binary not found"
ok "FollyChess built: $FOLLY_BIN"

# ── Run match ────────────────────────────────────────────────────────────────
cd "$HYPER_DIR"

if $USE_CUTECHESS; then
    info "Running $GAMES-game match with cutechess-cli (TC: $TC)..."
    CUTECHESS_ARGS=(
        -engine "name=HyperSearch" "cmd=$HYPER_BIN" proto=uci
        -engine "name=FollyChess" "cmd=$FOLLY_BIN" proto=uci
        -each tc="$TC"
        -games "$GAMES"
        -rounds "$GAMES"
        -pgnout "$HYPER_DIR/match_results.pgn"
        -recover
        -repeat
    )
    if [ -n "$DEPTH" ]; then
        CUTECHESS_ARGS=( "${CUTECHESS_ARGS[@]/tc=$TC/}" )
        CUTECHESS_ARGS+=( -each "depth=$DEPTH" )
    fi
    cutechess-cli "${CUTECHESS_ARGS[@]}"
    ok "Match complete. PGN saved to match_results.pgn"
else
    info "Running $GAMES-game match with python-chess runner (TC: $TC)..."
    "$PYTHON" "$HYPER_DIR/run_match.py" \
        --engine1 "$HYPER_BIN" --name1 "HyperSearch" \
        --engine2 "$FOLLY_BIN" --name2 "FollyChess" \
        --games "$GAMES" \
        --tc "$TC" \
        ${DEPTH:+--depth "$DEPTH"} \
        --pgn "$HYPER_DIR/match_results.pgn"
    ok "Match complete. PGN saved to match_results.pgn"
fi
