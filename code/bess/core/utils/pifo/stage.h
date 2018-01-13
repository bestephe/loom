#ifndef STAGE_H_
#define STAGE_H_

#include <tuple>
#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

#include "packet.h"
#include "atom.h"

/// The core logic that captures the packet processing functionality
/// of a stage. The semantics of a stage are parallel execution
/// of all the underlying atoms. All state for a physical stage is
/// encapsulated within these atoms.
/// This class also passes data between the input of this stage
/// and the output from this stage i.e. between
/// the read and write pipeline registers on either side of the stage's
/// combinational circuit. (i.e. input and output from Stage::tick())
class Stage {
 public:
  /// Constructor for Stage that takes an std::initializer_list
  Stage(const std::initializer_list<Atom> & t_atom_list) : Stage(std::vector<Atom>(t_atom_list)) {};

  /// Constructor for Stage that takes an Atom vector
  Stage(const std::vector<Atom> & t_atoms) : atoms_(t_atoms) {};

  /// Tick this stage by calling all atoms
  /// and combining their outputs.
  Packet tick(const Packet & input) {
    if (input.is_bubble()) {
      return Packet();
    } else {
      Packet ret;
      /// These functions can be executed in any order
      /// A shuffle emulates this non determinisim.
      std::random_shuffle(atoms_.begin(), atoms_.end());
      for (auto & atom : atoms_) {
        Packet tmp = input;
        atom(tmp);
        ret += tmp;
      }
      return ret;
    }
  }

 private:
  /// Atom vector, to be executed in parallel
  std::vector<Atom> atoms_;
};

#endif  // STAGE_H_
