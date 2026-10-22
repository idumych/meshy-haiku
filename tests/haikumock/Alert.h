#ifndef MOCK_ALERT_H
#define MOCK_ALERT_H
#include <Window.h>
enum alert_type { B_EMPTY_ALERT, B_INFO_ALERT, B_IDEA_ALERT, B_WARNING_ALERT, B_STOP_ALERT };
#define B_WIDTH_AS_USUAL 0
class BInvoker;
class BAlert : public BWindow {
public:
    BAlert(const char* title, const char* text, const char* b1, const char* b2 = 0,
        const char* b3 = 0, uint32 width = 0, alert_type type = B_INFO_ALERT)
        : BWindow(BRect(0,0,100,100), title, B_MODAL_WINDOW, 0)
        { (void)text;(void)b1;(void)b2;(void)b3;(void)width;(void)type; }
    void SetShortcut(int32, char) {}
    int32 Go() { return 0; }
    status_t Go(BInvoker*) { return B_OK; }
};
#endif
