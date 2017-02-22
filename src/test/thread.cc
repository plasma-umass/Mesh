#include <iostream>
#include <mutex>
#include <thread>

int g_i = 0;
std::mutex g_i_mutex;  // protects g_i

volatile __thread char *c;

void safe_increment() {
  std::lock_guard<std::mutex> lock(g_i_mutex);
  ++g_i;

  for (size_t i = 0; i < 100; ++i) {
    c = new char[16];
    delete c;
  }

  std::cout << std::this_thread::get_id() << ": " << g_i << '\n';

  // g_i_mutex is automatically released when lock
  // goes out of scope
}

int main() {
  std::cout << __func__ << ": " << g_i << '\n';

  std::thread t1(safe_increment);
  std::thread t2(safe_increment);

  t1.join();
  t2.join();

  std::cout << __func__ << ": " << g_i << '\n';
}
