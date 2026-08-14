struct external_flags
{
  static const unsigned short digit = 2048;
};

struct defined_flags
{
  static const unsigned short space = 8192;
};

const unsigned short defined_flags::space;

unsigned short take_value(unsigned short);
unsigned short take_reference(const unsigned short&);

unsigned short static_member_value()
{
  return take_value(external_flags::digit);
}

unsigned short static_member_reference()
{
  return take_reference(external_flags::digit)
       + take_reference(defined_flags::space);
}

const unsigned short& distinct_local_static()
{
  static const unsigned short digit = 7;
  return digit;
}
