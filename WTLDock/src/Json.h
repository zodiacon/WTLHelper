#pragma once

// A minimal JSON reader/writer, just enough for the layout file. Strings are UTF-8.

#include <string>
#include <string_view>
#include <vector>

namespace WTLDock::Json {

struct Value {
	enum class Type { Null, Bool, Number, String, Array, Object };

	Type Kind{ Type::Null };
	bool Bool{};
	double Number{};
	std::string String;
	std::vector<std::string> Keys;	// objects only; parallel to Items
	std::vector<Value> Items;

	static Value MakeBool(bool b) {
		Value v;
		v.Kind = Type::Bool;
		v.Bool = b;
		return v;
	}
	static Value MakeNumber(double d) {
		Value v;
		v.Kind = Type::Number;
		v.Number = d;
		return v;
	}
	static Value MakeString(std::string s) {
		Value v;
		v.Kind = Type::String;
		v.String = std::move(s);
		return v;
	}
	static Value MakeArray() {
		Value v;
		v.Kind = Type::Array;
		return v;
	}
	static Value MakeObject() {
		Value v;
		v.Kind = Type::Object;
		return v;
	}

	void Push(Value item) {
		Items.push_back(std::move(item));
	}
	void Add(std::string key, Value item) {
		Keys.push_back(std::move(key));
		Items.push_back(std::move(item));
	}

	bool IsObject() const {
		return Kind == Type::Object;
	}
	bool IsArray() const {
		return Kind == Type::Array;
	}
	bool IsString() const {
		return Kind == Type::String;
	}
	bool IsNumber() const {
		return Kind == Type::Number;
	}
	// first member with that key, or null
	const Value* Find(std::string_view key) const {
		if (Kind == Type::Object)
			for (size_t i = 0; i < Keys.size(); i++)
				if (Keys[i] == key)
					return &Items[i];
		return nullptr;
	}
};

std::string Write(const Value& value);
bool Parse(std::string_view text, Value& value, std::string* error = nullptr);

}
