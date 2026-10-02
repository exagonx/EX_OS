// tools/exilla/prova-cxx-fili.cpp — la libstdc++ di EX-OS con i fili (tappa 6)
// Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Si costruisce con la toolchain di tools/exilla/gcc-fili.sh e si esegue con
// tools/exilla/esegui-in-exos.sh. Ogni controllo stampa [OK] o [NO].
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>
#include <sys/mman.h>
#include <cstdlib>
#include <cstring>
#include <cstdint>

static int g_no = 0;
static void ok(const char* cosa, bool va) {
  std::printf("  %s  %s\n", va ? "[OK]" : "[NO]", cosa);
  if (!va) g_no++;
}

static std::atomic<int> g_costruzioni{0};
struct Lento {
  Lento() {
    g_costruzioni++;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
};
static Lento& unico() {
  static Lento l;  // con i fili: __cxa_guard_acquire la costruisce una volta
  return l;
}

int main() {
  std::printf("prova-cxx-fili: la libstdc++ con i fili dentro EX-OS\n");

  std::mutex m;
  long conto = 0;
  std::vector<std::thread> fili;
  for (int i = 0; i < 4; i++)
    fili.emplace_back([&] {
      for (int j = 0; j < 20000; j++) {
        std::lock_guard<std::mutex> g(m);
        conto++;
      }
    });
  for (auto& f : fili) f.join();
  ok("quattro std::thread e uno std::mutex: 80000", conto == 80000);

  std::condition_variable cv;
  bool pronto = false;
  std::thread t([&] {
    std::lock_guard<std::mutex> g(m);
    pronto = true;
    cv.notify_one();
  });
  {
    std::unique_lock<std::mutex> l(m);
    ok("std::condition_variable",
       cv.wait_for(l, std::chrono::seconds(5), [&] { return pronto; }));
  }
  t.join();

  std::vector<std::thread> altri;
  for (int i = 0; i < 4; i++) altri.emplace_back([] { unico(); });
  for (auto& f : altri) f.join();
  ok("statica locale costruita una volta sola da quattro fili",
     g_costruzioni == 1);

  // malloc da quattro fili insieme: lo heap della libc ha un lucchetto
  // (1 ottobre 2026); senza, si rovinava la lista dei blocchi.
  std::atomic<int> guasti{0};
  std::vector<std::thread> allocatori;
  for (int i = 0; i < 4; i++)
    allocatori.emplace_back([&guasti, i] {
      void* tenuti[64] = {};
      for (int j = 0; j < 20000; j++) {
        int k = (j * 7 + i) % 64;
        if (tenuti[k]) {
          if (static_cast<unsigned char*>(tenuti[k])[0] != (unsigned char)(i + 1)) guasti++;
          free(tenuti[k]);
        }
        size_t dim = 8 + (j % 200);
        tenuti[k] = malloc(dim);
        memset(tenuti[k], i + 1, dim);
      }
      for (auto* t : tenuti) free(t);
    });
  for (auto& f : allocatori) f.join();
  ok("malloc e free da quattro fili insieme, 80000 giri", guasti == 0);

  // POSIX: getpid() e' il PROCESSO, uguale in tutti i fili (libc, 1 ottobre 2026)
  int pid_filo = 0;
  std::thread([&] { pid_filo = getpid(); }).join();
  ok("getpid() uguale nel filo principale e in un altro filo", pid_filo == getpid());

  // wchar_t: la libstdc++ lo ha solo se <wchar.h> dichiara tutto (lib/libc.c)
  std::wostringstream w;
  w << L"ciao " << 42;
  std::wstring ws = w.str();
  ok("std::wostringstream e std::wstring", ws == L"ciao 42" && ws.size() == 7);

  // malloc e new allineati a 16 come promette il compilatore (max_align_t,
  // __STDCPP_DEFAULT_NEW_ALIGNMENT__): con 8 Gecko sfondava le intestazioni
  struct alignas(16) Sedici { char c[24]; };
  int storti = 0;
  std::vector<void*> tenuti;
  for (int i = 1; i < 3000; i++) {
    void* p = std::malloc((size_t)(i * 7) % 1000 + 1);
    if (((uintptr_t)p & 15u) != 0) storti++;
    if (i % 3) tenuti.push_back(p); else std::free(p);
    Sedici* s = new Sedici;
    if (((uintptr_t)s & 15u) != 0) storti++;
    delete s;
  }
  for (void* p : tenuti) std::free(p);
  void* r = std::realloc(std::malloc(5), 77);
  if (((uintptr_t)r & 15u) != 0) storti++;
  std::free(r);
  ok("malloc, realloc e new allineati a 16", storti == 0);

  // Lo spazio degli indirizzi e' del gruppo (kernel 0.230): sbrk e mmap da
  // fili diversi non devono darsi gli stessi indirizzi. Prima ogni filo aveva
  // il suo heap_end e mappava pagine azzerate sopra la malloc di un altro.
  std::atomic<int> rovinati{0};
  std::vector<std::thread> misti;
  for (int f = 0; f < 4; f++) {
    misti.emplace_back([f, &rovinati] {
      unsigned char motivo = (unsigned char)(0x31 + f);
      // Si TIENE tutto fino alla fine: lo heap deve crescere da piu' fili
      // insieme, e' li' che i confini copiati si pestavano.
      std::vector<std::pair<unsigned char*, size_t>> m_tenuti, z_tenuti;
      for (int giro = 0; giro < 120; giro++) {
        size_t n = 8000 + (size_t)((giro * 7919 + f * 104729) % 40000);
        unsigned char* m = (unsigned char*)std::malloc(n);
        size_t lung = (size_t)(1 + giro % 4) * 4096;
        unsigned char* z = (unsigned char*)mmap(nullptr, lung, PROT_READ | PROT_WRITE,
                                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (m == nullptr || z == (unsigned char*)MAP_FAILED) { rovinati++; return; }
        std::memset(m, motivo, n);
        std::memset(z, motivo ^ 0xFF, lung);
        m_tenuti.push_back({m, n});
        z_tenuti.push_back({z, lung});
        if (giro % 8 == 0) std::this_thread::yield();
      }
      for (auto& [m, n] : m_tenuti) {
        for (size_t i = 0; i < n; i += 61) if (m[i] != motivo) { rovinati++; break; }
        std::free(m);
      }
      for (auto& [z, lung] : z_tenuti) {
        for (size_t i = 0; i < lung; i += 61)
          if (z[i] != (unsigned char)(motivo ^ 0xFF)) { rovinati++; break; }
        munmap(z, lung);
      }
    });
  }
  for (auto& t : misti) t.join();
  std::printf("  (blocchi rovinati: %d)\n", rovinati.load());
  ok("malloc e mmap da quattro fili: nessuno pesta la memoria di un altro",
     rovinati == 0);

  std::printf("prova-cxx-fili: %d NO\n", g_no);
  return g_no;
}
