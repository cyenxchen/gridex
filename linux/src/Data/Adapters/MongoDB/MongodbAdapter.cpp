#include "Data/Adapters/MongoDB/MongodbAdapter.h"

#include <chrono>
#include <mutex>
#include <sstream>

#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/builder/basic/kvp.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/types.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/database.hpp>
#include <mongocxx/exception/exception.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/uri.hpp>

#include "Core/Errors/GridexError.h"

namespace gridex {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

// Convert a bsoncxx element value to a RowValue.
RowValue bsonElementToRowValue(const bsoncxx::document::element& elem) {
    using bsoncxx::type;
    switch (elem.type()) {
        case type::k_string:
            return RowValue::makeString(std::string(elem.get_string().value));
        case type::k_int32:
            return RowValue::makeInteger(static_cast<std::int64_t>(elem.get_int32().value));
        case type::k_int64:
            return RowValue::makeInteger(elem.get_int64().value);
        case type::k_double:
            return RowValue::makeDouble(elem.get_double().value);
        case type::k_bool:
            return RowValue::makeBoolean(elem.get_bool().value);
        case type::k_null:
            return RowValue::makeNull();
        case type::k_oid:
            return RowValue::makeString(elem.get_oid().value.to_string());
        case type::k_date:
            // Milliseconds since epoch → human-readable string
            return RowValue::makeString(std::to_string(
                elem.get_date().value.count()) + "ms");
        case type::k_document:
            return RowValue::makeJson(bsoncxx::to_json(elem.get_document().value));
        case type::k_array:
            return RowValue::makeJson(bsoncxx::to_json(elem.get_array().value));
        case type::k_undefined:
        case type::k_binary:
        case type::k_regex:
        case type::k_dbpointer:
        case type::k_code:
        case type::k_symbol:
        case type::k_codewscope:
        case type::k_timestamp:
        case type::k_decimal128:
        case type::k_maxkey:
        case type::k_minkey:
        default:
            // Serialize complex/unknown types as JSON string
            return RowValue::makeString(bsoncxx::to_json(
                bsoncxx::builder::basic::make_document(
                    bsoncxx::builder::basic::kvp("v", elem.get_value()))));
    }
}

// Determine the BSON type name as a string for column metadata.
std::string bsonTypeName(bsoncxx::type t) {
    using bsoncxx::type;
    switch (t) {
        case type::k_string:    return "string";
        case type::k_int32:     return "int32";
        case type::k_int64:     return "int64";
        case type::k_double:    return "double";
        case type::k_bool:      return "boolean";
        case type::k_null:      return "null";
        case type::k_oid:       return "objectId";
        case type::k_date:      return "date";
        case type::k_document:  return "document";
        case type::k_array:     return "array";
        case type::k_binary:    return "binary";
        case type::k_regex:     return "regex";
        case type::k_timestamp: return "timestamp";
        case type::k_decimal128:return "decimal128";
        default:                return "unknown";
    }
}

// Convert a RowValue to a BSON value appended into a builder document.
void appendRowValueToBson(bsoncxx::builder::basic::document& doc,
                          const std::string& key,
                          const RowValue& v) {
    using namespace bsoncxx::builder::basic;
    if (v.isNull()) {
        doc.append(kvp(key, bsoncxx::types::b_null{}));
    } else if (v.isString()) {
        doc.append(kvp(key, v.asString()));
    } else if (v.isInteger()) {
        doc.append(kvp(key, v.asInteger()));
    } else if (v.isDouble()) {
        doc.append(kvp(key, v.asDouble()));
    } else if (v.isBoolean()) {
        doc.append(kvp(key, v.asBoolean()));
    } else if (v.isJson()) {
        // Parse JSON string back to BSON document
        try {
            auto bson = bsoncxx::from_json(v.asJson());
            doc.append(kvp(key, bsoncxx::types::b_document{bson.view()}));
        } catch (...) {
            doc.append(kvp(key, v.asJson()));
        }
    } else {
        // Fallback: store as string representation
        doc.append(kvp(key, v.description()));
    }
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Pimpl struct holding mongocxx objects
// ---------------------------------------------------------------------------

struct MongodbAdapter::Impl {
    std::unique_ptr<mongocxx::client> client;

    mongocxx::database getDb(const std::string& dbName) {
        if (!client) throw ConnectionError("MongoDB client is not initialized");
        return (*client)[dbName];
    }
};

// ---------------------------------------------------------------------------
// mongocxx::instance singleton guard
// ---------------------------------------------------------------------------

namespace {

void ensureMongocxxInstance() {
    static std::once_flag flag;
    std::call_once(flag, []() {
        // mongocxx::instance must be created exactly once per process.
        // Storing in a static local ensures lifetime until program exit.
        static mongocxx::instance inst{};
        (void)inst;
    });
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

MongodbAdapter::MongodbAdapter()
    : impl_(std::make_unique<Impl>()) {
    ensureMongocxxInstance();
}

MongodbAdapter::~MongodbAdapter() {
    std::lock_guard lock(mutex_);
    impl_->client.reset();
    connected_.store(false, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// Connection lifecycle
// ---------------------------------------------------------------------------

void MongodbAdapter::connect(const ConnectionConfig& config,
                              const std::optional<std::string>& password) {
    std::lock_guard lock(mutex_);

    const std::string host = config.host.value_or("localhost");
    const int port = config.port.value_or(27017);

    // Build URI: mongodb://[username:password@]host:port[/database]
    std::string uri = "mongodb://";
    if (config.username && !config.username->empty()) {
        uri += *config.username;
        if (password && !password->empty()) uri += ":" + *password;
        uri += "@";
    }
    uri += host + ":" + std::to_string(port);

    const std::string dbName = config.database.value_or("admin");
    if (!dbName.empty()) uri += "/" + dbName;

    // Add server-selection timeout (5 s) so connect() fails fast
    uri += "?serverSelectionTimeoutMS=5000&connectTimeoutMS=5000";

    try {
        mongocxx::uri mongoUri(uri);
        impl_->client = std::make_unique<mongocxx::client>(mongoUri);

        // Force an actual connection attempt by pinging the server
        auto db = (*impl_->client)[dbName];
        using namespace bsoncxx::builder::basic;
        auto pingCmd = make_document(kvp("ping", 1));
        db.run_command(pingCmd.view());

        currentDb_ = dbName;
        connected_.store(true, std::memory_order_release);
    } catch (const mongocxx::exception& ex) {
        impl_->client.reset();
        throw ConnectionError(std::string("MongoDB connect failed: ") + ex.what());
    }
}

void MongodbAdapter::disconnect() {
    std::lock_guard lock(mutex_);
    impl_->client.reset();
    connected_.store(false, std::memory_order_release);
    currentDb_.clear();
}

bool MongodbAdapter::testConnection(const ConnectionConfig& config,
                                     const std::optional<std::string>& password) {
    MongodbAdapter probe;
    probe.connect(config, password);
    probe.disconnect();
    return true;
}

void MongodbAdapter::ensureConnected() const {
    if (!connected_.load(std::memory_order_acquire) || !impl_->client)
        throw QueryError("Not connected to MongoDB");
}

// ---------------------------------------------------------------------------
// Query execution — treat query/sql as a JSON command document
// ---------------------------------------------------------------------------

QueryResult MongodbAdapter::execute(const std::string& query,
                                     const std::vector<QueryParameter>& /*parameters*/) {
    return executeRaw(query);
}

QueryResult MongodbAdapter::executeRaw(const std::string& sql) {
    ensureConnected();
    const auto start = std::chrono::steady_clock::now();

    std::lock_guard lock(mutex_);
    try {
        auto db = impl_->getDb(currentDb_);
        auto cmdDoc = bsoncxx::from_json(sql);
        auto result = db.run_command(cmdDoc.view());

        QueryResult qr;
        qr.queryType = QueryType::Other;
        qr.executionTime = std::chrono::steady_clock::now() - start;

        // Present the command result as a single row with one "result" column
        ColumnHeader h;
        h.name = "result";
        h.dataType = "document";
        qr.columns.push_back(std::move(h));

        std::vector<RowValue> row;
        row.push_back(RowValue::makeJson(bsoncxx::to_json(result.view())));
        qr.rows.push_back(std::move(row));
        qr.rowsAffected = 1;
        return qr;
    } catch (const mongocxx::exception& ex) {
        throw QueryError(std::string("MongoDB command failed: ") + ex.what());
    } catch (const std::exception& ex) {
        // bsoncxx::from_json throws bsoncxx::exception (a std::system_error);
        // catch via std::exception base to avoid potential macro-expansion issues.
        throw QueryError(std::string("MongoDB BSON parse error: ") + ex.what());
    }
}

// ---------------------------------------------------------------------------
// Schema inspection
// ---------------------------------------------------------------------------

std::vector<std::string> MongodbAdapter::listDatabases() {
    ensureConnected();
    std::lock_guard lock(mutex_);
    try {
        return impl_->client->list_database_names();
    } catch (const mongocxx::exception& ex) {
        throw QueryError(std::string("MongoDB listDatabases failed: ") + ex.what());
    }
}

std::vector<std::string> MongodbAdapter::listSchemas(const std::optional<std::string>& /*database*/) {
    // MongoDB has no schema concept; return a single "default" namespace
    return {"default"};
}

std::vector<TableInfo> MongodbAdapter::listTables(const std::optional<std::string>& /*schema*/) {
    ensureConnected();
    std::lock_guard lock(mutex_);
    try {
        auto db = impl_->getDb(currentDb_);
        auto names = db.list_collection_names();
        std::vector<TableInfo> out;
        out.reserve(names.size());
        for (const auto& name : names) {
            TableInfo t;
            t.name = name;
            t.schema = "default";
            t.type = TableKind::Table;
            out.push_back(std::move(t));
        }
        return out;
    } catch (const mongocxx::exception& ex) {
        throw QueryError(std::string("MongoDB listTables failed: ") + ex.what());
    }
}

std::vector<ViewInfo> MongodbAdapter::listViews(const std::optional<std::string>& /*schema*/) {
    // MongoDB views exist but require a separate listCollections filter; not
    // implemented — return empty to avoid unnecessary complexity (YAGNI).
    return {};
}

TableDescription MongodbAdapter::describeTable(const std::string& name,
                                                const std::optional<std::string>& schema) {
    ensureConnected();
    std::lock_guard lock(mutex_);

    TableDescription desc;
    desc.name = name;
    desc.schema = schema;

    try {
        auto db = impl_->getDb(currentDb_);
        auto coll = db[name];

        // Sample the first document to infer the schema
        auto cursor = coll.find({}, mongocxx::options::find{}.limit(1));
        int pos = 1;
        for (const auto& doc : cursor) {
            for (const auto& elem : doc) {
                ColumnInfo col;
                col.name = std::string(elem.key());
                col.dataType = bsonTypeName(elem.type());
                col.isNullable = true;
                col.ordinalPosition = pos++;
                if (col.name == "_id") {
                    col.isPrimaryKey = true;
                    col.isNullable = false;
                }
                desc.columns.push_back(std::move(col));
            }
            break; // only the first document
        }
        // If collection is empty or no fields found, add a placeholder _id column
        if (desc.columns.empty()) {
            ColumnInfo idCol;
            idCol.name = "_id";
            idCol.dataType = "objectId";
            idCol.isNullable = false;
            idCol.isPrimaryKey = true;
            idCol.ordinalPosition = 1;
            desc.columns.push_back(std::move(idCol));
        }
    } catch (const mongocxx::exception& ex) {
        throw QueryError(std::string("MongoDB describeTable failed: ") + ex.what());
    }
    return desc;
}

std::vector<IndexInfo> MongodbAdapter::listIndexes(const std::string& table,
                                                    const std::optional<std::string>& /*schema*/) {
    ensureConnected();
    std::lock_guard lock(mutex_);
    std::vector<IndexInfo> out;
    try {
        auto db = impl_->getDb(currentDb_);
        auto coll = db[table];
        auto cursor = coll.list_indexes();
        for (const auto& idxDoc : cursor) {
            IndexInfo idx;
            // name field
            if (idxDoc["name"] && idxDoc["name"].type() == bsoncxx::type::k_string)
                idx.name = std::string(idxDoc["name"].get_string().value);
            // unique field
            if (idxDoc["unique"] && idxDoc["unique"].type() == bsoncxx::type::k_bool)
                idx.isUnique = idxDoc["unique"].get_bool().value;
            // key field → column names
            if (idxDoc["key"] && idxDoc["key"].type() == bsoncxx::type::k_document) {
                for (const auto& kv : idxDoc["key"].get_document().value)
                    idx.columns.push_back(std::string(kv.key()));
            }
            idx.tableName = table;
            out.push_back(std::move(idx));
        }
    } catch (const mongocxx::exception& ex) {
        throw QueryError(std::string("MongoDB listIndexes failed: ") + ex.what());
    }
    return out;
}

std::vector<ForeignKeyInfo> MongodbAdapter::listForeignKeys(
    const std::string& /*table*/,
    const std::optional<std::string>& /*schema*/) {
    // MongoDB has no foreign keys
    return {};
}

std::vector<std::string> MongodbAdapter::listFunctions(const std::optional<std::string>& /*schema*/) {
    return {};
}

std::string MongodbAdapter::getFunctionSource(const std::string& /*name*/,
                                               const std::optional<std::string>& /*schema*/) {
    return {};
}

// ---------------------------------------------------------------------------
// Data manipulation
// ---------------------------------------------------------------------------

QueryResult MongodbAdapter::insertRow(const std::string& table,
                                       const std::optional<std::string>& /*schema*/,
                                       const std::unordered_map<std::string, RowValue>& values) {
    ensureConnected();
    const auto start = std::chrono::steady_clock::now();
    std::lock_guard lock(mutex_);

    try {
        auto db = impl_->getDb(currentDb_);
        auto coll = db[table];

        bsoncxx::builder::basic::document docBuilder;
        for (const auto& [key, val] : values)
            appendRowValueToBson(docBuilder, key, val);

        auto result = coll.insert_one(docBuilder.view());

        QueryResult qr;
        qr.queryType = QueryType::Insert;
        qr.rowsAffected = result ? 1 : 0;
        qr.executionTime = std::chrono::steady_clock::now() - start;

        ColumnHeader h;
        h.name = "insertedId";
        h.dataType = "objectId";
        qr.columns.push_back(std::move(h));
        if (result && result->inserted_id().type() == bsoncxx::type::k_oid) {
            std::vector<RowValue> row;
            row.push_back(RowValue::makeString(result->inserted_id().get_oid().value.to_string()));
            qr.rows.push_back(std::move(row));
        }
        return qr;
    } catch (const mongocxx::exception& ex) {
        throw QueryError(std::string("MongoDB insertRow failed: ") + ex.what());
    }
}

QueryResult MongodbAdapter::updateRow(const std::string& table,
                                       const std::optional<std::string>& /*schema*/,
                                       const std::unordered_map<std::string, RowValue>& set,
                                       const std::unordered_map<std::string, RowValue>& where) {
    ensureConnected();
    const auto start = std::chrono::steady_clock::now();
    std::lock_guard lock(mutex_);

    try {
        auto db = impl_->getDb(currentDb_);
        auto coll = db[table];

        bsoncxx::builder::basic::document filterDoc;
        for (const auto& [key, val] : where)
            appendRowValueToBson(filterDoc, key, val);

        bsoncxx::builder::basic::document setDoc;
        bsoncxx::builder::basic::document updateDoc;
        for (const auto& [key, val] : set)
            appendRowValueToBson(setDoc, key, val);
        updateDoc.append(bsoncxx::builder::basic::kvp("$set", setDoc.view()));

        auto result = coll.update_one(filterDoc.view(), updateDoc.view());

        QueryResult qr;
        qr.queryType = QueryType::Update;
        qr.rowsAffected = result ? static_cast<int>(result->modified_count()) : 0;
        qr.executionTime = std::chrono::steady_clock::now() - start;
        return qr;
    } catch (const mongocxx::exception& ex) {
        throw QueryError(std::string("MongoDB updateRow failed: ") + ex.what());
    }
}

QueryResult MongodbAdapter::deleteRow(const std::string& table,
                                       const std::optional<std::string>& /*schema*/,
                                       const std::unordered_map<std::string, RowValue>& where) {
    ensureConnected();
    const auto start = std::chrono::steady_clock::now();
    std::lock_guard lock(mutex_);

    try {
        auto db = impl_->getDb(currentDb_);
        auto coll = db[table];

        bsoncxx::builder::basic::document filterDoc;
        for (const auto& [key, val] : where)
            appendRowValueToBson(filterDoc, key, val);

        auto result = coll.delete_one(filterDoc.view());

        QueryResult qr;
        qr.queryType = QueryType::Delete;
        qr.rowsAffected = result ? static_cast<int>(result->deleted_count()) : 0;
        qr.executionTime = std::chrono::steady_clock::now() - start;
        return qr;
    } catch (const mongocxx::exception& ex) {
        throw QueryError(std::string("MongoDB deleteRow failed: ") + ex.what());
    }
}

// ---------------------------------------------------------------------------
// Transactions (MongoDB 4.0+ replica-set sessions)
// ---------------------------------------------------------------------------

void MongodbAdapter::beginTransaction() {
    // Transactions require a client session and replica set; throw a clear
    // error rather than silently ignoring — callers need to know.
    throw QueryError("MongoDB transactions require a replica-set session; "
                     "use executeRaw with a JSON command to manage sessions manually");
}

void MongodbAdapter::commitTransaction() {
    throw QueryError("MongoDB transactions require a replica-set session");
}

void MongodbAdapter::rollbackTransaction() {
    throw QueryError("MongoDB transactions require a replica-set session");
}

// ---------------------------------------------------------------------------
// Pagination
// ---------------------------------------------------------------------------

QueryResult MongodbAdapter::fetchRows(const std::string& table,
                                       const std::optional<std::string>& /*schema*/,
                                       const std::optional<std::vector<std::string>>& columns,
                                       const std::optional<FilterExpression>& /*where*/,
                                       const std::optional<std::vector<QuerySortDescriptor>>& /*orderBy*/,
                                       int limit, int offset) {
    ensureConnected();
    const auto start = std::chrono::steady_clock::now();
    std::lock_guard lock(mutex_);

    QueryResult qr;
    qr.queryType = QueryType::Select;

    try {
        auto db = impl_->getDb(currentDb_);
        auto coll = db[table];

        // Build optional projection from requested columns
        mongocxx::options::find opts;
        if (columns && !columns->empty()) {
            bsoncxx::builder::basic::document projDoc;
            for (const auto& col : *columns)
                projDoc.append(bsoncxx::builder::basic::kvp(col, 1));
            opts.projection(projDoc.view());
        }
        opts.skip(static_cast<std::int64_t>(offset));
        opts.limit(static_cast<std::int64_t>(limit));

        auto cursor = coll.find({}, opts);

        bool headerBuilt = false;
        for (const auto& doc : cursor) {
            // Build column headers from the first document
            if (!headerBuilt) {
                for (const auto& elem : doc) {
                    ColumnHeader h;
                    h.name = std::string(elem.key());
                    h.dataType = bsonTypeName(elem.type());
                    qr.columns.push_back(std::move(h));
                }
                headerBuilt = true;
            }

            // Build row aligned to the header column order
            std::vector<RowValue> row;
            row.reserve(qr.columns.size());
            for (const auto& col : qr.columns) {
                const auto elem = doc[col.name];
                if (elem) {
                    row.push_back(bsonElementToRowValue(elem));
                } else {
                    row.push_back(RowValue::makeNull());
                }
            }
            qr.rows.push_back(std::move(row));
        }

        // If collection was empty, add a minimal _id column so the caller
        // gets a valid (but empty) result rather than a headerless table.
        if (!headerBuilt) {
            ColumnHeader h;
            h.name = "_id";
            h.dataType = "objectId";
            qr.columns.push_back(std::move(h));
        }
    } catch (const mongocxx::exception& ex) {
        throw QueryError(std::string("MongoDB fetchRows failed: ") + ex.what());
    }

    qr.rowsAffected = static_cast<int>(qr.rows.size());
    qr.executionTime = std::chrono::steady_clock::now() - start;
    return qr;
}

// ---------------------------------------------------------------------------
// Server info
// ---------------------------------------------------------------------------

std::string MongodbAdapter::serverVersion() {
    ensureConnected();
    std::lock_guard lock(mutex_);
    try {
        auto db = impl_->getDb(currentDb_);
        using namespace bsoncxx::builder::basic;
        auto cmd = make_document(kvp("buildInfo", 1));
        auto result = db.run_command(cmd.view());

        const auto versionElem = result.view()["version"];
        if (versionElem && versionElem.type() == bsoncxx::type::k_string)
            return "MongoDB " + std::string(versionElem.get_string().value);
        return "MongoDB";
    } catch (const mongocxx::exception& ex) {
        throw QueryError(std::string("MongoDB serverVersion failed: ") + ex.what());
    }
}

std::optional<std::string> MongodbAdapter::currentDatabase() {
    if (currentDb_.empty()) return std::nullopt;
    return currentDb_;
}

// ---------------------------------------------------------------------------
// Document-level operations for MongoCollectionView
// ---------------------------------------------------------------------------

std::vector<std::string> MongodbAdapter::findDocuments(
    const std::string& collection,
    const std::string& filterJson,
    int limit, int skip) {
    ensureConnected();
    std::lock_guard lock(mutex_);
    try {
        auto db = impl_->getDb(currentDb_);
        auto coll = db[collection];

        bsoncxx::document::value filterDoc = filterJson.empty()
            ? bsoncxx::from_json("{}")
            : bsoncxx::from_json(filterJson);

        mongocxx::options::find opts;
        opts.skip(static_cast<std::int64_t>(skip));
        opts.limit(static_cast<std::int64_t>(limit));

        auto cursor = coll.find(filterDoc.view(), opts);
        std::vector<std::string> out;
        for (const auto& doc : cursor)
            out.push_back(bsoncxx::to_json(doc));
        return out;
    } catch (const std::exception& ex) {
        throw QueryError(std::string("MongoDB findDocuments failed: ") + ex.what());
    }
}

std::string MongodbAdapter::insertDocument(const std::string& collection,
                                            const std::string& json) {
    ensureConnected();
    std::lock_guard lock(mutex_);
    try {
        auto db = impl_->getDb(currentDb_);
        auto coll = db[collection];
        auto docVal = bsoncxx::from_json(json);
        auto result = coll.insert_one(docVal.view());
        if (result && result->inserted_id().type() == bsoncxx::type::k_oid)
            return result->inserted_id().get_oid().value.to_string();
        return {};
    } catch (const std::exception& ex) {
        throw QueryError(std::string("MongoDB insertDocument failed: ") + ex.what());
    }
}

void MongodbAdapter::updateDocument(const std::string& collection,
                                     const std::string& idJson,
                                     const std::string& json) {
    ensureConnected();
    std::lock_guard lock(mutex_);
    try {
        auto db = impl_->getDb(currentDb_);
        auto coll = db[collection];
        auto filterDoc = bsoncxx::from_json(idJson);
        auto newDoc = bsoncxx::from_json(json);
        using namespace bsoncxx::builder::basic;
        auto updateDoc = make_document(kvp("$set", newDoc.view()));
        coll.update_one(filterDoc.view(), updateDoc.view());
    } catch (const std::exception& ex) {
        throw QueryError(std::string("MongoDB updateDocument failed: ") + ex.what());
    }
}

void MongodbAdapter::deleteDocument(const std::string& collection,
                                     const std::string& idJson) {
    ensureConnected();
    std::lock_guard lock(mutex_);
    try {
        auto db = impl_->getDb(currentDb_);
        auto coll = db[collection];
        auto filterDoc = bsoncxx::from_json(idJson);
        coll.delete_one(filterDoc.view());
    } catch (const std::exception& ex) {
        throw QueryError(std::string("MongoDB deleteDocument failed: ") + ex.what());
    }
}

long MongodbAdapter::countDocuments(const std::string& collection,
                                     const std::string& filterJson) {
    ensureConnected();
    std::lock_guard lock(mutex_);
    try {
        auto db = impl_->getDb(currentDb_);
        auto coll = db[collection];
        bsoncxx::document::value filterDoc = filterJson.empty()
            ? bsoncxx::from_json("{}")
            : bsoncxx::from_json(filterJson);
        return static_cast<long>(coll.count_documents(filterDoc.view()));
    } catch (const std::exception& ex) {
        throw QueryError(std::string("MongoDB countDocuments failed: ") + ex.what());
    }
}

} // namespace gridex
