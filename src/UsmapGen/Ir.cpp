#include "UsmapGen/Schema.h"

#include <array>
#include <format>
#include <span>

namespace UsmapGen {

namespace {

constexpr std::uint32_t irVersion = 1;

//! In the order of PropertyType.
constexpr std::array<std::string_view, 37> typeNames{
    "Byte", "Bool", "Int", "Float", "Object", "Name", "Delegate", "Double", "Array", "Struct",
    "Str", "Text", "Interface", "MulticastDelegate", "WeakObject", "LazyObject", "AssetObject", "SoftObject", "UInt64", "UInt32",
    "UInt16", "Int64", "Int16", "Int8", "Map", "Set", "Enum", "FieldPath", "Optional", "Utf8Str",
    "AnsiStr", "Class", "MulticastInlineDelegate", "SoftClass", "VerseString", "VerseDynamic", "VerseFunction"};

class IrWriter {
public:
    explicit IrWriter(const Schema& schema) : schema(schema), names(schema.names.getNames()) {}

    std::string write() {
        const Ir& ir = schema.ir;
        json += std::format("{{\"version\":{},\"unreadFunctions\":{},\n\"enums\":[", irVersion, ir.unreadFunctions);
        for (std::size_t index = 0; index < schema.enums.size(); index++) {
            json += index == 0 ? "\n" : ",\n";
            appendEnum(schema.enums[index], packageOf(ir.enumPackages, index));
        }
        json += "],\n\"structs\":[";
        for (std::size_t index = ir.classCount; index < schema.structs.size(); index++) {
            json += index == ir.classCount ? "\n" : ",\n";
            appendStruct(schema.structs[index], packageOf(ir.structPackages, index));
            json += '}';
        }
        json += "],\n\"classes\":[";
        std::size_t function = 0;
        for (std::size_t index = 0; index < ir.classCount && index < schema.structs.size(); index++) {
            json += index == 0 ? "\n" : ",\n";
            appendStruct(schema.structs[index], packageOf(ir.structPackages, index));
            json += ",\"functions\":[";
            for (bool first = true; function < ir.functions.size() && ir.functions[function].owner == index; function++, first = false) {
                if (!first)
                    json += ',';
                appendFunction(ir.functions[function]);
            }
            json += "]}";
        }
        json += "]}\n";
        return std::move(json);
    }

private:
    std::uint32_t packageOf(const std::vector<std::uint32_t>& packages, const std::size_t index) const {
        return index < packages.size() ? packages[index] : noName;
    }

    void appendString(const std::string_view text) {
        json += '"';
        for (const char character : text) {
            if (character == '"' || character == '\\')
                json += '\\';
            if (static_cast<unsigned char>(character) < 0x20)
                json += std::format("\\u{:04x}", static_cast<unsigned char>(character));
            else
                json += character;
        }
        json += '"';
    }

    void appendName(const std::uint32_t name) { appendName(name, names); }

    void appendName(const std::uint32_t name, const std::vector<std::string>& table) {
        if (name == noName)
            json += "null";
        else
            appendString(table[name]);
    }

    void appendPackage(const std::uint32_t package) {
        if (package < schema.ir.packages.size())
            appendString(schema.ir.packages[package]);
        else
            json += "null";
    }

    std::size_t appendType(std::size_t at) {
        const std::uint32_t index = std::uint32_t(at);
        const TypeNode& node = schema.types[at++];
        json += "{\"type\":";
        appendString(typeNames[std::size_t(node.type)]);
        switch (node.type) {
        case PropertyType::Enum:
            json += ",\"enum\":";
            appendName(node.name);
            json += ",\"inner\":";
            at = appendType(at);
            break;
        case PropertyType::Struct:
            json += ",\"struct\":";
            appendName(node.name);
            break;
        case PropertyType::Array:
        case PropertyType::Set:
        case PropertyType::Optional:
            json += ",\"inner\":";
            at = appendType(at);
            break;
        case PropertyType::Map:
            json += ",\"key\":";
            at = appendType(at);
            json += ",\"value\":";
            at = appendType(at);
            break;
        case PropertyType::Object:
        case PropertyType::WeakObject:
        case PropertyType::LazyObject:
        case PropertyType::SoftObject:
        case PropertyType::Interface:
        case PropertyType::Class:
            json += ",\"class\":";
            appendName(node.name);
            if (const auto metaClass = schema.ir.metaClasses.find(index); metaClass != schema.ir.metaClasses.end()) {
                json += ",\"metaClass\":";
                appendName(metaClass->second);
            }
            break;
        //! FSoftClassPropertyParams names only the class of TSoftClassPtr.
        case PropertyType::SoftClass:
            json += ",\"metaClass\":";
            appendName(node.name);
            break;
        default:
            break;
        }
        json += '}';
        return at;
    }

    //! The usmap entries that share a static array of more than 255 elements are one property again.
    void appendProperties(const std::span<const Property> properties, const std::vector<std::string>& propertyNames) {
        json += '[';
        for (std::size_t at = 0; at < properties.size();) {
            const Property& property = properties[at];
            std::uint32_t arrayDim = 0;
            for (; at < properties.size() && properties[at].name == property.name && properties[at].firstType == property.firstType; at++)
                arrayDim += properties[at].arrayDim;
            json += &property == properties.data() ? "\n  {\"name\":" : ",\n  {\"name\":";
            appendName(property.name, propertyNames);
            json += std::format(",\"arrayDim\":{},\"flags\":{},\"type\":", arrayDim, property.flags);
            appendType(property.firstType);
            json += '}';
        }
        json += ']';
    }

    void appendStruct(const Struct& entry, const std::uint32_t package) {
        json += "{\"name\":";
        appendName(entry.name);
        json += ",\"package\":";
        appendPackage(package);
        json += ",\"super\":";
        appendName(entry.super);
        json += ",\"properties\":";
        appendProperties(std::span(schema.properties).subspan(entry.firstProperty, entry.propertyCount), names);
    }

    void appendFunction(const Function& function) {
        json += "\n {\"name\":";
        const std::vector<std::string>& functionNames = schema.ir.names.getNames();
        appendName(function.name, functionNames);
        json += std::format(",\"flags\":{},\"parameters\":", function.flags);
        appendProperties(std::span(schema.ir.parameters).subspan(function.firstParameter, function.parameterCount), functionNames);
        json += '}';
    }

    //! A value is written as the signed 64-bit integer that UEnum stores.
    void appendEnum(const Enum& entry, const std::uint32_t package) {
        json += "{\"name\":";
        appendName(entry.name);
        json += ",\"package\":";
        appendPackage(package);
        json += ",\"values\":[";
        const std::span<const EnumValue> values = std::span(schema.enumValues).subspan(entry.firstValue, entry.valueCount);
        for (const EnumValue& value : values) {
            json += &value == values.data() ? "{\"name\":" : ",{\"name\":";
            appendName(value.name);
            json += std::format(",\"value\":{}}}", std::int64_t(value.value));
        }
        json += "]}";
    }

    const Schema& schema;
    const std::vector<std::string>& names;
    std::string json;
};

}

std::string writeIr(const Schema& schema) {
    return IrWriter(schema).write();
}

}
