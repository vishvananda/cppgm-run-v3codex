// VALIDATION: compile-fail

struct base { int value; };
struct left : base {};
struct right : base {};
struct derived : left, right {};

int reject(derived& object, int base::*member)
{
	return object.*member;
}
