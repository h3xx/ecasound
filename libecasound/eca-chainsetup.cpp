// ------------------------------------------------------------------------
// eca-chainsetup.cpp: Class representing an ecasound chainsetup object.
// Copyright (C) 1999-2002 Kai Vehmanen (kai.vehmanen@wakkanet.fi)
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
// ------------------------------------------------------------------------

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string>
#include <cstring>
#include <algorithm> /* find() */
#include <fstream>
#include <vector>
#include <list>
#include <iostream>

#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h> /* for mlockall() */
#endif

#include <kvu_dbc.h>
#include <kvu_message_item.h>
#include <kvu_numtostr.h>
#include <kvu_rtcaps.h>
#include <kvu_utils.h>

#include "eca-resources.h"
#include "eca-session.h"

#include "generic-controller.h"
#include "eca-chainop.h"
#include "eca-chain.h"

#include "audioio.h"
#include "audioio-manager.h"
#include "audioio-device.h"
#include "audioio-buffered.h"
#include "audioio-loop.h"
#include "audioio-null.h"

#include "eca-engine-driver.h"
#include "eca-object-factory.h"

#include "midiio.h"
#include "midi-client.h"

#include "eca-object-factory.h"
#include "eca-chainsetup-position.h"
#include "sample-specs.h"

#include "eca-error.h"
#include "eca-logger.h"

#include "eca-chainsetup.h"
#include "eca-chainsetup_impl.h"

using std::cerr;
using std::endl;

const string ECA_CHAINSETUP::default_audio_format_const = "s16_le,2,44100,i";
const string ECA_CHAINSETUP::default_bmode_nonrt_const = "1024,true,50,true,100000,true";
const string ECA_CHAINSETUP::default_bmode_rt_const = "1024,true,50,true,100000,true";
const string ECA_CHAINSETUP::default_bmode_rtlowlatency_const = "256,true,50,true,100000,false";

/**
 * Construct from a vector of options.
 * 
 * If any invalid options are passed us argument, 
 * interpret_result() will be 'true', and 
 * interpret_result_verbose() contains more detailed 
 * error description.
 */
ECA_CHAINSETUP::ECA_CHAINSETUP(const vector<string>& opts) 
  : cparser_rep(this) 
{

  ECA_LOG_MSG(ECA_LOGGER::subsystems, "Chainsetup created (cmdline)");

  impl_repp = new ECA_CHAINSETUP_impl;

  // FIXME: set default audio format here!

  setup_name_rep = "command-line-setup";
  setup_filename_rep = "";

  set_defaults();

  vector<string> options (opts);
  cparser_rep.preprocess_options(options);
  interpret_options(options);
  add_default_output();
}

/**
 * Constructs an empty chainsetup.
 *
 * @post buffersize != 0
 */
ECA_CHAINSETUP::ECA_CHAINSETUP(void) 
  : cparser_rep(this)
{
  ECA_LOG_MSG(ECA_LOGGER::subsystems, "Chainsetup created (empty)");

  impl_repp = new ECA_CHAINSETUP_impl;

  setup_name_rep = "";
  set_defaults();
}

/**
 * Construct from a chainsetup file.
 * 
 * If any invalid options are passed us argument, 
 * interpret_result() will be 'true', and 
 * interpret_result_verbose() contains more detailed 
 * error description.
 *
 * @post buffersize != 0
 */
ECA_CHAINSETUP::ECA_CHAINSETUP(const string& setup_file) 
  : cparser_rep(this)
{
  ECA_LOG_MSG(ECA_LOGGER::subsystems, "Chainsetup created (file)");

  impl_repp = new ECA_CHAINSETUP_impl;

  setup_name_rep = "";
  set_defaults();
  vector<string> options;
  load_from_file(setup_file, options);
  set_filename(setup_file);
  if (name() == "") set_name(setup_file);
  cparser_rep.preprocess_options(options);
  interpret_options(options);
  add_default_output();
}

/**
 * Destructor
 */
ECA_CHAINSETUP::~ECA_CHAINSETUP(void)
{ 
  ECA_LOG_MSG(ECA_LOGGER::system_objects,"ECA_CHAINSETUP destructor!");

  DBC_CHECK(is_locked() != true);
  DBC_CHECK(is_enabled() != true);

  /* delete chain objects */
  for(vector<CHAIN*>::iterator q = chains.begin(); q != chains.end(); q++) {
    ECA_LOG_MSG(ECA_LOGGER::user_objects, "(eca-chainsetup) Deleting chain \"" + (*q)->name() + "\".");
    delete *q;
    *q = 0;
  }

  /* take the garbage out (must be done before deleting input 
   * and output objects) */
  for(list<AUDIO_IO*>::iterator q = aobj_garbage_rep.begin(); q != aobj_garbage_rep.end(); q++) {
    ECA_LOG_MSG(ECA_LOGGER::user_objects, "(eca-chainsetup) Deleting garbage audio object \"" + (*q)->label() + "\".");
    delete *q;
    *q = 0;
  }

  /* delete input proxy objects; reset all pointers to null */
  for(vector<AUDIO_IO*>::iterator q = inputs.begin(); q != inputs.end(); q++) {
    if (dynamic_cast<AUDIO_IO_BUFFERED_PROXY*>(*q) != 0) {
      ECA_LOG_MSG(ECA_LOGGER::user_objects, "(eca-chainsetup) Deleting audio proxy \"" + (*q)->label() + "\".");
      delete *q;
    }
    *q = 0;
  }

  /* delete all actual audio input objects except loop devices; reset all pointers to null */
  for(vector<AUDIO_IO*>::iterator q = inputs_direct_rep.begin(); q != inputs_direct_rep.end(); q++) {
    if (dynamic_cast<LOOP_DEVICE*>(*q) == 0) { 
      ECA_LOG_MSG(ECA_LOGGER::user_objects, "(eca-chainsetup) Deleting audio object \"" + (*q)->label() + "\".");
      delete *q;
    }
    *q = 0;
  }

  /* delete output proxy objects; reset all pointers to null */
  for(vector<AUDIO_IO*>::iterator q = outputs.begin(); q != outputs.end(); q++) {
    if (dynamic_cast<AUDIO_IO_BUFFERED_PROXY*>(*q) != 0) {
      ECA_LOG_MSG(ECA_LOGGER::user_objects, "(eca-chainsetup) Deleting audio proxy \"" + (*q)->label() + "\".");
      delete *q;
    }
    *q = 0;
  }

  /* delete all actual audio output objects except loop devices; reset all pointers to null */
  for(vector<AUDIO_IO*>::iterator q = outputs_direct_rep.begin(); q != outputs_direct_rep.end(); q++) {
    // trouble with dynamic_cast with libecasoundc apps like ecalength?
    if (dynamic_cast<LOOP_DEVICE*>(*q) == 0) { 
      ECA_LOG_MSG(ECA_LOGGER::user_objects, "(eca-chainsetup) Deleting audio object \"" + (*q)->label() + "\".");
      delete *q;
      *q = 0;
    }
  }

  /* delete loop objects */
  for(map<int,LOOP_DEVICE*>::iterator q = loop_map.begin(); q != loop_map.end(); q++) {
    ECA_LOG_MSG(ECA_LOGGER::user_objects, "(eca-chainsetup) Deleting loop device \"" + q->second->label() + "\".");
    delete q->second;
    q->second = 0;
  }

  /* delete aio manager objects */
  for(vector<AUDIO_IO_MANAGER*>::iterator q = aio_managers_rep.begin(); q != aio_managers_rep.end(); q++) {
    ECA_LOG_MSG(ECA_LOGGER::user_objects, "(eca-chainsetup) Deleting audio manager \"" + (*q)->name() + "\".");
    delete *q;
    *q = 0;
  }

  delete impl_repp;
}

/**
 * Tests whether chainsetup is in a valid state.
 */
bool ECA_CHAINSETUP::is_valid(void) const
{
  // FIXME: waiting for a better implementation...
  return(is_valid_for_connection());
}

/**
 * Sets default values.
 *
 * @pre is_enabled() != true
 */
void ECA_CHAINSETUP::set_defaults(void)
{
  // --------
  DBC_REQUIRE(is_enabled() != true);
  // --------

  precise_sample_rates_rep = false;
  ignore_xruns_rep = true;

  pserver_repp = &impl_repp->pserver_rep;
  midi_server_repp = &impl_repp->midi_server_rep;
  engine_driver_repp = 0;

  if (kvu_check_for_mlockall() == true && 
      kvu_check_for_sched_fifo() == true) {
    rtcaps_rep = true;
    ECA_LOG_MSG(ECA_LOGGER::system_objects, "(eca-chainsetup) Rtcaps detected.");
  }
  else 
    rtcaps_rep = false;

  proxy_clients_rep = 0;
  is_enabled_rep = false;
  multitrack_mode_rep = false;
  multitrack_mode_override_rep = false;
  memory_locked_rep = false;
  is_locked_rep = false;
  active_chain_index_rep = 0;
  active_chainop_index_rep = 0;
  active_chainop_param_index_rep = 0;

  buffering_mode_rep = cs_bmode_auto;
  active_buffering_mode_rep = cs_bmode_none;

  set_output_openmode(AUDIO_IO::io_readwrite);

  ECA_RESOURCES ecaresources;
  if (ecaresources.has_any() != true) {
    ECA_LOG_MSG(ECA_LOGGER::info, 
		"(eca-chainsetup) Warning! Unable to read global resources. May result in incorrect behaviour.");
  }
  
  set_default_midi_device(ecaresources.resource("midi-device"));

  string aformat_temp = set_resource_helper(ecaresources,
					    "default-audio-format", 
					    ECA_CHAINSETUP::default_audio_format_const);
  cparser_rep.interpret_object_option("-f:" + aformat_temp);

  set_samples_per_second(default_audio_format().samples_per_second());

  toggle_precise_sample_rates(ecaresources.boolean_resource("default-to-precise-sample-rates"));

  impl_repp->bmode_nonrt_rep.set_all(set_resource_helper(ecaresources,
							 "bmode-defaults-nonrt",
							 ECA_CHAINSETUP::default_bmode_nonrt_const));
  impl_repp->bmode_rt_rep.set_all(set_resource_helper(ecaresources,
						      "bmode-defaults-rt",
						      ECA_CHAINSETUP::default_bmode_rt_const));
  impl_repp->bmode_rtlowlatency_rep.set_all(set_resource_helper(ecaresources,
								"bmode-defaults-rtlowlatency",
								ECA_CHAINSETUP::default_bmode_rtlowlatency_const));

  impl_repp->bmode_active_rep = impl_repp->bmode_nonrt_rep;
}

/**
 * Sets a resource value.
 *
 * Only used by ECA_CHAINSETUP::set_defaults.
 */
string ECA_CHAINSETUP::set_resource_helper(const ECA_RESOURCES& ecaresources, const string& tag, const string& alternative)
{
  if (ecaresources.has(tag) == true) {
    return(ecaresources.resource(tag));
  }
  else {
    ECA_LOG_MSG(ECA_LOGGER::system_objects,
		"(eca-chaisetup) Using hardcoded defaults for '" +
		tag + "'.");
    return(alternative);
  }
}

/**
 * Checks whether chainsetup is valid for 
 * enabling/connecting.
 */
bool ECA_CHAINSETUP::is_valid_for_connection(void) const 
{
  if (inputs.size() == 0) {
    ECA_LOG_MSG(ECA_LOGGER::system_objects, "(eca-chainsetup) No inputs in the current chainsetup.");
    return(false);
  }
  if (outputs.size() == 0) {
    ECA_LOG_MSG(ECA_LOGGER::system_objects, "(eca-chainsetup) No outputs in the current chainsetup.");
    return(false);
  }
  if (chains.size() == 0) {
    ECA_LOG_MSG(ECA_LOGGER::system_objects, "(eca-chainsetup) No chains in the current chainsetup.");
    return(false);
  }
  for(vector<CHAIN*>::const_iterator q = chains.begin(); q != chains.end();
      q++) {
    // debug info printed in CHAIN::is_valid()
    if ((*q)->is_valid() == false) return(false);
  }
  return(true);
}

void ECA_CHAINSETUP::set_buffering_mode(Buffering_mode_t value)
{
  if (value == ECA_CHAINSETUP::cs_bmode_none)
    buffering_mode_rep = ECA_CHAINSETUP::cs_bmode_auto;
  else
    buffering_mode_rep = value;
}

/**
 * Sets audio i/o manager option for manager
 * object type 'mgrname' to be 'optionstr'.
 * Previously set option string is overwritten.
 */
void ECA_CHAINSETUP::set_audio_io_manager_option(const string& mgrname, const string& optionstr)
{
  ECA_LOG_MSG(ECA_LOGGER::system_objects, 
	      "(eca-chainsetup) Set manager '" +
	      mgrname + "' option string to '" +
	      optionstr + "'.");

  aio_manager_option_map_rep[mgrname] = optionstr;
  propagate_audio_io_manager_options();
}

/**
 * Determinates the active buffering parameters based on
 * defaults, user overrides and analyzing the current 
 * chainsetup configuration. If the resulting parameters 
 * are different from current ones, a state change is
 * performed.
 */ 
void ECA_CHAINSETUP::select_active_buffering_mode(void)
{
  if (buffering_mode() == ECA_CHAINSETUP::cs_bmode_none) {
    active_buffering_mode_rep = ECA_CHAINSETUP::cs_bmode_auto;
  }
  
  if (!(multitrack_mode_override_rep == true && 
	multitrack_mode_rep != true) && 
      ((multitrack_mode_override_rep == true && 
	multitrack_mode_rep == true) ||
       (number_of_realtime_inputs() > 0 && 
	number_of_realtime_outputs() > 0 &&
	number_of_non_realtime_inputs() > 0 && 
	number_of_non_realtime_outputs() > 0 &&
	chains.size() > 1))) {
    ECA_LOG_MSG(ECA_LOGGER::info, "(eca-chainsetup) Multitrack-mode enabled.");
    multitrack_mode_rep = true;
  }
  else
    multitrack_mode_rep = false;
  
  if (buffering_mode() == ECA_CHAINSETUP::cs_bmode_auto) {

    /* initialize to 'nonrt', mt-disabled */
    active_buffering_mode_rep = ECA_CHAINSETUP::cs_bmode_nonrt;

    if (has_realtime_objects() == true) {
      /* case 1: a multitrack setup */
      if (multitrack_mode_rep == true) {
	active_buffering_mode_rep = ECA_CHAINSETUP::cs_bmode_rt;
	ECA_LOG_MSG(ECA_LOGGER::system_objects, "(eca-chainsetup) bmode-selection case-1");
      }

      /* case 2: rt-objects without priviledges for rt-scheduling */
      else if (rtcaps_rep != true) {
	toggle_raised_priority(false);
	active_buffering_mode_rep = ECA_CHAINSETUP::cs_bmode_rt;
	ECA_LOG_MSG(ECA_LOGGER::system_objects, "(eca-chainsetup) bmode-selection case-2");
      }

      /* case 3: no chain operators and "one-way rt-operation" */
      else if (number_of_chain_operators() == 0 &&
	       (number_of_realtime_inputs() == 0 || 
		number_of_realtime_outputs() == 0)) {
	active_buffering_mode_rep = ECA_CHAINSETUP::cs_bmode_rt;
	ECA_LOG_MSG(ECA_LOGGER::system_objects, "(eca-chainsetup) bmode-selection case-3");
      }

      /* case 4: default for rt-setups */
      else {
	active_buffering_mode_rep = ECA_CHAINSETUP::cs_bmode_rtlowlatency;
	ECA_LOG_MSG(ECA_LOGGER::system_objects, "(eca-chainsetup) bmode-selection case-4");
      }
    }
    else { 
      /* case 5: no rt-objects */
      active_buffering_mode_rep = ECA_CHAINSETUP::cs_bmode_nonrt;
      ECA_LOG_MSG(ECA_LOGGER::system_objects, "(eca-chainsetup) bmode-selection case-5");
    }
  }
  else {
    /* user has explicitly selected the buffering mode */
    active_buffering_mode_rep = buffering_mode();
    ECA_LOG_MSG(ECA_LOGGER::system_objects, "(eca-chainsetup) bmode-selection explicit");
  }
  
  switch(active_buffering_mode_rep) 
    {
    case ECA_CHAINSETUP::cs_bmode_nonrt: { 
      impl_repp->bmode_active_rep = impl_repp->bmode_nonrt_rep;
      ECA_LOG_MSG(ECA_LOGGER::info, 
		    "(eca-chainsetup) 'nonrt' buffering mode selected.");
      break; 
    }
    case ECA_CHAINSETUP::cs_bmode_rt: { 
      impl_repp->bmode_active_rep = impl_repp->bmode_rt_rep;
      ECA_LOG_MSG(ECA_LOGGER::info, 
		    "(eca-chainsetup) 'rt' buffering mode selected.");
      break; 
    }
    case ECA_CHAINSETUP::cs_bmode_rtlowlatency: { 
      impl_repp->bmode_active_rep = impl_repp->bmode_rtlowlatency_rep;
      ECA_LOG_MSG(ECA_LOGGER::info, 
		    "(eca-chainsetup) 'rtlowlatency' buffering mode selected.");
      break;
    }
    default: { /* error! */ }
    }

  ECA_LOG_MSG(ECA_LOGGER::system_objects,
		"(eca-chainsetup) Set buffering parameters to: \n--cut--" +
		impl_repp->bmode_active_rep.to_string() +"\n--cut--");
}

/**
 * Enable chosen active buffering mode.
 * 
 * Called only from enable().
 */
void ECA_CHAINSETUP::enable_active_buffering_mode(void)
{
  /* 1. if requested, lock all memory */
  if (raised_priority() == true) {
    lock_all_memory();
  }
  else {
    unlock_all_memory();
  }

  /* 2. if necessary, switch between different proxy and direct modes */
  if (double_buffering() == true) {
    if (has_realtime_objects() != true) {
      ECA_LOG_MSG(ECA_LOGGER::system_objects,
		    "(eca-chainsetup) No realtime objects; switching to direct mode.");
      switch_to_direct_mode();
      impl_repp->bmode_active_rep.toggle_double_buffering(false);
    }
    else if (has_nonrealtime_objects() != true) {
      ECA_LOG_MSG(ECA_LOGGER::system_objects,
		    "(eca-chainsetup) Only realtime objects; switching to direct mode.");
      switch_to_direct_mode();
      impl_repp->bmode_active_rep.toggle_double_buffering(false);
    }
    else if (proxy_clients_rep == 0) {
      ECA_LOG_MSG(ECA_LOGGER::system_objects,
		    "(eca-chainsetup) Switching to proxy mode.");
      switch_to_proxy_mode();
    }

    impl_repp->pserver_rep.set_buffer_defaults(double_buffer_size() / buffersize(), 
					       buffersize());
  }
  else {
    /* double_buffering() != true */
    if (proxy_clients_rep > 0) {
      ECA_LOG_MSG(ECA_LOGGER::system_objects,
		    "(eca-chainsetup) Switching to direct mode.");
      switch_to_direct_mode();
    }
  }

  /* 3. propagate buffersize value to all dependent objects */
  /* FIXME: create a system for tracking buffesize aware objs */
}

void ECA_CHAINSETUP::switch_to_direct_mode(void)
{
  switch_to_direct_mode_helper(&inputs, inputs_direct_rep);
  switch_to_direct_mode_helper(&outputs, outputs_direct_rep);
  // --
  DBC_ENSURE(proxy_clients_rep == 0);
  // --
}

void ECA_CHAINSETUP::switch_to_direct_mode_helper(vector<AUDIO_IO*>* objs, 
						  const vector<AUDIO_IO*>& directobjs)
{
  // --
  DBC_CHECK(objs->size() == directobjs.size());
  // --

  for(size_t n = 0; n < objs->size(); n++) {
    AUDIO_IO_BUFFERED_PROXY* pobj = dynamic_cast<AUDIO_IO_BUFFERED_PROXY*>((*objs)[n]);
    if (pobj != 0) {
      //  aobj_garbage_rep.push_back((*objs)[n]);
      delete (*objs)[n];
      (*objs)[n] = directobjs[n];
      --proxy_clients_rep;
    }
  } 
}

void ECA_CHAINSETUP::switch_to_proxy_mode(void)
{
  switch_to_proxy_mode_helper(&inputs, inputs_direct_rep);
  switch_to_proxy_mode_helper(&outputs, outputs_direct_rep);
}

void ECA_CHAINSETUP::switch_to_proxy_mode_helper(vector<AUDIO_IO*>* objs, 
						 const vector<AUDIO_IO*>& directobjs)
{
  // --
  DBC_CHECK(objs->size() == directobjs.size());
  // --

  for(size_t n = 0; n < directobjs.size(); n++) {
    (*objs)[n] = add_audio_object_helper(directobjs[n]);
  } 

  // --
  DBC_ENSURE(proxy_clients_rep > 0);
  // --
}

/**
 * Locks all memory with mlockall().
 */
void ECA_CHAINSETUP::lock_all_memory(void)
{
#ifdef HAVE_MLOCKALL
  if (::mlockall (MCL_CURRENT|MCL_FUTURE)) {
    ECA_LOG_MSG(ECA_LOGGER::info, "(eca-chainsetup) Warning! Couldn't lock all memory!");
  }
  else {
    ECA_LOG_MSG(ECA_LOGGER::system_objects, "(eca-chainsetup) Memory locked!");
    memory_locked_rep = true;
  }
#else
  ECA_LOG_MSG(ECA_LOGGER::info, "(eca-chainsetup) Memory locking not available.");
#endif
}

/**
 * Unlocks all memory with munlockall().
 */
void ECA_CHAINSETUP::unlock_all_memory(void)
{
#ifdef HAVE_MUNLOCKALL
  if (memory_locked_rep == true) {
    if (::munlockall()) {
      ECA_LOG_MSG(ECA_LOGGER::system_objects, "(eca-chainsetup) Warning! Couldn't unlock all memory!");
    }
    else 
      ECA_LOG_MSG(ECA_LOGGER::system_objects, "(eca-chainsetup) Memory unlocked!");
    memory_locked_rep = false;
  }
#else
  memory_locked_rep = false;
  ECA_LOG_MSG(ECA_LOGGER::system_objects, "(eca-chainsetup) Memory unlocking not available.");
#endif
}

/**
 * Adds a "default" chain to this chainsetup.
 *
 * @pre buffersize >= 0 && chains.size() == 0
 * @pre is_locked() != true
 *
 * @post chains.back()->name() == "default" && 
 * @post active_chainids.back() == "default"
 */
void ECA_CHAINSETUP::add_default_chain(void)
{
  // --------
  DBC_REQUIRE(buffersize() >= 0);
  DBC_REQUIRE(chains.size() == 0);
  DBC_REQUIRE(is_locked() != true);
  // --------

  add_chain_helper("default");
  selected_chainids.push_back("default");

  // --------
  DBC_ENSURE(chains.back()->name() == "default");
  DBC_ENSURE(selected_chainids.back() == "default");
  // --------  
}

/**
 * Adds new chains to this chainsetup.
 * 
 * @pre is_enabled() != true
 */
void ECA_CHAINSETUP::add_new_chains(const vector<string>& newchains)
{
  // --------
  DBC_REQUIRE(is_enabled() != true);
  // --------

  for(vector<string>::const_iterator p = newchains.begin(); p != newchains.end(); p++) {
    bool exists = false;
    for(vector<CHAIN*>::iterator q = chains.begin(); q != chains.end(); q++) {
      if (*p == (*q)->name()) exists = true;
    }
    if (exists == false) {
      add_chain_helper(*p);
    }
  }
}

void ECA_CHAINSETUP::add_chain_helper(const string& name)
{
  chains.push_back(new CHAIN());
  chains.back()->name(name);
  ECA_LOG_MSG(ECA_LOGGER::user_objects, "(eca-chainsetup) Chain \"" + name + "\" created.");
}

/**
 * Removes all selected chains from this chainsetup.
 *
 * @pre is_enabled() != true
 */
void ECA_CHAINSETUP::remove_chains(void)
{
  // --------
  DBC_REQUIRE(is_enabled() != true);
  DBC_DECLARE(size_t old_chains_size = chains.size());
  DBC_DECLARE(size_t sel_chains_size = selected_chainids.size());
  // --------

  for(vector<string>::const_iterator a = selected_chainids.begin(); a != selected_chainids.end(); a++) {
    vector<CHAIN*>::iterator q = chains.begin();
    while(q != chains.end()) {
      if (*a == (*q)->name()) {
	delete *q;
	chains.erase(q);
	break;
      }
      ++q;
    }
  }
  selected_chainids.resize(0);

  // --
  DBC_ENSURE(chains.size() == old_chains_size - sel_chains_size);
  // --
}

/**
 * Clears all selected chains. Removes all chain operators
 * and controllers.
 *
 * @pre is_locked() != true
 */
void ECA_CHAINSETUP::clear_chains(void)
{
  // --------
  DBC_REQUIRE(is_locked() != true);
  // --------

  for(vector<string>::const_iterator a = selected_chainids.begin(); a != selected_chainids.end(); a++) {
    for(vector<CHAIN*>::iterator q = chains.begin(); q != chains.end(); q++) {
      if (*a == (*q)->name()) {
	(*q)->clear();
      }
    }
  }
}

/**
 * Renames the first selected chain.
 */
void ECA_CHAINSETUP::rename_chain(const string& name)
{
  for(vector<string>::const_iterator a = selected_chainids.begin(); a != selected_chainids.end(); a++) {
    for(vector<CHAIN*>::iterator q = chains.begin(); q != chains.end(); q++) {
      if (*a == (*q)->name()) {
	(*q)->name(name);
	return;
      }
    }
  }
}

/**
 * Selects all chains present in this chainsetup.
 */
void ECA_CHAINSETUP::select_all_chains(void)
{
  vector<CHAIN*>::const_iterator p = chains.begin();
  selected_chainids.resize(0);
  while(p != chains.end()) {
    selected_chainids.push_back((*p)->name());
    ++p;
  }
}

/**
 * Returns the index number of first selected chains. If no chains 
 * are selected, returns 'last_index + 1' (chains.size()).
 */
unsigned int ECA_CHAINSETUP::first_selected_chain(void) const
{
  const vector<string>& schains = selected_chains();
  vector<string>::const_iterator o = schains.begin();
  unsigned int p = chains.size();
  while(o != schains.end()) {
    for(p = 0; p != chains.size(); p++) {
      if (chains[p]->name() == *o)
	return(p);
    }
    ++o;
  }
  return(p);
}

/**
 * Toggles chain muting of all selected chains.
 *
 * @pre is_locked() != true
 */
void ECA_CHAINSETUP::toggle_chain_muting(void)
{
  // ---
  DBC_REQUIRE(is_locked() != true);
  // ---

  for(vector<string>::const_iterator a = selected_chainids.begin(); a != selected_chainids.end(); a++) {
    for(vector<CHAIN*>::iterator q = chains.begin(); q != chains.end(); q++) {
      if (*a == (*q)->name()) {
	if ((*q)->is_muted()) 
	  (*q)->toggle_muting(false);
	else 
	  (*q)->toggle_muting(true);
      }
    }
  }
}

/**
 * Toggles chain bypass of all selected chains.
 *
 * @pre is_locked() != true
 */
void ECA_CHAINSETUP::toggle_chain_bypass(void)
{
  // ---
  DBC_REQUIRE(is_locked() != true);
  // ---

  for(vector<string>::const_iterator a = selected_chainids.begin(); a != selected_chainids.end(); a++) {
    for(vector<CHAIN*>::iterator q = chains.begin(); q != chains.end(); q++) {
      if (*a == (*q)->name()) {
	if ((*q)->is_processing()) 
	  (*q)->toggle_processing(false);
	else 
	  (*q)->toggle_processing(true);
      }
    }
  }
}

const ECA_CHAINSETUP_BUFPARAMS& ECA_CHAINSETUP::active_buffering_parameters(void) const 
{
  return(impl_repp->bmode_active_rep);
}

const ECA_CHAINSETUP_BUFPARAMS& ECA_CHAINSETUP::override_buffering_parameters(void) const 
{
  return(impl_repp->bmode_override_rep);
}

vector<string> ECA_CHAINSETUP::chain_names(void) const
{
  vector<string> result;
  vector<CHAIN*>::const_iterator p = chains.begin();
  while(p != chains.end()) {
    result.push_back((*p)->name());
    ++p;
  }
  return(result);
}

vector<string> ECA_CHAINSETUP::audio_input_names(void) const
{
  vector<string> result;
  vector<AUDIO_IO*>::const_iterator p = inputs.begin();
  while(p != inputs.end()) {
    result.push_back((*p)->label());
    ++p;
  }
  return(result);
}

vector<string> ECA_CHAINSETUP::audio_output_names(void) const
{
  vector<string> result;
  vector<AUDIO_IO*>::const_iterator p = outputs.begin();
  while(p != outputs.end()) {
    result.push_back((*p)->label());
    ++p;
  }
  return(result);
}

vector<string> ECA_CHAINSETUP::get_attached_chains_to_input(AUDIO_IO* aiod) const 
{ 
  vector<string> res;
  
  vector<CHAIN*>::const_iterator q = chains.begin();
  while(q != chains.end()) {
    if (aiod == inputs[(*q)->connected_input()]) {
      res.push_back((*q)->name());
    }
    ++q;
  }
  
  return(res); 
}

vector<string> ECA_CHAINSETUP::get_attached_chains_to_output(AUDIO_IO* aiod) const
{ 
  vector<string> res;
  
  vector<CHAIN*>::const_iterator q = chains.begin();
  while(q != chains.end()) {
    if (aiod == outputs[(*q)->connected_output()]) {
      res.push_back((*q)->name());
    }
    ++q;
  }

  return(res); 
}

int ECA_CHAINSETUP::number_of_attached_chains_to_input(AUDIO_IO* aiod) const
{
  int count = 0;
  
  vector<CHAIN*>::const_iterator q = chains.begin();
  while(q != chains.end()) {
    if (aiod == inputs[(*q)->connected_input()]) {
      ++count;
    }
    ++q;
  }

  return(count); 
}

int ECA_CHAINSETUP::number_of_attached_chains_to_output(AUDIO_IO* aiod) const
{
  int count = 0;
  
  vector<CHAIN*>::const_iterator q = chains.begin();
  while(q != chains.end()) {
    if (aiod == outputs[(*q)->connected_output()]) {
      ++count;
    }
    ++q;
  }

  return(count); 
}

/**
 * Output object is realtime target if it is not 
 * connected to any chains with non-realtime inputs.
 * In other words all data coming to a rt target
 * output comes from realtime devices.
 */
bool ECA_CHAINSETUP::is_realtime_target_output(int output_id) const
{
  bool result = true;
  bool output_found = false;
  vector<CHAIN*>::const_iterator q = chains.begin();
  while(q != chains.end()) {
    if ((*q)->connected_output() == output_id) {
      output_found = true;
      AUDIO_IO_DEVICE* p = dynamic_cast<AUDIO_IO_DEVICE*>(inputs[(*q)->connected_input()]);
      if (p == 0) {
	result = false;
      }
    }
    ++q;
  }
  if (output_found == true && result == true) 
    ECA_LOG_MSG(ECA_LOGGER::system_objects,"(eca-chainsetup) slave output detected: " + outputs[output_id]->label());
  else
    result = false;

  return(result);
}

vector<string> ECA_CHAINSETUP::get_attached_chains_to_iodev(const string& filename) const
{
  vector<AUDIO_IO*>::size_type p;

  p = 0;
  while (p < inputs.size()) {
    if (inputs[p]->label() == filename)
      return(get_attached_chains_to_input(inputs[p]));
    ++p;
  }

  p = 0;
  while (p < outputs.size()) {
    if (outputs[p]->label() == filename)
      return(get_attached_chains_to_output(outputs[p]));
    ++p;
  }
  return(vector<string> (0));
}

/**
 * Returns number of realtime audio input objects.
 */
int ECA_CHAINSETUP::number_of_chain_operators(void) const
{
  int cops = 0;
  vector<CHAIN*>::const_iterator q = chains.begin();
  while(q != chains.end()) {
    cops += (*q)->number_of_chain_operators();
    ++q;
  }
  return(cops);
}

/**
 * Returns true if the connected chainsetup contains at least
 * one realtime audio input or output.
 */
bool ECA_CHAINSETUP::has_realtime_objects(void) const
{
  if (number_of_realtime_inputs() > 0 ||
      number_of_realtime_outputs() > 0) 
    return(true);

  return(false);
}

/**
 * Returns true if the connected chainsetup contains at least
 * one nonrealtime audio input or output.
 */
bool ECA_CHAINSETUP::has_nonrealtime_objects(void) const
{
  if (static_cast<int>(inputs_direct_rep.size() + outputs_direct_rep.size()) >
      number_of_realtime_inputs() + number_of_realtime_outputs())
    return(true);
  
  return(false);
}

/**
 * Returns a string containing currently active chainsetup
 * options and settings. Syntax is the same as used for
 * saved chainsetup files.
 */
string ECA_CHAINSETUP::options_to_string(void) const
{
  return(cparser_rep.general_options_to_string());
}

/**
 * Returns number of realtime audio input objects.
 */
int ECA_CHAINSETUP::number_of_realtime_inputs(void) const
{
  int res = 0;
  for(size_t n = 0; n < inputs_direct_rep.size(); n++) {
    AUDIO_IO_DEVICE* p = dynamic_cast<AUDIO_IO_DEVICE*>(inputs_direct_rep[n]);
    if (p != 0) res++;
  }
  return(res);
}

/**
 * Returns number of realtime audio output objects.
 */
int ECA_CHAINSETUP::number_of_realtime_outputs(void) const
{
  int res = 0;
  for(size_t n = 0; n < outputs_direct_rep.size(); n++) {
    AUDIO_IO_DEVICE* p = dynamic_cast<AUDIO_IO_DEVICE*>(outputs_direct_rep[n]);
    if (p != 0) res++;
  }
  return(res);
}

/**
 * Returns number of non-realtime audio input objects.
 */
int ECA_CHAINSETUP::number_of_non_realtime_inputs(void) const
{
  return(inputs.size() - number_of_realtime_inputs());
}

/**
 * Returns number of non-realtime audio input objects.
 */
int ECA_CHAINSETUP::number_of_non_realtime_outputs(void) const
{
  return(outputs.size() - number_of_realtime_outputs());
}

/**
 * Returns a pointer to the manager handling audio object 'aobj'.
 *
 * @return 0 if 'aobj' is not handled by any manager
 */
AUDIO_IO_MANAGER* ECA_CHAINSETUP::get_audio_object_manager(AUDIO_IO* aio) const
{
  for(vector<AUDIO_IO_MANAGER*>::const_iterator q = aio_managers_rep.begin(); q != aio_managers_rep.end(); q++) {
    if ((*q)->get_object_id(aio) != -1) {
      ECA_LOG_MSG(ECA_LOGGER::system_objects, 
		    "(eca-chainsetup) Found object manager '" +
		    (*q)->name() + 
		    "' for aio '" +
		    aio->label() + "'.");
      
      return(*q);
    }
  }
  return(0);
}

/**
 * Returns a pointer to the manager handling audio 
 * objects of type 'aobj'.
 *
 * @return 0 if 'aobj' type is not handled by any manager
 */
AUDIO_IO_MANAGER* ECA_CHAINSETUP::get_audio_object_type_manager(AUDIO_IO* aio) const
{
  for(vector<AUDIO_IO_MANAGER*>::const_iterator q = aio_managers_rep.begin(); q != aio_managers_rep.end(); q++) {
    if ((*q)->is_managed_type(aio) == true) {
      ECA_LOG_MSG(ECA_LOGGER::system_objects, 
		    "(eca-chainsetup) Found object manager '" +
		    (*q)->name() + 
		    "' for aio type '" +
		    aio->name() + "'.");
      
      return(*q);
    }
  }
  return(0);
}

/**
 * If 'amgr' implements the ECA_ENGINE_DRIVER interface, 
 * it is registered as the active driver.
 */
void ECA_CHAINSETUP::register_engine_driver(AUDIO_IO_MANAGER* amgr)
{
  ECA_ENGINE_DRIVER* driver = dynamic_cast<ECA_ENGINE_DRIVER*>(amgr);

  if (driver != 0) {
    engine_driver_repp = driver;
    ECA_LOG_MSG(ECA_LOGGER::system_objects, 
		  "(eca-chainsetup) Registered audio i/o manager '" +
		  amgr->name() +
		  "' as the current engine driver.");
  }
}

/**
 * Registers audio object to a manager. If no managers are
 * available for object's type, and it can create one,
 * a new manager is created.
 */
void ECA_CHAINSETUP::register_audio_object_to_manager(AUDIO_IO* aio)
{
  AUDIO_IO_MANAGER* mgr = get_audio_object_type_manager(aio);
  if (mgr == 0) {
    mgr = aio->create_object_manager();
    if (mgr != 0) {
      ECA_LOG_MSG(ECA_LOGGER::system_objects, 
		    "(eca-chainsetup) Creating object manager '" +
		    mgr->name() + 
		    "' for aio '" +
		    aio->name() + "'.");
      aio_managers_rep.push_back(mgr);
      propagate_audio_io_manager_options();
      mgr->register_object(aio);

      /* in case manager is also a driver */
      register_engine_driver(mgr);
    }
  }
  else {
    mgr->register_object(aio);
  }
}

/**
 * Unregisters audio object from manager.
 */
void ECA_CHAINSETUP::unregister_audio_object_from_manager(AUDIO_IO* aio)
{
  AUDIO_IO_MANAGER* mgr = get_audio_object_manager(aio);
  if (mgr != 0) {
    int id = mgr->get_object_id(aio);
    if (id != -1) {
      ECA_LOG_MSG(ECA_LOGGER::system_objects, 
		    "(eca-chainsetup) Unregistering object '" +
		    aio->name() + 
		    "' from manager '" +
		    mgr->name() + "'.");
      mgr->unregister_object(id);
    }
  }
}

/**
 * Propagates to set manager options to all existing 
 * audio i/o manager objects.
 */
void ECA_CHAINSETUP::propagate_audio_io_manager_options(void)
{
  for(vector<AUDIO_IO_MANAGER*>::const_iterator q = aio_managers_rep.begin(); q != aio_managers_rep.end(); q++) {
    if (aio_manager_option_map_rep.find((*q)->name()) != 
	aio_manager_option_map_rep.end()) {
      
      const string& optstring = aio_manager_option_map_rep[(*q)->name()];
      int numparams = (*q)->number_of_params();
      for(int n = 0; n < numparams; n++) {
	(*q)->set_parameter(n + 1, kvu_get_argument_number(n + 1, optstring));
	ECA_LOG_MSG(ECA_LOGGER::system_objects, 
		    "(eca-chainsetup) Manager '" +
		    (*q)->name() + "', " + 
		    kvu_numtostr(n + 1) + ". parameter set to '" +
		    (*q)->get_parameter(n + 1) + "'.");
      }
    }      
  }
}

/** 
 * Helper function used by add_input() and add_output().
 * 
 * All audio object creates go through this function,
 * so this is good place to do global operations that
 * apply to both inputs and outputs.
 */
AUDIO_IO* ECA_CHAINSETUP::add_audio_object_helper(AUDIO_IO* aio)
{
  AUDIO_IO* retobj = aio;
  
  AUDIO_IO_DEVICE* p = dynamic_cast<AUDIO_IO_DEVICE*>(aio);
  LOOP_DEVICE* q = dynamic_cast<LOOP_DEVICE*>(aio);
  if (p == 0 && q == 0) {
    /* not a realtime or loop device */
    retobj = new AUDIO_IO_BUFFERED_PROXY(&impl_repp->pserver_rep, aio, false);
    ++proxy_clients_rep;
  }
  return(retobj);
}

/** 
 * Helper function used by remove_audio_input() and remove_audio_output().
 */
void ECA_CHAINSETUP::remove_audio_object_helper(AUDIO_IO* aio)
{
  AUDIO_IO_BUFFERED_PROXY* p = dynamic_cast<AUDIO_IO_BUFFERED_PROXY*>(aio);
  if (p != 0) {
    /* a proxied object */
    //  aobj_garbage_rep.push_back(aio);
    delete aio;
    --proxy_clients_rep;
  }
}

/**
 * Adds a new input object and attaches it to selected chains.
 * 
 * If double-buffering is enabled (double_buffering() == true),
 * and the object in question is not a realtime object, it
 * is wrapped in a AUDIO_IO_BUFFERED_PROXY object before 
 * inserted to the chainsetup. Otherwise object is added
 * as is. 
 * 
 * Ownership of the insert object is transfered to 
 * ECA_CHAINSETUP.
 *
 * @pre aiod != 0
 * @pre chains.size() > 0 
 * @pre is_enabled() != true
 * @post inputs.size() == old(inputs.size() + 1
 */
void ECA_CHAINSETUP::add_input(AUDIO_IO* aio)
{
  // --------
  DBC_REQUIRE(aio != 0);
  DBC_REQUIRE(chains.size() > 0);
  DBC_REQUIRE(is_enabled() != true);
  DBC_DECLARE(size_t old_inputs_size = inputs.size());
  // --------

  aio->set_io_mode(AUDIO_IO::io_read);
  aio->set_audio_format(default_audio_format());
  aio->set_buffersize(buffersize());
  
  register_audio_object_to_manager(aio);
  AUDIO_IO* layerobj = add_audio_object_helper(aio);
  inputs.push_back(layerobj);
  inputs_direct_rep.push_back(aio);
  input_start_pos.push_back(0);
  attach_input_to_selected_chains(layerobj);

  // --------
  DBC_ENSURE(inputs.size() == old_inputs_size + 1);
  DBC_ENSURE(inputs.size() == inputs_direct_rep.size());
  // --------
}

/**
 * Add a new output object and attach it to selected chains.
 * 
 * If double-buffering is enabled (double_buffering() == true),
 * and the object in question is not a realtime object, it
 * is wrapped in a AUDIO_IO_BUFFERED_PROXY object before 
 * inserted to the chainsetup. Otherwise object is added
 * as is. 
 * 
 * Ownership of the insert object is transfered to 
 * ECA_CHAINSETUP.
 *
 * @pre aiod != 0
 * @pre chains.size() > 0
 * @pre is_enabled() != true
 * @post outputs.size() == outputs_direct_rep.size()
 */
void ECA_CHAINSETUP::add_output(AUDIO_IO* aio, bool truncate)
{
  // --------
  DBC_REQUIRE(aio != 0);
  DBC_REQUIRE(is_enabled() != true);
  DBC_REQUIRE(chains.size() > 0);
  DBC_DECLARE(size_t old_outputs_size = outputs.size());
  // --------

  aio->set_audio_format(default_audio_format());
  aio->set_buffersize(buffersize());
  if (truncate == true) 
    aio->set_io_mode(AUDIO_IO::io_write);
  else
    aio->set_io_mode(AUDIO_IO::io_readwrite);


  register_audio_object_to_manager(aio);
  AUDIO_IO* layerobj = add_audio_object_helper(aio);
  outputs.push_back(layerobj);
  outputs_direct_rep.push_back(aio);
  output_start_pos.push_back(0);
  attach_output_to_selected_chains(layerobj);

  // ---
  DBC_ENSURE(outputs.size() == old_outputs_size + 1);
  DBC_ENSURE(outputs.size() == outputs_direct_rep.size());
  // ---
}

/**
 * Removes the labeled audio input from this chainsetup.
 *
 * @pre is_enabled() != true
 */
void ECA_CHAINSETUP::remove_audio_input(const string& label)
{
  // ---
  DBC_REQUIRE(is_enabled() != true);
  // ---

  for(size_t n = 0; n < inputs.size(); n++) {
    if (inputs[n]->label() == label) {
      ECA_LOG_MSG(ECA_LOGGER::user_objects, "(eca-chainsetup) Removing input " + label + ".");

      remove_audio_object_helper(inputs[n]);

      vector<CHAIN*>::iterator q = chains.begin();
      while(q != chains.end()) {
	if ((*q)->connected_input() == static_cast<int>(n)) (*q)->disconnect_input();
	++q;
      }

      unregister_audio_object_from_manager(inputs_direct_rep[n]);

      delete inputs_direct_rep[n];
      inputs[n] = inputs_direct_rep[n] = new NULLFILE("null");
    }
  }

  // ---
  DBC_ENSURE(inputs.size() == inputs_direct_rep.size());
  // ---
}

/**
 * Removes the labeled audio output from this chainsetup.
 *
 * @pre is_enabled() != true
 */
void ECA_CHAINSETUP::remove_audio_output(const string& label)
{
  // --------
  DBC_REQUIRE(is_enabled() != true);
  // --------

  for(size_t n = 0; n < outputs.size(); n++) {
    if (outputs[n]->label() == label) {
      ECA_LOG_MSG(ECA_LOGGER::user_objects, "(eca-chainsetup) Removing output " + label + ".");

      remove_audio_object_helper(outputs[n]);

      vector<CHAIN*>::iterator q = chains.begin();
      while(q != chains.end()) {
	if ((*q)->connected_output() == static_cast<int>(n)) (*q)->disconnect_output();
	++q;
      }

      unregister_audio_object_from_manager(outputs_direct_rep[n]);

      delete outputs_direct_rep[n];
      outputs[n] = outputs_direct_rep[n] = new NULLFILE("null");
    }
  }

  // ---
  DBC_ENSURE(outputs.size() == outputs_direct_rep.size());
  // ---
}

/**
 * Print format and id information
 *
 * @pre aio != 0
 */
void ECA_CHAINSETUP::audio_object_info(const AUDIO_IO* aio)
{
  // --------
  DBC_REQUIRE(aio != 0);
  // --------

  string temp = "(eca-chainsetup) Audio object \"" + aio->label();
  temp += "\", mode \"";
  if (aio->io_mode() == AUDIO_IO::io_read) temp += "read";
  if (aio->io_mode() == AUDIO_IO::io_write) temp += "write";
  if (aio->io_mode() == AUDIO_IO::io_readwrite) temp += "read/write";
  temp += "\".\n";
  temp += aio->format_info();

  ECA_LOG_MSG(ECA_LOGGER::info, temp);
}


/**
 * Adds a new MIDI-device object.
 *
 * @pre mididev != 0
 * @pre is_enabled() != true
 * @post midi_devices.size() > 0
 */
void ECA_CHAINSETUP::add_midi_device(MIDI_IO* mididev)
{
  // --------
  DBC_REQUIRE(mididev != 0);
  DBC_REQUIRE(is_enabled() != true);
  // --------

  midi_devices.push_back(mididev);
  impl_repp->midi_server_rep.register_client(mididev);

  // --------
  DBC_ENSURE(midi_devices.size() > 0);
  // --------
}

/**
 * Remove an MIDI-device by the name 'mdev_name'.
 *
 * @pre is_enabled() != true
 */
void ECA_CHAINSETUP::remove_midi_device(const string& mdev_name)
{
  // --------
  DBC_REQUIRE(is_enabled() != true);
  // --------

  for(vector<MIDI_IO*>::iterator q = midi_devices.begin(); q != midi_devices.end(); q++) {
    if (mdev_name == (*q)->label()) {
      delete *q;
      midi_devices.erase(q);
      break;
    }
  }
}

const CHAIN* ECA_CHAINSETUP::get_chain_with_name(const string& name) const
{
  vector<CHAIN*>::const_iterator p = chains.begin();
  while(p != chains.end()) {
    if ((*p)->name() == name) return(*p);
    ++p;
  }
  return(0);
}

/**
 * Attaches input 'obj' to all selected chains.
 *
 * @pre is_locked() != true
 */
void ECA_CHAINSETUP::attach_input_to_selected_chains(const AUDIO_IO* obj)
{
  // --------
  DBC_REQUIRE(obj != 0);
  DBC_REQUIRE(is_locked() != true);
  // --------

  string temp;
  vector<AUDIO_IO*>::size_type c = 0;

  while (c < inputs.size()) {
    if (inputs[c] == obj) {
      for(vector<CHAIN*>::iterator q = chains.begin(); q != chains.end(); q++) {
	if ((*q)->connected_input() == static_cast<int>(c)) {
	  (*q)->disconnect_input();
	}
      }
      temp += "(eca-chainsetup) Assigning file to chains:";
      for(vector<string>::const_iterator p = selected_chainids.begin(); p!= selected_chainids.end(); p++) {
	for(vector<CHAIN*>::iterator q = chains.begin(); q != chains.end(); q++) {
	  if (*p == (*q)->name()) {
	    (*q)->connect_input(c);
	    temp += " " + *p;
	  }
	}
      }
    }
    ++c;
  }
  ECA_LOG_MSG(ECA_LOGGER::system_objects, temp);
}

/**
 * Attaches output 'obj' to all selected chains.
 *
 * @pre is_locked() != true
 */
void ECA_CHAINSETUP::attach_output_to_selected_chains(const AUDIO_IO* obj)
{
  // --------
  DBC_REQUIRE(obj != 0);
  DBC_REQUIRE(is_locked() != true);
  // --------

  string temp;
  vector<AUDIO_IO*>::size_type c = 0;
  while (c < outputs.size()) {
    if (outputs[c] == obj) {
      for(vector<CHAIN*>::iterator q = chains.begin(); q != chains.end(); q++) {
	if ((*q)->connected_output() == static_cast<int>(c)) {
	  (*q)->disconnect_output();
	}
      }
      temp += "(eca-chainsetup) Assigning file to chains:";
      for(vector<string>::const_iterator p = selected_chainids.begin(); p!= selected_chainids.end(); p++) {
	for(vector<CHAIN*>::iterator q = chains.begin(); q != chains.end(); q++) {
	  if (*p == (*q)->name()) {
	    (*q)->connect_output(static_cast<int>(c));
	    temp += " " + *p;
	  }
	}
      }
    }
    ++c;
  }
  ECA_LOG_MSG(ECA_LOGGER::system_objects, temp);
}

/**
 * Returns true if 'aobj' is a pointer to some input
 * or output object.
 */
bool ECA_CHAINSETUP::ok_audio_object(const AUDIO_IO* aobj) const
{
  if (ok_audio_object_helper(aobj, inputs) == true ||
      ok_audio_object_helper(aobj, outputs) == true ) return(true);

  return(false);
  
}

bool ECA_CHAINSETUP::ok_audio_object_helper(const AUDIO_IO* aobj,
					    const vector<AUDIO_IO*>& aobjs)
{
  for(size_t n = 0; n < aobjs.size(); n++) {
    if (aobjs[n] == aobj) return(true);
  }
  return(false);
}

void ECA_CHAINSETUP::check_object_samplerate(const AUDIO_IO* obj,
					     SAMPLE_SPECS::sample_rate_t srate) throw(ECA_ERROR&)
{
  if (obj->samples_per_second() != srate) {
    throw(ECA_ERROR("ECA-CHAINSETUP", 
		    string("All audio objects must have a common") +
		    " sampling rate; sampling rate of audio object '" +
 		    obj->label() +
		    "' differs from engine rate (" +
		    kvu_numtostr(obj->samples_per_second()) +
		    " <-> " + 
		    kvu_numtostr(srate) + 
		    "); unable to continue."));
  }
}

void ECA_CHAINSETUP::enable_audio_object_helper(AUDIO_IO* aobj) const 
{
  aobj->set_buffersize(buffersize());
  AUDIO_IO_DEVICE* dev = dynamic_cast<AUDIO_IO_DEVICE*>(aobj);
  if (dev != 0) {
    dev->toggle_max_buffers(max_buffers());
    dev->toggle_ignore_xruns(ignore_xruns());
  }
  if (aobj->is_open() == false) aobj->open();
  if (aobj->is_open() == true) {
    aobj->seek_position_in_samples(aobj->position_in_samples());
    audio_object_info(aobj);
  }
}

/**
 * Enable chainsetup. Opens all devices and reinitializes all 
 * chain operators if necessary.
 *
 * This action is performed before connecting the chainsetup
 * to a engine object (for instance ECA_ENGINE). 
 * 
 * @pre is_locked() != true
 * @post is_enabled() == true
 */
void ECA_CHAINSETUP::enable(void) throw(ECA_ERROR&)
{
  // --------
  DBC_REQUIRE(is_locked() != true);
  // --------

  try {
    if (is_enabled_rep != true) {

      /* 1. select and enable buffering parameters */
      select_active_buffering_mode();
      enable_active_buffering_mode();

      /* 2. open input devices */
      for(vector<AUDIO_IO*>::iterator q = inputs.begin(); q != inputs.end(); q++) {
	enable_audio_object_helper(*q);
	if ((*q)->is_open() != true) { 
	  throw(ECA_ERROR("ECA-CHAINSETUP", "Open failed without explicit exception!"));
	}
      }

      /* 3. make sure that all input devices have a common 
       *    sampling rate */
      SAMPLE_SPECS::sample_rate_t first_srate = 0;
      for(vector<AUDIO_IO*>::iterator q = inputs.begin(); q != inputs.end(); q++) {
	if (q == inputs.begin()) {
	  first_srate = (*q)->samples_per_second();
	}
	else {
	  check_object_samplerate(*q, first_srate);
	}
      }

      /* 4. set chainsetup sampling rate to 'first_srate'. */
      set_samples_per_second(first_srate);
   
      /* 5. open output devices */
      for(vector<AUDIO_IO*>::iterator q = outputs.begin(); q != outputs.end(); q++) {
	enable_audio_object_helper(*q);
	if ((*q)->is_open() != true) { 
	  throw(ECA_ERROR("ECA-CHAINSETUP", "Open failed without explicit exception!"));
	}
	check_object_samplerate(*q, first_srate);
      }

      /* 6. enable the MIDI server */
      if (impl_repp->midi_server_rep.is_enabled() != true &&
	  midi_devices.size() > 0) impl_repp->midi_server_rep.enable();

      /* 7. enable all MIDI-devices */
      for(vector<MIDI_IO*>::iterator q = midi_devices.begin(); q != midi_devices.end(); q++) {
	(*q)->toggle_nonblocking_mode(true);
	if ((*q)->is_open() != true) {
	  (*q)->open();
	  if ((*q)->is_open() != true) {
	    throw(ECA_ERROR("ECA-CHAINSETUP", 
			    string("Unable to open MIDI-device: ") +
			    (*q)->label() +
			    "."));
	  }
	}
      }

      /* 8. calculate chainsetup length */
      calculate_processing_length();

    }
    is_enabled_rep = true;
  }
  catch(AUDIO_IO::SETUP_ERROR& e) {
    ECA_LOG_MSG(ECA_LOGGER::system_objects, 
		"(eca-chainsetup) Connecting chainsetup failed, throwing an SETUP_ERROR exception.");
    throw(ECA_ERROR("ECA-CHAINSETUP", 
		    string("Enabling chainsetup: ")
		    + e.message()));
  }
  catch(...) { 
    ECA_LOG_MSG(ECA_LOGGER::system_objects, 
		"(eca-chainsetup) Connecting chainsetup failed, throwing a generic exception.");
    throw; 
  }

  // --------
  DBC_ENSURE(is_enabled() == true);
  // --------
}



/**
 * Disable chainsetup. Closes all devices. 
 * 
 * This action is performed before disconnecting the 
 * chainsetup from a engine object (for instance 
 * ECA_ENGINE). 
 * 
 * @pre is_locked() != true
 * @post is_enabled() != true
 */
void ECA_CHAINSETUP::disable(void)
{
  // --------
  DBC_REQUIRE(is_locked() != true);
  // --------

  if (is_enabled_rep == true) {
    ECA_LOG_MSG(ECA_LOGGER::system_objects, "Closing chainsetup \"" + name() + "\"");
    for(vector<AUDIO_IO*>::iterator q = inputs.begin(); q != inputs.end(); q++) {
      ECA_LOG_MSG(ECA_LOGGER::system_objects, "(eca-chainsetup) Closing audio device/file \"" + (*q)->label() + "\".");
      if ((*q)->is_open() == true) (*q)->close();
    }
    
    for(vector<AUDIO_IO*>::iterator q = outputs.begin(); q != outputs.end(); q++) {
      ECA_LOG_MSG(ECA_LOGGER::system_objects, "(eca-chainsetup) Closing audio device/file \"" + (*q)->label() + "\".");
      if ((*q)->is_open() == true) (*q)->close();
    }

    if (impl_repp->midi_server_rep.is_enabled() == true) impl_repp->midi_server_rep.disable();
    for(vector<MIDI_IO*>::iterator q = midi_devices.begin(); q != midi_devices.end(); q++) {
      ECA_LOG_MSG(ECA_LOGGER::system_objects, "(eca-chainsetup) Closing midi device \"" + (*q)->label() + "\".");
      if ((*q)->is_open() == true) (*q)->close();
    }

    is_enabled_rep = false;
  }

  // --------
  DBC_ENSURE(is_enabled() != true);
  // --------
}

/**
 * Updates the chainsetup processing length based on 
 * 1) requested length, 2) lengths of individual 
 * input objects, and 3) looping settings.
 */
void ECA_CHAINSETUP::calculate_processing_length(void)
{
  long int max_input_length = 0;
  for(unsigned int n = 0; n < inputs.size(); n++) {
    if (inputs[n]->length_in_samples() > max_input_length)
      max_input_length = inputs[n]->length_in_samples();
  }
  
  if (length_set() != true) {
    if (max_input_length > 0) {
      set_length_in_samples(max_input_length);
    }
  }
}

/**
 * Reimplemented from ECA_CHAINSETUP_POSITION.
 */
void ECA_CHAINSETUP::set_samples_per_second(SAMPLE_SPECS::sample_rate_t new_value)
{
  /* not necessarily a problem */
  DBC_CHECK(is_locked() != true);

  ECA_LOG_MSG(ECA_LOGGER::user_objects,
		"(eca-chainsetup) sample rate change, chainsetup " +
		name() +
		" to rate " + 
		kvu_numtostr(new_value) + ".");

  for(vector<AUDIO_IO*>::iterator q = inputs.begin(); q != inputs.end(); q++) {
    (*q)->set_samples_per_second(new_value);
  }
  
  for(vector<AUDIO_IO*>::iterator q = outputs.begin(); q != outputs.end(); q++) {
    (*q)->set_samples_per_second(new_value);
  }

  for(vector<CHAIN*>::iterator q = chains.begin(); q != chains.end(); q++) {
    (*q)->set_samples_per_second(new_value);
  }
  
  ECA_CHAINSETUP_POSITION::set_samples_per_second(new_value);
}

/**
 * Reimplemented from ECA_AUDIO_POSITION.
 */
void ECA_CHAINSETUP::seek_position(void)
{
  ECA_LOG_MSG(ECA_LOGGER::user_objects,
		"(eca-chainsetup) seek position, chainsetup '" +
		name() +
		"' to pos in sec " + 
		kvu_numtostr(position_in_seconds()) + ".");

  if (double_buffering() == true) pserver_repp->flush();

  for(vector<AUDIO_IO*>::iterator q = inputs.begin(); q != inputs.end(); q++) {
    (*q)->seek_position_in_samples(position_in_samples());
  }
  
  for(vector<AUDIO_IO*>::iterator q = outputs.begin(); q != outputs.end(); q++) {
    (*q)->seek_position_in_samples(position_in_samples());
  }

  for(vector<CHAIN*>::iterator q = chains.begin(); q != chains.end(); q++) {
    (*q)->seek_position_in_samples(position_in_samples());
  }
}

/**
 * Interprets one option. This is the most generic variant of
 * the interpretation routines; both global and object specific
 * options are handled.
 *
 * @pre argu.size() > 0
 * @pre argu[0] == '-'
 * @pre is_enabled() != true
 * 
 * @post (option succesfully interpreted && interpret_result() ==  true) ||
 *       (unknown or invalid option && interpret_result() != true)
 */
void ECA_CHAINSETUP::interpret_option (const string& arg)
{
  // --------
  DBC_REQUIRE(is_enabled() != true);
  // --------

  cparser_rep.interpret_option(arg);
}

/**
 * Interprets one option. All non-global options are ignored. Global
 * options can be interpreted multiple times and in any order.
 *
 * @pre argu.size() > 0
 * @pre argu[0] == '-'
 * @pre is_enabled() != true
 * @post (option succesfully interpreted && interpretation_result() ==  true) ||
 *       (unknown or invalid option && interpretation_result() == false)
 */
void ECA_CHAINSETUP::interpret_global_option (const string& arg)
{
  // --------
  DBC_REQUIRE(is_enabled() != true);
  // --------

  cparser_rep.interpret_global_option(arg);
}

/**
 * Interprets one option. All options not directly related to 
 * ecasound objects are ignored.
 *
 * @pre argu.size() > 0
 * @pre argu[0] == '-'
 * @pre is_enabled() != true
 * 
 * @post (option succesfully interpreted && interpretation_result() ==  true) ||
 *       (unknown or invalid option && interpretation_result() == false)
 */
void ECA_CHAINSETUP::interpret_object_option (const string& arg)
{
  // --------
  // FIXME: this requirement is broken by eca-control.h (for 
  //        adding effects on-the-fly, just stopping the engine)
  DBC_REQUIRE(is_enabled() != true);
  // --------

  cparser_rep.interpret_object_option(arg);
}

/**
 * Interpret a vector of options.
 *
 * @pre is_enabled() != true
 */
void ECA_CHAINSETUP::interpret_options(vector<string>& opts)
{
  // --------
  DBC_REQUIRE(is_enabled() != true);
  // --------

  cparser_rep.interpret_options(opts);
}

void ECA_CHAINSETUP::set_buffersize(long int value)
{
  ECA_LOG_MSG(ECA_LOGGER::system_objects, "(eca-chainsetup) overriding buffersize.");
  impl_repp->bmode_override_rep.set_buffersize(value); 
}

void ECA_CHAINSETUP::toggle_raised_priority(bool value) { 
  ECA_LOG_MSG(ECA_LOGGER::system_objects, "(eca-chainsetup) overriding raised priority.");
  impl_repp->bmode_override_rep.toggle_raised_priority(value); 
}

void ECA_CHAINSETUP::set_sched_priority(int value)
{
  ECA_LOG_MSG(ECA_LOGGER::system_objects, "(eca-chainsetup) sched_priority.");
  impl_repp->bmode_override_rep.set_sched_priority(value); 
}

void ECA_CHAINSETUP::toggle_double_buffering(bool value)
{
  ECA_LOG_MSG(ECA_LOGGER::system_objects, "(eca-chainsetup) overriding doublebuffering.");
  impl_repp->bmode_override_rep.toggle_double_buffering(value); 
}

void ECA_CHAINSETUP::set_double_buffer_size(long int v)
{
  ECA_LOG_MSG(ECA_LOGGER::system_objects, "(eca-chainsetup) overriding db-size.");
  impl_repp->bmode_override_rep.set_double_buffer_size(v); 
}

void ECA_CHAINSETUP::toggle_max_buffers(bool v)
{
  ECA_LOG_MSG(ECA_LOGGER::system_objects, "(eca-chainsetup) overriding max_buffers.");
  impl_repp->bmode_override_rep.toggle_max_buffers(v); 
}

long int ECA_CHAINSETUP::buffersize(void) const
{
  if (impl_repp->bmode_override_rep.is_set_buffersize() == true)
    return(impl_repp->bmode_override_rep.buffersize());
  
  return(impl_repp->bmode_active_rep.buffersize()); 
}

bool ECA_CHAINSETUP::raised_priority(void) const
{
  if (impl_repp->bmode_override_rep.is_set_raised_priority() == true)
    return(impl_repp->bmode_override_rep.raised_priority());

  return(impl_repp->bmode_active_rep.raised_priority()); 
}

int ECA_CHAINSETUP::get_sched_priority(void) const
{
  if (impl_repp->bmode_override_rep.is_set_sched_priority() == true)
    return(impl_repp->bmode_override_rep.get_sched_priority());

  return(impl_repp->bmode_active_rep.get_sched_priority()); 
}

bool ECA_CHAINSETUP::double_buffering(void) const { 
  if (impl_repp->bmode_override_rep.is_set_double_buffering() == true)
    return(impl_repp->bmode_override_rep.double_buffering());

  return(impl_repp->bmode_active_rep.double_buffering()); 
}

long int ECA_CHAINSETUP::double_buffer_size(void) const { 
  if (impl_repp->bmode_override_rep.is_set_double_buffer_size() == true)
    return(impl_repp->bmode_override_rep.double_buffer_size());

  return(impl_repp->bmode_active_rep.double_buffer_size()); 
}

bool ECA_CHAINSETUP::max_buffers(void) const { 
  if (impl_repp->bmode_override_rep.is_set_max_buffers() == true)
    return(impl_repp->bmode_override_rep.max_buffers());

  return(impl_repp->bmode_active_rep.max_buffers()); 
}

void ECA_CHAINSETUP::set_default_audio_format(ECA_AUDIO_FORMAT& value) { 
  impl_repp->default_audio_format_rep = value; 
}

const ECA_AUDIO_FORMAT& ECA_CHAINSETUP::default_audio_format(void) const
{ 
  return(impl_repp->default_audio_format_rep); 
}

/**
 * Select controllers as targets for parameter control
 */
void ECA_CHAINSETUP::set_target_to_controller(void) {
  vector<string> schains = selected_chains();
  for(vector<string>::const_iterator a = schains.begin(); a != schains.end(); a++) {
    for(vector<CHAIN*>::iterator q = chains.begin(); q != chains.end(); q++) {
      if (*a == (*q)->name()) {
	(*q)->selected_controller_as_target();
	return;
      }
    }
  }
}

/**
 * Add general controller to selected chainop.
 *
 * @pre csrc != 0
 * @pre is_locked() != true
 * @pre selected_chains().size() == 1
 */
void ECA_CHAINSETUP::add_controller(GENERIC_CONTROLLER* csrc)
{
  // --------
  DBC_REQUIRE(csrc != 0);
  DBC_REQUIRE(is_locked() != true);
  // --------

  AUDIO_STAMP_CLIENT* p = dynamic_cast<AUDIO_STAMP_CLIENT*>(csrc->source_pointer());
  if (p != 0) {
    p->register_server(&impl_repp->stamp_server_rep);
  }

  DBC_CHECK(buffersize() != 0);
  DBC_CHECK(samples_per_second() != 0);

  vector<string> schains = selected_chains();
  for(vector<string>::const_iterator a = schains.begin(); a != schains.end(); a++) {
    for(vector<CHAIN*>::iterator q = chains.begin(); q != chains.end(); q++) {
      if (*a == (*q)->name()) {
	if ((*q)->selected_target() == 0) return;
	(*q)->add_controller(csrc);
	return;
      }
    }
  }
}

/**
 * Add chain operator to selected chain.
 *
 * @pre cotmp != 0
 * @pre is_locked() != true
 * @pre selected_chains().size() == 1
 */
void ECA_CHAINSETUP::add_chain_operator(CHAIN_OPERATOR* cotmp)
{
  // --------
  DBC_REQUIRE(cotmp != 0);
  DBC_REQUIRE(is_locked() != true);
  // --------
  
  AUDIO_STAMP* p = dynamic_cast<AUDIO_STAMP*>(cotmp);
  if (p != 0) {
    impl_repp->stamp_server_rep.register_stamp(p);
  }

  vector<string> schains = selected_chains();
  for(vector<string>::const_iterator p = schains.begin(); p != schains.end(); p++) {
    for(vector<CHAIN*>::iterator q = chains.begin(); q != chains.end(); q++) {
      if (*p == (*q)->name()) {
	ECA_LOG_MSG(ECA_LOGGER::system_objects, "Adding chainop to chain " + (*q)->name() + ".");
	(*q)->add_chain_operator(cotmp);
	(*q)->selected_chain_operator_as_target();
	return;
      }
    }
  }
}

/**
 * If chainsetup has inputs, but no outputs, a default output is
 * added.
 * 
 * @pre is_enabled() != true
 */
void ECA_CHAINSETUP::add_default_output(void)
{
  // --------
  DBC_REQUIRE(is_enabled() != true);
  // --------

  if (inputs.size() > 0 && outputs.size() == 0) {
    // No -o[:] options specified; let's use the default output
    select_all_chains();
    ECA_RESOURCES ecaresources;
    interpret_object_option(string("-o:" + ecaresources.resource("default-output")));
  }
}

/**
 * Loads chainsetup options from file.
 *
 * @pre is_enabled() != true
 */
void ECA_CHAINSETUP::load_from_file(const string& filename,
				    vector<string>& opts) const throw(ECA_ERROR&) 
{
  // --------
  DBC_REQUIRE(is_enabled() != true);
  // --------

  std::ifstream fin (filename.c_str());
  if (!fin) throw(ECA_ERROR("ECA_CHAINSETUP", "Couldn't open setup read file: \"" + filename + "\".", ECA_ERROR::retry));

  vector<string> options;
  string temp;
  while(getline(fin,temp)) {
    if (temp.size() > 0 && temp[0] == '#') {
      continue;
    }
    vector<string> words = kvu_string_to_tokens_quoted(temp);
    for(unsigned int n = 0; n < words.size(); n++) {
      ECA_LOG_MSG(ECA_LOGGER::system_objects, "(eca-chainsetup) Adding \"" + words[n] + "\" to options (loaded from \"" + filename + "\".");
      options.push_back(words[n]);
    }
  }
  fin.close();

  opts = COMMAND_LINE::combine(options);
}

void ECA_CHAINSETUP::save(void) throw(ECA_ERROR&)
{ 
  if (setup_filename_rep.empty() == true)
    setup_filename_rep = setup_name_rep + ".ecs";
  save_to_file(setup_filename_rep);
}

void ECA_CHAINSETUP::save_to_file(const string& filename) throw(ECA_ERROR&)
{
  // make sure that all overrides are processed
  select_active_buffering_mode();

  std::ofstream fout (filename.c_str());
  if (!fout) {
    cerr << "Going to throw an exception...\n";
    throw(ECA_ERROR("ECA_CHAINSETUP", "Couldn't open setup save file: \"" +
  			filename + "\".", ECA_ERROR::retry));
  }
  else {
    fout << "# ecasound chainsetup file" << endl;
    fout << endl;

    fout << "# general " << endl;
    fout << cparser_rep.general_options_to_string() << endl;
    fout << endl;

    string tmpstr = cparser_rep.midi_to_string();
    if (tmpstr.size() > 0) {
      fout << "# MIDI " << endl;
      fout << tmpstr << endl;
      fout << endl;      
    }

    fout << "# audio inputs " << endl;
    fout << cparser_rep.inputs_to_string() << endl;
    fout << endl;

    fout << "# audio outputs " << endl;
    fout << cparser_rep.outputs_to_string() << endl;
    fout << endl;

    tmpstr = cparser_rep.chains_to_string();
    if (tmpstr.size() > 0) {
      fout << "# chain operators and controllers " << endl;
      fout << tmpstr << endl;
      fout << endl;      
    }

    fout.close();
    set_filename(filename);
  }
}
