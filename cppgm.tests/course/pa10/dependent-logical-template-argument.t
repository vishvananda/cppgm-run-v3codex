template<bool, class> struct enable_if {};
template<class, class> struct same {};
template<class T, class U>
typename enable_if<same<T, U>::value &&
  (same<T, T>::value || same<U, U>::value), void>::type select();
