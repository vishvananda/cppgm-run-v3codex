function @coalesce_noalias(
    %source : ptr [alias=noalias],
    %destination : ptr [alias=noalias]) -> void {
  block ^entry:
    %source8 = index i8 [projection=field] %source, 8
    %destination8 = index i8 [projection=field] %destination, 8
    %value8 = load i64 %source8
    store i64 %value8, %destination8
    %source16 = index i8 [projection=field] %source, 16
    %destination16 = index i8 [projection=field] %destination, 16
    %value16 = load i64 %source16
    store i64 %value16, %destination16
    return void
}

function @retain_aliasable(%source : ptr, %destination : ptr) -> void {
  block ^entry:
    %source8 = index i8 [projection=field] %source, 8
    %destination8 = index i8 [projection=field] %destination, 8
    %value8 = load i64 %source8
    store i64 %value8, %destination8
    %source16 = index i8 [projection=field] %source, 16
    %destination16 = index i8 [projection=field] %destination, 16
    %value16 = load i64 %source16
    store i64 %value16, %destination16
    return void
}

function @retain_volatile(
    %source : ptr [alias=noalias],
    %destination : ptr [alias=noalias]) -> void {
  block ^entry:
    %source8 = index i8 [projection=field] %source, 8
    %destination8 = index i8 [projection=field] %destination, 8
    %value8 = load volatile i64 %source8
    store i64 %value8, %destination8
    %source16 = index i8 [projection=field] %source, 16
    %destination16 = index i8 [projection=field] %destination, 16
    %value16 = load i64 %source16
    store volatile i64 %value16, %destination16
    return void
}
