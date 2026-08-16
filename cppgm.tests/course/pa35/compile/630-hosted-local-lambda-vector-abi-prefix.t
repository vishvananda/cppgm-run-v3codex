#include <string>
#include <vector>

// Encoding vector<Frame>'s nested implementation recursively resolves the
// local type's lambda context.  The outer member prefix must remain stable if
// that resolution grows the canonical ABI type graph.
bool use(const std::string& name)
{
  std::vector<std::string> retained(1, name);
  const auto run = [&](const std::string& value) {
    struct Frame
    {
      std::string name;
    };
    std::vector<Frame> stack(1, Frame{value});
    return stack[0].name == retained[0] && retained.size() == 1;
  };
  return run(name);
}

int main()
{
  return use("start") ? 0 : 1;
}
