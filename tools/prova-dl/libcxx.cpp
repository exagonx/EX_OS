/* tools/prova-dl/libcxx.cpp - a C++ shared object for tools/prova-dl/prova-cxx.cpp
 * (voce 85): exceptions thrown here and caught in the program, a callback of
 * the program that throws THROUGH a frame of this library, a global object
 * built at load, std::string across the boundary. */
#include <stdexcept>
#include <string>
#include <functional>

static int g_costruito;
struct Avvio { Avvio() { g_costruito = 42; } };
static Avvio g_avvio;                        /* built by DT_INIT_ARRAY */

int libcxx_costruito() { return g_costruito; }

void libcxx_lancia(int n)
{
    if (n > 0) throw std::runtime_error("dalla libreria " + std::to_string(n));
}

/* the program's function throws, the exception crosses this frame */
int libcxx_chiama(const std::function<int(int)> &f, int x)
{
    std::string tieni("un distruttore da eseguire durante lo srotolamento");
    return f(x) + (int)tieni.size();
}

std::string libcxx_unisci(const std::string &a, const std::string &b) { return a + "+" + b; }
