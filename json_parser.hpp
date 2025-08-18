#ifndef JSON_HPP
#define JSON_HPP

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <cctype>
#include <sstream>

struct JsonValue;

using JsonObject = std::map<std::string, JsonValue>;
using JsonArray = std::vector<JsonValue>;

struct JsonValue {
    enum Type { Null, Bool, Number, String, Array, Object } type;
    bool b;
    double n;
    std::string s;
    JsonArray a;
    JsonObject o;

    std::string to_string() const;
};

struct Parser {
    const std::string& text;
    size_t pos;

    Parser(const std::string& t) : text(t), pos(0) {}

    void skip() {
        while (pos < text.size() && std::isspace(text[pos])) pos++;
    }

    bool match(char c) {
        skip();
        if (pos < text.size() && text[pos] == c) {
            pos++;
            return true;
        }
        return false;
    }

    std::string parse_string() {
        std::string out;
        if (text[pos] != '"') return out;
        pos++;
        while (pos < text.size()) {
            char c = text[pos++];
            if (c == '"') break;
            if (c == '\\') {
                char next = text[pos++];
                if (next == '"') out += '"';
                else if (next == '\\') out += '\\';
                else if (next == '/') out += '/';
                else if (next == 'b') out += '\b';
                else if (next == 'f') out += '\f';
                else if (next == 'n') out += '\n';
                else if (next == 'r') out += '\r';
                else if (next == 't') out += '\t';
            }
            else {
                out += c;
            }
        }
        return out;
    }

    double parse_number() {
        skip();
        size_t start = pos;
        if (text[pos] == '-') pos++;
        while (pos < text.size() && std::isdigit(text[pos])) pos++;
        if (pos < text.size() && text[pos] == '.') {
            pos++;
            while (pos < text.size() && std::isdigit(text[pos])) pos++;
        }
        if (pos < text.size() && (text[pos] == 'e' || text[pos] == 'E')) {
            pos++;
            if (text[pos] == '+' || text[pos] == '-') pos++;
            while (pos < text.size() && std::isdigit(text[pos])) pos++;
        }
        return std::stod(text.substr(start, pos - start));
    }

    JsonValue parse_value() {
        skip();
        if (pos >= text.size()) return JsonValue{ JsonValue::Null };

        if (text[pos] == '"') {
            return JsonValue{ JsonValue::String, false, 0.0, parse_string() };
        }
        if (std::isdigit(text[pos]) || text[pos] == '-') {
            return JsonValue{ JsonValue::Number, false, parse_number() };
        }
        if (text.compare(pos, 4, "true") == 0) {
            pos += 4;
            return JsonValue{ JsonValue::Bool, true };
        }
        if (text.compare(pos, 5, "false") == 0) {
            pos += 5;
            return JsonValue{ JsonValue::Bool, false };
        }
        if (text.compare(pos, 4, "null") == 0) {
            pos += 4;
            return JsonValue{ JsonValue::Null };
        }
        if (text[pos] == '[') {
            pos++;
            JsonArray arr;
            skip();
            if (match(']')) return JsonValue{ JsonValue::Array, false, 0.0, "", arr };
            while (true) {
                arr.push_back(parse_value());
                skip();
                if (match(']')) break;
                match(',');
            }
            return JsonValue{ JsonValue::Array, false, 0.0, "", arr };
        }
        if (text[pos] == '{') {
            pos++;
            JsonObject obj;
            skip();
            if (match('}')) return JsonValue{ JsonValue::Object, false, 0.0, "", {}, obj };
            while (true) {
                skip();
                std::string key = parse_string();
                skip();
                match(':');
                JsonValue val = parse_value();
                obj[key] = val;
                skip();
                if (match('}')) break;
                match(',');
            }
            return JsonValue{ JsonValue::Object, false, 0.0, "", {}, obj };
        }
        return JsonValue{ JsonValue::Null };
    }
};

void print_json(const JsonValue& v, int indent = 0) {
    std::string pad(indent, ' ');
    switch (v.type) {
    case JsonValue::Null:
        std::cout << "null";
        break;
    case JsonValue::Bool:
        std::cout << (v.b ? "true" : "false");
        break;
    case JsonValue::Number:
        std::cout << v.n;
        break;
    case JsonValue::String:
        std::cout << "\"" << v.s << "\"";
        break;
    case JsonValue::Array:
        std::cout << "[\n";
        for (size_t i = 0; i < v.a.size(); i++) {
            std::cout << pad << "  ";
            print_json(v.a[i], indent + 2);
            if (i + 1 < v.a.size()) std::cout << ",";
            std::cout << "\n";
        }
        std::cout << pad << "]";
        break;
    case JsonValue::Object:
        std::cout << "{\n";
        for (auto it = v.o.begin(); it != v.o.end(); ++it) {
            std::cout << pad << "  \"" << it->first << "\": ";
            print_json(it->second, indent + 2);
            if (std::next(it) != v.o.end()) std::cout << ",";
            std::cout << "\n";
        }
        std::cout << pad << "}";
        break;
    }
}

std::string JsonValue::to_string() const {
    std::ostringstream os;

    switch (type) {
    case Null:
        os << "null";
        break;
    case Bool:
        os << (b ? "true" : "false");
        break;
    case Number:
        os << n;
        break;
    case String:
        os << '"';
        for (char c : s) {
            if (c == '\\') os << "\\\\";
            else if (c == '"') os << "\\\"";
            else if (c == '\n') os << "\\n";
            else if (c == '\r') os << "\\r";
            else if (c == '\t') os << "\\t";
            else os << c;
        }
        os << '"';
        break;
    case Array:
        os << '[';
        for (size_t i = 0; i < a.size(); ++i) {
            if (i > 0) os << ',';
            os << a[i].to_string();
        }
        os << ']';
        break;
    case Object:
        os << '{';
        bool first = true;
        for (const auto& [key, val] : o) {
            if (!first) os << ',';
            first = false;
            os << '"' << key << "\":" << val.to_string();
        }
        os << '}';
        break;
    }

    return os.str();
}

#endif // JSON_HPP
