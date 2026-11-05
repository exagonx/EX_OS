/* tools/prova-dl/prova-cxx.cpp - a C++ program linked to libstdc++.so.6,
 * libgcc_s.so.1 and libcxx.so (voce 85). Prints "prova-cxx: N NO" at the end. */
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <mutex>
#include <vector>
#include <map>
#include <algorithm>
#include <functional>
#include <new>
#include <sstream>

int libcxx_costruito();
void libcxx_lancia(int n);
int libcxx_chiama(const std::function<int(int)> &f, int x);
std::string libcxx_unisci(const std::string &a, const std::string &b);

static int no;
static void esito(bool ok, const char *cosa)
{
    std::cout << (ok ? "  [OK] " : "  [NO] ") << cosa << std::endl;
    if (!ok) no++;
}

static int conta_statica()
{
    static int n = []{ return 7; }();        /* guarded static, threads on */
    return n;
}

int main()
{
    esito(libcxx_costruito() == 42, "il costruttore globale della libreria");
    try { libcxx_lancia(3); esito(false, "eccezione dalla libreria"); }
    catch (const std::runtime_error &e) { esito(std::string(e.what()) == "dalla libreria 3", "eccezione dalla libreria presa nel programma"); }

    try {
        libcxx_chiama([](int x) -> int { if (x) throw std::logic_error("dal programma"); return 0; }, 1);
        esito(false, "eccezione attraverso la libreria");
    } catch (const std::logic_error &e) { esito(std::string(e.what()) == "dal programma", "eccezione del programma attraverso un frame della libreria"); }

    try { std::vector<char> v((size_t)-1 / 2); esito(false, "bad_alloc"); }
    catch (const std::bad_alloc &) { esito(true, "std::bad_alloc da libstdc++.so"); }
    catch (const std::length_error &) { esito(true, "std::length_error da libstdc++.so"); }

    esito(libcxx_unisci("a", "b") == "a+b", "std::string fra programma e libreria");

    std::mutex m; long tot = 0; std::vector<std::thread> fili;
    for (int i = 0; i < 4; i++)
        fili.emplace_back([&] { for (int k = 0; k < 10000; k++) { std::lock_guard<std::mutex> g(m); tot++; } conta_statica(); });
    for (auto &f : fili) f.join();
    esito(tot == 40000, "4 std::thread e std::mutex");
    esito(conta_statica() == 7, "statica locale con lucchetto");

    std::map<std::string, int> mp{{"tre", 3}, {"uno", 1}, {"due", 2}};
    std::vector<int> v; for (auto &p : mp) v.push_back(p.second);
    std::sort(v.begin(), v.end());
    std::ostringstream os; for (int x : v) os << x;
    esito(os.str() == "123", "map, sort, ostringstream");

    std::cout << "prova-cxx: " << no << " NO" << std::endl;
    return no;
}
