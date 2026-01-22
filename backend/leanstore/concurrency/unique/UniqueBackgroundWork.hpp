#include <array>
#include <functional>
#include "Units.hpp"

namespace mean
{
struct UniqueBackgroundWork {
   // what are background threads/tasks
   // - they should work as normal tasks
   // - stored in a hashmap (for faster access)
   // - never blocking
   // - they should update a value of how much work they have done after each invocation

   // what are the meta information that is needed in order to make them work
   // - last_run: timestamp                        (should store when the task was run the last time)
   // - avg_work: int                              (so this measures the average work metric of this task)
   // - const max_work: int                        (so this measures what the max work is for this task)
   // - runtime: { last[10]: int, avg: int, var: int, min: int, max: int } (basic information about how long this task usually runs)
   // - current_frequency: int                     (this is the current calculated EWMA (exponentially weighted moving average))

   // what is the lifecycle of these tasks:
   // - these tasks will be scheduled by a timing wheel (1ms total : 10us slices)
   // - the tasks will be placed adaptively in that timing wheel (without any help of the developer (-> maybe this will change but for now this is the
   // way))
   // - but step by step:
   // 1. we create a background task and assign initial values for the current_frequency and max_work
   // 2. we need to define a work function (or the task needs to know that it has a set method function (in the jumpmu (at this point it is the TLS of
   // my framework) maybe))
   // 3. we place the work in a array (with 8 entries for now -> easier to implement)

   // - probably we need to make this token based to distribute the stuff over a longer period of time (-> eg for each microsecond of )
   // - whenever we reschedule (are in the loop) we run the timer wheel and update it (OR over all values)
   // 0. if there are any tasks in the prio queue we run them
   // 1. knapsack like (fifo) we run the biggest subset of task that we are allowed to run (to achieve a latency bound)
   // -> the other task will be put in a priority queue
   // 2. after running shortly evaluate the runtimes (every X runs (X = 5/10/50) idk yet) and get new frequency
   // -> could be more realtime and just calculate the next one and i guess this gets the trend better

   struct background_meta_t {
      uint64_t timestamp;
      const uint64_t max_work;
      struct runtime_avg_t {
         uint8_t counter = 0;
         std::array<uint64_t, 10> last;
         uint64_t avg = 0;

         int operator+=(uint64_t runtime)
         {
            // 1. Store the value first so we don't drop the 11th sample
            // Use modulo to wrap around safely (0-9)
            last[counter % 10] = runtime;
            counter++;

            // 2. Check if we have filled the buffer
            if (counter >= 10) {
               uint64_t s = 0;
               for (auto& i : last) {
                  s += i;
               }
               avg = s / 10;

               // 3. OPTIONAL: Reset counter if you want "batches" of 10
               // OR keep it growing and use circular buffer logic above.
               // Based on your original code's intent (batching), reset here:
               counter = 0;
            }
            return avg;
         }

         operator uint64_t() const { return avg; }
      } runtime_avg;
      uint16_t cfrequency;

      background_meta_t(uint64_t max_value) : max_work(max_value) {};
   } meta;

   std::function<uint64_t()> background_fn;

   UniqueBackgroundWork(std::function<uint64_t()> bg_fun, uint64_t max_work) : meta(max_work), background_fn(std::move(bg_fun))
   {
      meta.cfrequency = 500;  // ADD THIS - initialize frequency
   };
};
}  // namespace mean