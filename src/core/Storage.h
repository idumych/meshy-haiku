// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// SQLite persistence, ported from upstream Meshy src/storage.py.  The schema
// matches upstream exactly — including the columns upstream only adds through
// migrations — so a meshy.db copied from a Linux install opens unchanged, and
// the older-database migrations are applied on open as well.

#ifndef MESHY_CORE_STORAGE_H
#define MESHY_CORE_STORAGE_H

#include <stdint.h>

#include <string>
#include <vector>

#include "Models.h"

struct sqlite3;
struct sqlite3_stmt;

namespace meshy {

// Thin RAII wrapper over a prepared statement.  Binding is 1-based, as in the
// SQLite API; column reads are 0-based.
class Stmt {
public:
    Stmt(sqlite3* db, const std::string& sql);
    ~Stmt();

    bool Ok() const { return fStmt != NULL; }

    Stmt& BindNull(int i);
    Stmt& BindInt(int i, int64_t v);
    Stmt& BindDouble(int i, double v);
    Stmt& BindText(int i, const std::string& v);
    Stmt& BindBlob(int i, const ByteBuffer& v);
    // Binds a timestamp, writing SQL NULL when the value is unset.
    Stmt& BindTime(int i, double t);

    bool Step();  // true while a row is available
    bool Exec();  // runs to completion; true on success

    int64_t ColInt(int i) const;
    double ColDouble(int i) const;
    std::string ColText(int i) const;
    ByteBuffer ColBlob(int i) const;
    bool ColIsNull(int i) const;
    double ColTime(int i) const;  // kNoTime when NULL

private:
    Stmt(const Stmt&);
    Stmt& operator=(const Stmt&);

    sqlite3* fDb;
    sqlite3_stmt* fStmt;
};

class Storage {
public:
    Storage();
    ~Storage();

    // Opens (and creates) the database.  An empty path uses the per-user data
    // directory.  Returns false if the file cannot be opened or migrated.
    bool Open(const std::string& path = std::string());
    void Close();
    bool IsOpen() const { return fDb != NULL; }
    const std::string& Path() const { return fPath; }
    std::string LastError() const { return fLastError; }

    // Default on-disk location for the database on this platform.
    static std::string DefaultPath();

    // Wrap a burst of writes in one transaction.  Nested calls are refcounted.
    void BeginBatch();
    void EndBatch();

    // ── Contacts ──
    std::vector<Contact> GetContacts(bool include_inactive = false);
    bool GetContact(const std::string& public_key_hex, Contact* out);
    // Matches a 6-byte (12 hex char) wire prefix to a stored contact.
    bool GetContactByPrefix(const std::string& prefix_hex, Contact* out);
    bool SaveContact(const Contact& c);
    bool DeleteContact(const std::string& public_key_hex);
    bool SetContactActive(const std::string& public_key_hex, bool active);
    bool SetContactFlags(const std::string& public_key_hex, int flags);
    bool SetContactCustomName(const std::string& public_key_hex, const std::string& name);
    bool SetContactPath(const std::string& public_key_hex, int path_length, const ByteBuffer& path,
        int path_hash_size);
    bool TouchContactMessageTime(const std::string& public_key_hex, double when);

    // ── Direct messages ──
    int64_t AddMessage(const Message& m);
    std::vector<Message> GetMessages(const std::string& contact_key_hex, int limit = 0,
        int before_id = 0);
    bool UpdateMessageStatus(int64_t id, int status);
    bool UpdateMessageDelivered(int64_t id, double delivered_at, bool has_trip, int trip_ms);
    bool MarkContactRead(const std::string& contact_key_hex);
    int UnreadCount(const std::string& contact_key_hex);
    // Unread counts for every contact, as (contact_key_hex, count).
    std::vector<std::pair<std::string, int> > AllUnreadCounts();
    bool DeleteMessage(int64_t id);
    bool DeleteConversation(const std::string& contact_key_hex);
    // True when a message with this uid already exists (incoming dedup).
    bool MessageUidExists(const std::string& uid);

    // ── Channels ──
    std::vector<Channel> GetChannels();
    bool GetChannel(int index, Channel* out);
    bool SaveChannel(const Channel& c);
    bool DeleteChannel(int index);

    // ── Channel messages ──
    int64_t AddChannelMessage(const ChannelMessage& m);
    std::vector<ChannelMessage> GetChannelMessages(int channel_index, int limit = 0,
        int before_id = 0);
    bool MarkChannelRead(int channel_index);
    int ChannelUnreadCount(int channel_index);
    std::vector<std::pair<int, int> > AllChannelUnreadCounts();
    bool DeleteChannelMessage(int64_t id);
    bool ChannelMessageUidExists(const std::string& uid);
    // Path metadata arrives separately, via the 0x88 RX log.
    bool RecordChannelPath(int channel_index, uint32_t sender_timestamp,
        const std::string& msg_text_hash, bool has_snr, double snr, int path_len,
        const std::string& path_bytes_hex, const std::string& region);

    // ── Discovered contacts (adverts not yet added) ──
    std::vector<DiscoveredContact> GetDiscovered();
    bool SaveDiscovered(const DiscoveredContact& d);
    bool DeleteDiscovered(const std::string& public_key_hex);
    bool ClearDiscovered();

    // ── Settings, regions, passwords, blocks ──
    std::string GetSetting(const std::string& key, const std::string& fallback = std::string());
    bool SetSetting(const std::string& key, const std::string& value);
    int GetSettingInt(const std::string& key, int fallback);
    bool SetSettingInt(const std::string& key, int value);
    bool GetSettingBool(const std::string& key, bool fallback);
    bool SetSettingBool(const std::string& key, bool value);

    std::vector<std::string> GetRegions();
    bool AddRegion(const std::string& name);
    bool RemoveRegion(const std::string& name);

    std::string GetRoomPassword(const std::string& room_key_hex);
    bool SetRoomPassword(const std::string& room_key_hex, const std::string& password);
    bool ClearRoomPassword(const std::string& room_key_hex);

    std::vector<std::string> GetBlockedSenders();
    bool BlockSender(const std::string& sender_name);
    bool UnblockSender(const std::string& sender_name);

    // ── Path history (smart routing) ──
    std::vector<PathHistoryEntry> GetPathHistory(const std::string& contact_key_hex);
    bool RecordPathResult(const std::string& contact_key_hex, const std::string& path_bytes_hex,
        int path_hash_size, int hop_count, bool success, bool has_trip, int trip_ms);
    bool ClearPathHistory(const std::string& contact_key_hex);

    // ── Tombstones (sync / backup restore) ──
    bool RecordTombstone(const std::string& uid, const std::string& origin);
    std::vector<std::string> TombstonedUids();

private:
    Storage(const Storage&);
    Storage& operator=(const Storage&);

    bool _Exec(const std::string& sql);
    bool _CreateTables();
    bool _Migrate();
    bool _HasColumn(const std::string& table, const std::string& column);
    bool _AddColumnIfMissing(const std::string& table, const std::string& column,
        const std::string& decl);
    bool _MigrationDone(const std::string& name);
    void _MarkMigrationDone(const std::string& name);
    int64_t _NextSyncSeq();
    void _Fail(const std::string& what);

    sqlite3* fDb;
    std::string fPath;
    std::string fLastError;
    int fBatchDepth;
};

}  // namespace meshy

#endif  // MESHY_CORE_STORAGE_H
