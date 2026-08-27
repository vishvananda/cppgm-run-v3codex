global @recs = {
  zero 672
}
global @obj = {
  zero 128
}
global @info = {
  u32 2
  u32 0
}

function @grow(%this : ptr, %type : u32) -> u32 {
  block ^entry:
    %wide = convert zext i64 u32 %type
    %sum = binary add i64 %wide, 1
    %out = convert trunc u32 i64 %sum
    return u32 %out
}

function @seed(%this : ptr, %variable : u32, %binding : u32, %type : u32) -> void {
  block ^entry:
    %sum32 = binary add u32 %binding, %type
    %nodes = index i8 [projection=field] %this, 104
    %data = load ptr %nodes
    %wide = convert zext i64 u32 %variable
    %scaled = binary mul i64 %wide, 168
    %rec = index i8 %data, %scaled
    %tail = index i8 [projection=field] %rec, 136
    store u32 %sum32, %tail
    return void
}

function @probe(%this : ptr, %variable : u32, %binding : u32, %type : u32,
                %initializer : ptr [pass=by_address], %has_initializer : u8,
                %declaration_only : u8, %qualified : u8) -> u32 {
  slot $object : u32

  block ^entry:
    branch %has_initializer, ^then, ^other

  block ^then:
    %t3 = index i8 [projection=field] %this, 104
    %t4 = copy ptr %initializer
    %t6 = load u32 %t4
    %t7 = convert zext i64 u32 %t6
    %d0 = load ptr %t3
    %m0 = binary mul i64 %t7, 168
    %a0 = index i8 %d0, %m0
    %f0 = index i8 [projection=field] %a0, 72
    %v0 = load i64 %f0
    %t14 = convert zext i64 u32 %variable
    %d1 = load ptr %t3
    %m1 = binary mul i64 %t14, 168
    %a1 = index i8 %d1, %m1
    %f1 = index i8 [projection=field] %a1, 72
    store i64 %v0, %f1
    %t19 = copy ptr %initializer
    %t21 = load u32 %t19
    %t22 = convert zext i64 u32 %t21
    %d2 = load ptr %t3
    %m2 = binary mul i64 %t22, 168
    %a2 = index i8 %d2, %m2
    %f2 = index i8 [projection=field] %a2, 132
    %v2 = load u32 %f2
    %d3 = load ptr %t3
    %a3 = index i8 %d3, %m1
    %f3 = index i8 [projection=field] %a3, 132
    store u32 %v2, %f3
    %t32 = copy ptr %initializer
    %t34 = load u32 %t32
    return u32 %t34

  block ^other:
    %t37 = call u32 @grow(%this, %type)
    store u32 %t37, $object
    branch %declaration_only, ^bail, ^orcheck

  block ^orcheck:
    %t39 = load u32 $object
    %t40 = cmp eq u32 %t39, 0
    branch %t40, ^bail, ^orright

  block ^orright:
    branch %qualified, ^andright, ^tail

  block ^andright:
    %t43 = index i8 [projection=field] %this, 104
    %t44 = load u32 $object
    %t45 = convert zext i64 u32 %t44
    %d4 = load ptr %t43
    %m4 = binary mul i64 %t45, 168
    %a4 = index i8 %d4, %m4
    %t47 = index i8 [projection=field] %a4, 136
    %t49 = index u32 [projection=array_element] %t47, 1
    %t50 = load u32 %t49
    %t51 = cmp ne u32 %t50, 0
    branch %t51, ^bail, ^tail

  block ^bail:
    return u32 4294967295

  block ^tail:
    call void @seed(%this, %variable, %binding, %type)
    %t57 = index i8 [projection=field] %this, 104
    %t59 = convert zext i64 u32 %variable
    %d5 = load ptr %t57
    %m5 = binary mul i64 %t59, 168
    %a5 = index i8 %d5, %m5
    %t61 = index i8 [projection=field] %a5, 136
    %t64 = load u32 %t61
    return u32 %t64
}

function @main() -> i64 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %obj = addr @obj
    %recs = addr @recs
    %nodes = index i8 [projection=field] %obj, 104
    store ptr %recs, %nodes
    %rec2 = index i8 %recs, 336
    %size2 = index i8 [projection=field] %rec2, 72
    store i64 77, %size2
    %align2 = index i8 [projection=field] %rec2, 132
    store u32 8, %align2
    %info = addr @info
    %out = call u32 @probe(%obj, 1, 10, 20, %info, 1, 0, 0)
    %outwide = convert zext i64 u32 %out
    %bad0 = cmp ne i64 %outwide, 2
    branch %bad0, ^fail, ^check1

  block ^check1:
    %rec1 = index i8 %recs, 168
    %size1a = index i8 [projection=field] %rec1, 72
    %size1 = load i64 %size1a
    %bad1 = cmp ne i64 %size1, 77
    branch %bad1, ^fail, ^check2

  block ^check2:
    %align1a = index i8 [projection=field] %rec1, 132
    %align1v = load u32 %align1a
    %align1 = convert zext i64 u32 %align1v
    %bad2 = cmp ne i64 %align1, 8
    branch %bad2, ^fail, ^good

  block ^good:
    return i64 0

  block ^fail:
    return i64 1
}
