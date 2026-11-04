// Test_Cpp_Stress_Standard.cpp
// Test autonomo per IDE, compilatore e runtime. Richiede C++17 e solo STL.
// Compilazione: g++ -std=c++17 -O2 -pthread Test_Cpp_Stress_Standard.cpp -o test_stress
// Uso: test_stress [rapido|normale|intenso]   (predefinito: normale)
// Nessun file viene letto, creato o modificato dal programma.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using Clock = std::chrono::steady_clock;
using U64 = std::uint64_t;

struct Config {
    std::string name;
    int width, height, iterations, sieve_limit, vertices, repetitions;
};

class Timer {
    Clock::time_point start_ = Clock::now();
public:
    double milliseconds() const {
        return std::chrono::duration<double, std::milli>(Clock::now() - start_).count();
    }
};

// Generatore esplicito: lo stesso seme produce gli stessi risultati su ogni piattaforma.
class Random {
    U64 state_;
public:
    explicit Random(U64 seed) : state_(seed) {}
    U64 next() {
        state_ += UINT64_C(0x9e3779b97f4a7c15);
        U64 x = state_;
        x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
        x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
        return x ^ (x >> 31);
    }
    unsigned bounded(unsigned limit) { return static_cast<unsigned>(next() % limit); }
};

struct PrimeResult { std::size_t count; U64 sum; };
PrimeResult primes(int limit) {
    std::vector<bool> composite(static_cast<std::size_t>(limit) + 1, false);
    for (int p = 2; p <= limit / p; ++p)
        if (!composite[p])
            for (int multiple = p * p; multiple <= limit; multiple += p)
                composite[multiple] = true;
    PrimeResult result{0, 0};
    for (int n = 2; n <= limit; ++n)
        if (!composite[n]) { ++result.count; result.sum += static_cast<U64>(n); }
    return result;
}

using Edge = std::pair<int, int>; // destinazione, peso positivo
using Graph = std::vector<std::vector<Edge>>;

Graph make_graph(int count) {
    Graph graph(static_cast<std::size_t>(count));
    Random random(20261101);
    for (int from = 0; from < count; ++from) {
        graph[from].emplace_back((from + 1) % count, 1); // tutti i nodi sono raggiungibili
        for (int k = 0; k < 8; ++k)
            graph[from].emplace_back(static_cast<int>(random.bounded(count)),
                                     1 + static_cast<int>(random.bounded(1000)));
    }
    return graph;
}

std::vector<int> shortest_paths(const Graph& graph, int source) {
    constexpr int infinity = std::numeric_limits<int>::max() / 4;
    std::vector<int> distance(graph.size(), infinity);
    using Entry = std::pair<int, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> pending;
    distance[source] = 0;
    pending.emplace(0, source);
    while (!pending.empty()) {
        const auto [cost, current] = pending.top();
        pending.pop();
        if (cost != distance[current]) continue;
        for (const auto& [next, weight] : graph[current]) {
            const int candidate = cost + weight;
            if (candidate < distance[next]) {
                distance[next] = candidate;
                pending.emplace(candidate, next);
            }
        }
    }
    return distance;
}

// Lavoro CPU parallelo: ogni thread scrive in righe distinte del vettore.
std::vector<std::uint32_t> mandelbrot(const Config& config, unsigned workers) {
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(config.width) * config.height);
    workers = std::max(1u, std::min(workers, static_cast<unsigned>(config.height)));
    std::vector<std::thread> threads;
    threads.reserve(workers);
    try {
        for (unsigned id = 0; id < workers; ++id) {
            threads.emplace_back([&, id] {
                for (int y = static_cast<int>(id); y < config.height; y += static_cast<int>(workers)) {
                    for (int x = 0; x < config.width; ++x) {
                        const double cr = -2.0 + 3.0 * x / config.width;
                        const double ci = -1.2 + 2.4 * y / config.height;
                        double zr = 0.0, zi = 0.0;
                        int iteration = 0;
                        while (iteration < config.iterations && zr * zr + zi * zi <= 4.0) {
                            const double next = zr * zr - zi * zi + cr;
                            zi = 2.0 * zr * zi + ci;
                            zr = next;
                            ++iteration;
                        }
                        pixels[static_cast<std::size_t>(y) * config.width + x] = iteration;
                    }
                }
            });
        }
    } catch (...) {
        for (auto& thread : threads) if (thread.joinable()) thread.join();
        throw;
    }
    for (auto& thread : threads) thread.join();
    return pixels;
}

U64 checksum(const std::vector<std::uint32_t>& data) {
    U64 hash = UINT64_C(14695981039346656037);
    for (const auto value : data) { hash ^= value; hash *= UINT64_C(1099511628211); }
    return hash;
}

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

int main(int argc, char** argv) {
    try {
        if (argc > 2) throw std::invalid_argument("Uso: test_stress [rapido|normale|intenso]");
        const std::string mode = argc == 2 ? argv[1] : "normale";
        Config config;
        if (mode == "rapido") config = {mode, 320, 200, 100, 1000000, 2500, 2};
        else if (mode == "normale") config = {mode, 1200, 800, 500, 10000000, 30000, 5};
        else if (mode == "intenso") config = {mode, 2400, 1600, 1500, 30000000, 100000, 10};
        else throw std::invalid_argument("Profilo non valido. Usa rapido, normale oppure intenso.");

        const unsigned workers = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
        std::cout << "Test C++17 - profilo " << config.name << ", thread: " << workers << "\n";
        std::cout << std::fixed << std::setprecision(2);
        Timer total;

        Timer timer;
        const auto prime = primes(config.sieve_limit);
        require(primes(100).count == 25 && primes(100).sum == 1060, "Autotest numeri primi fallito");
        std::cout << "Numeri primi fino a " << config.sieve_limit << ": " << prime.count
                  << ", somma " << prime.sum << ", " << timer.milliseconds() << " ms\n";

        timer = Timer{};
        const auto graph = make_graph(config.vertices);
        U64 graph_sum = 0;
        for (int run = 0; run < config.repetitions; ++run) {
            const int source = run % config.vertices;
            const auto distances = shortest_paths(graph, source);
            require(distances[source] == 0, "Distanza iniziale errata");
            for (int from = 0; from < config.vertices; ++from) {
                require(distances[from] < std::numeric_limits<int>::max() / 4, "Nodo non raggiungibile");
                for (const auto& [to, weight] : graph[from])
                    require(distances[to] <= distances[from] + weight, "Distanza incoerente");
                graph_sum += static_cast<U64>(distances[from]);
            }
        }
        std::cout << "Grafo: " << config.vertices << " nodi, " << config.repetitions
                  << " percorsi, checksum " << graph_sum << ", " << timer.milliseconds() << " ms\n";

        timer = Timer{};
        const auto image = mandelbrot(config, workers);
        require(image.size() == static_cast<std::size_t>(config.width) * config.height, "Dimensione immagine errata");
        require(*std::min_element(image.begin(), image.end()) > 0 &&
                *std::max_element(image.begin(), image.end()) == static_cast<unsigned>(config.iterations),
                "Intervallo Mandelbrot inatteso");
        Config small{"verifica", 100, 60, 100, 0, 0, 0};
        require(mandelbrot(small, 1) == mandelbrot(small, workers), "Risultato parallelo incoerente");
        std::cout << "Mandelbrot: " << config.width << "x" << config.height
                  << ", checksum " << checksum(image) << ", " << timer.milliseconds() << " ms\n";
        std::cout << "Verifiche superate. Tempo totale: " << total.milliseconds() << " ms\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Errore: " << error.what() << '\n';
        return 1;
    }
}
