function @not_strlen(%value : i64) -> i64 [effects=readonly, unwind=no, binding=strong, object=cppgm_builtin_strlen] {
  block ^entry:
    return i64 %value
}

function @incompatible_signature_control() -> i64 [binding=strong] {
  block ^entry:
    %value = call i64 @not_strlen(9)
    return i64 %value
}
