// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Storage.h"

#include <sqlite3.h>

#include <cstdlib>
#include <ctime>

#ifdef __HAIKU__
#include <FindDirectory.h>
#include <Path.h>
#endif

#include <sys/stat.h>
#include <sys/types.h>

namespace meshy {

// ─── Stmt ────────────────────────────────────────────────────────

Stmt::Stmt(sqlite3* db, const std::string& sql) : fDb(db), fStmt(NULL)
{
    if (db == NULL)
        return;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &fStmt, NULL) != SQLITE_OK)
        fStmt = NULL;
}

Stmt::~Stmt()
{
    if (fStmt != NULL)
        sqlite3_finalize(fStmt);
}

Stmt& Stmt::BindNull(int i)
{
    if (fStmt) sqlite3_bind_null(fStmt, i);
    return *this;
}

Stmt& Stmt::BindInt(int i, int64_t v)
{
    if (fStmt) sqlite3_bind_int64(fStmt, i, v);
    return *this;
}

Stmt& Stmt::BindDouble(int i, double v)
{
    if (fStmt) sqlite3_bind_double(fStmt, i, v);
    return *this;
}

Stmt& Stmt::BindText(int i, const std::string& v)
{
    if (fStmt)
        sqlite3_bind_text(fStmt, i, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
    return *this;
}

Stmt& Stmt::BindBlob(int i, const ByteBuffer& v)
{
    if (!fStmt)
        return *this;
    if (v.empty())
        sqlite3_bind_zeroblob(fStmt, i, 0);
    else
        sqlite3_bind_blob(fStmt, i, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
    return *this;
}

Stmt& Stmt::BindTime(int i, double t)
{
    return HasTime(t) ? BindDouble(i, t) : BindNull(i);
}

bool Stmt::Step()
{
    if (!fStmt)
        return false;
    return sqlite3_step(fStmt) == SQLITE_ROW;
}

bool Stmt::Exec()
{
    if (!fStmt)
        return false;
    int rc = sqlite3_step(fStmt);
    return rc == SQLITE_DONE || rc == SQLITE_ROW;
}

int64_t Stmt::ColInt(int i) const { return fStmt ? sqlite3_column_int64(fStmt, i) : 0; }
double Stmt::ColDouble(int i) const { return fStmt ? sqlite3_column_double(fStmt, i) : 0.0; }
bool Stmt::ColIsNull(int i) const
{
    return !fStmt || sqlite3_column_type(fStmt, i) == SQLITE_NULL;
}
double Stmt::ColTime(int i) const { return ColIsNull(i) ? kNoTime : ColDouble(i); }

std::string Stmt::ColText(int i) const
{
    if (!fStmt)
        return std::string();
    const unsigned char* p = sqlite3_column_text(fStmt, i);
    if (p == NULL)
        return std::string();
    int n = sqlite3_column_bytes(fStmt, i);
    return std::string(reinterpret_cast<const char*>(p), static_cast<size_t>(n));
}

ByteBuffer Stmt::ColBlob(int i) const
{
    if (!fStmt)
        return ByteBuffer();
    const void* p = sqlite3_column_blob(fStmt, i);
    int n = sqlite3_column_bytes(fStmt, i);
    if (p == NULL || n <= 0)
        return ByteBuffer();
    const uint8_t* b = static_cast<const uint8_t*>(p);
    return ByteBuffer(b, b + n);
}

// ─── Storage ─────────────────────────────────────────────────────

Storage::Storage() : fDb(NULL), fBatchDepth(0) {}

Storage::~Storage() { Close(); }

void Storage::_Fail(const std::string& what)
{
    fLastError = what;
    if (fDb != NULL) {
        const char* msg = sqlite3_errmsg(fDb);
        if (msg != NULL) {
            fLastError += ": ";
            fLastError += msg;
        }
    }
}

std::string Storage::DefaultPath()
{
#ifdef __HAIKU__
    // Haiku keeps per-user application data under the settings directory.
    BPath path;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
        path.Append("Meshy");
        mkdir(path.Path(), 0755);
        path.Append("meshy.db");
        return std::string(path.Path());
    }
    return std::string("/boot/home/config/settings/Meshy/meshy.db");
#else
    const char* xdg = getenv("XDG_DATA_HOME");
    std::string base;
    if (xdg != NULL && xdg[0] != '\0') {
        base = xdg;
    } else {
        const char* home = getenv("HOME");
        base = std::string(home != NULL ? home : ".") + "/.local/share";
    }
    base += "/meshy";
    mkdir(base.c_str(), 0755);
    return base + "/meshy.db";
#endif
}

bool Storage::Open(const std::string& path)
{
    Close();
    fPath = path.empty() ? DefaultPath() : path;
    if (sqlite3_open(fPath.c_str(), &fDb) != SQLITE_OK) {
        _Fail("cannot open database");
        if (fDb != NULL) {
            sqlite3_close(fDb);
            fDb = NULL;
        }
        return false;
    }
    sqlite3_busy_timeout(fDb, 5000);
    _Exec("PRAGMA journal_mode=WAL");
    _Exec("PRAGMA foreign_keys=ON");
    if (!_CreateTables())
        return false;
    return _Migrate();
}

void Storage::Close()
{
    if (fDb != NULL) {
        sqlite3_close(fDb);
        fDb = NULL;
    }
    fBatchDepth = 0;
}

bool Storage::_Exec(const std::string& sql)
{
    if (fDb == NULL)
        return false;
    char* err = NULL;
    if (sqlite3_exec(fDb, sql.c_str(), NULL, NULL, &err) != SQLITE_OK) {
        fLastError = err != NULL ? err : "sql error";
        if (err != NULL)
            sqlite3_free(err);
        return false;
    }
    return true;
}

void Storage::BeginBatch()
{
    if (fBatchDepth++ == 0)
        _Exec("BEGIN");
}

void Storage::EndBatch()
{
    if (fBatchDepth > 0 && --fBatchDepth == 0)
        _Exec("COMMIT");
}

bool Storage::_CreateTables()
{
    // Columns upstream only reaches through migrations are declared here too,
    // so a database created by this port matches a fully migrated one.
    static const char* kSchema =
        "CREATE TABLE IF NOT EXISTS contacts ("
        "  public_key_hex TEXT PRIMARY KEY,"
        "  name TEXT NOT NULL,"
        "  custom_name TEXT NOT NULL DEFAULT '',"
        "  type INTEGER NOT NULL DEFAULT 1,"
        "  flags INTEGER NOT NULL DEFAULT 0,"
        "  path_length INTEGER NOT NULL DEFAULT -1,"
        "  path BLOB NOT NULL DEFAULT X'',"
        "  path_hash_size INTEGER NOT NULL DEFAULT 1,"
        "  force_flood INTEGER NOT NULL DEFAULT 0,"
        "  auto_reset_path INTEGER NOT NULL DEFAULT 1,"
        "  smart_route_selection INTEGER NOT NULL DEFAULT -1,"
        "  latitude REAL,"
        "  longitude REAL,"
        "  last_seen REAL,"
        "  device_lastmod REAL,"
        "  last_seed_lastmod REAL,"
        "  last_message_at REAL,"
        "  is_active INTEGER NOT NULL DEFAULT 1"
        ");"

        "CREATE TABLE IF NOT EXISTS messages ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  contact_key_hex TEXT NOT NULL,"
        "  sender_key_hex TEXT NOT NULL,"
        "  text TEXT NOT NULL,"
        "  timestamp REAL NOT NULL,"
        "  is_outgoing INTEGER NOT NULL,"
        "  is_cli INTEGER NOT NULL DEFAULT 0,"
        "  status INTEGER NOT NULL DEFAULT 0,"
        "  read INTEGER NOT NULL DEFAULT 0,"
        "  room_sender_name TEXT NOT NULL DEFAULT '',"
        "  message_id TEXT,"
        "  uid TEXT,"
        "  sync_seq INTEGER NOT NULL DEFAULT 0,"
        "  FOREIGN KEY (contact_key_hex) REFERENCES contacts(public_key_hex)"
        ");"

        "CREATE TABLE IF NOT EXISTS channels ("
        "  idx INTEGER PRIMARY KEY,"
        "  name TEXT NOT NULL DEFAULT '',"
        "  psk BLOB NOT NULL DEFAULT X'00000000000000000000000000000000',"
        "  notification_level INTEGER NOT NULL DEFAULT 0,"
        "  region TEXT NOT NULL DEFAULT '',"
        "  retention_days INTEGER NOT NULL DEFAULT 0"
        ");"

        "CREATE TABLE IF NOT EXISTS channel_messages ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  channel_index INTEGER NOT NULL,"
        "  sender_name TEXT NOT NULL DEFAULT '',"
        "  text TEXT NOT NULL,"
        "  timestamp REAL NOT NULL,"
        "  is_outgoing INTEGER NOT NULL DEFAULT 0,"
        "  read INTEGER NOT NULL DEFAULT 0,"
        "  uid TEXT,"
        "  sync_seq INTEGER NOT NULL DEFAULT 0,"
        "  FOREIGN KEY (channel_index) REFERENCES channels(idx)"
        ");"

        "CREATE TABLE IF NOT EXISTS channel_message_paths ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  channel_index INTEGER NOT NULL,"
        "  sender_timestamp INTEGER NOT NULL,"
        "  msg_text_hash TEXT NOT NULL,"
        "  snr REAL,"
        "  path_len INTEGER NOT NULL DEFAULT -1,"
        "  path_bytes TEXT NOT NULL DEFAULT '',"
        "  region TEXT NOT NULL DEFAULT ''"
        ");"

        "CREATE TABLE IF NOT EXISTS deleted_uids ("
        "  uid TEXT PRIMARY KEY,"
        "  deleted_at REAL NOT NULL,"
        "  origin TEXT NOT NULL DEFAULT '',"
        "  sync_seq INTEGER NOT NULL DEFAULT 0"
        ");"

        "CREATE TABLE IF NOT EXISTS regions (name TEXT PRIMARY KEY);"

        "CREATE TABLE IF NOT EXISTS discovered_contacts ("
        "  public_key_hex TEXT PRIMARY KEY,"
        "  name TEXT NOT NULL,"
        "  type INTEGER NOT NULL DEFAULT 1,"
        "  flags INTEGER NOT NULL DEFAULT 0,"
        "  path_length INTEGER NOT NULL DEFAULT -1,"
        "  path BLOB NOT NULL DEFAULT X'',"
        "  path_hash_size INTEGER NOT NULL DEFAULT 1,"
        "  latitude REAL,"
        "  longitude REAL,"
        "  last_seen REAL,"
        "  received_at REAL,"
        "  source TEXT NOT NULL DEFAULT 'advert'"
        ");"

        "CREATE TABLE IF NOT EXISTS settings ("
        "  key TEXT PRIMARY KEY,"
        "  value TEXT NOT NULL DEFAULT ''"
        ");"

        "CREATE TABLE IF NOT EXISTS room_passwords ("
        "  room_key_hex TEXT PRIMARY KEY,"
        "  password TEXT NOT NULL"
        ");"

        "CREATE TABLE IF NOT EXISTS path_history ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  contact_key_hex TEXT NOT NULL,"
        "  path_bytes TEXT NOT NULL,"
        "  path_hash_size INTEGER NOT NULL DEFAULT 1,"
        "  hop_count INTEGER NOT NULL DEFAULT 0,"
        "  success_count INTEGER NOT NULL DEFAULT 0,"
        "  failure_count INTEGER NOT NULL DEFAULT 0,"
        "  last_trip_ms INTEGER,"
        "  route_weight REAL NOT NULL DEFAULT 2.5,"
        "  last_success_at REAL,"
        "  UNIQUE(contact_key_hex, path_bytes)"
        ");"

        "CREATE TABLE IF NOT EXISTS blocked_channel_senders (sender_name TEXT PRIMARY KEY);"

        "CREATE INDEX IF NOT EXISTS idx_messages_contact"
        "  ON messages(contact_key_hex, timestamp);"
        "CREATE INDEX IF NOT EXISTS idx_channel_messages_channel"
        "  ON channel_messages(channel_index, timestamp);"
        "CREATE INDEX IF NOT EXISTS idx_channel_message_paths"
        "  ON channel_message_paths(channel_index, sender_timestamp, msg_text_hash);"
        "CREATE INDEX IF NOT EXISTS idx_messages_contact_outgoing"
        "  ON messages(contact_key_hex, is_outgoing, timestamp);"
        "CREATE INDEX IF NOT EXISTS idx_channel_messages_unread"
        "  ON channel_messages(channel_index, is_outgoing, timestamp);";

    if (!_Exec(kSchema)) {
        _Fail("schema creation failed");
        return false;
    }
    return true;
}

bool Storage::_HasColumn(const std::string& table, const std::string& column)
{
    Stmt s(fDb, "SELECT 1 FROM pragma_table_info(?) WHERE name = ?");
    s.BindText(1, table).BindText(2, column);
    return s.Step();
}

bool Storage::_AddColumnIfMissing(const std::string& table, const std::string& column,
    const std::string& decl)
{
    if (_HasColumn(table, column))
        return true;
    return _Exec("ALTER TABLE " + table + " ADD COLUMN " + column + " " + decl);
}

bool Storage::_MigrationDone(const std::string& name)
{
    Stmt s(fDb, "SELECT value FROM settings WHERE key = ?");
    s.BindText(1, "migration_" + name);
    return s.Step() && s.ColText(0) == "1";
}

void Storage::_MarkMigrationDone(const std::string& name)
{
    Stmt s(fDb, "INSERT OR REPLACE INTO settings (key, value) VALUES (?, '1')");
    s.BindText(1, "migration_" + name);
    s.Exec();
}

bool Storage::_Migrate()
{
    // Brings a database created by an older Meshy up to the current schema.
    // A database this port created already has every column, so each check is
    // a no-op.
    _AddColumnIfMissing("channels", "notification_level", "INTEGER NOT NULL DEFAULT 0");
    _AddColumnIfMissing("channels", "region", "TEXT NOT NULL DEFAULT ''");
    _AddColumnIfMissing("channels", "retention_days", "INTEGER NOT NULL DEFAULT 0");

    _AddColumnIfMissing("channel_message_paths", "region", "TEXT NOT NULL DEFAULT ''");

    _AddColumnIfMissing("contacts", "path_hash_size", "INTEGER NOT NULL DEFAULT 1");
    _AddColumnIfMissing("contacts", "device_lastmod", "REAL");
    _AddColumnIfMissing("contacts", "force_flood", "INTEGER NOT NULL DEFAULT 0");
    _AddColumnIfMissing("contacts", "auto_reset_path", "INTEGER NOT NULL DEFAULT 1");
    _AddColumnIfMissing("contacts", "last_seed_lastmod", "REAL");
    _AddColumnIfMissing("contacts", "custom_name", "TEXT NOT NULL DEFAULT ''");

    if (!_HasColumn("contacts", "smart_route_selection")) {
        _Exec("ALTER TABLE contacts ADD COLUMN smart_route_selection INTEGER NOT NULL DEFAULT -1");
        // A fresh column already defaults to -1, so flag the one-time
        // conversion as done; running it later would clobber contacts the user
        // deliberately set to 0 ("not smart").
        _MarkMigrationDone("smart_route_selection_default");
    } else if (!_MigrationDone("smart_route_selection_default")) {
        _Exec("UPDATE contacts SET smart_route_selection = -1 WHERE smart_route_selection = 0");
        _MarkMigrationDone("smart_route_selection_default");
    }

    _AddColumnIfMissing("messages", "room_sender_name", "TEXT NOT NULL DEFAULT ''");
    _AddColumnIfMissing("messages", "read", "INTEGER NOT NULL DEFAULT 0");
    _AddColumnIfMissing("messages", "uid", "TEXT");
    _AddColumnIfMissing("channel_messages", "read", "INTEGER NOT NULL DEFAULT 0");
    _AddColumnIfMissing("channel_messages", "uid", "TEXT");

    const char* seq_tables[] = { "messages", "channel_messages", "deleted_uids" };
    for (size_t i = 0; i < 3; i++) {
        _AddColumnIfMissing(seq_tables[i], "sync_seq", "INTEGER NOT NULL DEFAULT 0");
        _Exec(std::string("CREATE INDEX IF NOT EXISTS idx_") + seq_tables[i] + "_sync_seq ON "
            + seq_tables[i] + "(sync_seq)");
    }

    _AddColumnIfMissing("discovered_contacts", "received_at", "REAL");
    _AddColumnIfMissing("discovered_contacts", "source", "TEXT NOT NULL DEFAULT 'advert'");

    // Uniqueness on uid is what makes an incoming message idempotent.
    _Exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_messages_uid ON messages(uid) "
          "WHERE uid IS NOT NULL");
    _Exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_channel_messages_uid ON channel_messages(uid) "
          "WHERE uid IS NOT NULL");
    return true;
}

int64_t Storage::_NextSyncSeq()
{
    Stmt s(fDb,
        "SELECT MAX(s) FROM (SELECT MAX(sync_seq) AS s FROM messages "
        "UNION ALL SELECT MAX(sync_seq) FROM channel_messages "
        "UNION ALL SELECT MAX(sync_seq) FROM deleted_uids)");
    int64_t cur = s.Step() && !s.ColIsNull(0) ? s.ColInt(0) : 0;
    return cur + 1;
}

// ─── Contacts ────────────────────────────────────────────────────

static Contact _ReadContact(const Stmt& s)
{
    Contact c;
    c.public_key = FromHexOrEmpty(s.ColText(0));
    c.name = s.ColText(1);
    c.custom_name = s.ColText(2);
    c.type = static_cast<int>(s.ColInt(3));
    c.flags = static_cast<int>(s.ColInt(4));
    c.path_length = static_cast<int>(s.ColInt(5));
    c.path = s.ColBlob(6);
    c.path_hash_size = static_cast<int>(s.ColInt(7));
    c.force_flood = s.ColInt(8) != 0;
    c.auto_reset_path = s.ColInt(9) != 0;
    c.smart_route_selection = static_cast<int>(s.ColInt(10));
    if (!s.ColIsNull(11) && !s.ColIsNull(12)) {
        c.has_location = true;
        c.latitude = s.ColDouble(11);
        c.longitude = s.ColDouble(12);
    }
    c.last_seen = s.ColTime(13);
    c.device_lastmod = s.ColTime(14);
    c.last_message_at = s.ColTime(15);
    c.is_active = s.ColInt(16) != 0;
    return c;
}

static const char* kContactCols
    = "public_key_hex, name, custom_name, type, flags, path_length, path, path_hash_size,"
      " force_flood, auto_reset_path, smart_route_selection, latitude, longitude, last_seen,"
      " device_lastmod, last_message_at, is_active";

std::vector<Contact> Storage::GetContacts(bool include_inactive)
{
    std::vector<Contact> out;
    std::string sql = std::string("SELECT ") + kContactCols + " FROM contacts";
    if (!include_inactive)
        sql += " WHERE is_active = 1";
    sql += " ORDER BY name COLLATE NOCASE";
    Stmt s(fDb, sql);
    while (s.Step())
        out.push_back(_ReadContact(s));
    return out;
}

bool Storage::GetContact(const std::string& public_key_hex, Contact* out)
{
    Stmt s(fDb,
        std::string("SELECT ") + kContactCols + " FROM contacts WHERE public_key_hex = ?");
    s.BindText(1, public_key_hex);
    if (!s.Step())
        return false;
    *out = _ReadContact(s);
    return true;
}

bool Storage::GetContactByPrefix(const std::string& prefix_hex, Contact* out)
{
    Stmt s(fDb,
        std::string("SELECT ") + kContactCols
            + " FROM contacts WHERE public_key_hex LIKE ? || '%' LIMIT 1");
    s.BindText(1, prefix_hex);
    if (!s.Step())
        return false;
    *out = _ReadContact(s);
    return true;
}

bool Storage::SaveContact(const Contact& c)
{
    Stmt s(fDb,
        "INSERT INTO contacts (public_key_hex, name, custom_name, type, flags, path_length,"
        " path, path_hash_size, force_flood, auto_reset_path, smart_route_selection,"
        " latitude, longitude, last_seen, device_lastmod, last_message_at, is_active)"
        " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"
        " ON CONFLICT(public_key_hex) DO UPDATE SET"
        "  name=excluded.name, custom_name=excluded.custom_name, type=excluded.type,"
        "  flags=excluded.flags, path_length=excluded.path_length, path=excluded.path,"
        "  path_hash_size=excluded.path_hash_size, force_flood=excluded.force_flood,"
        "  auto_reset_path=excluded.auto_reset_path,"
        "  smart_route_selection=excluded.smart_route_selection,"
        "  latitude=excluded.latitude, longitude=excluded.longitude,"
        "  last_seen=excluded.last_seen, device_lastmod=excluded.device_lastmod,"
        "  last_message_at=excluded.last_message_at, is_active=excluded.is_active");
    s.BindText(1, c.PublicKeyHex())
        .BindText(2, c.name)
        .BindText(3, c.custom_name)
        .BindInt(4, c.type)
        .BindInt(5, c.flags)
        .BindInt(6, c.path_length)
        .BindBlob(7, c.path)
        .BindInt(8, c.path_hash_size)
        .BindInt(9, c.force_flood ? 1 : 0)
        .BindInt(10, c.auto_reset_path ? 1 : 0)
        .BindInt(11, c.smart_route_selection);
    if (c.has_location)
        s.BindDouble(12, c.latitude).BindDouble(13, c.longitude);
    else
        s.BindNull(12).BindNull(13);
    s.BindTime(14, c.last_seen)
        .BindTime(15, c.device_lastmod)
        .BindTime(16, c.last_message_at)
        .BindInt(17, c.is_active ? 1 : 0);
    return s.Exec();
}

bool Storage::DeleteContact(const std::string& public_key_hex)
{
    BeginBatch();
    Stmt m(fDb, "DELETE FROM messages WHERE contact_key_hex = ?");
    m.BindText(1, public_key_hex);
    m.Exec();
    Stmt s(fDb, "DELETE FROM contacts WHERE public_key_hex = ?");
    s.BindText(1, public_key_hex);
    bool ok = s.Exec();
    EndBatch();
    return ok;
}

bool Storage::SetContactActive(const std::string& public_key_hex, bool active)
{
    Stmt s(fDb, "UPDATE contacts SET is_active = ? WHERE public_key_hex = ?");
    s.BindInt(1, active ? 1 : 0).BindText(2, public_key_hex);
    return s.Exec();
}

bool Storage::SetContactFlags(const std::string& public_key_hex, int flags)
{
    Stmt s(fDb, "UPDATE contacts SET flags = ? WHERE public_key_hex = ?");
    s.BindInt(1, flags).BindText(2, public_key_hex);
    return s.Exec();
}

bool Storage::SetContactCustomName(const std::string& public_key_hex, const std::string& name)
{
    Stmt s(fDb, "UPDATE contacts SET custom_name = ? WHERE public_key_hex = ?");
    s.BindText(1, name).BindText(2, public_key_hex);
    return s.Exec();
}

bool Storage::SetContactPath(const std::string& public_key_hex, int path_length,
    const ByteBuffer& path, int path_hash_size)
{
    Stmt s(fDb,
        "UPDATE contacts SET path_length = ?, path = ?, path_hash_size = ?"
        " WHERE public_key_hex = ?");
    s.BindInt(1, path_length).BindBlob(2, path).BindInt(3, path_hash_size).BindText(
        4, public_key_hex);
    return s.Exec();
}

bool Storage::TouchContactMessageTime(const std::string& public_key_hex, double when)
{
    Stmt s(fDb,
        "UPDATE contacts SET last_message_at = ?"
        " WHERE public_key_hex = ? AND (last_message_at IS NULL OR last_message_at < ?)");
    s.BindDouble(1, when).BindText(2, public_key_hex).BindDouble(3, when);
    return s.Exec();
}

// ─── Direct messages ─────────────────────────────────────────────

int64_t Storage::AddMessage(const Message& m)
{
    Stmt s(fDb,
        "INSERT OR IGNORE INTO messages (contact_key_hex, sender_key_hex, text, timestamp,"
        " is_outgoing, is_cli, status, read, room_sender_name, message_id, uid, sync_seq)"
        " VALUES (?,?,?,?,?,?,?,?,?,?,?,?)");
    s.BindText(1, m.contact_key_hex)
        .BindText(2, ToHex(m.sender_key))
        .BindText(3, m.text)
        .BindDouble(4, m.timestamp)
        .BindInt(5, m.is_outgoing ? 1 : 0)
        .BindInt(6, m.is_cli ? 1 : 0)
        .BindInt(7, m.status)
        .BindInt(8, (m.read || m.is_outgoing) ? 1 : 0)
        .BindText(9, m.room_sender_name)
        .BindText(10, m.message_id);
    if (m.uid.empty())
        s.BindNull(11);
    else
        s.BindText(11, m.uid);
    s.BindInt(12, _NextSyncSeq());
    if (!s.Exec())
        return 0;
    if (sqlite3_changes(fDb) == 0)
        return 0;  // duplicate uid, already stored
    int64_t id = sqlite3_last_insert_rowid(fDb);
    TouchContactMessageTime(m.contact_key_hex, m.timestamp);
    return id;
}

static Message _ReadMessage(const Stmt& s)
{
    Message m;
    m.id = s.ColInt(0);
    m.contact_key_hex = s.ColText(1);
    m.sender_key = FromHexOrEmpty(s.ColText(2));
    m.text = s.ColText(3);
    m.timestamp = s.ColDouble(4);
    m.is_outgoing = s.ColInt(5) != 0;
    m.is_cli = s.ColInt(6) != 0;
    m.status = static_cast<int>(s.ColInt(7));
    m.read = s.ColInt(8) != 0;
    m.room_sender_name = s.ColText(9);
    m.message_id = s.ColText(10);
    m.uid = s.ColText(11);
    return m;
}

static const char* kMessageCols
    = "id, contact_key_hex, sender_key_hex, text, timestamp, is_outgoing, is_cli, status, read,"
      " room_sender_name, message_id, uid";

std::vector<Message> Storage::GetMessages(const std::string& contact_key_hex, int limit,
    int before_id)
{
    std::vector<Message> out;
    std::string sql
        = std::string("SELECT ") + kMessageCols + " FROM messages WHERE contact_key_hex = ?";
    if (before_id > 0)
        sql += " AND id < ?";
    sql += " ORDER BY timestamp DESC, id DESC";
    if (limit > 0)
        sql += " LIMIT ?";
    Stmt s(fDb, sql);
    int bind = 1;
    s.BindText(bind++, contact_key_hex);
    if (before_id > 0)
        s.BindInt(bind++, before_id);
    if (limit > 0)
        s.BindInt(bind++, limit);
    while (s.Step())
        out.push_back(_ReadMessage(s));
    // Callers render oldest-first; the query is newest-first so LIMIT takes
    // the most recent page.
    std::vector<Message> rev(out.rbegin(), out.rend());
    return rev;
}

bool Storage::UpdateMessageStatus(int64_t id, int status)
{
    Stmt s(fDb, "UPDATE messages SET status = ?, sync_seq = ? WHERE id = ?");
    s.BindInt(1, status).BindInt(2, _NextSyncSeq()).BindInt(3, id);
    return s.Exec();
}

bool Storage::UpdateMessageDelivered(int64_t id, double delivered_at, bool has_trip, int trip_ms)
{
    (void)delivered_at;
    (void)has_trip;
    (void)trip_ms;
    // Delivery time and trip time are presentation state that upstream keeps
    // in memory on the Message object; only the status is persisted.
    return UpdateMessageStatus(id, MSG_DELIVERED);
}

bool Storage::MarkContactRead(const std::string& contact_key_hex)
{
    Stmt s(fDb,
        "UPDATE messages SET read = 1 WHERE contact_key_hex = ? AND is_outgoing = 0 AND read = 0");
    s.BindText(1, contact_key_hex);
    return s.Exec();
}

int Storage::UnreadCount(const std::string& contact_key_hex)
{
    Stmt s(fDb,
        "SELECT COUNT(*) FROM messages WHERE contact_key_hex = ? AND is_outgoing = 0 AND read = 0");
    s.BindText(1, contact_key_hex);
    return s.Step() ? static_cast<int>(s.ColInt(0)) : 0;
}

std::vector<std::pair<std::string, int> > Storage::AllUnreadCounts()
{
    std::vector<std::pair<std::string, int> > out;
    Stmt s(fDb,
        "SELECT contact_key_hex, COUNT(*) FROM messages"
        " WHERE is_outgoing = 0 AND read = 0 GROUP BY contact_key_hex");
    while (s.Step())
        out.push_back(std::make_pair(s.ColText(0), static_cast<int>(s.ColInt(1))));
    return out;
}

bool Storage::DeleteMessage(int64_t id)
{
    Stmt u(fDb, "SELECT uid FROM messages WHERE id = ?");
    u.BindInt(1, id);
    std::string uid = u.Step() ? u.ColText(0) : std::string();
    Stmt s(fDb, "DELETE FROM messages WHERE id = ?");
    s.BindInt(1, id);
    bool ok = s.Exec();
    if (ok && !uid.empty())
        RecordTombstone(uid, "local");
    return ok;
}

bool Storage::DeleteConversation(const std::string& contact_key_hex)
{
    Stmt s(fDb, "DELETE FROM messages WHERE contact_key_hex = ?");
    s.BindText(1, contact_key_hex);
    return s.Exec();
}

bool Storage::MessageUidExists(const std::string& uid)
{
    if (uid.empty())
        return false;
    Stmt s(fDb, "SELECT 1 FROM messages WHERE uid = ? LIMIT 1");
    s.BindText(1, uid);
    return s.Step();
}

// ─── Channels ────────────────────────────────────────────────────

std::vector<Channel> Storage::GetChannels()
{
    std::vector<Channel> out;
    Stmt s(fDb,
        "SELECT idx, name, psk, notification_level, region, retention_days"
        " FROM channels ORDER BY idx");
    while (s.Step()) {
        Channel c;
        c.index = static_cast<int>(s.ColInt(0));
        c.name = s.ColText(1);
        c.psk = s.ColBlob(2);
        c.notification_level = static_cast<int>(s.ColInt(3));
        c.region = s.ColText(4);
        c.retention_days = static_cast<int>(s.ColInt(5));
        out.push_back(c);
    }
    return out;
}

bool Storage::GetChannel(int index, Channel* out)
{
    Stmt s(fDb,
        "SELECT idx, name, psk, notification_level, region, retention_days"
        " FROM channels WHERE idx = ?");
    s.BindInt(1, index);
    if (!s.Step())
        return false;
    out->index = static_cast<int>(s.ColInt(0));
    out->name = s.ColText(1);
    out->psk = s.ColBlob(2);
    out->notification_level = static_cast<int>(s.ColInt(3));
    out->region = s.ColText(4);
    out->retention_days = static_cast<int>(s.ColInt(5));
    return true;
}

bool Storage::SaveChannel(const Channel& c)
{
    Stmt s(fDb,
        "INSERT INTO channels (idx, name, psk, notification_level, region, retention_days)"
        " VALUES (?,?,?,?,?,?)"
        " ON CONFLICT(idx) DO UPDATE SET name=excluded.name, psk=excluded.psk,"
        "  notification_level=excluded.notification_level, region=excluded.region,"
        "  retention_days=excluded.retention_days");
    s.BindInt(1, c.index)
        .BindText(2, c.name)
        .BindBlob(3, c.psk)
        .BindInt(4, c.notification_level)
        .BindText(5, c.region)
        .BindInt(6, c.retention_days);
    return s.Exec();
}

bool Storage::DeleteChannel(int index)
{
    BeginBatch();
    Stmt m(fDb, "DELETE FROM channel_messages WHERE channel_index = ?");
    m.BindInt(1, index);
    m.Exec();
    Stmt s(fDb, "DELETE FROM channels WHERE idx = ?");
    s.BindInt(1, index);
    bool ok = s.Exec();
    EndBatch();
    return ok;
}

// ─── Channel messages ────────────────────────────────────────────

int64_t Storage::AddChannelMessage(const ChannelMessage& m)
{
    Stmt s(fDb,
        "INSERT OR IGNORE INTO channel_messages (channel_index, sender_name, text, timestamp,"
        " is_outgoing, read, uid, sync_seq) VALUES (?,?,?,?,?,?,?,?)");
    s.BindInt(1, m.channel_index)
        .BindText(2, m.sender_name)
        .BindText(3, m.text)
        .BindDouble(4, m.timestamp)
        .BindInt(5, m.is_outgoing ? 1 : 0)
        .BindInt(6, (m.read || m.is_outgoing) ? 1 : 0);
    if (m.uid.empty())
        s.BindNull(7);
    else
        s.BindText(7, m.uid);
    s.BindInt(8, _NextSyncSeq());
    if (!s.Exec() || sqlite3_changes(fDb) == 0)
        return 0;
    return sqlite3_last_insert_rowid(fDb);
}

std::vector<ChannelMessage> Storage::GetChannelMessages(int channel_index, int limit,
    int before_id)
{
    std::vector<ChannelMessage> out;
    // Path metadata is recorded separately by the RX log, keyed on the
    // sender timestamp and a hash of the text.
    std::string sql
        = "SELECT m.id, m.channel_index, m.sender_name, m.text, m.timestamp, m.is_outgoing,"
          " m.read, m.uid, p.snr, p.path_len, p.path_bytes, p.region"
          " FROM channel_messages m"
          " LEFT JOIN channel_message_paths p"
          "   ON p.channel_index = m.channel_index"
          "  AND p.sender_timestamp = CAST(m.timestamp AS INTEGER)"
          " WHERE m.channel_index = ?";
    if (before_id > 0)
        sql += " AND m.id < ?";
    sql += " ORDER BY m.timestamp DESC, m.id DESC";
    if (limit > 0)
        sql += " LIMIT ?";
    Stmt s(fDb, sql);
    int bind = 1;
    s.BindInt(bind++, channel_index);
    if (before_id > 0)
        s.BindInt(bind++, before_id);
    if (limit > 0)
        s.BindInt(bind++, limit);
    while (s.Step()) {
        ChannelMessage m;
        m.id = s.ColInt(0);
        m.channel_index = static_cast<int>(s.ColInt(1));
        m.sender_name = s.ColText(2);
        m.text = s.ColText(3);
        m.timestamp = s.ColDouble(4);
        m.is_outgoing = s.ColInt(5) != 0;
        m.read = s.ColInt(6) != 0;
        m.uid = s.ColText(7);
        if (!s.ColIsNull(8)) {
            m.has_snr = true;
            m.snr = s.ColDouble(8);
        }
        m.path_len = s.ColIsNull(9) ? -1 : static_cast<int>(s.ColInt(9));
        m.path_bytes = s.ColText(10);
        m.region = s.ColText(11);
        out.push_back(m);
    }
    std::vector<ChannelMessage> rev(out.rbegin(), out.rend());
    return rev;
}

bool Storage::MarkChannelRead(int channel_index)
{
    Stmt s(fDb,
        "UPDATE channel_messages SET read = 1"
        " WHERE channel_index = ? AND is_outgoing = 0 AND read = 0");
    s.BindInt(1, channel_index);
    return s.Exec();
}

int Storage::ChannelUnreadCount(int channel_index)
{
    Stmt s(fDb,
        "SELECT COUNT(*) FROM channel_messages"
        " WHERE channel_index = ? AND is_outgoing = 0 AND read = 0");
    s.BindInt(1, channel_index);
    return s.Step() ? static_cast<int>(s.ColInt(0)) : 0;
}

std::vector<std::pair<int, int> > Storage::AllChannelUnreadCounts()
{
    std::vector<std::pair<int, int> > out;
    Stmt s(fDb,
        "SELECT channel_index, COUNT(*) FROM channel_messages"
        " WHERE is_outgoing = 0 AND read = 0 GROUP BY channel_index");
    while (s.Step())
        out.push_back(
            std::make_pair(static_cast<int>(s.ColInt(0)), static_cast<int>(s.ColInt(1))));
    return out;
}

bool Storage::DeleteChannelMessage(int64_t id)
{
    Stmt u(fDb, "SELECT uid FROM channel_messages WHERE id = ?");
    u.BindInt(1, id);
    std::string uid = u.Step() ? u.ColText(0) : std::string();
    Stmt s(fDb, "DELETE FROM channel_messages WHERE id = ?");
    s.BindInt(1, id);
    bool ok = s.Exec();
    if (ok && !uid.empty())
        RecordTombstone(uid, "local");
    return ok;
}

bool Storage::ChannelMessageUidExists(const std::string& uid)
{
    if (uid.empty())
        return false;
    Stmt s(fDb, "SELECT 1 FROM channel_messages WHERE uid = ? LIMIT 1");
    s.BindText(1, uid);
    return s.Step();
}

bool Storage::RecordChannelPath(int channel_index, uint32_t sender_timestamp,
    const std::string& msg_text_hash, bool has_snr, double snr, int path_len,
    const std::string& path_bytes_hex, const std::string& region)
{
    Stmt s(fDb,
        "INSERT INTO channel_message_paths (channel_index, sender_timestamp, msg_text_hash,"
        " snr, path_len, path_bytes, region) VALUES (?,?,?,?,?,?,?)");
    s.BindInt(1, channel_index).BindInt(2, sender_timestamp).BindText(3, msg_text_hash);
    if (has_snr)
        s.BindDouble(4, snr);
    else
        s.BindNull(4);
    s.BindInt(5, path_len).BindText(6, path_bytes_hex).BindText(7, region);
    return s.Exec();
}

// ─── Discovered contacts ─────────────────────────────────────────

std::vector<DiscoveredContact> Storage::GetDiscovered()
{
    std::vector<DiscoveredContact> out;
    Stmt s(fDb,
        "SELECT public_key_hex, name, type, flags, path_length, path, path_hash_size,"
        " latitude, longitude, last_seen, received_at, source"
        " FROM discovered_contacts ORDER BY last_seen DESC");
    while (s.Step()) {
        DiscoveredContact d;
        d.public_key_hex = s.ColText(0);
        d.name = s.ColText(1);
        d.type = static_cast<int>(s.ColInt(2));
        d.flags = static_cast<int>(s.ColInt(3));
        d.path_length = static_cast<int>(s.ColInt(4));
        d.path = s.ColBlob(5);
        d.path_hash_size = static_cast<int>(s.ColInt(6));
        if (!s.ColIsNull(7) && !s.ColIsNull(8)) {
            d.has_location = true;
            d.latitude = s.ColDouble(7);
            d.longitude = s.ColDouble(8);
        }
        d.last_seen = s.ColTime(9);
        d.received_at = s.ColTime(10);
        d.source = s.ColText(11);
        out.push_back(d);
    }
    return out;
}

bool Storage::SaveDiscovered(const DiscoveredContact& d)
{
    Stmt s(fDb,
        "INSERT INTO discovered_contacts (public_key_hex, name, type, flags, path_length, path,"
        " path_hash_size, latitude, longitude, last_seen, received_at, source)"
        " VALUES (?,?,?,?,?,?,?,?,?,?,?,?)"
        " ON CONFLICT(public_key_hex) DO UPDATE SET name=excluded.name, type=excluded.type,"
        "  flags=excluded.flags, path_length=excluded.path_length, path=excluded.path,"
        "  path_hash_size=excluded.path_hash_size, latitude=excluded.latitude,"
        "  longitude=excluded.longitude, last_seen=excluded.last_seen,"
        "  received_at=excluded.received_at, source=excluded.source");
    s.BindText(1, d.public_key_hex)
        .BindText(2, d.name)
        .BindInt(3, d.type)
        .BindInt(4, d.flags)
        .BindInt(5, d.path_length)
        .BindBlob(6, d.path)
        .BindInt(7, d.path_hash_size);
    if (d.has_location)
        s.BindDouble(8, d.latitude).BindDouble(9, d.longitude);
    else
        s.BindNull(8).BindNull(9);
    s.BindTime(10, d.last_seen).BindTime(11, d.received_at).BindText(12, d.source);
    return s.Exec();
}

bool Storage::DeleteDiscovered(const std::string& public_key_hex)
{
    Stmt s(fDb, "DELETE FROM discovered_contacts WHERE public_key_hex = ?");
    s.BindText(1, public_key_hex);
    return s.Exec();
}

bool Storage::ClearDiscovered() { return _Exec("DELETE FROM discovered_contacts"); }

// ─── Settings and small tables ───────────────────────────────────

std::string Storage::GetSetting(const std::string& key, const std::string& fallback)
{
    Stmt s(fDb, "SELECT value FROM settings WHERE key = ?");
    s.BindText(1, key);
    return s.Step() ? s.ColText(0) : fallback;
}

bool Storage::SetSetting(const std::string& key, const std::string& value)
{
    Stmt s(fDb, "INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?)");
    s.BindText(1, key).BindText(2, value);
    return s.Exec();
}

int Storage::GetSettingInt(const std::string& key, int fallback)
{
    std::string v = GetSetting(key);
    if (v.empty())
        return fallback;
    return atoi(v.c_str());
}

bool Storage::SetSettingInt(const std::string& key, int value)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    return SetSetting(key, buf);
}

bool Storage::GetSettingBool(const std::string& key, bool fallback)
{
    std::string v = GetSetting(key);
    if (v.empty())
        return fallback;
    return v == "1" || v == "true";
}

bool Storage::SetSettingBool(const std::string& key, bool value)
{
    return SetSetting(key, value ? "1" : "0");
}

std::vector<std::string> Storage::GetRegions()
{
    std::vector<std::string> out;
    Stmt s(fDb, "SELECT name FROM regions ORDER BY name COLLATE NOCASE");
    while (s.Step())
        out.push_back(s.ColText(0));
    return out;
}

bool Storage::AddRegion(const std::string& name)
{
    Stmt s(fDb, "INSERT OR IGNORE INTO regions (name) VALUES (?)");
    s.BindText(1, name);
    return s.Exec();
}

bool Storage::RemoveRegion(const std::string& name)
{
    Stmt s(fDb, "DELETE FROM regions WHERE name = ?");
    s.BindText(1, name);
    return s.Exec();
}

std::string Storage::GetRoomPassword(const std::string& room_key_hex)
{
    Stmt s(fDb, "SELECT password FROM room_passwords WHERE room_key_hex = ?");
    s.BindText(1, room_key_hex);
    return s.Step() ? s.ColText(0) : std::string();
}

bool Storage::SetRoomPassword(const std::string& room_key_hex, const std::string& password)
{
    Stmt s(fDb, "INSERT OR REPLACE INTO room_passwords (room_key_hex, password) VALUES (?, ?)");
    s.BindText(1, room_key_hex).BindText(2, password);
    return s.Exec();
}

bool Storage::ClearRoomPassword(const std::string& room_key_hex)
{
    Stmt s(fDb, "DELETE FROM room_passwords WHERE room_key_hex = ?");
    s.BindText(1, room_key_hex);
    return s.Exec();
}

std::vector<std::string> Storage::GetBlockedSenders()
{
    std::vector<std::string> out;
    Stmt s(fDb, "SELECT sender_name FROM blocked_channel_senders ORDER BY sender_name");
    while (s.Step())
        out.push_back(s.ColText(0));
    return out;
}

bool Storage::BlockSender(const std::string& sender_name)
{
    Stmt s(fDb, "INSERT OR IGNORE INTO blocked_channel_senders (sender_name) VALUES (?)");
    s.BindText(1, sender_name);
    return s.Exec();
}

bool Storage::UnblockSender(const std::string& sender_name)
{
    Stmt s(fDb, "DELETE FROM blocked_channel_senders WHERE sender_name = ?");
    s.BindText(1, sender_name);
    return s.Exec();
}

// ─── Path history ────────────────────────────────────────────────

std::vector<PathHistoryEntry> Storage::GetPathHistory(const std::string& contact_key_hex)
{
    std::vector<PathHistoryEntry> out;
    Stmt s(fDb,
        "SELECT id, contact_key_hex, path_bytes, path_hash_size, hop_count, success_count,"
        " failure_count, last_trip_ms, route_weight, last_success_at"
        " FROM path_history WHERE contact_key_hex = ? ORDER BY route_weight DESC");
    s.BindText(1, contact_key_hex);
    while (s.Step()) {
        PathHistoryEntry e;
        e.id = s.ColInt(0);
        e.contact_key_hex = s.ColText(1);
        e.path_bytes = s.ColText(2);
        e.path_hash_size = static_cast<int>(s.ColInt(3));
        e.hop_count = static_cast<int>(s.ColInt(4));
        e.success_count = static_cast<int>(s.ColInt(5));
        e.failure_count = static_cast<int>(s.ColInt(6));
        if (!s.ColIsNull(7)) {
            e.has_trip_ms = true;
            e.last_trip_ms = static_cast<int>(s.ColInt(7));
        }
        e.route_weight = s.ColDouble(8);
        e.last_success_at = s.ColTime(9);
        out.push_back(e);
    }
    return out;
}

bool Storage::RecordPathResult(const std::string& contact_key_hex,
    const std::string& path_bytes_hex, int path_hash_size, int hop_count, bool success,
    bool has_trip, int trip_ms)
{
    double now = static_cast<double>(time(NULL));
    Stmt s(fDb,
        "INSERT INTO path_history (contact_key_hex, path_bytes, path_hash_size, hop_count,"
        " success_count, failure_count, last_trip_ms, last_success_at)"
        " VALUES (?,?,?,?,?,?,?,?)"
        " ON CONFLICT(contact_key_hex, path_bytes) DO UPDATE SET"
        "  success_count = success_count + excluded.success_count,"
        "  failure_count = failure_count + excluded.failure_count,"
        "  last_trip_ms = COALESCE(excluded.last_trip_ms, last_trip_ms),"
        "  last_success_at = COALESCE(excluded.last_success_at, last_success_at),"
        "  path_hash_size = excluded.path_hash_size,"
        "  hop_count = excluded.hop_count");
    s.BindText(1, contact_key_hex)
        .BindText(2, path_bytes_hex)
        .BindInt(3, path_hash_size)
        .BindInt(4, hop_count)
        .BindInt(5, success ? 1 : 0)
        .BindInt(6, success ? 0 : 1);
    if (has_trip)
        s.BindInt(7, trip_ms);
    else
        s.BindNull(7);
    if (success)
        s.BindDouble(8, now);
    else
        s.BindNull(8);
    if (!s.Exec())
        return false;

    // Weight favours paths that succeed and answer quickly; it is recomputed
    // from the running counters rather than stored incrementally.
    Stmt w(fDb,
        "UPDATE path_history SET route_weight ="
        "  (CAST(success_count AS REAL) + 1.0) / (success_count + failure_count + 2.0) * 5.0"
        " WHERE contact_key_hex = ? AND path_bytes = ?");
    w.BindText(1, contact_key_hex).BindText(2, path_bytes_hex);
    return w.Exec();
}

bool Storage::ClearPathHistory(const std::string& contact_key_hex)
{
    Stmt s(fDb, "DELETE FROM path_history WHERE contact_key_hex = ?");
    s.BindText(1, contact_key_hex);
    return s.Exec();
}

// ─── Tombstones ──────────────────────────────────────────────────

bool Storage::RecordTombstone(const std::string& uid, const std::string& origin)
{
    if (uid.empty())
        return false;
    Stmt s(fDb,
        "INSERT OR REPLACE INTO deleted_uids (uid, deleted_at, origin, sync_seq)"
        " VALUES (?,?,?,?)");
    s.BindText(1, uid)
        .BindDouble(2, static_cast<double>(time(NULL)))
        .BindText(3, origin)
        .BindInt(4, _NextSyncSeq());
    return s.Exec();
}

std::vector<std::string> Storage::TombstonedUids()
{
    std::vector<std::string> out;
    Stmt s(fDb, "SELECT uid FROM deleted_uids");
    while (s.Step())
        out.push_back(s.ColText(0));
    return out;
}

}  // namespace meshy
