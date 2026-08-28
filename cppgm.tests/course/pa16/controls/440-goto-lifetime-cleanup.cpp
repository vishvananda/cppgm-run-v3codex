int trace = 0;

struct Guard
{
	int digit;
	Guard(int value) : digit(value) {}
	~Guard() { trace = trace * 10 + digit; }
};

void leave_nested_scopes()
{
	{
		Guard outer(1);
		{
			Guard inner(2);
			goto nested_done;
		}
	}
nested_done:
	trace = trace * 10 + 4;
}

void repeat_same_scope()
{
	int pass = 0;
repeat:
	Guard repeated(3);
	++pass;
	if (pass == 1) goto repeat;
}

int main()
{
	leave_nested_scopes();
	if (trace != 214) return 1;
	trace = 0;
	repeat_same_scope();
	return trace == 33 ? 0 : 2;
}
