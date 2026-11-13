#include <algorithm>
#include <array>
#include <cctype>
#include <coroutine>
#include <cstddef>
#include <exception>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

constexpr int MIN_BOARD_SIZE = 3;
constexpr int MAX_BOARD_SIZE = 10;
constexpr int MIN_STREAK = 3;
constexpr char EMPTY_CELL = '.';

struct Move {
    int row;
    int col;
};

class MoveGenerator {
public:
    struct promise_type {
        Move current_value{};
        std::exception_ptr exception;

        MoveGenerator get_return_object() {
            return MoveGenerator{ std::coroutine_handle<promise_type>::from_promise(*this) };
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        std::suspend_always yield_value(Move move) noexcept {
            current_value = move;
            return {};
        }
        void return_void() noexcept {}
        void unhandled_exception() { exception = std::current_exception(); }
    };

    explicit MoveGenerator(std::coroutine_handle<promise_type> h) : handle_(h) {}
    ~MoveGenerator() { destroy(); }

    MoveGenerator(const MoveGenerator&) = delete;
    MoveGenerator& operator=(const MoveGenerator&) = delete;

    MoveGenerator(MoveGenerator&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
    MoveGenerator& operator=(MoveGenerator&& other) noexcept {
        if (this != &other) {
            destroy();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    bool next() {
        if (!handle_ || handle_.done()) return false;
        handle_.resume();
        if (handle_.promise().exception) {
            std::rethrow_exception(handle_.promise().exception);
        }
        return !handle_.done();
    }

    Move value() const { return handle_.promise().current_value; }

private:
    void destroy() {
        if (handle_) {
            handle_.destroy();
            handle_ = nullptr;
        }
    }

    std::coroutine_handle<promise_type> handle_;
};

class GameBoard {
public:
    GameBoard(int size, int streak)
        : size_(size), streak_(streak),
        grid_(static_cast<std::size_t>(size), std::vector<char>(static_cast<std::size_t>(size), EMPTY_CELL)) {
    }

    bool apply_move(Move m, char symbol) {
        if (!in_bounds(m.row, m.col) || cell(m.row, m.col) != EMPTY_CELL) return false;
        grid_[static_cast<std::size_t>(m.row)][static_cast<std::size_t>(m.col)] = symbol;
        ++filled_;
        return true;
    }

    bool is_full() const { return filled_ == static_cast<std::size_t>(size_) * static_cast<std::size_t>(size_); }

    std::vector<Move> free_cells() const {
        std::vector<Move> result;
        for (int r = 0; r < size_; ++r)
            for (int c = 0; c < size_; ++c)
                if (cell(r, c) == EMPTY_CELL) result.push_back({ r, c });
        return result;
    }

    bool is_winning_move(Move m, char symbol) const {
        static constexpr std::array<std::pair<int, int>, 4> directions{ {{0, 1}, {1, 0}, {1, 1}, {1, -1}} };
        for (const auto& [dr, dc] : directions) {
            int length = 1 + count_in_direction(m, dr, dc, symbol) + count_in_direction(m, -dr, -dc, symbol);
            if (length >= streak_) return true;
        }
        return false;
    }

    void display() const {
        std::cout << "  ";
        for (int c = 0; c < size_; ++c) std::cout << c << ' ';
        std::cout << '\n';
        for (int r = 0; r < size_; ++r) {
            std::cout << r << ' ';
            for (int c = 0; c < size_; ++c) std::cout << cell(r, c) << ' ';
            std::cout << '\n';
        }
        std::cout << std::endl;
    }

private:
    bool in_bounds(int r, int c) const { return r >= 0 && r < size_ && c >= 0 && c < size_; }
    char cell(int r, int c) const { return grid_[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)]; }

    int count_in_direction(Move m, int dr, int dc, char symbol) const {
        int count = 0;
        int r = m.row + dr;
        int c = m.col + dc;
        while (in_bounds(r, c) && cell(r, c) == symbol) {
            ++count;
            r += dr;
            c += dc;
        }
        return count;
    }

    int size_;
    int streak_;
    std::size_t filled_ = 0;
    std::vector<std::vector<char>> grid_;
};

MoveGenerator player_coroutine(const GameBoard& board) {
    std::mt19937 rng{ std::random_device{}() };
    while (true) {
        const std::vector<Move> free = board.free_cells();
        if (free.empty()) co_return;
        std::uniform_int_distribution<std::size_t> dist(0, free.size() - 1);
        co_yield free[dist(rng)];
    }
}

class GameEngine {
public:
    GameEngine(int size, int streak, const std::vector<char>& symbols) : board_(size, streak) {
        for (char symbol : symbols) {
            players_.push_back(Player{ symbol, player_coroutine(board_) });
        }
    }

    GameEngine(const GameEngine&) = delete;
    GameEngine& operator=(const GameEngine&) = delete;

    void run() {
        std::cout << "--- Game Protocol Start ---\n";
        board_.display();

        for (std::size_t turn = 0;; ++turn) {
            Player& player = players_[turn % players_.size()];

            if (!player.moves.next()) {
                throw std::logic_error("Player coroutine produced no move");
            }
            const Move m = player.moves.value();
            if (!board_.apply_move(m, player.symbol)) {
                throw std::logic_error(std::string("Player '") + player.symbol + "' attempted an invalid move");
            }

            std::cout << "Turn " << turn + 1 << ": player '" << player.symbol << "' placed at ("
                << m.row << ", " << m.col << ")\n";
            board_.display();

            if (board_.is_winning_move(m, player.symbol)) {
                std::cout << "Game Over! Player '" << player.symbol << "' wins!\n";
                return;
            }
            if (board_.is_full()) {
                std::cout << "Game Over! It's a draw.\n";
                return;
            }
        }
    }

private:
    struct Player {
        char symbol;
        MoveGenerator moves;
    };

    GameBoard board_;
    std::vector<Player> players_;
};

static std::string trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

static int read_int(const std::string& prompt, int min_val, int max_val) {
    while (true) {
        std::cout << prompt << " [" << min_val << '-' << max_val << "]: ";
        std::string line;
        if (!std::getline(std::cin, line)) {
            throw std::runtime_error("End of input reached. Application terminated.");
        }
        const std::string text = trim(line);

        const bool digits_only = !text.empty() && text.size() <= 9 &&
            std::all_of(text.begin(), text.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; });
        if (!digits_only) {
            std::cout << "Invalid input: please enter a whole number (digits only).\n";
            continue;
        }
        const int value = std::stoi(text);
        if (value < min_val || value > max_val) {
            std::cout << "Invalid input: the number must be between " << min_val << " and " << max_val << ".\n";
            continue;
        }
        return value;
    }
}

int main() {
    try {
        std::cout << "=== Coroutine Tic-Tac-Toe ===\n";
        const int board_size = read_int("Enter board size N (board is N x N)", MIN_BOARD_SIZE, MAX_BOARD_SIZE);
        const int win_streak = read_int("Enter win streak length K", MIN_STREAK, board_size);

        std::cout << "\nStarting game with 3 coroutine players (X, O, Z)...\n\n";
        GameEngine engine(board_size, win_streak, { 'X', 'O', 'Z' });
        engine.run();
    }
    catch (const std::exception& e) {
        std::cerr << "Fatal Error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}