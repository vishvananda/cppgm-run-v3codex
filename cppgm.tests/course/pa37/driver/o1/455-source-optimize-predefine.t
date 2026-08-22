// An optimizing driver invocation publishes the __OPTIMIZE__ predefine.
#ifdef __OPTIMIZE__
int main() { return 0; }
#else
int main() { return 1; }
#endif
