#include "process_queries.h"

#include <execution>

std::vector<std::vector<Document>> ProcessQueries(
        const SearchServer& search_server,
        const std::vector<std::string>& queries){

    std::vector<std::vector<Document>> documents_lists(queries.size());
    std::transform(std::execution::par, queries.begin(), queries.end(), documents_lists.begin(),
                   [&search_server](std::string query){
        return search_server.FindTopDocuments(query);
    });

    return documents_lists;
}

std::vector<Document> ProcessQueriesJoined(
        const SearchServer& search_server,
        const std::vector<std::string>& queries){

    std::vector<std::vector<Document>> documents_lists(queries.size());
    documents_lists = ProcessQueries(search_server, queries);
    std::vector<Document> documents;

    for(std::vector<Document>& docs : documents_lists){
        for (Document& doc : docs)
            documents.push_back(doc);
    }
    return documents;
}