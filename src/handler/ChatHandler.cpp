//
// Created by yangb on 2026/10/8.
//

#include "handler/ChatHandler.h"
#include "databases/DatabaseError.h"
#include "nlohmann/json.hpp"

#include <algorithm>
#include <boost/uuid/string_generator.hpp>
#include <limits>
#include <unordered_map>
#include <stdexcept>
#include <charconv>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>

namespace {
bool isHexOfSize(std::string_view text, std::size_t byteCount) {
    if (text.size() != byteCount * 2) {
        return false;
    }
    return std::all_of(text.begin(), text.end(), [](unsigned char c) {
        return (c >= '0' && c <= '9') ||
               (c >= 'a' && c <= 'f') ||
               (c >= 'A' && c <= 'F');
    });
}
}

ChatHandler::ChatHandler(KeyService& keyService) : keyService_(keyService) {}

HttpResponse ChatHandler::registerNewDevice(
    const HttpRequest& request, const boost::uuids::uuid& user_id) {
    if (request.body.empty()) {
        return ErrorResponseMaker(HttpStatus::Bad_Request,
            "missing_request_body", "Request body is required");
    }
    const auto body = nlohmann::json::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        return ErrorResponseMaker(HttpStatus::Bad_Request,
            "invalid_json", "Request body must be a valid JSON object");
    }

    const char* requiredFields[] = {
        "identity_public_key_hex", "device_name", "protocol_suite",
        "signed_prekey_id", "signed_prekey_public_hex",
        "signed_prekey_signature_hex"
    };
    for (const auto* field : requiredFields) {
        const auto value = body.find(field);
        if (value == body.end() || !value->is_string() ||
            value->get_ref<const std::string&>().empty()) {
            return ErrorResponseMaker(HttpStatus::Bad_Request,
                std::string{"invalid_"} + field,
                std::string{field} + " must be a non-empty string");
        }
    }

    const auto& identityKey = body.at("identity_public_key_hex").get_ref<const std::string&>();
    const auto& deviceName = body.at("device_name").get_ref<const std::string&>();
    const auto& protocolSuite = body.at("protocol_suite").get_ref<const std::string&>();
    const auto& prekeyId = body.at("signed_prekey_id").get_ref<const std::string&>();
    const auto& prekeyPublic = body.at("signed_prekey_public_hex").get_ref<const std::string&>();
    const auto& prekeySignature = body.at("signed_prekey_signature_hex").get_ref<const std::string&>();

    if (deviceName.find_first_not_of(" \t\r\n") == std::string::npos) {
        return ErrorResponseMaker(HttpStatus::Bad_Request,
            "invalid_device_name", "device_name must not be blank");
    }
    if (protocolSuite.find_first_not_of(" \t\r\n") == std::string::npos) {
        return ErrorResponseMaker(HttpStatus::Bad_Request,
            "invalid_protocol_suite", "protocol_suite must not be blank");
    }
    if (!isHexOfSize(identityKey, 32)) {
        return ErrorResponseMaker(HttpStatus::Bad_Request,
            "invalid_identity_public_key_hex", "identity_public_key_hex must encode 32 bytes");
    }
    if (!isHexOfSize(prekeyPublic, 32)) {
        return ErrorResponseMaker(HttpStatus::Bad_Request,
            "invalid_signed_prekey_public_hex", "signed_prekey_public_hex must encode 32 bytes");
    }
    if (!isHexOfSize(prekeySignature, 64)) {
        return ErrorResponseMaker(HttpStatus::Bad_Request,
            "invalid_signed_prekey_signature_hex", "signed_prekey_signature_hex must encode 64 bytes");
    }
    std::int64_t numericPrekeyId = 0;
    const auto [end, parseError] = std::from_chars(
        prekeyId.data(), prekeyId.data() + prekeyId.size(), numericPrekeyId);
    const bool digitsOnly = std::all_of(prekeyId.begin(), prekeyId.end(),
        [](unsigned char c) { return c >= '0' && c <= '9'; });
    if (!digitsOnly || parseError != std::errc{} ||
        end != prekeyId.data() + prekeyId.size() || numericPrekeyId < 0) {
        return ErrorResponseMaker(HttpStatus::Bad_Request,
            "invalid_signed_prekey_id", "signed_prekey_id must be a non-negative BIGINT decimal string");
    }

    try {
        // The authenticated route supplies user_id; never use a body-supplied ID.
        const auto deviceId = keyService_.registerNewDevice(
            user_id, identityKey, deviceName, protocolSuite,
            prekeyId, prekeyPublic, prekeySignature);
        HttpResponse response;
        response.status = HttpStatus::Created;
        response.headers["Content-Type"] = "application/json";
        response.body = nlohmann::json{
            {"device_id", deviceId}, {"device_status", "active"}
        }.dump();
        return response;
    } catch (const DatabaseError& error) {
        std::cerr << "[registerNewDevice] database failure; SQLSTATE="
                  << error.code() << '\n';
        if (error.kind() == DatabaseErrorKind::Connection) {
            return ErrorResponseMaker(HttpStatus::Service_Unavailable,
                "database_unavailable", "Service temporarily unavailable");
        }
        return ErrorResponseMaker(HttpStatus::Internal_Server_Error,
            "register_device_failed", "Unable to register device");
    } catch (const std::exception&) {
        std::cerr << "[registerNewDevice] unexpected failure\n";
        return ErrorResponseMaker(HttpStatus::Internal_Server_Error,
            "register_device_failed", "Unable to register device");
    }
}

HttpResponse ChatHandler::uploadOneTimePrekeys(
    const HttpRequest& request, const boost::uuids::uuid& user_id) {
    if (request.body.empty()) {
        return ErrorResponseMaker(HttpStatus::Bad_Request,
            "missing_request_body", "Request body is required");
    }
    const auto body = nlohmann::json::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        return ErrorResponseMaker(HttpStatus::Bad_Request,
            "invalid_json", "Request body must be a valid JSON object");
    }
    const auto deviceField = body.find("device_id");
    if (deviceField == body.end() || !deviceField->is_string()) {
        return ErrorResponseMaker(HttpStatus::Bad_Request,
            "invalid_device_id", "device_id must be a UUID string");
    }
    const auto& deviceText = deviceField->get_ref<const std::string&>();
    boost::uuids::uuid deviceId;
    try {
        if (deviceText.size() != 36 || deviceText[8] != '-' ||
            deviceText[13] != '-' || deviceText[18] != '-' || deviceText[23] != '-') {
            throw std::invalid_argument("Invalid UUID format");
        }
        deviceId = boost::uuids::string_generator{}(deviceText);
    } catch (const std::exception&) {
        return ErrorResponseMaker(HttpStatus::Bad_Request,
            "invalid_device_id", "device_id must be a UUID string");
    }
    const auto batch = body.find("prekeys");
    if (batch == body.end() || !batch->is_array() || batch->empty() || batch->size() > 10) {
        return ErrorResponseMaker(HttpStatus::Bad_Request,
            "invalid_prekeys", "prekeys must be an array containing 1 to 10 entries");
    }
    std::unordered_map<int, std::string> prekeys;
    for (const auto& entry : *batch) {
        if (!entry.is_object()) {
            return ErrorResponseMaker(HttpStatus::Bad_Request,
                "invalid_prekey", "Each prekey must be an object");
        }
        const auto id = entry.find("prekey_id");
        const auto key = entry.find("public_key_hex");
        if (id == entry.end() || !id->is_number_integer() ||
            (id->is_number_unsigned() ? id->get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<int>::max())
                                     : id->get<std::int64_t>() < 0 || id->get<std::int64_t>() > std::numeric_limits<int>::max())) {
            return ErrorResponseMaker(HttpStatus::Bad_Request,
                "invalid_prekey_id", "prekey_id must be an integer from 0 to 2147483647");
        }
        if (key == entry.end() || !key->is_string() ||
            !isHexOfSize(key->get_ref<const std::string&>(), 32)) {
            return ErrorResponseMaker(HttpStatus::Bad_Request,
                "invalid_public_key_hex", "public_key_hex must encode 32 bytes");
        }
        if (!prekeys.emplace(id->get<int>(), key->get<std::string>()).second) {
            return ErrorResponseMaker(HttpStatus::Bad_Request,
                "duplicate_prekey_id", "prekey_id must be unique within the batch");
        }
    }
    try {
        if (!keyService_.uploadOneTimePrekeys(user_id, deviceId, prekeys)) {
            return ErrorResponseMaker(HttpStatus::Forbidden,
                "device_unavailable", "Device must belong to the authenticated user and be active");
        }
        HttpResponse response;
        response.status = HttpStatus::Created;
        response.headers["Content-Type"] = "application/json";
        response.body = nlohmann::json{
            {"device_id", deviceText}, {"uploaded_count", prekeys.size()}
        }.dump();
        return response;
    } catch (const DatabaseError& error) {
        std::cerr << "[uploadOneTimePrekeys] database failure; SQLSTATE=" << error.code() << '\n';
        if (error.code() == "23505") {
            return ErrorResponseMaker(HttpStatus::Conflict,
                "prekey_already_exists", "A prekey ID already exists for this device");
        }
        if (error.kind() == DatabaseErrorKind::Connection) {
            return ErrorResponseMaker(HttpStatus::Service_Unavailable,
                "database_unavailable", "Service temporarily unavailable");
        }
        return ErrorResponseMaker(HttpStatus::Internal_Server_Error,
            "upload_prekeys_failed", "Unable to upload one-time prekeys");
    } catch (const std::exception&) {
        std::cerr << "[uploadOneTimePrekeys] unexpected failure\n";
        return ErrorResponseMaker(HttpStatus::Internal_Server_Error,
            "upload_prekeys_failed", "Unable to upload one-time prekeys");
    }
}

HttpResponse ChatHandler::getOneTimePrekey(
    const HttpRequest& request, const boost::uuids::uuid& user_id) {
    if (request.body.empty()) {
        return ErrorResponseMaker(HttpStatus::Bad_Request,
            "missing_request_body", "Request body is required");
    }
    const auto body = nlohmann::json::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        return ErrorResponseMaker(HttpStatus::Bad_Request,
            "invalid_json", "Request body must be a valid JSON object");
    }
    boost::uuids::uuid friendId, deviceId;
    for (const auto* field : {"friend_id", "device_id"}) {
        const auto value = body.find(field);
        if (value == body.end() || !value->is_string()) {
            return ErrorResponseMaker(HttpStatus::Bad_Request,
                std::string{"invalid_"} + field, std::string{field} + " must be a UUID string");
        }
        const auto& text = value->get_ref<const std::string&>();
        try {
            if (text.size() != 36 || text[8] != '-' || text[13] != '-' ||
                text[18] != '-' || text[23] != '-') {
                throw std::invalid_argument("Invalid UUID format");
            }
            const auto id = boost::uuids::string_generator{}(text);
            if (std::string_view{field} == "friend_id") {
                friendId = id;
            } else {
                deviceId = id;
            }
        } catch (const std::exception&) {
            return ErrorResponseMaker(HttpStatus::Bad_Request,
                std::string{"invalid_"} + field, std::string{field} + " must be a UUID string");
        }
    }
    try {
        const auto result = keyService_.getOneTimePrekey(user_id, friendId, deviceId);
        switch (result.status) {
            case PrekeyClaimStatus::NotFriends:
                return ErrorResponseMaker(HttpStatus::Forbidden,
                    "not_friends", "Target user must be a friend of the authenticated user");
            case PrekeyClaimStatus::DeviceUnavailable:
                return ErrorResponseMaker(HttpStatus::Forbidden,
                    "device_unavailable", "Target device must belong to the friend and be active");
            case PrekeyClaimStatus::NoPrekey:
                return ErrorResponseMaker(HttpStatus::Not_Found,
                    "prekey_unavailable", "No one-time prekey is currently available");
            case PrekeyClaimStatus::Success:
                break;
        }
        if (!result.prekey) {
            throw std::runtime_error("Missing claimed prekey");
        }
        HttpResponse response;
        response.status = HttpStatus::Ok;
        response.headers["Content-Type"] = "application/json";
        response.body = nlohmann::json{
            {"friend_id", boost::uuids::to_string(friendId)},
            {"device_id", boost::uuids::to_string(result.prekey->device_id)},
            {"prekey_id", result.prekey->prekey_id},
            {"public_key_hex", result.prekey->public_key_hex}
        }.dump();
        return response;
    } catch (const DatabaseError& error) {
        std::cerr << "[getOneTimePrekey] database failure; SQLSTATE=" << error.code() << '\n';
        if (error.kind() == DatabaseErrorKind::Connection) {
            return ErrorResponseMaker(HttpStatus::Service_Unavailable,
                "database_unavailable", "Service temporarily unavailable");
        }
        return ErrorResponseMaker(HttpStatus::Internal_Server_Error,
            "claim_prekey_failed", "Unable to claim one-time prekey");
    } catch (const std::exception&) {
        std::cerr << "[getOneTimePrekey] unexpected failure\n";
        return ErrorResponseMaker(HttpStatus::Internal_Server_Error,
            "claim_prekey_failed", "Unable to claim one-time prekey");
    }
}

HttpResponse ChatHandler::fetchFriendDevices(
    const HttpRequest& request, const boost::uuids::uuid& user_id) {
    if (request.body.empty()) {
        return ErrorResponseMaker(HttpStatus::Bad_Request,
            "missing_request_body", "Request body is required");
    }
    const auto body = nlohmann::json::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        return ErrorResponseMaker(HttpStatus::Bad_Request,
            "invalid_json", "Request body must be a valid JSON object");
    }
    const auto field = body.find("friend_id");
    if (field == body.end() || !field->is_string()) {
        return ErrorResponseMaker(HttpStatus::Bad_Request,
            "invalid_friend_id", "friend_id must be a UUID string");
    }
    boost::uuids::uuid friendId;
    const auto& text = field->get_ref<const std::string&>();
    try {
        if (text.size() != 36 || text[8] != '-' || text[13] != '-' ||
            text[18] != '-' || text[23] != '-') {
            throw std::invalid_argument("Invalid UUID format");
        }
        friendId = boost::uuids::string_generator{}(text);
    } catch (const std::exception&) {
        return ErrorResponseMaker(HttpStatus::Bad_Request,
            "invalid_friend_id", "friend_id must be a UUID string");
    }
    try {
        const auto result = keyService_.fetchFriendDevices(user_id, friendId);
        if (!result) {
            return ErrorResponseMaker(HttpStatus::Forbidden,
                "not_friends", "Target user must be a friend of the authenticated user");
        }
        auto devices = nlohmann::json::array();
        constexpr char hexDigits[] = "0123456789abcdef";
        for (const auto& device : *result) {
            std::string identityHex;
            identityHex.reserve(device.identity_public_key.size() * 2);
            for (const auto byte : device.identity_public_key) {
                identityHex.push_back(hexDigits[byte >> 4]);
                identityHex.push_back(hexDigits[byte & 0x0f]);
            }
            devices.push_back({
                {"device_id", boost::uuids::to_string(device.device_id)},
                {"device_name", device.device_name}, {"status", "active"},
                {"identity_public_key_hex", identityHex},
                {"protocol_suite", device.protocol_suite}
            });
        }
        HttpResponse response;
        response.status = HttpStatus::Ok;
        response.headers["Content-Type"] = "application/json";
        response.body = nlohmann::json{
            {"friend_id", boost::uuids::to_string(friendId)},
            {"devices", std::move(devices)}
        }.dump();
        return response;
    } catch (const DatabaseError& error) {
        std::cerr << "[fetchFriendDevices] database failure; SQLSTATE=" << error.code() << '\n';
        if (error.kind() == DatabaseErrorKind::Connection) {
            return ErrorResponseMaker(HttpStatus::Service_Unavailable,
                "database_unavailable", "Service temporarily unavailable");
        }
        return ErrorResponseMaker(HttpStatus::Internal_Server_Error,
            "fetch_friend_devices_failed", "Unable to fetch friend devices");
    } catch (const std::exception&) {
        std::cerr << "[fetchFriendDevices] unexpected failure\n";
        return ErrorResponseMaker(HttpStatus::Internal_Server_Error,
            "fetch_friend_devices_failed", "Unable to fetch friend devices");
    }
}

HttpResponse ChatHandler::getDeviceKeyBundle(
    const HttpRequest& request, const boost::uuids::uuid& user_id) {
    if (request.body.empty()) {
        return ErrorResponseMaker(HttpStatus::Bad_Request,
            "missing_request_body", "Request body is required");
    }
    const auto body = nlohmann::json::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        return ErrorResponseMaker(HttpStatus::Bad_Request,
            "invalid_json", "Request body must be a valid JSON object");
    }
    boost::uuids::uuid friendId, deviceId;
    for (const auto* field : {"friend_id", "device_id"}) {
        const auto value = body.find(field);
        if (value == body.end() || !value->is_string()) {
            return ErrorResponseMaker(HttpStatus::Bad_Request,
                std::string{"invalid_"} + field, std::string{field} + " must be a UUID string");
        }
        const auto& text = value->get_ref<const std::string&>();
        try {
            if (text.size() != 36 || text[8] != '-' || text[13] != '-' ||
                text[18] != '-' || text[23] != '-') {
                throw std::invalid_argument("Invalid UUID format");
            }
            const auto id = boost::uuids::string_generator{}(text);
            if (std::string_view{field} == "friend_id") {
                friendId = id;
            } else {
                deviceId = id;
            }
        } catch (const std::exception&) {
            return ErrorResponseMaker(HttpStatus::Bad_Request,
                std::string{"invalid_"} + field, std::string{field} + " must be a UUID string");
        }
    }
    try {
        const auto result = keyService_.getDeviceKeyBundle(friendId, user_id, deviceId);
        if (!result) {
            return ErrorResponseMaker(HttpStatus::Not_Found,
                "key_bundle_unavailable", "Target device key bundle is unavailable");
        }
        const auto toHex = [](const std::vector<std::uint8_t>& bytes) {
            constexpr char digits[] = "0123456789abcdef";
            std::string hex;
            hex.reserve(bytes.size() * 2);
            for (const auto byte : bytes) {
                hex.push_back(digits[byte >> 4]);
                hex.push_back(digits[byte & 0x0f]);
            }
            return hex;
        };
        HttpResponse response;
        response.status = HttpStatus::Ok;
        response.headers["Content-Type"] = "application/json";
        response.body = nlohmann::json{
            {"friend_id", boost::uuids::to_string(friendId)},
            {"device_id", boost::uuids::to_string(result->device_id)},
            {"signed_prekey_id", result->signed_preKey_id},
            {"signed_prekey_public_hex", toHex(result->preKey_public)},
            {"signed_prekey_signature_hex", toHex(result->preKey_signature)},
            {"is_current", result->is_current}
        }.dump();
        return response;
    } catch (const DatabaseError& error) {
        std::cerr << "[getDeviceKeyBundle] database failure; SQLSTATE=" << error.code() << '\n';
        if (error.kind() == DatabaseErrorKind::Connection) {
            return ErrorResponseMaker(HttpStatus::Service_Unavailable,
                "database_unavailable", "Service temporarily unavailable");
        }
        return ErrorResponseMaker(HttpStatus::Internal_Server_Error,
            "fetch_key_bundle_failed", "Unable to fetch device key bundle");
    } catch (const std::exception&) {
        std::cerr << "[getDeviceKeyBundle] unexpected failure\n";
        return ErrorResponseMaker(HttpStatus::Internal_Server_Error,
            "fetch_key_bundle_failed", "Unable to fetch device key bundle");
    }
}
