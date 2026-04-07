#include "ApiClient.h"

namespace OpenSpore {

ApiClient::ApiClient(HttpClient& http) : mHttp(http) {}

void ApiClient::RegisterEmpire(const std::string& playerId, const std::string& name,
                                const std::string& homeWorld, const std::array<int, 3>& color,
                                RegisterCallback callback) {
    json body;
    body["playerId"] = playerId;
    body["name"] = name;
    body["homeWorld"] = homeWorld;
    body["color"] = {color[0], color[1], color[2]};

    mHttp.Post("/empire/register", body.dump(), [callback](const HttpResponse& resp) {
        if (!resp.success) {
            callback(false, {}, "");
            return;
        }
        try {
            auto j = json::parse(resp.body);
            auto empire = EmpireData::FromJson(j["empire"]);
            auto token = j.value("token", "");
            callback(true, empire, token);
        } catch (...) {
            callback(false, {}, "");
        }
    });
}

void ApiClient::SendHeartbeat(const std::string& empireId, SimpleCallback callback) {
    mHttp.Patch("/empire/" + empireId + "/heartbeat", "{}", [callback](const HttpResponse& resp) {
        callback(resp.success);
    });
}

void ApiClient::GetGalaxyEmpires(int page, int limit, GalaxyCallback callback) {
    std::string path = "/galaxy/empires?page=" + std::to_string(page) +
                       "&limit=" + std::to_string(limit);

    mHttp.Get(path, [callback](const HttpResponse& resp) {
        GalaxyResponse data;
        if (!resp.success) {
            callback(false, data);
            return;
        }
        try {
            auto j = json::parse(resp.body);
            data.total = j.value("total", 0);
            data.page = j.value("page", 0);
            data.limit = j.value("limit", 0);
            for (auto& item : j["empires"]) {
                data.empires.push_back(EmpireData::FromJson(item));
            }
            callback(true, data);
        } catch (...) {
            callback(false, data);
        }
    });
}

void ApiClient::CreateDiplomacyOffer(const std::string& fromId, const std::string& toId,
                                      const std::string& type, SimpleCallback callback) {
    json body;
    body["fromId"] = fromId;
    body["toId"] = toId;
    body["type"] = type;

    mHttp.Post("/diplomacy/offer", body.dump(), [callback](const HttpResponse& resp) {
        callback(resp.success);
    });
}

void ApiClient::GetDiplomacyOffers(const std::string& empireId, DiplomacyListCallback callback) {
    mHttp.Get("/diplomacy/offers/" + empireId, [callback](const HttpResponse& resp) {
        std::vector<DiplomacyOffer> offers;
        if (!resp.success) {
            callback(false, offers);
            return;
        }
        try {
            auto j = json::parse(resp.body);
            for (auto& item : j) {
                offers.push_back(DiplomacyOffer::FromJson(item));
            }
            callback(true, offers);
        } catch (...) {
            callback(false, offers);
        }
    });
}

void ApiClient::AcceptOffer(const std::string& offerId, SimpleCallback callback) {
    mHttp.Patch("/diplomacy/offer/" + offerId + "/accept", "{}", [callback](const HttpResponse& resp) {
        callback(resp.success);
    });
}

void ApiClient::RejectOffer(const std::string& offerId, SimpleCallback callback) {
    mHttp.Patch("/diplomacy/offer/" + offerId + "/reject", "{}", [callback](const HttpResponse& resp) {
        callback(resp.success);
    });
}

} // namespace OpenSpore
