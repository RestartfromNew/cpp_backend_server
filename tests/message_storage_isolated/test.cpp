#include "websocket/WebSocketDispatcher.h"
#include "service/ServiceThreadPool.h"
#include "repository/EnvelopeRepository.h"
#include "boost/uuid/uuid_io.hpp"
#include <iostream>
#include <stdexcept>
int main(){
 DatabasePool pool;EnvelopeRepository repo(pool);ChatService service(repo);
 ServiceThreadPool threads(1,service);AuthMiddleWare auth;WebSocketDispatcher dispatcher(threads,auth);
 boost::uuids::uuid user{"11111111-1111-4111-8111-111111111111"};
 boost::uuids::uuid sender{"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"};
 boost::uuids::uuid receiver{"bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb"};
 NewTextMessageRequest request{boost::uuids::uuid{"cccccccc-cccc-4ccc-8ccc-cccccccccccc"},receiver,"TEST",{1,2,3}};
 int cases=0;auto require=[&](bool value){if(!value)throw std::runtime_error("assertion case "+std::to_string(cases));++cases;};
 auto first=service.newTextMessage(user,sender,request);require(first.status==NewTextMessageStatus::Success&&first.envelope->seq==1);
 auto duplicate=service.newTextMessage(user,sender,request);require(duplicate.status==NewTextMessageStatus::Duplicate_request&&duplicate.envelope->envelope_id==first.envelope->envelope_id&&pool.connection.records.size()==1);
 auto changed=request;changed.packet={4};require(service.newTextMessage(user,sender,changed).status==NewTextMessageStatus::Conflict&&pool.connection.records.size()==1);
 changed=request;changed.protocol_version="OTHER";require(service.newTextMessage(user,sender,changed).status==NewTextMessageStatus::Conflict);
 changed=request;changed.message_id.text="dddddddd-dddd-4ddd-8ddd-dddddddddddd";require(service.newTextMessage(user,sender,changed).envelope->seq==2);
 pool.connection.senderUser="22222222-2222-4222-8222-222222222222";pool.connection.receiverUser=user.text;
 changed.recipient_device_id=sender;require(service.newTextMessage({pool.connection.senderUser},receiver,changed).envelope->seq==3);
 pool.connection.senderUser=user.text;pool.connection.receiverUser="22222222-2222-4222-8222-222222222222";
 changed=request;changed.recipient_device_id.text="ffffffff-ffff-4fff-8fff-ffffffffffff";require(service.newTextMessage(user,sender,changed).envelope->seq==1);
 auto count=pool.connection.records.size();pool.connection.permission=false;require(service.newTextMessage(user,sender,request).status==NewTextMessageStatus::PermissionDenied&&pool.connection.records.size()==count);pool.connection.permission=true;
 pool.connection.friends=false;require(service.newTextMessage(user,sender,request).status==NewTextMessageStatus::PermissionDenied);pool.connection.friends=true;
 pool.connection.sameAccount=true;pool.connection.friends=false;require(service.newTextMessage(user,sender,request).status==NewTextMessageStatus::Duplicate_request);pool.connection.sameAccount=false;pool.connection.friends=true;
 require(service.newTextMessage({"99999999-9999-4999-8999-999999999999"},sender,request).status==NewTextMessageStatus::PermissionDenied);
 for(int mode=0;mode<6;++mode){changed=request;if(mode==0)changed.packet.clear();if(mode==1)changed.packet.resize(65537);if(mode==2)changed.protocol_version=" ";if(mode==3)changed.protocol_version=std::string(129,'a');if(mode==4)changed.message_id={};if(mode==5)changed.recipient_device_id=sender;int before=pool.connection.transactions;require(service.newTextMessage(user,sender,changed).status==NewTextMessageStatus::InvalidRequest&&pool.connection.transactions==before);}
 changed=request;changed.message_id.text="12345678-1234-4234-8234-123456789abc";
 for(auto code:{"40001","40P01"}){pool.connection.faultCode=code;pool.connection.faults=1;require(service.newTextMessage(user,sender,changed).status!=NewTextMessageStatus::InternalError);}
 pool.connection.faultCode="40001";pool.connection.faults=3;int before=pool.connection.transactions;require(service.newTextMessage(user,sender,changed).status==NewTextMessageStatus::RetryLater&&pool.connection.transactions==before+3);
 pool.connection.faultCode="23505";pool.connection.faultConstraint="message_envelopes_pkey";pool.connection.faults=1;require(service.newTextMessage(user,sender,changed).status==NewTextMessageStatus::Duplicate_request);
 pool.connection.faultConstraint="envelope_pair_seq_unique";pool.connection.faults=1;require(service.newTextMessage(user,sender,changed).status==NewTextMessageStatus::InternalError);
 pool.connection.faultCode="08006";pool.connection.faults=1;require(service.newTextMessage(user,sender,changed).status==NewTextMessageStatus::DatabaseUnavailable);
 changed.message_id.text="87654321-4321-4321-8321-cba987654321";count=pool.connection.records.size();pool.connection.commitFault=true;pool.connection.faults=1;require(service.newTextMessage(user,sender,changed).status==NewTextMessageStatus::DatabaseUnavailable&&pool.connection.records.size()==count);pool.connection.commitFault=false;
 nlohmann::json payload={{"type","chat.newTextMessage"},{"authorization","Bearer valid"},{"sender_device_id",sender.text},{"message_id","13572468-1234-4234-8234-123456789abc"},{"recipient_device_id",receiver.text},{"protocol_version","TEST"},{"packet_base64","AQID"}};
 auto dispatch=[&](nlohmann::json frame){nlohmann::json result;int replies=0;dispatcher.dispatch(std::move(frame),[&](nlohmann::json r){result=std::move(r);++replies;});if(replies!=1)throw std::runtime_error("reply count");return result;};
 auto wrap=[](const nlohmann::json& b){return nlohmann::json{{"payload_format","json"},{"payload",b}};};
 auto result=dispatch(wrap(payload));require(result["success"]==true&&result["seq"].is_number_integer());require(dispatch(wrap(payload))["duplicate"]==true);
 for(auto field:{"sender_device_id","message_id","recipient_device_id","protocol_version","packet_base64"}){auto b=payload;b.erase(field);require(dispatch(wrap(b))["error"]["code"]=="invalid_request");b=payload;b[field]=12;require(dispatch(wrap(b))["error"]["code"]=="invalid_request");}
 auto b=payload;b["packet_base64"]="!!!!";require(dispatch(wrap(b))["error"]["code"]=="invalid_request");
 for(auto token:{"Bearer expired","Bearer bad"}){b=payload;b["authorization"]=token;require(dispatch(wrap(b))["success"]==false);}
 b=payload;b.erase("authorization");require(dispatch(wrap(b))["error"]["code"]=="missing_authorization");
 b=payload;b["type"]="unknown";require(dispatch(wrap(b))["error"]["code"]=="unsupported_type");
 b=payload;b["type"]=12;require(dispatch(wrap(b))["error"]["code"]=="invalid_request");
 require(dispatch(nlohmann::json::array())["error"]["code"]=="invalid_payload");require(dispatch({{"payload_format","bytes"},{"payload",payload}})["error"]["code"]=="invalid_payload");
 pool.connection.permission=false;require(dispatch(wrap(payload))["error"]["code"]=="send_permission_denied");pool.connection.permission=true;
 b=payload;b["protocol_version"]="OTHER";require(dispatch(wrap(b))["error"]["code"]=="envelope_conflict");
 threads.close();require(dispatch(wrap(payload))["error"]["code"]=="server_busy");
 std::cout<<"PASS: "<<cases<<" actual Dispatcher/Service/Repository cases; database, UUID, token and Base64 dependencies simulated.\n";
}
