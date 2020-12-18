#include <algorithm>
#include <isa-l.h>
#include <set>

#include "mongo/db/index/index_descriptor.h"
#include "mongo/db/repl/erasure_coder.h"

ErasureCoder::ErasureCoder(int numSourceSplits, int numTotalSplits)
    : _k(numSourceSplits),
      _m(numTotalSplits),
      _encodeMatrix(_m, _k),
      _encodeTable((_m - _k) * _k * 32) {
    // Initialize the encode matrix and encode table.
    gf_gen_cauchy1_matrix(_encodeMatrix.data(), _m, _k);
    ec_init_tables(_k, _m - _k, &_encodeMatrix(_k, 0), _encodeTable.data());
}

std::vector<std::vector<std::byte>> ErasureCoder::encodeData(const std::byte* data,
                                                             int size) const {
    // Initialize buffers for source splits and parity splits.
    const auto splitSize = size % _k == 0 ? size / _k : size / _k + 1;
    std::vector<std::vector<std::byte>> splits(_m, std::vector<std::byte>(splitSize));
    for (auto i = 0; i < _k - 1; ++i)
        std::copy(data + i * splitSize, data + (i + 1) * splitSize, splits[i].begin());
    std::copy(data + (_k - 1) * splitSize, data + size, splits[_k - 1].begin());

    // Create views of buffers for source splits and parity splits respectively.
    std::vector<const std::byte*> sourceSplits(_k);
    std::vector<std::byte*> paritySplits(_m - _k);
    std::transform(splits.cbegin(),
                   splits.cbegin() + _k,
                   sourceSplits.begin(),
                   [](const std::vector<std::byte>& split) { return split.data(); });
    std::transform(splits.begin() + _k,
                   splits.end(),
                   paritySplits.begin(),
                   [](std::vector<std::byte>& split) { return split.data(); });

    // Compute parity splits.
    ec_encode_data(splitSize,
                   _k,
                   _m - _k,
                   const_cast<uint8_t*>(_encodeTable.data()),
                   reinterpret_cast<uint8_t**>(const_cast<std::byte**>(sourceSplits.data())),
                   reinterpret_cast<uint8_t**>(paritySplits.data()));

    return splits;
}

std::vector<std::byte> ErasureCoder::decodeData(
    const std::vector<std::pair<const std::byte*, int>>& splitsWithIdxs, int splitSize) const {
    assert(splitsWithIdxs.size() == _k);

    // Initialize buffers for data and parity splits, and compute indexes of missing splits.
    std::vector<std::byte> data(_k * splitSize);
    std::vector<std::vector<std::byte>> paritySplits(_m - _k, std::vector<std::byte>(splitSize));
    std::set<int> missingIdxs;
    for (auto idx = 0; idx < _m; ++idx)
        missingIdxs.insert(idx);
    for (auto [split, idx] : splitsWithIdxs) {
        std::copy(split,
                  split + splitSize,
                  idx < _k ? &data[idx * splitSize] : paritySplits[idx - _k].data());
        missingIdxs.erase(idx);
    }

    // Initialize the decode matrix.
    Matrix<uint8_t> decodeMatrix(_m, _k);
    for (auto i = 0; i < _k; ++i) {
        const auto idx = splitsWithIdxs[i].second;
        std::copy(&_encodeMatrix(idx, 0), &_encodeMatrix(idx + 1, 0), &decodeMatrix(i, 0));
    }
    Matrix<uint8_t> inverseMatrix(_k, _k);
    gf_invert_matrix(decodeMatrix.data(), inverseMatrix.data(), _k);
    for (auto [i, it] = std::make_pair(0, missingIdxs.cbegin()); i < _m - _k; ++i, ++it) {
        const auto missingIdx = *it;
        if (missingIdx < _k)
            std::copy(&inverseMatrix(missingIdx, 0),
                      &inverseMatrix(missingIdx + 1, 0),
                      &decodeMatrix(i + _k, 0));
        else
            for (auto j = 0; j < _k; ++j) {
                uint8_t value = 0;
                for (auto l = 0; l < _k; ++l)
                    value ^= gf_mul(_encodeMatrix(missingIdx, l), inverseMatrix(l, j));
                decodeMatrix(i + _k, j) = value;
            }
    }

    // Initialize the decode table.
    std::vector<uint8_t> decodeTable((_m - _k) * _k * 32);
    ec_init_tables(_k, _m - _k, &decodeMatrix(_k, 0), decodeTable.data());

    // Initialize views of buffers for existing splits and missing splits.
    std::vector<const std::byte*> splits(_k);
    std::vector<std::byte*> missingSplits(_m - _k);
    std::transform(
        splitsWithIdxs.cbegin(),
        splitsWithIdxs.cend(),
        splits.begin(),
        [](std::pair<const std::byte*, int> splitWithIdx) { return splitWithIdx.first; });
    std::transform(missingIdxs.cbegin(), missingIdxs.cend(), missingSplits.begin(), [&](int idx) {
        return idx < _k ? &data[idx * splitSize] : paritySplits[idx - _k].data();
    });

    // Compute missing splits.
    ec_encode_data(splitSize,
                   _k,
                   _m - _k,
                   decodeTable.data(),
                   reinterpret_cast<uint8_t**>(const_cast<std::byte**>(splits.data())),
                   reinterpret_cast<uint8_t**>(missingSplits.data()));

    return data;
}

BSONObj ErasureCoder::encodeDocument(OperationContext& opCtx,
                                     const IndexCatalog& indexCatalog,
                                     const BSONObj& document) const {
    BSONObjBuilder documentBuilder;
    BufBuilder nonIndexedElements;

    static const auto isFieldIndexed = [&](std::string_view name) {
        for (auto it = indexCatalog.getIndexIterator(&opCtx, true); it->more();) {
            const auto* const indexDescriptor = it->next()->descriptor();
            if (indexDescriptor->indexName().find(name) != std::string::npos)
                return true;
        }
        return false;
    };

    // Indexed elements are appended to the document builder while non-indexed elements are appended
    // to a buffer.
    for (const auto& element : document) {
        if (isFieldIndexed(element.fieldName()))
            documentBuilder << element;
        else
            nonIndexedElements.appendBuf(element.rawdata(), element.size());
    }

    // Encode non-indexed elements into splits.
    const auto splits = encodeData(reinterpret_cast<const std::byte*>(nonIndexedElements.buf()),
                                   nonIndexedElements.len());
    BSONArrayBuilder splitsBuilder;
    for (const auto& split : splits)
        splitsBuilder.appendBinData(split.size(), BinDataGeneral, split.data());

    // Append erasure-coded length and splits field.
    documentBuilder.appendNumber(lengthFieldName, nonIndexedElements.len());
    documentBuilder.appendArray(splitsFieldName, splitsBuilder.obj());

    return documentBuilder.obj();
}


BSONObj ErasureCoder::decodeDocument(
    const std::pair<BSONObj, int>& primaryDocumentWithIdx,
    const std::vector<std::pair<BSONObj, int>>& secondarySplitsWithIdxs) const {
    // Initialize views of buffers for existing splits
    std::vector<std::pair<const std::byte*, int>> splitsWithIdxs;
    const auto& [primaryDocument, primaryIdx] = primaryDocumentWithIdx;
    const auto primarySplit = primaryDocument.getObjectField(splitsFieldName).getField("0");
    int splitSize;
    splitsWithIdxs.emplace_back(reinterpret_cast<const std::byte*>(primarySplit.binData(splitSize)),
                                primaryIdx);
    for (const auto& [splitsField, idx] : secondarySplitsWithIdxs) {
        const auto split = splitsField.getField("0");
        splitsWithIdxs.emplace_back(reinterpret_cast<const std::byte*>(split.binData(splitSize)),
                                    idx);
    }

    // Decode non-indexed (erasure-coded) fields.
    const auto nonIndexedData = decodeData(splitsWithIdxs, splitSize);

    // Build the decoded document.
    BSONObjBuilder documentBuilder;
    for (const auto& element : primaryDocument) {
        if (element.fieldName() != splitsFieldName && element.fieldName() != lengthFieldName)
            documentBuilder << element;
    }
    const auto length = primaryDocument.getIntField(lengthFieldName);
    documentBuilder.bb().appendBuf(nonIndexedData.data(), length);

    return documentBuilder.obj();
}
