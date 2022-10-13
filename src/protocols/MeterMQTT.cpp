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

#include "mqtt.hpp"
#include "Options.hpp"
#include "protocols/MeterMQTT.hpp"
#include <VZException.hpp>
#include "Json.hpp"

MeterMQTT::MeterMQTT(const std::list<Option> &options)
	: Protocol("mqtt") {
	_open = false;
	OptionList optlist;

	try {
		_subscription = optlist.lookup_string(options, "subscription");
		if (_subscription.length() == 0)
			throw vz::OptionNotFoundException("subscription empty"); 
		if (_subscription.find('#') != std::string::npos || _subscription.find('+') != std::string::npos)
			throw vz::VZException("subscription wildcards '#' or '+' not supported"); 
	} catch (vz::OptionNotFoundException &e) {
		print(log_alert, "Missing subscription", name().c_str());
		throw;		
	} catch (vz::VZException &e) {
		std::stringstream oss;
		oss << e.what();
		print(log_alert, "Invalid subscription: %s", name().c_str(), oss.str().c_str());
		throw;
	}

	try {
		_use_local_time = optlist.lookup_bool(options, "use_local_time");
	} catch (vz::OptionNotFoundException &e) {
		/* using default value if not specified */
		_use_local_time = false;
	}
	
	try {
		_data_format = optlist.lookup_string(options, "data_format");
		if  (_data_format != "json")
			throw vz::VZException("data_format should be 'json'"); 
	} catch (vz::OptionNotFoundException &e) {
		/* using default value if not specified */
		_data_format = "json";
	} catch (vz::VZException &e) {
		std::stringstream oss;
		oss << e.what();
		print(log_alert, "Invalid data_format: %s", name().c_str(), oss.str().c_str());
		throw;
	}
	
	try {
		const Option& opt = optlist.contains(options, "data_query_values") ? optlist.lookup(options, "data_query_values") : optlist.lookup(options, "data_query_value");
		std::string query_value;
		if (opt.type() == Option::type_array) {			
			struct json_object *jso = (json_object *)opt;
			if (jso) {
				int count = json_object_array_length(jso);
				for (int idx = 0; idx < count; idx++) {
					struct json_object *jb = json_object_array_get_idx(jso, idx);
					if (jb) {
						query_value = json_object_get_string(jb);
						if (query_value.size() > 0)
							_data_query_values.push_back(query_value);
					} 
				}
			} 
		}		
		else if (opt.type() == Option::type_string) {
			query_value = (const char *)opt;
			if (query_value.size() > 0)
				_data_query_values.push_back(query_value);
		}
		else {
			throw vz::OptionNotFoundException("data_query_value(s) has invalid type"); 
		}
		if (_data_query_values.size() == 0)
			throw vz::OptionNotFoundException("data_query_value(s) empty"); 
	} catch (vz::OptionNotFoundException &e) {
		print(log_alert, "Missing data_query_value(s)", name().c_str());
		throw;		
	}	
	
	try {
		_data_query_time = optlist.lookup_string(options, "data_query_time");
		if (_data_query_time.length() == 0)
			throw vz::OptionNotFoundException("data_query_time empty"); 
	} catch (vz::OptionNotFoundException &e) {
		/* using default value if not specified */
		_use_local_time = true;		
	}	
		
	try {
		std::string time_unit = optlist.lookup_string(options, "time_unit");
		if (time_unit.length() == 0)
			throw vz::OptionNotFoundException("time_unit empty"); 
		if (time_unit == "ms")
			_time_unit = 0;
		else if (time_unit == "s")
			_time_unit = 1;
		else
			throw vz::VZException("Only time_unit 's' or 'ms' are supported"); 
	} catch (vz::OptionNotFoundException &e) {
		/* using default value if not specified */
		_time_unit = 0;		
	} catch (vz::VZException &e) {
		std::stringstream oss;
		oss << e.what();
		print(log_alert, "Invalid time_unit: %s", name().c_str(), oss.str().c_str());
		throw;
	}
	
	print(log_info, "Initialized: subscription: %s", name().c_str(), _subscription.c_str());
	for (size_t idx = 0; idx < _data_query_values.size(); idx++)
		print(log_info, "data_query_value[%u]: %s", name().c_str(), idx, _data_query_values[idx].c_str());
	print(log_info, "data_query_time: %s, unit: %d", name().c_str(), _data_query_time.c_str(), _time_unit);

	//test();
}

MeterMQTT::~MeterMQTT() {
}

int MeterMQTT::open() {
	if (mqttClient == NULL) {
		print(log_alert, "mqttClient not initialized", name().c_str());
		return ERR;
	}
	
	if (!mqttClient->subscribe(_subscription)) {
		print(log_alert, "mqttClient.subscribe failed", name().c_str());
		return ERR;
	}
	
	_open = true;
	return SUCCESS;
}

int MeterMQTT::close() {
	if (_open && mqttClient)	{
		mqttClient->unsubscribe(_subscription);
	}
	_open = false;
	return SUCCESS;
}

ssize_t MeterMQTT::read(std::vector<Reading> &rds, size_t rds_max) {
	if (_open && mqttClient) {
		std::vector<std::string> data;
		
		if (_read_pending.size() == 0) {
			mqttClient->receive(_subscription, data);
		} else {
			_read_pending.swap(data);
			std::vector<std::string> data_add;
			mqttClient->receive(_subscription, data_add);
			data.insert(data.end(), data_add.begin(), data_add.end());
		}
			
		if (data.size() > 0) {
			size_t rds_pos = 0;
			size_t parsed = 0;
			if (rds.size() < rds_max)
				rds_max = rds.size();
			std::vector<std::string>::iterator it = data.begin();			
			for (; it != data.end() && rds_pos < rds_max; ++it) {				
				ssize_t res = parse(*it, rds, rds_pos, rds_max);
				if (res < 0) {
					print(log_warning, "Parsing aborted, %d data-messages queued again", name().c_str(), data.size() - parsed);
					break;
				}
				parsed++;
				rds_pos += res;				
			}
			
			if (it != data.end()) {
				data.erase(data.begin(), ++it);
				data.swap(_read_pending);
			} 
			
			return rds_pos;
		}
	}	
	return 0;
}

ssize_t MeterMQTT::parse(const std::string& msg, std::vector<Reading> &rds, size_t rds_pos, size_t rds_max) {
	if (rds_pos >= rds_max)
		return -1;
		
	ssize_t res = 0;
	struct json_object *json_data = NULL;
	struct json_tokener *json_tok = json_tokener_new();
	
	json_data = json_tokener_parse_ex(json_tok, msg.c_str(), msg.length());
	if (json_tok->err == json_tokener_success) {
		struct timeval tv;
		bool tv_valid = false;
		
		for (size_t idx = 0; idx < _data_query_values.size() && rds_pos < rds_max; idx++) {
			std::string query_value = _data_query_values[idx];
			std::string id;
			struct json_object *jvalue = json_path_single(json_data, query_value.c_str(), &id, 2, json_type_double, json_type_int);
			if (jvalue) {
				rds[rds_pos].value( json_object_get_double(jvalue) );				
			
				ReadingIdentifier *rid(new StringIdentifier(id));
				rds[rds_pos].identifier(rid);
				
				if (!tv_valid) {				
					if (!_use_local_time) {
						struct json_object *jtime = json_path_single(json_data, _data_query_time.c_str(), 2, json_type_int, json_type_double);
						if (jtime) {
							int64_t t = json_object_get_int64(jtime);
							if (_time_unit == 0) {
								tv.tv_sec = (long)(t / 1000);
								tv.tv_usec = (long)(t % 1000) * 1000;	
								tv_valid = true;					
							} else if (_time_unit == 1) {					
								tv.tv_sec = (long)t;
								tv.tv_usec = 0;
								tv_valid = true;
							} 
						}															
					} else {
						gettimeofday(&tv, NULL); /* use local time */
						tv_valid = true;
					}
				}
				
				if (!tv_valid) {
					print(log_finest, "Time '%s' not found in object: %s", name().c_str(), _data_query_time.c_str(), json_object_to_json_string(json_data)); 	
					break;
				}
				
				rds[rds_pos].time(tv);
				res++;
				rds_pos++;
			} else {
				print(log_finest, "Value '%s' not found in object: %s", name().c_str(), query_value.c_str(), json_object_to_json_string(json_data)); 	
			}
		}
		json_object_put(json_data);	
	}
	
	json_tokener_free(json_tok);
		
	return res;	
}

void MeterMQTT::test() {
	struct json_object *obj;
	struct json_object *jvalue;
	obj = json_tokener_parse("{ 'timestamp': 1661448704422, 'value': 21.192586616100357 }");
	inspect_json_object(obj, 0);
	
	std::string path;
	jvalue = json_path_single(obj, path.c_str(), 0);
	print(log_finest, "test: path='%s' found: %s", name().c_str(), path.c_str(), json_object_to_json_string(jvalue)); 	
	
	path.assign("$.value");
	jvalue = json_path_single(obj, path.c_str(), 1, json_type_double);
	print(log_finest, "test: path='%s' found: %s", name().c_str(), path.c_str(), json_object_to_json_string(jvalue)); 	
	
	path.assign("$.timestamp");
	jvalue = json_path_single(obj, path.c_str(), 1, json_type_int);
	print(log_finest, "test: path='%s' found: %s", name().c_str(), path.c_str(), json_object_to_json_string(jvalue)); 	
	
	json_object_put(obj);
	
	obj = json_tokener_parse(
 "{"
  "'switch:0': {"
    "'id': 0,"
    "'source': 'timer',"
    "'output': true,"
    "'ts': 1626935739.79,"
    "'timer_duration': 60,"
    "'apower': 8.9,"
    "'voltage': 237.5,"
    "'aenergy': {"
      "'total': 6.532,"
      "'by_minute': ["
        "45.199,"
        "47.141,"
        "88.397"
      "],"
      "'minute_ts': 1626935779"
    "},"
    "'temperature': {"
      "'tC': 23.5,"
      "'tF': 74.4"
    "}"
  "} }");
  
	inspect_json_object(obj, 0);
	
	if (json_object_object_get_ex(obj, "switch:0", &jvalue) && jvalue) {
		print(log_finest, "test: 'switch:0' found: %s", name().c_str(), json_object_to_json_string(jvalue)); 	
	} else {
		print(log_finest, "test: 'switch:0' not found", name().c_str()); 	
	}
	
	if (json_object_object_get_ex(obj, "source", &jvalue) && jvalue) {
		print(log_finest, "test: 'source' found: %s", name().c_str(), json_object_to_json_string(jvalue)); 	
	} else {
		print(log_finest, "test: 'source' not found", name().c_str()); 	
	}
		
	path.assign("$.switch:0.source");
	jvalue = json_path_single(obj, path.c_str(), 1, json_type_string);
	print(log_finest, "test: path='%s' found: %s", name().c_str(), path.c_str(), json_object_to_json_string(jvalue)); 	

	jvalue = json_path_single(obj, path.c_str(), 1, json_type_boolean);
	print(log_finest, "test: path='%s' found: %s", name().c_str(), path.c_str(), json_object_to_json_string(jvalue)); 	
	
	path.assign("$.switch:0.ts");
	jvalue = json_path_single(obj, path.c_str(), 2, json_type_int, json_type_double );
	int64_t ts = json_object_get_int64(jvalue);	
	print(log_finest, "test: path='%s' found: %s, ts-int64=%lld", name().c_str(), path.c_str(), json_object_to_json_string(jvalue), ts); 
	
	path.assign("$.switch:0.apower");
	jvalue = json_path_single(obj, path.c_str(), 2, json_type_double, json_type_int);
	print(log_finest, "test: path='%s' found: %s", name().c_str(), path.c_str(), json_object_to_json_string(jvalue)); 	
	
	path.assign("$.switch:0.aenergy.total");
	jvalue = json_path_single(obj, path.c_str(), 2, json_type_double, json_type_int);
	print(log_finest, "test: path='%s' found: %s", name().c_str(), path.c_str(), json_object_to_json_string(jvalue)); 	
	
	path.assign("$.switch:0.aenergy.by_minute");
	jvalue = json_path_single(obj, path.c_str(), 2, json_type_double, json_type_int);
	print(log_finest, "test: path='%s' found: %s", name().c_str(), path.c_str(), json_object_to_json_string(jvalue)); 	
	
	path.assign("$.switch:0.aenergy.by_minute[0]");
	jvalue = json_path_single(obj, path.c_str(), 2, json_type_double, json_type_int);
	print(log_finest, "test: path='%s' found: %s", name().c_str(), path.c_str(), json_object_to_json_string(jvalue)); 	
	
	path.assign("$.switch:0.aenergy.by_minute[0]");
	jvalue = json_path_single(obj, path.c_str(), 1, json_type_string);
	print(log_finest, "test: path='%s' found: %s", name().c_str(), path.c_str(), json_object_to_json_string(jvalue)); 	
	
	path.assign("$.switch:0.aenergy.by_minute[-1]");
	jvalue = json_path_single(obj, path.c_str(), 2, json_type_double, json_type_int);
	print(log_finest, "test: path='%s' found: %s", name().c_str(), path.c_str(), json_object_to_json_string(jvalue)); 	
	
	json_object_put(obj);  
}


