extern "C" int hosted_identity(int);

namespace hosted_bridge
{
using ::hosted_identity;
}

using hosted_bridge::hosted_identity;
using hosted_bridge::hosted_identity;

int (*hosted_identity_anchor)(int) = &hosted_identity;
