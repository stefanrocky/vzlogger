#include "mqttex.hpp"
#include "common.h"
#include "mosquitto.h"
#include <cassert>
#include <sstream>
#include <unistd.h>

#define SUB_OFF 0
#define SUB_ON 1
#define SUB_RETRY_MAX 5
#define SUB_SUCCESS 100

// class impl.
MqttClientEx::MqttClientEx(struct json_object *option) : MqttClient(option) {
	print(log_finest, "MqttClientEx::MqttClientEx called", "mqtt");
}


MqttClientEx::~MqttClientEx() {
	print(log_finest, "~MqttClientEx called", "mqtt");
}

bool MqttClientEx::subscribe(const std::string& sub) {	
	if (sub.empty() || sub.find('#') != std::string::npos || sub.find('+') != std::string::npos)
		return false;
	
	std::unique_lock<std::mutex> lock(_subscriptionMapMutex);
	auto it = _subscriptionMap.find(sub);	
	if (it != _subscriptionMap.end()) {		
		if ((*it).second._state != SUB_OFF)
			return false;
		(*it).second._state = SUB_ON;		
	} else {		
		auto& ref = _subscriptionMap[sub];
		ref._state = SUB_ON;
	}

	return true;
}

void MqttClientEx::unsubscribe(const std::string& sub) {	
	std::unique_lock<std::mutex> lock(_subscriptionMapMutex);	
	auto it = _subscriptionMap.find(sub);	
	if (it != _subscriptionMap.end()) {				
		auto unsubscribe = (*it).second._state == SUB_SUCCESS;
		(*it).second._state = SUB_OFF;
		std::unique_lock<std::mutex> lockData((*it).second._dataMutex);
		(*it).second._data.clear();
		lockData.unlock();
		lock.unlock();	
			
		if (unsubscribe && _mcs) {
			int res = mosquitto_unsubscribe(_mcs, NULL, sub.c_str());	
					
			if (res != MOSQ_ERR_SUCCESS) {
				print(log_finest, "MqttClientEx:unsubscribe unsubscribe \"%s\" failed: %s", "mqtt",
					  sub.c_str(), mosquitto_strerror(res));
			} else {
				print(log_finest, "MqttClientEx:unsubscribe unsubscribe \"%s\" success", "mqtt",
					  sub.c_str());
			}	
		}	
	}		
}

size_t MqttClientEx::receive(const std::string& sub, std::vector<std::string>& data) {
	data.clear();
	std::unique_lock<std::mutex> lock(_subscriptionMapMutex);	
	auto it = _subscriptionMap.find(sub);	
	if (it == _subscriptionMap.end())
		return 0;
	
	auto& entry = (*it).second;
	lock.unlock();
	
	update(sub, entry);
	std::unique_lock<std::mutex> lockData(entry._dataMutex);	
	entry._data.swap(data);
	return data.size();
}

void MqttClientEx::update(const std::string& sub, SubscriptionEntry& entry) {
	if (!_mcs)
		return;
		
	if (entry._state > SUB_OFF && entry._state < SUB_RETRY_MAX) {
		//int mid = 0;			
		int res = mosquitto_subscribe(_mcs, NULL, sub.c_str(), 0);			
		if (res != MOSQ_ERR_SUCCESS) {
			entry._state ++;
			print(log_finest, "MqttClientEx:update subscribe \"%s\" (state=%d) failed: %s", "mqtt",
				  sub.c_str(), entry._state, mosquitto_strerror(res));
		} else {
			entry._state = SUB_SUCCESS;
			print(log_finest, "MqttClientEx:update subscribe \"%s\" (state=%d) success", "mqtt",
				  sub.c_str(), entry._state);
		}							
	}		
}

void MqttClientEx::update() {
	if (!_mcs)
		return;

	std::unique_lock<std::mutex> lock(_subscriptionMapMutex);
	for (auto it = _subscriptionMap.begin(); it != _subscriptionMap.end(); ++it) {
		update((*it).first, (*it).second);
	}
}

void MqttClientEx::reset() {
	std::unique_lock<std::mutex> lock(_subscriptionMapMutex);
	for (auto it = _subscriptionMap.begin(); it != _subscriptionMap.end(); ++it) {
		if ((*it).second._state > SUB_OFF) {
			print(log_finest, "MqttClientEx:reset \"%s\" (state=%d)", "mqtt",
				  (*it).first.c_str(), (*it).second._state);			
			(*it).second._state = SUB_ON;
		}		
	}
}
	
void MqttClientEx::connect_callback(struct mosquitto *mosq, int result) {
	bool isConnected = _isConnected;
	MqttClient::connect_callback(mosq, result);
	if(isConnected && !_isConnected) {
		reset();
	}
}

void MqttClientEx::disconnect_callback(struct mosquitto *mosq, int result) {	
	bool isConnected = _isConnected;
	MqttClient::disconnect_callback(mosq, result);
	if(isConnected && !_isConnected) {
		reset();
	}
}

void MqttClientEx::message_callback(struct mosquitto *mosq, const struct mosquitto_message *msg) {	
	MqttClient::message_callback(mosq, msg);
	if(msg == NULL || msg->payloadlen <= 0 || msg->topic == NULL)
		return;

	print(log_finest, "MqttClientEx:message_callback \"%s\": mid=%d", "mqtt",
			  msg->topic, msg->mid);		

	std::unique_lock<std::mutex> lock(_subscriptionMapMutex);	
	auto it = _subscriptionMap.find(msg->topic);	
	if (it != _subscriptionMap.end() && (*it).second._state == SUB_SUCCESS) {		
		std::unique_lock<std::mutex> lockData((*it).second._dataMutex);	
		std::string data((const char*)msg->payload, msg->payloadlen);
		(*it).second._data.emplace_back(data);
		print(log_finest, "MqttClientEx:message_callback \"%s\": %s", "mqtt",
			  (*it).first.c_str(), data.c_str());		
	}		
}
