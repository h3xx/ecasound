#ifndef _AUDIOFX_TIMEBASED_H
#define _AUDIOFX_TIMEBASED_H

#include <vector>
#include <deque>
#include <string>

#include "audiofx.h"
#include "audiofx_filter.h"
#include "osc-sine.h"

typedef deque<SAMPLE_SPECS::sample_type> SINGLE_BUFFER;

/**
 * Base class for time-based effects (delays, reverbs, etc).
 */
class EFFECT_TIME_BASED : public EFFECT_BASE {

 public:

  virtual EFFECT_TIME_BASED* clone(void) = 0;
};

/** 
 * Delay effect
 */
class EFFECT_DELAY : public EFFECT_TIME_BASED {

 private:

  SAMPLE_ITERATOR_CHANNEL l,r;

  parameter_type surround;
  parameter_type dnum;
  parameter_type dtime;
  parameter_type mix;

  parameter_type laskuri;
  vector<vector<SINGLE_BUFFER> > buffer;

 public:

  string name(void) const { return("Delay"); }

  string parameter_names(void) const { return("delay-time-msec,surround-mode,number-of-delays,mix-%"); }

  parameter_type get_parameter(int param) const;
  void set_parameter(int param, parameter_type value);

  void init(SAMPLE_BUFFER* insample);
  void process(void);
  int output_channels(int i_channels) const { return(2); }

  parameter_type get_delta_in_samples(void) { return(dnum * dtime); }

  EFFECT_DELAY* clone(void)  { return new EFFECT_DELAY(*this); }
  EFFECT_DELAY (parameter_type delay_time = 0.0, int surround_mode = 0, int num_of_delays = 1, parameter_type mix_percent = 50.0);
};

/** 
 * Multi-tap delay
 */
class EFFECT_MULTITAP_DELAY : public EFFECT_TIME_BASED {

 private:

  SAMPLE_ITERATOR_INTERLEAVED i;

  parameter_type surround;
  parameter_type mix;

  long int dtime, dnum;

  vector<long int> delay_index;
  vector<vector<bool> > filled;
  vector<vector<SAMPLE_SPECS::sample_type> > buffer;

 public:

  string name(void) const { return("Multitap delay"); }
  string parameter_names(void) const { return("delay-time-msec,number-of-delays,mix-%"); }

  parameter_type get_parameter(int param) const;
  void set_parameter(int param, parameter_type value);

  void init(SAMPLE_BUFFER* insample);
  void process(void);

  EFFECT_MULTITAP_DELAY* clone(void)  { return new EFFECT_MULTITAP_DELAY(*this); }
  EFFECT_MULTITAP_DELAY (parameter_type delay_time = 0.0, int num_of_delays = 1, parameter_type mix_percent = 50.0);
};

/*
 * Transforms a mono signal to stereo using a panned delay signal.
 * Suitable delays values range from 1 to 40 milliseconds. 
 */
class EFFECT_FAKE_STEREO : public EFFECT_TIME_BASED {

  vector<deque<SAMPLE_SPECS::sample_type> > buffer;
  SAMPLE_ITERATOR_CHANNEL l,r;
  parameter_type dtime;

 public:

  string name(void) const { return("Fake stereo"); }

  string parameter_names(void) const { return("delay-time-msec"); }

  parameter_type get_parameter(int param) const;
  void set_parameter(int param, parameter_type value);

  void init(SAMPLE_BUFFER* insample);
  void process(void);
  int output_channels(int i_channels) const { return(2); }

  EFFECT_FAKE_STEREO* clone(void)  { return new EFFECT_FAKE_STEREO(*this); }
  EFFECT_FAKE_STEREO (parameter_type delay_time = 0.0);
};

/*
 * Simple reverb (based on a iir comb filter)
 */
class EFFECT_REVERB : public EFFECT_TIME_BASED {

 private:
    
  vector<deque<SAMPLE_SPECS::sample_type>  > buffer;
  SAMPLE_ITERATOR_CHANNEL l,r;

  parameter_type surround;
  parameter_type feedback;
  parameter_type dtime;

 public:

  string name(void) const { return("Reverb"); }

  string parameter_names(void) const { return("delay-time,surround-mode,feedback-%"); }

  parameter_type get_parameter(int param) const;
  void set_parameter(int param, parameter_type value);

  void init(SAMPLE_BUFFER* insample);
  void process(void);
  int output_channels(int i_channels) const { return(2); }

  parameter_type get_delta_in_samples(void) { return(dtime); }

  EFFECT_REVERB* clone(void)  { return new EFFECT_REVERB(*this); }
  EFFECT_REVERB (parameter_type delay_time = 0.0, int surround_mode = 0, parameter_type feedback_percent = 50.0);
};

/*
 * Base class for modulating delay effects
 */
class EFFECT_MODULATING_DELAY : public EFFECT_TIME_BASED {

 protected:

  vector<vector<SAMPLE_SPECS::sample_type> > buffer;
  SAMPLE_ITERATOR_CHANNELS i;
  SINE_OSCILLATOR lfo;
  parameter_type feedback, vartime;
  long int dtime;
  vector<long int> delay_index;
  vector<bool> filled;

 public:

  parameter_type get_parameter(int param) const;
  void set_parameter(int param, parameter_type value);

  void init(SAMPLE_BUFFER* insample);
};

/*
 * Flanger
 */
class EFFECT_FLANGER : public EFFECT_MODULATING_DELAY {

 public:

  string name(void) const { return("Flanger"); }
  string parameter_names(void) const { return("delay-time-msec,variance-time-samples,feedback-%,lfo-freq"); }

  void process(void);

  EFFECT_FLANGER* clone(void)  { return new EFFECT_FLANGER(*this); }
};

/*
 * Chorus
 */
class EFFECT_CHORUS : public EFFECT_MODULATING_DELAY {

 public:

  string name(void) const { return("Chorus"); }
  string parameter_names(void) const { return("delay-time-msec,variance-time-samples,feedback-%,lfo-freq"); }

  void process(void);

  EFFECT_CHORUS* clone(void)  { return new EFFECT_CHORUS(*this); }
};

/*
 * Phaser
 */
class EFFECT_PHASER : public EFFECT_MODULATING_DELAY {

 public:

  string name(void) const { return("Phaser"); }
  string parameter_names(void) const { return("delay-time-msec,variance-time-samples,feedback-%,lfo-freq"); }

  void process(void);

  EFFECT_PHASER* clone(void)  { return new EFFECT_PHASER(*this); }
};

#endif
