global @observed_terminal_phi : i64 = 0

function @observe_terminal_phi(%value : i64) -> void
    [binding=internal, no_inline=yes, unwind=no] {
  block ^entry:
    store i64 %value, @observed_terminal_phi
    return void
}

function @thread_terminal_chain(%value : i64, %choose : i64) -> i64
    [no_inline=yes, unwind=no] {
  block ^entry:
    branch %choose, ^left, ^right

  block ^left:
    %left_value = unary neg i64 %value
    jump ^merge

  block ^right:
    %right_value = unary bitnot i64 %value
    jump ^merge

  block ^merge:
    %selected = phi i64 [^left: %left_value, ^right: %right_value]
    %step0 = unary bitnot i64 %selected
    %step1 = unary neg i64 %step0
    %step2 = unary bitnot i64 %step1
    return i64 %step2
}

function @thread_phi_to_return_branch(%value : i64, %choose : i64) -> i64
    [no_inline=yes, unwind=no] {
  block ^entry:
    branch %choose, ^left, ^right

  block ^left:
    jump ^merge

  block ^right:
    jump ^merge

  block ^merge:
    %selected = phi i64 [^left: %value, ^right: 0]
    branch %selected, ^taken, ^not_taken

  block ^taken:
    return i64 11

  block ^not_taken:
    return i64 22
}

function @retain_shared_terminal_phi(%value : i64, %choose : i64) -> i64
    [no_inline=yes, unwind=no] {
  block ^entry:
    branch %choose, ^left, ^right

  block ^left:
    jump ^merge

  block ^right:
    jump ^merge

  block ^merge:
    %selected = phi i64 [^left: %value, ^right: 0]
    call void @observe_terminal_phi(%selected)
    return i64 %selected
}

function @retain_long_terminal_chain(%value : i64, %choose : i64) -> i64
    [no_inline=yes, unwind=no] {
  block ^entry:
    branch %choose, ^left, ^right

  block ^left:
    jump ^merge

  block ^right:
    jump ^merge

  block ^merge:
    %selected = phi i64 [^left: %value, ^right: 3]
    %step0 = unary neg i64 %selected
    %step1 = unary bitnot i64 %step0
    %step2 = unary neg i64 %step1
    %step3 = unary bitnot i64 %step2
    return i64 %step3
}

function @main() -> i32 [role=entry, unwind=no] {
  block ^entry:
    %chain_left = call i64 @thread_terminal_chain(5, 1)
    %chain_right = call i64 @thread_terminal_chain(5, 0)
    %branch_left = call i64 @thread_phi_to_return_branch(1, 1)
    %branch_right = call i64 @thread_phi_to_return_branch(1, 0)
    %shared = call i64 @retain_shared_terminal_phi(9, 1)
    %long = call i64 @retain_long_terminal_chain(7, 1)
    %observed = load i64 @observed_terminal_phi
    %sum0 = binary add i64 %chain_left, %chain_right
    %sum1 = binary add i64 %sum0, %branch_left
    %sum2 = binary add i64 %sum1, %branch_right
    %sum3 = binary add i64 %sum2, %shared
    %sum4 = binary add i64 %sum3, %long
    %sum5 = binary add i64 %sum4, %observed
    %bad = cmp ne i64 %sum5, 63
    %status = convert trunc i32 i64 %bad
    return i32 %status
}
