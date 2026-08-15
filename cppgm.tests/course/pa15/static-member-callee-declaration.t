namespace std
{
class ios_base
{
public:
	static bool sync_with_stdio(bool);
};
}

void call_sync()
{
	std::ios_base::sync_with_stdio(false);
}
