#ifndef INCLUDED_PRESET_IMPL_H
#define INCLUDED_PRESET_IMPL_H

#include <string>
#include <vector>
 
#include "eca-chainop.h"
#include "sample-specs.h"

using std::string;
using std::vector;

class AUDIO_IO;
class GENERIC_CONTROLLER;
class OPERATOR::PARAM_DESCRIPTION;

class PRESET_impl {

 public:

  friend class PRESET;

 private:

  vector<string> preset_param_names_rep;

  vector<vector<int> > slave_param_indices_rep;
  vector<vector<DYNAMIC_OBJECT<SAMPLE_SPECS::sample_type>* > > slave_param_objects_rep;

  vector<GENERIC_CONTROLLER*> gctrls_rep;
  vector<OPERATOR::PARAM_DESCRIPTION*> pardesclist_rep;

  bool parsed_rep;
  std::string parse_string_rep;
  std::string name_rep;
  std::string description_rep;

};

#endif /* INCLUDED_PRESET_IMPL_H */
