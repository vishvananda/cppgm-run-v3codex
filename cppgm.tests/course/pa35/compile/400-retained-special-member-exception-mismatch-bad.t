template<class T>
struct exception_mismatch
{
  exception_mismatch(T*) noexcept;
};

template<class U>
exception_mismatch<U>::exception_mismatch(U*)
{
}
