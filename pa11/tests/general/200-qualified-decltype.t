namespace N { int x; int f(); }
decltype(N::x) y;
decltype((N::x)) z = N::x;
decltype(N::f) *fp;
