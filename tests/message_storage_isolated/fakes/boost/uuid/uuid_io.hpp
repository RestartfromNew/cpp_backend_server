#pragma once
#include "uuid.hpp"
namespace boost::uuids {inline std::string to_string(const uuid& id){return id.text;}}
