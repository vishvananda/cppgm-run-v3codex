declare global @__external_rtti__int : ptr [binding=strong, object=_ZTIi]
declare global @__rtti_class : ptr [role=rtti_class]
declare global @__rtti_si : ptr [role=rtti_si]
declare global @__rtti_vmi : ptr [role=rtti_vmi]
declare global @__rtti_data : ptr [role=rtti_data]
declare global @__more_rtti_data : ptr [role=rtti_data]
declare function @external_hook() -> i32 [linkage=c, binding=strong, object=external_hook]
declare function @allocate(%n : i64) -> ptr [role=allocate_memory]
declare function @deallocate(%p : ptr) -> void [role=free_memory]
declare function @pure() -> void [role=pure_virtual]
declare function @cast(%p : ptr) -> ptr [role=dynamic_cast]
declare function @throw_bad_cast() -> void [role=bad_cast]
declare function @throw_bad_typeid() -> void [role=bad_typeid]
declare function @kept_call_shape() -> void [no_inline=yes]

function @main() -> i64 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %0 = const i64 0
    return i64 %0
}
