#ifndef INCLUDED_AUDIOIO_WAVE_H
#define INCLUDED_AUDIOIO_WAVE_H

#include <string>
#include <inttypes.h>
#include <sys/types.h>

#include "audioio-types.h"
#include "samplebuffer.h"
#include "eca-fileio.h"

/**
 * Represents a RIFF WAVE -file (wav).
 *
 * This class currently supports only a limited set of features:
 *
 * Format 1 sample data: Pulse Code Modulation (PCM) Format
 *
 * Format 3 sample data: IEEE754 floats, range [-1, +1)

 * - multiple channels are interleaved
 *
 * - 8, 16, 24 and 32 bit data supported
 * 
 * - if more than 8 bits, least significant byte first as specified
 *   in the stantard
 */
class WAVEFILE : public AUDIO_IO_BUFFERED {

 public:

  typedef struct {
    uint16_t format;
    uint16_t channels;
    uint32_t srate;
    uint32_t byte_second;
    uint16_t align;
    uint16_t bits;
  } RF;

  typedef struct {
    uint8_t sig[4];
    uint32_t bsize;
  } RB;

  typedef struct {
    uint8_t id[4];
    uint32_t size;
    uint8_t wname[4];
  } RH;

 private:

  ECA_FILE_IO* fio_repp;

  RH riff_header_rep;
  RF riff_format_rep;

  long int data_start_position_rep;
  bool double_buffering_rep;

  /**
   * Do a info query prior to actually opening the device.
   *
   * require:
   *  !is_open()
   *
   * ensure:
   *  !is_open()
   *  fio == 0
   */
  void format_query(void) throw(SETUP_ERROR&);

  enum Format_tags {
    unknown		= (0x0000),
    pcm			= (0x0001), 
    adpcm		= (0x0002),
    ieee_float          = (0x0003),
    alaw		= (0x0006),
    mulaw		= (0x0007),
    oki_adpcm		= (0x0010),
    ima_adpcm		= (0x0011),
    digistd		= (0x0015),
    digifix		= (0x0016),
    dolby_ac2           = (0x0030),
    gsm610              = (0x0031),
    rockwell_adpcm      = (0x003b),
    rockwell_digitalk   = (0x003c),
    g721_adpcm          = (0x0040),
    g728_celp           = (0x0041),
    mpeg                = (0x0050),
    mpeglayer3          = (0x0055),
    g726_adpcm          = (0x0064),
    g722_adpcm          = (0x0065)
  };
    
 public:

  virtual string name(void) const { return("RIFF wave file"); }
  virtual bool locked_audio_format(void) const { return(true); }

  virtual void open(void) throw(SETUP_ERROR&);
  virtual void close(void);

  virtual long int read_samples(void* target_buffer, long int samples);
  virtual void write_samples(void* target_buffer, long int samples);

  virtual bool finished(void) const;
  virtual void seek_position(void);

  WAVEFILE (const string& name = "", bool double_buffering = false);
  ~WAVEFILE(void);
  WAVEFILE* clone(void) { cerr << "Not implemented!" << endl; return this; }
  WAVEFILE* new_expr(void) { return new WAVEFILE(); }

 private:

  WAVEFILE(const WAVEFILE& x) { cerr << "WAVE construct"; }
  WAVEFILE& operator=(const WAVEFILE& x) {  return(*this); }

  void update(void);        
  void set_length_in_bytes(void);
  void read_riff_header (void) throw(SETUP_ERROR&);
  bool next_riff_block(RB *t, unsigned long int *offtmp);
  void read_riff_fmt(void) throw(SETUP_ERROR&);
  void write_riff_header (void) throw(SETUP_ERROR&);
  void write_riff_fmt(void);
  void write_riff_datablock(void);
  void update_riff_datablock(void);
  void find_riff_datablock (void) throw(SETUP_ERROR&);
  signed long int find_block(const char* fblock);
};

#endif
