struct Mandatory
{
	int value() const { return 7; }
};

struct Guarded
{
	int stored;

	__attribute__((noinline)) Guarded() : stored(11) {}
	int value() const { return stored; }
};

int main()
{
	return Mandatory().value() + Guarded().value() - 18;
}
