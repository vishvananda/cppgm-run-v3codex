__attribute__((section("bad-name")))
int rejected_section_name = 1;

int main()
{
	return rejected_section_name;
}
