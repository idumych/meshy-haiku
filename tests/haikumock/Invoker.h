#ifndef MOCK_INVOKER_H
#define MOCK_INVOKER_H
#include <Message.h>
#include <Messenger.h>
class BHandler;
class BInvoker {
public:
    BInvoker() : fMessage(0) {}
    virtual ~BInvoker() { delete fMessage; }
    virtual status_t SetTarget(const BHandler* h, const BLooper* l = 0) { (void)h;(void)l; return B_OK; }
    void SetMessage(BMessage* m) { delete fMessage; fMessage = m; }
    BMessage* Message() const { return fMessage; }
private:
    BMessage* fMessage;
};
#endif
