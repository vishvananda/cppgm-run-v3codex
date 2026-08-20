int f(int x) {
  switch (x) {
    case 0: {
      int y = 1;
      return y;
    }
    default:
      return 0;
  }
}

int main() {
  return f(0) - 1;
}
