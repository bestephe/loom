#ifndef FIELD_CONTAINER_H_
#define FIELD_CONTAINER_H_

#include <cassert>

#include <vector>
#include <ostream>
#include <map>

enum fields_t {
  FIELD_PTR = 0,
  FIELD_DATAQ_NUM,
  FIELD_XMIT_TS,
  FIELD_TENANT,
  FIELD_TC,
  FIELD_ROOT_VT,
  FIELD_L1_VT,
  FIELD_L2_VT,
  NUM_FIELDS,     // Sentinel
};

/// FieldContainer: a map from the field name as an int
/// to the field value as an integer.
/// Two FieldContainers, can be added (unioned) together.  The second field
/// container's values take precedence.
/// FieldContainers can be used to represent both packets with named fields
/// and state with named fields as part of that state.
template <class FieldType>
class FieldContainer {
 public:
  typedef uint64_t FieldName;

  /// Constructor from map
  /* Loom: XXX(Brent): I have no clue why, but deleting fm_ causes memory access errors */
  FieldContainer(const std::map<FieldName, FieldType> & t_field_map = {}) : fm_(t_field_map) {
    for (const auto & key_pair : t_field_map) field_map_[key_pair.first] = key_pair.second;
  }

  virtual ~FieldContainer() {}

  /// Return reference to underlying member
  /* Loom: Note: No error checking at the moment! */
  //FieldType & operator() (const FieldName & field_name) { return fm_[field_name]; }
  FieldType & operator() (const FieldName & field_name) { return field_map_[field_name]; }

  /// Return const reference to underlying member
  /* Loom: Note: No error checking at the moment! */
  //const FieldType & operator() (const FieldName & field_name) const { return fm_.at(field_name); }
  const FieldType & operator() (const FieldName & field_name) const { return field_map_[field_name]; }

  /// Overload += operator to merge a FieldContainer into this
  /// as long as they have no fields in common
  FieldContainer & operator+=(const FieldContainer & fc) {
    throw std::logic_error("Function not implemented!");

    // Collapse the fc key set
    //for (const auto & key_pair : fc.field_map_) field_map_[key_pair.first] = key_pair.second;

    return *this;
  }

  /// String representation of object
  std::string str() const {
    std::string ret = "(";
    //for (const auto & key_pair : fm_) ret += key_pair.first + " : " + std::to_string(key_pair.second) + ", ";
    for (int i = 0; i < NUM_FIELDS; i++) ret += std::to_string(i) + " : " + std::to_string(field_map_[i]) + ", ";
    ret += ")";
    return ret;
  }

  /// Print to stream
  friend std::ostream & operator<< (std::ostream & out, const FieldContainer & field_container) {
    out << field_container.str();
    return out;
  }

  /// Get list of all fields
  std::vector<FieldName> field_list() const {
    throw std::logic_error("Function not implemented!");
    std::vector<FieldName> ret;
    for (const auto & key_pair : fm_) ret.emplace_back(key_pair.first);
    /* Loom: TODO: This would be improved if it checked to see if the field is *
     * valid (has been set) */
    //for (int i = 0; i < NUM_FIELDS; i++) ret.emplace_back(i);
    return ret;
  }

 private:
  /// Map from FieldName to field value.
  /* Loom: TODO: This would be improved if it checked to see if the field is *
   * valid (has been set) */
  std::map<FieldName, FieldType> fm_ = {};
  FieldType field_map_[NUM_FIELDS];
};

#endif  // FIELD_CONTAINER_H_
