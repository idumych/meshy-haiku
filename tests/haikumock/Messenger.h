#ifndef MOCK_MESSENGER_H
#define MOCK_MESSENGER_H
#include <Handler.h>
class BLooper; class BHandler;
class BMessenger {
public:
    BMessenger() {}
    BMessenger(const BHandler* h, const BLooper* l = 0) { (void)h; (void)l; }
    BMessenger(const BMessenger& o) { (void)o; }
    BMessenger& operator=(const BMessenger&) { return *this; }
    status_t SendMessage(BMessage*) const { return B_OK; }
    status_t SendMessage(uint32) const { return B_OK; }
};
#endif
