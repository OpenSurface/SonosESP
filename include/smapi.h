#pragma once
/**
 * SMAPI (Sonos Music API) helpers — pure, framework-free.
 *
 * Everything a controller needs to browse a streaming service the way the
 * official app does, minus the network and the crypto: parsing the household's
 * account list, the service descriptors, SMAPI responses and faults, building
 * the SOAP envelope, and turning a browsed item into the URI + DIDL the speaker
 * needs to play it.
 *
 * Split out of music_services.cpp so it builds and runs on the host
 * (test/test_smapi). No Arduino, FreeRTOS or LVGL here — std::string only.
 *
 * The protocol details follow the reverse-engineered desktop-controller flow
 * documented in SoCo PR #1010 (soco/music_services/browser/). Where a value
 * below looks arbitrary (a flags number, an id prefix), it is what the official
 * controller sends and was verified against real players there.
 */

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <utility>
#include <vector>

namespace smapi {

static const char* const NS = "http://www.sonos.com/Services/1.1";

// Descriptor capability bits (ListAvailableServices "Capabilities").
static const uint32_t CAP_BEARER_TOKEN = 1u << 3;   // token travels as HTTP Bearer, not in SOAP
static const uint32_t CAP_CONTEXT      = 1u << 16;  // send <context><timeZone>
static const uint32_t CAP_ZONE_PLAYER  = 1u << 18;  // send <zonePlayerId>

// ---------------------------------------------------------------------------
// XML text helpers
// ---------------------------------------------------------------------------

inline std::string xmlEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   out += c;
        }
    }
    return out;
}

inline void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += (char)cp;
    } else if (cp < 0x800) {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x110000) {
        out += (char)(0xF0 | (cp >> 18));
        out += (char)(0x80 | ((cp >> 12) & 0x3F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
}

// One level of entity decoding, in a single pass — so "&amp;lt;" becomes the
// literal text "&lt;", never "<". Numeric references become UTF-8.
inline std::string xmlUnescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] != '&') { out += s[i]; continue; }
        size_t semi = s.find(';', i + 1);
        if (semi == std::string::npos || semi - i > 10) { out += s[i]; continue; }
        std::string ent = s.substr(i + 1, semi - i - 1);
        if      (ent == "amp")  out += '&';
        else if (ent == "lt")   out += '<';
        else if (ent == "gt")   out += '>';
        else if (ent == "quot") out += '"';
        else if (ent == "apos") out += '\'';
        else if (ent.size() > 1 && ent[0] == '#') {
            char* endp = nullptr;
            uint32_t cp = (ent[1] == 'x' || ent[1] == 'X')
                        ? (uint32_t)strtoul(ent.c_str() + 2, &endp, 16)
                        : (uint32_t)strtoul(ent.c_str() + 1, &endp, 10);
            if (!endp || *endp != '\0') { out += s[i]; continue; }
            appendUtf8(out, cp);
        } else {
            out += s[i];
            continue;
        }
        i = semi;
    }
    return out;
}

inline std::string toLower(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c += 32;
    return s;
}

// ---------------------------------------------------------------------------
// Element lookup by LOCAL name
// ---------------------------------------------------------------------------
// SMAPI providers disagree on prefixes: one answers <mediaCollection>, the next
// <ns2:mediaCollection>, a third <s:Fault> vs <soap:Fault>. Matching on the
// local name is the only way to parse all of them with one code path.
//
// Finds the first element named `local` that STARTS in [from, to). On success
// fills the content range [*cs, *ce) and *end (index just past the closing
// tag). A self-closing element has an empty content range.
inline bool findElement(const std::string& xml, const char* local, size_t from, size_t to,
                        size_t* cs, size_t* ce, size_t* end) {
    const size_t llen = strlen(local);
    if (to > xml.size()) to = xml.size();
    size_t pos = xml.find('<', from);
    while (pos != std::string::npos && pos < to) {
        char c1 = pos + 1 < xml.size() ? xml[pos + 1] : '\0';
        if (c1 != '/' && c1 != '?' && c1 != '!') {
            size_t n = pos + 1;
            while (n < xml.size() && xml[n] != ' ' && xml[n] != '>' && xml[n] != '/' &&
                   xml[n] != '\t' && xml[n] != '\r' && xml[n] != '\n') n++;
            std::string qname = xml.substr(pos + 1, n - pos - 1);
            size_t colon = qname.find(':');
            const char* lname = qname.c_str() + (colon == std::string::npos ? 0 : colon + 1);
            if (strlen(lname) == llen && memcmp(lname, local, llen) == 0) {
                size_t gt = xml.find('>', n);
                if (gt == std::string::npos) return false;
                if (xml[gt - 1] == '/') {
                    *cs = *ce = gt + 1;
                    *end = gt + 1;
                    return true;
                }
                std::string close = "</" + qname + ">";
                size_t cpos = xml.find(close, gt + 1);
                if (cpos == std::string::npos) return false;
                *cs = gt + 1;
                *ce = cpos;
                *end = cpos + close.size();
                return true;
            }
        }
        pos = xml.find('<', pos + 1);
    }
    return false;
}

// Decoded text of the first `local` element in [from, to), or "" if absent.
inline std::string childText(const std::string& xml, const char* local,
                             size_t from = 0, size_t to = std::string::npos) {
    size_t cs, ce, end;
    if (!findElement(xml, local, from, to, &cs, &ce, &end)) return "";
    return xmlUnescape(xml.substr(cs, ce - cs));
}

// Decoded value of attribute `name` inside one tag's text ("<X a=\"1\" b=\"2\">").
inline std::string attr(const std::string& tag, const char* name) {
    std::string key = std::string(name) + "=\"";
    size_t p = 0;
    while ((p = tag.find(key, p)) != std::string::npos) {
        if (p > 0 && (tag[p - 1] == ' ' || tag[p - 1] == '\t' || tag[p - 1] == '\n' ||
                      tag[p - 1] == '\r')) {
            size_t vs = p + key.size();
            size_t ve = tag.find('"', vs);
            if (ve == std::string::npos) return "";
            return xmlUnescape(tag.substr(vs, ve - vs));
        }
        p += key.size();
    }
    return "";
}

inline bool parseBool(const std::string& v, bool dflt) {
    if (v.empty()) return dflt;
    return v == "true" || v == "1" || v == "TRUE" || v == "True";
}

// ---------------------------------------------------------------------------
// Household accounts (decrypted ThirdPartyMediaServersX)
// ---------------------------------------------------------------------------

struct Account {
    int serviceId   = 0;   // Sonos service id ("sid"), e.g. 12 for Spotify
    int serviceType = 0;   // the number in SA_RINCON<type>_ — sid * 256 + 7
    int serial      = 0;   // SerialNum0 — the "sn" in playback URIs
    std::string udn;       // SA_RINCON3079_X_#Svc3079-<uid>-Token — the DIDL cdudn
    std::string username, password, token, key, nickname;
};

// "SA_RINCON3079_..." -> 3079. False if the prefix is not there.
inline bool udnServiceType(const std::string& udn, int* type) {
    static const char kPrefix[] = "SA_RINCON";
    if (udn.compare(0, sizeof(kPrefix) - 1, kPrefix) != 0) return false;
    size_t p = sizeof(kPrefix) - 1;
    int v = 0, digits = 0;
    while (p < udn.size() && udn[p] >= '0' && udn[p] <= '9') {
        v = v * 10 + (udn[p] - '0');
        p++; digits++;
    }
    if (!digits) return false;
    *type = v;
    return true;
}

// The account UID is the hex field of a token UDN: X_#Svc<type>-<uid>-Token.
// Anonymous accounts carry a bare "SA_RINCON<type>_" and have none.
inline bool accountUid(const std::string& udn, uint32_t* uid) {
    size_t p = udn.find("X_#Svc");
    if (p == std::string::npos) return false;
    p += 6;
    while (p < udn.size() && udn[p] >= '0' && udn[p] <= '9') p++;
    if (p >= udn.size() || udn[p] != '-') return false;
    size_t hs = ++p;
    while (p < udn.size() && isxdigit((unsigned char)udn[p])) p++;
    if (p == hs || udn.compare(p, std::string::npos, "-Token") != 0) return false;
    *uid = (uint32_t)strtoul(udn.substr(hs, p - hs).c_str(), nullptr, 16);
    return true;
}

// loginToken.householdId. The desktop controller sends an account-scoped
// identity, "<household>_<uid as 8 hex digits>", whenever the account has a
// token UDN; Apple Music rejects the bare household id with InvalidToken.
inline std::string scopedHouseholdId(const std::string& household, const std::string& udn) {
    uint32_t uid;
    if (!accountUid(udn, &uid)) return household;
    char buf[16];
    snprintf(buf, sizeof(buf), "_%08x", (unsigned)uid);
    return household + buf;
}

// Every element carrying a UDN="SA_RINCON..." attribute is one account. The
// element names are not documented, so match on the attribute alone.
inline std::vector<Account> parseAccounts(const std::string& xml) {
    std::vector<Account> out;
    size_t pos = 0;
    while ((pos = xml.find('<', pos)) != std::string::npos) {
        size_t gt = xml.find('>', pos);
        if (gt == std::string::npos) break;
        std::string tag = xml.substr(pos, gt - pos + 1);
        pos = gt + 1;
        std::string udn = attr(tag, "UDN");
        int type = 0;
        if (!udnServiceType(udn, &type)) continue;
        Account a;
        a.serviceType = type;
        a.serviceId   = type / 256;
        a.udn         = udn;
        a.serial      = atoi(attr(tag, "SerialNum0").c_str());
        a.username    = attr(tag, "Username0");
        a.password    = attr(tag, "Password0");
        a.token       = attr(tag, "Token0");
        a.key         = attr(tag, "Key0");
        a.nickname    = attr(tag, "Nickname0");
        out.push_back(a);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Service descriptors (decoded ListAvailableServices descriptor list)
// ---------------------------------------------------------------------------

struct Service {
    int id = 0;
    uint32_t caps = 0;
    std::string name, uri, secureUri, auth, containerType;
    std::string manifestUri;   // JSON manifest (newer services: content home page, strings)
    std::string pmapUri;       // presentation map (search categories, display rules)

    // The SMAPI endpoint to POST to. HTTPS only: the request carries the
    // account's token, and a descriptor with only a plain-http Uri is a
    // service we will not send credentials to.
    std::string endpoint() const {
        if (secureUri.compare(0, 8, "https://") == 0) return secureUri;
        if (uri.compare(0, 8, "https://") == 0) return uri;
        return "";
    }
};

inline std::vector<Service> parseServices(const std::string& xml) {
    std::vector<Service> out;
    size_t pos = 0;
    while ((pos = xml.find("<Service ", pos)) != std::string::npos) {
        size_t gt = xml.find('>', pos);
        if (gt == std::string::npos) break;
        std::string tag = xml.substr(pos, gt - pos + 1);
        size_t next = xml.find("<Service ", gt);
        size_t stop = xml.find("</Service>", gt);
        if (stop == std::string::npos || (next != std::string::npos && next < stop)) stop = next;
        if (stop == std::string::npos) stop = xml.size();

        Service s;
        s.id            = atoi(attr(tag, "Id").c_str());
        s.name          = attr(tag, "Name");
        s.uri           = attr(tag, "Uri");
        s.secureUri     = attr(tag, "SecureUri");
        s.containerType = attr(tag, "ContainerType");
        s.caps          = (uint32_t)strtoul(attr(tag, "Capabilities").c_str(), nullptr, 10);
        // Child elements of this <Service>: the first <Name ...> tag inside it.
        auto childAttr = [&](const char* open, const char* name) -> std::string {
            size_t cp = xml.find(open, gt);
            if (cp == std::string::npos || cp >= stop) return "";
            size_t cgt = xml.find('>', cp);
            if (cgt == std::string::npos) return "";
            return attr(xml.substr(cp, cgt - cp + 1), name);
        };
        s.auth        = childAttr("<Policy ", "Auth");
        s.manifestUri = childAttr("<Manifest ", "Uri");
        s.pmapUri     = childAttr("<PresentationMap ", "Uri");
        if (s.id > 0) out.push_back(s);
        pos = gt + 1;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Request envelope
// ---------------------------------------------------------------------------

struct Credentials {
    uint32_t caps = 0;
    std::string auth;          // Anonymous | UserId | UserIdPassword | DeviceLink | AppLink
    std::string deviceId;      // the speaker's R_TrialZPSerial
    std::string zonePlayerId;  // the speaker's RINCON id (only sent with CAP_ZONE_PLAYER)
    std::string householdId;   // already scoped (scopedHouseholdId)
    std::string token, key, username, password;
    std::string timeZone;      // IANA name, e.g. "Europe/Paris"
};

typedef std::vector<std::pair<std::string, std::string>> Fields;

// Mirrors _ConfiguredSmapiClient._credentials()/_envelope() in the reference.
// `refresh` builds the refreshAuthToken variant, where the old token and key
// always travel in SOAP even for a Bearer-token service.
inline std::string buildEnvelope(const char* action, const Fields& fields,
                                 const Credentials& c, bool refresh = false) {
    std::string e;
    e.reserve(1024);
    e += "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
         "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" xmlns:ns=\"";
    e += NS;
    e += "\"><s:Header><ns:credentials>";
    if ((c.caps & CAP_ZONE_PLAYER) && !c.zonePlayerId.empty())
        e += "<ns:zonePlayerId>" + xmlEscape(c.zonePlayerId) + "</ns:zonePlayerId>";
    e += "<ns:deviceId>" + xmlEscape(c.deviceId) + "</ns:deviceId>"
         "<ns:deviceProvider>Sonos</ns:deviceProvider>";

    const bool anonymous = c.auth == "Anonymous";
    const bool userId    = c.auth == "UserId" || c.auth == "UserIdPassword";
    if (!anonymous) {
        if (!refresh && userId) {
            e += "<ns:login><ns:username>" + xmlEscape(c.username) + "</ns:username>"
                 "<ns:password>" + xmlEscape(c.password) + "</ns:password></ns:login>";
        } else if (!refresh && !c.token.empty()) {
            e += "<ns:loginToken>";
            if (!(c.caps & CAP_BEARER_TOKEN)) {
                e += "<ns:token>" + xmlEscape(c.token) + "</ns:token>";
                if (!c.key.empty()) e += "<ns:key>" + xmlEscape(c.key) + "</ns:key>";
            }
            e += "<ns:householdId>" + xmlEscape(c.householdId) + "</ns:householdId></ns:loginToken>";
        } else if (!refresh && c.auth == "DeviceLink") {
            // Token-less DeviceLink needs a getSessionId session — not supported.
        } else if (!c.token.empty() || !c.householdId.empty()) {
            e += "<ns:loginToken>";
            if (!(c.caps & CAP_BEARER_TOKEN) || refresh) {
                if (!c.token.empty()) e += "<ns:token>" + xmlEscape(c.token) + "</ns:token>";
                if (!c.key.empty())   e += "<ns:key>" + xmlEscape(c.key) + "</ns:key>";
            }
            e += "<ns:householdId>" + xmlEscape(c.householdId) + "</ns:householdId></ns:loginToken>";
        }
    }
    e += "</ns:credentials>";
    if (c.caps & CAP_CONTEXT)
        e += "<ns:context><ns:timeZone>" + xmlEscape(c.timeZone) + "</ns:timeZone></ns:context>";
    e += "</s:Header><s:Body><ns:";
    e += action;
    e += ">";
    for (const auto& f : fields)
        e += "<ns:" + f.first + ">" + xmlEscape(f.second) + "</ns:" + f.first + ">";
    e += "</ns:";
    e += action;
    e += "></s:Body></s:Envelope>";
    return e;
}

// ---------------------------------------------------------------------------
// Faults
// ---------------------------------------------------------------------------

struct Fault {
    std::string code, message;
    std::string newToken, newKey;   // refreshed credentials, when the fault carries them
    bool retryable = false;         // provider asked for a plain retry (Apple SonosError 999)
};

inline bool parseFault(const std::string& xml, Fault* f) {
    size_t cs, ce, end;
    if (!findElement(xml, "Fault", 0, std::string::npos, &cs, &ce, &end)) return false;
    f->code      = childText(xml, "faultcode", cs, ce);
    f->message   = childText(xml, "faultstring", cs, ce);
    f->newToken  = childText(xml, "authToken", cs, ce);
    f->newKey    = childText(xml, "privateKey", cs, ce);
    f->retryable = childText(xml, "SonosError", cs, ce) == "999";
    return true;
}

// Same markers as the reference's _is_expired_fault().
inline bool isExpiredFault(const Fault& f, int httpStatus) {
    std::string s = toLower(f.code + " " + f.message);
    return s.find("authtokenexpired") != std::string::npos ||
           s.find("invalidtoken") != std::string::npos ||
           s.find("tokenrefreshrequired") != std::string::npos ||
           s.find("token expired") != std::string::npos ||
           s.find("unauthorized") != std::string::npos ||
           httpStatus == 401;
}

inline bool isTransient(const Fault& f, int httpStatus) {
    if (f.retryable) return true;
    if (httpStatus == 408 || httpStatus == 429 || httpStatus == 502 ||
        httpStatus == 503 || httpStatus == 504) return true;
    std::string s = toLower(f.code + " " + f.message);
    return s.find("timed out") != std::string::npos ||
           s.find("temporarily unavailable") != std::string::npos ||
           s.find("try again") != std::string::npos;
}

// ---------------------------------------------------------------------------
// getMetadata results
// ---------------------------------------------------------------------------

struct Item {
    std::string id, title, subtitle, itemType, mimeType, albumArt;
    bool collection   = false;   // mediaCollection (container) vs mediaMetadata (playable leaf)
    bool canPlay      = false;
    bool canEnumerate = false;
};

struct Page {
    int index = 0, count = 0, total = 0;
    std::vector<Item> items;
};

// Parses <resultName> (getMetadataResult, searchResult) into a page. False when
// the result element is missing — i.e. this was not a metadata response.
inline bool parseMetadataResult(const std::string& xml, const char* resultName, Page* out) {
    size_t rs, re, rend;
    if (!findElement(xml, resultName, 0, std::string::npos, &rs, &re, &rend)) return false;

    size_t pos = rs;
    while (pos < re) {
        size_t mcS, mcE, mcEnd, mmS, mmE, mmEnd;
        bool hasMc = findElement(xml, "mediaCollection", pos, re, &mcS, &mcE, &mcEnd);
        bool hasMm = findElement(xml, "mediaMetadata", pos, re, &mmS, &mmE, &mmEnd);
        if (!hasMc && !hasMm) break;
        bool isMc = hasMc && (!hasMm || mcS < mmS);
        size_t cs = isMc ? mcS : mmS, ce = isMc ? mcE : mmE, end = isMc ? mcEnd : mmEnd;

        Item it;
        it.collection   = isMc;
        it.id           = childText(xml, "id", cs, ce);
        it.title        = childText(xml, "title", cs, ce);
        it.itemType     = childText(xml, "itemType", cs, ce);
        it.mimeType     = childText(xml, "mimeType", cs, ce);
        it.albumArt     = childText(xml, "albumArtURI", cs, ce);
        // mediaCollection carries <artist>; a track's lives in <trackMetadata>.
        // First occurrence covers both. Fall back to the album, then the genre.
        it.subtitle     = childText(xml, "artist", cs, ce);
        if (it.subtitle.empty()) it.subtitle = childText(xml, "album", cs, ce);
        // Containers default to browsable, leaves to playable — the SMAPI schema
        // defaults, which several providers rely on by omitting the element.
        it.canPlay      = parseBool(childText(xml, "canPlay", cs, ce), !isMc);
        it.canEnumerate = parseBool(childText(xml, "canEnumerate", cs, ce), isMc);
        if (!it.id.empty()) out->items.push_back(it);
        pos = end;
    }

    // Only the result's own <index>/<count>/<total>: search outside the items.
    size_t firstItem = re;
    size_t a, b, c;
    if (findElement(xml, "mediaCollection", rs, re, &a, &b, &c)) firstItem = a;
    if (findElement(xml, "mediaMetadata", rs, firstItem, &a, &b, &c)) firstItem = a;
    std::string idx = childText(xml, "index", rs, firstItem);
    std::string cnt = childText(xml, "count", rs, firstItem);
    std::string tot = childText(xml, "total", rs, firstItem);
    out->index = idx.empty() ? 0 : atoi(idx.c_str());
    out->count = cnt.empty() ? (int)out->items.size() : atoi(cnt.c_str());
    out->total = tot.empty() ? out->index + (int)out->items.size() : atoi(tot.c_str());
    return true;
}

// ---------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------

// Punctuation in a provider id is percent-encoded, lowercase, exactly as the
// reference does. The URI and the DIDL item id MUST use the same spelling —
// a speaker treats "%3a" and ":" as two different objects (see the note on
// favourites in ui_settings_screens.cpp).
inline std::string escapeId(const std::string& id) {
    std::string out;
    out.reserve(id.size() + 16);
    for (char c : id) {
        switch (c) {
            case ':': out += "%3a"; break;
            case '/': out += "%2f"; break;
            case '?': out += "%3f"; break;
            case '=': out += "%3d"; break;
            case '&': out += "%26"; break;
            case '#': out += "%23"; break;
            default:  out += c;
        }
    }
    return out;
}

// "trackList.program" -> "program". Providers compound the type; the base type
// after the last dot is the authoritative one.
inline std::string baseType(const std::string& itemType) {
    size_t dot = itemType.rfind('.');
    return toLower(dot == std::string::npos ? itemType : itemType.substr(dot + 1));
}

struct Playback {
    std::string uri;
    std::string didl;        // plain DIDL-Lite (one level of escaping inside)
    bool container = false;  // true: enqueue (AddURIToQueue); false: SetAVTransportURI
};

inline std::string didlItem(const std::string& itemId, const std::string& title,
                            const std::string& upnpClass, const std::string& udn,
                            const std::string& res = "") {
    return "<DIDL-Lite xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
           "xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\" "
           "xmlns:r=\"urn:schemas-rinconnetworks-com:metadata-1-0/\" "
           "xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\">"
           "<item id=\"" + xmlEscape(itemId) + "\" parentID=\"-1\" restricted=\"true\">"
           "<dc:title>" + xmlEscape(title) + "</dc:title>"
           "<upnp:class>" + upnpClass + "</upnp:class>" + res +
           "<desc id=\"cdudn\" nameSpace=\"urn:schemas-rinconnetworks-com:metadata-1-0/\">" +
           xmlEscape(udn) + "</desc></item></DIDL-Lite>";
}

// Turns a browsed item into what the speaker needs to play it. False when the
// item is not playable (canPlay false, or a type with no known URI form).
//
// Leaves follow build_uri()/build_metadata() in the reference. Containers
// (albums, playlists) follow the form the Sonos app stores in favourites:
//     x-rincon-cpcontainer:1004206c<id>?sid=<sid>&flags=8300&sn=<sn>
// where the 8-hex prefix is the class code followed by the flags in hex
// (0x206c == 8300) — the same shape this firmware already plays from
// Favorites via playContainer().
inline bool buildPlayback(const Item& it, int sid, int serial, const std::string& udn,
                          Playback* out) {
    if (!it.canPlay || it.id.empty()) return false;
    const std::string type = baseType(it.itemType);
    const std::string mime = toLower(it.mimeType);
    const std::string enc  = escapeId(it.id);
    char q[64];

    if (type == "program") {   // radio-style programmes (Sonos Radio, Pandora)
        snprintf(q, sizeof(q), "?sid=%d&flags=104&sn=%d", sid, serial);
        out->uri  = "x-sonosapi-radio:" + enc + q;
        out->didl = didlItem("000c0068" + enc, it.title,
                             "object.item.audioItem.audioBroadcast", udn);
        out->container = false;
        return true;
    }
    if (type == "audiobook") {
        snprintf(q, sizeof(q), "?sid=%d&flags=16584&sn=%d", sid, serial);
        out->uri  = "x-rincon-cpcontainer:101340c8" + enc + q;
        out->didl = didlItem("101340c8" + enc, it.title, "object.item.audioItem.audioBook", udn);
        out->container = true;
        return true;
    }

    if (it.collection) {
        const bool album = type == "album";
        const std::string prefix = album ? "1004206c" : "1006206c";
        snprintf(q, sizeof(q), "?sid=%d&flags=8300&sn=%d", sid, serial);
        out->uri  = "x-rincon-cpcontainer:" + prefix + enc + q;
        out->didl = didlItem(prefix + enc, it.title,
                             album ? "object.container.album.musicAlbum"
                                   : "object.container.playlistContainer", udn);
        out->container = true;
        return true;
    }

    if (type == "stream") {
        const char* scheme = "x-sonosapi-stream";
        int flags = 32;
        if (mime == "audio/aac") {
            flags = 8224;
        } else if (mime == "application/x-mpegurl" || mime == "audio/x-mpegurl" ||
                   mime == "audio/x-scpls") {
            scheme = "x-sonosapi-hls";
            flags = 288;
        }
        snprintf(q, sizeof(q), "?sid=%d&flags=%d&sn=%d", sid, flags, serial);
        out->uri  = std::string(scheme) + ":" + enc + q;
        out->didl = didlItem("00090000" + enc, it.title,
                             "object.item.audioItem.audioBroadcast", udn);
        out->container = false;
        return true;
    }
    if (type == "show" || type == "episode") {
        snprintf(q, sizeof(q), "?sid=%d&flags=24616&sn=%d", sid, serial);
        out->uri  = "x-sonosapi-hls-static:" + enc + q;
        out->didl = didlItem("10156128" + enc, it.title, "object.item.audioItem.podcast", udn);
        out->container = false;
        return true;
    }

    // Everything else that canPlay is treated as a track — "track", and the
    // "other" some providers use for one. Spotify is recognised by its MIME
    // type, not its sid: the sid is regional (12, but 9 on many EU accounts).
    const bool spotify = mime == "audio/x-spotify";
    const int trackFlags = (spotify || sid == 204) ? 8224 : 32;   // Spotify, Apple Music
    std::string res;
    if (spotify) {
        snprintf(q, sizeof(q), "?sid=%d&flags=%d&sn=%d", sid, trackFlags, serial);
        out->uri = "x-sonos-spotify:" + enc + q;
        // The player only accepts an x-sonos-spotify track with this protocol
        // info in the DIDL (verified in the reference against six models).
        res = "<res protocolInfo=\"sonos.com-spotify:*:audio/x-spotify:*\">" +
              xmlEscape(out->uri) + "</res>";
    } else if (mime == "audio/flac") {
        snprintf(q, sizeof(q), "?sid=%d&flags=0&sn=%d", sid, serial);
        out->uri = "x-sonos-http:" + enc + ".flac" + q;
    } else {
        const char* ext = "mp3";
        if (mime == "audio/aac" || mime == "audio/mp4") ext = "mp4";
        else if (mime == "audio/x-ms-wma" || mime == "audio/wma") ext = "wma";
        else if (mime == "audio/ogg" || mime == "application/ogg") ext = "ogg";
        snprintf(q, sizeof(q), "?sid=%d&flags=%d&sn=%d", sid, trackFlags, serial);
        out->uri = "x-sonos-http:" + enc + "." + ext + q;
    }
    out->didl = didlItem("10030020" + enc, it.title, "object.item.audioItem.musicTrack", udn, res);
    out->container = false;
    return true;
}

}  // namespace smapi
