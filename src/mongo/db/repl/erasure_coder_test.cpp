#include "mongo/unittest/unittest.h"

#include "mongo/db/repl/erasure_coder.h"

using namespace std::string_view_literals;

TEST(ErasureCoderTest, EncodeAndDecodeData) {
    ErasureCoder erasureCoder(2, 4);
    const auto data = "Hello, world!"sv;
    const auto splits = erasureCoder.encodeData(reinterpret_cast<const std::byte *>(data.data()), data.size() + 1);
    std::vector<std::pair<const std::byte *, int>> partialSplits = {{splits[1].data(), 1},
                                                                    {splits[2].data(), 2}};
    const auto decodedData = erasureCoder.decodeData(partialSplits, (data.size() + 1) / 2);
    ASSERT_EQ(reinterpret_cast<const char *>(decodedData.data()), data);
}
