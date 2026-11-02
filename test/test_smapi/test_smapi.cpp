/**
 * Host tests for include/smapi.h
 *
 *     pio test -e native
 *
 * The payload shapes below are the ones the reference implementation (SoCo
 * PR #1010) records from real households and providers: prefixed and
 * unprefixed SMAPI responses, compound item types, faults carrying refreshed
 * credentials, and the account attributes of a decrypted
 * ThirdPartyMediaServersX.
 */

#include <unity.h>
#include <string>
#include "smapi.h"

using namespace smapi;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// XML helpers
// ---------------------------------------------------------------------------

void test_unescape_single_level(void) {
    // One level only: a double-escaped "&amp;lt;" must come out as the text
    // "&lt;", not as "<" — that is what keeps a title with a literal "&lt;"
    // from turning into markup.
    TEST_ASSERT_EQUAL_STRING("R&B", xmlUnescape("R&amp;B").c_str());
    TEST_ASSERT_EQUAL_STRING("&lt;", xmlUnescape("&amp;lt;").c_str());
    TEST_ASSERT_EQUAL_STRING("<a href=\"x\">", xmlUnescape("&lt;a href=&quot;x&quot;&gt;").c_str());
}

void test_unescape_numeric_to_utf8(void) {
    TEST_ASSERT_EQUAL_STRING("Beyonc\xC3\xA9", xmlUnescape("Beyonc&#233;").c_str());
    TEST_ASSERT_EQUAL_STRING("Bj\xC3\xB6rk", xmlUnescape("Bj&#xF6;rk").c_str());
    // Not an entity: left alone rather than eaten.
    TEST_ASSERT_EQUAL_STRING("AT&T rocks", xmlUnescape("AT&T rocks").c_str());
}

void test_escape_roundtrip(void) {
    std::string s = "Tom & Jerry <\"live\"> 'mix'";
    TEST_ASSERT_EQUAL_STRING(s.c_str(), xmlUnescape(xmlEscape(s)).c_str());
}

void test_find_element_ignores_prefix(void) {
    std::string a = "<x><ns2:title>Hello</ns2:title></x>";
    std::string b = "<x><title>Hello</title></x>";
    TEST_ASSERT_EQUAL_STRING("Hello", childText(a, "title").c_str());
    TEST_ASSERT_EQUAL_STRING("Hello", childText(b, "title").c_str());
}

void test_find_element_exact_local_name(void) {
    // <idx> must not satisfy a lookup for <id>.
    std::string x = "<r><idx>wrong</idx><id>right</id></r>";
    TEST_ASSERT_EQUAL_STRING("right", childText(x, "id").c_str());
}

void test_find_element_self_closing(void) {
    std::string x = "<r><title/><id>7</id></r>";
    TEST_ASSERT_EQUAL_STRING("", childText(x, "title").c_str());
    TEST_ASSERT_EQUAL_STRING("7", childText(x, "id").c_str());
}

void test_attr_requires_word_boundary(void) {
    std::string tag = "<A xUDN=\"no\" UDN=\"yes\" Token0=\"a&amp;b\">";
    TEST_ASSERT_EQUAL_STRING("yes", attr(tag, "UDN").c_str());
    TEST_ASSERT_EQUAL_STRING("a&b", attr(tag, "Token0").c_str());
    TEST_ASSERT_EQUAL_STRING("", attr(tag, "Missing").c_str());
}

// ---------------------------------------------------------------------------
// Accounts
// ---------------------------------------------------------------------------

static const char* ACCOUNTS =
    "<Accounts LastUpdateDevice=\"RINCON_X\" Version=\"21\" NextSerialNum=\"9\">"
    "<Account Type=\"3079\" SerialNum=\"3\" UDN=\"SA_RINCON3079_X_#Svc3079-1a2b3c4d-Token\" "
    "SerialNum0=\"3\" Username0=\"\" Token0=\"tok+/=\" Key0=\"key&amp;1\" Nickname0=\"Ben\"/>"
    "<Account Type=\"65031\" UDN=\"SA_RINCON65031_\" SerialNum0=\"0\"/>"
    "<Other Foo=\"bar\"/>"
    "</Accounts>";

void test_parse_accounts(void) {
    std::vector<Account> a = parseAccounts(ACCOUNTS);
    TEST_ASSERT_EQUAL_INT(2, (int)a.size());

    TEST_ASSERT_EQUAL_INT(12, a[0].serviceId);       // 3079 / 256 — Spotify
    TEST_ASSERT_EQUAL_INT(3079, a[0].serviceType);
    TEST_ASSERT_EQUAL_INT(3, a[0].serial);
    TEST_ASSERT_EQUAL_STRING("tok+/=", a[0].token.c_str());
    TEST_ASSERT_EQUAL_STRING("key&1", a[0].key.c_str());
    TEST_ASSERT_EQUAL_STRING("Ben", a[0].nickname.c_str());

    TEST_ASSERT_EQUAL_INT(254, a[1].serviceId);      // 65031 / 256 — TuneIn
    TEST_ASSERT_TRUE(a[1].token.empty());
}

void test_account_uid_and_scoped_household(void) {
    uint32_t uid = 0;
    TEST_ASSERT_TRUE(accountUid("SA_RINCON3079_X_#Svc3079-1a2b3c4d-Token", &uid));
    TEST_ASSERT_EQUAL_HEX32(0x1a2b3c4d, uid);
    TEST_ASSERT_EQUAL_STRING("Sonos_HH_1a2b3c4d",
        scopedHouseholdId("Sonos_HH", "SA_RINCON3079_X_#Svc3079-1a2b3c4d-Token").c_str());

    // Zero-padded to 8 hex digits.
    TEST_ASSERT_EQUAL_STRING("HH_0000abcd",
        scopedHouseholdId("HH", "SA_RINCON52231_X_#Svc52231-abcd-Token").c_str());

    // Anonymous / truncated UDNs carry no uid: bare household id.
    TEST_ASSERT_FALSE(accountUid("SA_RINCON65031_", &uid));
    TEST_ASSERT_EQUAL_STRING("HH", scopedHouseholdId("HH", "SA_RINCON65031_").c_str());
}

// ---------------------------------------------------------------------------
// Service descriptors
// ---------------------------------------------------------------------------

static const char* SERVICES =
    "<Services SchemaVersion=\"1\">"
    "<Service Capabilities=\"30476819\" Id=\"12\" MaxMessagingChars=\"0\" Name=\"Spotify\" "
    "SecureUri=\"https://spotify-v5.ws.sonos.com/smapi\" Uri=\"https://spotify-v5.ws.sonos.com/smapi\" "
    "Version=\"1.1\" ContainerType=\"MService\">"
    "<Policy Auth=\"AppLink\" PollInterval=\"30\"/>"
    "<Presentation><PresentationMap Uri=\"https://x/pmap.xml\" Version=\"1\"/></Presentation>"
    "</Service>"
    "<Service Id=\"333\" Name=\"Plain\" Uri=\"http://insecure.example/smapi\" Capabilities=\"0\">"
    "<Policy Auth=\"Anonymous\"/><Manifest Uri=\"https://m/manifest.json\" Version=\"3\"/></Service>"
    "</Services>";

void test_parse_services(void) {
    std::vector<Service> s = parseServices(SERVICES);
    TEST_ASSERT_EQUAL_INT(2, (int)s.size());
    TEST_ASSERT_EQUAL_INT(12, s[0].id);
    TEST_ASSERT_EQUAL_STRING("Spotify", s[0].name.c_str());
    TEST_ASSERT_EQUAL_STRING("AppLink", s[0].auth.c_str());
    TEST_ASSERT_EQUAL_UINT32(30476819u, s[0].caps);
    TEST_ASSERT_EQUAL_STRING("https://spotify-v5.ws.sonos.com/smapi", s[0].endpoint().c_str());

    TEST_ASSERT_EQUAL_STRING("https://x/pmap.xml", s[0].pmapUri.c_str());
    TEST_ASSERT_EQUAL_STRING("", s[0].manifestUri.c_str());

    TEST_ASSERT_EQUAL_STRING("Anonymous", s[1].auth.c_str());
    TEST_ASSERT_EQUAL_STRING("https://m/manifest.json", s[1].manifestUri.c_str());
    // Child lookups stop at the service's own end: Plain's pmap is not Spotify's.
    TEST_ASSERT_EQUAL_STRING("", s[1].pmapUri.c_str());
    // Credentials never go to a plain-http endpoint.
    TEST_ASSERT_EQUAL_STRING("", s[1].endpoint().c_str());
}

// ---------------------------------------------------------------------------
// Envelope
// ---------------------------------------------------------------------------

static Credentials spotifyCreds(void) {
    Credentials c;
    c.caps = 30476819u;   // Spotify: context bit set, no Bearer, no zonePlayerId
    c.auth = "AppLink";
    c.deviceId = "00-0E-58-AA-BB-CC:1";
    c.zonePlayerId = "RINCON_000E58AABBCC01400";
    c.householdId = "Sonos_HH_1a2b3c4d";
    c.token = "TOK";
    c.key = "KEY";
    c.timeZone = "Europe/Paris";
    return c;
}

void test_envelope_token_in_soap(void) {
    Fields f = {{"id", "root"}, {"index", "0"}, {"count", "50"}};
    std::string e = buildEnvelope("getMetadata", f, spotifyCreds());
    TEST_ASSERT_TRUE(e.find("<ns:token>TOK</ns:token><ns:key>KEY</ns:key>"
                            "<ns:householdId>Sonos_HH_1a2b3c4d</ns:householdId>") != std::string::npos);
    TEST_ASSERT_TRUE(e.find("<ns:timeZone>Europe/Paris</ns:timeZone>") != std::string::npos);
    TEST_ASSERT_TRUE(e.find("<ns:getMetadata><ns:id>root</ns:id>") != std::string::npos);
    TEST_ASSERT_TRUE(e.find("zonePlayerId") == std::string::npos);   // bit 18 not set
}

void test_envelope_bearer_service_keeps_token_out_of_soap(void) {
    Credentials c = spotifyCreds();
    c.caps = CAP_BEARER_TOKEN | CAP_ZONE_PLAYER;
    std::string e = buildEnvelope("getMetadata", Fields(), c);
    TEST_ASSERT_TRUE(e.find("<ns:token>") == std::string::npos);
    TEST_ASSERT_TRUE(e.find("<ns:householdId>") != std::string::npos);
    TEST_ASSERT_TRUE(e.find("<ns:zonePlayerId>RINCON_000E58AABBCC01400</ns:zonePlayerId>")
                     != std::string::npos);
    TEST_ASSERT_TRUE(e.find("<ns:context>") == std::string::npos);

    // ...except for refreshAuthToken, which always sends the old pair in SOAP.
    std::string r = buildEnvelope("refreshAuthToken", Fields(), c, true);
    TEST_ASSERT_TRUE(r.find("<ns:token>TOK</ns:token><ns:key>KEY</ns:key>") != std::string::npos);
}

void test_envelope_anonymous_and_userid(void) {
    Credentials c = spotifyCreds();
    c.auth = "Anonymous";
    std::string e = buildEnvelope("getMetadata", Fields(), c);
    TEST_ASSERT_TRUE(e.find("loginToken") == std::string::npos);
    TEST_ASSERT_TRUE(e.find("<ns:deviceProvider>Sonos</ns:deviceProvider>") != std::string::npos);

    c.auth = "UserId";
    c.username = "me&you";
    c.password = "pw";
    e = buildEnvelope("getMetadata", Fields(), c);
    TEST_ASSERT_TRUE(e.find("<ns:login><ns:username>me&amp;you</ns:username>") != std::string::npos);
    TEST_ASSERT_TRUE(e.find("loginToken") == std::string::npos);
}

void test_envelope_escapes_field_values(void) {
    Fields f = {{"id", "a&b<c>"}};
    std::string e = buildEnvelope("getMetadata", f, spotifyCreds());
    TEST_ASSERT_TRUE(e.find("<ns:id>a&amp;b&lt;c&gt;</ns:id>") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Faults
// ---------------------------------------------------------------------------

void test_fault_with_refreshed_credentials(void) {
    std::string x =
        "<soap:Envelope xmlns:soap=\"http://schemas.xmlsoap.org/soap/envelope/\"><soap:Body>"
        "<soap:Fault><faultcode>Client.TokenRefreshRequired</faultcode>"
        "<faultstring>tokenRefreshRequired</faultstring>"
        "<detail><ms:RefreshAuthTokenResult xmlns:ms=\"http://www.sonos.com/Services/1.1\">"
        "<ms:authToken>NEWTOK</ms:authToken><ms:privateKey>NEWKEY</ms:privateKey>"
        "</ms:RefreshAuthTokenResult></detail></soap:Fault></soap:Body></soap:Envelope>";
    Fault f;
    TEST_ASSERT_TRUE(parseFault(x, &f));
    TEST_ASSERT_EQUAL_STRING("Client.TokenRefreshRequired", f.code.c_str());
    TEST_ASSERT_EQUAL_STRING("NEWTOK", f.newToken.c_str());
    TEST_ASSERT_EQUAL_STRING("NEWKEY", f.newKey.c_str());
    TEST_ASSERT_TRUE(isExpiredFault(f, 500));
}

void test_fault_classification(void) {
    Fault f;
    f.code = "Client.AuthTokenExpired";
    TEST_ASSERT_TRUE(isExpiredFault(f, 500));
    f.code = "Client.ItemNotFound";
    TEST_ASSERT_FALSE(isExpiredFault(f, 500));
    TEST_ASSERT_TRUE(isExpiredFault(f, 401));
    TEST_ASSERT_FALSE(isTransient(f, 500));
    TEST_ASSERT_TRUE(isTransient(f, 503));
    f.retryable = true;   // Apple's SonosError 999
    TEST_ASSERT_TRUE(isTransient(f, 500));

    Fault none;
    TEST_ASSERT_FALSE(parseFault("<s:Envelope><s:Body><ok/></s:Body></s:Envelope>", &none));
}

void test_fault_sonos_error_999(void) {
    std::string x = "<s:Fault><faultcode>s:Server</faultcode><faultstring>err</faultstring>"
                    "<detail><SonosError>999</SonosError></detail></s:Fault>";
    Fault f;
    TEST_ASSERT_TRUE(parseFault(x, &f));
    TEST_ASSERT_TRUE(f.retryable);
}

// ---------------------------------------------------------------------------
// getMetadata
// ---------------------------------------------------------------------------

static const char* METADATA =
    "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body>"
    "<getMetadataResponse xmlns=\"http://www.sonos.com/Services/1.1\"><getMetadataResult>"
    "<index>0</index><count>3</count><total>120</total>"
    "<mediaCollection><id>spotify:album:4aawyAB9vmqN3uQ7FjRGTy</id><itemType>album</itemType>"
    "<title>Random Access Memories</title><artist>Daft Punk</artist>"
    "<albumArtURI>https://i.scdn.co/image/abc</albumArtURI><canPlay>true</canPlay></mediaCollection>"
    "<mediaCollection><id>spotify:artist:4tZwfgrHOc3mvqYlEYSvVi</id><itemType>artist</itemType>"
    "<title>R&amp;B Legends</title><canPlay>false</canPlay></mediaCollection>"
    "<mediaMetadata><id>spotify:track:2Foc5Q5nqNiosCNqttzHof</id><itemType>track</itemType>"
    "<title>Get Lucky</title><mimeType>audio/x-spotify</mimeType>"
    "<trackMetadata><artist>Daft Punk</artist><album>RAM</album><duration>369</duration>"
    "<canPlay>true</canPlay></trackMetadata></mediaMetadata>"
    "</getMetadataResult></getMetadataResponse></s:Body></s:Envelope>";

void test_parse_metadata(void) {
    Page p;
    TEST_ASSERT_TRUE(parseMetadataResult(METADATA, "getMetadataResult", &p));
    TEST_ASSERT_EQUAL_INT(0, p.index);
    TEST_ASSERT_EQUAL_INT(3, p.count);
    TEST_ASSERT_EQUAL_INT(120, p.total);
    TEST_ASSERT_EQUAL_INT(3, (int)p.items.size());

    const Item& album = p.items[0];
    TEST_ASSERT_TRUE(album.collection);
    TEST_ASSERT_TRUE(album.canPlay);
    TEST_ASSERT_TRUE(album.canEnumerate);   // default for a collection
    TEST_ASSERT_EQUAL_STRING("album", album.itemType.c_str());
    TEST_ASSERT_EQUAL_STRING("Daft Punk", album.subtitle.c_str());
    TEST_ASSERT_EQUAL_STRING("https://i.scdn.co/image/abc", album.albumArt.c_str());

    TEST_ASSERT_EQUAL_STRING("R&B Legends", p.items[1].title.c_str());
    TEST_ASSERT_FALSE(p.items[1].canPlay);

    const Item& track = p.items[2];
    TEST_ASSERT_FALSE(track.collection);
    TEST_ASSERT_TRUE(track.canPlay);
    TEST_ASSERT_FALSE(track.canEnumerate);  // default for a leaf
    TEST_ASSERT_EQUAL_STRING("audio/x-spotify", track.mimeType.c_str());
    TEST_ASSERT_EQUAL_STRING("Daft Punk", track.subtitle.c_str());
}

void test_parse_metadata_prefixed(void) {
    std::string x =
        "<soap:Envelope><soap:Body><ns2:getMetadataResponse><ns2:getMetadataResult>"
        "<ns2:index>50</ns2:index><ns2:count>1</ns2:count><ns2:total>51</ns2:total>"
        "<ns2:mediaMetadata><ns2:id>s123</ns2:id><ns2:itemType>stream</ns2:itemType>"
        "<ns2:title>Radio</ns2:title><ns2:mimeType>audio/aac</ns2:mimeType></ns2:mediaMetadata>"
        "</ns2:getMetadataResult></ns2:getMetadataResponse></soap:Body></soap:Envelope>";
    Page p;
    TEST_ASSERT_TRUE(parseMetadataResult(x, "getMetadataResult", &p));
    TEST_ASSERT_EQUAL_INT(50, p.index);
    TEST_ASSERT_EQUAL_INT(51, p.total);
    TEST_ASSERT_EQUAL_INT(1, (int)p.items.size());
    TEST_ASSERT_EQUAL_STRING("stream", p.items[0].itemType.c_str());
}

void test_parse_metadata_empty_and_missing(void) {
    Page p;
    TEST_ASSERT_TRUE(parseMetadataResult(
        "<getMetadataResult><index>0</index><count>0</count><total>0</total></getMetadataResult>",
        "getMetadataResult", &p));
    TEST_ASSERT_EQUAL_INT(0, (int)p.items.size());
    Page q;
    TEST_ASSERT_FALSE(parseMetadataResult("<nothing/>", "getMetadataResult", &q));
}

// ---------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------

static const std::string SPOTIFY_UDN = "SA_RINCON3079_X_#Svc3079-1a2b3c4d-Token";

void test_escape_id_and_base_type(void) {
    TEST_ASSERT_EQUAL_STRING("spotify%3aalbum%3aX", escapeId("spotify:album:X").c_str());
    TEST_ASSERT_EQUAL_STRING("a%2fb%3fc%3dd%26e%23f", escapeId("a/b?c=d&e#f").c_str());
    TEST_ASSERT_EQUAL_STRING("program", baseType("trackList.program").c_str());
    TEST_ASSERT_EQUAL_STRING("track", baseType("Track").c_str());
}

void test_play_spotify_album_container(void) {
    Item it;
    it.collection = true; it.canPlay = true;
    it.id = "spotify:album:4aaw"; it.itemType = "album"; it.title = "Tom & Jerry";
    Playback pb;
    TEST_ASSERT_TRUE(buildPlayback(it, 12, 3, SPOTIFY_UDN, &pb));
    TEST_ASSERT_TRUE(pb.container);
    TEST_ASSERT_EQUAL_STRING(
        "x-rincon-cpcontainer:1004206cspotify%3aalbum%3a4aaw?sid=12&flags=8300&sn=3", pb.uri.c_str());
    TEST_ASSERT_TRUE(pb.didl.find("<item id=\"1004206cspotify%3aalbum%3a4aaw\"") != std::string::npos);
    TEST_ASSERT_TRUE(pb.didl.find("object.container.album.musicAlbum") != std::string::npos);
    TEST_ASSERT_TRUE(pb.didl.find("<dc:title>Tom &amp; Jerry</dc:title>") != std::string::npos);
    TEST_ASSERT_TRUE(pb.didl.find(">SA_RINCON3079_X_#Svc3079-1a2b3c4d-Token</desc>") != std::string::npos);
}

void test_play_playlist_container(void) {
    Item it;
    it.collection = true; it.canPlay = true;
    it.id = "libraryplaylist:p.8Wx"; it.itemType = "playlist"; it.title = "Mix";
    Playback pb;
    TEST_ASSERT_TRUE(buildPlayback(it, 204, 4, "SA_RINCON52231_X_#Svc52231-0-Token", &pb));
    TEST_ASSERT_EQUAL_STRING(
        "x-rincon-cpcontainer:1006206clibraryplaylist%3ap.8Wx?sid=204&flags=8300&sn=4", pb.uri.c_str());
    TEST_ASSERT_TRUE(pb.didl.find("object.container.playlistContainer") != std::string::npos);
}

void test_play_spotify_track(void) {
    Item it;
    it.collection = false; it.canPlay = true;
    it.id = "spotify:track:2Foc"; it.itemType = "track"; it.mimeType = "audio/x-spotify";
    it.title = "Get Lucky";
    Playback pb;
    TEST_ASSERT_TRUE(buildPlayback(it, 12, 3, SPOTIFY_UDN, &pb));
    TEST_ASSERT_FALSE(pb.container);
    TEST_ASSERT_EQUAL_STRING("x-sonos-spotify:spotify%3atrack%3a2Foc?sid=12&flags=8224&sn=3",
                             pb.uri.c_str());
    TEST_ASSERT_TRUE(pb.didl.find("<res protocolInfo=\"sonos.com-spotify:*:audio/x-spotify:*\">"
                                  "x-sonos-spotify:spotify%3atrack%3a2Foc?sid=12&amp;flags=8224&amp;sn=3</res>")
                     != std::string::npos);
    TEST_ASSERT_TRUE(pb.didl.find("<item id=\"10030020spotify%3atrack%3a2Foc\"") != std::string::npos);

    // EU accounts are sid 9: same resource, same flags — keyed on the MIME type.
    TEST_ASSERT_TRUE(buildPlayback(it, 9, 16, SPOTIFY_UDN, &pb));
    TEST_ASSERT_EQUAL_STRING("x-sonos-spotify:spotify%3atrack%3a2Foc?sid=9&flags=8224&sn=16",
                             pb.uri.c_str());
}

void test_play_other_service_tracks(void) {
    Item it;
    it.canPlay = true; it.id = "song:1844"; it.itemType = "track"; it.mimeType = "audio/mp4";
    Playback pb;
    TEST_ASSERT_TRUE(buildPlayback(it, 204, 4, "U", &pb));
    TEST_ASSERT_EQUAL_STRING("x-sonos-http:song%3a1844.mp4?sid=204&flags=8224&sn=4", pb.uri.c_str());

    it.mimeType = "audio/flac";
    TEST_ASSERT_TRUE(buildPlayback(it, 174, 1, "U", &pb));
    TEST_ASSERT_EQUAL_STRING("x-sonos-http:song%3a1844.flac?sid=174&flags=0&sn=1", pb.uri.c_str());

    it.mimeType = "";
    TEST_ASSERT_TRUE(buildPlayback(it, 2, 1, "U", &pb));
    TEST_ASSERT_EQUAL_STRING("x-sonos-http:song%3a1844.mp3?sid=2&flags=32&sn=1", pb.uri.c_str());
}

void test_play_streams_and_programs(void) {
    Item it;
    it.canPlay = true; it.id = "s24861"; it.itemType = "stream"; it.mimeType = "audio/aac";
    Playback pb;
    TEST_ASSERT_TRUE(buildPlayback(it, 254, 0, "SA_RINCON65031_", &pb));
    TEST_ASSERT_EQUAL_STRING("x-sonosapi-stream:s24861?sid=254&flags=8224&sn=0", pb.uri.c_str());
    TEST_ASSERT_TRUE(pb.didl.find("object.item.audioItem.audioBroadcast") != std::string::npos);

    it.mimeType = "application/x-mpegURL";
    TEST_ASSERT_TRUE(buildPlayback(it, 254, 0, "U", &pb));
    TEST_ASSERT_EQUAL_STRING("x-sonosapi-hls:s24861?sid=254&flags=288&sn=0", pb.uri.c_str());

    // A programme is played as radio even when it is a collection.
    Item prog;
    prog.collection = true; prog.canPlay = true; prog.id = "P:1"; prog.itemType = "trackList.program";
    TEST_ASSERT_TRUE(buildPlayback(prog, 303, 1, "U", &pb));
    TEST_ASSERT_FALSE(pb.container);
    TEST_ASSERT_EQUAL_STRING("x-sonosapi-radio:P%3a1?sid=303&flags=104&sn=1", pb.uri.c_str());
}

void test_play_refuses_unplayable(void) {
    Item it;
    it.collection = true; it.canPlay = false; it.id = "spotify:artist:1"; it.itemType = "artist";
    Playback pb;
    TEST_ASSERT_FALSE(buildPlayback(it, 12, 3, SPOTIFY_UDN, &pb));
}

// ── Search categories ───────────────────────────────────────────────────────

static void test_search_categories_spotify(void) {
    const char* pmap =
        "<Presentation><PresentationMap type=\"DisplayType\"><DisplayType id=\"x\"/></PresentationMap>"
        "<PresentationMap type=\"Search\"><Match><SearchCategories>"
        "<Category id=\"artists\" mappedId=\"artist\"/>"
        "<Category id=\"albums\" mappedId=\"album\"/>"
        "<Category id=\"all\" mappedId=\"all\"/>"
        "</SearchCategories></Match></PresentationMap></Presentation>";
    auto c = smapi::parseSearchCategories(pmap);
    TEST_ASSERT_EQUAL(3, (int)c.size());
    TEST_ASSERT_EQUAL_STRING("artists", c[0].id.c_str());
    TEST_ASSERT_EQUAL_STRING("artist", c[0].mappedId.c_str());
    TEST_ASSERT_EQUAL_STRING("all", c[2].mappedId.c_str());
}

static void test_search_categories_first_wins_and_custom_skipped(void) {
    // Apple Music: catalogue first, then the library repeats the same ids.
    const char* pmap =
        "<PresentationMap type=\"Search\">\n<Match>\n<SearchCategories>\n"
        "\t<Category id=\"artists\" mappedId=\"artist\"/>\n"
        "\t<CustomCategory stringId=\"radioShows\" mappedId=\"radioshow\"/>\n"
        "</SearchCategories></Match>\n<Match><SearchCategories>"
        "<Category id=\"artists\" mappedId=\"libraryartist\"/>"
        "<Category id=\"stations\" mappedId=\"search:station\"/>"
        "</SearchCategories></Match></PresentationMap>";
    auto c = smapi::parseSearchCategories(pmap);
    TEST_ASSERT_EQUAL(2, (int)c.size());
    TEST_ASSERT_EQUAL_STRING("artist", c[0].mappedId.c_str());
    TEST_ASSERT_EQUAL_STRING("stations", c[1].id.c_str());
    TEST_ASSERT_EQUAL_STRING("search:station", c[1].mappedId.c_str());
}

static void test_search_categories_absent(void) {
    TEST_ASSERT_EQUAL(0, (int)smapi::parseSearchCategories(
        "<PresentationMap type=\"DisplayType\"><Category id=\"a\" mappedId=\"b\"/></PresentationMap>").size());
    TEST_ASSERT_EQUAL(0, (int)smapi::parseSearchCategories("").size());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_unescape_single_level);
    RUN_TEST(test_unescape_numeric_to_utf8);
    RUN_TEST(test_escape_roundtrip);
    RUN_TEST(test_find_element_ignores_prefix);
    RUN_TEST(test_find_element_exact_local_name);
    RUN_TEST(test_find_element_self_closing);
    RUN_TEST(test_attr_requires_word_boundary);
    RUN_TEST(test_parse_accounts);
    RUN_TEST(test_account_uid_and_scoped_household);
    RUN_TEST(test_parse_services);
    RUN_TEST(test_envelope_token_in_soap);
    RUN_TEST(test_envelope_bearer_service_keeps_token_out_of_soap);
    RUN_TEST(test_envelope_anonymous_and_userid);
    RUN_TEST(test_envelope_escapes_field_values);
    RUN_TEST(test_fault_with_refreshed_credentials);
    RUN_TEST(test_fault_classification);
    RUN_TEST(test_fault_sonos_error_999);
    RUN_TEST(test_parse_metadata);
    RUN_TEST(test_parse_metadata_prefixed);
    RUN_TEST(test_parse_metadata_empty_and_missing);
    RUN_TEST(test_escape_id_and_base_type);
    RUN_TEST(test_play_spotify_album_container);
    RUN_TEST(test_play_playlist_container);
    RUN_TEST(test_play_spotify_track);
    RUN_TEST(test_play_other_service_tracks);
    RUN_TEST(test_play_streams_and_programs);
    RUN_TEST(test_play_refuses_unplayable);
    RUN_TEST(test_search_categories_spotify);
    RUN_TEST(test_search_categories_first_wins_and_custom_skipped);
    RUN_TEST(test_search_categories_absent);
    return UNITY_END();
}
