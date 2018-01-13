#ifndef CALENDAR_QUEUE_H_
#define CALENDAR_QUEUE_H_

#include <iostream>

#include "pifo.h"

/// Calendar queue abstraction to determine
/// absolute time of transmission of packets
template <typename ElementType, typename PriorityType>
class CalendarQueue {
 public:
  /// Enqueue method
  void enq(const ElementType & element, const PriorityType & prio,
           const uint32_t & tick) {
    // Don't push in a packet that was due in the past.
    assert_exception(prio >= tick);
    pifo_.push(element, prio);
  }

  /// Dequeue method
  Optional<ElementType> deq(const uint32_t & tick) {
    // Get top of pifo
    auto top_prio = pifo_.top_prio();

    if (top_prio.initialized() and top_prio.get() <= tick) {
      // If top element's tick is less than current time,
      // assert that the top element and the current time match up
      assert_exception(top_prio.get() == tick);
      return pifo_.pop();
    } else {
      // Otherwise, return nothing
      std::cout << "Returning nothing \n";
      return Optional<ElementType>();
    }
  }

  /// Print method / stream insertion operator
  friend std::ostream & operator<<(std::ostream & out,
                                   const CalendarQueue & calendar_queue) {
    out << calendar_queue.pifo_;
    return out;
  }

 private:
  /// Underlying PIFO
  PIFO<ElementType, PriorityType> pifo_ = {};
};

#endif  // CALENDAR_QUEUE_H_
