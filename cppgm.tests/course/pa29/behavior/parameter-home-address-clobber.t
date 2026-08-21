function @main() -> i32 [role=entry, binding=strong, keep_alias=yes] {
  slot $object : obj<24x8>
  slot $first : i64
  slot $second : i64
  slot $context : i64
  slot $atomic_object : obj<24x8>
  slot $atomic_context : i64

  block ^entry:
    %object_address = addr $object
    %first_address = addr $first
    %second_address = addr $second
    %context_address = addr $context
    %stored_context = call ptr @initialize(%object_address, %first_address,
                                           %second_address, %context_address)
    %wrong = cmp ne ptr %stored_context, %context_address
    %atomic_object_address = addr $atomic_object
    %atomic_context_address = addr $atomic_context
    %stored_atomic_context = call ptr @initialize_atomic(
        %atomic_object_address, %first_address, %second_address,
        %atomic_context_address)
    %atomic_wrong = cmp ne ptr %stored_atomic_context, %atomic_context_address
    %either_wrong = binary or i64 %wrong, %atomic_wrong
    %exit = convert trunc i32 i64 %either_wrong
    return i32 %exit
}

function @initialize(%this : ptr, %first : ptr, %second : ptr,
                     %context : ptr) -> ptr [binding=internal] {
  block ^entry:
    %context_field = index i8 %this, 16
    store ptr %context, %context_field
    eh_try ^unwind
    call void @touch()
    eh_end
    jump ^read_context

  block ^unwind:
    resume

  block ^read_context:
    %stored_context = load ptr %context_field
    return ptr %stored_context
}

function @initialize_atomic(%this : ptr, %first : ptr, %second : ptr,
                            %context : ptr) -> ptr [binding=internal] {
  block ^entry:
    %context_field = index i8 %this, 16
    atomic_store ptr %context, %context_field, 0
    eh_try ^unwind
    call void @touch()
    eh_end
    jump ^read_context

  block ^unwind:
    resume

  block ^read_context:
    %stored_context = load ptr %context_field
    return ptr %stored_context
}

function @touch() -> void [binding=internal] {
  block ^entry:
    return void
}
