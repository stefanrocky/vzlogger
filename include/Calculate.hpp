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
 
#ifndef _CALCULATE_H_
#define _CALCULATE_H_

#include "Reading.hpp"
#include <vector>
#include "common.h"
#include "Options.hpp"

class Calculate {

  public:
    typedef vz::shared_ptr<Calculate> Ptr;
    
	enum OPERATION { 
		SUM, // summerize data of >=2 channels, examples: energy of NT HT
		DERIVATION // numeric derivation of >= 1 channels, examples: power from energy, power from energy-out (factor=-1) and energy-in (factor=1)
	};
	Calculate(const std::string &meterName, ReadingIdentifier::Ptr rid, OPERATION operation, long max_time_difference_same_data_ms = 10, 
		long min_time_difference_derivation_s = 2, long max_time_difference_derivation_s = 60);
	
	void addChannel(ReadingIdentifier::Ptr rid, double factor = 1.0);
	size_t channels() const { return _channels.size(); }
	
	// add and read in one step
	size_t calculateData(std::vector<Reading> &rds, size_t rds_pos, size_t rds_count);
	
	void addData(const std::vector<Reading> &rds, size_t rds_count);
	size_t readData(std::vector<Reading> &rds, size_t rds_pos, size_t rds_count);
	 
	static void construct(std::vector<Calculate::Ptr> &calculations, meter_protocol_t protocol, const std::string &meterName, const Option& calculate_config);
	static size_t calculate(const std::vector<Calculate::Ptr> &calculations, std::vector<Reading> &rds, size_t rds_pos, size_t rds_count);
	
  protected:
    std::string _logContext;
	OPERATION _operation;
	std::string _operationName;
	long _max_time_difference_same_data_ms;
	long _min_time_difference_derivation_s;
	long _max_time_difference_derivation_s;
	ReadingIdentifier::Ptr _identifier;
	bool _initialized;
	
	struct channel_data
	{
		ReadingIdentifier::Ptr identifier;
		double factor;
	};
	
	std::vector<channel_data> _channels;

	struct reading_data
	{
		struct timeval time;
		double value;
	};
	
	std::vector<reading_data> _data;

	bool _pending_data_valid;
	reading_data _pending_data;
	
	bool findChannelData(const std::vector<Reading> &rds, size_t rds_max, size_t &rds_pos, std::vector<size_t> &channels_pos) const;	
	bool hasSameTime(const Reading *prd1, const Reading *prd2) const;
	double getDerivationTime_s(const reading_data *prd1, const reading_data *prd2) const;
	
	void addChannels(meter_protocol_t protocol, const Option& channel_config);	
};

#endif
