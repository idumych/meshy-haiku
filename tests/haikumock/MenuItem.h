#ifndef MOCK_MENUITEM_H
#define MOCK_MENUITEM_H
#include <Invoker.h>
#include <Menu.h>
class BMenuItem : public BInvoker {
public:
    BMenuItem(const char* label, BMessage* m, char sc = 0, uint32 mod = 0)
        : fLabel(label?label:"") { SetMessage(m); (void)sc; (void)mod; }
    BMenuItem(BMenu* sub, BMessage* m = 0) : fLabel("") { SetMessage(m); (void)sub; }
    void SetMarked(bool) {}
    bool IsMarked() const { return false; }
    void SetEnabled(bool) {}
    const char* Label() const { return fLabel.String(); }
private:
    BString fLabel;
};
#endif
