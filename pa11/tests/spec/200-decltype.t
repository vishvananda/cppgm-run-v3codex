// N3485 focus: 7.1.6.2 [dcl.type.simple] decltype-specifier
int x;
decltype(x) y;
decltype((x)) z = x;
