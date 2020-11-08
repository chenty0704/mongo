#define MONGO_LOGV2_DEFAULT_COMPONENT ::mongo::logv2::LogComponent::kReplication

#include "mongo/db/repl/ec_split_collector.h"

#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/bson/mutable/algorithm.h"
#include "mongo/bson/mutable/document.h"
#include "mongo/bson/util/bson_check.h"
#include "mongo/db/jsobj.h"
#include "mongo/db/repl/replication_coordinator.h"
#include "mongo/logv2/log.h"

namespace mongo {
namespace repl {

SplitCollector::SplitCollector(const ReplicationCoordinator* replCoord,
                               const NamespaceString& nss,
                               BSONObj* out)
    : _replCoord(replCoord), _nss(nss), _out(out) {
    out->getObjectID(_oidElem);
    LOGV2(30008,
          "SplitCollector::SplitCollector",
          "ns"_attr = _nss.toString(),
          "self"_attr = _replCoord->getSelfIndex(),
          "_oid"_attr = _oidElem.toString());
}

SplitCollector::~SplitCollector() {}

Status SplitCollector::_connect(ConnPtr& conn, const HostAndPort& target) {
    Status connectStatus = Status::OK();
    do {
        if (!connectStatus.isOK()) {
            LOGV2(30014, "reconnect", "target"_attr = target.toString());
            conn->checkConnection();
        } else {
            LOGV2(30013, "connect", "target"_attr = target.toString());
            connectStatus = conn->connect(target, "SplitCollector");
        }
    } while (!connectStatus.isOK());

    LOGV2(30012, "success", "target"_attr = target.toString());

    return connectStatus;
}

BSONObj SplitCollector::_makeFindQuery() const {
    BSONObjBuilder queryBob;
    queryBob.append("query", BSON(_oidElem));
    return queryBob.obj();
}

void SplitCollector::collect() noexcept {
    const auto& members = _replCoord->getMemberData();
    _splits.reserve(members.size());
    LOGV2(30011, "members", "member.size"_attr = members.size());
    for (auto memId = 0; memId < members.size(); ++memId) {
        if (memId == _replCoord->getSelfIndex())
            continue;
        const auto target = members[memId].getHostAndPort();

        auto conn = std::make_unique<DBClientConnection>(true);
        auto connStatus = _connect(conn, target);

        _projection =
            BSON(splitsFieldName << BSON(
                     "$arrayElemAt" << BSON_ARRAY(std::string("$") + splitsFieldName << memId)));

        LOGV2(30007,
              "memid and proj",
              "memId"_attr = memId,
              "self"_attr = _replCoord->getSelfIndex(),
              "_projection"_attr = _projection.toString());

        conn->query(
            [=](DBClientCursorBatchIterator& i) {
                BSONObj qresult;
                while (i.moreInCurrentBatch()) {
                    qresult = i.nextSafe();
                }

                if (qresult.hasField(splitsFieldName)) {
                    LOGV2(30015,
                          "get qresult",
                          "memId"_attr = memId,
                          "_splits"_attr = qresult.getField(splitsFieldName).toString());

                    const std::vector<BSONElement> arr = qresult.getField(splitsFieldName).Array();
                    BSONElement elem = arr[0];
                    // invariant(arr.size() == 1);
                    LOGV2(30019,
                          "collect, array",
                          "memId"_attr = memId,
                          "[0].type"_attr = typeName(elem.type()),
                          "[0].data"_attr = elem.toString());
                    checkBSONType(BSONType::BinData, elem);
                    {
                        stdx::lock_guard<Latch> lk(_mutex);
                        this->_splits.push_back(std::make_pair(BSON(elem).getOwned(), memId));
                    }
                } else {
                    // error
                    LOGV2(30016,
                          "split field not found",
                          "memId"_attr = memId,
                          "qresult"_attr = qresult.toString());
                }
            },
            _nss,
            _makeFindQuery(),
            nullptr /* fieldsToReturn */,
            QueryOption_CursorTailable | QueryOption_SlaveOk | QueryOption_Exhaust);
    }

    _toBSON();
}

void SplitCollector::_toBSON() {
    stdx::lock_guard<Latch> lk(_mutex);
    // get local split
    const std::vector<BSONElement>& arr = _out->getField(splitsFieldName).Array();
    checkBSONType(BSONType::BinData, arr[0]);
    _splits.push_back(std::make_pair(BSON(arr[0]).getOwned(), _replCoord->getSelfIndex()));

    for (const auto& split : _splits) {
        LOGV2(30018,
              "SplitCollector::_toBSON()",
              "split"_attr = split.first.toString(),
              "id"_attr = split.second);
    }

    // // find splitsFieldName
    // mutablebson::Document document(*_out);
    // auto splitsField = mutablebson::findFirstChildNamed(document.root(), splitsFieldName);
    // // delete splitsField
    // splitsField.remove();
    // *_out = document.getObject();

    // // decode _splits and get a bsonobj
    // BSONObj decodedBSON = decodeDocument(_splits, _out->getIntField(lengthFieldName));

    // // iterate bsonobj and append to _out
    // BSONObjBuilder bob(std::move(*_out));
    // bob.appendElements(decodedBSON);
    // *_out = bob.obj();
}


}  // namespace repl
}  // namespace mongo
