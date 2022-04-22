#pragma once

#include <string>
#include <map>
#include <set>
#include <vector>
#include <algorithm>
#include <utility>
#include <cmath>
#include <iostream>
#include <numeric>
#include <execution>
#include <functional>
#include <mutex>

#include "document.h"
#include "string_processing.h"
#include "log_duration.h"

const int MAX_RESULT_DOCUMENT_COUNT = 5;
const double RELEVANCE_COMPARISON_ERR = 1e-6;

class SearchServer {
public:
    template <typename StringContainer>
    explicit SearchServer(const StringContainer& stop_words)
            : stop_words_(MakeUniqueNonEmptyStrings(stop_words))
    {
        using namespace std::string_literals;
        if (!all_of(stop_words_.begin(), stop_words_.end(), IsValidWord)) {
            throw std::invalid_argument("Some of stop words are invalid"s);
        }
    }

    explicit SearchServer(const std::string& stop_words_text);
    explicit SearchServer(const std::string_view stop_words_text);

    void AddDocument(int document_id, const std::string_view document, DocumentStatus status, const std::vector<int>& ratings);

    template <typename ExecutionPolicy, typename DocumentPredicate>
    std::vector<Document> FindTopDocuments(ExecutionPolicy& policy, std::string_view raw_query, DocumentPredicate document_predicate) const {
        const Query query = ParseQuery(raw_query, true);

        auto matched_documents = FindAllDocuments(policy, query, document_predicate);

        //LOG_DURATION("sort_area");
        sort(matched_documents.begin(), matched_documents.end(), [](const Document& lhs, const Document& rhs) {
            if (std::abs(lhs.relevance - rhs.relevance) < RELEVANCE_COMPARISON_ERR) {
                return lhs.rating > rhs.rating;
            } else {
                return lhs.relevance > rhs.relevance;
            }
        });
        if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT) {
            matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
        }

        return matched_documents;
    }

    template <typename DocumentPredicate>
    std::vector<Document> FindTopDocuments(std::string_view raw_query, DocumentPredicate document_predicate) const {
        return FindTopDocuments(std::execution::seq, raw_query, document_predicate);
    }

    template<typename ExecutionPolicy>
    std::vector<Document> FindTopDocuments(ExecutionPolicy& policy, std::string_view raw_query, DocumentStatus status) const {
        return FindTopDocuments(policy, raw_query, [status](int document_id, DocumentStatus document_status, int rating) {
            return document_status == status;
        });
    }

    template<typename ExecutionPolicy>
    std::vector<Document> FindTopDocuments(ExecutionPolicy& policy, std::string_view raw_query) const {
        return FindTopDocuments(policy, raw_query, DocumentStatus::ACTUAL);
    }

    std::vector<Document> FindTopDocuments(std::string_view raw_query, DocumentStatus status) const {
        return FindTopDocuments(std::execution::seq, raw_query, status);
    }

    std::vector<Document> FindTopDocuments(std::string_view raw_query) const {
        return FindTopDocuments(std::execution::seq, raw_query, DocumentStatus::ACTUAL);
    }

    int GetDocumentCount() const;
    
    std::vector<int>::const_iterator begin() const;
    std::vector<int>::const_iterator end() const;

    std::tuple<std::vector<std::string_view>, DocumentStatus> MatchDocument(std::string_view raw_query, int document_id) const;
    std::tuple<std::vector<std::string_view>, DocumentStatus> MatchDocument(std::execution::sequenced_policy ex_policy, std::string_view raw_query, int document_id) const;
    std::tuple<std::vector<std::string_view>, DocumentStatus> MatchDocument(std::execution::parallel_policy ex_policy, std::string_view raw_query, int document_id) const;

    const std::map<std::string_view, double>& GetWordFrequencies(int document_id);
    void RemoveDocument(int document_id);
    void RemoveDocument(std::execution::sequenced_policy ex_policy, int document_id);
    void RemoveDocument(std::execution::parallel_policy ex_policy, int document_id);


private:

    template <typename Key, typename Value>
    class ConcurrentMap {
    public:
        static_assert(std::is_integral_v<Key>, "ConcurrentMap supports only integer keys");

        struct Access {
            std::lock_guard<std::mutex> guard;
            Value& ref_to_value;
        };

        explicit ConcurrentMap(size_t bucket_count):
                bucket_cnt_(bucket_count),
                devided_maps_(bucket_count),
                vector_mutexes_(bucket_count)
        {}

        Access operator[](const Key& key){
            uint64_t submap_number = static_cast<uint64_t>(key) % bucket_cnt_;
            return Access{
                    std::lock_guard(vector_mutexes_[submap_number]),
                    devided_maps_[submap_number][key]
            };
        }


        std::map<Key, Value> BuildOrdinaryMap(){
            std::map<Key, Value> ordinary_map;
            for(size_t i = 0; i < bucket_cnt_; ++i){
                std::lock_guard guard(vector_mutexes_[i]);
                ordinary_map.insert(
                        std::make_move_iterator(devided_maps_[i].begin()),
                        std::make_move_iterator(devided_maps_[i].end())
                );
            }
            return ordinary_map;
        }

    private:
        size_t bucket_cnt_;
        std::vector<std::map<Key, Value>> devided_maps_;
        std::vector<std::mutex> vector_mutexes_;
    };

    struct DocumentData {
        int rating;
        DocumentStatus status;
        std::string content;
    };
    
    struct QueryWord {
        std::string_view data;
        bool is_minus;
        bool is_stop;
    };
    
    struct Query {
        std::vector<std::string_view> plus_words;
        std::vector<std::string_view> minus_words;
    };
    
    const std::set<std::string, std::less<>> stop_words_;
    std::map<std::string_view, std::map<int, double>> word_to_document_freqs_;
    std::map<int, std::map<std::string_view, double>> document_to_word_freqs_;
    std::map<int, DocumentData> documents_;
    std::vector<int> document_ids_;

    bool IsStopWord(std::string_view word) const;
    
    static bool IsValidWord(std::string_view word);

    std::vector<std::string_view> SplitIntoWordsNoStop(std::string_view text) const;

    static int ComputeAverageRating(const std::vector<int>& ratings);

    QueryWord ParseQueryWord(std::string_view text) const;
    Query ParseQuery(std::string_view text, const bool s) const;

    double ComputeWordInverseDocumentFreq(const std::string_view& word) const;

    template <typename ExecutionPolicy, typename DocumentPredicate>
    std::vector<Document> FindAllDocuments(ExecutionPolicy& policy, const Query& query, DocumentPredicate document_predicate) const {

        if (std::is_same_v<std::decay_t<ExecutionPolicy>, std::execution::parallel_policy>) {
            //LOG_DURATION("par_search_area");

            bool isMinusWordInDoc = any_of(policy,
                                           query.minus_words.begin(),
                                           query.minus_words.end(),
                                           [this](const std::string_view &word) {
                                               return word_to_document_freqs_.count(word);
                                           });

            ConcurrentMap<int, double> document_to_relevance(8);
            if (!isMinusWordInDoc) {
                std::for_each(std::execution::par,
                              query.plus_words.begin(), query.plus_words.end(),
                              [this, &document_to_relevance, &document_predicate](const std::string_view word) {
                                  if (!word_to_document_freqs_.count(word) == 0) {
                                      const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
                                      for (const auto[document_id, term_freq]: word_to_document_freqs_.at(word)) {
                                          const auto &document_data = documents_.at(document_id);
                                          if (document_predicate(document_id, document_data.status,
                                                                 document_data.rating)) {
                                              document_to_relevance[document_id].ref_to_value +=
                                                      term_freq * inverse_document_freq;
                                          }
                                      }
                                  }
                              });
            }

            auto result = document_to_relevance.BuildOrdinaryMap();

            std::vector<Document> matched_documents;
            std::for_each(
                    result.begin(), result.end(),
                    [this, &matched_documents](const auto &elem) {
                        matched_documents.push_back({elem.first, elem.second, documents_.at(elem.first).rating});
                    });
            return matched_documents;
        } else {

            //LOG_DURATION("seq_search_area");
            std::map<int, double> document_to_relevance;
            for (const std::string_view &word: query.plus_words) {
                if (word_to_document_freqs_.count(word) == 0) {
                    continue;
                }
                const double inverse_document_freq = ComputeWordInverseDocumentFreq(word);
                for (const auto[document_id, term_freq]: word_to_document_freqs_.at(word)) {
                    const auto &document_data = documents_.at(document_id);
                    if (document_predicate(document_id, document_data.status, document_data.rating)) {
                        document_to_relevance[document_id] += term_freq * inverse_document_freq;
                    }
                }
            }

            for (const std::string_view &word: query.minus_words) {
                if (word_to_document_freqs_.count(word) == 0) {
                    continue;
                }
                for (const auto[document_id, _]: word_to_document_freqs_.at(word)) {
                    document_to_relevance.erase(document_id);
                }
            }

            std::vector<Document> matched_documents;
            for (const auto[document_id, relevance]: document_to_relevance) {
                matched_documents.push_back({document_id, relevance, documents_.at(document_id).rating});
            }
            return matched_documents;
        }
    }
    
};

std::ostream& operator<<(std::ostream& out, const Document& document);

void AddDocument(SearchServer& search_server, int document_id,
                 const std::string& document,
                 DocumentStatus status,
                 const std::vector<int>& ratings);

void FindTopDocuments(const SearchServer& search_server, const std::string& raw_query);
void MatchDocuments(const SearchServer& search_server, const std::string& query);
void PrintDocument(const Document& document);
void PrintMatchDocumentResult(int document_id, const std::vector<std::string>& words, DocumentStatus status);
