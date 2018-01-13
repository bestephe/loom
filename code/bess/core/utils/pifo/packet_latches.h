#ifndef PACKET_LATCHES_H_
#define PACKET_LATCHES_H_

#include <algorithm>

#include "field_container.h"
#include "packet.h"

/// Two Packets representing
/// the read and write halves of a pipeline register
class PacketLatches {
 public:
  /// Reference to read half
  auto & read_half() const { return read_ ; }

  /// Reference to write half
  auto & write_half() { return write_; }

  /// Swap halves
  void swap() { std::swap(read_, write_); }

 private:
  Packet read_  = {};
  Packet write_ = {};
};

#endif  // PACKET_LATCHES_H_
