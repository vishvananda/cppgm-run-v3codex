extern void x_base_constructor(void*) __asm__("_ZN1XC2Ev");
extern void y_base_constructor(void*) __asm__("_ZN1YC2Ev");
extern int exported_value __asm__("_ZN1n14exported_valueE");

int invoke_abi_symbols(void* x_storage, void* y_storage)
{
  x_base_constructor(x_storage);
  y_base_constructor(y_storage);
  return *(int*)x_storage == 42 && *(int*)y_storage == 43 &&
    exported_value == 44 ? 0 : 1;
}
