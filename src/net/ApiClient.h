#pragma once

#include "HttpClient.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <functional>
#include <array>

namespace OpenSpore {

using json = nlohmann::json;

struct EmpireData {
    std::string id;
    std::string playerId;
    std::string name;
    std::string homeWorld;
    std::array<int, 3> color;
    bool online = false;

    static EmpireData FromJson(const json& j) {
        EmpireData e;
        e.id = j.value("id", "");
        e.playerId = j.value("playerId", "");
        e.name = j.value("name", "");
        e.homeWorld = j.value("homeWorld", "");
        e.online = j.value("online", false);
        if (j.contains("color") && j["color"].is_array()) {
            auto& c = j["color"];
            e.color = {c[0].get<int>(), c[1].get<int>(), c[2].get<int>()};
        }
        return e;
    }
};

struct DiplomacyOffer {
    std::string id;
    std::string fromId;
    std::string toId;
    std::string type;   // "alliance", "war", "trade"
    std::string status; // "pending", "accepted", "rejected"

    static DiplomacyOffer FromJson(const json& j) {
        DiplomacyOffer d;
        d.id = j.value("id", "");
        d.fromId = j.value("fromId", "");
        d.toId = j.value("toId", "");
        d.type = j.value("type", "");
        d.status = j.value("status", "");
        return d;
    }
};

struct GalaxyResponse {
    std::vector<EmpireData> empires;
    int total = 0;
    int page = 0;
    int limit = 0;
};

using RegisterCallback = std::function<void(bool success, const EmpireData& empire, const std::string& token)>;
using GalaxyCallback = std::function<void(bool success, const GalaxyResponse& data)>;
using SimpleCallback = std::function<void(bool success)>;
using DiplomacyListCallback = std::function<void(bool success, const std::vector<DiplomacyOffer>& offers)>;

/// High-level API client wrapping the OpenSpore server REST endpoints.
class ApiClient {
public:
    ApiClient(HttpClient& http);

    /// POST /empire/register
    void RegisterEmpire(const std::string& playerId, const std::string& name,
                        const std::string& homeWorld, const std::array<int, 3>& color,
                        RegisterCallback callback);

    /// PATCH /empire/:id/heartbeat
    void SendHeartbeat(const std::string& empireId, SimpleCallback callback);

    /// GET /galaxy/empires?page=&limit=
    void GetGalaxyEmpires(int page, int limit, GalaxyCallback callback);

    /// POST /diplomacy/offer
    void CreateDiplomacyOffer(const std::string& fromId, const std::string& toId,
                              const std::string& type, SimpleCallback callback);

    /// GET /diplomacy/offers/:empireId
    void GetDiplomacyOffers(const std::string& empireId, DiplomacyListCallback callback);

    /// PATCH /diplomacy/offer/:id/accept
    void AcceptOffer(const std::string& offerId, SimpleCallback callback);

    /// PATCH /diplomacy/offer/:id/reject
    void RejectOffer(const std::string& offerId, SimpleCallback callback);

private:
    HttpClient& mHttp;
};

} // namespace OpenSpore
