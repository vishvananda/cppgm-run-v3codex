declare function @measure_bytes(%data : ptr) -> i64 [effects=readonly, unwind=no, binding=strong, object=cppgm_builtin_strlen]

function @ordinary_length(%data : ptr) -> i64 [binding=strong] {
  block ^entry:
    return i64 7
}

function @declared_builtin_probe(%data : ptr) -> i64 [binding=strong] {
  block ^entry:
    %length = call i64 @measure_bytes(%data)
    return i64 %length
}

function @ordinary_call_control(%data : ptr) -> i64 [binding=strong] {
  block ^entry:
    %length = call i64 @ordinary_length(%data)
    return i64 %length
}
