typedef long T;

namespace A {}
namespace B { using namespace A; }
namespace C {
	using namespace B;
	T before_target_extension;
}

namespace A { typedef char T; }
namespace C { T after_target_extension; }

namespace D {
	T before_local_edge;
	using namespace A;
	T after_local_edge;
}
