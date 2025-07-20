namespace leanstore
{
namespace utils
{
// Template for std::function_ref
template <typename T>
class function_ref;

template <typename Ret, typename... Args>
class function_ref<Ret(Args...)> {
    void* obj = nullptr;
    Ret (*callback)(void*, Args...) = nullptr;

public:
    // Constructor accepting callable objects
    template <typename F>
    function_ref(F&& f) noexcept
        : obj(const_cast<void*>(reinterpret_cast<const void*>(std::addressof(f)))),
          callback([](void* obj, Args... args) -> Ret {
              return (*reinterpret_cast<std::remove_reference_t<F>*>(obj))(std::forward<Args>(args)...);
          }) {}

    // Callable operator
    Ret operator()(Args... args) const {
        return callback(obj, std::forward<Args>(args)...);
    }
};

}
}