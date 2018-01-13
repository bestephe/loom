#ifndef PACKET_H_
#define PACKET_H_

#include <set>
#include <ostream>
#include <random>

#include "field_container.h"

/// Wrapper around FieldContainer representing a Packet
/// including a Boolean denoting whether it's initialized or not.
class Packet {
 public:
  typedef std::string FieldName;
  /* Loom: TODO: Using an unsigned field makes more sense to me and avoids
   * unexpected problems with large numbers (timestamps.) */
  typedef uint64_t FieldType;
  //typedef int FieldType;

  /// Packet constructor
  Packet(const FieldContainer<FieldType> & t_field_container)
    : bubble_(false),
      packet_(t_field_container) {}
  Packet() {}

  /// Check if we have a bubble, to bypass packet processing
  bool is_bubble() const { return bubble_; }

  /// Reference to underlying field
  FieldType & operator() (const FieldName & field_name) { return packet_(field_name); }

  /// Overload += operator
  Packet & operator+=(const Packet & t_packet) {
    assert(not t_packet.is_bubble());
    if (this->bubble_) {
      this->bubble_ = false;
      this->packet_ = t_packet.packet_;
      return *this;
    } else {
      assert(not t_packet.bubble_ and not this->bubble_);
      this->packet_ += t_packet.packet_;
      return *this;
    }
  }

  /// Print to stream
  friend std::ostream & operator<< (std::ostream & out, const Packet & t_packet) {
    if (t_packet.bubble_) out << "Bubble \n";
    else out << t_packet.packet_ << "\n";
    return out;
  }

 private:
  /// Is this a bubble? i.e. no packet
  bool bubble_ = true;

  /// Underlying FieldContainer managed by Packet
  FieldContainer<FieldType> packet_ = FieldContainer<FieldType>();
};

/// Typedef for an std::set<std::string> representing a set of packet fields
typedef std::set<std::string> PacketFieldSet;

#endif  // PACKET_H_
