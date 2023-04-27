/*
 * This file is part of volkzaehler.org
 *
 * volkzaehler.org is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * volkzaehler.org is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with volkszaehler.org. If not, see <http://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "Options.hpp"
#include "protocols/MeterModbus.hpp"
#include <VZException.hpp>

MeterModbus::MeterModbus(const std::list<Option> &options)
	: Protocol("modbus") {	
	_mb = NULL;
	_connected = false;
	OptionList optlist;


	try {
		_host = optlist.lookup_string(options, "host");
		if (_host.length() == 0)
			throw vz::OptionNotFoundException("host empty"); 
	} catch (vz::OptionNotFoundException &e) {
		print(log_alert, "Missing host", name().c_str());
		throw;		
	} 

	try {
		_port = optlist.lookup_int(options, "port");
	} catch (vz::OptionNotFoundException &e) {
		// using default value if not specified 
		_port = 502;
	}
	
	try {
		_unitID = optlist.lookup_int(options, "unit_id");
	} catch (vz::OptionNotFoundException &e) {
		print(log_alert, "Missing unit_id", name().c_str());
		throw;		
	}	
	
	std::vector<std::string> register_format;
	
	try {
		const Option& opt = optlist.contains(options, "registers") ? optlist.lookup(options, "registers") : optlist.lookup(options, "register");
		std::string fmt;
		if (opt.type() == Option::type_array) {			
			struct json_object *jso = (json_object *)opt;
			if (jso) {
				int count = json_object_array_length(jso);
				for (int idx = 0; idx < count; idx++) {
					struct json_object *jb = json_object_array_get_idx(jso, idx);
					if (jb) {
						fmt = json_object_get_string(jb);
						if (fmt.size() > 0)
							register_format.push_back(fmt);
					} 
				}
			} 
		}		
		else if (opt.type() == Option::type_string) {
			fmt = (const char *)opt;
			if (fmt.size() > 0)
				register_format.push_back(fmt);
		}
		else {
			throw vz::OptionNotFoundException("register(s) has invalid type"); 
		}
		if (register_format.size() == 0)
			throw vz::OptionNotFoundException("register(s) empty"); 
	} catch (vz::OptionNotFoundException &e) {
		print(log_alert, "Missing register(s)", name().c_str());
		throw;		
	}	
	
	for(std::vector<std::string>::iterator it = register_format.begin(); it != register_format.end(); ++it) {
		std::string fmt = *it;
		if(fmt.size() > 50) {
			throw vz::OptionNotFoundException("register is to long, max 50 characters are allowed");
		}
		char str[52];
		int value, size;
		char type;		
		if(sscanf(fmt.c_str(), "%s %d %d %c", str, &value, &size, &type) != 4 || strlen(str) == 0 || value <= 0 || size <= 0 || size > 4 || 
		    !(type == 'u' || type == 's' || type == 'a' || type == 'b' || type == 'c' || type == 'd')) {
			print(log_alert, "Invalid format of register: %s", name().c_str(), fmt.c_str());
			throw vz::OptionNotFoundException("invalid format of register");
		}
		_register_name.push_back(str);
		_register_value.push_back(value);
		_register_size.push_back(size);
		_register_type.push_back(type);
		print(log_finest, "Adding register: %s %d %d %c", name().c_str(), str, value, size, type);
	}
}

MeterModbus::~MeterModbus() {
}

int MeterModbus::open() {
	if(_mb == NULL) {
	   _connected = false;
	   _mb = modbus_new_tcp(_host.c_str(), _port);
	   
	   if(_mb == NULL) {
		   print(log_alert, "modbus_new_tcp failed: %s", name().c_str(), modbus_strerror(errno));
		   return ERR;
	   }
	   if(_unitID) {
		   if(modbus_set_slave(_mb, _unitID) != 0) {
			   print(log_warning, "modbus_set_slave to unit_id=%d failed: %s", name().c_str(), _unitID, modbus_strerror(errno));
		   }
	   }
	   /*
	   if(modbus_set_error_recovery(_mb, MODBUS_ERROR_RECOVERY_LINK | MODBUS_ERROR_RECOVERY_PROTOCOL) != 0) {
		   print(log_warning, "modbus_set_error_recovery failed: %s", name().c_str(), mdobus_strerror(errno));
	   }*/
	   _connected  = modbus_connect(_mb) == 0;
	   if(!_connected) {
		   print(log_warning, "modbus_connect failed: %s", name().c_str(), modbus_strerror(errno));
	   }
	     
	}	   
	return SUCCESS;
}

int MeterModbus::close() {
	if(_mb) {
		if(_connected)
			modbus_close(_mb);
		_connected = false;
		modbus_free(_mb);
		_mb = NULL;
	}

	return SUCCESS;
}

ssize_t MeterModbus::read(std::vector<Reading> &rds, size_t rds_max) {
	if(_mb == NULL)
		return 0;
	if(!_connected) {
		_connected  = modbus_connect(_mb) == 0;
	}
	if(!_connected)
		return 0;
		
	uint16_t regs[4];
	size_t rds_pos = 0;
	int shift;
	uint64_t uvalue;
	float fvalue;
	
	for(size_t idx = 0; idx < _register_name.size(); ++idx) {
		if(_register_size[idx] > 4 || _register_size[idx] <= 0)
			continue;
		
		int cnt = modbus_read_registers(_mb, _register_value[idx], _register_size[idx], regs);
		if(cnt <= 0 || cnt != _register_size[idx]) {
			print(log_finest, "modbus_read_registers failed (entering disconnected state): %s", name().c_str(), modbus_strerror(errno));
			_connected = false;
			return rds_pos;
		}		
		
		switch(_register_type[idx]) {
			case 'u':
			case 's':
			    uvalue = 0;
			    shift = (cnt - 1) * 16;
			    for(int n = 0; n < cnt; n++) {
					uvalue |= regs[n] << shift;
					shift -= 16;
				}	
				if(IsNaN(uvalue, _register_type[idx], cnt * 2)) {
					print(log_finest, "modbus_read_registers skipping NaN value: %s=0x%llx", name().c_str(), _register_name[idx].c_str(), uvalue);
					continue;
				}
				if(	_register_type[idx] == 'u')
					rds[rds_pos].value((double)uvalue);				
				else if(cnt == 1)
				    rds[rds_pos].value((double)(int16_t)uvalue);				
				else if(cnt == 2)
				    rds[rds_pos].value((double)(int32_t)uvalue);
				else
					rds[rds_pos].value((double)(int64_t)uvalue);
				break;				
			case 'a':
			case 'b':
			case 'c':
			case 'd':
			    if(cnt != 2) {
					print(log_alert, "invalid size %d for float register-type: %c", name().c_str(), _register_size[idx], _register_type[idx]);
					continue;
				}
				switch(_register_type[idx]) {
					case 'a': fvalue = modbus_get_float_abcd(regs); break;
					case 'b': fvalue = modbus_get_float_badc(regs); break;
					case 'c': fvalue = modbus_get_float_cdab(regs); break;
					case 'd': fvalue = modbus_get_float_dcba(regs); break;
				}
				rds[rds_pos].value((double)fvalue);
				break;
			default:
				print(log_alert, "unknown register-type: %c", name().c_str(), _register_type[idx]);
		}
		
		ReadingIdentifier *rid(new StringIdentifier(_register_name[idx]));
		rds[rds_pos].identifier(rid);
		
		print(log_finest, "modbus_read_registers got value: %s=%lf", name().c_str(), _register_name[idx].c_str(), rds[rds_pos].value());
		
		struct timeval tv;
		gettimeofday(&tv, NULL); /* use local time */
		rds[rds_pos].time(tv);
		
		rds_pos++;
	}
	
	return rds_pos;	
}

// Check for NaN values "Not a Number"
bool MeterModbus::IsNaN(uint64_t value, char type, int byte_size) {
	if(byte_size <= 0 || byte_size > 8)
		return true;
		
	uint64_t nan;
	
	switch(type) {
		case 'u': 
		    nan = byte_size == 8 ? -1 : (1ULL << (byte_size * 8)) - 1;
			return value == nan;
		case 's':
		    nan = 1ULL << ((byte_size * 8) - 1);
			return value == nan;			
		default:
			return true;
	}	
	return false;
}
