// An explicit -O0 driver invocation does not publish __OPTIMIZE__.
#ifndef __OPTIMIZE__
int main() { return 0; }
#else
int main() { return 1; }
#endif
