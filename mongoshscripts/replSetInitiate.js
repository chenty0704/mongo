db.adminCommand({
    replSetInitiate: {
        _id: "rs0",
        members: [
            {_id: 0, host: "localhost:27017", priority: 1},
            {_id: 1, host: "localhost:27018", priority: 0},
            {_id: 2, host: "localhost:27019", priority: 0},
            {_id: 3, host: "localhost:27020", priority: 0}
        ],
        settings: {
            chainingAllowed: false,
            numSourceSplits: 2,
            numTotalSplits: 4
        }
    }
})
