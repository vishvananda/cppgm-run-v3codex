struct Text
{
};

struct HeaderScanner
{
	Text& StartTokenSpelling();
	void AppendTake(Text*);

	bool ScanHeaderName()
	{
		Text& spelling = StartTokenSpelling();
		AppendTake(&spelling);
		return true;
	}
};
