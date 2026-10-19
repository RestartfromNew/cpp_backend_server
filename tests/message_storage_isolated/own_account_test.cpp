#include "service/ChatService.h"
#include "repository/EnvelopeRepository.h"
#include <iostream>
#include <stdexcept>

// Compile with the existing fake database/UUID headers first on the include path.
// This tests real Service/Repository control flow, not PostgreSQL or encryption.
int main() {
    DatabasePool pool;
    EnvelopeRepository repository(pool);
    ChatService service(repository);
    const boost::uuids::uuid user{"11111111-1111-4111-8111-111111111111"};
    const boost::uuids::uuid sender{"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"};
    const boost::uuids::uuid recipient{"bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb"};
    NewTextMessageRequest request{
        boost::uuids::uuid{"cccccccc-cccc-4ccc-8ccc-cccccccccccc"},
        recipient, "TEST", {1, 2, 3}};
    int cases = 0;
    const auto require = [&](bool condition) {
        if (!condition) throw std::runtime_error("Failed case " + std::to_string(cases + 1));
        ++cases;
    };
    pool.connection.sameAccount = true;
    pool.connection.friends = false;
    const auto first = service.newTextMessage(user, sender, request);
    require(first.status == NewTextMessageStatus::Success && first.envelope->seq == 1);
    const auto duplicate = service.newTextMessage(user, sender, request);
    require(duplicate.status == NewTextMessageStatus::Duplicate_request &&
        duplicate.envelope->envelope_id == first.envelope->envelope_id &&
        pool.connection.records.size() == 1);
    auto changed = request;
    changed.packet = {4};
    require(service.newTextMessage(user, sender, changed).status == NewTextMessageStatus::Conflict);
    changed = request;
    changed.recipient_device_id = sender;
    require(service.newTextMessage(user, sender, changed).status == NewTextMessageStatus::InvalidRequest);
    pool.connection.permission = false;
    require(service.newTextMessage(user, sender, request).status == NewTextMessageStatus::PermissionDenied);
    pool.connection.permission = true;
    require(service.newTextMessage({"99999999-9999-4999-8999-999999999999"}, sender, request).status ==
        NewTextMessageStatus::PermissionDenied);
    pool.connection.sameAccount = false;
    require(service.newTextMessage(user, sender, request).status == NewTextMessageStatus::PermissionDenied);
    pool.connection.friends = true;
    require(service.newTextMessage(user, sender, request).status == NewTextMessageStatus::Duplicate_request);
    require(pool.connection.records.size() == 1);
    std::cout << "PASS: " << cases << " own-account envelope cases (simulated database).\n";
}
