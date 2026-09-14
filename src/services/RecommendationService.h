#pragma once

#include "models/Catalog.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace auction::services
{

struct RecommendationResult
{
    std::vector<models::Lot> lots;
    bool personalized;
};

// Content-based recommendation engine using TF-IDF and cosine similarity.
//
// Pipeline:
//   Lots -> text features (category + title + description)
//         -> tokenized terms
//         -> TF-IDF vectors
//
//   User interactions (view/bid) -> weighted lot vectors -> user preference vector
//
//   User vector + candidate lot vectors -> cosine similarity -> sorted Top-N
class RecommendationService final
{
  public:
    explicit RecommendationService(std::filesystem::path databasePath);

    RecommendationResult recommendations(std::int64_t userId, int limit = 8) const;
    void recordView(std::int64_t userId, std::int64_t lotId) const;

    // Public for testing — pure functions with no database access.
    using TfIdfVector = std::unordered_map<std::string, double>;

    static std::vector<std::string> tokenize(const std::string& text);
    static std::string buildFeatureText(const std::string& category,
                                        const std::string& title,
                                        const std::string& description);
    static double termFrequency(const std::string& term, const std::vector<std::string>& tokens);
    static double inverseDocumentFrequency(const std::string& term,
                                           const std::unordered_map<std::string, int>& documentFrequency,
                                           int totalDocuments);
    static TfIdfVector computeTfIdfVector(const std::vector<std::string>& tokens,
                                          const std::unordered_map<std::string, int>& documentFrequency,
                                          int totalDocuments);
    static double cosineSimilarity(const TfIdfVector& a, const TfIdfVector& b);
    static TfIdfVector buildUserVector(
        const std::vector<std::pair<TfIdfVector, double>>& weightedVectors);
    static std::vector<std::pair<std::int64_t, double>> rankBySimilarity(
        const TfIdfVector& userVector,
        const std::vector<std::pair<std::int64_t, TfIdfVector>>& candidateVectors);

  private:
    std::filesystem::path databasePath_;

    RecommendationResult coldStartFallback(int limit) const;
};

}
