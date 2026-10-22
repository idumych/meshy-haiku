#ifndef MOCK_HANDLER_H
#define MOCK_HANDLER_H
#include <Message.h>
class BLooper;
class BHandler {
public:
    BHandler(const char* name = 0) { (void)name; }
    virtual ~BHandler() {}
    virtual void MessageReceived(BMessage*) {}
    BLooper* Looper() const { return 0; }
};
#endif
