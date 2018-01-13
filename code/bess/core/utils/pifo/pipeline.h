#ifndef PIPELINE_H_
#define PIPELINE_H_

#include <iostream>
#include <algorithm>
#include <vector>

#include "packet_latches.h"
#include "stage.h"

/// A Pipeline class that repesents a switch pipeline made up of several
/// stages that pass data from one stage to the next.
class Pipeline {
 public:
  /// Pipeline constructor from initializer list
  Pipeline(const std::initializer_list<Stage> & t_stages_list) : Pipeline(std::vector<Stage>(t_stages_list)) {}

  /// Pipeline constructor
  Pipeline(const std::vector<Stage> & t_stages)
    : stages_(t_stages),
      packet_latches_(std::vector<PacketLatches>(stages_.size() >= 1 ? stages_.size() - 1 : 0)) {}

  /// Tick the pipeline synchronously with input from the outside
  Packet tick(const Packet & packet) {
    assert(not stages_.empty());

    /// If there's only one pipeline stage, there are no latches, directly return
    if (stages_.size() == 1) {
      assert(packet_latches_.empty());
      return stages_.front().tick(packet);
    } else {
      /// Feed input to the first stage of the pipeline
      packet_latches_.front().write_half() = stages_.front().tick(packet);

      /// Execute stages 1 through n - 2
      for (uint32_t i = 1; i < stages_.size() - 1; i++) packet_latches_.at(i).write_half() = stages_.at(i).tick(packet_latches_.at(i - 1).read_half());

      /// Execute last stage
      auto ret = stages_.back().tick(packet_latches_.back().read_half());

      /// Swap read and write halves of packet latches akin to double buffering
      for (auto & packet_latch : packet_latches_) packet_latch.swap();

      return ret;
    }
  }

 private:
  /// All stages that are part of the pipeline
  std::vector<Stage> stages_;

  /// All latches that are part of the pipeline
  std::vector<PacketLatches> packet_latches_;
};

#endif  // PIPELINE_H_
