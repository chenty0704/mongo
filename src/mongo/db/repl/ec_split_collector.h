#pragma once

#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/base/status_with.h"
#include "mongo/bson/timestamp.h"
#include "mongo/client/dbclient_connection.h"
#include "mongo/client/dbclient_cursor.h"
#include "mongo/db/namespace_string.h"
#include "mongo/db/repl/erasure_coder.h"
#include "mongo/platform/mutex.h"

namespace mongo {
namespace repl {

const Milliseconds kSplitCollectorSocketTimeout(30 * 1000);  // 30s
const int splitCollectorBatchSize = (16 * 1024 * 1024) / 12 * 10;

class ReplicationCoordinator;

using ConnPtr = std::unique_ptr<DBClientConnection>;
using CursorPtr = std::unique_ptr<DBClientCursor>;

class SplitCollector {
    SplitCollector(const SplitCollector&) = delete;
    SplitCollector& operator=(const SplitCollector&) = delete;

public:
    SplitCollector(const ReplicationCoordinator* replCoord,
                    const NamespaceString& nss,
                    BSONObj* out);

    virtual ~SplitCollector();
    void collect() noexcept;

private:
    Status _connect(ConnPtr& conn, const HostAndPort& target);
    BSONObj _makeFindQuery() const;
    void _toBSON();

    BSONElement _oidElem;
    BSONObj* _out;
    BSONObj _projection;
    NamespaceString _nss;
    Mutex _mutex = MONGO_MAKE_LATCH("SplitCollector::_mutex");
    std::vector<std::pair<BSONObj, int>> _splits;
    const ReplicationCoordinator* _replCoord;
};

}  // namespace repl
}  // namespace mongo
