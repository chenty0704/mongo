#pragma once

#include <stdexcept>
#include <vector>

#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/db/catalog/index_catalog.h"

using namespace mongo;

static constexpr auto lengthFieldName = "_len";
static constexpr auto splitsFieldName = "_splits";

template<typename T>
struct Matrix {
public:
    Matrix(int numRows, int numCols) : _data(numRows * numCols), _numRows(numRows), _numCols(numCols) {}

    [[nodiscard]] T *data() noexcept {
        return _data.data();
    }
    [[nodiscard]] const T *data() const noexcept {
        return _data.data();
    }

    [[nodiscard]] int numRows() const {
        return _numRows;
    }
    [[nodiscard]] int numCols() const {
        return _numCols;
    }

    [[nodiscard]] int size() const noexcept {
        return _numRows * _numCols;
    }

    T &operator()(int i, int j) noexcept {
        return _data[i * _numCols + j];
    }
    const T &operator()(int i, int j) const noexcept {
        return _data[i * _numCols + j];
    }

private:
    std::vector<T> _data;
    int _numRows, _numCols;
};

class ErasureCoder {
public:
    ErasureCoder(int numSourceSplits, int numTotalSplits);

    [[nodiscard]] int numSourceSplits() const {
        return _k;
    }

    [[nodiscard]] int numTotalSplits() const {
        return _m;
    }

    [[nodiscard]] int numParitySplits() const {
        return _m - _k;
    }

    [[nodiscard]] std::vector<std::vector<std::byte>> encodeData(const std::byte *data, int size) const;

    [[nodiscard]] std::vector<std::byte>
    decodeData(const std::vector<std::pair<const std::byte *, int>> &splitsWithIdxs, int splitSize) const;

    [[nodiscard]] BSONObj encodeDocument(OperationContext &opCtx,
                                         const IndexCatalog &indexCatalog,
                                         const BSONObj &document) const;

    [[nodiscard]] BSONObj decodeDocument(const std::vector<std::pair<BSONObj, int>>& splits,
                                         int size) const;

private:
    int _k, _m;
    Matrix<uint8_t> _encodeMatrix;
    std::vector<uint8_t> _encodeTable;
};

class ErasureCodingException : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
