#include "request_queue.h"

using namespace std;

RequestQueue::RequestQueue(const SearchServer& search_server): server_(search_server) {}

void RequestQueue::CountEmptyRequests(const vector<Document>& result){
    if (!requests_.empty() && time_ > min_in_day_) {
        if(requests_.front().result.empty())
            --empty_res_cnt_;

        requests_.pop_front();
        requests_.push_back({result});

        if (result.empty())
            ++empty_res_cnt_;

    } else{
        requests_.push_back({result});
        if (result.empty())
            ++empty_res_cnt_;
    }
}

int RequestQueue::GetNoResultRequests() const {
    return empty_res_cnt_;
}

vector<Document> RequestQueue::AddFindRequest(string_view raw_query, DocumentStatus status) {
    return AddFindRequest(raw_query, [status](int document_id, DocumentStatus document_status, int rating) {
        return document_status == status;
    });
}

vector<Document> RequestQueue::AddFindRequest(string_view raw_query) {
    return AddFindRequest(raw_query, DocumentStatus::ACTUAL);
}
