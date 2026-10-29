#pragma once
#include "MySqlConnection.hpp"
#include <mysql.h>
#include <array>
#include <memory>
#include <vector>
#include <type_traits>

namespace storage::db {
// All external values travel through bound parameters. Statements/results own no connection.
inline DbStatus sql(MySqlConnection &connection, const std::string &query,
                    const std::vector<std::string> &params,
                    std::vector<std::vector<std::string>> *rows = nullptr) {
    if (rows) rows->clear();
    if (!connection.isOpen()) return {DbCode::NotConnected, "MySQL connection is not open"};
    std::unique_ptr<MYSQL_STMT, decltype(&mysql_stmt_close)> stmt(
        mysql_stmt_init(connection.nativeHandle()), mysql_stmt_close);
    if (!stmt) return {DbCode::DatabaseError, "statement allocation failed"};
    auto error = [&]() -> DbStatus {
        unsigned code = mysql_stmt_errno(stmt.get());
        return {code == 1062 ? DbCode::Conflict : DbCode::DatabaseError,
                "prepared statement failed (MySQL error " + std::to_string(code) + ")"};
    };
    if (mysql_stmt_prepare(stmt.get(), query.data(), query.size())) return error();
    if (mysql_stmt_param_count(stmt.get()) != params.size())
        return {DbCode::InvalidState, "SQL parameter count mismatch"};
    std::vector<MYSQL_BIND> input(params.size());
    std::vector<unsigned long> lengths(params.size());
    for (size_t i = 0; i < params.size(); ++i) {
        lengths[i] = params[i].size();
        input[i].buffer_type = MYSQL_TYPE_STRING;
        input[i].buffer = const_cast<char *>(params[i].data());
        input[i].buffer_length = lengths[i];
        input[i].length = &lengths[i];
    }
    if (!params.empty() && mysql_stmt_bind_param(stmt.get(), input.data())) return error();
    if (mysql_stmt_execute(stmt.get())) return error();
    if (!rows) return {DbCode::Ok, {}};
    unsigned count = mysql_stmt_field_count(stmt.get());
    if (!count) return {DbCode::InvalidState, "query has no result columns"};
    std::vector<std::array<char, 4096>> buffers(count);
    std::vector<MYSQL_BIND> output(count);
    std::vector<unsigned long> sizes(count);
    using NullFlag = std::remove_pointer_t<decltype(MYSQL_BIND{}.is_null)>;
    auto nulls = std::make_unique<NullFlag[]>(count);
    for (unsigned i = 0; i < count; ++i) {
        output[i].buffer_type = MYSQL_TYPE_STRING;
        output[i].buffer = buffers[i].data();
        output[i].buffer_length = buffers[i].size();
        output[i].length = &sizes[i];
        output[i].is_null = &nulls[i];
    }
    if (mysql_stmt_bind_result(stmt.get(), output.data()) || mysql_stmt_store_result(stmt.get())) return error();
    int fetched;
    while ((fetched = mysql_stmt_fetch(stmt.get())) == 0) {
        std::vector<std::string> row;
        for (unsigned i = 0; i < count; ++i) {
            if (nulls[i]) return {DbCode::DatabaseError, "Unexpected NULL SQL result"};
            if (sizes[i] > buffers[i].size()) return {DbCode::DatabaseError, "SQL result too large"};
            row.emplace_back(buffers[i].data(), sizes[i]);
        }
        rows->push_back(std::move(row));
    }
    if (fetched != MYSQL_NO_DATA) { rows->clear(); return error(); }
    return {DbCode::Ok, {}};
}
}
