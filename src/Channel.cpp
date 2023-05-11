/**
 * Channel class
 *
 * @package vzlogger
 * @copyright Copyright (c) 2011, The volkszaehler.org project
 * @license http://www.gnu.org/licenses/gpl.txt GNU Public License
 * @author Steffen Vogel <info@steffenvogel.de>
 */
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

#include <sstream>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

#include "Channel.hpp"

int Channel::instances = 0;

Channel::Channel(const std::list<Option> &pOptions, const std::string apiProtocol,
				 const std::string uuid, ReadingIdentifier::Ptr pIdentifier)
	: _thread_running(false), _options(pOptions), _buffer(new Buffer()), _identifier(pIdentifier),
	  _last(0), _uuid(uuid), _apiProtocol(apiProtocol), _duplicates(0), _mqtt(true) {
	id = instances++;

	// set channel name
	std::stringstream oss;
	oss << "chn" << id;
	_name = oss.str();

	OptionList optlist;

	try {
		// aggmode
		const char *aggmode_str = optlist.lookup_string(pOptions, "aggmode");
		if (strcasecmp(aggmode_str, "max") == 0) {
			_buffer->set_aggmode(Buffer::MAX);
		} else if (strcasecmp(aggmode_str, "avg") == 0) {
			_buffer->set_aggmode(Buffer::AVG);
		} else if (strcasecmp(aggmode_str, "sum") == 0) {
			_buffer->set_aggmode(Buffer::SUM);
		} else if (strcasecmp(aggmode_str, "none") == 0) {
			_buffer->set_aggmode(Buffer::NONE);
		} else {
			throw vz::VZException("Aggmode unknown.");
		}
	} catch (vz::OptionNotFoundException &e) {
		// using default value if not specified
		_buffer->set_aggmode(Buffer::NONE);
	} catch (vz::VZException &e) {
		std::stringstream oss;
		oss << e.what();
		print(log_alert, "Missing or invalid aggmode (%s)", name(), oss.str().c_str());
		throw;
	}

	try {
		_duplicates = optlist.lookup_int(pOptions, "duplicates");
		if (_duplicates < 0)
			throw vz::VZException("duplicates < 0 not allowed");
	} catch (vz::OptionNotFoundException &e) {
		// using default value if not specified (from above)
	} catch (vz::VZException &e) {
		std::stringstream oss;
		oss << e.what();
		print(log_alert, "Invalid parameter duplicates (%s)", name(), oss.str().c_str());
		throw;
	}

	try {
		_mqtt = optlist.lookup_bool(pOptions, "mqtt");
	} catch (vz::OptionNotFoundException &e) {
		// using default value if not specified (from above)
	} 
	
	try {
		_mqttName = optlist.lookup_string(pOptions, "mqtt_name");
	} catch (vz::OptionNotFoundException &e) {
		// using default value if not specified (from above)
	} 	

	try {
		_mqttDescription = optlist.lookup_string(pOptions, "mqtt_description");
	} catch (vz::OptionNotFoundException &e) {
		// using default value if not specified (from above)
	} 	
	try {
		_mqttGroupKey = optlist.lookup_string(pOptions, "mqtt_group");
		size_t pos = _mqttGroupKey.find_first_of( "./" );
		if (pos != std::string::npos) {
			_mqttGroupName = _mqttGroupKey.substr(pos + 1);
			_mqttGroupKey.erase(pos);
		}
		if (!_mqttGroupKey.empty() && _mqttGroupName.empty()) {
			_mqttGroupName = _mqttName;
			if (_mqttGroupName.empty())
				_mqttGroupName = _uuid;
			if (_mqttGroupName.empty())
				_mqttGroupName = _name;
			if (_mqttGroupName.empty())
				_mqttGroupKey.clear();
		}
	} catch (vz::OptionNotFoundException &e) {
		// using default value if not specified (from above)
	} 
	
	pthread_cond_init(&condition, NULL); // initialize thread syncronization helpers
}

/**
 * Free all allocated memory recursively
 */
Channel::~Channel() {
	// this hangs is the readingthread was pthread_cancelled during wait!
	// pthread_cond_destroy(&condition);
}
