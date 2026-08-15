// VALIDATION: taking an incomplete object's address does not require its size.

struct opaque;

void consume(opaque*);

void relay(opaque& value)
{
  consume(&value);
}
