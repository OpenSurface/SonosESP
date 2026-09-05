/**
 * Host tests for include/sonos_topology.h
 *
 *     pio test -e native
 *
 * Every case here is a bug that actually shipped, or the boundary next to one.
 * The v1.15.1 group regression and the satellite miscount were both found by
 * reading code and reproduced in a throwaway Python model; these are that model,
 * made permanent and pointed at the real implementation.
 */

#include <unity.h>
#include <string.h>
#include <string>
#include "sonos_topology.h"

using namespace sonos_topology;

static const char* LIVING = "RINCON_C43875F1193A01400";
static const char* KITCHEN = "RINCON_B8E93700001400";
static const char* LOUNGE = "RINCON_B8E93700011400";

// ---------------------------------------------------------------------------
// uuidEquals — the v1.15.1 regression
// ---------------------------------------------------------------------------

void test_uuid_same_case(void) {
    TEST_ASSERT_TRUE(uuidEquals(LIVING, LIVING));
}

// The exact shape of the shipped bug: the topology parser lowercased its whole
// response, so the id it stored never matched the uppercase rinconID from the
// device description, and every membership test silently answered "no".
void test_uuid_differing_case(void) {
    std::string lower(LIVING);
    for (auto& c : lower) if (c >= 'A' && c <= 'Z') c += 32;
    TEST_ASSERT_FALSE(strcmp(lower.c_str(), LIVING) == 0);   // == would fail
    TEST_ASSERT_TRUE(uuidEquals(lower.c_str(), LIVING));     // uuidEquals holds
}

void test_uuid_different_ids(void) {
    TEST_ASSERT_FALSE(uuidEquals(KITCHEN, LOUNGE));
}

// A prefix must not compare equal to the longer id it is a prefix of.
void test_uuid_prefix_is_not_equal(void) {
    TEST_ASSERT_FALSE(uuidEquals("RINCON_B8E9370000", KITCHEN));
}

void test_uuid_empty_and_null(void) {
    TEST_ASSERT_TRUE(uuidEquals("", ""));
    TEST_ASSERT_FALSE(uuidEquals(KITCHEN, ""));
    TEST_ASSERT_FALSE(uuidEquals(nullptr, KITCHEN));
    TEST_ASSERT_FALSE(uuidEquals(KITCHEN, nullptr));
}

// ---------------------------------------------------------------------------
// decodeInPlace
// ---------------------------------------------------------------------------

static std::string decoded(const std::string& in) {
    std::string buf(in);
    size_t n = decodeInPlace(&buf[0], buf.size());
    buf.resize(n);
    return buf;
}

void test_decode_entities_and_lowercase(void) {
    TEST_ASSERT_EQUAL_STRING("<zonegroup id=\"a\">",
                             decoded("&lt;ZoneGroup ID=&quot;A&quot;&gt;").c_str());
}

// &amp; must resolve last. "&amp;lt;" is a literal "&lt;" in the document and
// must NOT become "<": that would fabricate a tag the payload never contained.
void test_decode_ampersand_does_not_double_decode(void) {
    TEST_ASSERT_EQUAL_STRING("&lt;", decoded("&amp;lt;").c_str());
}

void test_decode_leaves_plain_text(void) {
    TEST_ASSERT_EQUAL_STRING("kitchen & lounge", decoded("Kitchen &amp; Lounge").c_str());
}

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

static std::string member(const char* uuid, const char* zone, int satellites = 0) {
    std::string s = std::string("<ZoneGroupMember UUID=\"") + uuid +
                    "\" ZoneName=\"" + zone +
                    "\" Location=\"http://192.168.1.9:1400/xml/device_description.xml\""
                    " Configuration=\"1\" SoftwareVersion=\"83.1-61123\" WifiEnabled=\"1\"";
    if (satellites == 0) return s + "/>";
    s += ">";
    for (int i = 0; i < satellites; i++) {
        s += std::string("<Satellite UUID=\"") + uuid + ":SAT" + std::to_string(i) +
             "\" ZoneName=\"" + zone + " (sat)\" Configuration=\"1\"/>";
    }
    return s + "</ZoneGroupMember>";
}

static std::string group(const char* coord, const std::string& membersXml) {
    return std::string("<ZoneGroup Coordinator=\"") + coord + "\" ID=\"" + coord + ":1\">" +
           membersXml + "</ZoneGroup>";
}

static std::string wrap(const std::string& groupsXml) {
    return "<ZoneGroupState><ZoneGroups>" + groupsXml + "</ZoneGroups></ZoneGroupState>";
}

struct Parsed {
    std::string buf;
    Group groups[16];
    Slice members[64];
    int count = 0;
};

static void parse(Parsed& p, const std::string& xml) {
    p.buf = xml;
    size_t n = decodeInPlace(&p.buf[0], p.buf.size());
    p.buf.resize(n);
    p.count = parseZoneGroups(p.buf.c_str(), p.buf.size(),
                              p.groups, 16, p.members, 64);
}

static bool memberIs(const Parsed& p, int idx, const char* uuid) {
    return uuidEquals(p.members[idx].ptr, p.members[idx].len, uuid, cstrLen(uuid));
}

static bool coordIs(const Parsed& p, int g, const char* uuid) {
    return uuidEquals(p.groups[g].coordinator.ptr, p.groups[g].coordinator.len,
                      uuid, cstrLen(uuid));
}

// ---------------------------------------------------------------------------
// parseZoneGroups
// ---------------------------------------------------------------------------

void test_single_standalone_speaker(void) {
    Parsed p;
    parse(p, wrap(group(LIVING, member(LIVING, "Living Room"))));
    TEST_ASSERT_EQUAL_INT(1, p.count);
    TEST_ASSERT_EQUAL_INT(1, p.groups[0].memberCount);
    TEST_ASSERT_TRUE(coordIs(p, 0, LIVING));
    TEST_ASSERT_TRUE(memberIs(p, 0, LIVING));
}

// The reported topology: everything in one group. Before the fix the Groups
// screen showed this as "12 speakers, 1 group" with the row reading Standalone.
void test_twelve_speakers_in_one_group(void) {
    std::string members;
    char uuid[64];
    for (int i = 0; i < 12; i++) {
        snprintf(uuid, sizeof(uuid), "RINCON_B8E937%02d01400", i);
        members += member(uuid, "Room");
    }
    Parsed p;
    parse(p, wrap(group("RINCON_B8E9370001400", members)));

    TEST_ASSERT_EQUAL_INT(1, p.count);
    TEST_ASSERT_EQUAL_INT(12, p.groups[0].memberCount);
    TEST_ASSERT_TRUE(memberIs(p, 0, "RINCON_B8E9370001400"));
    TEST_ASSERT_TRUE(memberIs(p, 11, "RINCON_B8E9371101400"));
}

void test_mixed_grouped_and_standalone(void) {
    std::string xml = group(KITCHEN, member(KITCHEN, "Kitchen") + member(LOUNGE, "Lounge")) +
                      group(LIVING, member(LIVING, "Living Room"));
    Parsed p;
    parse(p, wrap(xml));

    TEST_ASSERT_EQUAL_INT(2, p.count);
    TEST_ASSERT_EQUAL_INT(2, p.groups[0].memberCount);
    TEST_ASSERT_TRUE(coordIs(p, 0, KITCHEN));
    TEST_ASSERT_EQUAL_INT(1, p.groups[1].memberCount);
    TEST_ASSERT_TRUE(coordIs(p, 1, LIVING));

    // Members of group 1 start after group 0's, not at zero.
    TEST_ASSERT_EQUAL_INT(2, p.groups[1].firstMember);
    TEST_ASSERT_TRUE(memberIs(p, p.groups[1].firstMember, LIVING));
}

// A standalone home theatre: one ZoneGroupMember with nested <Satellite> children
// for its sub and surrounds. Counting every uuid=" made this a 4-speaker group,
// which put a standalone speaker onto the group volume path and printed
// "4 speakers in group" on screen.
void test_satellites_are_not_members(void) {
    Parsed p;
    parse(p, wrap(group(LIVING, member(LIVING, "Beam", /*satellites=*/3))));
    TEST_ASSERT_EQUAL_INT(1, p.count);
    TEST_ASSERT_EQUAL_INT(1, p.groups[0].memberCount);
    TEST_ASSERT_TRUE(memberIs(p, 0, LIVING));
}

// The coordinator is identified by id, never by position, so a group whose
// coordinator is not its first member still resolves correctly.
void test_coordinator_need_not_be_first_member(void) {
    Parsed p;
    parse(p, wrap(group(LOUNGE, member(KITCHEN, "Kitchen") + member(LOUNGE, "Lounge"))));
    TEST_ASSERT_EQUAL_INT(2, p.groups[0].memberCount);
    TEST_ASSERT_TRUE(coordIs(p, 0, LOUNGE));
    TEST_ASSERT_TRUE(memberIs(p, 0, KITCHEN));
    TEST_ASSERT_TRUE(memberIs(p, 1, LOUNGE));
}

// <ZoneGroups> (plural) must not be mistaken for a <ZoneGroup> - the open tag is
// matched with its trailing space and the close tag with its '>'.
void test_zonegroups_wrapper_is_not_a_group(void) {
    Parsed p;
    parse(p, wrap(""));
    TEST_ASSERT_EQUAL_INT(0, p.count);
}

// A response cut short mid-group must yield the groups that did close, not read
// past the buffer.
void test_truncated_response_is_safe(void) {
    std::string xml = wrap(group(KITCHEN, member(KITCHEN, "Kitchen")) +
                           group(LIVING, member(LIVING, "Living Room")));
    xml = xml.substr(0, xml.size() - 40);          // chop the tail
    Parsed p;
    parse(p, xml);
    TEST_ASSERT_EQUAL_INT(1, p.count);
    TEST_ASSERT_TRUE(coordIs(p, 0, KITCHEN));
}

void test_empty_input_is_safe(void) {
    Group g[2];
    Slice m[2];
    TEST_ASSERT_EQUAL_INT(0, parseZoneGroups("", 0, g, 2, m, 2));
    TEST_ASSERT_EQUAL_INT(0, parseZoneGroups(nullptr, 0, g, 2, m, 2));
}

// Caller buffers are honoured: a topology larger than the arrays must stop, not
// overrun. MAX_SONOS_DEVICES is 32, so a 40-speaker household is reachable.
void test_respects_buffer_limits(void) {
    std::string members;
    char uuid[64];
    for (int i = 0; i < 40; i++) {
        snprintf(uuid, sizeof(uuid), "RINCON_AAAA%02d01400", i);
        members += member(uuid, "Room");
    }
    std::string buf = wrap(group("RINCON_AAAA0001400", members));
    size_t n = decodeInPlace(&buf[0], buf.size());

    Group g[2];
    Slice m[8];
    int count = parseZoneGroups(buf.c_str(), n, g, 2, m, 8);
    TEST_ASSERT_EQUAL_INT(1, count);
    TEST_ASSERT_TRUE(g[0].memberCount <= 8);
}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_uuid_same_case);
    RUN_TEST(test_uuid_differing_case);
    RUN_TEST(test_uuid_different_ids);
    RUN_TEST(test_uuid_prefix_is_not_equal);
    RUN_TEST(test_uuid_empty_and_null);

    RUN_TEST(test_decode_entities_and_lowercase);
    RUN_TEST(test_decode_ampersand_does_not_double_decode);
    RUN_TEST(test_decode_leaves_plain_text);

    RUN_TEST(test_single_standalone_speaker);
    RUN_TEST(test_twelve_speakers_in_one_group);
    RUN_TEST(test_mixed_grouped_and_standalone);
    RUN_TEST(test_satellites_are_not_members);
    RUN_TEST(test_coordinator_need_not_be_first_member);
    RUN_TEST(test_zonegroups_wrapper_is_not_a_group);
    RUN_TEST(test_truncated_response_is_safe);
    RUN_TEST(test_empty_input_is_safe);
    RUN_TEST(test_respects_buffer_limits);

    return UNITY_END();
}
