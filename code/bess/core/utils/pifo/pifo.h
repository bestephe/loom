#ifndef PIFO_H_
#define PIFO_H_

#include <ostream>
#include <iostream>
#include <queue>

#include "optional.h"

/// Helper class for an element that can be "pushed"
/// into a PIFO i.e. a class that pairs an ElementType element
/// with a PriorityType priority for that element in the PIFO
template <typename ElementType, typename PriorityType>
struct PushableElement {
 public:
  /// Constructor for PushableElement
  PushableElement(const ElementType & t_element, const PriorityType & t_prio)
    : element_(t_element),
      prio_(t_prio) {}

  /// Print method, overload stream insertion operator
  friend std::ostream & operator<<(std::ostream & out, const PushableElement & push_element) {
    out << "(ele: " << push_element.element_ << ", prio: " << push_element.prio_ << ")";
    return out;
  }

  /// Override the less-than operator for std::priority_queue to work
  /// N.B. higher priorities have lower absolute values, which is why the comparison is inverted.
  /// Put differently, we have a min-heap, not a max heap
  bool operator<(const PushableElement<ElementType, PriorityType> & other) const { return prio_ >= other.prio_; }

  /// Element itself
  ElementType element_ = ElementType();

  /// Element's priority in PIFO
  PriorityType prio_   = PriorityType();
};

template <typename ElementType, typename PriorityType>
class PIFO {
 static_assert(std::is_integral<PriorityType>::value, "PriorityType must be an integral type for PIFO");
 public:
  /// Default constructor
  PIFO() : queue_(std::priority_queue<PushableElement<ElementType, PriorityType>>()) {}

  /// Push element of ElementType into PIFO using prio of PriorityType as its priority
  void push(const ElementType & element, const PriorityType & prio) {
    PushableElement<ElementType, PriorityType> element_with_prio(element, prio);
    queue_.push(element_with_prio);
  }

  /// Pop and return element from the top of the PIFO
  Optional<ElementType> pop(void) {
    if (not queue_.empty()) {
      auto top_element = queue_.top();
      queue_.pop();
      return Optional<ElementType>(top_element.element_);
    } else {
      return Optional<ElementType>();
    }
  }

  /// Get the top-most priority from the PIFO, but don't pop it
  Optional<PriorityType> top_prio(void) const {
    if (not queue_.empty()) {
      return Optional<PriorityType>(queue_.top().prio_);
    } else {
      return Optional<PriorityType>();
    }
  }

  /// print queue contents
  friend std::ostream & operator<<(std::ostream & out, const PIFO & pifo) {
    // Copy priority_queue and then iterate over it
    // The copy is required because there is no way to iterate over a std::priority_queue
    // without dequeueing its elements in the process
    auto shadow_copy = pifo.queue_;
    out << "HEAD ";
    while (not shadow_copy.empty()) {
      out << shadow_copy.top();
      shadow_copy.pop();
    }
    out << " TAIL";
    out << std::endl;
    return out;
  }

 private:
  /// Underlying priority_queue of PushableElement
  std::priority_queue<PushableElement<ElementType, PriorityType>> queue_;
};

#endif  // PIFO_H_
