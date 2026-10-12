#pragma once
#include "uuid.hpp"
#include <stdexcept>
namespace boost::uuids {struct string_generator {uuid operator()(const std::string& s)const {
 if(s.size()!=36)throw std::invalid_argument("uuid");
 for(char c:s)if(c!='-' && !(c>='0'&&c<='9') && !(c>='a'&&c<='f') && !(c>='A'&&c<='F'))throw std::invalid_argument("uuid");
 return {s};
}};}
