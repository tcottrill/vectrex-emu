// -----------------------------------------------------------------------------
// deftypes.h
//
// Description:
// Type definitions and memory access structures for emulator framework.
// Originally from RAINE, this header defines standard integer types, memory
// read/write interfaces, and common macros used throughout the system.
// Compatible with C and C++.
//
// Valid Inputs:
//   - Standard C/C++ source inclusion
//
// Notes:
//   - Structures MemoryWriteByte, MemoryReadByte, etc. may be moved to cpu_fw.h
// -----------------------------------------------------------------------------

#ifndef __DEFTYPES_H__
#define __DEFTYPES_H__

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
// Compiler-Specific Warning Management
// -----------------------------------------------------------------------------
//#if defined(_MSC_VER) && _MSC_VER > 1000
//#pragma warning(disable : 4005 4057 4127 4131 4142 4146 4152 4232 4245 4706 4710 4711 4761)
//#endif

// -----------------------------------------------------------------------------
// Basic Integer Type Definitions
// -----------------------------------------------------------------------------
	typedef unsigned char  UINT8;   //  8-bit unsigned
	typedef unsigned short UINT16;  // 16-bit unsigned
	typedef unsigned int   UINT32;  // 32-bit unsigned

	typedef signed char    INT8;    //  8-bit signed
	typedef signed short   INT16;   // 16-bit signed
	typedef signed int     INT32;   // 32-bit signed

	// -----------------------------------------------------------------------------
	// Memory Access Function Structures
	// These are used to register custom handlers for memory regions.
	// Suggestion: Move to cpu_fw.h or memory_map.h if project is modularized.
	// -----------------------------------------------------------------------------
#ifndef _MEMORYREADWRITEBYTE_
#define _MEMORYREADWRITEBYTE_

	typedef struct MemoryWriteByte {
		UINT32 lowAddr;
		UINT32 highAddr;
		void (*memoryCall)(UINT32, UINT8, struct MemoryWriteByte*);
		void* pUserArea;
	} MemoryWriteByte;

	typedef struct MemoryWriteWord {
		UINT32 lowAddr;
		UINT32 highAddr;
		void (*memoryCall)(UINT32, UINT16, struct MemoryWriteWord*);
		void* pUserArea;
	} MemoryWriteWord;

	typedef struct MemoryReadByte {
		UINT32 lowAddr;
		UINT32 highAddr;
		UINT8(*memoryCall)(UINT32, struct MemoryReadByte*);
		void* pUserArea;
	} MemoryReadByte;

	typedef struct MemoryReadWord {
		UINT32 lowAddr;
		UINT32 highAddr;
		UINT16(*memoryCall)(UINT32, struct MemoryReadWord*);
		void* pUserArea;
	} MemoryReadWord;

	typedef struct z80PortWrite {
		UINT16 lowIoAddr;
		UINT16 highIoAddr;
		void (*IOCall)(UINT16, UINT8, struct z80PortWrite*);
		void* pUserArea;
	} z80PortWrite;

	typedef struct z80PortRead {
		UINT16 lowIoAddr;
		UINT16 highIoAddr;
		UINT16(*IOCall)(UINT16, struct z80PortRead*);
		void* pUserArea;
	} z80PortRead;

#endif // _MEMORYREADWRITEBYTE_

// -----------------------------------------------------------------------------
// Memory / port handler declaration + table-building macros (AAE / RAINE style).
// Guarded so a later include of the canonical AAE header won't redefine them.
// -----------------------------------------------------------------------------
#ifndef _MEM_HANDLER_MACROS_
#define _MEM_HANDLER_MACROS_

#define READ_HANDLER_NS(name)    UINT8 name(UINT32 address, struct MemoryReadByte *psMemRead)
#define WRITE_HANDLER_NS(name)   void  name(UINT32 address, UINT8 data, struct MemoryWriteByte *psMemWrite)

#define READ_HANDLER(name)       static UINT8 name(UINT32 address, struct MemoryReadByte *psMemRead)
#define WRITE_HANDLER(name)      static void  name(UINT32 address, UINT8 data, struct MemoryWriteByte *psMemWrite)

#define READ16_HANDLER(name)     static UINT16 name(UINT32 address, struct MemoryReadWord *psMemRead)
#define WRITE16_HANDLER(name)    static void   name(UINT32 address, UINT16 data, struct MemoryWriteWord *psMemWrite)

#define MEM_WRITE(name)   struct MemoryWriteByte name[] = {
#define MEM_READ(name)    struct MemoryReadByte  name[] = {
#define MEM_WRITE16(name) struct MemoryWriteWord name[] = {
#define MEM_READ16(name)  struct MemoryReadWord  name[] = {
// For 8-bit memory handlers that also specify a base pointer
#define MEM_ADDR8(start, end, routine, base)  { (start), (end), (routine), (base) },
// For 16-bit memory handlers (word access)
#define MEM_ADDR16(start, end, routine, base) { (start), (end), (routine), (base) },

#define MEM_ADDR(start,end,routine) {start,end,routine},
#define MEM_END {(UINT32) -1,(UINT32) -1,NULL}};

#define PORT_WRITE_HANDLER(name) static void   name(UINT16 port, UINT8 data, struct z80PortWrite *pPW)
#define PORT_READ_HANDLER(name)  static UINT16 name(UINT16 port, struct z80PortRead *pPR)
#define PORT_WRITE(name) struct z80PortWrite name[] = {
#define PORT_READ(name)  struct z80PortRead  name[] = {
#define PORT_ADDR(start,end,routine) {start,end,routine},
#define PORT_END {(UINT16) -1, (UINT16) -1,NULL}};

#endif // _MEM_HANDLER_MACROS_

// -----------------------------------------------------------------------------
// Boolean and Utility Macros
// -----------------------------------------------------------------------------
//#ifndef TRUE
//#define TRUE  1
//#define FALSE 0
//#endif

//#ifndef MIN
//#define MIN(x, y)   (((x) < (y)) ? (x) : (y))
//#define MAX(x, y)   (((x) > (y)) ? (x) : (y))
//#define MID(x, y, z) MAX((x), MIN((y), (z)))
//#endif

#define bitget(p, m)        ((p) & (m))
#define bitset(p, m)        ((p) |= (m))
#define bitclr(p, m)        ((p) &= ~(m))
#define bitflp(p, m)        ((p) ^= (m))
#define bit_write(c, p, m)  ((c) ? bitset(p, m) : bitclr(p, m))

#ifdef __cplusplus
}
#endif

#endif // __DEFTYPES_H__