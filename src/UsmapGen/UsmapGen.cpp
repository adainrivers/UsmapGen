#include "UsmapGen/UsmapGen.h"

#include "UsmapGen/Image.h"
#include "UsmapGen/Reflection.h"
#include "UsmapGen/Schema.h"

namespace UsmapGen {

namespace {

std::expected<Schema, Error> readSchema(const Source& executable, const bool withIr = false) {
    Image image(executable);
    if (std::expected<void, Error> opened = image.open(); !opened)
        return std::unexpected(opened.error());
    return readReflection(image, withIr);
}

}

std::expected<std::vector<std::byte>, Error> generateUsmap(const Source& executable) {
    return readSchema(executable).transform(writeUsmap);
}

std::expected<Mappings, Error> generateMappings(const Source& executable, const Options& options) {
    return readSchema(executable, options.ir).transform([&](const Schema& schema) {
        return Mappings{writeUsmap(schema), writeMapStructTypes(schema), options.ir ? writeIr(schema) : std::string()};
    });
}

}
