__attribute__((noinline)) int guarded_value(bool impossible)
{
	if(impossible) __builtin_unreachable();
	return 7;
}

int main()
{
	return guarded_value(false) - 7;
}
