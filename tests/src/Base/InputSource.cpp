#include <gtest/gtest.h>

#include <Base/InputSource.h>

#include <sstream>
#include <string>

TEST(StdInputStream, SanitizesNullAndInvalidUtf8)
{
    // bytes: 'A' 0xC3 0x28 'B' 0x00 'C' 0x80
    // - 0xC3 0x28 is an invalid 2-byte sequence; we replace the lead byte with '?'
    // - 0x00 is replaced with '?'
    // - 0x80 is a stray continuation byte; replaced with '?'
    std::string bytes;
    bytes.push_back('A');
    bytes.push_back(static_cast<char>(0xC3));
    bytes.push_back(static_cast<char>(0x28));
    bytes.push_back('B');
    bytes.push_back(static_cast<char>(0x00));
    bytes.push_back('C');
    bytes.push_back(static_cast<char>(0x80));

    std::istringstream in(std::string(bytes.data(), bytes.size()), std::ios::binary);
    Base::StdInputStream stream(in);

    XMLByte out[32] {};
    const XMLSize_t n = stream.readBytes(out, sizeof(out));
    ASSERT_EQ(n, bytes.size());

    // Ensure no embedded NUL remains.
    for (XMLSize_t i = 0; i < n; ++i) {
        EXPECT_NE(out[i], static_cast<XMLByte>(0)) << "NUL byte remained at index " << i;
    }

    EXPECT_EQ(out[0], static_cast<XMLByte>('A'));
    EXPECT_EQ(out[1], static_cast<XMLByte>('?'));  // invalid lead byte replaced
    EXPECT_EQ(out[2], static_cast<XMLByte>('('));  // ASCII preserved
    EXPECT_EQ(out[3], static_cast<XMLByte>('B'));
    EXPECT_EQ(out[4], static_cast<XMLByte>('?'));  // embedded NUL replaced
    EXPECT_EQ(out[5], static_cast<XMLByte>('C'));
    EXPECT_EQ(out[6], static_cast<XMLByte>('?'));  // stray continuation replaced
}

namespace
{

std::string readAll(Base::StdInputStream& stream, XMLSize_t chunk)
{
    std::string result;
    std::string buffer(chunk, '\0');
    for (;;) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        const XMLSize_t n = stream.readBytes(reinterpret_cast<XMLByte*>(buffer.data()), chunk);
        if (n == 0) {
            return result;
        }
        result.append(buffer.data(), n);
    }
}

}  // namespace

TEST(StdInputStream, KeepsCharactersSplitBetweenReads)
{
    // U+7D44 (3 bytes), U+1F600 (4 bytes) and U+00E9 (2 bytes) at every offset against reads of 4
    // to 8 bytes, so that reads end at every byte inside them
    const std::string chars[] = {"\xE7\xB5\x84", "\xF0\x9F\x98\x80", "\xC3\xA9"};
    for (const auto& ch : chars) {
        for (std::size_t pad = 0; pad < 8; ++pad) {
            for (XMLSize_t chunk = 4; chunk <= 8; ++chunk) {
                const std::string bytes = std::string(pad, 'x') + ch + "y" + ch + ch;
                std::istringstream in(bytes, std::ios::binary);
                Base::StdInputStream stream(in);
                EXPECT_EQ(readAll(stream, chunk), bytes) << "pad " << pad << ", chunk " << chunk;
            }
        }
    }
}

TEST(StdInputStream, ReplacesCharacterCutShortByTheEnd)
{
    const std::string bytes = "AB\xE7\xB5";
    for (XMLSize_t chunk = 1; chunk <= 5; ++chunk) {
        std::istringstream in(bytes, std::ios::binary);
        Base::StdInputStream stream(in);
        EXPECT_EQ(readAll(stream, chunk), "AB??") << "chunk " << chunk;
    }
}
