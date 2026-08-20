int constructed;
int destroyed;

struct YA {
  YA() {
    constructed = constructed + 1;
    if (constructed == 8) throw 1;
  }

  ~YA() {
    destroyed = destroyed + 1;
  }
};

struct YB {
  YA first;
  YA second;
  YA third;
  YA fourth;
  YA fifth;
  YA sixth;
  YA seventh;
  YA eighth;

  YB()
    : first(), second(), third(), fourth(), fifth(), sixth(), seventh(),
      eighth() {}
};

int main() {
  try {
    YB owner;
  } catch (int) {
    return constructed == 8 && destroyed == 7 ? 0 : 1;
  }
  return 2;
}
