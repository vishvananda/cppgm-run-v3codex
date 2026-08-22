declare function @observe(%value : i64) -> void

function @forwards(%dest : ptr, %a : i64, %b : i64) -> void {
  slot $staging : obj<24x8>

  block ^entry:
    %base = addr $staging
    %f8 = index i8 [projection=field] %base, 8
    %f16 = index i8 [projection=field] %base, 16
    store i64 %a, %base
    store i64 %b, %f8
    store i64 7, %f16
    copyobj 24x8 %base, %dest
    return void
}

function @keeps_load(%dest : ptr, %a : i64) -> i64 {
  slot $staging : obj<16x8>

  block ^entry:
    %base = addr $staging
    %f8 = index i8 [projection=field] %base, 8
    store i64 %a, %base
    store i64 9, %f8
    %read = load i64 %base
    copyobj 16x8 %base, %dest
    return i64 %read
}

function @keeps_two_copies(%dest : ptr, %other : ptr, %a : i64) -> void {
  slot $staging : obj<16x8>

  block ^entry:
    %base = addr $staging
    %f8 = index i8 [projection=field] %base, 8
    store i64 %a, %base
    store i64 11, %f8
    copyobj 16x8 %base, %dest
    copyobj 16x8 %base, %other
    return void
}

function @keeps_conditional_store(%dest : ptr, %a : i64, %pick : u8) -> void {
  slot $staging : obj<16x8>

  block ^entry:
    %base = addr $staging
    %f8 = index i8 [projection=field] %base, 8
    store i64 %a, %base
    branch %pick, ^set, ^done

  block ^set:
    store i64 13, %f8
    jump ^done

  block ^done:
    copyobj 16x8 %base, %dest
    return void
}
