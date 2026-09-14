#include <drogon/drogon.h>
#include <drogon/drogon_test.h>

#include "database/Database.h"
#include "repositories/RecommendationRepository.h"
#include "runtime/RuntimePaths.h"
#include "security/HttpSecurity.h"
#include "services/RecommendationService.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace
{
std::filesystem::path gDatabasePath;

struct TokenizedDoc
{
    std::vector<std::vector<std::string>> tokens;
    std::unordered_map<std::string, int> documentFrequency;
    int totalDocuments;
};

TokenizedDoc buildModel(const std::vector<std::string>& featureTexts)
{
    std::vector<std::vector<std::string>> tokenized;
    tokenized.reserve(featureTexts.size());
    for (const auto& text : featureTexts)
        tokenized.push_back(auction::services::RecommendationService::tokenize(text));

    std::unordered_map<std::string, int> df;
    for (const auto& doc : tokenized)
    {
        std::unordered_map<std::string, bool> seen;
        for (const auto& term : doc) seen[term] = true;
        for (const auto& [term, present] : seen)
            if (present) ++df[term];
    }
    return {tokenized, df, static_cast<int>(featureTexts.size())};
}

void insertCategory(auction::database::Connection& connection,
                    std::int64_t id, const std::string& name)
{
    auto statement = connection.prepare(
        "INSERT INTO categories(id, name, slug) VALUES (?, ?, ?)");
    statement.bind(1, id);
    statement.bind(2, name);
    statement.bind(3, name);
    statement.run();
}

void insertLot(auction::database::Connection& connection,
               std::int64_t id, const std::string& title, const std::string& description,
               std::int64_t categoryId, const std::string& status,
               const std::string& closedAt = "")
{
    auto statement = connection.prepare(
        "INSERT INTO auctions(id, title, description, image_url, category_id,"
        " starting_price_cents, current_price_cents, starts_at, ends_at, status, closed_at,"
        " created_at, updated_at)"
        " VALUES (?, ?, ?, ?, ?, 1000, 1000, '2026-09-01T00:00:00Z',"
        " '2027-01-01T00:00:00Z', ?, ?, '2026-09-01T00:00:00Z', '2026-09-01T00:00:00Z')");
    statement.bind(1, id);
    statement.bind(2, title);
    statement.bind(3, description);
    statement.bind(4, "/images/products/rec" + std::to_string(id) + ".jpg");
    statement.bind(5, categoryId);
    statement.bind(6, status);
    if (closedAt.empty())
        statement.bindNull(7);
    else
        statement.bind(7, closedAt);
    statement.run();
}
}

DROGON_TEST(TokenizerLowercasesAndSplits)
{
    using auction::services::RecommendationService;
    const auto tokens = RecommendationService::tokenize("iPhone 15 - SMARTPHONE, Apple; (Pro)");
    CHECK(tokens.size() == 4);
    CHECK(tokens == std::vector<std::string>({"iphone", "smartphone", "apple", "pro"}));
}

DROGON_TEST(TokenizerRemovesStopWords)
{
    using auction::services::RecommendationService;
    const auto tokens = RecommendationService::tokenize("the laptop for a developer and the book");
    for (const auto& stop : {"the", "for", "a", "and"})
        CHECK(std::find(tokens.begin(), tokens.end(), stop) == tokens.end());
    CHECK(std::find(tokens.begin(), tokens.end(), "laptop") != tokens.end());
    CHECK(std::find(tokens.begin(), tokens.end(), "developer") != tokens.end());
    CHECK(std::find(tokens.begin(), tokens.end(), "book") != tokens.end());
}

DROGON_TEST(TermFrequency)
{
    using auction::services::RecommendationService;
    const std::vector<std::string> tokens = {"cat", "cat", "dog"};
    CHECK(std::fabs(RecommendationService::termFrequency("cat", tokens) - 2.0 / 3.0) < 1e-9);
    CHECK(std::fabs(RecommendationService::termFrequency("dog", tokens) - 1.0 / 3.0) < 1e-9);
    CHECK(RecommendationService::termFrequency("bird", tokens) == 0.0);
    CHECK(RecommendationService::termFrequency("cat", {}) == 0.0);
}

DROGON_TEST(InverseDocumentFrequency)
{
    using auction::services::RecommendationService;
    const std::unordered_map<std::string, int> df = {
        {"rare", 1}, {"common", 10}, {"everywhere", 100}};
    // idf(term) = log((N+1)/(df+1)) + 1
    constexpr int total = 100;
    const auto rareIdf = RecommendationService::inverseDocumentFrequency("rare", df, total);
    const auto commonIdf = RecommendationService::inverseDocumentFrequency("common", df, total);
    const auto everywhereIdf = RecommendationService::inverseDocumentFrequency("everywhere", df, total);
    const auto expectedRare = std::log(101.0 / 2.0) + 1.0;
    const auto expectedCommon = std::log(101.0 / 11.0) + 1.0;
    const auto expectedEverywhere = std::log(101.0 / 101.0) + 1.0;
    CHECK(std::fabs(rareIdf - expectedRare) < 1e-9);
    CHECK(std::fabs(commonIdf - expectedCommon) < 1e-9);
    CHECK(std::fabs(everywhereIdf - expectedEverywhere) < 1e-9);
    CHECK(rareIdf > commonIdf);
    CHECK(commonIdf > everywhereIdf);
    CHECK(everywhereIdf == 1.0); // log(1) + 1
}

DROGON_TEST(TfIdfVector)
{
    using auction::services::RecommendationService;
    const auto model = buildModel({
        "laptop computer laptop",   // document 0
        "book desk book book"});    // document 1

    const auto doc0 = RecommendationService::computeTfIdfVector(
        model.tokens[0], model.documentFrequency, model.totalDocuments);
    CHECK(doc0.size() == 2); // {laptop, computer}
    // df(laptop)=1, tf(laptop)=2/3
    const auto expectedLaptop = (2.0 / 3.0) * (std::log(3.0 / 2.0) + 1.0);
    CHECK(std::fabs(doc0.at("laptop") - expectedLaptop) < 1e-9);

    const auto doc1 = RecommendationService::computeTfIdfVector(
        model.tokens[1], model.documentFrequency, model.totalDocuments);
    CHECK(doc1.size() == 2); // {book, desk}
    const auto expectedDesk = (1.0 / 4.0) * (std::log(3.0 / 2.0) + 1.0);
    CHECK(std::fabs(doc1.at("desk") - expectedDesk) < 1e-9);
}

DROGON_TEST(CosineSimilarity)
{
    using auction::services::RecommendationService;
    RecommendationService::TfIdfVector v1{{"a", 1.0}, {"b", 2.0}};
    RecommendationService::TfIdfVector v2{{"a", 1.0}, {"b", 2.0}};
    RecommendationService::TfIdfVector v3{{"c", 1.0}};
    RecommendationService::TfIdfVector empty;

    CHECK(std::fabs(RecommendationService::cosineSimilarity(v1, v2) - 1.0) < 1e-9);
    CHECK(RecommendationService::cosineSimilarity(v1, v3) == 0.0); // orthogonal
    CHECK(RecommendationService::cosineSimilarity(empty, v2) == 0.0);
    CHECK(RecommendationService::cosineSimilarity(v1, empty) == 0.0);
}

DROGON_TEST(UserVectorIsWeightedSum)
{
    using auction::services::RecommendationService;
    RecommendationService::TfIdfVector lotA{{"phone", 0.8}};
    RecommendationService::TfIdfVector lotB{{"laptop", 0.5}, {"phone", 0.3}};

    const auto userVector = RecommendationService::buildUserVector({{lotA, 1.0}, {lotB, 5.0}});
    CHECK(std::fabs(userVector.at("laptop") - 2.5) < 1e-9);  // 5.0 * 0.5
    CHECK(std::fabs(userVector.at("phone") - (0.8 + 5.0 * 0.3)) < 1e-9);
}

DROGON_TEST(RankingViewIphoneBidLaptop)
{
    using auction::services::RecommendationService;

    const std::vector<std::string> featureTexts = {
        "Electronics iPhone 15 smartphone",    // lot 101: viewed
        "Electronics MacBook Pro laptop computer",  // lot 102: bid
        "Art oil painting landscape",          // lot 103
        "Books vintage history book collection"};    // lot 104

    const auto model = buildModel(featureTexts);
    const std::vector<RecommendationService::TfIdfVector> lotVectors = {
        RecommendationService::computeTfIdfVector(model.tokens[0], model.documentFrequency, model.totalDocuments),
        RecommendationService::computeTfIdfVector(model.tokens[1], model.documentFrequency, model.totalDocuments),
        RecommendationService::computeTfIdfVector(model.tokens[2], model.documentFrequency, model.totalDocuments),
        RecommendationService::computeTfIdfVector(model.tokens[3], model.documentFrequency, model.totalDocuments)};

    // view = 1.0 on the iPhone lot, bid = 5.0 on the laptop lot.
    const auto userVector = RecommendationService::buildUserVector(
        {{lotVectors[0], 1.0}, {lotVectors[1], 5.0}});

    const std::vector<std::pair<std::int64_t, RecommendationService::TfIdfVector>> candidates = {
        {101, lotVectors[0]}, {102, lotVectors[1]}, {103, lotVectors[2]}, {104, lotVectors[3]}};
    const auto ranked = RecommendationService::rankBySimilarity(userVector, candidates);

    REQUIRE(ranked.size() == 4);
    // Smartphone (101) and laptop (102) rank above the painting (103) and book (104).
    const auto position = [&](std::int64_t id) {
        for (std::size_t i = 0; i < ranked.size(); ++i)
            if (ranked[i].first == id) return i;
        return ranked.size();
    };
    CHECK(position(101) < position(103));
    CHECK(position(101) < position(104));
    CHECK(position(102) < position(103));
    CHECK(position(102) < position(104));
    CHECK(ranked[0].second > 0.0);
    CHECK(ranked[3].second == 0.0); // painting shares no terms with the user vector
}

DROGON_TEST(IntegrationPersonalizedAndClosedLotExclusion)
{
    using auction::services::RecommendationService;
    const RecommendationService service(gDatabasePath);

    const auto result = service.recommendations(1, 8);
    REQUIRE(result.personalized == true);
    REQUIRE(result.lots.size() >= 2);

    bool sawPreferredSmartphone = false;
    for (const auto& lot : result.lots)
    {
        CHECK(lot.status == "active");
        // The user viewed/bid on smartphones and laptops: those lots must surface.
        if (lot.id == 5000 || lot.id == 5002) sawPreferredSmartphone = true;
        CHECK(lot.id != 5001); // bidding lot excluded
        CHECK(lot.id != 5003); // painting: no shared terms
        CHECK(lot.id != 5004); // book: no shared terms
        CHECK(lot.id != 5005); // closed lot must never be recommended
    }
    CHECK(sawPreferredSmartphone);
}

DROGON_TEST(IntegrationColdStartForUnknownUser)
{
    using auction::services::RecommendationService;
    const RecommendationService service(gDatabasePath);

    const auto cold = service.recommendations(0, 4);
    CHECK(cold.personalized == false);
    REQUIRE(!cold.lots.empty());
    for (const auto& lot : cold.lots) CHECK(lot.status == "active");
}

DROGON_TEST(ViewRecording)
{
    using auction::services::RecommendationService;
    const RecommendationService service(gDatabasePath);

    service.recordView(1, 5003); // watch the painting too
    const auto interactions =
        auction::repositories::RecommendationRepository(gDatabasePath).userInteractions(1);

    bool found = false;
    for (const auto& interaction : interactions)
    {
        if (interaction.lotId == 5003 && interaction.type == "view")
        {
            CHECK(std::fabs(interaction.weight - 1.0) < 1e-9);
            found = true;
        }
    }
    CHECK(found);
}

int main(int argc, char* argv[])
{
    try
    {
        Json::Value config;
        std::ifstream input(auction::prepareRuntime());
        input >> config;
        config["listeners"][0]["port"] = 18950;

        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        gDatabasePath = std::filesystem::absolute(
            std::filesystem::path("runtime") / ("recommendation-test-" + std::to_string(suffix) + ".sqlite3"));
        config["custom_config"]["database"]["path"] = gDatabasePath.string();

        {
            auction::database::Connection database(gDatabasePath);
            auction::database::initialize(database, "database");
            database.execute("PRAGMA foreign_keys = ON");

            // A full seed ships with the runtime; add a focused catalog on top of it.
            insertCategory(database, 20, "Electronics");
            insertCategory(database, 21, "Art & Valuables");
            insertCategory(database, 22, "Books & Media");

            insertLot(database, 5000, "Zephyr wireless smartphone device",
                      "Latest Zephyr smartphone with wireless charging", 20, "active");
            insertLot(database, 5001, "Nebula laptop computer device",
                      "Fast Nebula laptop computer for developers", 20, "active");
            insertLot(database, 5002, "Zephyr smartphone charging dock",
                      "Desktop dock for a Zephyr smartphone", 20, "active");
            insertLot(database, 5003, "Muted oil painting landscape",
                      "Hand painted oil painting of a calm landscape", 21, "active");
            insertLot(database, 5004, "Leather history book volume",
                      "Antique leather bound history book", 22, "active");
            // A smartphone product that is closed: must never appear in recommendations.
            insertLot(database, 5005, "Zephyr smartphone screen protector",
                      "Tempered glass protector for a Zephyr smartphone", 20, "closed",
                      "2026-09-10T00:00:00Z");

            database.execute(
                "INSERT INTO users(id, username, display_name, email, password_hash, created_at, updated_at)"
                " VALUES (1, 'recuser', 'Rec User', 'rec@test.dev', 'x', '2026-09-01T00:00:00Z', '2026-09-01T00:00:00Z')");
            database.execute(
                "INSERT INTO bids(auction_id, user_id, amount_cents, created_at)"
                " VALUES (5001, 1, 1500, '2026-09-02T00:00:00Z')");

            // user 1 views the smartphone (weight 1.0) and the painting, bids the laptop (5.0).
            auction::services::RecommendationService(gDatabasePath).recordView(1, 5000);
        }

        auction::security::relaxSecurityForTests(config);
        drogon::app().loadConfigJson(config);
        auction::security::registerHttpSecurity();
        std::thread server([] { drogon::app().run(); });
        while (!drogon::app().isRunning()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const auto result = drogon::test::run(argc, argv);
        drogon::app().quit();
        server.join();
        std::filesystem::remove(gDatabasePath);
        return result;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FATAL in main: " << error.what() << "\n\n";
        std::this_thread::sleep_for(std::chrono::seconds(1));
        return 99;
    }
}