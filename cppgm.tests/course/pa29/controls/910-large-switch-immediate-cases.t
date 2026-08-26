global @selector : i64 = 14
global @dynamic_case : i64 = 77

function @main() -> i64 [role=entry, unwind=no] {
  block ^entry:
    %which = load i64 @selector
    %dynamic = load i64 @dynamic_case
    switch %which, ^miss, 1:^case0, 2:^case1, 3:^case2, 4:^case3, 5:^case4, 6:^case5, 7:^case6, 8:^case7, 9:^case8, 10:^case9, 11:^case10, 12:^case11, 13:^case12, 14:^case13, 15:^case14, 16:^case15, %dynamic:^dynamic

  block ^case0:
    return i64 60
  block ^case1:
    return i64 61
  block ^case2:
    return i64 62
  block ^case3:
    return i64 63
  block ^case4:
    return i64 64
  block ^case5:
    return i64 65
  block ^case6:
    return i64 66
  block ^case7:
    return i64 67
  block ^case8:
    return i64 68
  block ^case9:
    return i64 69
  block ^case10:
    return i64 70
  block ^case11:
    return i64 71
  block ^case12:
    return i64 72
  block ^case13:
    return i64 73
  block ^case14:
    return i64 74
  block ^case15:
    return i64 75
  block ^dynamic:
    return i64 76
  block ^miss:
    return i64 77
}
