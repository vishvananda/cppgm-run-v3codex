global @signed_byte : i8 = -1
global @normalization_sink : i32 = 0

function @zero_extended_load() -> i64 [unwind=no] {
  block ^entry:
    %narrow = load i8 @signed_byte
    %wide = convert zext i64 i8 %narrow
    return i64 %wide
}

function @sign_extended_load() -> i64 [unwind=no] {
  block ^entry:
    %narrow = load i8 @signed_byte
    %wide = convert sext i64 i8 %narrow
    return i64 %wide
}

function @zero_extended_parameter_load(%address : ptr) -> i64 [unwind=no] {
  block ^entry:
    %narrow = load i8 %address
    %wide = convert zext i64 i8 %narrow
    return i64 %wide
}

function @sign_extended_parameter_load(%address : ptr) -> i64 [unwind=no] {
  block ^entry:
    %narrow = load i8 %address
    %wide = convert sext i64 i8 %narrow
    return i64 %wide
}

function @reinterpreted_unsigned_load(%address : ptr) -> i32 [unwind=no] {
  block ^entry:
    %raw = load i8 %address !dbg(normalization.cpp, 3, 5)
    %unsigned = copy u8 %raw !dbg(normalization.cpp, 4, 5)
    %wide = convert zext i32 u8 %unsigned !dbg(normalization.cpp, 5, 5)
    return i32 %wide !dbg(normalization.cpp, 6, 3)
}

function @reinterpreted_signed_load(%address : ptr) -> i32 [unwind=no] {
  block ^entry:
    %raw = load u8 %address !dbg(normalization.cpp, 8, 5)
    %signed = copy i8 %raw !dbg(normalization.cpp, 9, 5)
    %wide = convert sext i32 i8 %signed !dbg(normalization.cpp, 10, 5)
    return i32 %wide !dbg(normalization.cpp, 11, 3)
}

function @zero_then_signed_wider(%value : u8) -> i64 [unwind=no] {
  block ^entry:
    %medium = convert zext i32 u8 %value !dbg(normalization.cpp, 10, 5)
    %wide = convert sext i64 i32 %medium !dbg(normalization.cpp, 11, 5)
    return i64 %wide !dbg(normalization.cpp, 12, 3)
}

function @signed_then_signed_wider(%value : i8) -> i64 [unwind=no] {
  block ^entry:
    %medium = convert sext i32 i8 %value !dbg(normalization.cpp, 16, 5)
    %wide = convert sext i64 i32 %medium !dbg(normalization.cpp, 17, 5)
    return i64 %wide !dbg(normalization.cpp, 18, 3)
}

function @signed_then_unsigned_guard(%value : i8) -> i64 [unwind=no] {
  block ^entry:
    %signed_medium = convert sext i32 i8 %value
    %medium = copy u32 %signed_medium
    %wide = convert zext i64 u32 %medium
    return i64 %wide
}

function @intervening_use_guard(%value : u8) -> i64 [unwind=no] {
  block ^entry:
    %medium = convert zext u32 u8 %value
    store volatile u32 %medium, @normalization_sink
    %signed_medium = copy i32 %medium
    %wide = convert sext i64 i32 %signed_medium
    return i64 %wide
}

function @main() -> i32 [role=entry, unwind=no] {
  block ^entry:
    %zero_load = call i64 @zero_extended_load()
    %signed_load = call i64 @sign_extended_load()
    %signed_byte = addr @signed_byte
    %zero_parameter_load = call i64 @zero_extended_parameter_load(%signed_byte)
    %signed_parameter_load = call i64 @sign_extended_parameter_load(%signed_byte)
    %reinterpreted_load = call i32 @reinterpreted_unsigned_load(%signed_byte)
    %reinterpreted_signed_load = call i32 @reinterpreted_signed_load(%signed_byte)
    %zero_wider = call i64 @zero_then_signed_wider(255)
    %signed_wider = call i64 @signed_then_signed_wider(-1)
    %unsigned_guard = call i64 @signed_then_unsigned_guard(-1)
    %intervening = call i64 @intervening_use_guard(255)
    %sink = load volatile i32 @normalization_sink
    %bad_zero_load = cmp ne i64 %zero_load, 255
    %bad_signed_load = cmp ne i64 %signed_load, -1
    %bad_zero_parameter_load = cmp ne i64 %zero_parameter_load, 255
    %bad_signed_parameter_load = cmp ne i64 %signed_parameter_load, -1
    %bad_reinterpreted_load = cmp ne i32 %reinterpreted_load, 255
    %bad_reinterpreted_signed_load = cmp ne i32 %reinterpreted_signed_load, -1
    %bad_zero_wider = cmp ne i64 %zero_wider, 255
    %bad_signed_wider = cmp ne i64 %signed_wider, -1
    %bad_unsigned_guard = cmp ne i64 %unsigned_guard, 4294967295
    %bad_intervening = cmp ne i64 %intervening, 255
    %bad_sink = cmp ne i32 %sink, 255
    %bad_direct_loads = binary or i64 %bad_zero_load, %bad_signed_load
    %bad_reinterpreted_loads = binary or i64 %bad_reinterpreted_load, %bad_reinterpreted_signed_load
    %bad_loads = binary or i64 %bad_direct_loads, %bad_reinterpreted_loads
    %bad_parameter_loads = binary or i64 %bad_zero_parameter_load, %bad_signed_parameter_load
    %bad_a = binary or i64 %bad_loads, %bad_parameter_loads
    %bad_b = binary or i64 %bad_zero_wider, %bad_signed_wider
    %bad_c = binary or i64 %bad_unsigned_guard, %bad_intervening
    %bad_d = binary or i64 %bad_a, %bad_b
    %bad_e = binary or i64 %bad_c, %bad_sink
    %bad = binary or i64 %bad_d, %bad_e
    %exit = convert trunc i32 i64 %bad
    return i32 %exit
}
