#ifndef MOCK_STRING_H
#define MOCK_STRING_H
#include <SupportDefs.h>
#include <string>
#include <cstdio>
#include <cstdarg>
class BString {
public:
    BString() {}
    BString(const char* s) : fStr(s ? s : "") {}
    BString(const BString& o) : fStr(o.fStr) {}
    BString& operator=(const BString& o) { fStr = o.fStr; return *this; }
    const char* String() const { return fStr.c_str(); }
    int32 Length() const { return (int32)fStr.size(); }
    char operator[](int32 i) const { return fStr[(size_t)i]; }
    BString& operator<<(const char* s) { if (s) fStr += s; return *this; }
    BString& operator<<(const BString& s) { fStr += s.fStr; return *this; }
    BString& operator<<(int32 v) { char b[32]; snprintf(b,sizeof(b),"%d",(int)v); fStr += b; return *this; }
    BString& operator<<(uint32 v) { char b[32]; snprintf(b,sizeof(b),"%u",(unsigned)v); fStr += b; return *this; }
    BString& operator<<(long long v) { char b[32]; snprintf(b,sizeof(b),"%lld",v); fStr += b; return *this; }
    BString& operator<<(long v) { char b[32]; snprintf(b,sizeof(b),"%ld",v); fStr += b; return *this; }
    BString& operator<<(unsigned long v) { char b[32]; snprintf(b,sizeof(b),"%lu",v); fStr += b; return *this; }
    BString& operator<<(double v) { char b[64]; snprintf(b,sizeof(b),"%g",v); fStr += b; return *this; }
    BString& Trim() { size_t a=fStr.find_first_not_of(" \t\n\r"); size_t b=fStr.find_last_not_of(" \t\n\r");
        fStr = (a==std::string::npos)?std::string():fStr.substr(a,b-a+1); return *this; }
    BString& Truncate(int32 n) { if ((size_t)n < fStr.size()) fStr.resize((size_t)n); return *this; }
    BString& Prepend(const char* s) { fStr = std::string(s) + fStr; return *this; }
    BString& SetTo(const char* s) { fStr = s ? s : ""; return *this; }
    void SetToFormat(const char* fmt, ...) { char b[1024]; va_list ap; va_start(ap,fmt);
        vsnprintf(b,sizeof(b),fmt,ap); va_end(ap); fStr = b; }
    bool operator==(const BString& o) const { return fStr == o.fStr; }
private:
    std::string fStr;
};
#endif
