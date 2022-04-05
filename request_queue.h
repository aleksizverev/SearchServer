#pragma once

#include <deque>

#include "search_server.h"

class RequestQueue {
public:
    explicit RequestQueue(const SearchServer& search_server);

    void CountEmptyRequests(const std::vector<Document>& result);

    template <typename DocumentPredicate>
    std::vector<Document> AddFindRequest(std::string_view raw_query, DocumentPredicate document_predicate) {
        const std::vector<Document> result = server_.FindTopDocuments(raw_query, document_predicate);
        ++time_;
        CountEmptyRequests(result);
        return result;
    }

    std::vector<Document> AddFindRequest(std::string_view raw_query, DocumentStatus status);

    std::vector<Document> AddFindRequest(std::string_view raw_query);

    int GetNoResultRequests() const;
    
private:
    struct QueryResult {
        std::vector<Document> result;
    };
    std::deque<QueryResult> requests_;
    const static int min_in_day_ = 1440;
    const SearchServer& server_;
    int empty_res_cnt_ = 0;
    int time_ = 0;
};
