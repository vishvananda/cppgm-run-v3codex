void copy_bytes(void *destination, const void *source)
{
	__builtin_memcpy(destination, source, 4);
}

void move_bytes(void *destination, const void *source)
{
	__builtin_memmove(destination, source, 4);
}
