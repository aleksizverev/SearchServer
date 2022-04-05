#include "remove_duplicates.h"

using namespace std;

void RemoveDuplicates(SearchServer& search_server){
    std::map<set<string_view>, map<int, int>> aux_map ;
    vector<int> documents_to_delete;
    for (const int document_id : search_server){
        set<string_view> document_words;
        for (const auto& word_freq : search_server.GetWordFrequencies(document_id)){
            document_words.insert(word_freq.first);
        }
        ++aux_map[document_words][document_id];
        
        if (aux_map[document_words].size() > 1){
            documents_to_delete.push_back(document_id);
        }
    }
    
    for (int document_id : documents_to_delete){
        cout << "Found duplicate document id "s << document_id << endl;
        search_server.RemoveDocument(document_id);
    }
}

