// AUDIT: using-directive lookup merges function declarations before overload
// resolution instead of diagnosing value-style lookup ambiguity.
namespace exact_ns {
int pick(int value) { return value + 1; }
}

namespace conversion_ns {
int pick(double value) { return (int)value + 2; }
}

using namespace exact_ns;
using namespace conversion_ns;

int main() { return pick(4) == 5 ? 0 : 1; }
