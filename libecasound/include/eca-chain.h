#ifndef _CHAIN_H
#define _CHAIN_H

#include <string>
#include <vector>

#include "eca-debug.h"
#include "audioio.h"
#include "samplebuffer.h"
#include "eca-chainop.h"
#include "generic-controller.h"

#include "eca-chainop-map.h"
#include "eca-controller-map.h"

class CHAIN : public ECA_CONTROLLER_MAP {

  friend class ECA_PROCESSOR;
  friend class ECA_SESSION;
  friend class ECA_AUDIO_OBJECTS;
  friend class ECA_CONTROLLER_OBJECTS;
  friend void *mthread_process_chains(void* params);

 private:

  bool initialized_rep;

  string chainname;

  bool muted;
  bool sfx;

  int in_channels_rep;
  int out_channels_rep;

  vector<CHAIN_OPERATOR*> chainops;
  vector<GENERIC_CONTROLLER*> gcontrollers;

  CHAIN_OPERATOR* selected_chainop;
  int selected_chainop_number;

  vector<CHAIN_OPERATOR*>::const_iterator chainop_citer;

  AUDIO_IO* input_id;
  AUDIO_IO* output_id;

  SAMPLE_BUFFER audioslot;
 
 public:

  bool is_initialized(void) const { return(initialized_rep); }
  bool is_muted(void) const { return(muted); }
  bool is_processing(void) const { return(sfx); }

  void toggle_muting(bool v) { muted = v; }
  void toggle_processing(bool v) { sfx = v; }

  string name(void) const { return(chainname); }
  void name(const string& c) { chainname = c; }

  /**
   * Whether chain is in a valid state (= ready for processing)?
   */
  bool is_valid(void) const;

  /**
   * Connect input to chain
   */
  void connect_input(AUDIO_IO* input);

  /**
   * Disconnect input
   */
  void disconnect_input(void) { input_id = 0; initialized_rep = false; }

  /**
   * Connect output to chain
   */
  void connect_output(AUDIO_IO* output);

  /**
   * Disconnect output
   */
  void disconnect_output(void) { output_id = 0; initialized_rep = false; }

  /**
   * Clear chain (removes all chain operators and controllers)
   */
  void clear(void);

  /**
   * Add chain operator to the end of the chain
   *
   * require:
   *  chainop != 0
   *
   * ensure:
   *  selected_chain_operator() == number_of_chain_operators()
   *  is_processing()
   */
  void add_chain_operator(CHAIN_OPERATOR* chainop);

  /**
   * Remove selected chain operator
   *
   * require:
   *  selected_chain_operator() <= number_of_chain_operators();
   *  selected_chain_operator() > 0
   *
   * ensure:
   *  (chainsops.size() == 0 && is_processing()) ||
   *  (chainsops.size() != 0 && !is_processing())
   */
  void remove_chain_operator(void);

  /**
   * Set parameter value (selected chain operator) 
   *
   * @param index parameter number
   * @param value new value
   *
   * require:
   *  selected_chainop_number > 0 && selected_chainop_number <= number_of_chain_operators()
   *  index > 0
   */
  void set_parameter(int index, DYNAMIC_PARAMETERS::parameter_type value);

  /**
   * Get parameter value (selected chain operator) 
   *
   * @param index parameter number
   *
   * require:
   *  index > 0 &&
   *  selected_chain_operator() != ""
   */
  DYNAMIC_PARAMETERS::parameter_type get_parameter(int index) const;

  /**
   * Add a generic controller and assign it to selected chain operator
   *
   * require:
   *  csrc != 0 && selected_chain_operator() != 0
   */
  void add_controller(GENERIC_CONTROLLER* gcontroller);

  /**
   * Select chain operator
   *
   * require:
   *  index > 0
   *
   * ensure:
   *  index == select_chain_operator()
   */
  void select_chain_operator(int index);

  /**
   * Index of selected chain operator
   *
   * ensure:
   */
  int selected_chain_operator(void) const { return(selected_chainop_number); }

  int number_of_chain_operators(void) const { return(chainops.size()); }

  /**
   * Prepare chain for processing.
   *
   * require:
   *  input_id != 0 &&
   *  output_id != 0);
   *
   * ensure:
   *  is_initialized() == true
   */
  void init(void);

  /**
   * Process chain data with all chain operators.
   *
   * require:
   *  is_initialized() == true
   */
  void process(void);

  /**
   * Calculate/fetch new values for all controllers.
   */
  void controller_update(void);

  /**
   * Re-initializes all effect parameters.
   */
  void refresh_parameters(void);

  /**
   * Convert chain to a formatted string.
   */
  string to_string(void) const;
  string chain_operator_to_string(CHAIN_OPERATOR* chainop) const;
  string controller_to_string(GENERIC_CONTROLLER* gctrl) const;

  CHAIN (int bsize, int channels);
  virtual ~CHAIN (void);
};

#endif
