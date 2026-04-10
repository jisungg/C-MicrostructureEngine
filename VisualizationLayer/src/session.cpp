#include "visualization/session.hpp"

#include <charconv>
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
    for (char c : s) {
        if      (c == '"')  { out += "\\\""; }
        else if (c == '\\') { out += "\\\\"; }
        else if (c == '\n') { out += "\\n";  }
        else if (c == '\r') { out += "\\r";  }
        else if (c == '\t') { out += "\\t";  }
        else                { out += c;      }
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

SourceMode source_mode_from_str(const std::string& s) noexcept {
    if (s == "synthetic") return SourceMode::Synthetic;
    if (s == "realistic") return SourceMode::Realistic;
    if (s == "csv")       return SourceMode::Csv;
    return SourceMode::Demo;
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
            char c = s[++pos];
            if      (c == '"')  out += '"';
            else if (c == '\\') out += '\\';
            else if (c == 'n')  out += '\n';
            else if (c == 'r')  out += '\r';
            else if (c == 't')  out += '\t';
            else                out += c;
        } else {
            out += s[pos];
        }
        ++pos;
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
