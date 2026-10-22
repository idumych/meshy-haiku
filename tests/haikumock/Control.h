#ifndef MOCK_CONTROL_H
#define MOCK_CONTROL_H
#include <View.h>
#include <Invoker.h>
class BControl : public BView, public BInvoker {
public:
    BControl(const char* name, const char* label, BMessage* m, uint32 flags = B_WILL_DRAW)
        : BView(name, flags), fValue(0), fLabel(label ? label : "") { SetMessage(m); }
    virtual void SetEnabled(bool) {}
    bool IsEnabled() const { return true; }
    virtual void SetValue(int32 v) { fValue = v; }
    int32 Value() const { return fValue; }
    void SetLabel(const char* l) { fLabel = l ? l : ""; }
    const char* Label() const { return fLabel.String(); }
private:
    int32 fValue;
    BString fLabel;
};
#endif
