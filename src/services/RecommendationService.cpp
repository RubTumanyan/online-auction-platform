#include "services/RecommendationService.h"

#include "repositories/CatalogRepository.h"
#include "repositories/RecommendationRepository.h"
#include "services/AuthBidService.h"

#include <trantor/utils/Logger.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <memory>
#include <mutex>
#include <set>
#include <unordered_set>

namespace auction::services
{
namespace
{
constexpr double kViewWeight = 1.0;
constexpr double kBidWeight = 5.0;
constexpr int kMaxLimit = 20;

const std::set<std::string> kStopWords = {
    "a", "an", "the", "and", "or", "but", "if", "is", "are", "was", "were",
    "be", "been", "being", "have", "has", "had", "do", "does", "did",
    "will", "would", "could", "should", "may", "might", "shall", "can",
    "of", "in", "to", "for", "with", "on", "at", "from", "by", "about",
    "as", "into", "this", "that", "these", "those", "it", "its",
    "not", "no", "nor", "so", "too", "very", "just", "than", "then",
    "also", "more", "most", "some", "any", "each", "all", "both",
    "such", "only", "own", "same", "other", "new", "one", "two"};

std::unordered_map<std::string, int> buildDocumentFrequency(
    const std::vector<std::vector<std::string>>& tokenizedDocuments)
{
    std::unordered_map<std::string, int> df;
    for (const auto& doc : tokenizedDocuments)
    {
        std::unordered_set<std::string> seen(doc.begin(), doc.end());
        for (const auto& term : seen) ++df[term];
    }
    return df;
}

// Immutable TF-IDF model over all active lots, reused across requests. The
// corpus only has to be rebuilt when the active-lot set actually changes
// (seed, new lot, or closing/bidding), not on every recommendation request.
struct CorpusData
{
    int totalDocuments = 0;
    std::unordered_map<std::string, int> documentFrequency;
    std::vector<std::pair<std::int64_t, RecommendationService::TfIdfVector>> lotVectors;
};

struct CorpusCacheEntry
{
    std::string version;
    std::shared_ptr<const CorpusData> corpus;
};

std::mutex& corpusCacheMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<std::string, CorpusCacheEntry>& corpusCache()
{
    static std::unordered_map<std::string, CorpusCacheEntry> cache;
    return cache;
}

std::shared_ptr<const CorpusData> corpusFor(
    const std::filesystem::path& databasePath,
    const repositories::RecommendationRepository& repo)
{
    const auto cacheKey = databasePath.string();
    const auto version = repo.activeLotsVersion();
    {
        std::lock_guard<std::mutex> lock(corpusCacheMutex());
        const auto& cache = corpusCache();
        const auto it = cache.find(cacheKey);
        if (it != cache.end() && it->second.version == version && it->second.corpus)
            return it->second.corpus;
    }

    // Build the TF-IDF model for all active lots (only when the catalog changed).
    const auto lots = repo.allActiveLots();
    std::vector<std::pair<std::int64_t, std::string>> featureTexts;
    featureTexts.reserve(lots.size());
    for (const auto& lot : lots)
        featureTexts.push_back({lot.id,
                                RecommendationService::buildFeatureText(
                                    lot.categoryName, lot.title, lot.description)});

    std::vector<std::vector<std::string>> allTokens;
    allTokens.reserve(featureTexts.size());
    for (const auto& [id, text] : featureTexts)
        allTokens.push_back(RecommendationService::tokenize(text));

    auto corpus = std::make_shared<CorpusData>();
    corpus->totalDocuments = static_cast<int>(featureTexts.size());
    corpus->documentFrequency = buildDocumentFrequency(allTokens);
    corpus->lotVectors.reserve(featureTexts.size());
    for (std::size_t i = 0; i < featureTexts.size(); ++i)
        corpus->lotVectors.emplace_back(
            featureTexts[i].first,
            RecommendationService::computeTfIdfVector(
                allTokens[i], corpus->documentFrequency, corpus->totalDocuments));

    {
        std::lock_guard<std::mutex> lock(corpusCacheMutex());
        corpusCache()[cacheKey] = {version, corpus};
    }
    return corpus;
}
}

std::vector<std::string> RecommendationService::tokenize(const std::string& text)
{
    std::vector<std::string> tokens;
    std::string current;
    current.reserve(text.size());
    for (const auto ch : text)
    {
        if (std::isalpha(static_cast<unsigned char>(ch)))
        {
            current.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
        else if (!current.empty())
        {
            if (kStopWords.find(current) == kStopWords.end())
                tokens.push_back(std::move(current));
            current.clear();
        }
    }
    if (!current.empty())
    {
        if (kStopWords.find(current) == kStopWords.end())
            tokens.push_back(std::move(current));
    }
    return tokens;
}

std::string RecommendationService::buildFeatureText(
    const std::string& category, const std::string& title, const std::string& description)
{
    return category + " " + title + " " + description;
}

double RecommendationService::termFrequency(const std::string& term, const std::vector<std::string>& tokens)
{
    if (tokens.empty()) return 0.0;
    const auto count = static_cast<double>(std::count(tokens.begin(), tokens.end(), term));
    return count / static_cast<double>(tokens.size());
}

double RecommendationService::inverseDocumentFrequency(
    const std::string& term,
    const std::unordered_map<std::string, int>& documentFrequency,
    int totalDocuments)
{
    const auto it = documentFrequency.find(term);
    const auto df = it != documentFrequency.end() ? it->second : 0;
    return std::log(static_cast<double>(totalDocuments + 1) / static_cast<double>(df + 1)) + 1.0;
}

RecommendationService::TfIdfVector RecommendationService::computeTfIdfVector(
    const std::vector<std::string>& tokens,
    const std::unordered_map<std::string, int>& documentFrequency,
    int totalDocuments)
{
    TfIdfVector vector;
    for (const auto& token : tokens)
        vector[token] = termFrequency(token, tokens) *
                        inverseDocumentFrequency(token, documentFrequency, totalDocuments);
    return vector;
}

double RecommendationService::cosineSimilarity(const TfIdfVector& a, const TfIdfVector& b)
{
    if (a.empty() || b.empty()) return 0.0;

    double dot = 0.0;
    double normA = 0.0;
    double normB = 0.0;

    for (const auto& [term, value] : a)
    {
        normA += value * value;
        const auto it = b.find(term);
        if (it != b.end()) dot += value * it->second;
    }
    for (const auto& [term, value] : b) normB += value * value;

    const auto denominator = std::sqrt(normA) * std::sqrt(normB);
    return denominator > 0.0 ? dot / denominator : 0.0;
}

RecommendationService::TfIdfVector RecommendationService::buildUserVector(
    const std::vector<std::pair<TfIdfVector, double>>& weightedVectors)
{
    TfIdfVector userVector;
    for (const auto& [lotVector, weight] : weightedVectors)
        for (const auto& [term, value] : lotVector)
            userVector[term] += value * weight;
    return userVector;
}

std::vector<std::pair<std::int64_t, double>> RecommendationService::rankBySimilarity(
    const TfIdfVector& userVector,
    const std::vector<std::pair<std::int64_t, TfIdfVector>>& candidateVectors)
{
    std::vector<std::pair<std::int64_t, double>> scored;
    scored.reserve(candidateVectors.size());
    for (const auto& [lotId, lotVector] : candidateVectors)
        scored.emplace_back(lotId, cosineSimilarity(userVector, lotVector));

    std::sort(scored.begin(), scored.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    return scored;
}

RecommendationService::RecommendationService(std::filesystem::path databasePath)
    : databasePath_(std::move(databasePath))
{
}

RecommendationResult RecommendationService::recommendations(std::int64_t userId, int limit) const
{
    limit = std::clamp(limit, 1, kMaxLimit);
    LOG_DEBUG << "[ML] Recommendation request userId=" << userId << " limit=" << limit;

    const repositories::RecommendationRepository repo(databasePath_);

    // Load the user's interaction history BEFORE building any TF-IDF model.
    // Visitors without any history (including every anonymous visitor) go
    // straight to the cheap cold-start path instead of paying for a full
    // catalog-wide model build.
    const auto interactions = repo.userInteractions(userId);
    if (interactions.empty()) return coldStartFallback(limit);

    // Personalized path: reuse the cached TF-IDF corpus, rebuilding it only
    // when the set of active lots changes.
    const auto corpus = corpusFor(databasePath_, repo);
    if (corpus->lotVectors.empty()) return coldStartFallback(limit);

    // Build user preference vector from interaction history.
    std::vector<std::pair<TfIdfVector, double>> weightedVectors;
    std::unordered_set<std::int64_t> bidLotIds;
    for (const auto& interaction : interactions)
    {
        const auto weight = interaction.type == "bid" ? kBidWeight : kViewWeight;
        if (interaction.type == "bid") bidLotIds.insert(interaction.lotId);
        for (const auto& [lotId, lotVec] : corpus->lotVectors)
        {
            if (lotId == interaction.lotId)
            {
                weightedVectors.emplace_back(lotVec, weight);
                break;
            }
        }
    }

    const auto userVector = buildUserVector(weightedVectors);
    if (userVector.empty()) return coldStartFallback(limit);

    // Score all candidates by cosine similarity.
    const auto scored = rankBySimilarity(userVector, corpus->lotVectors);

    // Collect top-N as full Lot models for JSON output.
    // Exclude lots the user already bid on.
    RecommendationResult result;
    result.personalized = true;
    const repositories::CatalogRepository catalogRepo(databasePath_);
    for (const auto& [lotId, score] : scored)
    {
        if (static_cast<int>(result.lots.size()) >= limit) break;
        if (score <= 0.0) break;
        if (bidLotIds.find(lotId) != bidLotIds.end()) continue;
        auto lot = catalogRepo.lotById(lotId);
        if (lot && lot->status == "active")
            result.lots.push_back(std::move(*lot));
    }

    if (result.lots.empty()) return coldStartFallback(limit);
    LOG_INFO << "[ML] Personalized recommendations for userId=" << userId
             << " (interactions=" << interactions.size()
             << ", candidates=" << corpus->lotVectors.size()
             << ", served=" << result.lots.size() << ")";
    return result;
}

void RecommendationService::recordView(std::int64_t userId, std::int64_t lotId) const
{
    const repositories::RecommendationRepository repo(databasePath_);
    if (!repo.lotExists(lotId))
        throw ApiError(ApiErrorKind::notFound, "Lot not found");
    repo.recordInteraction(userId, lotId, "view", kViewWeight);
}

RecommendationResult RecommendationService::coldStartFallback(int limit) const
{
    RecommendationResult result;
    result.personalized = false;
    const repositories::RecommendationRepository repo(databasePath_);

    auto endingSoon = repo.lotsEndingSoon(limit);
    auto diverse = repo.diverseActiveLots(limit);

    std::unordered_set<std::int64_t> seen;
    for (auto& lot : endingSoon)
    {
        if (static_cast<int>(result.lots.size()) >= limit) break;
        if (seen.insert(lot.id).second) result.lots.push_back(std::move(lot));
    }
    for (auto& lot : diverse)
    {
        if (static_cast<int>(result.lots.size()) >= limit) break;
        if (seen.insert(lot.id).second) result.lots.push_back(std::move(lot));
    }

    LOG_INFO << "[ML] Cold-start fallback for user without history: served "
             << result.lots.size() << " lot(s)";
    return result;
}
}
