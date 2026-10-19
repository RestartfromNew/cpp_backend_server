#pragma once
#include <optional>
#include <boost/uuid/uuid.hpp>
struct PasswordRecord{boost::uuids::uuid id{"11111111-1111-4111-8111-111111111111"};std::string password_hash="password";bool is_active=true;};
struct UserRepository{bool exists=true;PasswordRecord row;std::optional<PasswordRecord> FindLoginRecordByEmail(const std::string&){return exists?std::optional<PasswordRecord>{row}:std::nullopt;}};
