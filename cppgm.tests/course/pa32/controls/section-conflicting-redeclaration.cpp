__attribute__((section("first_section")))
extern int conflicting_section;

__attribute__((section("second_section")))
int conflicting_section = 1;

int main()
{
	return conflicting_section;
}
