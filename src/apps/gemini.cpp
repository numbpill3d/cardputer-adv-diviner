// GEMINI — a gemtext browser for the small web. Fetches gemini:// capsules
// over TLS, renders headings in Daedric, numbers the links so you jump with a
// keypress, scrolls, keeps history and bookmarks, and answers input prompts
// (status 1x) so you can post to Station, guestbooks and gemlogs.
//
// keys  ;/.  scroll   1-9/0 follow link   enter open bar / submit input
//       u up (back)   b bookmarks   g go (type a url)   ` back/leave
#include "../net.h"
#include "../rite.h"
#include "../textedit.h"
#include "../theme.h"
#include <math.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <vector>

using namespace theme;

namespace {
enum Line { L_TEXT, L_H1, L_H2, L_H3, L_LINK, L_QUOTE, L_LIST, L_PRE };
struct GLine {
    uint8_t kind;
    int linkNo;       // 1.. for L_LINK, else 0
    std::string text; // display text
    std::string url;  // for links
};
enum Mode { M_BROWSE, M_URLBAR, M_INPUT, M_BOOKMARKS };

struct State {
    std::vector<GLine> lines;
    std::vector<std::string> links; // resolved URLs, indexed by linkNo-1
    std::vector<std::string> history;
    std::vector<std::string> marks;
    int scroll = 0, markSel = 0;
    Mode mode = M_BROWSE;
    TextEdit edit;
    std::string pendingBase; // base URL for resolving links / input target
    bool loadedOnce = false;
    uint32_t spin = 0;
} *S = nullptr;

std::string resolve(const std::string &base, const std::string &href) {
    if (href.find("://") != std::string::npos) return href;
    if (href.rfind("gemini://", 0) == 0) return href;
    // base = gemini://host/path...
    size_t h = base.find("://");
    if (h == std::string::npos) return "gemini://" + href;
    size_t pathStart = base.find('/', h + 3);
    std::string host = pathStart == std::string::npos ? base : base.substr(0, pathStart);
    if (href.size() && href[0] == '/') return host + href;
    // relative to current dir
    std::string dir = base;
    size_t lastSlash = dir.rfind('/');
    if (lastSlash > h + 2) dir = dir.substr(0, lastSlash + 1);
    else dir = host + "/";
    return dir + href;
}

void parse(const std::string &body, const std::string &base) {
    State &s = *S;
    s.lines.clear();
    s.links.clear();
    s.scroll = 0;
    bool pre = false;
    size_t i = 0;
    while (i < body.size()) {
        size_t nl = body.find('\n', i);
        std::string raw = body.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
        i = nl == std::string::npos ? body.size() : nl + 1;
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        if (raw.rfind("```", 0) == 0) { pre = !pre; continue; }
        GLine g;
        g.linkNo = 0;
        if (pre) { g.kind = L_PRE; g.text = raw; }
        else if (raw.rfind("=>", 0) == 0) {
            std::string r = raw.substr(2);
            size_t a = r.find_first_not_of(" \t");
            if (a == std::string::npos) continue;
            r = r.substr(a);
            size_t sp = r.find_first_of(" \t");
            std::string href = sp == std::string::npos ? r : r.substr(0, sp);
            std::string label = sp == std::string::npos ? r : r.substr(r.find_first_not_of(" \t", sp));
            g.kind = L_LINK;
            g.url = resolve(base, href);
            s.links.push_back(g.url);
            g.linkNo = s.links.size();
            g.text = label.empty() ? href : label;
        } else if (raw.rfind("###", 0) == 0) { g.kind = L_H3; g.text = raw.substr(3); }
        else if (raw.rfind("##", 0) == 0) { g.kind = L_H2; g.text = raw.substr(2); }
        else if (raw.rfind("#", 0) == 0) { g.kind = L_H1; g.text = raw.substr(1); }
        else if (raw.rfind(">", 0) == 0) { g.kind = L_QUOTE; g.text = raw.substr(1); }
        else if (raw.rfind("* ", 0) == 0) { g.kind = L_LIST; g.text = raw.substr(2); }
        else { g.kind = L_TEXT; g.text = raw; }
        // trim one leading space on headers/quotes
        if (g.kind != L_PRE && !g.text.empty() && g.text[0] == ' ') g.text.erase(0, 1);
        s.lines.push_back(g);
    }
}

void go(const char *url, bool pushHistory = true) {
    State &s = *S;
    if (pushHistory && !s.pendingBase.empty()) s.history.push_back(s.pendingBase);
    s.pendingBase = url;
    net::gemGet(url);
}

void enter() {
    if (!S) S = new State();
    net::wifiBegin();
    if (!S->loadedOnce) {
        S->marks.push_back("gemini://geminiprotocol.net/");
        S->marks.push_back("gemini://warmedal.se/~antenna/");
        S->marks.push_back("gemini://station.martinrue.com/");
        go("gemini://geminiprotocol.net/");
        S->loadedOnce = true;
    }
}
void leave() { net::gemStop(); }

// wrap a string to width w (chars), appending wrapped pieces as display rows
int visibleRows(int &outFirst);

bool onKey(const hal::Key &k) {
    State &s = *S;
    if (s.mode == M_URLBAR) {
        if (k.nav == hal::N_ESC) { s.mode = M_BROWSE; return true; }
        if (k.nav == hal::N_ENTER) { go(s.edit.buf); s.mode = M_BROWSE; return true; }
        s.edit.key(k);
        return true;
    }
    if (s.mode == M_INPUT) {
        if (k.nav == hal::N_ESC) { s.mode = M_BROWSE; return true; }
        if (k.nav == hal::N_ENTER) {
            // submit: base?percent-encoded
            std::string u = s.pendingBase;
            u += "?";
            for (char c : std::string(s.edit.buf)) {
                if (isalnum((unsigned char)c)) u += c;
                else { char e[4]; snprintf(e, sizeof e, "%%%02X", (unsigned char)c); u += e; }
            }
            go(u.c_str(), false);
            s.mode = M_BROWSE;
            return true;
        }
        s.edit.key(k);
        return true;
    }
    if (s.mode == M_BOOKMARKS) {
        if (k.nav == hal::N_ESC) { s.mode = M_BROWSE; return true; }
        if (k.nav == hal::N_UP) s.markSel = (s.markSel + s.marks.size() - 1) % s.marks.size();
        else if (k.nav == hal::N_DOWN) s.markSel = (s.markSel + 1) % s.marks.size();
        else if (k.nav == hal::N_ENTER && !s.marks.empty()) { go(s.marks[s.markSel].c_str()); s.mode = M_BROWSE; }
        return true;
    }
    // M_BROWSE
    if (k.nav == hal::N_UP) { if (s.scroll > 0) s.scroll--; return true; }
    if (k.nav == hal::N_DOWN) { s.scroll++; return true; }
    if (k.ch >= '1' && k.ch <= '9') {
        size_t idx = k.ch - '1';
        if (idx < s.links.size()) go(s.links[idx].c_str());
        return true;
    }
    if (k.ch == '0') { if (s.links.size() >= 10) go(s.links[9].c_str()); return true; }
    if (k.ch == 'u') { // back
        if (!s.history.empty()) { std::string u = s.history.back(); s.history.pop_back(); go(u.c_str(), false); }
        return true;
    }
    if (k.ch == 'g') { s.edit.set(s.pendingBase.c_str()); s.mode = M_URLBAR; return true; }
    if (k.ch == 'b') {
        // 'b' also bookmarks current if held? keep simple: open list
        bool have = false;
        for (auto &m : s.marks) have |= m == s.pendingBase;
        if (!have && !s.pendingBase.empty()) s.marks.push_back(s.pendingBase);
        s.mode = M_BOOKMARKS;
        return true;
    }
    return true;
}

uint16_t lineColor(uint8_t k) {
    switch (k) {
    case L_H1: return C_EMBER;
    case L_H2: return C_RUST;
    case L_H3: return C_BLOOD;
    case L_LINK: return C_GLOW;
    case L_QUOTE: return C_BONE;
    case L_PRE: return C_DIM;
    default: return C_BONE;
    }
}

void draw(uint32_t now) {
    State &s = *S;
    Canvas &cv = hal::canvas();
    cv.fillScreen(C_VOID);
    header("GEMINI", "gemini");

    net::GemState gs = net::gemState();
    // show URL (shortened) top-right
    std::string u = net::gemURL();
    if (u.empty()) u = s.pendingBase;
    char ub[40];
    const char *uu = u.c_str();
    if (u.size() > 30) uu = u.c_str() + u.size() - 30;
    snprintf(ub, sizeof ub, "%s", uu);
    textRight(ub, 178, 5, C_DIM);

    if (gs == net::GEM_FETCHING) {
        textCentre("opening the channel...", 120, 60, C_DIM);
        float a = now * 0.006f;
        for (int i = 0; i < 7; ++i) {
            float t = a + i * 6.2831853f / 7;
            diamond(120 + (int)(cosf(t) * 16), 76 + (int)(sinf(t) * 10), 1,
                    blend(C_EMBER, C_VOID, 60 + (i * 40 % 195)));
        }
        char b[24];
        snprintf(b, sizeof b, "%d b", net::gemProgress());
        textCentre(b, 120, 92, C_DIM);
        return;
    }
    if (gs == net::GEM_INPUT && s.mode != M_INPUT) {
        s.mode = M_INPUT;
        s.edit.clear();
        s.edit.mask = net::gemStatus() == 11; // sensitive input
    }
    if (gs == net::GEM_ERROR) {
        char b[64];
        snprintf(b, sizeof b, "status %d", net::gemStatus());
        textCentre(b, 120, 54, C_BLOOD);
        std::string m = net::gemMeta();
        textCentre(m.empty() ? "no response" : m.c_str(), 120, 68, C_DIM);
        footer("u back  g go  b marks");
        return;
    }
    if (s.mode == M_URLBAR) {
        cv.fillRect(10, 50, 220, 36, C_DUSK);
        frame(10, 50, 220, 36, C_RUST, C_EMBER);
        textCentre("go to capsule", 120, 55, C_BONE);
        s.edit.draw(18, 68, 204, now, C_GLOW);
        footer("enter: open   ` : cancel");
        return;
    }
    if (s.mode == M_INPUT) {
        cv.fillRect(10, 44, 220, 48, C_DUSK);
        frame(10, 44, 220, 48, C_RUST, C_EMBER);
        std::string prompt = net::gemMeta();
        char p[40] = {};
        strncpy(p, prompt.c_str(), 38);
        textCentre(p, 120, 49, C_GLOW);
        s.edit.draw(18, 64, 204, now, C_BONE);
        textCentre("enter: send to the wired   ` : cancel", 120, 80, C_DIM);
        return;
    }
    if (s.mode == M_BOOKMARKS) {
        textCentre("- bookmarks -", 120, 24, C_EMBER);
        for (int i = 0; i < (int)s.marks.size(); ++i) {
            int y = 40 + i * 12;
            bool here = i == s.markSel;
            if (here) cv.fillRect(6, y - 1, 228, 11, C_DUSK);
            char m[40] = {};
            strncpy(m, s.marks[i].c_str(), 38);
            text(m, 12, y, here ? C_GLOW : C_BONE);
        }
        footer("enter: open   ` : back");
        return;
    }

    // parse once per load
    if (gs == net::GEM_DONE) {
        static std::string shown;
        std::string b = net::gemBody();
        std::string url = net::gemURL();
        if (url != s.pendingBase || b != shown || !s.loadedOnce) {
            // (re)parse only when the body changed
        }
        if (shown != b) { parse(b, url.empty() ? s.pendingBase : url); shown = b; }
    }

    // render with wrapping, 38 cols, from s.scroll
    const int COLS = 38, X = 4, Y0 = 20, LH = 9, VIS = 11;
    // build wrapped rows lazily
    std::vector<std::pair<const GLine *, std::string>> rows;
    for (auto &g : s.lines) {
        std::string prefix;
        if (g.kind == L_LINK) { char n[8]; snprintf(n, sizeof n, "[%d] ", g.linkNo); prefix = n; }
        else if (g.kind == L_LIST) prefix = "- ";
        else if (g.kind == L_QUOTE) prefix = "| ";
        std::string t = prefix + g.text;
        if (t.empty()) { rows.push_back({&g, ""}); continue; }
        for (size_t p = 0; p < t.size(); p += COLS) rows.push_back({&g, t.substr(p, COLS)});
    }
    int total = rows.size();
    if (s.scroll > total - 1) s.scroll = total > 0 ? total - 1 : 0;
    for (int i = 0; i < VIS && s.scroll + i < total; ++i) {
        auto &r = rows[s.scroll + i];
        int y = Y0 + i * LH;
        uint16_t c = lineColor(r.first->kind);
        if (r.first->kind == L_H1) {
            // a daedric drop-glyph for the first letter of H1 rows
            text(r.second.c_str(), X, y, c);
        } else text(r.second.c_str(), X, y, c);
    }
    // scrollbar
    if (total > VIS) {
        int bh = VIS * (VIS * LH) / total;
        int by = Y0 + s.scroll * (VIS * LH) / total;
        cv.fillRect(237, Y0, 2, VIS * LH, C_DUSK);
        cv.fillRect(237, by, 2, bh < 4 ? 4 : bh, C_RUST);
    }
    footer("1-9 link  u back  g go  b marks");
}
} // namespace

const Rite RITE_GEMINI = {"GEMINI", "gemini", "browse the small web", enter, leave, onKey, draw};
