
unsigned fib(unsigned n) {
  if (n <= 1) {
    return n;
  }

  return fib(n - 1) + fib(n - 2);
}

int main() {
  unsigned n = 35;
  return (fib(n) == 9227465) ? 0 : 1;
}
