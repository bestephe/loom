#ifndef PRIORITY_QUEUE_H_
#define PRIORITY_QUEUE_H_

#include "pifo.h"
#include "optional.h"

/// The priority queue abstraction to determine
/// the order of transmission of packets
template <typename ElementType, typename PriorityType>
class PriorityQueue {
 public:
  /// Enqueue method
  void enq(const ElementType & element, const PriorityType & prio,
           const uint32_t & tick __attribute__((unused))) {
    pifo_.push(element, prio);
  }

  /// Dequeue method
  Optional<ElementType> deq(const uint32_t & tick __attribute__((unused))) {
    return pifo_.pop();
  }

  /// Print method / stream insertion operator
  friend std::ostream & operator<<(std::ostream & out,
                                   const PriorityQueue & priority_queue) {
    out << priority_queue.pifo_;
    return out;
  }

 private:
  /// Underlying PIFO
  PIFO<ElementType, PriorityType> pifo_ = {};
};

#endif  // PRIORITY_QUEUE_H_
