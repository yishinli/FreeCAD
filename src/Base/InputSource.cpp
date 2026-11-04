// SPDX-License-Identifier: LGPL-2.1-or-later

/***************************************************************************
 *   Copyright (c) 2011 Jürgen Riegel <juergen.riegel@web.de>              *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#include <algorithm>
#include <array>
#include <istream>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "InputSource.h"
#include "XMLTools.h"

using namespace Base;
using namespace std;


// ---------------------------------------------------------------------------
//  StdInputStream: Constructors and Destructor
// ---------------------------------------------------------------------------

namespace
{

// Replaces embedded NUL bytes and invalid UTF-8 byte sequences with '?' to avoid Xerces treating
// NUL as a string terminator and to keep input reasonably UTF-8-like.
void sanitizeUtf8Bytes(XMLByte* const toFill, const std::size_t len)
{
    auto* data = reinterpret_cast<std::uint8_t*>(
        toFill
    );  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)

    auto markInvalidByte = [&](std::size_t pos) {
        data[pos] = static_cast<std::uint8_t>('?');
    };

    auto isCont = [&](std::uint8_t b) {
        return (b & 0xC0) == 0x80;
    };

    std::size_t i = 0;
    while (i < len) {
        const std::uint8_t b0 = data[i];

        if (b0 == 0) {
            markInvalidByte(i);
            ++i;
            continue;
        }

        if (b0 < 0x80) {
            ++i;
            continue;
        }

        // Reject stray continuation bytes
        if (isCont(b0)) {
            markInvalidByte(i);
            ++i;
            continue;
        }

        // 2-byte sequence: C2..DF 80..BF
        if (b0 >= 0xC2 && b0 <= 0xDF) {
            if (i + 1 >= len || !isCont(data[i + 1])) {
                markInvalidByte(i);
                ++i;
                continue;
            }
            i += 2;
            continue;
        }

        // 3-byte sequences
        if (b0 >= 0xE0 && b0 <= 0xEF) {
            if (i + 2 >= len) {
                markInvalidByte(i);
                ++i;
                continue;
            }

            const std::uint8_t b1 = data[i + 1];
            const std::uint8_t b2 = data[i + 2];
            if (!isCont(b1) || !isCont(b2)) {
                markInvalidByte(i);
                ++i;
                continue;
            }

            // Overlong / surrogate checks
            if (b0 == 0xE0 && b1 < 0xA0) {
                markInvalidByte(i);
                ++i;
                continue;
            }
            if (b0 == 0xED && b1 >= 0xA0) {
                // UTF-16 surrogate range U+D800..U+DFFF
                markInvalidByte(i);
                ++i;
                continue;
            }

            i += 3;
            continue;
        }

        // 4-byte sequences
        if (b0 >= 0xF0 && b0 <= 0xF4) {
            if (i + 3 >= len) {
                markInvalidByte(i);
                ++i;
                continue;
            }

            const std::uint8_t b1 = data[i + 1];
            const std::uint8_t b2 = data[i + 2];
            const std::uint8_t b3 = data[i + 3];
            if (!isCont(b1) || !isCont(b2) || !isCont(b3)) {
                markInvalidByte(i);
                ++i;
                continue;
            }

            // Overlong / > U+10FFFF checks
            if (b0 == 0xF0 && b1 < 0x90) {
                markInvalidByte(i);
                ++i;
                continue;
            }
            if (b0 == 0xF4 && b1 > 0x8F) {
                markInvalidByte(i);
                ++i;
                continue;
            }

            i += 4;
            continue;
        }

        // Invalid lead byte (C0/C1/F5..FF)
        markInvalidByte(i);
        ++i;
    }
}

// The number of bytes at the end of the buffer that start a multi-byte sequence the buffer cuts
// short, or 0 when the buffer ends on a whole character.
std::size_t incompleteTail(const XMLByte* const data, const std::size_t len)
{
    const std::size_t back = std::min<std::size_t>(len, 3);
    for (std::size_t k = 1; k <= back; ++k) {
        const std::uint8_t b = data[len - k];
        if ((b & 0xC0) == 0x80) {
            continue;
        }
        const std::size_t need = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
        return need > k ? k : 0;
    }
    return 0;
}

}  // namespace

struct StdInputStream::TextCodec
{
    // Bytes of a character the last read cut in two, kept for the next read, so that a
    // character on the boundary of two reads is checked whole and not replaced with '?'
    std::array<XMLByte, 3> carry {};
    std::size_t carried = 0;
};

StdInputStream::StdInputStream(
    std::istream& Stream,
    XERCES_CPP_NAMESPACE_QUALIFIER MemoryManager* const manager
)
    : stream(Stream)
    , codec(new TextCodec)
{
    (void)manager;
}


StdInputStream::~StdInputStream() = default;


// ---------------------------------------------------------------------------
//  StdInputStream: Implementation of the input stream interface
// ---------------------------------------------------------------------------
XMLFilePos StdInputStream::curPos() const
{
    const auto pos = stream.tellg();
    if (pos < 0) {
        return static_cast<XMLFilePos>(pos);
    }
    // Bytes kept for the next read have not been handed on yet
    return static_cast<XMLFilePos>(pos) - codec->carried;
}

XMLSize_t StdInputStream::readBytes(XMLByte* const toFill, const XMLSize_t maxToRead)
{
    //
    //  Read up to the maximum bytes requested. We return the number
    //  actually read.
    //
    const std::size_t carried = std::min<std::size_t>(codec->carried, maxToRead);
    std::memcpy(toFill, codec->carry.data(), carried);
    std::memmove(codec->carry.data(), codec->carry.data() + carried, codec->carried - carried);
    codec->carried -= carried;

    const std::size_t wanted = maxToRead - carried;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    stream.read(reinterpret_cast<char*>(toFill + carried), static_cast<std::streamsize>(wanted));
    const auto got = static_cast<std::size_t>(std::max<std::streamsize>(stream.gcount(), 0));
    std::size_t len = carried + got;

    // Keep a character cut short for the next read, unless the stream has ended (then it is
    // invalid) or it is all this read has (returning 0 would end the input). Xerces always asks
    // for its whole raw buffer (48 KiB), never for fewer bytes than are carried.
    if (got == wanted && codec->carried == 0) {
        const std::size_t tail = incompleteTail(toFill, len);
        if (tail > 0 && tail < len) {
            len -= tail;
            std::memcpy(codec->carry.data(), toFill + len, tail);
            codec->carried = tail;
        }
    }

    sanitizeUtf8Bytes(toFill, len);
    return static_cast<XMLSize_t>(len);
}


// ---------------------------------------------------------------------------
//  StdInputSource: Constructors and Destructor
// ---------------------------------------------------------------------------
StdInputSource::StdInputSource(
    std::istream& Stream,
    const char* filePath,
    XERCES_CPP_NAMESPACE::MemoryManager* const manager
)
    : InputSource(manager)
    , stream(Stream)
{
    // we have to set the file name in case an error occurs
    XStr tmpBuf(filePath);
    setSystemId(tmpBuf.unicodeForm());
}


StdInputSource::~StdInputSource() = default;


// ---------------------------------------------------------------------------
//  StdInputSource: InputSource interface implementation
// ---------------------------------------------------------------------------
XERCES_CPP_NAMESPACE::BinInputStream* StdInputSource::makeStream() const
{
    StdInputStream* retStrm = new StdInputStream(stream /*, getMemoryManager()*/);
    return retStrm;
}
