// ------------------------------------------------------------------------
// eca-logger-default.cpp: 
// Copyright (C) 2002-2004 Kai Vehmanen
//
// Attributes:
//     eca-style-version: 2
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

#include <algorithm> /* find() */
#include <string>

#include "eca-logger-default.h"

void ECA_LOGGER_DEFAULT::do_msg(ECA_LOGGER::Msg_level_t level, const std::string& module_name, const std::string& log_message)
{
  if (is_log_level_set(level) == true) {
    if (level == ECA_LOGGER::subsystems) {
      std::cerr << "[* ";
    }
      
    if (is_log_level_set(ECA_LOGGER::module_names) == true) {
      std::cerr << "(" 
		<< std::string(module_name.begin(), 
			       find(module_name.begin(), module_name.end(), '.'))
		<< ") ";
    }
    
    std::cerr << log_message;
    
    if (level == ECA_LOGGER::subsystems) {
      std::cerr << " *]";
    }
    std::cerr << std::endl;
  }
}

void ECA_LOGGER_DEFAULT::do_flush(void)
{
}

void ECA_LOGGER_DEFAULT::do_log_level_changed(void)
{
}

ECA_LOGGER_DEFAULT::~ECA_LOGGER_DEFAULT(void)
{
}
