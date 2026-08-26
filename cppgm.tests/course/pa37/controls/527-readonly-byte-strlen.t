declare function @measure_bytes(%data : ptr [capture=nocapture, access=read]) -> i64 [effects=readonly, unwind=no, binding=strong, object=cppgm_builtin_strlen]

global @readonly_word [storage=readonly, binding=internal] = {
  i8 97
  i8 98
  i8 99
  i8 0
}
global @readonly_embedded_nul [storage=readonly, binding=internal] = {
  i8 120
  i8 0
  i8 121
  i8 0
}
global @writable_word [binding=internal] = {
  i8 97
  i8 98
  i8 99
  i8 0
}
global @readonly_unterminated [storage=readonly, binding=internal] = {
  i8 97
  i8 98
}

function @folds_complete_word() -> i64 [binding=strong] {
  block ^entry:
    %data = addr @readonly_word
    %length = call i64 @measure_bytes(%data)
    return i64 %length
}

function @folds_at_first_nul() -> i64 [binding=strong] {
  block ^entry:
    %data = addr @readonly_embedded_nul
    %length = call i64 @measure_bytes(%data)
    return i64 %length
}

function @keeps_writable_data() -> i64 [binding=strong] {
  block ^entry:
    %data = addr @writable_word
    %length = call i64 @measure_bytes(%data)
    return i64 %length
}

function @keeps_unterminated_data() -> i64 [binding=strong] {
  block ^entry:
    %data = addr @readonly_unterminated
    %length = call i64 @measure_bytes(%data)
    return i64 %length
}

function @keeps_dynamic_pointer(%data : ptr) -> i64 [binding=strong] {
  block ^entry:
    %length = call i64 @measure_bytes(%data)
    return i64 %length
}
