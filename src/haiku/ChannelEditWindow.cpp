// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ChannelEditWindow.h"

#include <Button.h>
#include <LayoutBuilder.h>
#include <StringView.h>
#include <TextControl.h>

#include "Crypto.h"
#include "Protocol.h"
#include "UiMessages.h"
#include "UiUtils.h"

using namespace meshy;

enum {
    kMsgUseHashtag = 'ceHt',
    kMsgUsePublic = 'cePb',
    kMsgOk = 'ceOk',
    kMsgCancel = 'ceCa',
    kMsgNameChanged = 'ceNm',
};

ChannelEditWindow::ChannelEditWindow(int index, const Channel& channel, const BMessenger& target)
    : BWindow(BRect(0, 0, 460, 240), "Channel", B_TITLED_WINDOW,
        B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS | B_NOT_ZOOMABLE
            | B_NOT_RESIZABLE),
      fIndex(index),
      fTarget(target)
{
    BString title;
    title << "Channel " << index;
    SetTitle(title.String());

    fNameControl = new BTextControl("name", "Name:", channel.name.c_str(),
        new BMessage(kMsgNameChanged));
    fNameControl->SetModificationMessage(new BMessage(kMsgNameChanged));

    BString psk;
    if (!channel.psk.empty())
        psk = ToHex(channel.psk).c_str();
    fPskControl = new BTextControl("psk", "Key (32 hex digits):", psk.String(), NULL);

    BButton* hashtagButton = new BButton("hashtag", "Derive from name",
        new BMessage(kMsgUseHashtag));
    BButton* publicButton = new BButton("public", "Use public key", new BMessage(kMsgUsePublic));

    fHintView = new BStringView("hint", "");
    fHintView->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_2_TINT);

    fOkButton = new BButton("ok", "Save", new BMessage(kMsgOk));
    BButton* cancelButton = new BButton("cancel", "Cancel", new BMessage(kMsgCancel));

    BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
        .SetInsets(B_USE_WINDOW_INSETS)
        .Add(fNameControl)
        .Add(fPskControl)
        .AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
            .Add(hashtagButton)
            .Add(publicButton)
            .AddGlue()
        .End()
        .Add(fHintView)
        .AddGlue()
        .AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
            .AddGlue()
            .Add(cancelButton)
            .Add(fOkButton)
        .End()
    .End();

    SetDefaultButton(fOkButton);
    CenterOnScreen();
    _UpdateHint();
}

ChannelEditWindow::~ChannelEditWindow() {}

void ChannelEditWindow::_UpdateHint()
{
    BString name(fNameControl->Text());
    name.Trim();
    if (name.Length() > 0 && name[0] == '#') {
        fHintView->SetText("A name starting with # has a key derived from the name itself.");
    } else {
        fHintView->SetText("Both ends of a channel must use the same key.");
    }
}

void ChannelEditWindow::_UseHashtagKey()
{
    BString name(fNameControl->Text());
    name.Trim();
    if (name.Length() == 0)
        return;
    if (name[0] != '#')
        name.Prepend("#");
    ByteBuffer psk = HashtagChannelPsk(ui::Std(name));
    fNameControl->SetText(name.String());
    fPskControl->SetText(ToHex(psk).c_str());
}

void ChannelEditWindow::_UsePublicKey()
{
    fPskControl->SetText(Channel::kPublicChannelPsk);
    if (BString(fNameControl->Text()).Trim().Length() == 0)
        fNameControl->SetText("public");
}

void ChannelEditWindow::_Save()
{
    BString name(fNameControl->Text());
    name.Trim();
    BString psk(fPskControl->Text());
    psk.Trim();

    ByteBuffer key;
    if (!FromHex(ui::Std(psk), key) || key.size() != 16) {
        fHintView->SetText("The key must be exactly 32 hexadecimal digits.");
        return;
    }

    BMessage msg(kMsgChannelSave);
    msg.AddInt32("index", fIndex);
    msg.AddString("name", name.String());
    msg.AddString("psk", psk.String());
    fTarget.SendMessage(&msg);
    PostMessage(B_QUIT_REQUESTED);
}

void ChannelEditWindow::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case kMsgNameChanged:
            _UpdateHint();
            break;
        case kMsgUseHashtag:
            _UseHashtagKey();
            break;
        case kMsgUsePublic:
            _UsePublicKey();
            break;
        case kMsgOk:
            _Save();
            break;
        case kMsgCancel:
            PostMessage(B_QUIT_REQUESTED);
            break;
        default:
            BWindow::MessageReceived(message);
            break;
    }
}
