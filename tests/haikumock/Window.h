#ifndef MOCK_WINDOW_H
#define MOCK_WINDOW_H
#include <Looper.h>
#include <View.h>
enum window_type { B_TITLED_WINDOW, B_DOCUMENT_WINDOW, B_MODAL_WINDOW, B_FLOATING_WINDOW };
#define B_ASYNCHRONOUS_CONTROLS 1
#define B_AUTO_UPDATE_SIZE_LIMITS 2
#define B_QUIT_ON_WINDOW_CLOSE 4
#define B_NOT_ZOOMABLE 8
#define B_NOT_RESIZABLE 16
#define B_CURRENT_WORKSPACE 0
class BButton; class BLayout;
class BWindow : public BLooper {
public:
    BWindow(BRect f, const char* t, window_type ty, uint32 fl, uint32 ws = B_CURRENT_WORKSPACE)
        : BLooper(t) { (void)f;(void)ty;(void)fl;(void)ws; }
    virtual ~BWindow() {}
    virtual void MessageReceived(BMessage*) {}
    virtual bool QuitRequested() { return true; }
    void Show() {} void Hide() {}
    void SetTitle(const char*) {}
    void CenterOnScreen() {}
    void SetLayout(BLayout*) {}
    void AddChild(BView*) {}
    void SetDefaultButton(BButton*) {}
    void AddShortcut(uint32, uint32, BMessage*) {}
    BView* FindView(const char*) const { return 0; }
};
#endif
