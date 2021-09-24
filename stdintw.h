#ifndef __STDINTW_H
#define __STDINTW_H

#ifdef _MSC_VER

// there is no stdint.h in Visual Studio, I have to declare the types here
//#define WIN32_LEAN_AND_MEAN
//#include <windows.h>

typedef char              int8_t;
typedef unsigned char     uint8_t;
typedef short             int16_t;
typedef unsigned short    uint16_t;
typedef unsigned long     uint32_t;
typedef long              int32_t;
//typedef size_t        ssize_t;
typedef __int64           int64_t;
typedef unsigned __int64  uint64_t;

#else
#include <stdint.h>
#endif

#endif
