function @main() -> i64 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %eq = cmp eq i128 18446744073709551617, 18446744073709551617
    branch %eq, ^ne, ^bad

  block ^ne:
    %ne = cmp ne i128 18446744073709551617, 18446744073709551618
    branch %ne, ^lt, ^bad

  block ^lt:
    %lt = cmp lt i128 -18446744073709551616, 0
    branch %lt, ^le, ^bad

  block ^le:
    %le = cmp le i128 -18446744073709551616, -18446744073709551616
    branch %le, ^gt, ^bad

  block ^gt:
    %gt = cmp gt i128 18446744073709551616, 0
    branch %gt, ^ge, ^bad

  block ^ge:
    %ge = cmp ge i128 18446744073709551616, 18446744073709551616
    branch %ge, ^ult, ^bad

  block ^ult:
    %ult = cmp ult i128 18446744073709551616, 18446744073709551617
    branch %ult, ^ule, ^bad

  block ^ule:
    %ule = cmp ule i128 18446744073709551616, 18446744073709551616
    branch %ule, ^ugt, ^bad

  block ^ugt:
    %ugt = cmp ugt i128 -1, 18446744073709551616
    branch %ugt, ^uge, ^bad

  block ^uge:
    %uge = cmp uge i128 -1, -1
    branch %uge, ^good, ^bad

  block ^bad:
    return i64 1

  block ^good:
    return i64 0
}
