#ifndef _Json_hpp_
#define _Json_hpp_

#include <json-c/json.h>
#include <shared_ptr.hpp>
#include <stdarg.h>
#include <string>

class Json {
  public:
	typedef vz::shared_ptr<Json> Ptr;

	Json(struct json_object *jso) : _jso(jso){};
	~Json() { _jso = NULL; };

	struct json_object *Object() {
		return _jso;
	}

  private:
	struct json_object *_jso;
};

struct json_object * json_path_single(struct json_object *jso, const char *path, int allowed_json_types_count = 0, ...);
struct json_object * json_path_single(struct json_object *jso, const char *path, std::string* name, int allowed_json_types_count = 0, ...);
struct json_object * json_path_singleV(struct json_object *jso, const std::string &path, std::string* name, int allowed_json_types_count, va_list allowed_json_types);

void inspect_json_object(struct json_object *jobject, int nested_level = 0);

#endif /* _Json_hpp_ */
