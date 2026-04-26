#pragma once

#include <stdexcept>
#include <string>
#include <utility>

namespace gridex {

enum class ErrorCategory {
    Unknown,
    Connection,
    Query,
    Schema,
    Authentication,
    Permission,
    Configuration,
    Serialization,
    Network,
    Internal,
};

class GridexError : public std::runtime_error {
public:
    GridexError(ErrorCategory category, std::string message)
        : std::runtime_error(std::move(message)), category_(category) {}

    [[nodiscard]] ErrorCategory category() const noexcept { return category_; }

private:
    ErrorCategory category_;
};

class ConnectionError : public GridexError {
public:
    explicit ConnectionError(std::string message)
        : GridexError(ErrorCategory::Connection, std::move(message)) {}
};

class QueryError : public GridexError {
public:
    explicit QueryError(std::string message)
        : GridexError(ErrorCategory::Query, std::move(message)) {}
};

class SchemaError : public GridexError {
public:
    explicit SchemaError(std::string message)
        : GridexError(ErrorCategory::Schema, std::move(message)) {}
};

class AuthenticationError : public GridexError {
public:
    explicit AuthenticationError(std::string message)
        : GridexError(ErrorCategory::Authentication, std::move(message)) {}
};

class PermissionError : public GridexError {
public:
    explicit PermissionError(std::string message)
        : GridexError(ErrorCategory::Permission, std::move(message)) {}
};

class ConfigurationError : public GridexError {
public:
    explicit ConfigurationError(std::string message)
        : GridexError(ErrorCategory::Configuration, std::move(message)) {}
};

class SerializationError : public GridexError {
public:
    explicit SerializationError(std::string message)
        : GridexError(ErrorCategory::Serialization, std::move(message)) {}
};

class NetworkError : public GridexError {
public:
    explicit NetworkError(std::string message)
        : GridexError(ErrorCategory::Network, std::move(message)) {}
};

class InternalError : public GridexError {
public:
    explicit InternalError(std::string message)
        : GridexError(ErrorCategory::Internal, std::move(message)) {}
};

}
