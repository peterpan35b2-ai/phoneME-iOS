/*
 *   
 *
 * Copyright  1990-2007 Sun Microsystems, Inc. All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 only, as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details (a copy is
 * included at /legal/license.txt).
 * 
 * You should have received a copy of the GNU General Public License
 * version 2 along with this work; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 * 
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 or visit www.sun.com if you need additional
 * information or have any questions.
 */

#ifndef _INFLATE_INT_H_
#define _INFLATE_INT_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/**
 * @file
 *
 * Utility functions for inflating file data.
 */

typedef long (*GetBytesFunctionType)(void* state, unsigned char* buffer,
                                long numberOfChars);

/* returns a memory handle, call addrFromHandle to use */
typedef void* (*MallocBytesFunctionType)(void* state, int size);

/* handle, is a memory handle */
typedef void (*FreeBytesFunctionType)(void* state, void* handle);

/* This function is to support heaps that compact memory. */
typedef void* (*AddrFromHandleFunctionType)(void* state, void* handle);

/*
 * The HuffmanCodeTable structure contains the dynamic huffman codes for
 * the Code Length Codes or the Distance Codes. The structure is
 * dynamically allocated. We just allocate enough space to contain all
 * possible codes.
 */
typedef struct HuffmanCodeTableHeader {
    uint16_t quickBits;   /* quick bit size */
    uint16_t maxCodeLen;  /* Max number of bits in any code */
} HuffmanCodeTableHeader;

/* If this bit is set in a huffman entry, it means that this is not
 * really an entry, but a pointer into the long codes table.
 * The remaining 15 bits is the offset (in bytes) from the table header
 * to first "long" entry representing this item.
 */
#define HUFFINFO_LONG_MASK 0x8000 /*  high bit set */

#define MAX_QUICK_CXD  6
#define MAX_QUICK_LXL  9

#if !defined(ASSERT)
#    if 0
#        define ASSERT(x) assert((x))
#    else
#        define ASSERT(x) (void)0
#    endif
#endif

/*=========================================================================
 * JAR file reader defines and macros
 *=======================================================================*/

#define BTYPE_NO_COMPRESSION 0x00  
#define BTYPE_FIXED_HUFFMAN  0x01  /* Fixed Huffman Code */
#define BTYPE_DYNA_HUFFMAN   0x02  /* Dynamic Huffman code */
#define BTYPE_INVALID        0x03  /* Invalid code */

#define MAX_BITS 15   /* Maximum number of codes in Huffman Code Table */

#define LITXLEN_BASE 257

#define INFLATEBUFFERSIZE 256

/* A normal sized huffman code table with a 9-bit quick bit */
typedef struct _HuffmanCodeTable {
    struct HuffmanCodeTableHeader h;
    /* There are 1 << quickBit entries.  512 is just an example. 
     * For each entry:
     *     If the high bit is 0:
     *        Next 11 bits give the character
     *        Low   4 bits give the actual number of bits used
     *     If the high bit is 1:
     *        Next 15 bits give the offset (in bytes) from the header to 
     *        the first long entry dealing with this long code.
     */
    uint16_t entries[512];
} HuffmanCodeTable;

/* A small sized huffman code table with a 9-bit quick bit.  We have
 * this so that we can initialize fixedHuffmanDistanceTable in jartables.h
 */
typedef struct shortHuffmanCodeTable {
    struct HuffmanCodeTableHeader h;
    uint16_t entries[32];
} shortHuffmanCodeTable;

typedef struct _InflaterState {
    /* The input stream */
    void* fileState;               /* The state information */
    GetBytesFunctionType getBytes;

    void* heapState;
    MallocBytesFunctionType mallocBytes;
    FreeBytesFunctionType freeBytes;
    AddrFromHandleFunction addrFromHandle;

    int inRemaining;            /* Number of bytes left that we can read */
    uint32_t inDataSize;        /* Number of good bits in inData */
    uint32_t inData;            /* Low inDataSize bits are from stream. */
                                /* High unused bits must be zero */
    /* The output buffer */
    unsigned char* outBuffer;
    unsigned int outOffset;
    unsigned int outLength;
    int outBufferIsAHandle; /* non-zero if decompBuffer is mem handle that
                       must be given to heapObj.addrFromHandle before using */

    int inflateBufferIndex;
    int inflateBufferCount;
    unsigned char inflateBuffer[INFLATEBUFFERSIZE];
} InflaterState;

/*=========================================================================
 * Macros used internally
 *=======================================================================*/

static uint32_t inflater_next_byte(InflaterState* state) {
    long bytesRead;

    if (state->inflateBufferCount > 0) {
        state->inflateBufferCount--;
        return (uint32_t)state->inflateBuffer[state->inflateBufferIndex++];
    }

    bytesRead = state->getBytes(state->fileState, state->inflateBuffer,
                                INFLATEBUFFERSIZE);
    if (bytesRead <= 0 || bytesRead > INFLATEBUFFERSIZE) {
        state->inflateBufferIndex = 0;
        state->inflateBufferCount = 0;
        return UINT32_C(0xff);
    }

    state->inflateBufferIndex = 1;
    state->inflateBufferCount = (int)bytesRead - 1;
    return (uint32_t)state->inflateBuffer[0];
}

#define NEXTBYTE inflater_next_byte(state)

/* Call this macro to make sure that we have at least "j" bits of
 * input available
 */
#define NEEDBITS(j) {                                         \
      while (inDataSize < (j)) {                              \
           inData |= ((uint32_t)NEXTBYTE) << inDataSize;      \
           inRemaining--; inDataSize += 8U;                   \
      }                                                       \
      ASSERT(inDataSize <= 32);                               \
}

/* Return (without consuming) the next "j" bits of the input */
#define NEXTBITS(j) \
       (ASSERT((j) <= inDataSize), \
        inData & ((((uint32_t)1U) << (j)) - (uint32_t)1U))

/* Consume (quietly) "j" bits of input, and make them no longer available
 * to the user
 */
#define DUMPBITS(j) {                                         \
       ASSERT((j) <= inDataSize);                             \
       inData >>= (j);                                        \
       inDataSize -= (j);                                     \
    }  

/* Read bits from the input stream and decode it using the specified
 * table.  The resulting decoded character is placed into "result".
 * If there is a problem, we set error and break or of the loop.
 *
 * For speed, we assume that quickBits = table->h.quickBits and that
 * it has been cached into a variable.
 */
#define GET_HUFFMAN_ENTRY(table, quickBits, result) {                  \
    uint32_t huff = (table)->entries[NEXTBITS(quickBits)];             \
    if (huff & HUFFINFO_LONG_MASK) {                                   \
        uint32_t delta = huff & ~((uint32_t)HUFFINFO_LONG_MASK);       \
        size_t tableIndex =                                            \
            (size_t)(NEXTBITS((table)->h.maxCodeLen) >> (quickBits));  \
        uint16_t longEntry;                                            \
        memcpy(&longEntry,                                             \
               (const unsigned char *)(table) + delta +                \
                   tableIndex * sizeof (longEntry),                    \
               sizeof (longEntry));                                    \
        huff = longEntry;                                              \
    }                                                                  \
    if (huff == 0U) {                                                  \
        error = INFLATE_HUFFMAN_ENTRY_ERROR;                           \
        break;                                                         \
    }                                                                  \
    DUMPBITS(huff & 0xFU);                                             \
    result = huff >> 4;                                                \
}

#define DECLARE_IN_VARIABLES      \
    register uint32_t inData;     \
    register uint32_t inDataSize; \
    register int inRemaining;

/* Copy values from the inflaterState structure to local variables */
#define LOAD_IN                       \
    inData = state->inData;           \
    inDataSize = state->inDataSize;   \
    inRemaining = state->inRemaining; \

/* Copy values from local variables back to the inflaterState structure */
#define STORE_IN                      \
    state->inData = inData;           \
    state->inDataSize = inDataSize;   \
    state->inRemaining = inRemaining; 

#define DECLARE_OUT_VARIABLES                              \
    register unsigned char *outBuffer = state->outBuffer; \
    register unsigned int outLength = state->outLength;    \
    register unsigned int outOffset;

#define LOAD_OUT outOffset = state->outOffset;

#define STORE_OUT state->outOffset = outOffset;

#endif /* INFLATE_INT_H_ */


