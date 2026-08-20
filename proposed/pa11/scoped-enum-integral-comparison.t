enum class FY { X, Y = 3 };
using GY = FY;
FY f;
decltype(FY::X) fx;
static_assert(FY::Y == 3, "ok");
static_assert(GY::Y == 3, "ok");
