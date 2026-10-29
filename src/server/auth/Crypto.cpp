#include "Crypto.hpp"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <array>
#include <stdexcept>
#include <vector>
namespace storage::auth {
namespace {
std::string hex(const unsigned char *bytes, size_t count) {
    const char *digits="0123456789abcdef";
    std::string out; out.reserve(count*2);
    for (size_t i=0;i<count;++i) {out+=digits[bytes[i]>>4];out+=digits[bytes[i]&15];}
    return out;
}
bool unhex(const std::string &s, unsigned char *out, size_t n) {
    if(s.size()!=n*2) return false;
    auto digit=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
    for(size_t i=0;i<n;++i) {int a=digit(s[2*i]),b=digit(s[2*i+1]);if(a<0||b<0)return false;out[i]=(a<<4)|b;}
    return true;
}
std::array<unsigned char,32> derive(const std::string &password,const unsigned char *salt) {
    std::array<unsigned char,32> result{};
    if(EVP_PBE_scrypt(password.data(),password.size(),salt,16,131072,8,1,256ULL*1024*1024,result.data(),result.size())!=1)
        throw std::runtime_error("Password derivation failed");
    return result;
}
const std::string prefix="scrypt$131072$8$1$";
}
std::string randomHex(unsigned bytes) {
    if(bytes>1024) throw std::runtime_error("Random size invalid");
    std::vector<unsigned char> data(bytes);
    if(RAND_bytes(data.data(),bytes)!=1) throw std::runtime_error("Secure random failed");
    return hex(data.data(),data.size());
}
std::string tokenHash(const std::string &token) {
    unsigned char digest[EVP_MAX_MD_SIZE];unsigned length=0;
    if(EVP_Digest(token.data(),token.size(),digest,&length,EVP_sha256(),nullptr)!=1)
        throw std::runtime_error("Token digest failed");
    return hex(digest,length);
}
std::string hashPassword(const std::string &password) {
    if(password.empty()||password.size()>1024) throw std::runtime_error("Password length invalid");
    std::string saltHex=randomHex(16);unsigned char salt[16];unhex(saltHex,salt,16);
    auto key=derive(password,salt);auto encoded=prefix+saltHex+"$"+hex(key.data(),key.size());
    OPENSSL_cleanse(key.data(),key.size());return encoded;
}
bool verifyPassword(const std::string &password,const std::string &encoded) {
    unsigned char salt[16]{};std::array<unsigned char,32> expected{};
    bool valid=encoded.size()==prefix.size()+32+1+64 && encoded.compare(0,prefix.size(),prefix)==0;
    if(valid) valid=encoded[prefix.size()+32]=='$' && unhex(encoded.substr(prefix.size(),32),salt,16)
        && unhex(encoded.substr(prefix.size()+33),expected.data(),32);
    // Missing users and unsupported hash encodings still perform one derivation.
    auto actual=derive(password,salt);
    bool matched=CRYPTO_memcmp(actual.data(),expected.data(),32)==0;
    OPENSSL_cleanse(actual.data(),actual.size());return valid&&matched;
}
}
