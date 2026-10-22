#ifndef MOCK_MESSAGE_H
#define MOCK_MESSAGE_H
#include <SupportDefs.h>
#include <String.h>
class BMessage {
public:
    uint32 what;
    BMessage() : what(0) {}
    BMessage(uint32 w) : what(w) {}
    status_t AddInt32(const char*, int32) { return B_OK; }
    status_t AddInt64(const char*, int64) { return B_OK; }
    status_t AddBool(const char*, bool) { return B_OK; }
    status_t AddDouble(const char*, double) { return B_OK; }
    status_t AddFloat(const char*, float) { return B_OK; }
    status_t AddString(const char*, const char*) { return B_OK; }
    status_t AddData(const char*, type_code, const void*, ssize_t) { return B_OK; }
    status_t FindInt32(const char*, int32*) const { return B_OK; }
    status_t FindInt64(const char*, int64*) const { return B_OK; }
    status_t FindBool(const char*, bool*) const { return B_OK; }
    status_t FindDouble(const char*, double*) const { return B_OK; }
    status_t FindFloat(const char*, float*) const { return B_OK; }
    status_t FindString(const char*, const char**) const { return B_OK; }
    status_t FindString(const char*, int32, const char**) const { return B_OK; }
    status_t FindData(const char*, type_code, const void**, ssize_t*) const { return B_OK; }
};
#define B_QUIT_REQUESTED 'QUIT'
#define B_ABOUT_REQUESTED 'ABOU'
#endif
