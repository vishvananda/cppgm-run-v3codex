int t5 = 5;

int use(int t3, int other) {
  int t7 = t3 + other;
  int adjustment = 2;
  if (t7 > 3) {
    t7 += t5;
  } else {
    t7 -= adjustment;
  }
  for (int t2 = 0; t2 < 3; ++t2) {
    t7 += t2;
  }
  return t7;
}

int main() {
  return use(2, 2) == 12 ? 0 : 1;
}
