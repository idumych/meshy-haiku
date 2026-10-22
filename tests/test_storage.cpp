// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Round-trip tests for the SQLite layer.  Also dumps the resulting schema so
// tests/run_diff.sh can compare it with a database created by upstream.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "Models.h"
#include "Storage.h"

using namespace meshy;

static int g_failures = 0;
static int g_checks = 0;

static void Check(const std::string& name, const std::string& got, const std::string& want)
{
    g_checks++;
    if (got != want) {
        g_failures++;
        printf("FAIL %s\n  got  %s\n  want %s\n", name.c_str(), got.c_str(), want.c_str());
    }
}

static void CheckTrue(const std::string& name, bool cond)
{
    g_checks++;
    if (!cond) {
        g_failures++;
        printf("FAIL %s\n", name.c_str());
    }
}

static std::string Num(long long v)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", v);
    return buf;
}

int main(int argc, char** argv)
{
    std::string path = argc > 1 ? argv[1] : "/tmp/meshy_test.db";
    remove(path.c_str());

    Storage db;
    CheckTrue("open", db.Open(path));

    // ── Contacts ──
    Contact c;
    c.public_key = FromHexOrEmpty(
        "aabbccddeeff00112233445566778899aabbccddeeff00112233445566778899");
    c.name = "Alice";
    c.type = CONTACT_CHAT;
    c.flags = CONTACT_FLAG_FAVORITE;
    c.path_length = 2;
    c.path = FromHexOrEmpty("1122");
    c.path_hash_size = 1;
    c.has_location = true;
    c.latitude = 50.0755;
    c.longitude = 14.4378;
    c.last_seen = 1700000000.0;
    CheckTrue("save contact", db.SaveContact(c));

    Contact got;
    CheckTrue("get contact", db.GetContact(c.PublicKeyHex(), &got));
    Check("contact name", got.name, "Alice");
    Check("contact path", ToHex(got.path), "1122");
    CheckTrue("contact favorite", got.IsFavorite());
    CheckTrue("contact location", got.has_location);
    Check("contact lat", Num(static_cast<long long>(got.latitude * 10000)), "500755");
    Check("contact smart default", Num(got.smart_route_selection), "-1");

    // Display name falls back to the advert name until an alias is set.
    Check("display name", got.DisplayName(), "Alice");
    db.SetContactCustomName(c.PublicKeyHex(), "Ali");
    db.GetContact(c.PublicKeyHex(), &got);
    Check("display alias", got.DisplayName(), "Ali");

    // Prefix lookup is how incoming frames resolve a sender.
    Contact byprefix;
    CheckTrue("prefix lookup", db.GetContactByPrefix("aabbccddeeff", &byprefix));
    Check("prefix match", byprefix.name, "Alice");
    CheckTrue("prefix miss", !db.GetContactByPrefix("ffffffffffff", &byprefix));

    // Upsert must update, not duplicate.
    c.name = "Alice2";
    db.SaveContact(c);
    Check("contact count", Num(static_cast<long long>(db.GetContacts().size())), "1");
    db.GetContact(c.PublicKeyHex(), &got);
    Check("contact updated", got.name, "Alice2");

    // ── Messages ──
    Message m;
    m.contact_key_hex = c.PublicKeyHex();
    m.sender_key = c.public_key;
    m.text = "hello";
    m.timestamp = 1700000100.0;
    m.is_outgoing = false;
    m.uid = ContentUidDm(m.contact_key_hex, ToHex(m.sender_key), false, m.timestamp, m.text);
    int64_t id = db.AddMessage(m);
    CheckTrue("add message", id > 0);

    // The same incoming message must not be stored twice.
    CheckTrue("uid exists", db.MessageUidExists(m.uid));
    Check("duplicate ignored", Num(db.AddMessage(m)), "0");
    Check("message count", Num(static_cast<long long>(db.GetMessages(m.contact_key_hex).size())),
        "1");

    Check("unread", Num(db.UnreadCount(m.contact_key_hex)), "1");
    db.MarkContactRead(m.contact_key_hex);
    Check("unread after read", Num(db.UnreadCount(m.contact_key_hex)), "0");

    // Outgoing messages count as read immediately.
    Message out;
    out.contact_key_hex = c.PublicKeyHex();
    out.sender_key = FromHexOrEmpty("00");
    out.text = "reply";
    out.timestamp = 1700000200.0;
    out.is_outgoing = true;
    out.status = MSG_PENDING;
    out.uid = NewUid();
    int64_t out_id = db.AddMessage(out);
    CheckTrue("add outgoing", out_id > 0);
    Check("unread unaffected", Num(db.UnreadCount(m.contact_key_hex)), "0");

    db.UpdateMessageStatus(out_id, MSG_DELIVERED);
    std::vector<Message> msgs = db.GetMessages(m.contact_key_hex);
    Check("messages", Num(static_cast<long long>(msgs.size())), "2");
    // GetMessages returns oldest-first.
    Check("order", msgs[0].text, "hello");
    Check("status", Num(msgs[1].status), Num(MSG_DELIVERED));

    // last_message_at tracks the newest message.
    db.GetContact(c.PublicKeyHex(), &got);
    Check("last_message_at", Num(static_cast<long long>(got.last_message_at)), "1700000200");

    // Deleting leaves a tombstone so a restore cannot resurrect it.
    db.DeleteMessage(id);
    Check("after delete", Num(static_cast<long long>(db.GetMessages(m.contact_key_hex).size())),
        "1");
    CheckTrue("tombstone", db.TombstonedUids().size() == 1);

    // ── Channels ──
    Channel ch;
    ch.index = 0;
    ch.name = "general";
    ch.psk = FromHexOrEmpty(Channel::kPublicChannelPsk);
    CheckTrue("save channel", db.SaveChannel(ch));
    Channel gotch;
    CheckTrue("get channel", db.GetChannel(0, &gotch));
    Check("channel psk", ToHex(gotch.psk), Channel::kPublicChannelPsk);
    // The model default is DEFAULT (-1); the column default of 0 only applies
    // when the column is left unspecified.
    Check("channel notify", Num(gotch.notification_level), Num(NOTIFY_DEFAULT));

    Channel unnamed;
    unnamed.index = 3;
    unnamed.psk = ByteBuffer(16, 0);
    db.SaveChannel(unnamed);
    db.GetChannel(3, &gotch);
    Check("channel fallback name", gotch.DisplayName(), "Channel 3");

    ChannelMessage cm;
    cm.channel_index = 0;
    cm.sender_name = "bob";
    cm.text = "hi all";
    cm.timestamp = 1700000300.0;
    cm.uid = ContentUidChannel(0, "bob", false, cm.timestamp, cm.text);
    CheckTrue("add channel message", db.AddChannelMessage(cm) > 0);
    Check("channel dup ignored", Num(db.AddChannelMessage(cm)), "0");
    Check("channel unread", Num(db.ChannelUnreadCount(0)), "1");
    db.MarkChannelRead(0);
    Check("channel read", Num(db.ChannelUnreadCount(0)), "0");

    db.RecordChannelPath(0, 1700000300u, "abc", true, -7.25, 2, "1122", "eu");
    std::vector<ChannelMessage> cms = db.GetChannelMessages(0);
    Check("channel messages", Num(static_cast<long long>(cms.size())), "1");
    CheckTrue("channel path joined", cms[0].has_snr);
    Check("channel snr", Num(static_cast<long long>(cms[0].snr * 100)), "-725");
    Check("channel region", cms[0].region, "eu");

    // ── Settings and small tables ──
    db.SetSetting("theme", "dark");
    Check("setting", db.GetSetting("theme"), "dark");
    Check("setting default", db.GetSetting("missing", "fallback"), "fallback");
    db.SetSettingInt("tx_power", 22);
    Check("setting int", Num(db.GetSettingInt("tx_power", 0)), "22");
    db.SetSettingBool("auto", true);
    CheckTrue("setting bool", db.GetSettingBool("auto", false));

    db.AddRegion("eu");
    db.AddRegion("eu");  // idempotent
    Check("regions", Num(static_cast<long long>(db.GetRegions().size())), "1");

    db.SetRoomPassword("deadbeef", "hunter2");
    Check("room password", db.GetRoomPassword("deadbeef"), "hunter2");

    db.BlockSender("spammer");
    Check("blocked", Num(static_cast<long long>(db.GetBlockedSenders().size())), "1");

    // ── Path history ──
    db.RecordPathResult(c.PublicKeyHex(), "1122", 1, 2, true, true, 450);
    db.RecordPathResult(c.PublicKeyHex(), "1122", 1, 2, true, true, 400);
    db.RecordPathResult(c.PublicKeyHex(), "3344", 1, 2, false, false, 0);
    std::vector<PathHistoryEntry> hist = db.GetPathHistory(c.PublicKeyHex());
    Check("path history rows", Num(static_cast<long long>(hist.size())), "2");
    // Ordered by weight, so the successful path comes first.
    Check("best path", hist[0].path_bytes, "1122");
    Check("successes", Num(hist[0].success_count), "2");
    Check("failures", Num(hist[1].failure_count), "1");
    CheckTrue("weight ordering", hist[0].route_weight > hist[1].route_weight);

    // ── Discovered contacts ──
    DiscoveredContact d;
    d.public_key_hex = "1111111111111111111111111111111111111111111111111111111111111111";
    d.name = "newnode";
    d.last_seen = 1700000400.0;
    db.SaveDiscovered(d);
    Check("discovered", Num(static_cast<long long>(db.GetDiscovered().size())), "1");
    db.ClearDiscovered();
    Check("discovered cleared", Num(static_cast<long long>(db.GetDiscovered().size())), "0");

    // ── Reopen: data must survive, migrations must be idempotent ──
    db.Close();
    Storage db2;
    CheckTrue("reopen", db2.Open(path));
    Check("persisted contacts", Num(static_cast<long long>(db2.GetContacts().size())), "1");
    Check("persisted setting", db2.GetSetting("theme"), "dark");
    db2.Close();

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
