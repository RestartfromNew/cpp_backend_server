#pragma once
#include <string>
namespace boost::uuids {
struct uuid {
 std::string text="00000000-0000-0000-0000-000000000000";
 bool is_nil()const{return text=="00000000-0000-0000-0000-000000000000";}
 bool operator==(const uuid&)const=default;
};
}
