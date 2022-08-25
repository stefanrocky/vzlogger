#include "Json.hpp"
#include "common.h"

struct json_object * json_path_single(struct json_object *jso, const char *path, int allowed_json_types_count, ...) {
	if (jso == NULL)
		return NULL;
		
	std::string spath;
	if(path != NULL)
		spath.assign(path);
		
	va_list allowed_json_types;
	va_start(allowed_json_types, allowed_json_types_count);
	struct json_object * ret = json_path_singleV(jso, spath, NULL, allowed_json_types_count, allowed_json_types);
	va_end(allowed_json_types);
	return ret;
}

struct json_object * json_path_single(struct json_object *jso, const char *path, std::string* name, int allowed_json_types_count, ...) {
	if (jso == NULL)
		return NULL;
		
	std::string spath;
	if(path != NULL)
		spath.assign(path);
		
	va_list allowed_json_types;
	va_start(allowed_json_types, allowed_json_types_count);
	struct json_object * ret = json_path_singleV(jso, spath, name, allowed_json_types_count, allowed_json_types);
	va_end(allowed_json_types);
	return ret;
}

struct json_object * json_path_singleV(struct json_object *jso, const std::string &path, std::string* name, int allowed_json_types_count, va_list allowed_json_types) {
	if (jso == NULL)
		return NULL;
				
	if (path.empty() || path == "$") {
		if (allowed_json_types_count > 0) {
			while(allowed_json_types_count-- > 0) {
				if (json_object_get_type(jso) == (json_type)va_arg(allowed_json_types, int))
					return jso;								
			}
			return NULL;
		}
		return jso;
	}
	
	char path0 = path.at(0);
	struct json_object *jso_next;
	
	if (path0 == '$') {
		return json_path_singleV(jso, path.substr(1), name, allowed_json_types_count, allowed_json_types);
	}	
	else if (path0 == '.') {
		size_t pos = path.find_first_of(".[", 1);
		std::string key;		
		if (pos == std::string::npos)
			key = path.substr(1);
		else
			key = path.substr(1, pos - 1);
		if (key.empty()) {
			print(log_error, "path-s: invalid syntax in path '%s', empty key after '.'", "json", path.c_str()); 		  			
			return NULL;
		}
		
		if (!json_object_object_get_ex(jso, key.c_str(), &jso_next))
			return NULL;
			
		if (name != NULL)
			name->assign(key);
		if (pos == std::string::npos)
			return json_path_singleV(jso_next, std::string(), name, allowed_json_types_count, allowed_json_types);
		else				
			return json_path_singleV(jso_next, path.substr(pos), name, allowed_json_types_count, allowed_json_types);
	}	
	else if (path0 == '[') {
		size_t pos = path.find_first_of("].");
		if (pos == std::string::npos || path.at(pos) != ']') {
			print(log_error, "path-s: invalid syntax in path '%s', missing closing ']' after '['", "json", path.c_str()); 		  			
			return NULL;
		}
		
		std::string sidx = path.substr(1, pos - 1);
		if (sidx.empty()) {
			print(log_error, "path-s: invalid syntax in path '%s', empty index between '[]'", "json", path.c_str()); 		  			
			return NULL;
		}
		
		if (json_object_get_type(jso) != json_type_array)
			return NULL;				
		int len = json_object_array_length(jso);
		if (len <= 0)
			return NULL;
			
		int idx = std::stoi(sidx);
		if (idx >= len)
			return NULL;
		if (idx < 0)
			idx = len + idx;
		if (idx < 0)
			return NULL;
		
		jso_next = json_object_array_get_idx(jso, idx);
		if(jso_next == NULL)
			return NULL;
		
		pos = path.find_first_of(".[", 1);
		
		if (pos == std::string::npos)
			return json_path_singleV(jso_next, std::string(), name, allowed_json_types_count, allowed_json_types);
		else				
			return json_path_singleV(jso_next, path.substr(pos), name, allowed_json_types_count, allowed_json_types);		
	} else {
		// invalid path syntax
		print(log_error, "path-s: invalid syntax in path '%s', one of '$.[' at position 0 expected", "json", path.c_str()); 		  			
	}
		
	return NULL;
}

void inspect_json_object(struct json_object *jobject, int nested_level) {   
	if(jobject == NULL)
		return;
	struct json_object_iterator it = json_object_iter_begin(jobject); 
	struct json_object_iterator itEnd = json_object_iter_end(jobject); 
 
    print(log_finest, "inspect: enter level %d", "json", nested_level);
 
    while (!json_object_iter_equal(&it, &itEnd)) {

        const char* jname = json_object_iter_peek_name(&it);
        if(jname == NULL)
			jname = "<null>";
        struct json_object *jvalue = json_object_iter_peek_value(&it);
        json_type jtype = json_object_get_type(jvalue);
 
        /* Drill down if a nested object was found */
        if (jtype == json_type_object) {
			print(log_finest, "inspect: level:%d, key:%s, object", "json", nested_level, jname); 		  			
            inspect_json_object(jvalue, nested_level+1);
        } else if (jtype == json_type_array) {
			int len = json_object_array_length(jvalue);
			print(log_finest, "inspect: level:%d, key:%s, array-len:%d", "json", nested_level, jname, len); 		  
		} else {
			print(log_finest, "inspect: level:%d, key:%s, type:%d", "json", nested_level, jname, jtype); 		  
		}

        json_object_iter_next(&it);
    }
	print(log_finest, "inspect: exit level %d", "json", nested_level); 
}
