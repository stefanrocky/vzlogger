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

#ifndef _MMQTT_H_
#define _MMQTT_H_

#include <protocols/Protocol.hpp>

class MeterMQTT : public vz::protocol::Protocol {

  public:
	MeterMQTT(std::list<Option> options);
	virtual ~MeterMQTT();

	ssize_t read(std::vector<Reading> &rds, size_t n);
	//virtual bool allowInterval() const {
	//	return false;
	//} // don't allow conf setting interval with MQTT

	int open();
	int close();
	
  protected:
	std::string _subscription;
	std::string _data_format;
	std::string _data_query_time;
	std::string _data_query_value;
	// 0 = ms, 1 = s
	int _time_unit;
	bool _use_local_time;
	bool _open;
	
	bool parse(const std::string& msg, Reading* rd);
	
private:
    void test();	
};

#endif /* _MMQTT_H_ */
