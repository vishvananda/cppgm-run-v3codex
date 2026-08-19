// User locals spelled like compiler-generated names keep exact serialized
// LowIR presentation and identical object behavior.
int t5 = 5;
int use(int t3, int retmerge__1) {
  int t7 = t3 + retmerge__1;
  int __force_inline_slot_2 = 2;
  if (t7 > 3) { t7 += t5; } else { t7 -= __force_inline_slot_2; }
  for (int t2 = 0; t2 < 3; ++t2) t7 += t2;
  return t7;
}
int main() { return use(2, 2) == 12 ? 0 : 1; }
