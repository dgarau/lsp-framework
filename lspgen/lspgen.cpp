#include <map>
#include <memory>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <lsp/json/json.h>

/*
 * This is a huge mess because it started out as an experiment only.
 * But it works and should keep on working with upcoming lsp versions
 * unless there are fundamental changes to the meta model in which case
 * everything should be rewritten from scratch...
 */

using namespace lsp;

namespace strings{

#define STRING(x) const std::string x{#x};
STRING(both)
STRING(clientToServer)
STRING(documentation)
STRING(element)
STRING(extends)
STRING(items)
STRING(key)
STRING(kind)
STRING(messageDirection)
STRING(method)
STRING(mixins)
STRING(name)
STRING(optional)
STRING(params)
STRING(partialResult)
STRING(errorData)
STRING(properties)
STRING(registrationOptions)
STRING(result)
STRING(serverToClient)
STRING(supportsCustomValues)
STRING(type)
STRING(value)
STRING(values)
#undef STRING

};

std::string capitalizeString(std::string_view str)
{
	std::string result{str};

	if(!result.empty())
		result[0] = static_cast<char>(std::toupper(result[0]));

	return result;
}

std::string uncapitalizeString(std::string_view str)
{
	std::string result{str};

	if(!result.empty())
		result[0] = static_cast<char>(std::tolower(result[0]));

	return result;
}

std::string replaceString(std::string_view str, std::string_view pattern, std::string_view replacement)
{
	std::string result;
	result.reserve(str.size() + replacement.size());
	std::size_t srcIdx = 0;

	for(std::size_t idx = str.find(pattern); idx != std::string_view::npos; idx = str.find(pattern, srcIdx))
	{
		result += str.substr(srcIdx, idx - srcIdx);
		result += replacement;
		srcIdx = idx + pattern.size();
	}

	result += str.substr(srcIdx);

	return result;
}

std::vector<std::string_view> splitStringView(std::string_view str, std::string_view separator, bool skipEmpty = false)
{
	std::vector<std::string_view> result;
	std::size_t srcIdx = 0;

	for(std::size_t idx = str.find(separator); idx != std::string_view::npos; idx = str.find(separator, srcIdx))
	{
		const auto part = str.substr(srcIdx, idx - srcIdx);
		srcIdx = idx + separator.size();

		if(part.empty() && skipEmpty)
			continue;

		result.push_back(part);
	}

	if(srcIdx < str.size())
		result.push_back(str.substr(srcIdx));

	return result;
}

std::vector<std::string_view> splitStringView(std::string&&, char, bool) = delete;

std::string joinStrings(const std::vector<std::string_view>& strings, const std::string& separator, auto transform = [](std::string_view s){ return s; })
{
	std::string result;

	if(auto it = strings.begin(); it != strings.end())
	{
		result = transform(*it);
		++it;

		while(it != strings.end())
		{
			result += separator + transform(*it);
			++it;
		}
	}

	return result;
}

std::string extractDocumentation(const json::Object& json)
{
	if(json.contains(strings::documentation))
		return json.get(strings::documentation).string();

	return {};
}

using TypePtr = std::unique_ptr<struct Type>;

struct Type{
	virtual ~Type() = default;

	enum Category{
		Base,
		Reference,
		Array,
		Map,
		And,
		Or,
		Tuple,
		StructureLiteral,
		StringLiteral,
		IntegerLiteral,
		BooleanLiteral
	};

	static constexpr std::string_view TypeCategoryStrings[] =
	{
		"base",
		"reference",
		"array",
		"map",
		"and",
		"or",
		"tuple",
		"literal",
		"stringLiteral",
		"integerLiteral",
		"booleanLiteral"
	};

	virtual Category category() const = 0;
	virtual void extract(const json::Object& json) = 0;

	bool isLiteral() const{
		const auto cat = category();
		return cat == StructureLiteral || cat == StringLiteral || cat == IntegerLiteral || cat == BooleanLiteral;
	}

	static Category categoryFromString(std::string_view str)
	{
		for(std::size_t i = 0; i < std::size(TypeCategoryStrings); ++i)
		{
			if(TypeCategoryStrings[i] == str)
				return static_cast<Type::Category>(i);
		}

		throw std::runtime_error{'\'' + std::string{str} + "' is not a valid type kind"};
	}

	template<typename T>
	bool isA() const
	{
		return dynamic_cast<const T*>(this) != nullptr;
	}

	template<typename T>
	T& as()
	{
		return dynamic_cast<T&>(*this);
	}

	template<typename T>
	const T& as() const
	{
		return dynamic_cast<const T&>(*this);
	}

	static TypePtr createFromJson(const json::Object& json);
};

struct BaseType : Type{
	enum Kind{
		Boolean,
		String,
		Integer,
		UInteger,
		Decimal,
		URI,
		DocumentUri,
		RegExp,
		Null,
		MAX
	};

	static constexpr std::string_view BaseTypeStrings[] =
	{
		"boolean",
		"string",
		"integer",
		"uinteger",
		"decimal",
		"URI",
		"DocumentUri",
		"RegExp",
		"null"
	};

	Kind kind = {};

	Category category() const override{ return Category::Base; }

	void extract(const json::Object& json) override
	{
		kind = kindFromString(json.get(strings::name).string());
	}

	static Kind kindFromString(std::string_view str)
	{
		for(std::size_t i = 0; i < std::size(BaseTypeStrings); ++i)
		{
			if(BaseTypeStrings[i] == str)
				return static_cast<Kind>(i);
		}

		throw std::runtime_error{'\'' + std::string{str} + "' is not a valid base type"};
	}
};

struct ReferenceType : Type{
	std::string name;

	Category category() const override{ return Category::Reference; }

	void extract(const json::Object& json) override
	{
		name = json.get(strings::name).string();
	}
};

struct ArrayType : Type{
	TypePtr elementType;

	Category category() const override{ return Category::Array; }

	void extract(const json::Object& json) override
	{
		const auto& elementTypeJson = json.get(strings::element).object();
		elementType = createFromJson(elementTypeJson);
	}
};

struct MapType : Type{
	TypePtr keyType;
	TypePtr valueType;

	void extract(const json::Object& json) override
	{
		keyType = createFromJson(json.get(strings::key).object());
		valueType = createFromJson(json.get(strings::value).object());
	}

	Category category() const override{ return Category::Map; }
};

struct AndType : Type{
	std::vector<TypePtr> typeList;

	void extract(const json::Object& json) override
	{
		const auto& items = json.get(strings::items).array();
		typeList.reserve(items.size());

		for(const auto& i : items)
			typeList.push_back(createFromJson(i.object()));
	}

	Category category() const override{ return Category::And; }
};

struct OrType : Type{
	std::vector<TypePtr> typeList;

	void extract(const json::Object& json) override;

	Category category() const override{ return Category::Or; }
};

struct TupleType : Type{
	std::vector<TypePtr> typeList;

	void extract(const json::Object& json) override
	{
		const auto& items = json.get(strings::items).array();
		typeList.reserve(items.size());

		for(const auto& i : items)
			typeList.push_back(createFromJson(i.object()));
	}

	Category category() const override{ return Category::Tuple; }
};

struct StructureProperty{
	std::string name;
	TypePtr     type;
	bool        isOptional = false;
	std::string documentation;

	void extract(const json::Object& json)
	{
		name = json.get(strings::name).string();
		type = Type::createFromJson(json.get(strings::type).object());
		isOptional = json.contains(strings::optional) && json.get(strings::optional).boolean();
		documentation = extractDocumentation(json);
	}
};

using StructurePropertyList = std::vector<StructureProperty>;

StructurePropertyList extractStructureProperties(const json::Array& json)
{
	StructurePropertyList result;
	result.reserve(json.size());
	std::transform(
		json.begin(),
		json.end(),
		std::back_inserter(result),
		[](const json::Any& e)
		{
			StructureProperty prop;
			prop.extract(e.object());
			return prop;
		});
	// Sort properties so non-optional ones come first
	std::stable_sort(
		result.begin(),
		result.end(),
		[](const auto& p1, const auto& p2)
		{
			return !p1.isOptional && p2.isOptional;
		});

	return result;
}

struct StructureLiteralType : Type{
	StructurePropertyList properties;

	void extract(const json::Object& json) override
	{
		const auto& value = json.get(strings::value).object();
		properties = extractStructureProperties(value.get(strings::properties).array());
	}

	Category category() const override{ return Category::StructureLiteral; }
};

void OrType::extract(const json::Object& json)
{
	const auto& items = json.get(strings::items).array();
	typeList.reserve(items.size());

	std::vector<std::unique_ptr<Type>> structureLiterals;

	for(const auto& item : items)
	{
		auto type = createFromJson(item.object());

		if(type->isA<StructureLiteralType>())
			structureLiterals.push_back(std::move(type));
		else
			typeList.push_back(std::move(type));
	}

	// Merge consecutive identical struct literals where the only difference is whether properties are optional or not

	for(std::size_t i = 1; i < structureLiterals.size(); ++i)
	{
		auto& first = structureLiterals[i - 1]->as<StructureLiteralType>();
		const auto& second = structureLiterals[i]->as<StructureLiteralType>();

		if(first.properties.size() == second.properties.size())
		{
			bool propertiesEqual = true;

			for(std::size_t p = 0; p < first.properties.size(); ++p)
			{
				if(first.properties[p].name != second.properties[p].name)
				{
					propertiesEqual = false;
					break;
				}
			}

			if(propertiesEqual)
			{
				for(std::size_t p = 0; p < first.properties.size(); ++p)
					first.properties[p].isOptional |= second.properties[p].isOptional;

				structureLiterals.erase(structureLiterals.begin() + static_cast<decltype(structureLiterals)::difference_type>(i));
				--i;
			}
		}
	}

	std::move(structureLiterals.begin(), structureLiterals.end(), std::back_inserter(typeList));
	structureLiterals.clear();

	if(typeList.empty())
		throw std::runtime_error{"OrType must not be empty!"};
}

struct StringLiteralType : Type{
	std::string stringValue;

	void extract(const json::Object& json) override
	{
		stringValue = json.get(strings::value).string();
	}

	Category category() const override{ return Category::StringLiteral; }
};

struct IntegerLiteralType : Type{
	json::Integer integerValue = 0;

	void extract(const json::Object& json) override
	{
		integerValue = static_cast<json::Integer>(json.get(strings::value).number());
	}

	Category category() const override{ return Category::IntegerLiteral; }
};

struct BooleanLiteralType : Type{
	bool booleanValue = false;

	void extract(const json::Object& json) override
	{
		booleanValue = json.get(strings::value).boolean();
	}

	Category category() const override{ return Category::BooleanLiteral; }
};

TypePtr Type::createFromJson(const json::Object& json)
{
	TypePtr result;
	auto category = categoryFromString(json.get(strings::kind).string());

	switch(category)
	{
	case Base:
		result = std::make_unique<BaseType>();
		break;
	case Reference:
		result = std::make_unique<ReferenceType>();
		break;
	case Array:
		result = std::make_unique<ArrayType>();
		break;
	case Map:
		result = std::make_unique<MapType>();
		break;
	case And:
		result = std::make_unique<AndType>();
		break;
	case Or:
		result = std::make_unique<OrType>();
		break;
	case Tuple:
		result = std::make_unique<TupleType>();
		break;
	case StructureLiteral:
		result = std::make_unique<StructureLiteralType>();
		break;
	case StringLiteral:
		result = std::make_unique<StringLiteralType>();
		break;
	case IntegerLiteral:
		result = std::make_unique<IntegerLiteralType>();
		break;
	case BooleanLiteral:
		result = std::make_unique<BooleanLiteralType>();
		break;
	default:
		assert(!"Invalid type category");
		throw std::logic_error{"Invalid type category"};
	}

	assert(result->category() == category);
	result->extract(json);

	return result;
}

//================================

struct Enumeration{
	std::string name;
	TypePtr     type;

	struct Value{
		std::string name;
		json::Any   value;
		std::string documentation;
	};

	std::vector<Value> values;
	std::string        documentation;
	bool               supportsCustomValues = false;

	void extract(const json::Object& json)
	{
		name = json.get(strings::name).string();
		const auto& typeJson = json.get(strings::type).object();
		type = Type::createFromJson(typeJson);
		const auto& valuesJson = json.get(strings::values).array();
		values.reserve(valuesJson.size());

		for(const auto& v : valuesJson)
		{
			const auto& obj = v.object();
			auto& enumValue = values.emplace_back();
			enumValue.name = obj.get(strings::name).string();
			enumValue.value = obj.get(strings::value);
			enumValue.documentation = extractDocumentation(obj);
		}

		documentation = extractDocumentation(json);

		if(json.contains(strings::supportsCustomValues))
			supportsCustomValues = json.get(strings::supportsCustomValues).boolean();
	}
};

struct Structure{
	std::string name;
	std::vector<StructureProperty> properties;
	std::vector<TypePtr>  extends;
	std::vector<TypePtr>  mixins;
	std::string           documentation;

	void extract(const json::Object& json)
	{
		name = json.get(strings::name).string();
		properties = extractStructureProperties(json.get(strings::properties).array());

		if(json.contains(strings::extends))
		{
			const auto& extendsJson = json.get(strings::extends).array();
			extends.reserve(extendsJson.size());

			for(const auto& e : extendsJson)
				extends.push_back(Type::createFromJson(e.object()));
		}

		if(json.contains(strings::mixins))
		{
			const auto& mixinsJson = json.get(strings::mixins).array();
			mixins.reserve(mixinsJson.size());

			for(const auto& e : mixinsJson)
				mixins.push_back(Type::createFromJson(e.object()));
		}

		documentation = extractDocumentation(json);
	}
};

struct TypeAlias{
	std::string name;
	TypePtr     type;
	std::string documentation;

	void extract(const json::Object& json)
	{
		name = json.get(strings::name).string();
		type = Type::createFromJson(json.get(strings::type).object());
		documentation = extractDocumentation(json);
	}
};

struct Message{
	enum class Direction{
		ClientToServer,
		ServerToClient,
		Both
	};

	std::string documentation;
	Direction   direction;
	// Those should be omitted if the strings are empty
	std::string paramsTypeName;
	std::string resultTypeName;
	std::string partialResultTypeName;
	std::string errorDataTypeName;
	std::string registrationOptionsTypeName;

	static std::string memberTypeName(const json::Object& json, const std::string& key)
	{
		if(!json.contains(key))
			return {};

		const auto& type = json.get(key).object();

		if(type.get(strings::kind).string() == "reference")
			return type.get(strings::name).string();

		return json.get(strings::method).string() + capitalizeString(key);
	}

	void extract(const json::Object& json)
	{
		documentation = extractDocumentation(json);
		const auto& dir = json.get(strings::messageDirection).string();

		if(dir == strings::clientToServer)
			direction = Direction::ClientToServer;
		else if(dir == strings::serverToClient)
			direction = Direction::ServerToClient;
		else if(dir == strings::both)
			direction = Direction::Both;
		else
			throw std::runtime_error{"Invalid message direction: " + dir};

		paramsTypeName = memberTypeName(json, strings::params);
		resultTypeName = memberTypeName(json, strings::result);
		partialResultTypeName = memberTypeName(json, strings::partialResult);
		errorDataTypeName = memberTypeName(json, strings::errorData);
		registrationOptionsTypeName = memberTypeName(json, strings::registrationOptions);
	}
};

class MetaModel{
public:
	MetaModel() = default;

	void extract(const json::Object& json)
	{
		extractMetaData(json);
		extractTypes(json);
		extractMessages(json);
	}

	std::variant<const Enumeration*, const Structure*, const TypeAlias*> typeForName(std::string_view name) const
	{
		if(auto it = m_typesByName.find(std::string{name}); it != m_typesByName.end())
		{
			switch(it->second.type)
			{
			case Type::Enumeration:
				return &m_enumerations[it->second.index];
			case Type::Structure:
				return &m_structures[it->second.index];
			case Type::TypeAlias:
				return &m_typeAliases[it->second.index];
			}
		}

		throw std::runtime_error{"Type with name '" + std::string{name} + "' does not exist"};
	}

	enum class MessageType{
		Request,
		Notification
	};

	const std::map<std::string, Message>& messagesByName(MessageType type) const
	{
		if(type == MessageType::Request)
			return m_requestsByMethod;

		assert(type == MessageType::Notification);
		return m_notificationsByMethod;
	}

	struct MetaData{
		std::string version;
	};

	const MetaData& metaData() const{ return m_metaData; }
	const std::vector<std::string_view>& typeNames() const{ return m_typeNames; }
	const std::vector<Enumeration>& enumerations() const{ return m_enumerations; }
	const std::vector<Structure>& structures() const{ return m_structures; }
	const std::vector<TypeAlias>& typeAliases() const{ return m_typeAliases; }

private:
	MetaData m_metaData;

	enum class Type{
		Enumeration,
		Structure,
		TypeAlias
	};

	struct TypeIndex{
		Type        type;
		std::size_t index;
	};

	std::vector<std::string_view>              m_typeNames;
	std::unordered_map<std::string, TypeIndex> m_typesByName;
	std::vector<Enumeration>                   m_enumerations;
	std::vector<Structure>                     m_structures;
	std::vector<TypeAlias>                     m_typeAliases;
	std::map<std::string, Message>             m_requestsByMethod;
	std::map<std::string, Message>             m_notificationsByMethod;

	void extractMetaData(const json::Object& json)
	{
		const auto& metaDataJson = json.get("metaData").object();
		m_metaData.version = metaDataJson.get("version").string();
	}

	void extractTypes(const json::Object& json)
	{
		extractEnumerations(json);
		extractStructures(json);
		extractTypeAliases(json);
	}

	void extractMessages(const json::Object& json)
	{
		extractRequests(json);
		extractNotifications(json);
	}

	void extractRequests(const json::Object& json)
	{
		const auto& requests = json.get("requests").array();

		for(const auto& r : requests)
		{
			const auto& obj = r.object();
			const auto& method = obj.get(strings::method).string();

			if(m_requestsByMethod.contains(method))
				throw std::runtime_error{"Duplicate request method: " + method};

			m_requestsByMethod[method].extract(obj);
		}
	}

	void extractNotifications(const json::Object& json)
	{
		const auto& notifications = json.get("notifications").array();

		for(const auto& r : notifications)
		{
			const auto& obj = r.object();
			const auto& method = obj.get(strings::method).string();

			if(m_notificationsByMethod.contains(method))
				throw std::runtime_error{"Duplicate request method: " + method};

			m_notificationsByMethod[method].extract(obj);
		}
	}

	void insertType(const std::string& name, Type type, std::size_t index)
	{
		if(m_typesByName.contains(name))
			throw std::runtime_error{"Duplicate type '" + name + '\"'};

		auto it = m_typesByName.insert(std::pair(name, TypeIndex{type, index})).first;
		m_typeNames.push_back(it->first);
	}

	void extractEnumerations(const json::Object& json)
	{
		const auto& enumerations = json.get("enumerations").array();
		auto prevSize = m_enumerations.size();
		m_enumerations.resize(prevSize+enumerations.size());

		std::size_t j = prevSize;
		for(std::size_t i = 0; i < enumerations.size(); ++i, ++j)
		{
			m_enumerations[j].extract(enumerations[i].object());
			insertType(m_enumerations[j].name, Type::Enumeration, j);
		}
	}

	void extractStructures(const json::Object& json)
	{
		const auto& structures = json.get("structures").array();
		auto prevSize = m_structures.size();
		m_structures.resize(prevSize+structures.size());

		std::size_t j = prevSize;
		for(std::size_t i = 0; i < structures.size(); ++i, ++j)
		{
			m_structures[j].extract(structures[i].object());
			insertType(m_structures[j].name, Type::Structure, j);
		}
	}

	void addTypeAlias(const json::Object& json, const std::string& key, const std::string& typeBaseName)
	{
		if(json.contains(key))
		{
			const auto& typeJson = json.get(key).object();

			if(typeJson.get(strings::kind).string() != "reference")
			{
				auto& alias = m_typeAliases.emplace_back();
				alias.name = typeBaseName + capitalizeString(key);
				alias.type = ::Type::createFromJson(typeJson);
				alias.documentation = extractDocumentation(typeJson);
				insertType(alias.name, Type::TypeAlias, m_typeAliases.size() - 1);
			}
		}
	}

	void extractTypeAliases(const json::Object& json)
	{
		const auto& typeAliases = json.get("typeAliases").array();
		auto prevSize = m_typeAliases.size();
		m_typeAliases.resize(prevSize+typeAliases.size());

		std::size_t j = prevSize;
		for(std::size_t i = 0; i < typeAliases.size(); ++i, ++j)
		{
			m_typeAliases[j].extract(typeAliases[i].object());
			insertType(m_typeAliases[j].name, Type::TypeAlias, j);
		}

		// Extract message and notification parameter and result types

		const auto& requests = json.get("requests").array();

		for(const auto& r : requests)
		{
			const auto& obj = r.object();
			const auto& typeBaseName = obj.get(strings::method).string();

			addTypeAlias(obj, strings::result, typeBaseName);
			addTypeAlias(obj, strings::params, typeBaseName);
			addTypeAlias(obj, strings::partialResult, typeBaseName);
			addTypeAlias(obj, strings::errorData, typeBaseName);
			addTypeAlias(obj, strings::registrationOptions, typeBaseName);
		}

		const auto& notifications = json.get("notifications").array();

		for(const auto& n : notifications)
		{
			const auto& obj = n.object();
			const auto& typeBaseName = obj.get(strings::method).string();
			addTypeAlias(obj, strings::params, typeBaseName);
			addTypeAlias(obj, strings::registrationOptions, typeBaseName);
		}
	}
};

static constexpr const char* TypesHeaderBegin =
R"(#pragma once

/*#############################################################
 * NOTE: This is a generated file and it shouldn't be modified!
 *#############################################################*/

#include <tuple>
#include <string>
#include <vector>
#include <variant>
#include <string_view>
#include <lsp/uri.h>
#include <lsp/strmap.h>
#include <lsp/fileuri.h>
#include <lsp/nullable.h>
#include <lsp/json/json.h>
#include <lsp/enumeration.h>
#include <lsp/serialization.h>

namespace lsp{

inline constexpr std::string_view VersionStr{"${LSP_VERSION}"};

using Null      = std::nullptr_t;
using uint      = unsigned int;
using String    = std::string;
using LSPArray  = json::Array;
using LSPObject = json::Object;
using LSPAny    = json::Any;

template<typename T>
using Opt = std::optional<T>;

template<typename... Args>
using Tuple = std::tuple<Args...>;

template<typename... Args>
using OneOf = std::variant<Args...>;

template<typename T>
using NullOr = Nullable<T>;

template<typename... Args>
using NullOrOneOf = NullableVariant<Args...>;

template<typename T>
using Array = std::vector<T>;

template<typename K, typename T>
using Map = StrMap<K, T>;

)";

static constexpr const char* TypesHeaderEnd =
R"(
} // namespace lsp
)";

static constexpr const char* TypesSourceBegin =
R"(#include "types.h"

/*#############################################################
 * NOTE: This is a generated file and it shouldn't be modified!
 *#############################################################*/

namespace lsp{

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4100) // unreferenced formal parameter
#endif

)";

static constexpr const char* TypesSourceEnd =
R"(#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
} // namespace lsp
)";

static constexpr const char* MessagesHeaderBegin =
R"(#pragma once

/*#############################################################
 * NOTE: This is a generated file and it shouldn't be modified!
 *#############################################################*/

#include <lsp/messagebase.h>
#include "types.h"

namespace lsp{

)";

static constexpr const char* MessagesHeaderEnd =
R"(} // namespace lsp
)";

class CppGenerator
{
public:
	CppGenerator(const MetaModel* metaModel) : m_metaModel{*metaModel}{}

	void generate()
	{
		generateTypes();
		generateMessages();
	}

	void writeFiles()
	{
		writeFile("types.h", replaceString(TypesHeaderBegin, "${LSP_VERSION}", m_metaModel.metaData().version) + m_typesHeaderFileContent + m_typesBoilerPlateHeaderFileContent + TypesHeaderEnd);
		writeFile("types.cpp", TypesSourceBegin + m_typesSourceFileContent + m_typesBoilerPlateSourceFileContent + TypesSourceEnd);
		writeFile("messages.h", MessagesHeaderBegin + m_messagesHeaderFileContent + MessagesHeaderEnd);
	}

private:
	std::string                                  m_typesHeaderFileContent;
	std::string                                  m_typesBoilerPlateHeaderFileContent;
	std::string                                  m_typesBoilerPlateSourceFileContent;
	std::string                                  m_typesSourceFileContent;
	std::string                                  m_messagesHeaderFileContent;
	const MetaModel&                             m_metaModel;
	std::unordered_set<std::string_view>         m_processedTypes;
	std::unordered_set<std::string_view>         m_typesBeingProcessed;
	std::unordered_map<const Type*, std::string> m_generatedTypeNames;

	struct CppBaseType
	{
		std::string name;
	};

	static const CppBaseType s_baseTypeMapping[BaseType::MAX];

	void generateTypes()
	{
		m_processedTypes = {"LSPArray", "LSPObject", "LSPAny"};
		m_typesBeingProcessed = {};
		m_typesBoilerPlateHeaderFileContent = "/*\n * Serialization boilerplate\n */\n\n";
		m_typesBoilerPlateSourceFileContent = m_typesBoilerPlateHeaderFileContent;

		for(const auto& name : m_metaModel.typeNames())
			generateNamedType(name);
	}

	void generateMessages()
	{
		const char* namespaceStr = "/*\n"
		                           " * Request messages\n"
		                           " */\n"
		                           "namespace requests{\n\n";

		m_messagesHeaderFileContent += namespaceStr;

		for(const auto& [method, message] : m_metaModel.messagesByName(MetaModel::MessageType::Request))
			generateMessage(method, message, false);

		namespaceStr = "} // namespace requests\n\n"
		               "/*\n"
		               " * Notification messages\n"
		               " */\n"
		               "namespace notifications{\n\n";


		m_messagesHeaderFileContent += namespaceStr;

		for(const auto& [method, message] : m_metaModel.messagesByName(MetaModel::MessageType::Notification))
			generateMessage(method, message, true);

		namespaceStr = "} // namespace notifications\n";

		m_messagesHeaderFileContent += namespaceStr;
	}

	void generateMessage(const std::string& method, const Message& message, bool isNotification)
	{
		auto messageCppName = upperCaseIdentifier(method);
		std::string messageDirection;

		switch(message.direction)
		{
		case Message::Direction::ClientToServer:
			messageDirection = "ClientToServer";
			break;
		case Message::Direction::ServerToClient:
			messageDirection = "ServerToClient";
			break;
		case Message::Direction::Both:
			messageDirection = "Bidirectional";
		}

		m_messagesHeaderFileContent += documentationComment(method, message.documentation) +
		                               "struct " + messageCppName + "{\n"
		                               "\tstatic constexpr auto Method    = std::string_view(\"" + method + "\");\n"
		                               "\tstatic constexpr auto Direction = MessageDirection::" + messageDirection + ";\n"
		                               "\tstatic constexpr auto Type      = Message::" + (isNotification ? "Notification" : "Request") + ";\n";

		const bool hasRegistrationOptions = !message.registrationOptionsTypeName.empty();
		const bool hasPartialResult = !message.partialResultTypeName.empty();
		const bool hasErrorData = !message.errorDataTypeName.empty();
		const bool hasParams = !message.paramsTypeName.empty();
		const bool hasResult = !message.resultTypeName.empty();

		if(hasRegistrationOptions || hasPartialResult || hasParams || hasResult)
			m_messagesHeaderFileContent += '\n';

		if(hasRegistrationOptions)
			m_messagesHeaderFileContent += "\tusing RegistrationOptions = " + upperCaseIdentifier(message.registrationOptionsTypeName) + ";\n";

		if(hasPartialResult)
			m_messagesHeaderFileContent += "\tusing PartialResult = " + upperCaseIdentifier(message.partialResultTypeName) + ";\n";

		if(hasErrorData)
			m_messagesHeaderFileContent += "\tusing ErrorData = " + upperCaseIdentifier(message.errorDataTypeName) + ";\n";

		if(hasParams)
			m_messagesHeaderFileContent += "\tusing Params = " + upperCaseIdentifier(message.paramsTypeName) + ";\n";

		if(hasResult)
			m_messagesHeaderFileContent += "\tusing Result = " + upperCaseIdentifier(message.resultTypeName) + ";\n";

		m_messagesHeaderFileContent += "};\n\n";
	}

	static void writeFile(const std::string& name, std::string_view content)
	{
		std::ofstream file{name, std::ios::trunc | std::ios::binary};
		file.write(content.data(), static_cast<std::streamsize>(content.size()));
	}

	static std::string upperCaseIdentifier(std::string_view str)
	{
		if(str.starts_with('$'))
			str.remove_prefix(1);

		auto parts = splitStringView(str, "/", true);
		auto id = joinStrings(parts, "_", [](auto&& s){ return capitalizeString(s); });

		std::transform(id.cbegin(), id.cend(), id.begin(), [](char c)
		{
			if(!std::isalnum(c) && c != '_')
				return '_';

			return c;
		});

		return id;
	}

	static std::string lowerCaseIdentifier(std::string_view str)
	{
		return uncapitalizeString(str);
	}

	static std::string toJsonSig(const std::string& typeName)
	{
		return "json::Any toJson(" + typeName + "&& value)";
	}

	static std::string fromJsonSig(const std::string& typeName)
	{
		return "void fromJson(json::Any&& json, " + typeName + "& value)";
	}

	static std::string documentationComment(const std::string& title, const std::string& documentation, int indentLevel = 0)
	{
		std::string indent(indentLevel, '\t');
		std::string comment = indent + "/*\n";

		if(!title.empty())
			comment += indent + " * " + title + "\n";

		auto documentationLines = splitStringView(documentation, "\n");

		if(!documentationLines.empty())
		{
			if(!title.empty())
				comment += indent + " *\n";

			for(const auto& line : documentationLines)
				comment += indent + " * " + replaceString(replaceString(line, "/*", "/_*"), "*/", "*_/") + '\n';
		}
		else if(title.empty())
		{
		 return {};
		}

		comment += indent + " */\n";

		return comment;
	}

	void generateNamedType(std::string_view name)
	{
		if(m_processedTypes.contains(name))
			return;

		m_processedTypes.insert(name);
		std::visit([this](const auto* ptr)
		{
			m_typesBeingProcessed.insert(ptr->name);
			generate(*ptr);
			m_typesBeingProcessed.erase(ptr->name);
		}, m_metaModel.typeForName(name));
	}

	void generate(const Enumeration& enumeration)
	{
		if(!enumeration.type->isA<BaseType>())
			throw std::runtime_error{"Enumeration value type for '" + enumeration.name + "' must be a base type"};

		const auto enumTypeCppName = upperCaseIdentifier(enumeration.name);
		const auto enumerationCppName = enumTypeCppName + "Enum";

		const auto& baseType = s_baseTypeMapping[enumeration.type->as<BaseType>().kind];

		m_typesHeaderFileContent += documentationComment(enumTypeCppName, enumeration.documentation);

		m_typesHeaderFileContent += "enum class " + enumTypeCppName + "{\n";

		m_typesSourceFileContent += "template<>\n"
		                            "const " + enumerationCppName + "::ConstInitType " +
		                            enumerationCppName + "::s_values[] = {\n";

		if(auto it = enumeration.values.begin(); it != enumeration.values.end())
		{
			m_typesHeaderFileContent += documentationComment({}, it->documentation, 1) +
			                            '\t' + capitalizeString(it->name);
			m_typesSourceFileContent += '\t' + json::stringify(it->value);
			++it;

			while(it != enumeration.values.end())
			{
				m_typesHeaderFileContent += ",\n" + documentationComment({}, it->documentation, 1) + '\t' + capitalizeString(it->name);
				m_typesSourceFileContent += ",\n\t" + json::stringify(it->value);
				++it;
			}
		}

		const auto enumCppTemplateType = "Enumeration<" + enumTypeCppName + ", " + baseType.name + '>';

		m_typesHeaderFileContent += ",\n\tMAX_VALUE\n"
		                            "};\n"
																"using " + enumTypeCppName + "Enum = " + enumCppTemplateType + ";\n"
		                            "template<>\n"
		                            "const " + enumerationCppName + "::ConstInitType " + enumerationCppName + "::s_values[];\n\n";

		m_typesSourceFileContent += "\n};\n\n";
	}

	bool isStringType(const TypePtr& type)
	{
		if(type->isA<BaseType>())
		{
			const auto& base = type->as<BaseType>();

			return base.kind == BaseType::String || base.kind == BaseType::URI || base.kind == BaseType::DocumentUri || base.kind == BaseType::RegExp;
		}
		else if(type->isA<ReferenceType>())
		{
			const auto& ref = type->as<ReferenceType>();
			auto refType = m_metaModel.typeForName(ref.name);

			if(std::holds_alternative<const TypeAlias*>(refType))
				return isStringType(std::get<const TypeAlias*>(refType)->type);
		}

		return false;
	}

	std::string cppTypeName(const Type& type, bool optional = false)
	{
		std::string typeName;

		if(optional)
		{
			if(!type.isA<ReferenceType>() || !m_typesBeingProcessed.contains(type.as<ReferenceType>().name))
				typeName = "Opt<";
			else
				typeName = "std::unique_ptr<";
		}

		switch(type.category())
		{
		case Type::Base:
			typeName += s_baseTypeMapping[static_cast<int>(type.as<BaseType>().kind)].name;
			break;
		case Type::Reference:
			{
				const auto& ref = type.as<ReferenceType>();
				typeName += upperCaseIdentifier(ref.name);

				const auto typeVariant = m_metaModel.typeForName(ref.name);
				if(std::holds_alternative<const Enumeration*>(typeVariant))
					typeName += "Enum";

				break;
			}
		case Type::Array:
			{
				const auto& arrayType = type.as<ArrayType>();
				if(arrayType.elementType->isA<ReferenceType>() && arrayType.elementType->as<ReferenceType>().name == "LSPAny")
					typeName += "LSPArray";
				else
					typeName += "Array<" + cppTypeName(*arrayType.elementType) + '>';

				break;
			}
		case Type::Map:
			{
				const auto& keyType = type.as<MapType>().keyType;
				const auto& valueType = type.as<MapType>().valueType;

				if(isStringType(keyType))
					typeName += "Map<";
				else
					typeName += "std::unordered_map<";

				typeName += cppTypeName(*keyType) + ", " + cppTypeName(*valueType) + '>';

				break;
			}
		case Type::And:
			typeName += "LSPObject"; // TODO: Generate a proper and type should they ever actually be used by the LSP
			break;
		case Type::Or:
			{
				const auto& orType = type.as<OrType>();

				if(orType.typeList.size() > 1)
				{
					auto nullType = std::find_if(orType.typeList.begin(), orType.typeList.end(), [](const TypePtr& type)
					{
					                                                                               return type->isA<BaseType>() && type->as<BaseType>().kind == BaseType::Null;
					                                                                             });
					std::string cppOrType;

					if(nullType == orType.typeList.end())
						cppOrType = "OneOf<";
					else if(orType.typeList.size() > 2)
						cppOrType = "NullOrOneOf<";
					else
						cppOrType = "NullOr<";

					if(auto it = orType.typeList.begin(); it != orType.typeList.end())
					{
						if(it == nullType)
							++it;

						assert(it != orType.typeList.end());
						cppOrType += cppTypeName(**it);
						++it;

						while(it != orType.typeList.end())
						{
							if(it != nullType)
								cppOrType += ", " + cppTypeName(**it);

							++it;
						}
					}

					cppOrType += '>';

					typeName += cppOrType;
				}
				else
				{
					assert(!orType.typeList.empty());
					typeName += cppTypeName(*orType.typeList[0]);
				}

				break;
			}
		case Type::Tuple:
			{
				std::string cppTupleType = "Tuple<";
				const auto& tupleType = type.as<TupleType>();

				if(auto it = tupleType.typeList.begin(); it != tupleType.typeList.end())
				{
					cppTupleType += cppTypeName(**it);
					++it;

					while(it != tupleType.typeList.end())
					{
						cppTupleType += ", " + cppTypeName(**it);
						++it;
					}
				}

				cppTupleType += '>';

				typeName += cppTupleType;
				break;
			}
		case Type::StructureLiteral:
			typeName += m_generatedTypeNames[&type];
			break;
		case Type::StringLiteral:
			typeName += "String";
			break;
		case Type::IntegerLiteral:
			typeName += "int";
			break;
		case Type::BooleanLiteral:
			typeName += "bool";
			break;
		default:
			assert(!"Invalid type category");
			throw std::logic_error{"Invalid type category"};
		}

		if(optional)
			typeName += '>';

		return typeName;
	}

	void generateAggregateTypeList(const std::vector<TypePtr>& typeList, const std::string& baseName)
	{
		// Only append unique number to type name if there are multiple structure literals
		for(const auto& t : typeList)
		{
			std::string nameSuffix;

			if(t->isA<StructureLiteralType>())
			{
				const auto& structLit = t->as<StructureLiteralType>();

				// Include all non-optional properties in the type name
				for(const auto& p : structLit.properties)
				{
					if(!p.isOptional)
						nameSuffix += '_' + capitalizeString(p.name);
				}
			}

			generateType(t, baseName + nameSuffix);
		}
	}

	void generateType(const TypePtr& type, const std::string& baseName, bool alias = false)
	{
		switch(type->category())
		{
		case Type::Reference:
			generateNamedType(type->as<ReferenceType>().name);
			break;
		case Type::Array:
			generateType(type->as<ArrayType>().elementType, baseName);
			break;
		case Type::Map:
			generateType(type->as<MapType>().keyType, baseName);
			generateType(type->as<MapType>().valueType, baseName);
			break;
		case Type::And:
			generateAggregateTypeList(type->as<AndType>().typeList, baseName + (alias ? "_Base" : ""));
			break;
		case Type::Or:
			generateAggregateTypeList(type->as<OrType>().typeList, baseName);
			break;
		case Type::Tuple:
			generateAggregateTypeList(type->as<TupleType>().typeList, baseName + (alias ? "_Element" : ""));
			break;
		case Type::StructureLiteral:
			{
				// HACK:
				// Temporarily transfer ownership of property types to temporary structure so the same generator code can be used for structure literals.
				auto& structureLiteral = type->as<StructureLiteralType>();
				Structure structure;

				structure.name = baseName;
				m_generatedTypeNames[type.get()] = structure.name;
				structure.properties = std::move(structureLiteral.properties);

				generate(structure);

				// HACK: Transfer unique_ptr ownership back from the temporary structure
				structureLiteral.properties = std::move(structure.properties);

				break;
			}
		default:
			break;
		}
	}

	static bool isTrivialBaseType(const TypePtr& type)
	{
		if(type->isA<BaseType>())
		{
			const auto& base = type->as<BaseType>();

			return base.kind == BaseType::Kind::Boolean ||
			       base.kind == BaseType::Kind::Integer ||
			       base.kind == BaseType::Kind::UInteger ||
			       base.kind == BaseType::Kind::Decimal ||
			       base.kind == BaseType::Kind::Null;
		}

		return false;
	}

	void generateStructureProperties(const std::vector<StructureProperty>& properties,
	                                 const std::unordered_map<std::string_view,
	                                 const StructureProperty*>& basePropertiesByName,
	                                 std::string& toJson,
	                                 std::string& fromJson,
	                                 std::vector<std::string>& requiredProperties,
	                                 std::vector<std::pair<std::string, std::string>>& literalProperties,
	                                 std::vector<std::pair<std::string_view, std::string>>& inheritedLiterals)
	{
		for(const auto& p : properties)
		{
			std::string literalValue;
			bool isInheritedLiteral = false;

			if(p.type->isLiteral())
			{
				switch(p.type->category())
				{
				case Type::StringLiteral:
					literalValue = json::toStringLiteral(p.type->as<StringLiteralType>().stringValue);
					break;
				case Type::IntegerLiteral:
					literalValue = std::to_string(p.type->as<IntegerLiteralType>().integerValue);
					break;
				case Type::BooleanLiteral:
					literalValue = std::string{p.type->as<BooleanLiteralType>().booleanValue ? "true" : "false"};
					break;
				default:
					break;
				}

				if(basePropertiesByName.contains(p.name))
				{
					inheritedLiterals.emplace_back(p.name, literalValue);
					isInheritedLiteral = true;
				}
			}

			// Don't write literal properties with the same name as an inherited property. Instead initialize the inherited property.
			if(!isInheritedLiteral)
			{
				const auto typeName = cppTypeName(*p.type, p.isOptional);

				m_typesHeaderFileContent += documentationComment({}, p.documentation, 1) +
				                            '\t' + typeName + ' ' + p.name;

				if(!literalValue.empty())
					m_typesHeaderFileContent += " = " + literalValue;
				else if (p.isOptional)
					m_typesHeaderFileContent += " = {}";

				m_typesHeaderFileContent += ";\n";
			}

			if(p.isOptional)
			{
				toJson += "\tif(value." + p.name + ")\n\t";
				fromJson += "\tif(const auto it = json.find(\"" + p.name + "\"); it != json.end())\n\t"
				            "\tfromJson(std::move(it->second), value." + p.name + ");\n";
			}
			else
			{
				if(!isInheritedLiteral)
					fromJson += "\tfromJson(std::move(json.get(\"" + p.name + "\")), value." + p.name + ");\n";

				if(literalValue.empty())
					requiredProperties.push_back(p.name);
			}

			if(!literalValue.empty())
			{
				literalProperties.push_back({p.name, literalValue});
				fromJson += "\tif(value." + p.name + " != " + literalValue + ")\n"
				            "\t\tthrow json::TypeError(\"Unexpected value for literal '" + p.name + "'\");\n";
			}

			if(!isInheritedLiteral)
			{
				std::string toJsonParam;

				if(isTrivialBaseType(p.type) && !p.isOptional)
					toJsonParam = "value." + p.name;
				else
					toJsonParam = "std::move(value." + p.name + ')';

				toJson += "\tjson[\"" + p.name + "\"] = toJson(" + toJsonParam + ");\n";
			}
		}
	}

	void generate(const Structure& structure)
	{
		std::string structureCppName = upperCaseIdentifier(structure.name);

		// Make sure dependencies are generated first
		{
			for(const auto& e : structure.extends)
				generateType(e, {});

			// Mixins technically don't have to be generated since their properties are directly copied into this structure.
			// However, generating them all here makes sure that all property types are also generated.
			for(const auto& m : structure.mixins)
				generateType(m, {});

			for(const auto& p : structure.properties)
				generateType(p.type, structureCppName + capitalizeString(p.name));
		}

		m_typesHeaderFileContent += documentationComment(structureCppName, structure.documentation) +
		                            "struct " + structureCppName;

		std::string propertiesToJson = "static void " + uncapitalizeString(structureCppName) + "ToJson(" +
		                               structureCppName + "& value, json::Object& json)\n{\n";
		std::string propertiesFromJson = "static void " + uncapitalizeString(structureCppName) + "FromJson("
		                                 "json::Object& json, " + structureCppName + "& value)\n{\n";
		const std::string requiredPropertiesSig = "template<>\nconst char** requiredProperties<" + structureCppName + ">()";
		const std::string literalPropertiesSig = "template<>\nconst std::pair<const char*, json::Any>* literalProperties<" + structureCppName + ">()";
		std::vector<std::string>                         requiredPropertiesList;
		std::vector<std::pair<std::string, std::string>> literalPropertiesList;
		std::string requiredProperties = requiredPropertiesSig + "\n{\n\tstatic const char* properties[] = {\n";
		std::string literalProperties = literalPropertiesSig + "\n{\n\tstatic const std::pair<const char*, json::Any> properties[] = {\n";

		// Add base classes

		std::unordered_map<std::string_view, const StructureProperty*> basePropertiesByName;
		if(auto it = structure.extends.begin(); it != structure.extends.end())
		{
			const auto* extends = &(*it)->as<ReferenceType>();
			m_typesHeaderFileContent += " : " + extends->name;
			std::string lower = uncapitalizeString(extends->name);
			propertiesToJson += '\t' + lower + "ToJson(value, json);\n";
			propertiesFromJson += '\t' + lower + "FromJson(json, value);\n";
			++it;

			for(const auto& p : std::get<const Structure*>(m_metaModel.typeForName(extends->name))->properties)
			{
				if(!p.isOptional)
					requiredPropertiesList.push_back(p.name);

				basePropertiesByName[p.name] = &p;
			}

			while(it != structure.extends.end())
			{
				extends = &(*it)->as<ReferenceType>();
				m_typesHeaderFileContent += ", " + extends->name;
				lower = uncapitalizeString(extends->name);
				propertiesToJson += '\t' + lower + "ToJson(value, json);\n";
				propertiesFromJson += '\t' + lower + "FromJson(json, value);\n";
				++it;

				for(const auto& p : std::get<const Structure*>(m_metaModel.typeForName(extends->name))->properties)
				{
					if(!p.isOptional)
						requiredPropertiesList.push_back(p.name);
				}
			}
		}

		m_typesHeaderFileContent += "{\n";

		// Generate properties

		std::vector<std::pair<std::string_view, std::string>> inheritedLiterals;
		for(const auto& m : structure.mixins)
		{
			if(!m->isA<ReferenceType>())
				throw std::runtime_error{"Mixin type for '" + structure.name + "' must be a type reference"};

			auto type = m_metaModel.typeForName(m->as<ReferenceType>().name);

			if(!std::holds_alternative<const Structure*>(type))
				throw std::runtime_error{"Mixin type for '" + structure.name + "' must be a structure type"};

			generateStructureProperties(std::get<const Structure*>(type)->properties, basePropertiesByName, propertiesToJson, propertiesFromJson, requiredPropertiesList, literalPropertiesList, inheritedLiterals);
		}

		generateStructureProperties(structure.properties, basePropertiesByName, propertiesToJson, propertiesFromJson, requiredPropertiesList, literalPropertiesList, inheritedLiterals);

		if(!inheritedLiterals.empty())
		{
			m_typesHeaderFileContent += "\n\t" + structure.name + "()\n\t{\n";

			for(const auto& v : inheritedLiterals)
			{
				m_typesHeaderFileContent += "\t\t";
				m_typesHeaderFileContent += v.first;
				m_typesHeaderFileContent += " = " + v.second + ";\n";
			}

			m_typesHeaderFileContent += "\t}\n";
		}

		m_typesHeaderFileContent += "};\n\n";

		propertiesToJson += "}\n\n";
		propertiesFromJson += "}\n\n";

		for(const auto& p : requiredPropertiesList)
			requiredProperties += "\t\t\"" + p + "\",\n";

		requiredProperties += "\t\tnullptr\n\t};\n\treturn properties;\n}\n\n";

		for(const auto& p : literalPropertiesList)
			literalProperties += "\t\t{\"" + p.first + "\", " + p.second + "},\n";

		literalProperties += "\t\t{nullptr, {}}\n\t};\n\treturn properties;\n}\n\n";

		std::string toJson = toJsonSig(structureCppName);
		std::string fromJson = fromJsonSig(structureCppName);

		if(!requiredPropertiesList.empty())
		{
			m_typesBoilerPlateHeaderFileContent += requiredPropertiesSig + ";\n";
			m_typesBoilerPlateSourceFileContent += requiredProperties;
		}

		if(!literalPropertiesList.empty())
		{
			m_typesBoilerPlateHeaderFileContent += literalPropertiesSig + ";\n";
			m_typesBoilerPlateSourceFileContent += literalProperties;
		}

		m_typesBoilerPlateHeaderFileContent += toJson + ";\n" +
		                                       fromJson + ";\n";
		m_typesSourceFileContent += propertiesToJson + propertiesFromJson;
		m_typesBoilerPlateSourceFileContent += toJson + "\n"
		                                       "{\n"
		                                       "\tjson::Object obj;\n"
		                                       "\t" + uncapitalizeString(structureCppName) + "ToJson(value, obj);\n"
		                                       "\treturn obj;\n"
		                                       "}\n\n" +
		                                       fromJson + "\n"
		                                       "{\n"
		                                       "\tauto& obj = json.object();\n"
		                                       "\t" + uncapitalizeString(structureCppName) + "FromJson(obj, value);\n"
		                                       "}\n\n";
	}

	void generate(const TypeAlias& typeAlias)
	{
		auto typeAliasCppName = upperCaseIdentifier(typeAlias.name);

		generateType(typeAlias.type, typeAliasCppName, true);

		m_typesHeaderFileContent += documentationComment(typeAliasCppName, typeAlias.documentation) +
		                            "using " + typeAliasCppName + " = " + cppTypeName(*typeAlias.type) + ";\n\n";
	}
};

const CppGenerator::CppBaseType CppGenerator::s_baseTypeMapping[] =
{
	{"bool"},
	{"String"},
	{"int"},
	{"uint"},
	{"double"},
	{"Uri"},
	{"DocumentUri"},
	{"String"},
	{"Null"}
};

int main(int argc, char** argv)
{
	int ExitCode = EXIT_SUCCESS;

	try
	{
		MetaModel metaModel;
		for (int i = 1; i < argc; ++i)
		{
			const char* inputFileName = argv[i];
			if(std::ifstream in{inputFileName, std::ios::binary})
			{
				in.seekg(0, std::ios::end);
				std::streamsize size = in.tellg();
				in.seekg(0, std::ios::beg);
				std::string jsonText;
				jsonText.resize(static_cast<std::string::size_type>(size));
				in.read(&jsonText[0], size);
				in.close();
				auto json = json::parse(jsonText).object();
				metaModel.extract(json);
			}
			else
			{
				std::cerr << "Failed to open " << inputFileName << std::endl;
				ExitCode = EXIT_FAILURE;
			}
		}
		CppGenerator generator{&metaModel};
		generator.generate();
		generator.writeFiles();
	}
	catch(const json::ParseError& e)
	{
		std::cerr << "JSON parse error at offset " << e.textPos() << ": " << e.what() << std::endl;
		ExitCode = EXIT_FAILURE;
	}
	catch(const std::exception& e)
	{
		std::cerr << "Error: " << e.what() << std::endl;
		ExitCode = EXIT_FAILURE;
	}
	catch(...)
	{
		std::cerr << "Unknown error" << std::endl;
		ExitCode = EXIT_FAILURE;
	}

	return ExitCode;
}
