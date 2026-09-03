// N3485 3.9.1/9: void is an incomplete type that can never be completed, so no
// object may be declared with it.  Pointers to void and void returns are fine
// and are covered by the positive control.

void x;

int main() {}
