int constructed;
int destroyed;

struct Member {
  Member(int seed) {
    if (seed == 1234) throw 5;
    constructed = constructed + 1;
  }
  ~Member() { destroyed = destroyed + 1; }
};

struct Holder {
  Member first;
  Member second;
  Holder(int seed, bool populate) : first(seed), second(seed + 1) {
    if (!populate) return;
    if (seed == 4321) throw 6;
    constructed = constructed + 100;
  }
};

int check(bool populate, int expected_constructed) {
  constructed = 0;
  destroyed = 0;
  {
    Holder holder(7, populate);
  }
  if (constructed != expected_constructed) return 1;
  if (destroyed != 2) return 2;
  return 0;
}

int main() {
  if (check(false, 2) != 0) return 1;
  if (check(true, 102) != 0) return 2;
  return 0;
}
