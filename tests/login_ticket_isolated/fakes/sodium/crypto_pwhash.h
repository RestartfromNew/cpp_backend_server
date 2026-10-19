#pragma once
#include <string>
inline int crypto_pwhash_str_verify(const char* hash,const char* text,unsigned long long size){return std::string(hash)==std::string(text,static_cast<std::size_t>(size))?0:-1;}
