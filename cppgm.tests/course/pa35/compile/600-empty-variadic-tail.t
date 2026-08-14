#define FIRST(value, ...) value
#define FORWARD(value, ...) FIRST(value, __VA_ARGS__)
#define GNU_TAIL(value, ...) value , ## __VA_ARGS__

static_assert(FIRST(7) == 7, "fixed-only invocation has an empty tail");
static_assert(FORWARD(11) == 11, "empty tail forwards through a macro");

int values[] = {GNU_TAIL(13)};
static_assert(sizeof(values) / sizeof(values[0]) == 1,
              "empty GNU tail removes its comma");
