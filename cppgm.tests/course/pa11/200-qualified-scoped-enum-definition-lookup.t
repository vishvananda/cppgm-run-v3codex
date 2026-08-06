struct writer { enum class state : int; };
enum class writer::state : int { ready = 1 };
static_assert(writer::state::ready == writer::state::ready, "ok");
