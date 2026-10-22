// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MESHY_HAIKU_CHANNELSVIEW_H
#define MESHY_HAIKU_CHANNELSVIEW_H

#include <ListItem.h>
#include <View.h>

#include <vector>

#include "Engine.h"

class BButton;
class BListView;
class BStringView;
class BTextView;

class ChannelItem : public BListItem {
public:
    ChannelItem(const meshy::Channel& channel, int unread);
    virtual void DrawItem(BView* owner, BRect frame, bool complete);
    virtual void Update(BView* owner, const BFont* font);
    const meshy::Channel& Get() const { return fChannel; }

private:
    meshy::Channel fChannel;
    int fUnread;
    float fBaselineOffset;
};

// The channel list beside the transcript of the selected channel.
class ChannelsView : public BView {
public:
    explicit ChannelsView(meshy::Engine* engine);
    virtual ~ChannelsView();

    virtual void AttachedToWindow();
    virtual void MessageReceived(BMessage* message);

    void Reload();
    void SelectChannel(int index);
    void OnMessageAdded(int channel_index);

private:
    void _ReloadTranscript();
    void _Send();
    void _EditChannel(bool create);

    meshy::Engine* fEngine;
    int fSelected;
    BListView* fList;
    BStringView* fHeaderView;
    BTextView* fTranscript;
    BTextView* fInput;
    BButton* fSendButton;
    BButton* fAddButton;
    BButton* fEditButton;
    BButton* fRemoveButton;
};

#endif  // MESHY_HAIKU_CHANNELSVIEW_H
