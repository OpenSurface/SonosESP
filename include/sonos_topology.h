#pragma once
/**
 * Sonos ZoneGroupState parsing — pure, framework-free, allocation-free.
 *
 * Split out of sonos_controller.cpp so it can be compiled and tested on the host.
 * Nothing here touches Arduino, FreeRTOS, LVGL or the network: it is string
 * arithmetic over a buffer, which is exactly the part that shipped bugs.
 *
 * Two of the three defects in the v1.15.1/v1.15.2 group work lived in this logic:
 *   - UUIDs stored in the case the payload happened to use, then compared with ==
 *   - member counting that walked every uuid=" and so counted <Satellite> children
 * Both are now covered by test/test_topology.
 *
 * Caller supplies the buffers; this code never allocates.
 */

#include <stddef.h>
#include <stdint.h>

namespace sonos_topology {

// ---------------------------------------------------------------------------
// UUID comparison
// ---------------------------------------------------------------------------
// RINCON ids reach us in inconsistent case: the device description spells them
// uppercase, GetZoneGroupState is lowercased wholesale before parsing (firmware
// varies), and the x-rincon: transport URI carries whatever the speaker sent.
// ALWAYS compare through this, never with ==. Ids are [A-Za-z0-9_:] by
// construction, so plain ASCII folding is correct and locale-free.
inline bool uuidEquals(const char* a, size_t na, const char* b, size_t nb) {
    if (na != nb) return false;
    for (size_t i = 0; i < na; i++) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return false;
    }
    return true;
}

inline size_t cstrLen(const char* s) {
    size_t n = 0;
    while (s && s[n]) n++;
    return n;
}

inline bool uuidEquals(const char* a, const char* b) {
    if (!a || !b) return false;
    return uuidEquals(a, cstrLen(a), b, cstrLen(b));
}

// ---------------------------------------------------------------------------
// Parsed shape
// ---------------------------------------------------------------------------
// Slices point INTO the caller's buffer; they are not null-terminated and do not
// outlive it.
struct Slice {
    const char* ptr;
    uint16_t    len;
};

struct Group {
    Slice coordinator;
    uint16_t firstMember;   // index into the members array
    uint16_t memberCount;
};

// ---------------------------------------------------------------------------
// parseZoneGroups
// ---------------------------------------------------------------------------
// `buf` must already be entity-decoded and ASCII-lowercased (see decodeInPlace).
//
// Members are the <ZoneGroupMember> elements only. A home-theatre member carries
// nested <Satellite UUID="..."/> children for its sub and surrounds; scanning for
// uuid=" instead counted those, which made a STANDALONE Beam + Sub + 2 surrounds
// report as a four-speaker group. Each element's attributes are read up to its
// first '>', so satellites are never mistaken for members.
//
// Returns the number of groups written (never more than maxGroups).
inline int parseZoneGroups(const char* buf, size_t len,
                           Group* groups, int maxGroups,
                           Slice* members, int maxMembers) {
    if (!buf || !groups || !members || maxGroups <= 0 || maxMembers <= 0) return 0;

    const char kGroupOpen[]  = "<zonegroup ";
    const char kGroupClose[] = "</zonegroup>";
    const char kMemberOpen[] = "<zonegroupmember ";
    const char kCoordAttr[]  = "coordinator=\"";
    const char kUuidAttr[]   = "uuid=\"";

    auto find = [&](size_t from, const char* needle, size_t nlen) -> size_t {
        if (nlen == 0 || from >= len || len - from < nlen) return (size_t)-1;
        for (size_t i = from; i + nlen <= len; i++) {
            size_t j = 0;
            while (j < nlen && buf[i + j] == needle[j]) j++;
            if (j == nlen) return i;
        }
        return (size_t)-1;
    };

    int groupCount = 0;
    int memberTotal = 0;
    size_t scan = 0;

    while (groupCount < maxGroups) {
        size_t gs = find(scan, kGroupOpen, sizeof(kGroupOpen) - 1);
        if (gs == (size_t)-1) break;
        size_t ge = find(gs, kGroupClose, sizeof(kGroupClose) - 1);
        if (ge == (size_t)-1) break;                 // truncated response
        scan = ge + sizeof(kGroupClose) - 1;

        // Coordinator attribute on the <ZoneGroup> tag.
        size_t cs = find(gs, kCoordAttr, sizeof(kCoordAttr) - 1);
        if (cs == (size_t)-1 || cs > ge) continue;
        cs += sizeof(kCoordAttr) - 1;
        size_t ce = cs;
        while (ce < ge && buf[ce] != '"') ce++;
        if (ce <= cs) continue;

        Group g;
        g.coordinator.ptr = buf + cs;
        g.coordinator.len = (uint16_t)(ce - cs);
        g.firstMember     = (uint16_t)memberTotal;
        g.memberCount     = 0;

        // <ZoneGroupMember> elements within this group only.
        size_t ms = gs;
        while (memberTotal < maxMembers) {
            size_t mstart = find(ms, kMemberOpen, sizeof(kMemberOpen) - 1);
            if (mstart == (size_t)-1 || mstart > ge) break;
            size_t mend = mstart;
            while (mend < ge && buf[mend] != '>') mend++;   // attributes end here
            ms = mend;

            size_t us = find(mstart, kUuidAttr, sizeof(kUuidAttr) - 1);
            if (us == (size_t)-1 || us > mend) continue;
            us += sizeof(kUuidAttr) - 1;
            size_t ue = us;
            while (ue < mend && buf[ue] != '"') ue++;
            if (ue <= us) continue;

            members[memberTotal].ptr = buf + us;
            members[memberTotal].len = (uint16_t)(ue - us);
            memberTotal++;
            g.memberCount++;
        }

        groups[groupCount++] = g;
    }
    return groupCount;
}

// ---------------------------------------------------------------------------
// decodeInPlace
// ---------------------------------------------------------------------------
// The ZoneGroupState payload arrives entity-encoded inside the SOAP body.
// Decodes &lt; &gt; &quot; &amp; and ASCII-lowercases, in one left-to-right pass
// over the buffer. Returns the new length.
//
// One pass, not four String::replace() calls: &amp; MUST resolve last or "&amp;lt;"
// double-decodes into a tag that was never in the document. Scanning once removes
// the ordering hazard entirely rather than relying on the calls staying in order.
inline size_t decodeInPlace(char* buf, size_t len) {
    if (!buf) return 0;
    size_t w = 0;
    for (size_t r = 0; r < len; ) {
        if (buf[r] == '&') {
            size_t remain = len - r;
            if (remain >= 4 && buf[r+1] == 'l' && buf[r+2] == 't' && buf[r+3] == ';') {
                buf[w++] = '<'; r += 4; continue;
            }
            if (remain >= 4 && buf[r+1] == 'g' && buf[r+2] == 't' && buf[r+3] == ';') {
                buf[w++] = '>'; r += 4; continue;
            }
            if (remain >= 6 && buf[r+1] == 'q' && buf[r+2] == 'u' &&
                buf[r+3] == 'o' && buf[r+4] == 't' && buf[r+5] == ';') {
                buf[w++] = '"'; r += 6; continue;
            }
            if (remain >= 5 && buf[r+1] == 'a' && buf[r+2] == 'm' &&
                buf[r+3] == 'p' && buf[r+4] == ';') {
                buf[w++] = '&'; r += 5; continue;
            }
        }
        char c = buf[r++];
        if (c >= 'A' && c <= 'Z') c += 32;
        buf[w++] = c;
    }
    return w;
}

}  // namespace sonos_topology
