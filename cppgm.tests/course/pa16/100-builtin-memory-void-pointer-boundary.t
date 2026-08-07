void copy(void *dst, const void *src)
{
  __builtin_memcpy(dst, src, 258);
  __builtin_memmove(dst, src, 258);
}
