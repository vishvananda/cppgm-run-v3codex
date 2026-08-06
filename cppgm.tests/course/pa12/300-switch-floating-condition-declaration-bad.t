// VALIDATION: a switch declaration condition must be integral or enum typed.

void invalid()
{
  switch (float value = 1.0f) {}
}
