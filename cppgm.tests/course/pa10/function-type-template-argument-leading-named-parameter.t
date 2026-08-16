template<class> struct function {};
template<class, class> struct pair {};
template<class> struct vector {};
struct string {};
typedef unsigned long size_t;

function<bool(size_t, const string &, size_t &)> callback;
function<bool(size_t, vector<pair<size_t, size_t> > &)> range_callback;
