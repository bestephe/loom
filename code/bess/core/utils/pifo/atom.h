#ifndef ATOM_H_
#define ATOM_H_

#include <functional>

#include "packet.h"

/// Convenience typedef for state scalar
typedef FieldContainer<int> StateScalar;

/// Convenience typedef for state array
typedef FieldContainer<std::vector<int>> StateArray;

/// A Function object that represents an atomic unit of execution
/// i.e. something that the hardware can finish before the next packet shows up
/// (This includes updates to any underlying hidden state.)
/// The Atom encapsulates a function that captures the functionality of this unit
/// and state that can be mutated between calls to this unit.
class Atom {
 public:
  /// Convenience typedef for a function that takes a packet and returns a
  /// new one. Represents a sequential block of code that executes within a stage.
  /// Could also modify state in the process.
  typedef std::function<void(Packet &, StateScalar &, StateArray &)> SequentialFunction;

  /// Constructor to Atom takes a SequentialFunction object and an initial value of state
  Atom(const SequentialFunction & t_sequential_function, const StateScalar & t_state_scalar, const StateArray & t_state_array)
    : sequential_function_(t_sequential_function), state_scalar_(t_state_scalar), state_array_(t_state_array) {}

  /// Overload function call operator
  void operator() (Packet & input) {
    assert(not input.is_bubble());
    sequential_function_(input, state_scalar_, state_array_);
  }

 private:
  /// Underlying sequential function that implements the atomic action
  SequentialFunction sequential_function_;

  /// Hidden State that is used to implement the atomic action
  StateScalar state_scalar_;

  /// Hidden StateArray
  StateArray  state_array_;
};

#endif  // ATOM_H_
