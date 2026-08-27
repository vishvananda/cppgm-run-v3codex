function @probe(%phi : ptr [pass=by_address], %compare : ptr [pass=by_address], %constant : i64, %predecessor : u32, %branch : ptr [pass=by_address], %selected : ptr) -> u8 {
  block ^entry:
    %t4 = index i8 [projection=field] %phi, 232
    %limit_addr = index i8 [projection=field] %t4, 8
    jump ^for_cond

  block ^for_cond:
    %i = phi i64 [^entry: 0, ^for_iter: %next]
    %limitp = load ptr %limit_addr
    %base = load ptr %t4
    %span = binary sub ptr %limitp, %base
    %limit = binary div i64 %span, 48
    %more = cmp ult i64 %i, %limit
    branch %more, ^for_body, ^for_end

  block ^for_body:
    %b1 = load ptr %t4
    %off = binary mul i64 %i, 48
    %elem = index i8 %b1, %off
    %kind = load i32 %elem
    %notlabel = cmp ne i32 %kind, 3
    branch %notlabel, ^for_iter, ^check2

  block ^check2:
    %b2 = load ptr %t4
    %elem2 = index i8 %b2, %off
    %blockf = index i8 [projection=field] %elem2, 12
    %blockv = load u32 %blockf
    %wrong = cmp ne u32 %blockv, %predecessor
    branch %wrong, ^for_iter, ^check3

  block ^check3:
    %i1 = binary add i64 %i, 1
    %b3 = load ptr %t4
    %off2 = binary mul i64 %i1, 48
    %elem3 = index i8 %b3, %off2
    %kind2 = load i32 %elem3
    %notint = cmp ne i32 %kind2, 4
    branch %notint, ^for_iter, ^check4

  block ^check4:
    %b4 = load ptr %t4
    %elem4 = index i8 %b4, %off2
    %hasf = index i8 [projection=field] %elem4, 8
    %has = load u8 %hasf
    %missing = cmp eq u8 %has, 0
    branch %missing, ^for_iter, ^hit

  block ^hit:
    %b5 = load ptr %t4
    %elem5 = index i8 %b5, %off2
    %valf = index i8 [projection=field] %elem5, 16
    %val = load i64 %valf
    %eq = cmp eq i64 %val, %constant
    %eq8 = convert trunc u8 i64 %eq
    %opf = index i8 [projection=field] %compare, 40
    %op = load i32 %opf
    %iseq = cmp eq i32 %op, 18
    branch %iseq, ^then, ^else

  block ^then:
    jump ^condend

  block ^else:
    %ne = cmp ne i64 %val, %constant
    %ne8 = convert trunc u8 i64 %ne
    jump ^condend

  block ^condend:
    %cond = phi u8 [^then: %eq8, ^else: %ne8]
    branch %cond, ^taddr, ^faddr

  block ^taddr:
    %second = index i8 [projection=field] %branch, 136
    jump ^sel

  block ^faddr:
    %third = index i8 [projection=field] %branch, 184
    jump ^sel

  block ^sel:
    %chosen = phi ptr [^taddr: %second, ^faddr: %third]
    copyobj 48x8 %chosen, %selected
    return u8 1

  block ^for_iter:
    %next = binary add i64 %i, 2
    jump ^for_cond

  block ^for_end:
    return u8 0
}

global @phimem = {
  zero 232
  ptr addr @argsmem
  ptr addr @argsmem + 192
}

global @argsmem = {
  i32 999
  zero 44
  zero 48
  i32 3
  zero 8
  u32 7
  zero 32
  i32 4
  i32 0
  u8 1
  zero 7
  i64 4242
  zero 24
}

global @comparemem = {
  zero 40
  i32 18
  zero 4
}

global @branchmem = {
  zero 136
  i64 1111
  zero 40
  i64 2222
  zero 40
}

global @outmem = {
  zero 48
}

function @main() -> i64 [role=entry, binding=strong, keep_alias=yes] {
  block ^entry:
    %phi = addr @phimem
    %compare = addr @comparemem
    %branch = addr @branchmem
    %out = addr @outmem
    %hit = call u8 @probe(%phi, %compare, 4242, 7, %branch, %out)
    %hit64 = convert zext i64 u8 %hit
    %miss = cmp ne i64 %hit64, 1
    branch %miss, ^bad, ^checkcopy

  block ^checkcopy:
    %v = load i64 @outmem
    %wrong = cmp ne i64 %v, 1111
    return i64 %wrong

  block ^bad:
    return i64 2
}
