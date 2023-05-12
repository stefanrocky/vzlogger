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
 
#include "Calculate.hpp"
#include <VZException.hpp>
#include <algorithm>
#include <unistd.h>
#include "Options.hpp"
#include <math.h>

Calculate::Calculate(const std::string &meterName, ReadingIdentifier::Ptr rid, OPERATION operation, long max_time_difference_same_data_ms, 
long min_time_difference_derivation_s, long max_time_difference_derivation_s, long negative_result_filter) {
	if (rid == NULL)
		throw vz::VZException("argument 'rid' is invalid NULL"); 
	
	_logContext.assign(meterName);
	
	char unparseBuf[200];
	unparseBuf[0] = 0;
	if (rid->unparse(unparseBuf, sizeof(unparseBuf))) {
		_logContext.append("/");
		_logContext.append(unparseBuf);
	}
	
	_identifier = rid;
	_operation = operation;
	_max_time_difference_same_data_ms = max_time_difference_same_data_ms;
	_min_time_difference_derivation_s = min_time_difference_derivation_s;
	_max_time_difference_derivation_s = max_time_difference_derivation_s;
	_negative_result_filter = negative_result_filter;
	_initialized = false;
	_pending_data_valid = false;
	
	if (operation == Calculate::OPERATION::SUM)
		_operationName.assign("SUM");
	else if (operation == Calculate::OPERATION::DERIVATION)
		_operationName.assign("DERIVATION");
	else 
		_operationName.assign("?");
}
	
	
void Calculate::addChannel(ReadingIdentifier::Ptr rid, double factor) {
	if(_initialized)
		throw vz::VZException("invalid operation: addChannel is only possible before addData was first called"); 
		
	if (rid == NULL)
		throw vz::VZException("argument 'rid' is invalid NULL"); 
		
	channel_data cd = {rid, factor};
	_channels.push_back( cd );
}
	
void Calculate::validateValue(double& value) const
{
	if (value < 0) {
		if (_negative_result_filter == 0)
			value = 0;
		else if (_negative_result_filter == -1)
			value = fabs(value);
		else
			value *= (double)_negative_result_filter;
	}
}
	
void Calculate::addData(const std::vector<Reading> &rds, size_t rds_count) {
	_initialized = true;
		
	if (rds_count < _channels.size())
		return;
		
	std::vector<size_t> channels_pos;
	size_t rds_pos = 0;
    size_t dataSizeBefore = _data.size();
    
	while (findChannelData(rds, rds_count, rds_pos, channels_pos)) {
		
		reading_data rd;
		rds[channels_pos[0]].time_get(&rd.time);
		rd.value = rds[channels_pos[0]].value() * _channels[0].factor;
		
		for (size_t idx = 1; idx < _channels.size(); idx++) {
			rd.value += rds[channels_pos[idx]].value() * _channels[idx].factor;
		}
		
		if (_operation == Calculate::OPERATION::SUM) {
			validateValue(rd.value);
			_data.push_back(rd);
		} else if (_operation == Calculate::OPERATION::DERIVATION) {
			if (!_pending_data_valid) {
				_pending_data_valid = true;
				_pending_data = rd;
				continue;
			} else {
				double dt = getDerivationTime_s(&_pending_data, &rd);
				if (dt < 0) {
					_pending_data = rd;
				} else if (dt > 0) {								
					double value = ((rd.value - _pending_data.value) / dt);
					_pending_data = rd;
					validateValue(value);
					rd.value = value;
					_data.push_back(rd);
				}
			}
		}
	}
	
	if (_data.size() > dataSizeBefore) {
		print(log_finest, "%s: Got %u new calculations", "calc", _logContext.c_str(), _data.size() - dataSizeBefore);
	}		
}

bool Calculate::findChannelData(const std::vector<Reading> &rds, size_t rds_count, size_t &rds_pos, std::vector<size_t> &channels_pos) const {
	if (_channels.size() == 0 || rds_pos >= rds_count)
		return false;
	
	channels_pos.clear();
	size_t rds_first = rds_pos;		
	bool retry = true;
	
	while (rds_first < rds_count && channels_pos.size() < _channels.size() && retry) {		
		retry = false;
		
		for (size_t idxc = 0; !retry && idxc < _channels.size(); idxc++) {
			const channel_data &channel = _channels[idxc];
			
			for (size_t idxr = rds_first; !retry && idxr < rds_count; idxr++) {
				if (*rds[idxr].identifier().get() == *channel.identifier.get()) {
					for (size_t idxp = 0; idxp < channels_pos.size(); idxp++) {
						if (!hasSameTime(&rds[idxr], &rds[channels_pos[idxp]]) ) {
							rds_first = idxr + 1;
							retry = true;
							channels_pos.clear();
							break;
						}
					}
					if (!retry) {
						channels_pos.push_back(idxr);
						break; // next channel
					}					
				}
			}
		}
	}
		
	if (channels_pos.size() == _channels.size()) {
		// we allow the data being sorted by identifier after time, so retry finding suitable data with next index of smallest channels_pos
		rds_pos = *std::min_element(channels_pos.begin(), channels_pos.end()) + 1;
		return true;
	}
		
	if (rds_pos == 0) {
		print(log_finest, "%s: No channel data found, data=%u, ch=%u<%u", "calc", _logContext.c_str(), rds_count, channels_pos.size(), _channels.size());
	}		
		
	return false;
}

bool Calculate::hasSameTime(const Reading *prd1, const Reading *prd2) const {
	if(prd1 == prd2)
		return true;
	if(prd1 == NULL)
		return false;
	int64_t dt = prd1->time_ms() - prd2->time_ms();
	if(dt < 0)
		dt = -dt;
	return dt < _max_time_difference_same_data_ms;
}

double Calculate::getDerivationTime_s(const reading_data *prd1, const reading_data *prd2) const {
	if(prd1 == prd2)
		return 0;
	if(prd1 == NULL)
		return 0;
		
    // dt in seconds
	double dt = (prd2->time.tv_sec - prd1->time.tv_sec) + (prd2->time.tv_usec / 1e6) - (prd1->time.tv_usec / 1e6);
	if (dt < 0 || dt > _max_time_difference_derivation_s)
		return -1;
	if (dt < _min_time_difference_derivation_s)
		return 0;
	return dt;
}

size_t Calculate::readData(std::vector<Reading> &rds, size_t rds_pos, size_t rds_max) {	
	if (_data.size() == 0)
		return 0;
				
	size_t ret = 0;
	for(size_t idx = 0; idx < _data.size(); idx++) {
		if (rds_pos >= rds_max)
			break;
		rds[rds_pos].identifier(_identifier);
		rds[rds_pos].value( _data[idx].value );
		rds[rds_pos].time( _data[idx].time );
				
		rds_pos++;
		ret++;
	}
	
	if (_data.size() > ret)
		print(log_warning, "%s: Reading buffer to small! Lost %u data.", "calc", _logContext.c_str(), _data.size() - ret);
	
	_data.clear();
	return ret;
}

size_t Calculate::calculateData(std::vector<Reading> &rds, size_t rds_pos, size_t rds_count) {
	addData(rds, rds_pos);
	return readData(rds, rds_pos, rds_count);
}

void Calculate::construct(std::vector<Calculate::Ptr> &calculations, meter_protocol_t protocol, const std::string &meterName, const Option& calculate_config) {
	calculations.clear();
	
	if (calculate_config.type() != Option::type_array)
		return;
							
	struct json_object *jso = (json_object*)calculate_config;		 
	int count = json_object_array_length(jso);
	for (int idx = 0; idx < count; idx++) {
		struct json_object *jit = json_object_array_get_idx(jso, idx);
		if (jit) {
			OptionList optList;
			std::list<Option> options;
			json_object_object_foreach(jit, key, val) {
				options.push_back(Option(key, val));
			}
				
			std::string operation_str = optList.lookup_string(options, "operation");
			Calculate::OPERATION operation;
			
			if (operation_str.find("SUM") == 0) {
				operation = Calculate::OPERATION::SUM;
				operation_str.erase(0, 3);
			}
			else if (operation_str.find("DERIVATION") == 0) {
				operation = Calculate::OPERATION::DERIVATION;
				operation_str.erase(0, 10);
			}
			else
				throw vz::VZException("Operation not found!");
				
			ReadingIdentifier::Ptr rid = reading_id_parse(protocol, optList.lookup_string(options, "identifier"));
				
			int max_time_difference_same_data_ms;
			try {
				max_time_difference_same_data_ms = optList.lookup_int(options, "max_time_difference_same_data_ms");
			} catch (vz::OptionNotFoundException &e) {
				max_time_difference_same_data_ms = 10;
			}
			
			int min_time_difference_derivation_s;
			try {
				min_time_difference_derivation_s = optList.lookup_int(options, "min_time_difference_derivation_s");
			} catch (vz::OptionNotFoundException &e) {
				min_time_difference_derivation_s = 2;
			}

			int max_time_difference_derivation_s;
			try {
				max_time_difference_derivation_s = optList.lookup_int(options, "max_time_difference_derivation_s");
			} catch (vz::OptionNotFoundException &e) {
				max_time_difference_derivation_s = 60;
			}

			int negative_result_filter;
			try {
				negative_result_filter = optList.lookup_int(options, "negative_result_filter");
			} catch (vz::OptionNotFoundException &e) {	
				if (operation_str == "+")
					negative_result_filter = 0;
				else if (operation_str == "^")
					negative_result_filter = -1;
				else
					negative_result_filter = 1;
			}

			Calculate::Ptr calc;
			calc.reset( new Calculate(meterName, rid, operation, 
				max_time_difference_same_data_ms, min_time_difference_derivation_s, max_time_difference_derivation_s,
				negative_result_filter));
			calc->addChannels(protocol, optList.lookup(options, "input_channels"));	
			
			if (calc->channels() == 0)
				throw vz::VZException("No 'input_channels' found!");
				
			calculations.push_back(calc);
		} 
	}		
}

void Calculate::addChannels(meter_protocol_t protocol, const Option& channel_config) {
	if (channel_config.type() != Option::type_array)
		throw vz::VZException("Type of 'input_channels' should be 'array'");
	
	struct json_object *jso = (json_object*)channel_config;		
	int count = json_object_array_length(jso);
	for (int idx = 0; idx < count; idx++) {
		struct json_object *jit = json_object_array_get_idx(jso, idx);
		if (jit) {
			OptionList optList;
			std::list<Option> options;
			json_object_object_foreach(jit, key, val) {
				options.push_back(Option(key, val));
			}
								
			ReadingIdentifier::Ptr rid = reading_id_parse(protocol, optList.lookup_string(options, "identifier"));
			double factor;
			try {
				const Option& opt = optList.lookup(options, "factor");
				factor = (opt.type() == Option::type_int) ? (int)opt : (double)opt;				
			} catch (vz::OptionNotFoundException &e) {
				factor = 1;
			}
			addChannel(rid, factor);
		}
	}
}

size_t Calculate::calculate(const std::vector<Calculate::Ptr> &calculations, std::vector<Reading> &rds, size_t rds_pos, size_t rds_count) {
	size_t ret = 0;
	for (std::vector<Calculate::Ptr>::const_iterator it = calculations.begin(); it != calculations.end(); ++it) {
		(*it)->addData(rds, rds_pos);
		size_t n = (*it)->calculateData(rds, rds_pos, rds_count);
		rds_pos += n;
		ret += n;
	}	
	return ret;
}

