#pragma once
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <stdexcept>
struct TestRows {std::vector<std::vector<std::string>> rows;bool isNull(std::size_t,std::size_t)const{return false;}std::size_t rowCount()const{return rows.size();}std::string_view value(std::size_t r,std::size_t c)const{return rows.at(r).at(c);}};
struct TestTicket {std::string user;bool expired=false,used=false;};
struct TestDevice {std::string user,status="active";};
struct DatabaseConnection {
 std::mutex mutex;std::map<std::string,TestTicket> tickets;std::map<std::string,TestDevice> devices;
 bool active=true,failInsert=false,failCommit=false,expireAfterRead=false;int tokens=0,newDevices=0;
 bool transaction=false;
 template<class F>auto withTransaction(F f){std::lock_guard lock(mutex);auto oldTickets=tickets;auto oldDevices=devices;auto oldTokens=tokens;auto oldNew=newDevices;transaction=true;try{auto result=f();if(failCommit)throw std::runtime_error("commit");transaction=false;return result;}catch(...){tickets=oldTickets;devices=oldDevices;tokens=oldTokens;newDevices=oldNew;transaction=false;throw;}}
 TestRows execute(std::string_view sql,std::span<const std::string> p = {}){
  if(!transaction)throw std::runtime_error("not in transaction");
  if(sql.find("SELECT id FROM app.users")!=sql.npos){if(sql.find("FOR SHARE")==sql.npos)throw std::runtime_error("account lock missing");return active?TestRows{{{p[0]}}}:TestRows{};}
  if(sql.find("INSERT INTO app.login_tickets")!=sql.npos){tickets[p[1]]={p[0]};return {{{p[1]}}};}
  if(sql.find("SELECT user_id FROM app.login_tickets")!=sql.npos){if(sql.find("FOR UPDATE")==sql.npos||sql.find("clock_timestamp()")==sql.npos)throw std::runtime_error("ticket lock/expiry missing");auto it=tickets.find(p[0]);if(it==tickets.end()||it->second.used||it->second.expired)return {};auto user=it->second.user;if(expireAfterRead)it->second.expired=true;return {{{user}}};}
  if(sql.find("SELECT user_id, status FROM app.user_devices")!=sql.npos){if(sql.find("FOR SHARE")==sql.npos)throw std::runtime_error("device lock missing");auto it=devices.find(p[0]);return it==devices.end()?TestRows{}:TestRows{{{it->second.user,it->second.status}}};}
  if(sql.find("UPDATE app.login_tickets")!=sql.npos){auto& ticket=tickets.at(p[0]);if(ticket.used||ticket.expired)return {};ticket.used=true;return {{{p[0]}}};}
  if(sql.find("INSERT INTO app.refresh_tokens")!=sql.npos){if(failInsert)throw std::runtime_error("insert");if(p.size()!=3||devices.at(p[1]).user!=p[0]||devices.at(p[1]).status!="active")throw std::runtime_error("unbound token");++tokens;return {{{"token"}}};}
  throw std::runtime_error("unexpected SQL");
 }
};
struct DatabasePool {DatabaseConnection connection;};
