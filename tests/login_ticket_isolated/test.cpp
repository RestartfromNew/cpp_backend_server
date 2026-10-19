#include "service/LoginService.h"
#include <future>
#include <iostream>
int main(){
 DatabasePool pool;KeyRepository keys;LoginTicketRepository repo(pool,keys);AccessTokenService access;RefreshTokenService refresh;LoginTicketService tickets(repo,access,refresh);UserRepository users;LoginService login(users,tickets);
 const boost::uuids::uuid user{"11111111-1111-4111-8111-111111111111"},device{"bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb"};
 const auto deviceText=boost::uuids::to_string(device);pool.connection.devices[deviceText]={boost::uuids::to_string(user),"active"};
 int checks=0;auto require=[&](bool v){if(!v)throw std::runtime_error("check "+std::to_string(checks));++checks;};
 require(login.loginByEmail("u","bad").login==LoginError::InvalidCredentials&&pool.connection.tickets.empty());
 users.exists=false;require(login.loginByEmail("u","password").login==LoginError::NoUserFound);users.exists=true;users.row.is_active=false;require(login.loginByEmail("u","password").login==LoginError::NotActiveUser);users.row.is_active=true;
 auto l=login.loginByEmail("u","password");require(l.login==LoginError::Success&&l.login_ticket&&l.login_ticket->size()==64&&pool.connection.tokens==0);require(!pool.connection.tickets.contains(*l.login_ticket));
 auto done=tickets.verifyDevice(*l.login_ticket,device);require(done.result.status==LoginTicketStatus::Success&&!done.access_token.empty()&&!done.refresh_token.empty()&&pool.connection.tokens==1);require(tickets.verifyDevice(*l.login_ticket,device).result.status==LoginTicketStatus::InvalidTicket);
 require(tickets.verifyDevice("bad",device).result.status==LoginTicketStatus::InvalidTicket);
 auto issue=[&]{return *tickets.issue(user);};auto key=[](const std::string& ticket){return "hashed:login-ticket-v1:"+ticket;};
 auto t=issue();pool.connection.tickets[key(t)].expired=true;require(tickets.verifyDevice(t,device).result.status==LoginTicketStatus::InvalidTicket);
 t=issue();pool.connection.expireAfterRead=true;require(tickets.verifyDevice(t,device).result.status==LoginTicketStatus::InvalidTicket);pool.connection.expireAfterRead=false;
 t=issue();pool.connection.devices[deviceText].user="other";require(tickets.verifyDevice(t,device).result.status==LoginTicketStatus::DeviceNotOwned&&!pool.connection.tickets[key(t)].used);pool.connection.devices[deviceText].user=boost::uuids::to_string(user);pool.connection.devices[deviceText].status="revoked";require(tickets.verifyDevice(t,device).result.status==LoginTicketStatus::DeviceNotActive);pool.connection.devices[deviceText].status="active";
 boost::uuids::uuid missing{"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"};require(tickets.verifyDevice(t,missing).result.status==LoginTicketStatus::DeviceNotFound);pool.connection.active=false;require(tickets.verifyDevice(t,device).result.status==LoginTicketStatus::AccountDisabled);pool.connection.active=true;
 pool.connection.failInsert=true;try{tickets.verifyDevice(t,device);throw std::logic_error("expected insert failure");}catch(const std::runtime_error&){}require(!pool.connection.tickets[key(t)].used);pool.connection.failInsert=false;
 pool.connection.failCommit=true;try{tickets.verifyDevice(t,device);}catch(const std::runtime_error&){}require(!pool.connection.tickets[key(t)].used);pool.connection.failCommit=false;
 auto f1=std::async(std::launch::async,[&]{return tickets.verifyDevice(t,device).result.status;});auto f2=std::async(std::launch::async,[&]{return tickets.verifyDevice(t,device).result.status;});auto a=f1.get(),b=f2.get();require((a==LoginTicketStatus::Success&&b==LoginTicketStatus::InvalidTicket)||(b==LoginTicketStatus::Success&&a==LoginTicketStatus::InvalidTicket));
 DeviceRegistrationInput input{"key","phone","TEST","42","prekey","signature"};t=issue();int before=pool.connection.newDevices;pool.connection.failInsert=true;try{tickets.registerDevice(t,input);}catch(const std::runtime_error&){}require(pool.connection.newDevices==before&&!pool.connection.tickets[key(t)].used);pool.connection.failInsert=false;
 done=tickets.registerDevice(t,input);require(done.result.status==LoginTicketStatus::Success&&pool.connection.newDevices==before+1&&!done.refresh_token.empty());require(tickets.verifyDevice(t,device).result.status==LoginTicketStatus::InvalidTicket);
 require(tickets.registerDevice(t,input).result.status==LoginTicketStatus::InvalidTicket);
 std::cout<<"PASS "<<checks<<" login-ticket checks, actual LoginService/TicketService/TicketRepository; simulated DB and crypto, including concurrent single-use and rollback\n";
}
