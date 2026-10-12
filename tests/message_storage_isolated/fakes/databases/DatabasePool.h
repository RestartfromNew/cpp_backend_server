#pragma once
#include "databases/DatabaseError.h"
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <cstdint>
struct FakeResult {
 std::vector<std::vector<std::string>> rows;
 std::size_t rowCount()const{return rows.size();}
 std::string_view value(std::size_t r,std::size_t c)const{return rows.at(r).at(c);}
};
struct FakeRecord {std::string sender,receiver,message,protocol,hex,id;std::int64_t seq;};
struct FakeConnection {
 std::vector<FakeRecord> records;
 bool permission=true,friends=true,sameAccount=false,transaction=false,pairLocked=false;
 int calls=0,transactions=0,commits=0,rollbacks=0,faults=0;
 std::string faultCode;bool commitFault=false;std::string faultConstraint;
 std::string senderUser="11111111-1111-4111-8111-111111111111";
 std::string receiverUser="22222222-2222-4222-8222-222222222222";
 void fault(){if(faults>0){--faults;throw DatabaseError(faultCode.starts_with("08")?DatabaseErrorKind::Connection:DatabaseErrorKind::Query,faultCode,"private SQL details",faultConstraint);}}
 FakeResult execute(std::string_view sql,std::span<const std::string> args={}) {
 ++calls;if(!transaction)throw std::runtime_error("SQL outside transaction");
 if(!commitFault)fault();
 if(sql.starts_with("SET TRANSACTION")){if(!args.empty())throw std::runtime_error("isolation params");return {};}
 if(sql.find("pg_advisory_xact_lock")!=std::string_view::npos){if(args.size()!=2)throw std::runtime_error("lock params");pairLocked=true;return {};}
 if(!pairLocked)throw std::runtime_error("missing pair lock");
 if(sql.find("FROM app.user_devices")!=std::string_view::npos){
 if(args.size()!=3 || sql.find("FOR SHARE OF ud, u")==std::string_view::npos || sql.find("ud.user_id = $1::uuid")==std::string_view::npos)throw std::runtime_error("device authorization SQL");
 if(!permission || args[0]!=senderUser)return {};
 return {{{args[1],senderUser},{args[2],sameAccount?senderUser:receiverUser}}};
 }
 if(sql.find("FROM app.friend_relation")!=std::string_view::npos){if(args.size()!=2||sql.find("FOR SHARE OF fr")==std::string_view::npos)throw std::runtime_error("friend lock SQL");return friends?FakeResult{{{args[0]},{args[1]}}}:FakeResult{};}
 if(sql.find("INSERT INTO app.message_envelopes")!=std::string_view::npos){
 if(args.size()!=5||sql.find("MAX(seq)")==std::string_view::npos||sql.find("RETURNING envelope_id, seq")==std::string_view::npos)throw std::runtime_error("insert SQL");
 std::int64_t seq=1;for(const auto& r:records){if((r.sender==args[0]&&r.receiver==args[1])||(r.sender==args[1]&&r.receiver==args[0]))seq=std::max(seq,r.seq+1);}
 std::string id="eeeeeeee-eeee-4eee-8eee-"+std::string(11,'0')+std::to_string(records.size()+1);
 records.push_back({args[0],args[1],args[2],args[3],args[4],id,seq});return {{{id,std::to_string(seq)}}};
 }
 if(sql.find("SELECT envelope_id, seq")!=std::string_view::npos){if(args.size()!=3)throw std::runtime_error("lookup params");for(const auto& r:records)if(r.sender==args[0]&&r.receiver==args[1]&&r.message==args[2])return {{{r.id,std::to_string(r.seq),r.protocol,r.hex}}};return {};}
 throw std::runtime_error("Unexpected SQL");
 }
 template<class F> auto withTransaction(F f){
 ++transactions;transaction=true;pairLocked=false;auto before=records;
 try{auto result=f();if(commitFault)fault();++commits;transaction=false;pairLocked=false;return result;}
 catch(...){records=before;++rollbacks;transaction=false;pairLocked=false;throw;}
 }
};
struct DatabasePool {FakeConnection connection;};
