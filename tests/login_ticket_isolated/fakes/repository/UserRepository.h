#pragma once
#include <optional>
#include <boost/uuid/uuid.hpp>
struct PasswordRecord{boost::uuids::uuid id{"11111111-1111-4111-8111-111111111111"};std::string password_hash="password";bool is_active=true;std::string email="alice@example.com",display_name="Alice",username="alice_chat01";};
struct UserRepository{bool exists=true;PasswordRecord row;std::optional<PasswordRecord> FindLoginRecordByEmail(const std::string&){return exists?std::optional<PasswordRecord>{row}:std::nullopt;}};
