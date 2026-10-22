// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ContactsView.h"

#include <Alert.h>
#include <Button.h>
#include <ControlLook.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <Region.h>

#include <math.h>
#include <ScrollView.h>
#include <SeparatorView.h>
#include <StringView.h>
#include <Window.h>

#include <map>

#include "UiMessages.h"
#include "UiUtils.h"

using namespace meshy;

// ─── ContactItem ─────────────────────────────────────────────────

ContactItem::ContactItem(const Contact& contact, int unread)
    : BListItem(), fContact(contact), fUnread(unread), fBaselineOffset(0), fSecondLineOffset(0)
{
}

ContactItem::~ContactItem() {}

void ContactItem::Update(BView* owner, const BFont* font)
{
    BListItem::Update(owner, font);
    font_height fh;
    font->GetHeight(&fh);
    float line = ceilf(fh.ascent + fh.descent + fh.leading);
    fBaselineOffset = ceilf(fh.ascent) + 3.0f;
    fSecondLineOffset = fBaselineOffset + line;
    // Two lines of text plus a little breathing room.
    SetHeight(line * 2 + 10.0f);
}

void ContactItem::DrawItem(BView* owner, BRect frame, bool complete)
{
    rgb_color lowColor = owner->LowColor();

    if (IsSelected() || complete) {
        rgb_color color = IsSelected() ? ui_color(B_LIST_SELECTED_BACKGROUND_COLOR)
                                       : owner->ViewColor();
        owner->SetLowColor(color);
        owner->FillRect(frame, B_SOLID_LOW);
    }

    rgb_color textColor = IsSelected() ? ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR)
                                       : ui_color(B_LIST_ITEM_TEXT_COLOR);
    owner->SetHighColor(textColor);

    float left = frame.left + 8.0f;
    float right = frame.right - 8.0f;

    // ── First line: name, favourite marker, unread badge ──
    BString name;
    if (fContact.IsFavorite())
        name << "\xe2\x98\x85 ";  // black star
    name << fContact.DisplayName().c_str();

    BFont bold(be_bold_font);
    BFont plain(be_plain_font);
    owner->SetFont(fUnread > 0 ? &bold : &plain);

    BString badge;
    float badgeWidth = 0.0f;
    if (fUnread > 0) {
        badge << fUnread;
        badgeWidth = owner->StringWidth(badge.String()) + 16.0f;
    }

    BString truncated(name);
    owner->TruncateString(&truncated, B_TRUNCATE_END, right - left - badgeWidth - 8.0f);
    owner->DrawString(truncated.String(), BPoint(left, frame.top + fBaselineOffset));

    if (fUnread > 0) {
        // A filled pill, drawn in the accent colour.
        float h = ceilf(be_plain_font->Size()) + 4.0f;
        BRect pill(right - badgeWidth, frame.top + 4.0f, right, frame.top + 4.0f + h);
        rgb_color accent = ui_color(B_CONTROL_HIGHLIGHT_COLOR);
        owner->SetHighColor(accent);
        owner->FillRoundRect(pill, h / 2.0f, h / 2.0f);
        owner->SetHighColor(255, 255, 255);
        float tw = owner->StringWidth(badge.String());
        owner->DrawString(badge.String(),
            BPoint(pill.left + (pill.Width() - tw) / 2.0f, frame.top + fBaselineOffset));
    }

    // ── Second line: type, route, last heard ──
    BFont small(be_plain_font);
    small.SetSize(small.Size() * 0.9f);
    owner->SetFont(&small);
    owner->SetHighColor(IsSelected() ? textColor : tint_color(textColor, B_LIGHTEN_1_TINT));

    BString detail;
    detail << ui::ContactTypeName(fContact.type);
    detail << " · " << ui::FormatPath(fContact.path_length);
    if (HasTime(fContact.last_seen))
        detail << " · " << ui::FormatRelative(fContact.last_seen);

    BString detailTrunc(detail);
    owner->TruncateString(&detailTrunc, B_TRUNCATE_END, right - left);
    owner->DrawString(detailTrunc.String(), BPoint(left, frame.top + fSecondLineOffset));

    owner->SetFont(be_plain_font);
    owner->SetLowColor(lowColor);
}

// ─── DiscoveredItem ──────────────────────────────────────────────

DiscoveredItem::DiscoveredItem(const DiscoveredContact& d)
    : BListItem(), fContact(d), fBaselineOffset(0), fSecondLineOffset(0)
{
}

void DiscoveredItem::Update(BView* owner, const BFont* font)
{
    BListItem::Update(owner, font);
    font_height fh;
    font->GetHeight(&fh);
    float line = ceilf(fh.ascent + fh.descent + fh.leading);
    fBaselineOffset = ceilf(fh.ascent) + 3.0f;
    fSecondLineOffset = fBaselineOffset + line;
    SetHeight(line * 2 + 10.0f);
}

void DiscoveredItem::DrawItem(BView* owner, BRect frame, bool complete)
{
    if (IsSelected() || complete) {
        rgb_color color = IsSelected() ? ui_color(B_LIST_SELECTED_BACKGROUND_COLOR)
                                       : owner->ViewColor();
        owner->SetLowColor(color);
        owner->FillRect(frame, B_SOLID_LOW);
    }
    rgb_color textColor = IsSelected() ? ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR)
                                       : ui_color(B_LIST_ITEM_TEXT_COLOR);
    owner->SetHighColor(textColor);

    float left = frame.left + 8.0f;
    BString name(fContact.name.empty() ? "(unnamed)" : fContact.name.c_str());
    owner->DrawString(name.String(), BPoint(left, frame.top + fBaselineOffset));

    BFont small(be_plain_font);
    small.SetSize(small.Size() * 0.9f);
    owner->SetFont(&small);
    BString detail;
    detail << ui::ContactTypeName(fContact.type) << " · "
           << fContact.public_key_hex.substr(0, 12).c_str();
    if (HasTime(fContact.last_seen))
        detail << " · " << ui::FormatRelative(fContact.last_seen);
    owner->DrawString(detail.String(), BPoint(left, frame.top + fSecondLineOffset));
    owner->SetFont(be_plain_font);
}

// ─── ContactsView ────────────────────────────────────────────────

ContactsView::ContactsView(Engine* engine) : BView("contacts", B_WILL_DRAW), fEngine(engine)
{
    SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

    fList = new BListView("contactList", B_SINGLE_SELECTION_LIST);
    fList->SetSelectionMessage(new BMessage(kMsgContactSelected));
    fList->SetInvocationMessage(new BMessage(kMsgContactInvoked));
    BScrollView* scroll = new BScrollView("contactScroll", fList, 0, false, true, B_FANCY_BORDER);

    fOpenButton = new BButton("open", "Open", new BMessage(kMsgContactInvoked));
    fFavoriteButton = new BButton("fav", "Favourite", new BMessage(kMsgContactFavorite));
    fResetPathButton = new BButton("reset", "Reset route", new BMessage(kMsgContactResetPath));
    fShareButton = new BButton("share", "Share", new BMessage(kMsgContactShare));
    fRemoveButton = new BButton("remove", "Remove…", new BMessage(kMsgContactRemove));

    fDiscoveredLabel = new BStringView("discLabel", "Heard recently");
    fDiscoveredLabel->SetFont(be_bold_font);

    fDiscoveredList = new BListView("discList", B_SINGLE_SELECTION_LIST);
    fDiscoveredList->SetInvocationMessage(new BMessage(kMsgImportDiscovered));
    BScrollView* discScroll = new BScrollView("discScroll", fDiscoveredList, 0, false, true,
        B_FANCY_BORDER);
    discScroll->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 150));

    fAddDiscoveredButton = new BButton("add", "Add contact", new BMessage(kMsgImportDiscovered));
    fDiscoverButton = new BButton("discover", "Discover nodes", new BMessage(kMsgDiscoverNodes));

    BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
        .SetInsets(B_USE_WINDOW_INSETS)
        .Add(scroll, 1.0f)
        .AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
            .Add(fOpenButton)
            .Add(fFavoriteButton)
            .Add(fResetPathButton)
            .Add(fShareButton)
            .AddGlue()
            .Add(fRemoveButton)
        .End()
        .Add(new BSeparatorView(B_HORIZONTAL))
        .Add(fDiscoveredLabel)
        .Add(discScroll, 0.4f)
        .AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
            .Add(fAddDiscoveredButton)
            .AddGlue()
            .Add(fDiscoverButton)
        .End()
    .End();
}

ContactsView::~ContactsView() {}

void ContactsView::AttachedToWindow()
{
    BView::AttachedToWindow();
    fList->SetTarget(this);
    fDiscoveredList->SetTarget(this);
    fOpenButton->SetTarget(this);
    fFavoriteButton->SetTarget(this);
    fResetPathButton->SetTarget(this);
    fShareButton->SetTarget(this);
    fRemoveButton->SetTarget(this);
    fAddDiscoveredButton->SetTarget(this);
    fDiscoverButton->SetTarget(this);
    Reload();
    ReloadDiscovered();
}

void ContactsView::Reload()
{
    // Remember the selection so a background refresh does not move it.
    std::string selected;
    const Contact* sel = _Selected();
    if (sel != NULL)
        selected = sel->PublicKeyHex();

    for (int32 i = fList->CountItems() - 1; i >= 0; i--)
        delete fList->RemoveItem(i);

    std::map<std::string, int> unread;
    std::vector<std::pair<std::string, int> > counts = fEngine->Store().AllUnreadCounts();
    for (size_t i = 0; i < counts.size(); i++)
        unread[counts[i].first] = counts[i].second;

    std::vector<Contact> contacts = fEngine->Store().GetContacts();
    // Favourites first, then anything with unread traffic, then by name.
    std::stable_sort(contacts.begin(), contacts.end(),
        [&unread](const Contact& a, const Contact& b) {
            if (a.IsFavorite() != b.IsFavorite())
                return a.IsFavorite();
            bool ua = unread.count(a.PublicKeyHex()) > 0;
            bool ub = unread.count(b.PublicKeyHex()) > 0;
            if (ua != ub)
                return ua;
            return a.DisplayName() < b.DisplayName();
        });

    int32 restore = -1;
    for (size_t i = 0; i < contacts.size(); i++) {
        std::map<std::string, int>::iterator it = unread.find(contacts[i].PublicKeyHex());
        fList->AddItem(new ContactItem(contacts[i], it != unread.end() ? it->second : 0));
        if (!selected.empty() && contacts[i].PublicKeyHex() == selected)
            restore = static_cast<int32>(i);
    }
    if (restore >= 0)
        fList->Select(restore);
    _UpdateButtons();
}

void ContactsView::ReloadDiscovered()
{
    for (int32 i = fDiscoveredList->CountItems() - 1; i >= 0; i--)
        delete fDiscoveredList->RemoveItem(i);

    std::vector<DiscoveredContact> discovered = fEngine->Store().GetDiscovered();
    for (size_t i = 0; i < discovered.size(); i++)
        fDiscoveredList->AddItem(new DiscoveredItem(discovered[i]));

    BString label("Heard recently");
    if (!discovered.empty())
        label << "  (" << static_cast<int>(discovered.size()) << ")";
    fDiscoveredLabel->SetText(label.String());
}

const Contact* ContactsView::_Selected() const
{
    int32 index = fList->CurrentSelection(0);
    if (index < 0)
        return NULL;
    ContactItem* item = dynamic_cast<ContactItem*>(fList->ItemAt(index));
    return item != NULL ? &item->GetContact() : NULL;
}

void ContactsView::_UpdateButtons()
{
    const Contact* c = _Selected();
    bool has = (c != NULL);
    fOpenButton->SetEnabled(has);
    fFavoriteButton->SetEnabled(has);
    fResetPathButton->SetEnabled(has);
    fShareButton->SetEnabled(has);
    fRemoveButton->SetEnabled(has);
    if (has)
        fFavoriteButton->SetLabel(c->IsFavorite() ? "Unfavourite" : "Favourite");
}

void ContactsView::_OpenSelected()
{
    const Contact* c = _Selected();
    if (c == NULL)
        return;

    // Each contact type has its own screen.
    BMessage msg(kShowChat);
    if (c->type == CONTACT_REPEATER || c->type == CONTACT_ROOM)
        msg.what = kShowRepeater;
    else if (c->type == CONTACT_SENSOR)
        msg.what = kShowSensor;
    // A room server is also a chat target, but its console is the useful
    // default; the repeater view links through to the chat.
    msg.AddString("contact", c->PublicKeyHex().c_str());
    Window()->PostMessage(&msg);
}

void ContactsView::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case kMsgContactSelected:
            _UpdateButtons();
            break;

        case kMsgContactInvoked:
            _OpenSelected();
            break;

        case kMsgContactFavorite: {
            const Contact* c = _Selected();
            if (c == NULL)
                break;
            int flags = c->flags ^ CONTACT_FLAG_FAVORITE;
            fEngine->Store().SetContactFlags(c->PublicKeyHex(), flags);
            Reload();
            break;
        }

        case kMsgContactResetPath: {
            const Contact* c = _Selected();
            if (c != NULL)
                fEngine->ResetPath(c->PublicKeyHex());
            break;
        }

        case kMsgContactShare: {
            const Contact* c = _Selected();
            if (c != NULL)
                fEngine->ShareContact(c->PublicKeyHex());
            break;
        }

        case kMsgContactRemove: {
            const Contact* c = _Selected();
            if (c == NULL)
                break;
            BString text("Remove \"");
            text << c->DisplayName().c_str() << "\" and its messages?";
            BAlert* alert = new BAlert("Meshy", text.String(), "Cancel", "Remove", NULL,
                B_WIDTH_AS_USUAL, B_WARNING_ALERT);
            alert->SetShortcut(0, B_ESCAPE);
            if (alert->Go() == 1) {
                fEngine->RemoveContact(c->PublicKeyHex());
                Reload();
            }
            break;
        }

        case kMsgImportDiscovered: {
            int32 index = fDiscoveredList->CurrentSelection(0);
            if (index < 0)
                break;
            DiscoveredItem* item = dynamic_cast<DiscoveredItem*>(fDiscoveredList->ItemAt(index));
            if (item != NULL)
                fEngine->ImportDiscovered(item->Get().public_key_hex);
            break;
        }

        case kMsgDiscoverNodes:
            fEngine->SendDiscoverRequest();
            break;

        default:
            BView::MessageReceived(message);
            break;
    }
}
