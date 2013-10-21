#ifndef TYPES_H
#define TYPES_H

#if !defined(__cplusplus)
#include <stdbool.h>
#endif
#include <stddef.h>
#include <stdint.h>

// This gives us all the runtime functions
#include <runtime/std.h>
#include <sys/kheap.h>
// Include panic functions
#include <runtime/panic.h>

#define PANIC(msg) panic(msg, __FILE__, __LINE__);
#define ASSERT(b) ((b) ? (void)0 : panic_assert(__FILE__, __LINE__, #b))

#endif