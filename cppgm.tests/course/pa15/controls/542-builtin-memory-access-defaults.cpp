void move_bytes(void *destination, const void *source)
{
	__builtin_memmove(destination, source, 4);
}
