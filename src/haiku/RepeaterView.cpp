// Copyright 2026, the Meshy for Haiku contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RepeaterView.h"

#include <Button.h>
#include <LayoutBuilder.h>
#include <ScrollView.h>
#include <SeparatorView.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>
#include <Window.h>

#include <math.h>

#include "UiMessages.h"
#include "UiUtils.h"

using namespace meshy;

RepeaterView::RepeaterView(Engine* engine)
    : BView("repeater", B_WILL_DRAW), fEngine(engine), fLoggedIn(false)
{
    SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

    fHeaderView = new BStringView("repHeader", "No repeater selected");
    BFont bold(be_bold_font);
    bold.SetSize(bold.Size() * 1.2f);
    fHeaderView->SetFont(&bold);

    fStateView = new BStringView("repState", "");
    fStateView->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_2_TINT);

    fPasswordControl = new BTextControl("password", "Password:", "",
        new BMessage(kMsgRepeaterLogin));
    fPasswordControl->TextView()->HideTyping(true);

    fLoginButton = new BButton("login", "Log in", new BMessage(kMsgRepeaterLogin));
    fLogoutButton = new BButton("logout", "Log out", new BMessage(kMsgRepeaterLogout));
    fStatusButton = new BButton("status", "Status", new BMessage(kMsgRepeaterStatus));
    fNeighborsButton = new BButton("neighbors", "Neighbours", new BMessage(kMsgRepeaterNeighbors));

    fConsole = new BTextView("console");
    fConsole->MakeEditable(false);
    fConsole->SetStylable(true);
    fConsole->SetWordWrap(true);
    fConsole->SetViewUIColor(B_LIST_BACKGROUND_COLOR);
    fConsole->SetInsets(8, 8, 8, 8);
    BFont mono(be_fixed_font);
    fConsole->SetFontAndColor(&mono);
    BScrollView* consoleScroll = new BScrollView("consoleScroll", fConsole, 0, false, true,
        B_FANCY_BORDER);

    fCommandControl = new BTextControl("command", "", "", new BMessage(kMsgRepeaterCli));
    fSendButton = new BButton("runCmd", "Run", new BMessage(kMsgRepeaterCli));

    BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
        .SetInsets(B_USE_WINDOW_INSETS)
        .Add(fHeaderView)
        .Add(fStateView)
        .AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
            .Add(fPasswordControl, 1.0f)
            .Add(fLoginButton)
            .Add(fLogoutButton)
        .End()
        .AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
            .Add(fStatusButton)
            .Add(fNeighborsButton)
            .AddGlue()
        .End()
        .Add(new BSeparatorView(B_HORIZONTAL))
        .Add(consoleScroll, 1.0f)
        .AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
            .Add(fCommandControl, 1.0f)
            .Add(fSendButton)
        .End()
    .End();
}

RepeaterView::~RepeaterView() {}

void RepeaterView::AttachedToWindow()
{
    BView::AttachedToWindow();
    ui::RetargetControls(this, this);
    _UpdateHeader();
}

void RepeaterView::ShowContact(const std::string& contact_key_hex)
{
    if (contact_key_hex != fContactKey) {
        fContactKey = contact_key_hex;
        fLoggedIn = false;
        fConsole->SetText("");
        // Offer a stored password so a return visit is one click.
        std::string saved = fEngine->Store().GetRoomPassword(fContactKey);
        fPasswordControl->SetText(saved.c_str());
    }
    _UpdateHeader();
}

void RepeaterView::_UpdateHeader()
{
    if (fContactKey.empty()) {
        fHeaderView->SetText("No repeater selected");
        fStateView->SetText("Pick a repeater or room server from the contact list.");
        fLoginButton->SetEnabled(false);
        fLogoutButton->SetEnabled(false);
        fStatusButton->SetEnabled(false);
        fNeighborsButton->SetEnabled(false);
        fSendButton->SetEnabled(false);
        return;
    }

    Contact c;
    if (!fEngine->Store().GetContact(fContactKey, &c)) {
        fHeaderView->SetText("Unknown node");
        return;
    }

    fHeaderView->SetText(c.DisplayName().c_str());
    BString state;
    state << ui::ContactTypeName(c.type) << " · " << ui::FormatPath(c.path_length);
    state << " · " << (fLoggedIn ? "logged in" : "not logged in");
    fStateView->SetText(state.String());

    fLoginButton->SetEnabled(!fLoggedIn);
    fLogoutButton->SetEnabled(fLoggedIn);
    fStatusButton->SetEnabled(true);
    fNeighborsButton->SetEnabled(true);
    // Most repeater commands need an authenticated session.
    fSendButton->SetEnabled(fLoggedIn);
}

void RepeaterView::_Append(const BString& text, bool is_input)
{
    int32 start = fConsole->TextLength();
    BString line(text);
    if (line.Length() == 0 || line[line.Length() - 1] != '\n')
        line << "\n";
    fConsole->Insert(line.String());

    BFont mono(be_fixed_font);
    rgb_color color = is_input ? ui_color(B_CONTROL_HIGHLIGHT_COLOR)
                               : ui_color(B_LIST_ITEM_TEXT_COLOR);
    fConsole->SetFontAndColor(start, fConsole->TextLength(), &mono, B_FONT_ALL, &color);
    fConsole->ScrollToOffset(fConsole->TextLength());
}

void RepeaterView::_SendCommand()
{
    if (fContactKey.empty())
        return;
    BString cmd(fCommandControl->Text());
    cmd.Trim();
    if (cmd.Length() == 0)
        return;
    BString echo("> ");
    echo << cmd;
    _Append(echo, true);
    fEngine->SendCliCommand(fContactKey, ui::Std(cmd));
    fCommandControl->SetText("");
}

void RepeaterView::AppendLocalCliReply(BMessage* message)
{
    const char* text = NULL;
    if (message->FindString("text", &text) == B_OK && text != NULL)
        _Append(BString(text), false);
}

void RepeaterView::AppendRemoteCliReply(BMessage* message)
{
    const char* contact = NULL;
    const char* text = NULL;
    message->FindString("contact", &contact);
    message->FindString("text", &text);
    if (text == NULL)
        return;
    // Only show replies from the node currently on screen.
    if (contact != NULL && !fContactKey.empty() && fContactKey != contact)
        return;
    _Append(BString(text), false);
}

void RepeaterView::HandleLoginResult(BMessage* message)
{
    const char* contact = NULL;
    bool success = false;
    message->FindString("contact", &contact);
    message->FindBool("success", &success);
    if (contact != NULL && !fContactKey.empty() && fContactKey != contact)
        return;

    fLoggedIn = success;
    _Append(BString(success ? "— logged in —" : "— login failed —"), false);
    if (success) {
        // Remember the password only once it is known to work.
        BString pw(fPasswordControl->Text());
        fEngine->Store().SetRoomPassword(fContactKey, ui::Std(pw));
    }
    _UpdateHeader();
}

void RepeaterView::HandleStatusResponse(BMessage* message)
{
    const char* contact = NULL;
    message->FindString("contact", &contact);
    if (contact != NULL && !fContactKey.empty() && fContactKey != contact)
        return;

    int32 batt = 0, queue = 0, noise = 0, rssi = 0, uptime = 0, recv = 0, sent = 0, errs = 0;
    double snr = 0.0;
    message->FindInt32("battery_mv", &batt);
    message->FindInt32("queue_len", &queue);
    message->FindInt32("noise_floor", &noise);
    message->FindInt32("last_rssi", &rssi);
    message->FindInt32("uptime_secs", &uptime);
    message->FindInt32("packets_recv", &recv);
    message->FindInt32("packets_sent", &sent);
    message->FindInt32("error_events", &errs);
    message->FindDouble("last_snr", &snr);

    BString s;
    s << "— status —\n";
    s << "  battery    " << ui::FormatVoltage(batt) << "\n";
    s << "  uptime     " << ui::FormatDuration(static_cast<uint32_t>(uptime)) << "\n";
    s << "  queue      " << queue << "\n";
    s << "  noise      " << noise << " dBm\n";
    s << "  last RSSI  " << rssi << " dBm\n";
    s << "  last SNR   " << ui::FormatSnr(snr) << "\n";
    s << "  packets    " << recv << " received, " << sent << " sent\n";
    s << "  errors     " << errs;
    _Append(s, false);
}

void RepeaterView::MessageReceived(BMessage* message)
{
    switch (message->what) {
        case kMsgRepeaterLogin: {
            if (fContactKey.empty())
                break;
            BString pw(fPasswordControl->Text());
            _Append(BString("— logging in —"), true);
            fEngine->Login(fContactKey, ui::Std(pw));
            break;
        }
        case kMsgRepeaterLogout:
            if (!fContactKey.empty()) {
                fEngine->Logout(fContactKey);
                fLoggedIn = false;
                _Append(BString("— logged out —"), true);
                _UpdateHeader();
            }
            break;
        case kMsgRepeaterCli:
            _SendCommand();
            break;
        case kMsgRepeaterStatus:
            if (!fContactKey.empty())
                fEngine->RequestStatus(fContactKey);
            break;
        case kMsgRepeaterNeighbors:
            if (!fContactKey.empty())
                fEngine->RequestNeighbors(fContactKey);
            break;
        default:
            BView::MessageReceived(message);
            break;
    }
}
