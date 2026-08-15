// VALIDATION: a member definition has its class's access while naming itself.

class outer
{
  struct nested
  {
    void function();
  };
};

void outer::nested::function()
{
}
