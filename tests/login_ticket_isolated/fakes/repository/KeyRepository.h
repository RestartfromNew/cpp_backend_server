#pragma once
#include "databases/DatabasePool.h"
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
struct Device{boost::uuids::uuid device_id;};
struct KeyRepository{
 Device RegisterNewDevice(DatabaseConnection& c,const boost::uuids::uuid& user,const std::string&,const std::string&,const std::string&,const std::string&,const std::string&,const std::string&){if(!c.transaction)throw std::runtime_error("registration outside transaction");boost::uuids::uuid id{"dddddddd-dddd-4ddd-8ddd-dddddddddddd"};c.devices[boost::uuids::to_string(id)]={boost::uuids::to_string(user),"active"};++c.newDevices;return {id};}
};
