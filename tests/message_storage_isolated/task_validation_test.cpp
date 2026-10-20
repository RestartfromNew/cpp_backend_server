#include "websocket/WebSocketDispatcher.h"
#include "service/ServiceThreadPool.h"
#include <thread>
#include <iostream>
int main(){
 DatabasePool pool;EnvelopeRepository repository(pool);ChatService service(repository);ServiceThreadPool threads(1,service);AuthMiddleWare auth;SessionIdentityService identity;WebSocketDispatcher dispatcher(threads,auth,identity);
 boost::uuids::uuid user{"11111111-1111-4111-8111-111111111111"},device{"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"};
 int checks=0;auto check=[&](bool v){if(!v)throw std::runtime_error("check "+std::to_string(checks));++checks;};
 std::vector<nlohmann::json> replies;auto call=[&](nlohmann::json p){replies.clear();dispatcher.dispatch(device,user,{{"header",nlohmann::json::object()},{"payload_format","json"},{"payload",p}},[&](auto r){replies.push_back(std::move(r));});};
 auto drain=[&]{std::thread worker([&]{threads.init();});worker.join();};
 auto payload=[&](const char* type){return nlohmann::json{{"type",type},{"authorization","Bearer valid"},{"sender_device_id",device.text},{"device_id",device.text},{"id","test"},{"message","hello"}};};
 for(auto type:{"chat.newTextMessage_Test","chat.newTextMessage","chat.sync.envelope","chat.envelopes.ack"}){
  identity.state=IdentityState::NotActivated;int before=identity.calls;call(payload(type));check(replies.empty()&&identity.calls==before);drain();check(replies.size()==1&&replies[0]["error"]["code"]=="device_do_not_match_session"&&identity.calls==before+1);check(identity.last_thread!=std::this_thread::get_id());
 }
 identity.state=IdentityState::Success;
 for(auto code:{"08006","40001","40P01","XX000"}){identity.fault_code=code;call(payload("chat.sync.envelope"));check(replies.empty());drain();check(replies.size()==1&&replies[0]["type"]=="error"&&replies[0]["error"]["code"]==(std::string(code)=="08006"?"database_unavailable":std::string(code)=="XX000"?"internal_server_error":"retry_later"));}
 identity.fault_code.clear();identity.unexpected=true;call(payload("chat.envelopes.ack"));drain();check(replies.size()==1&&replies[0]["error"]["code"]=="internal_server_error");identity.unexpected=false;
 auto p=payload("chat.newTextMessage");p["sender_device_id"]="bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";call(p);check(replies.empty());drain();check(replies.size()==1&&replies[0]["error"]["code"]=="session_device_mismatch"&&pool.connection.calls==0);
 p=payload("chat.newTextMessage_Test");p["authorization"]="Bearer expired";int before=identity.calls;call(p);check(replies.size()==1&&replies[0]["error"]["code"]=="access_token_expired"&&identity.calls==before);
 p=payload("chat.newTextMessage_Test");call(p);check(replies.empty());drain();check(replies.size()==1&&replies[0]["success"]==true);
 p=payload("chat.newTextMessage");p["message_id"]="cccccccc-cccc-4ccc-8ccc-cccccccccccc";p["recipient_device_id"]="bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";p["protocol_version"]="TEST";p["packet_base64"]="AQID";call(p);check(replies.empty());drain();check(replies.size()==2&&replies[0]["type"]=="Online_Sync"&&replies[1]["type"]=="newTextMessage.result"&&replies[1]["success"]==true);
 threads.close();before=identity.calls;call(payload("chat.newTextMessage_Test"));check(replies.size()==1&&replies[0]["error"]["code"]=="server_busy"&&identity.calls==before);
 std::cout<<"PASS "<<checks<<" Task validation checks: deferred DB checks on another thread, failures, binding, normal receipts/push and queue rejection (simulated dependencies)\n";
}
