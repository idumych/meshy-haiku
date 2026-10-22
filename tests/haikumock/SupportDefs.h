// Test-only stand-in for Haiku headers. NOT part of the application; it exists
// so the UI sources can be type-checked on a non-Haiku host. It mirrors only
// the API surface this project uses and is not an API reference.
#ifndef MOCK_SUPPORTDEFS_H
#define MOCK_SUPPORTDEFS_H
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
typedef int32_t int32; typedef uint32_t uint32; typedef int64_t int64;
typedef uint64_t uint64; typedef int16_t int16; typedef uint16_t uint16;
typedef int8_t int8; typedef uint8_t uint8; typedef uint8_t uchar;
typedef int32 status_t; typedef int64 bigtime_t; typedef uint32 type_code;
typedef int32 thread_id; typedef int32 team_id;
#define B_OK 0
#define B_ERROR (-1)
#define B_NO_MEMORY (-2)
#define B_BAD_VALUE (-3)
#define B_NAME_NOT_FOUND (-4)
#define B_RAW_TYPE 'RAWT'
#define B_STRING_TYPE 'CSTR'
#endif
