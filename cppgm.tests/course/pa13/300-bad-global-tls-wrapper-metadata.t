global @tls : i64 [storage=thread_local] = 0
global @bad : i64 [tls_for=@tls] = 0

function @main() -> i64 {
  block ^entry:
    return i64 0
}
