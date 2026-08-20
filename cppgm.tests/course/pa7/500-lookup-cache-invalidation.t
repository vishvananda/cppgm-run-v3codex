typedef long T;

namespace A {}
namespace B { using namespace A; }
namespace C {
	using namespace B;
	T before_target_extension;
}

namespace A { using ::T; }
namespace C {
	typedef char T;
	T after_target_extension;
}

namespace D {
	T before_local_edge;
	using namespace A;
	typedef char T;
	T after_local_edge;
}
