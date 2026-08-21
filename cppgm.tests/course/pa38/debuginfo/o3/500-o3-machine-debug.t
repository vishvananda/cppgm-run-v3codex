function @main() -> i64 [role=entry] {
  block ^entry:
    %value = binary add i64 40, 2 !dbg(test.cpp, 3, 3)
    %same = copy i64 %value !dbg(test.cpp, 4, 3)
    %bad = cmp ne i64 %same, 42 !dbg(test.cpp, 5, 3)
    return i64 %bad !dbg(test.cpp, 6, 3)
}
