#ifndef __mqttex_hpp_
#define __mqttex_hpp_

#include "mqtt.hpp"

class MqttClientEx : public MqttClient {
  public:
	MqttClientEx(struct json_object *option);
	MqttClientEx() = delete;                   // no default constr.
	MqttClientEx(const MqttClient &) = delete; // no copy constr.
	~MqttClientEx() override;

	bool subscribe(const std::string& sub) override;
	void unsubscribe(const std::string& sub) override;
	size_t receive(const std::string& sub, std::vector<std::string>& data) override;

  protected:
	void connect_callback(struct mosquitto *mosq, int result) override;
	void disconnect_callback(struct mosquitto *mosq, int result) override;
	void message_callback(struct mosquitto *mosq, const struct mosquitto_message *msg) override;
	
	struct SubscriptionEntry {
		// 0=off, 1=on, 2...n=max_retry, 100=success
		int _state = 0;
		std::mutex _dataMutex;
		std::vector<std::string> _data;
	};
	
	std::mutex _subscriptionMapMutex;
	std::unordered_map<std::string, SubscriptionEntry> _subscriptionMap;	
	
	void update();
	void update(const std::string& sub, SubscriptionEntry& entry);
	void reset();
};

#endif
