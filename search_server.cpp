#include "search_server.h"

using namespace std;

SearchServer::SearchServer(const string& stop_words_text): SearchServer::SearchServer(SplitIntoWordsView(stop_words_text)){}
SearchServer::SearchServer(const string_view stop_words_text): SearchServer::SearchServer(SplitIntoWordsView(stop_words_text)){}

void SearchServer::AddDocument(int document_id, const string_view document, DocumentStatus status, const vector<int>& ratings){
    if ((document_id < 0) || (documents_.count(document_id) > 0)) {
        throw invalid_argument("Invalid document_id"s);
    }

    const auto [it, inserted] = documents_.emplace(document_id, DocumentData{ComputeAverageRating(ratings), status, string(document)});

    const auto words = SplitIntoWordsNoStop(it->second.content);
    document_ids_.push_back(document_id);

    const double inv_word_count = 1.0 / words.size();
    for (const string_view word : words) {
        word_to_document_freqs_[word][document_id] += inv_word_count;
        document_to_word_freqs_[document_id][word] += inv_word_count;
    }
}

int SearchServer::GetDocumentCount() const {
    return documents_.size();
}

vector<int>::const_iterator SearchServer::begin() const {
    return document_ids_.begin();
}

vector<int>::const_iterator SearchServer::end() const {
    return document_ids_.end();
}

tuple<vector<string_view>, DocumentStatus> SearchServer::MatchDocument(
        string_view raw_query, int document_id) const {

    const Query query = ParseQuery(raw_query, true);

    for (string_view word : query.minus_words) {
        if (word_to_document_freqs_.count(word) == 0) {
            continue;
        }
        if (word_to_document_freqs_.at(word).count(document_id)) {
            return { vector<string_view>{}, documents_.at(document_id).status };
        }
    }


    vector<string_view> matched_words;
    for (const string_view& word : query.plus_words) {
        if (word_to_document_freqs_.count(word) == 0) {
            continue;
        }
        if (word_to_document_freqs_.at(word).count(document_id)) {
            matched_words.push_back(word);
        }
    }

    return {matched_words, documents_.at(document_id).status};
}

tuple<vector<string_view>, DocumentStatus> SearchServer::MatchDocument(
        execution::sequenced_policy ex_policy, string_view raw_query, int document_id) const{
    return MatchDocument(raw_query, document_id);
}


tuple<vector<string_view>, DocumentStatus> SearchServer::MatchDocument(
        execution::parallel_policy ex_policy, string_view raw_query, int document_id) const{

    const Query query = ParseQuery(raw_query, false);

    if(any_of(query.minus_words.begin(), query.minus_words.end(), [this, document_id](const string_view& word){
        return word_to_document_freqs_.at(word).count(document_id);
    })) {
        return {vector<string_view>(), documents_.at(document_id).status};
    }

    vector<string_view> matched_words(query.plus_words.size());
    copy_if(ex_policy, query.plus_words.begin(), query.plus_words.end(), matched_words.begin(),
                      [this, document_id](const string_view& word){
        return word_to_document_freqs_.at(word).count(document_id);
    });
    sort(ex_policy, matched_words.begin(), matched_words.end());
    auto it = unique(matched_words.begin(), matched_words.end());


    return {{matched_words.begin(), it-1}, documents_.at(document_id).status};
}

bool SearchServer::IsStopWord(string_view word) const {
    return stop_words_.count(word) > 0;
}

bool SearchServer::IsValidWord(string_view word) {
    return none_of(word.begin(), word.end(), [](char c) {
        return c >= '\0' && c < ' ';
    });
}

vector<string_view> SearchServer::SplitIntoWordsNoStop(const string_view text) const {
    vector<string_view> words;
    for (const string_view word : SplitIntoWordsView(text)) {
        if (!IsValidWord(word)) {
            throw invalid_argument("Word is invalid"s);
        }
        if (!IsStopWord(word) && word != ""s) {
            words.push_back(word);
        }
    }
    return words;
}

int SearchServer::ComputeAverageRating(const vector<int>& ratings) {
    if (ratings.empty()) {
        return 0;
    }
    int rating_sum = accumulate(ratings.begin(), ratings.end(), 0);
    return rating_sum / static_cast<int>(ratings.size());
}

SearchServer::QueryWord SearchServer::ParseQueryWord(string_view text) const {
    bool is_minus = false;
    if (text[0] == '-') {
        is_minus = true;
        text = text.substr(1);
    }
    if (text.empty() || text[0] == '-' || !IsValidWord(text)) {
        throw invalid_argument("Query word "s + text.data() + " is invalid");
    }

    return {text, is_minus, IsStopWord(text)};
}

SearchServer::Query SearchServer::ParseQuery(string_view text, const bool s) const {
    Query result;
    for (string_view word : SplitIntoWordsView(text)) {
        const auto query_word = ParseQueryWord(word);
        if (!query_word.is_stop) {
            if (query_word.is_minus) {
                result.minus_words.push_back(query_word.data);
            } else {
                result.plus_words.push_back(query_word.data);
            }
        }
    }

    if(s) {
        sort(result.plus_words.begin(), result.plus_words.end());
        sort(result.minus_words.begin(), result.minus_words.end());
        auto last = unique(result.plus_words.begin(), result.plus_words.end());
        result.plus_words.resize(distance(result.plus_words.begin(), last));
        last = unique(result.minus_words.begin(), result.minus_words.end());
        result.minus_words.resize(distance(result.minus_words.begin(), last));
    }
    return result;
}

double SearchServer::ComputeWordInverseDocumentFreq(const string_view& word) const {
    return log(GetDocumentCount() * 1.0 / word_to_document_freqs_.at(word).size());
}

const map<string_view , double>& SearchServer::GetWordFrequencies(int document_id){
    static const map<string_view, double> empty_map;
    
    if (document_to_word_freqs_.count(document_id) == 0) {
        return empty_map;
    }
    
    return document_to_word_freqs_.at(document_id);
}

void SearchServer::RemoveDocument(int document_id){
    documents_.erase(document_id);
    for (const auto& pair : document_to_word_freqs_.at(document_id)){
        word_to_document_freqs_[pair.first].erase(document_id);
    }
    document_to_word_freqs_.erase(document_id);
    document_ids_.erase(find(document_ids_.begin(), document_ids_.end(), document_id));
}
void SearchServer::RemoveDocument(execution::sequenced_policy ex_policy, int document_id) {
    RemoveDocument(document_id);
}
void SearchServer::RemoveDocument(execution::parallel_policy ex_policy, int document_id) {
    documents_.erase(document_id);

    const auto& word_freqs = document_to_word_freqs_.at(document_id);
    vector<const string_view*> words(word_freqs.size());

    transform(
            word_freqs.begin(), word_freqs.end(),
            words.begin(),
            [](const auto& item){
                return &item.first;
            });

    for_each(
            ex_policy,
            words.begin(), words.end(),
            [this, document_id](const string_view* ptr){
                word_to_document_freqs_.at(*ptr).erase(document_id);
            });

    document_to_word_freqs_.erase(document_id);
    document_ids_.erase(find(document_ids_.begin(), document_ids_.end(), document_id));
}
