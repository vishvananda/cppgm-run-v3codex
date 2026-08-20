// N3485 focus: 7.2 [dcl.enum] scoped enumerations
enum class FY { X, Y = 3 };
using GY = FY;
FY f;
decltype(FY::X) fx;
static_assert(FY::Y == FY::Y, "ok");
static_assert(GY::Y == FY::Y, "ok");
