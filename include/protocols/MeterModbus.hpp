/* along with volkszaehler.org. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _MMODBUS_H_
#define _MMODBUS_H_

#include <protocols/Protocol.hpp>
#include <modbus/modbus.h>

class MeterModbus : public vz::protocol::Protocol {

  public:
	MeterModbus(const std::list<Option> &options);
	virtual ~MeterModbus();

	ssize_t read(std::vector<Reading> &rds, size_t rds_max);

	int open();
	int close();
	
  protected:
	std::string _host;
	int _port;
	int _unitID;
	std::vector<std::string> _register_name;
	std::vector<int> _register_value;
	std::vector<int> _register_size; // number of 16 bit blocks
	std::vector<char> _register_type; // u=unsigned, i=signed, abcd=float
	std::vector<int> _register_flags; // Flags: 0x1: IsNaN to 0-value, 0x2: IsNaN to 0-value once
	std::vector<int> _register_state; // 0: nothing received, 1: last received OK, 2: last receive IsNaN 
	
    modbus_t *_mb;		
    bool _connected;	
    
    static bool IsNaN(uint64_t value, char type, int byte_size);
};

#endif /* _MMODBUS_H_ */
