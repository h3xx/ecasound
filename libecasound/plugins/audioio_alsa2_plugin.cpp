// ------------------------------------------------------------------------
// audioio-alsa2-plugin.cpp: ALSA pcm-plugin input/output.
// Copyright (C) 2000 Kai Vehmanen (kaiv@wakkanet.fi)
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

#include <config.h>
#ifdef ALSALIB_050

#include <string>
#include <cstring>
#include <cstdio>
#include <dlfcn.h>  
#include <unistd.h>  

#include <kvutils/message_item.h>
#include <kvutils/kvu_numtostr.h>
#include <kvutils.h>

extern "C" {
#include <sys/asoundlib.h>
}

#include "samplebuffer.h"
#include "audioio-types.h"
#include "audioio_alsa2_plugin.h"

#include "eca-error.h"
#include "eca-debug.h"

ALSA_PCM2_PLUGIN_DEVICE::ALSA_PCM2_PLUGIN_DEVICE (int card, 
						  int device, 
						  int subdevice) {
  ecadebug->msg(ECA_DEBUG::system_objects, "(audioio-alsa2) construct");
  card_number_rep = card;
  device_number_rep = device;
  subdevice_number_rep = subdevice;
  pcm_mode_rep = SND_PCM_MODE_BLOCK;
  is_triggered = false;
  is_prepared = false;
  overruns_rep = underruns_rep = 0;
}

void ALSA_PCM2_PLUGIN_DEVICE::open(void) throw(ECA_ERROR*) {
  assert(is_open() == false);
  assert(is_triggered == false);

  ecadebug->msg(ECA_DEBUG::system_objects, "(audioio-alsa2-plugin) open");

  //  ::eca_alsa_load_dynamic_support();

  // -------------------------------------------------------------------
  // Channel initialization

  struct snd_pcm_channel_params params;
  ::memset(&params, 0, sizeof(params));

  // -------------------------------------------------------------------
  // Open devices

  int err;
  if (io_mode() == io_read) {
    pcm_channel_rep = SND_PCM_CHANNEL_CAPTURE;
    err = ::snd_pcm_open_subdevice(&audio_fd_repp, 
				   card_number_rep, 
				   device_number_rep,
				   subdevice_number_rep,
				   SND_PCM_OPEN_CAPTURE | SND_PCM_OPEN_NONBLOCK);
    
    if (err < 0) {
      throw(new ECA_ERROR("AUDIOIO-ALSA2-PLUGIN", "Unable to open ALSA-device for capture; error: " + 
			  string(snd_strerror(err))));
    }
  }    
  else if (io_mode() == io_write) {
    pcm_channel_rep = SND_PCM_CHANNEL_PLAYBACK;
    err = ::snd_pcm_open_subdevice(&audio_fd_repp, 
				   card_number_rep, 
				   device_number_rep,
				   subdevice_number_rep,
				   SND_PCM_OPEN_PLAYBACK | SND_PCM_OPEN_NONBLOCK);
    if (err < 0) {
      throw(new ECA_ERROR("AUDIOIO-ALSA2-PLUGIN", "Unable to open ALSA-device for playback; error: " +  
			  string(snd_strerror(err))));
    }
  }
  else if (io_mode() == io_readwrite) {
      throw(new ECA_ERROR("AUDIOIO-ALSA2-PLUGIN", "Simultaneous intput/output not supported."));
  }

  // -------------------------------------------------------------------
  // Sets non-blocking mode 
  ::snd_pcm_nonblock_mode(audio_fd_repp, 0);

  // -------------------------------------------------------------------
  // Fetch channel info

  ::memset(&pcm_info_rep, 0, sizeof(pcm_info_rep));
  pcm_info_rep.channel = pcm_channel_rep;
  ::snd_pcm_plugin_info(audio_fd_repp, &pcm_info_rep);

  // -------------------------------------------------------------------
  // Select audio format

  ::snd_pcm_plugin_flush(audio_fd_repp, pcm_channel_rep);
  snd_pcm_format_t pf;
  ::memset(&pf, 0, sizeof(pf));

  if (channels() > 1 &&
      (pcm_info_rep.flags & SND_PCM_CHNINFO_INTERLEAVE) != SND_PCM_CHNINFO_INTERLEAVE)
    throw(new ECA_ERROR("AUDIOIO-ALSA3", "device can't handle interleaved streams!", ECA_ERROR::stop));
  pf.interleave = 1;

  int format;
  switch(sample_format()) 
    {
    case ECA_AUDIO_FORMAT::sfmt_u8:  { format = SND_PCM_SFMT_U8; break; }
    case ECA_AUDIO_FORMAT::sfmt_s8:  { format = SND_PCM_SFMT_S8; break; }
    case ECA_AUDIO_FORMAT::sfmt_s16_le:  { format = SND_PCM_SFMT_S16_LE; break; }
    case ECA_AUDIO_FORMAT::sfmt_s16_be:  { format = SND_PCM_SFMT_S16_BE; break; }
    case ECA_AUDIO_FORMAT::sfmt_s24_le:  { format = SND_PCM_SFMT_S24_LE; break; }
    case ECA_AUDIO_FORMAT::sfmt_s24_be:  { format = SND_PCM_SFMT_S24_BE; break; }
    case ECA_AUDIO_FORMAT::sfmt_s32_le:  { format = SND_PCM_SFMT_S32_LE; break; }
    case ECA_AUDIO_FORMAT::sfmt_s32_be:  { format = SND_PCM_SFMT_S32_BE; break; }
      
    default:
      {
	throw(new ECA_ERROR("AUDIOIO-ALSA2-PLUGIN", "Error when setting audio format not supported (1)"));
      }
    }

  unsigned int format_mask = (1 << format);
  if ((pcm_info_rep.formats & format_mask) != format_mask)
    throw(new ECA_ERROR("AUDIOIO-ALSA2-PLUGIN", "Selected sample format not supported by the device!", ECA_ERROR::stop));
  pf.format = format;

  if (samples_per_second() < pcm_info_rep.min_rate ||
      samples_per_second() > pcm_info_rep.max_rate)
    throw(new ECA_ERROR("AUDIOIO-ALSA2-PLUGIN", "Sample rate " +
			kvu_numtostr(samples_per_second()) + " is out of range!", ECA_ERROR::stop));
  pf.rate = samples_per_second();

  if (channels() < pcm_info_rep.min_voices ||
      channels() > pcm_info_rep.max_voices)
    throw(new ECA_ERROR("AUDIOIO-ALSA2-PLUGIN", "Channel count " +
			kvu_numtostr(channels()) + " is out of range!", ECA_ERROR::stop));
  pf.voices = channels();

  ::memcpy(&params.format, &pf, sizeof(pf));

  params.mode = pcm_mode_rep;
  params.channel = pcm_channel_rep;
  if (params.channel == SND_PCM_CHANNEL_PLAYBACK)
    params.start_mode = SND_PCM_START_GO;
  else
    params.start_mode = SND_PCM_START_DATA;
  params.stop_mode = SND_PCM_STOP_ROLLOVER; // SND_PCM_STOP_STOP

  // -------------------------------------------------------------------
  // Set fragment size.

  if (buffersize() * frame_size() < pcm_info_rep.min_fragment_size ||
      buffersize() * frame_size() > pcm_info_rep.max_fragment_size) 
    throw(new ECA_ERROR("AUDIOIO-ALSA2-PLUGIN", "buffersize " +
			kvu_numtostr(buffersize()) + " is out of range!", ECA_ERROR::stop));
  
  params.buf.block.frag_size = buffersize() * frame_size();
  params.buf.block.frags_max = 1;
  params.buf.block.frags_min = 1;

  // -------------------------------------------------------------------
  // Channel params

  err = ::snd_pcm_plugin_params(audio_fd_repp, &params);
  if (err < 0) {
    throw(new ECA_ERROR("AUDIOIO-ALSA2-PLUGIN", "Error when setting up channel params: " + string(snd_strerror(err))));
  }

  struct snd_pcm_channel_setup setup;
  setup.channel = params.channel;
  setup.mode = params.mode;
  ::snd_pcm_plugin_setup(audio_fd_repp, &setup);
  fragment_size_rep = setup.buf.block.frag_size;
  ecadebug->msg(ECA_DEBUG::user_objects, "(audioio-alsa2-plugin) Fragment size: " +
		kvu_numtostr(setup.buf.block.frag_size) + ", max: " +
		kvu_numtostr(setup.buf.block.frags_max) + ", min: " +
		kvu_numtostr(setup.buf.block.frags_min) + ", current: " +
		kvu_numtostr(setup.buf.block.frags) + ".");

//  ::snd_pcm_channel_rep_flush(audio_fd_repp, pcm_channel_rep);
//  err = ::snd_pcm_channel_prepare(audio_fd_repp, pcm_channel_rep);
//  if (err < 0)
//    throw(new ECA_ERROR("AUDIOIO-ALSA2-PLUGIN", "Error when preparing channel: " + string(snd_strerror(err))));

  is_triggered = false;
  is_prepared = false;
  toggle_open_state(true);
}

void ALSA_PCM2_PLUGIN_DEVICE::stop(void) {
  assert(is_triggered == true);
  assert(is_open() == true);
  assert(is_prepared == true);

  ecadebug->msg(ECA_DEBUG::system_objects, "(audioio-alsa2-plugin) stop");

  snd_pcm_channel_status_t status;
  ::memset(&status, 0, sizeof(status));
  status.channel = pcm_channel_rep;
  ::snd_pcm_plugin_status(audio_fd_repp, &status);
  overruns_rep += status.overrun;
  underruns_rep += status.underrun;

  int err = ::snd_pcm_plugin_flush(audio_fd_repp, pcm_channel_rep);
  if (err < 0)
    throw(new ECA_ERROR("AUDIOIO-ALSA2-PLUGIN", "Error when flushing channel: " + string(snd_strerror(err))));

  ecadebug->msg(ECA_DEBUG::user_objects, "(audioio-alsa2-plugin) Audio device \"" + label() + "\" disabled.");

  is_triggered = false;
  is_prepared = false;
}

void ALSA_PCM2_PLUGIN_DEVICE::close(void) {
  assert(is_open() == true);

  ecadebug->msg(ECA_DEBUG::system_objects, "(audioio-alsa2-plugin) close");

  if (is_triggered == true) stop();
  ::snd_pcm_close(audio_fd_repp);
  toggle_open_state(false);

  assert(is_triggered == false);
}

void ALSA_PCM2_PLUGIN_DEVICE::prepare(void) {
  assert(is_triggered == false);
  assert(is_open() == true);
  assert(is_prepared == false);

  ecadebug->msg(ECA_DEBUG::system_objects, "(audioio-alsa2-plugin) prepare");

  int err = ::snd_pcm_plugin_prepare(audio_fd_repp, pcm_channel_rep);
  if (err < 0)
    throw(new ECA_ERROR("AUDIOIO-ALSA2-PLUGIN", "Error when preparing channel: " + string(snd_strerror(err))));
  is_prepared = true;
}

void ALSA_PCM2_PLUGIN_DEVICE::start(void) {
  assert(is_triggered == false);
  assert(is_open() == true);
  assert(is_prepared == true);

  ecadebug->msg(ECA_DEBUG::system_objects, "(audioio-alsa2-plugin) start");

  if (pcm_channel_rep == SND_PCM_CHANNEL_PLAYBACK)
    ::snd_pcm_channel_go(audio_fd_repp, pcm_channel_rep);
  is_triggered = true;
  //  if (io_mode() == io_write) print_status_debug();
}

long int ALSA_PCM2_PLUGIN_DEVICE::read_samples(void* target_buffer, 
					long int samples) {
  assert(samples * frame_size() <= fragment_size_rep);
  return(::snd_pcm_plugin_read(audio_fd_repp, target_buffer, fragment_size_rep) / frame_size());
}

void ALSA_PCM2_PLUGIN_DEVICE::write_samples(void* target_buffer, long int samples) {
  if (samples * frame_size()== fragment_size_rep) {
    ::snd_pcm_plugin_write(audio_fd_repp, target_buffer, fragment_size_rep);
  }
  else {
    if ((samples * frame_size()) < pcm_info_rep.min_fragment_size ||
	(samples * frame_size()) > pcm_info_rep.max_fragment_size) {
      if (is_triggered) stop();
      return; 
    }
    bool was_triggered = false;
    if (is_triggered == true) { stop(); was_triggered = true; }
    close();
    buffersize(samples, samples_per_second());
    open();
    prepare();
    assert(samples * frame_size() <= fragment_size_rep);
    ::snd_pcm_plugin_write(audio_fd_repp, target_buffer, fragment_size_rep);
    if (was_triggered == true) start();
  }
}

long ALSA_PCM2_PLUGIN_DEVICE::position_in_samples(void) const {
  if (is_triggered == false) return(0);
  snd_pcm_channel_status_t status;
  memset(&status, 0, sizeof(status));
  status.channel = pcm_channel_rep;
  ::snd_pcm_plugin_status(audio_fd_repp, &status);
  return(status.scount / frame_size());
}

ALSA_PCM2_PLUGIN_DEVICE::~ALSA_PCM2_PLUGIN_DEVICE(void) { 
  if (is_open() == true) close(); 

  ecadebug->msg(ECA_DEBUG::system_objects, "(audioio-alsa2-plugin) destruct");

  if (io_mode() != io_read) {
    if (underruns_rep != 0) {
      cerr << "(audioio-alsa2-plugin) WARNING! While writing to ALSA-pcm device ";
      cerr << "C" << card_number_rep << "D" << device_number_rep;
      cerr << ", there were " << underruns_rep << " underruns.\n";
    }
  }
  else {
    if (overruns_rep != 0) {
      cerr << "(audioio-alsa2-plugin) WARNING! While reading from ALSA-pcm device ";
      cerr << "C" << card_number_rep << "D" << device_number_rep;
      cerr << ", there were " << overruns_rep << " overruns.\n";
    }
  }

  //  eca_alsa_unload_dynamic_support();
}

void ALSA_PCM2_PLUGIN_DEVICE::set_parameter(int param, 
				     string value) {
  switch (param) {
  case 1: 
    label(value);
    break;

  case 2: 
    card_number_rep = atoi(value.c_str());
    break;

  case 3: 
    device_number_rep = atoi(value.c_str());
    break;

  case 4: 
    subdevice_number_rep = atoi(value.c_str());
    break;
  }
}

string ALSA_PCM2_PLUGIN_DEVICE::get_parameter(int param) const {
  switch (param) {
  case 1: 
    return(label());

  case 2: 
    return(kvu_numtostr(card_number_rep));

  case 3: 
    return(kvu_numtostr(device_number_rep));

  case 4: 
    return(kvu_numtostr(subdevice_number_rep));
  }
  return("");
}

#endif // ALSALIB_050
