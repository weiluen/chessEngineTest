#include "chess/uci.hpp"

#include "chess/move.hpp"
#include "chess/position.hpp"
#include "chess/search.hpp"
#include "chess/transposition_table.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace chess::uci {

namespace {

constexpr char kStartPos[] = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return s;
}

bool moves_equal(const Move& a, const Move& b) {
    return a.from == b.from &&
           a.to == b.to &&
           a.promotion == b.promotion;
}

class Engine {
public:
    Engine()
        : position_(),
          tt_(128),
          search_(tt_) {}

    void run() {
        std::string line;
        while (std::getline(std::cin, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty()) {
                continue;
            }
            handle_command(line);
            if (quit_requested_) {
                break;
            }
        }
        stop_search();
        wait_for_search();
    }

private:
    void handle_command(const std::string& line) {
        std::istringstream iss(line);
        std::string token;
        iss >> token;
        if (token == "uci") {
            cmd_uci();
        } else if (token == "isready") {
            cmd_isready();
        } else if (token == "ucinewgame") {
            cmd_ucinewgame();
        } else if (token == "position") {
            cmd_position(iss);
        } else if (token == "go") {
            cmd_go(iss);
        } else if (token == "stop") {
            stop_search();
            wait_for_search();
        } else if (token == "quit") {
            quit_requested_ = true;
            stop_search();
            wait_for_search();
        } else if (token == "setoption") {
            cmd_setoption(iss);
        } else if (token == "perft") {
            // optional: ignore or print warning
            std::cout << "info string perft not supported through UCI\n";
        } else {
            std::cout << "info string unknown command: " << line << '\n';
        }
    }

    void cmd_uci() {
        std::cout << "id name HyperSearch\n";
        std::cout << "id author Codex\n";
        std::cout << "option name Hash type spin default 128 min 1 max 4096\n";
        std::cout << "uciok\n";
        std::cout.flush();
    }

    void cmd_isready() {
        std::cout << "readyok\n";
        std::cout.flush();
    }

    void cmd_ucinewgame() {
        stop_search();
        wait_for_search();
        tt_.clear();
        position_ = Position();
    }

    void cmd_position(std::istringstream& iss) {
        stop_search();
        wait_for_search();

        std::string token;
        if (!(iss >> token)) {
            return;
        }

        Position new_position;
        try {
            if (token == "startpos") {
                new_position.set_fen(kStartPos);
                if (iss.peek() == ' ') {
                    iss.get();
                }
            } else if (token == "fen") {
                std::string fen;
                std::string part;
                int fields = 0;
                while (fields < 6 && iss >> part) {
                    if (part == "moves") {
                        break;
                    }
                    if (!fen.empty()) {
                        fen.push_back(' ');
                    }
                    fen += part;
                    ++fields;
                }
                if (fields < 4) {
                    std::cout << "info string invalid FEN\n";
                    return;
                }
                new_position.set_fen(fen);
                if (part != "moves") {
                    token = part;
                } else {
                    token = "moves";
                }
            } else {
                std::cout << "info string unsupported position command\n";
                return;
            }
        } catch (const std::exception& ex) {
            std::cout << "info string failed to set FEN: " << ex.what() << '\n';
            return;
        }

        position_ = new_position;

        if (token != "moves") {
            if (!(iss >> token) || token != "moves") {
                return;
            }
        }

        std::string move_str;
        while (iss >> move_str) {
            if (!apply_move(move_str)) {
                std::cout << "info string illegal move in position: " << move_str << '\n';
                break;
            }
        }
    }

    void cmd_setoption(std::istringstream& iss) {
        std::string token;
        std::string name;
        std::string value;
        while (iss >> token) {
            if (token == "name") {
                name.clear();
                while (iss >> token && token != "value") {
                    if (!name.empty()) name.push_back(' ');
                    name += token;
                }
                if (token != "value") {
                    return;
                }
                value.clear();
                while (iss >> token) {
                    if (!value.empty()) value.push_back(' ');
                    value += token;
                }
                break;
            }
        }

        name = to_lower(name);
        if (name == "hash") {
            try {
                std::size_t mb = static_cast<std::size_t>(std::stoull(value));
                stop_search();
                wait_for_search();
                tt_.resize(mb);
                std::cout << "info string hash set to " << mb << " MB\n";
            } catch (...) {
                std::cout << "info string invalid hash value\n";
            }
        } else if (name == "clear hash") {
            stop_search();
            wait_for_search();
            tt_.clear();
        } else {
            std::cout << "info string unsupported option " << name << '\n';
        }
    }

    void cmd_go(std::istringstream& iss) {
        SearchLimits limits{};
        std::string token;
        while (iss >> token) {
            if (token == "wtime" && iss >> token) {
                limits.time_left[static_cast<int>(Color::White)] = static_cast<std::uint64_t>(std::stoll(token));
            } else if (token == "btime" && iss >> token) {
                limits.time_left[static_cast<int>(Color::Black)] = static_cast<std::uint64_t>(std::stoll(token));
            } else if (token == "winc" && iss >> token) {
                limits.increment[static_cast<int>(Color::White)] = static_cast<std::uint64_t>(std::stoll(token));
            } else if (token == "binc" && iss >> token) {
                limits.increment[static_cast<int>(Color::Black)] = static_cast<std::uint64_t>(std::stoll(token));
            } else if (token == "movestogo" && iss >> token) {
                limits.moves_to_go = std::stoi(token);
            } else if (token == "depth" && iss >> token) {
                limits.depth = std::stoi(token);
            } else if (token == "nodes" && iss >> token) {
                limits.nodes = static_cast<std::uint64_t>(std::stoll(token));
            } else if (token == "movetime" && iss >> token) {
                limits.time_ms = static_cast<std::uint64_t>(std::stoll(token));
            } else if (token == "infinite") {
                limits.infinite = true;
            }
        }

        launch_search(limits);
    }

    void launch_search(const SearchLimits& limits) {
        stop_search();
        wait_for_search();

        searching_.store(true, std::memory_order_relaxed);
        search_thread_ = std::thread([this, limits]() {
            auto start = std::chrono::steady_clock::now();
            SearchResult result = search_.iterative_deepening(position_, limits);
            auto end = std::chrono::steady_clock::now();
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            searching_.store(false, std::memory_order_relaxed);
            print_info(result, elapsed_ms);
            print_bestmove(result.best_move);
        });
    }

    void stop_search() {
        if (searching_.load(std::memory_order_relaxed)) {
            search_.stop();
        }
    }

    void wait_for_search() {
        if (search_thread_.joinable()) {
            search_thread_.join();
        }
        searching_.store(false, std::memory_order_relaxed);
    }

    void print_info(const SearchResult& result, long long elapsed_ms) {
        if (elapsed_ms <= 0) {
            elapsed_ms = 1;
        }
        std::uint64_t nodes = result.stats.nodes + result.stats.qnodes;
        std::uint64_t nps = static_cast<std::uint64_t>((nodes * 1000LL) / elapsed_ms);

        std::cout << "info depth " << result.depth_reached;
        if (std::abs(result.score) > CheckmateThreshold) {
            int mate_in = (CheckmateScore - std::abs(result.score)) / 2;
            if (mate_in < 0) mate_in = 0;
            std::cout << " score mate " << (result.score > 0 ? mate_in : -mate_in);
        } else {
            std::cout << " score cp " << result.score;
        }
        std::cout << " time " << elapsed_ms
                  << " nodes " << nodes
                  << " nps " << nps
                  << '\n';
        std::cout.flush();
    }

    void print_bestmove(const Move& move) {
        std::string uci_move = to_uci(move);
        std::cout << "bestmove " << uci_move << '\n';
        std::cout.flush();
    }

    bool apply_move(const std::string& move_str) {
        std::string lower = to_lower(move_str);
        std::vector<Move> legal = position_.generate_legal_moves();
        for (Move move : legal) {
            if (to_uci(move) == lower) {
                return position_.make_move(move);
            }
        }
        return false;
    }

    Position position_{};
    TranspositionTable tt_;
    Search search_;
    std::thread search_thread_;
    std::atomic<bool> searching_{false};
    bool quit_requested_ = false;
};

}  // namespace

void loop() {
    Engine engine;
    engine.run();
}

}  // namespace chess::uci
