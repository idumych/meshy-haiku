#ifndef MOCK_TEXTCONTROL_H
#define MOCK_TEXTCONTROL_H
#include <Control.h>
#include <TextView.h>
class BTextControl : public BControl {
public:
    BTextControl(const char* name, const char* label, const char* text, BMessage* m)
        : BControl(name, label, m), fText(text?text:""), fTextView(new BTextView("tv")) {}
    ~BTextControl() { delete fTextView; }
    void SetText(const char* t) { fText = t ? t : ""; }
    const char* Text() const { return fText.String(); }
    BTextView* TextView() const { return fTextView; }
    void SetModificationMessage(BMessage* m) { delete m; }
private:
    BString fText;
    BTextView* fTextView;
};
#endif
