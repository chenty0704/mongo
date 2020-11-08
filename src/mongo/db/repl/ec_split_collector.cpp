#define MONGO_LOGV2_DEFAULT_COMPONENT ::mongo::logv2::LogComponent::kReplication

#include "mongo/db/repl/ec_split_collector.h"

#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/bson/mutable/document.h"
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
            LOGV2(30014, "SplitCollector::_connect, reconnect", "target"_attr = target.toString());
            conn->checkConnection();
        } else {
            LOGV2(30013, "SplitCollector::_connect, connect", "target"_attr = target.toString());
            connectStatus = conn->connect(target, "SplitCollector");
        }
    } while (!connectStatus.isOK());

    LOGV2(30012, "SplitCollector::_connect, success", "target"_attr = target.toString());

    return connectStatus;
}

BSONObj SplitCollector::_makeFindQuery() const {
    BSONObjBuilder queryBob;
    queryBob.append("query", BSON(_oidElem));
    return queryBob.obj();
}

void SplitCollector::collect() noexcept {
    const auto& members = _replCoord->getConfig().members();
    _splits.reserve(members.size());
    LOGV2(30011, "SplitCollector::collect, members", "member.size"_attr = members.size());
    for (auto memId = 0; memId < members.size(); ++memId) {
        if (memId == _replCoord->getSelfIndex())
            continue;
        auto target = members[memId].getHostAndPort();

        auto conn = std::make_unique<DBClientConnection>(true);
        auto connStatus = _connect(conn, target);

        _projection = BSON(
            "o" << BSON(splitsFieldName << BSON("$arrayElemAt" << BSON_ARRAY(
                                                    std::string("$") + splitsFieldName << memId))));

        LOGV2(30007,
              "SplitCollector::collect",
              "memId"_attr = memId,
              "self"_attr = _replCoord->getSelfIndex(),
              "_projection"_attr = _projection.toString());

        conn->query(
            [=](DBClientCursorBatchIterator& i) {
                BSONObj qresult;
                while (i.moreInCurrentBatch()) {
                    qresult = i.nextSafe();
                    invariant(!i.moreInCurrentBatch());
                }
                LOGV2(30015,
                      "SplitCollector::collect",
                      "memId"_attr = memId,
                      "qresult"_attr = qresult.toString());

                if (qresult.hasField("o") && qresult.getObjectField("o").hasField(splitsFieldName)) {
                    this->_splits.emplace_back(std::make_pair(
                        qresult.getObjectField("o").getStringField(splitsFieldName), memId));
                } else {
                    // invairant
                    LOGV2(30016,
                          "SplitCollector::collect, split field not found",
                          "memId"_attr = memId,
                          "qresult"_attr = qresult.toString());
                }
            },
            _nss,
            _makeFindQuery(),
            &_projection /* fieldsToReturn */,
            QueryOption_CursorTailable | QueryOption_SlaveOk | QueryOption_Exhaust);
    }

    _toBSON();
}

void SplitCollector::_toBSON() const {
    // _splits[replCoord->getSelfIndex()] = _out->getStringField(splitsFieldName);

    mutablebson::Document document(*_out);
    // auto splitsField = mutablebson::findFirstChildNamed(document.root(), splitsFieldName);
    // invariant(splitsField.countChildren() == 1);
    // splitsField.popBack();

    // for (const auto& split : _splits) {
    //     splitsField.pushBack(split);
    // }
    *_out = document.getObject();
}


}  // namespace repl
}  // namespace mongo
