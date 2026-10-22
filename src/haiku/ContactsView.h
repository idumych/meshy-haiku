// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MESHY_HAIKU_CONTACTSVIEW_H
#define MESHY_HAIKU_CONTACTSVIEW_H

#include <ListItem.h>
#include <View.h>

#include <string>
#include <vector>

#include "Engine.h"

class BButton;
class BListView;
class BStringView;
class BTextControl;

// A two-line row: display name and unread badge on top, type, route and
// last-heard underneath.
class ContactItem : public BListItem {
public:
    ContactItem(const meshy::Contact& contact, int unread);
    virtual ~ContactItem();

    virtual void DrawItem(BView* owner, BRect frame, bool complete);
    virtual void Update(BView* owner, const BFont* font);

    const meshy::Contact& GetContact() const { return fContact; }
    void SetUnread(int n) { fUnread = n; }

private:
    meshy::Contact fContact;
    int fUnread;
    float fBaselineOffset;
    float fSecondLineOffset;
};

// A node heard over the air that is not in the contact list yet.
class DiscoveredItem : public BListItem {
public:
    explicit DiscoveredItem(const meshy::DiscoveredContact& d);
    virtual void DrawItem(BView* owner, BRect frame, bool complete);
    virtual void Update(BView* owner, const BFont* font);
    const meshy::DiscoveredContact& Get() const { return fContact; }

private:
    meshy::DiscoveredContact fContact;
    float fBaselineOffset;
    float fSecondLineOffset;
};

class ContactsView : public BView {
public:
    explicit ContactsView(meshy::Engine* engine);
    virtual ~ContactsView();

    virtual void AttachedToWindow();
    virtual void MessageReceived(BMessage* message);

    void Reload();
    void ReloadDiscovered();

private:
    const meshy::Contact* _Selected() const;
    void _UpdateButtons();
    void _OpenSelected();

    meshy::Engine* fEngine;
    BListView* fList;
    BListView* fDiscoveredList;
    BButton* fOpenButton;
    BButton* fRemoveButton;
    BButton* fResetPathButton;
    BButton* fShareButton;
    BButton* fFavoriteButton;
    BButton* fAddDiscoveredButton;
    BButton* fDiscoverButton;
    BStringView* fDiscoveredLabel;
};

#endif  // MESHY_HAIKU_CONTACTSVIEW_H
