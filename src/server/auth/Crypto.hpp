#pragma once
#include <string>
namespace storage::auth {
std::string randomHex(unsigned bytes);
std::string tokenHash(const std::string &token);
std::string hashPassword(const std::string &password);
bool verifyPassword(const std::string &password, const std::string &encoded);
}
