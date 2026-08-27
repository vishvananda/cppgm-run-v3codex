declare function @readonly_value() -> i64 [effects=readonly, unwind=no]
declare function @readnone_value() -> i64 [effects=readnone, unwind=no]
declare function @ordinary_value() -> i64 [unwind=no]
declare function @throwing_readonly_value() -> i64 [effects=readonly]

function @drop_effect_free_calls(%fn : ptr) -> i64 {
  block ^entry:
    %direct_readonly = call i64 @readonly_value()
    %direct_readnone = call i64 @readnone_value()
    %indirect_readonly = call i64 %fn() as () -> i64 [effects=readonly, unwind=no]
    %ordinary = call i64 @ordinary_value()
    %throwing = call i64 @throwing_readonly_value()
    return i64 0
}
