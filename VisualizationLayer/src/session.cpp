#include "visualization/session.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace visualization {

// ── Minimal JSON helpers ──────────────────────────────────────────────────────

namespace {

std::string json_str(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (const char raw_c : s) {
        const unsigned char c = static_cast<unsigned char>(raw_c);
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        case '\b': out += "\\b";  break;
        case '\f': out += "\\f";  break;
        default:
            if (c < 0x20u) {
                char esc[8];
                std::snprintf(esc, sizeof(esc), "\\u%04x", static_cast<unsigned>(c));
                out += esc;
            } else {
                out += raw_c;
            }
        }
    }
    out += '"';
    return out;
}

std::string source_mode_to_str(SourceMode m) noexcept {
    switch (m) {
        case SourceMode::Demo:      return "demo";
        case SourceMode::Synthetic: return "synthetic";
        case SourceMode::Realistic: return "realistic";
        case SourceMode::Csv:       return "csv";
    }
    return "demo";
}

// Throws on unrecognized token — silently falling back to Demo would hide
// corrupted or version-mismatched session files.
SourceMode source_mode_from_str(const std::string& s) {
    if (s == "demo")      return SourceMode::Demo;
    if (s == "synthetic") return SourceMode::Synthetic;
    if (s == "realistic") return SourceMode::Realistic;
    if (s == "csv")       return SourceMode::Csv;
    throw std::runtime_error(
        "VisualizationSession::from_json: invalid source_mode: '" + s + "'");
}

// ── Minimal JSON reader (handles only the flat structure we produce) ──────────

std::size_t skip_ws(const std::string& s, std::size_t pos) noexcept {
    while (pos < s.size() &&
           (s[pos]==' '||s[pos]=='\t'||s[pos]=='\n'||s[pos]=='\r'))
        ++pos;
    return pos;
}

std::string read_str(const std::string& s, std::size_t& pos) {
    if (pos >= s.size() || s[pos] != '"')
        throw std::runtime_error("VisualizationSession::from_json: expected '\"'");
    ++pos;
    std::string out;
    while (pos < s.size() && s[pos] != '"') {
        if (s[pos] == '\\' && pos + 1 < s.size()) {
            const char esc = s[pos + 1];
            pos += 2;  // skip both backslash and the escape letter
            switch (esc) {
            case '"':  out += '"';  break;
            case '\\': out += '\\'; break;
            case 'n':  out += '\n'; break;
            case 'r':  out += '\r'; break;
            case 't':  out += '\t'; break;
            case 'b':  out += '\b'; break;
            case 'f':  out += '\f'; break;
            case 'u': {
                // Parse exactly 4 hex digits (\u00XX — the range we emit for
                // control characters < 0x20).
                if (pos + 4 > s.size())
                    throw std::runtime_error(
                        "VisualizationSession::from_json: truncated \\uXXXX escape");
                unsigned int cp = 0;
                for (int i = 0; i < 4; ++i) {
                    const char h = s[pos++];
                    unsigned int d;
                    if      (h >= '0' && h <= '9') d = static_cast<unsigned int>(h - '0');
                    else if (h >= 'a' && h <= 'f') d = static_cast<unsigned int>(h - 'a') + 10U;
                    else if (h >= 'A' && h <= 'F') d = static_cast<unsigned int>(h - 'A') + 10U;
                    else throw std::runtime_error(
                        "VisualizationSession::from_json: invalid hex digit in \\uXXXX");
                    cp = cp * 16U + d;
                }
                // We only emit \\u00XX (codepoints < 0x80); reject anything wider.
                if (cp >= 0x80U)
                    throw std::runtime_error(
                        "VisualizationSession::from_json: non-ASCII \\uXXXX not supported");
                out += static_cast<char>(static_cast<unsigned char>(cp));
                break;
            }
            default:
                out += esc;  // unknown escape: pass through the escape character
            }
        } else {
            out += s[pos];
            ++pos;
        }
    }
    if (pos >= s.size())
        throw std::runtime_error("VisualizationSession::from_json: unterminated string");
    ++pos; // consume closing '"'
    return out;
}

std::size_t read_uint(const std::string& s, std::size_t& pos) {
    const char* first = s.data() + pos;
    const char* last  = s.data() + s.size();
    std::size_t val{};
    auto [ptr, ec] = std::from_chars(first, last, val);
    if (ec != std::errc{})
        throw std::runtime_error("VisualizationSession::from_json: expected uint");
    pos = static_cast<std::size_t>(ptr - s.data());
    return val;
}

long long read_int64(const std::string& s, std::size_t& pos) {
    const char* first = s.data() + pos;
    const char* last  = s.data() + s.size();
    long long val{};
    auto [ptr, ec] = std::from_chars(first, last, val);
    if (ec != std::errc{})
        throw std::runtime_error("VisualizationSession::from_json: expected int64");
    pos = static_cast<std::size_t>(ptr - s.data());
    return val;
}

} // anonymous namespace

// ── to_json ───────────────────────────────────────────────────────────────────

std::string VisualizationSession::to_json() const {
    std::string o;
    o.reserve(256);
    o += '{';
    o += "\"source_path\":"   + json_str(source_path) + ',';
    o += "\"source_mode\":"   + json_str(source_mode_to_str(source_mode)) + ',';
    o += "\"frame_count\":"   + std::to_string(frame_count) + ',';
    o += "\"first_ts\":"      + std::to_string(first_ts) + ',';
    o += "\"last_ts\":"       + std::to_string(last_ts) + ',';
    o += "\"current_frame\":" + std::to_string(current_frame) + ',';
    o += "\"active_filter\":" + json_str(active_filter) + ',';
    o += "\"bookmarks\":[";
    for (std::size_t i = 0; i < bookmarks.size(); ++i) {
        if (i) o += ',';
        o += std::to_string(bookmarks[i]);
    }
    o += "]}";
    return o;
}

// ── from_json ─────────────────────────────────────────────────────────────────

VisualizationSession VisualizationSession::from_json(const std::string& json) {
    VisualizationSession sess;
    std::size_t pos = skip_ws(json, 0);
    if (pos >= json.size() || json[pos] != '{')
        throw std::runtime_error("VisualizationSession::from_json: expected '{'");
    ++pos;

    while (true) {
        pos = skip_ws(json, pos);
        if (pos >= json.size()) break;
        if (json[pos] == '}') { ++pos; break; }
        if (json[pos] == ',') { ++pos; continue; }

        pos = skip_ws(json, pos);
        std::string key = read_str(json, pos);
        pos = skip_ws(json, pos);
        if (pos >= json.size() || json[pos] != ':')
            throw std::runtime_error("VisualizationSession::from_json: expected ':'");
        ++pos;
        pos = skip_ws(json, pos);

        if      (key == "source_path")   { sess.source_path   = read_str(json, pos); }
        else if (key == "source_mode")   { sess.source_mode   = source_mode_from_str(read_str(json, pos)); }
        else if (key == "frame_count")   { sess.frame_count   = read_uint(json, pos); }
        else if (key == "first_ts")      { sess.first_ts      = static_cast<microstructure::Timestamp>(read_int64(json, pos)); }
        else if (key == "last_ts")       { sess.last_ts       = static_cast<microstructure::Timestamp>(read_int64(json, pos)); }
        else if (key == "current_frame") { sess.current_frame = read_uint(json, pos); }
        else if (key == "active_filter") { sess.active_filter = read_str(json, pos); }
        else if (key == "bookmarks") {
            pos = skip_ws(json, pos);
            if (pos < json.size() && json[pos] == '[') {
                ++pos;
                while (true) {
                    pos = skip_ws(json, pos);
                    if (pos >= json.size() || json[pos] == ']') {
                        if (pos < json.size()) ++pos;
                        break;
                    }
                    if (json[pos] == ',') { ++pos; continue; }
                    sess.bookmarks.push_back(read_uint(json, pos));
                }
            }
        } else {
            // Skip unknown value: string, number, or nested array
            if (pos < json.size() && json[pos] == '"') {
                read_str(json, pos);
            } else {
                while (pos < json.size() && json[pos] != ',' && json[pos] != '}')
                    ++pos;
            }
        }
    }

    // ── Semantic validation ───────────────────────────────────────────────────

    // active_filter must be a known event-type token or empty / "ALL".
    if (!sess.active_filter.empty() && sess.active_filter != "ALL") {
        static const char* const kValid[] = {
            "ADD", "CANCEL", "MODIFY", "TRADE", "SNAPSHOT", nullptr
        };
        bool found = false;
        for (int ki = 0; kValid[ki]; ++ki) {
            if (sess.active_filter == kValid[ki]) { found = true; break; }
        }
        if (!found)
            throw std::runtime_error(
                "VisualizationSession::from_json: invalid active_filter: '" +
                sess.active_filter + "'");
    }

    // Validate frame position and bookmark ranges against frame_count.
    if (sess.frame_count == 0) {
        // An empty session must have no current position and no bookmarks.
        if (sess.current_frame != 0)
            throw std::runtime_error(
                "VisualizationSession::from_json: current_frame must be 0 "
                "when frame_count is 0");
        if (!sess.bookmarks.empty())
            throw std::runtime_error(
                "VisualizationSession::from_json: bookmarks must be empty "
                "when frame_count is 0");
    } else {
        // current_frame must be a valid index within frame_count.
        if (sess.current_frame >= sess.frame_count)
            throw std::runtime_error(
                "VisualizationSession::from_json: current_frame (" +
                std::to_string(sess.current_frame) + ") >= frame_count (" +
                std::to_string(sess.frame_count) + ")");
        // Silently drop bookmarks stale from a longer replay.
        sess.bookmarks.erase(
            std::remove_if(sess.bookmarks.begin(), sess.bookmarks.end(),
                           [&sess](std::size_t b){ return b >= sess.frame_count; }),
            sess.bookmarks.end());
    }

    return sess;
}

// ── save / load ───────────────────────────────────────────────────────────────

bool VisualizationSession::save(const std::string& path) const {
    std::ofstream out{path, std::ios::binary};
    if (!out.is_open()) return false;
    out << to_json();
    return out.good();
}

VisualizationSession VisualizationSession::load(const std::string& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in.is_open())
        throw std::runtime_error(
            "VisualizationSession::load: cannot open '" + path + "'");
    std::ostringstream ss;
    ss << in.rdbuf();
    return from_json(ss.str());
}

// ── matches ───────────────────────────────────────────────────────────────────

bool VisualizationSession::matches(const std::string& event_type) const {
    if (active_filter.empty() || active_filter == "ALL") return true;
    return event_type == active_filter;
}

} // namespace visualization
