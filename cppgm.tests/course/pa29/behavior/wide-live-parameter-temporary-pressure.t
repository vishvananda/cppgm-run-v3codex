global @base_vtable = {
  zero 128
}
global @derived_vtable = {
  zero 128
}

function @token_ctor(%address : ptr) -> void {
  block ^entry:
    store u8 99, %address
    return void
}

function @construct(%this : ptr, %reference : ptr, %a : i32, %b : i32,
                    %c : i64, %d : u8, %e : u8, %f : i32, %g : i32,
                    %h : i64, %i : i64) -> void {
  block ^entry:
    %base_vtable = addr @base_vtable
    %base_address_point = index i8 [projection=field] %base_vtable, 16
    store ptr %base_address_point, %this
    %derived_vtable = addr @derived_vtable
    %derived_address_point = index i8 [projection=field] %derived_vtable, 16
    store ptr %derived_address_point, %this
    %reference_field = index i8 [projection=field] %this, 8
    store ptr %reference, %reference_field
    %field_a = index i8 [projection=field] %this, 16
    store i32 %a, %field_a
    %field_b = index i8 [projection=field] %this, 20
    store i32 %b, %field_b
    %field_c = index i8 [projection=field] %this, 24
    store i64 %c, %field_c
    %field_d = index i8 [projection=field] %this, 32
    store u8 %d, %field_d
    %field_e = index i8 [projection=field] %this, 33
    store u8 %e, %field_e
    %field_f = index i8 [projection=field] %this, 36
    store i32 %f, %field_f
    %field_g = index i8 [projection=field] %this, 40
    store i32 %g, %field_g
    %field_h = index i8 [projection=field] %this, 48
    store i64 %h, %field_h
    %field_i = index i8 [projection=field] %this, 56
    store i64 %i, %field_i
    %token = index i8 [projection=field] %this, 72
    call void @token_ctor(%token)
    return void
}

function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  slot $object : obj<96x8>
  slot $reference : i64

  block ^entry:
    zeroinit 96x8 $object
    %object = addr $object
    %reference = addr $reference
    call void @construct(%object, %reference, 11, 12, 13, 14, 15, 16, 17, 18, 19)
    %stored_vtable = load ptr %object
    %expected_vtable = addr @derived_vtable
    %expected_address_point = index i8 [projection=field] %expected_vtable, 16
    %wrong_vtable = cmp ne ptr %stored_vtable, %expected_address_point
    %reference_field = index i8 [projection=field] %object, 8
    %stored_reference = load ptr %reference_field
    %wrong_reference = cmp ne ptr %stored_reference, %reference
    %field_a = index i8 [projection=field] %object, 16
    %stored_a = load i32 %field_a
    %wrong_a = cmp ne i32 %stored_a, 11
    %token = index i8 [projection=field] %object, 72
    %stored_token = load u8 %token
    %wrong_token = cmp ne u8 %stored_token, 99
    %wrong_left = binary or i64 %wrong_vtable, %wrong_reference
    %wrong_right = binary or i64 %wrong_a, %wrong_token
    %wrong = binary or i64 %wrong_left, %wrong_right
    %exit = convert trunc i32 i64 %wrong
    return i32 %exit
}
