declare function @observe(%value : i64) -> void

function @repair_fold_target_phi(%choose : i64) -> i64 [no_inline=yes] {
  block ^entry:
    branch %choose, ^selected, ^not_selected

  block ^selected:
    jump ^materialize

  block ^not_selected:
    jump ^materialize

  block ^materialize:
    %boolean = phi i64 [^selected: 1, ^not_selected: 0]
    %test = cmp ne i64 %boolean, 0
    branch %test, ^join, ^alternate

  block ^alternate:
    jump ^join

  block ^join:
    %result = phi i64 [^materialize: 11, ^alternate: 22]
    return i64 %result
}

function @stale_phi_callee(%choose : i64) -> i64 [binding=internal] {
  block ^entry:
    branch %choose, ^dead, ^live

  block ^dead:
    unreachable

  block ^live:
    jump ^join

  block ^join:
    %result = phi i64 [^live: 47]
    return i64 %result
}

function @inline_after_dead_predecessor(%choose : i64) -> i64
    [binding=strong] {
  block ^entry:
    %result = call i64 @stale_phi_callee(%choose)
    return i64 %result
}

function @cross_slot_needed_phi(%choose : i64) -> i64 [no_inline=yes] {
  slot $source : i64
  slot $forwarded : i64

  block ^entry:
    %unresolved = load i64 $source
    call void @observe(%unresolved)
    branch %choose, ^left, ^right

  block ^left:
    store i64 5, $source
    jump ^join

  block ^right:
    store i64 9, $source
    jump ^join

  block ^join:
    %merged = load i64 $source
    store i64 %merged, $forwarded
    %result = load i64 $forwarded
    return i64 %result
}
