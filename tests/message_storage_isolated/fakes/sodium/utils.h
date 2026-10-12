#pragma once
#include <cstddef>
#include <string_view>
constexpr int sodium_base64_VARIANT_ORIGINAL=1;
inline int sodium_base642bin(unsigned char* bytes,std::size_t cap,const char* input,std::size_t size,const char*,std::size_t* written,const char** end,int){
 if(std::string_view(input,size)!="AQID"||cap<3)return -1;
 bytes[0]=1;bytes[1]=2;bytes[2]=3;*written=3;*end=input+size;return 0;
}
