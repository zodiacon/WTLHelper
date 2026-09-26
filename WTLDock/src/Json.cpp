#include "Json.h"
#include <charconv>
#include <cstdio>
#include <cmath>
#include <system_error>

namespace WTLDock::Json {

namespace {

//
// writer
//

void WriteString(std::string& out, const std::string& s) {
	out += '"';
	for (unsigned char c : s) {
		switch (c) {
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if (c < 0x20) {
					char buf[8];
					snprintf(buf, sizeof(buf), "\\u%04x", c);
					out += buf;
				}
				else {
					out += (char)c;
				}
		}
	}
	out += '"';
}

void WriteNumber(std::string& out, double d) {
	char buf[40];
	std::to_chars_result r;
	if (std::floor(d) == d && std::fabs(d) < 1e15)
		r = std::to_chars(buf, buf + sizeof(buf), (long long)d);
	else
		r = std::to_chars(buf, buf + sizeof(buf), d);
	out.append(buf, r.ptr);
}

bool IsScalar(const Value& v) {
	return v.Kind != Value::Type::Array && v.Kind != Value::Type::Object;
}

void WriteValue(std::string& out, const Value& v, int indent) {
	switch (v.Kind) {
		case Value::Type::Null: out += "null"; break;
		case Value::Type::Bool: out += v.Bool ? "true" : "false"; break;
		case Value::Type::Number: WriteNumber(out, v.Number); break;
		case Value::Type::String: WriteString(out, v.String); break;

		case Value::Type::Array: {
			bool inlineItems = true;
			for (auto& item : v.Items)
				inlineItems &= IsScalar(item);
			if (v.Items.empty() || inlineItems) {
				out += '[';
				for (size_t i = 0; i < v.Items.size(); i++) {
					if (i)
						out += ", ";
					WriteValue(out, v.Items[i], indent);
				}
				out += ']';
				break;
			}
			out += "[\n";
			for (size_t i = 0; i < v.Items.size(); i++) {
				out.append((indent + 1) * 2, ' ');
				WriteValue(out, v.Items[i], indent + 1);
				out += i + 1 < v.Items.size() ? ",\n" : "\n";
			}
			out.append(indent * 2, ' ');
			out += ']';
			break;
		}

		case Value::Type::Object:
			if (v.Items.empty()) {
				out += "{}";
				break;
			}
			out += "{\n";
			for (size_t i = 0; i < v.Items.size(); i++) {
				out.append((indent + 1) * 2, ' ');
				WriteString(out, v.Keys[i]);
				out += ": ";
				WriteValue(out, v.Items[i], indent + 1);
				out += i + 1 < v.Items.size() ? ",\n" : "\n";
			}
			out.append(indent * 2, ' ');
			out += '}';
			break;
	}
}

//
// parser
//

class Parser {
public:
	Parser(std::string_view text) : m_Text(text) {
	}

	bool Run(Value& out, std::string* error) {
		if (m_Text.starts_with("\xEF\xBB\xBF"))
			m_Pos = 3;
		bool ok = ParseValue(out, 0);
		if (ok) {
			SkipSpace();
			if (m_Pos != m_Text.size())
				ok = Fail("unexpected content after the value");
		}
		if (!ok && error)
			*error = m_Error + " at offset " + std::to_string(m_Pos);
		return ok;
	}

private:
	static constexpr int MaxDepth = 64;

	bool Fail(const char* message) {
		if (m_Error.empty())
			m_Error = message;
		return false;
	}

	void SkipSpace() {
		while (m_Pos < m_Text.size() && (m_Text[m_Pos] == ' ' || m_Text[m_Pos] == '\t' || m_Text[m_Pos] == '\n' || m_Text[m_Pos] == '\r'))
			m_Pos++;
	}

	bool Consume(char c) {
		SkipSpace();
		if (m_Pos < m_Text.size() && m_Text[m_Pos] == c) {
			m_Pos++;
			return true;
		}
		return false;
	}

	bool ConsumeLiteral(std::string_view word) {
		if (m_Text.substr(m_Pos, word.size()) == word) {
			m_Pos += word.size();
			return true;
		}
		return false;
	}

	bool ParseValue(Value& out, int depth) {
		if (depth > MaxDepth)
			return Fail("nesting too deep");
		SkipSpace();
		if (m_Pos >= m_Text.size())
			return Fail("unexpected end of text");

		char c = m_Text[m_Pos];
		if (c == '{')
			return ParseObject(out, depth);
		if (c == '[')
			return ParseArray(out, depth);
		if (c == '"') {
			out = Value::MakeString({});
			return ParseString(out.String);
		}
		if (ConsumeLiteral("true")) {
			out = Value::MakeBool(true);
			return true;
		}
		if (ConsumeLiteral("false")) {
			out = Value::MakeBool(false);
			return true;
		}
		if (ConsumeLiteral("null")) {
			out = Value();
			return true;
		}
		return ParseNumber(out);
	}

	bool ParseNumber(Value& out) {
		size_t start = m_Pos;
		while (m_Pos < m_Text.size() && std::string_view("+-0123456789.eE").find(m_Text[m_Pos]) != std::string_view::npos)
			m_Pos++;
		if (start == m_Pos)
			return Fail("unexpected character");
		double d = 0;
		auto [ptr, ec] = std::from_chars(m_Text.data() + start, m_Text.data() + m_Pos, d);
		if (ec != std::errc() || ptr != m_Text.data() + m_Pos)
			return Fail("malformed number");
		out = Value::MakeNumber(d);
		return true;
	}

	static void AppendUtf8(std::string& s, unsigned cp) {
		if (cp < 0x80) {
			s += (char)cp;
		}
		else if (cp < 0x800) {
			s += (char)(0xC0 | (cp >> 6));
			s += (char)(0x80 | (cp & 0x3F));
		}
		else if (cp < 0x10000) {
			s += (char)(0xE0 | (cp >> 12));
			s += (char)(0x80 | ((cp >> 6) & 0x3F));
			s += (char)(0x80 | (cp & 0x3F));
		}
		else {
			s += (char)(0xF0 | (cp >> 18));
			s += (char)(0x80 | ((cp >> 12) & 0x3F));
			s += (char)(0x80 | ((cp >> 6) & 0x3F));
			s += (char)(0x80 | (cp & 0x3F));
		}
	}

	bool ParseHex4(unsigned& value) {
		if (m_Pos + 4 > m_Text.size())
			return Fail("truncated \\u escape");
		value = 0;
		for (int i = 0; i < 4; i++) {
			char c = m_Text[m_Pos++];
			value <<= 4;
			if (c >= '0' && c <= '9')
				value |= c - '0';
			else if (c >= 'a' && c <= 'f')
				value |= c - 'a' + 10;
			else if (c >= 'A' && c <= 'F')
				value |= c - 'A' + 10;
			else
				return Fail("bad \\u escape");
		}
		return true;
	}

	bool ParseString(std::string& out) {
		m_Pos++;	// opening quote
		while (m_Pos < m_Text.size()) {
			unsigned char c = m_Text[m_Pos++];
			if (c == '"')
				return true;
			if (c < 0x20)
				return Fail("control character in string");
			if (c != '\\') {
				out += (char)c;
				continue;
			}
			if (m_Pos >= m_Text.size())
				break;
			switch (char e = m_Text[m_Pos++]) {
				case '"': out += '"'; break;
				case '\\': out += '\\'; break;
				case '/': out += '/'; break;
				case 'b': out += '\b'; break;
				case 'f': out += '\f'; break;
				case 'n': out += '\n'; break;
				case 'r': out += '\r'; break;
				case 't': out += '\t'; break;
				case 'u': {
					unsigned cp;
					if (!ParseHex4(cp))
						return false;
					if (cp >= 0xD800 && cp < 0xDC00) {
						unsigned low;
						if (!ConsumeLiteral("\\u") || !ParseHex4(low) || low < 0xDC00 || low > 0xDFFF)
							return Fail("bad surrogate pair");
						cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
					}
					else if (cp >= 0xDC00 && cp <= 0xDFFF) {
						return Fail("unpaired surrogate");
					}
					AppendUtf8(out, cp);
					break;
				}
				default:
					(void)e;
					return Fail("bad escape");
			}
		}
		return Fail("unterminated string");
	}

	bool ParseArray(Value& out, int depth) {
		out = Value::MakeArray();
		m_Pos++;
		if (Consume(']'))
			return true;
		for (;;) {
			Value item;
			if (!ParseValue(item, depth + 1))
				return false;
			out.Push(std::move(item));
			if (Consume(','))
				continue;
			if (Consume(']'))
				return true;
			return Fail("expected ',' or ']'");
		}
	}

	bool ParseObject(Value& out, int depth) {
		out = Value::MakeObject();
		m_Pos++;
		if (Consume('}'))
			return true;
		for (;;) {
			SkipSpace();
			if (m_Pos >= m_Text.size() || m_Text[m_Pos] != '"')
				return Fail("expected a string key");
			std::string key;
			if (!ParseString(key))
				return false;
			if (!Consume(':'))
				return Fail("expected ':'");
			Value item;
			if (!ParseValue(item, depth + 1))
				return false;
			out.Add(std::move(key), std::move(item));
			if (Consume(','))
				continue;
			if (Consume('}'))
				return true;
			return Fail("expected ',' or '}'");
		}
	}

	std::string_view m_Text;
	size_t m_Pos{};
	std::string m_Error;
};

}

std::string Write(const Value& value) {
	std::string out;
	WriteValue(out, value, 0);
	return out;
}

bool Parse(std::string_view text, Value& value, std::string* error) {
	Value result;
	if (!Parser(text).Run(result, error))
		return false;
	value = std::move(result);
	return true;
}

}
