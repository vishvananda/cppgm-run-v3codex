class outer
{
  struct nested
  {
    nested();
  };
};

inline outer::nested::nested() = default;
