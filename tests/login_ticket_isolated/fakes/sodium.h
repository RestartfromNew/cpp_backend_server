#pragma once
#include <cstddef>
#include <atomic>
inline void randombytes_buf(void* data,std::size_t n){static std::atomic<unsigned> serial{0};auto v=++serial;auto* p=static_cast<unsigned char*>(data);for(std::size_t i=0;i<n;++i)p[i]=static_cast<unsigned char>(v+i);}
inline char* sodium_bin2hex(char* out,std::size_t,const unsigned char* bytes,std::size_t n){const char* d="0123456789abcdef";for(std::size_t i=0;i<n;++i){out[i*2]=d[bytes[i]>>4];out[i*2+1]=d[bytes[i]&15];}out[n*2]=0;return out;}
