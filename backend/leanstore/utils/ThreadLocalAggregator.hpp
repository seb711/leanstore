#pragma once
// -------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------
// -------------------------------------------------------------------------------------
namespace leanstore
{
namespace utils
{
namespace threadlocal
{

template <class CountersClass, class CounterType, typename T = uint64_t>
T sum(std::vector<CountersClass*>& counters, CounterType CountersClass::*c)
{
   T local_c = 0;
   for (auto& counterInstance : counters) {
      local_c += (counterInstance->*c).exchange(0);  // exchange and reset
   }
   return local_c;
}

template <class CountersClass, class CounterType, typename T = uint64_t>
T sum(std::vector<CountersClass*>& counters, CounterType CountersClass::*c, u8 index)
{
   T local_c = 0;
   for (auto& counterInstance : counters) {
      local_c += (counterInstance->*c)[index].exchange(0);  // exchange and reset
   }
   return local_c;
}
template <class CountersClass, class CounterType, typename T = uint64_t>
T thr_aggr_max(std::vector<CountersClass*>& counters, CounterType CountersClass::*c)
{
   T local_c = 0;
   for (auto& counterInstance : counters) {
      local_c = std::max(local_c, (counterInstance->*c).exchange(0));  // exchange and reset
   }
   return local_c;
}

template <class CountersClass, class CounterType, typename T = uint64_t>
T thr_aggr_max(std::vector<CountersClass*>& counters, CounterType CountersClass::*c, u8 index)
{
   T local_c = 0;
   for (auto& counterInstance : counters) {
      local_c = std::max(local_c, (counterInstance->*c)[index].exchange(0));  // exchange and reset
   }
   return local_c;
}
// -------------------------------------------------------------------------------------

template <class CountersClass, class CounterType, typename T = uint64_t>
T sum(std::vector<CountersClass*>& counters, CounterType CountersClass::*c, u8 row, u8 col)
{
   T local_c = 0;
   for (auto& counterInstance : counters) {
      local_c += (counterInstance->*c)[row][col].exchange(0);  // exchange and reset
   }
   return local_c;
}
// -------------------------------------------------------------------------------------
}  // namespace threadlocal
}  // namespace utils
}  // namespace leanstore
