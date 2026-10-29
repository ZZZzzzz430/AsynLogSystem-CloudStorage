#include "../auth/Crypto.hpp"
#include <iostream>
#include <stdexcept>
using namespace storage::auth;
void check(bool condition,const char *name) {
    if(!condition)throw std::runtime_error(name);std::cout<<"PASS "<<name<<'\n';
}
int main() {
    try {
        // Independently generated with Python hashlib.scrypt, fixed public test vector.
        const std::string encoded="scrypt$131072$8$1$000102030405060708090a0b0c0d0e0f$3da3a7881eb5853fe6d8ed34bf3d9623893abd004a7411e3a520002a03e9f600";
        check(verifyPassword("known-test-password",encoded),"scrypt independent known vector");
        check(!verifyPassword("wrong",encoded),"scrypt wrong password");
        check(!verifyPassword("anything","plaintext"),"plaintext hashes rejected");
        auto a=hashPassword("new-test-password"),b=hashPassword("new-test-password");
        check(a!=b&&verifyPassword("new-test-password",a),"random salts and encoded round trip");
        check(randomHex(32).size()==64&&randomHex(32)!=randomHex(32),"256-bit random tokens");
        check(tokenHash("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA256 known vector");
        return 0;
    }catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
