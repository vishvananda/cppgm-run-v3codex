declare function @ordinary() -> void [effects=readwrite]

function @main() -> i64 [role=entry] {
  block ^entry:
    return i64 0
}
