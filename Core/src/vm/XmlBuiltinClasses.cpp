#include "phoneme/vm/BuiltinClassRegistry.hpp"

#include <string_view>

#include "BuiltinClassSupport.hpp"

namespace phoneme::vm {
namespace {

using namespace builtin;

[[nodiscard]] BuiltinClassRegistry::ClassPtr build_xml_class(
    std::string_view name) {
    const u16 api = kPublic | kAbstract;

    if (name == "javax/xml/parsers/ParserConfigurationException") {
        return make_class(std::string(name), "java/lang/Exception", kOrdinary,
                          {}, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "<init>", "(Ljava/lang/String;)V"),
        });
    }
    if (name == "javax/xml/parsers/FactoryConfigurationError") {
        return make_class(std::string(name), "java/lang/Error", kOrdinary, {
            field(kPrivate, "exception", "Ljava/lang/Exception;"),
        }, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "<init>", "(Ljava/lang/Exception;)V"),
            method(kPublic, "<init>", "(Ljava/lang/Exception;Ljava/lang/String;)V"),
            method(kPublic, "<init>", "(Ljava/lang/String;)V"),
            method(kPublic, "getException", "()Ljava/lang/Exception;"),
            method(kPublic, "getMessage", "()Ljava/lang/String;"),
        });
    }
    if (name == "javax/xml/parsers/SAXParserFactory") {
        return make_class(std::string(name), "java/lang/Object",
                          kPublic | kAbstract | kSuper, {
            field(kPrivate, "namespaceAware", "I"),
            field(kPrivate, "validating", "I"),
        }, {
            method(kProtected, "<init>", "()V"),
            method(kPublic | kStatic, "newInstance",
                   "()Ljavax/xml/parsers/SAXParserFactory;"),
            method(api, "newSAXParser", "()Ljavax/xml/parsers/SAXParser;"),
            method(kPublic, "setNamespaceAware", "(Z)V"),
            method(kPublic, "setValidating", "(Z)V"),
            method(kPublic, "isNamespaceAware", "()Z"),
            method(kPublic, "isValidating", "()Z"),
            method(api, "setFeature", "(Ljava/lang/String;Z)V"),
            method(api, "getFeature", "(Ljava/lang/String;)Z"),
        });
    }
    if (name == "phoneme/xml/SAXParserFactoryImpl") {
        return make_class(std::string(name),
                          "javax/xml/parsers/SAXParserFactory",
                          kOrdinary | kFinal, {}, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "newSAXParser", "()Ljavax/xml/parsers/SAXParser;"),
            method(kPublic, "setFeature", "(Ljava/lang/String;Z)V"),
            method(kPublic, "getFeature", "(Ljava/lang/String;)Z"),
        });
    }
    if (name == "javax/xml/parsers/SAXParser") {
        return make_class(std::string(name), "java/lang/Object",
                          kPublic | kAbstract | kSuper, {}, {
            method(kProtected, "<init>", "()V"),
            method(api, "getParser", "()Lorg/xml/sax/Parser;"),
            method(api, "getXMLReader", "()Lorg/xml/sax/XMLReader;"),
            method(api, "isNamespaceAware", "()Z"),
            method(api, "isValidating", "()Z"),
            method(api, "setProperty", "(Ljava/lang/String;Ljava/lang/Object;)V"),
            method(api, "getProperty", "(Ljava/lang/String;)Ljava/lang/Object;"),
            method(kPublic, "parse",
                   "(Lorg/xml/sax/InputSource;Lorg/xml/sax/helpers/DefaultHandler;)V"),
            method(kPublic, "parse",
                   "(Ljava/io/InputStream;Lorg/xml/sax/helpers/DefaultHandler;)V"),
            method(kPublic, "parse",
                   "(Ljava/io/InputStream;Lorg/xml/sax/helpers/DefaultHandler;Ljava/lang/String;)V"),
            method(kPublic, "parse",
                   "(Ljava/lang/String;Lorg/xml/sax/helpers/DefaultHandler;)V"),
        });
    }
    if (name == "phoneme/xml/SAXParserImpl") {
        return make_class(std::string(name), "javax/xml/parsers/SAXParser",
                          kOrdinary | kFinal, {
            field(kPrivate, "namespaceAware", "I"),
            field(kPrivate, "validating", "I"),
            field(kPrivate, "entityResolver", "Lorg/xml/sax/EntityResolver;"),
            field(kPrivate, "dtdHandler", "Lorg/xml/sax/DTDHandler;"),
            field(kPrivate, "contentHandler", "Lorg/xml/sax/ContentHandler;"),
            field(kPrivate, "errorHandler", "Lorg/xml/sax/ErrorHandler;"),
            field(kPrivate, "documentHandler", "Lorg/xml/sax/DocumentHandler;"),
        }, {
            method(kPublic, "<init>", "(ZZ)V"),
            method(kPublic, "getParser", "()Lorg/xml/sax/Parser;"),
            method(kPublic, "getXMLReader", "()Lorg/xml/sax/XMLReader;"),
            method(kPublic, "isNamespaceAware", "()Z"),
            method(kPublic, "isValidating", "()Z"),
            method(kPublic, "setProperty", "(Ljava/lang/String;Ljava/lang/Object;)V"),
            method(kPublic, "getProperty", "(Ljava/lang/String;)Ljava/lang/Object;"),
            method(kPublic, "getFeature", "(Ljava/lang/String;)Z"),
            method(kPublic, "setFeature", "(Ljava/lang/String;Z)V"),
            method(kPublic, "getEntityResolver", "()Lorg/xml/sax/EntityResolver;"),
            method(kPublic, "setEntityResolver", "(Lorg/xml/sax/EntityResolver;)V"),
            method(kPublic, "getDTDHandler", "()Lorg/xml/sax/DTDHandler;"),
            method(kPublic, "setDTDHandler", "(Lorg/xml/sax/DTDHandler;)V"),
            method(kPublic, "getContentHandler", "()Lorg/xml/sax/ContentHandler;"),
            method(kPublic, "setContentHandler", "(Lorg/xml/sax/ContentHandler;)V"),
            method(kPublic, "getErrorHandler", "()Lorg/xml/sax/ErrorHandler;"),
            method(kPublic, "setErrorHandler", "(Lorg/xml/sax/ErrorHandler;)V"),
            method(kPublic, "setLocale", "(Ljava/util/Locale;)V"),
            method(kPublic, "setDocumentHandler", "(Lorg/xml/sax/DocumentHandler;)V"),
            method(kPublic, "parse", "(Lorg/xml/sax/InputSource;)V"),
            method(kPublic, "parse", "(Ljava/lang/String;)V"),
        }, {"org/xml/sax/XMLReader", "org/xml/sax/Parser"});
    }

    if (name == "org/xml/sax/AttributeList") {
        return make_class(std::string(name), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(api, "getLength", "()I"),
            method(api, "getName", "(I)Ljava/lang/String;"),
            method(api, "getType", "(I)Ljava/lang/String;"),
            method(api, "getValue", "(I)Ljava/lang/String;"),
            method(api, "getType", "(Ljava/lang/String;)Ljava/lang/String;"),
            method(api, "getValue", "(Ljava/lang/String;)Ljava/lang/String;"),
        });
    }
    if (name == "org/xml/sax/Attributes") {
        return make_class(std::string(name), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(api, "getLength", "()I"),
            method(api, "getURI", "(I)Ljava/lang/String;"),
            method(api, "getLocalName", "(I)Ljava/lang/String;"),
            method(api, "getQName", "(I)Ljava/lang/String;"),
            method(api, "getType", "(I)Ljava/lang/String;"),
            method(api, "getValue", "(I)Ljava/lang/String;"),
            method(api, "getIndex", "(Ljava/lang/String;Ljava/lang/String;)I"),
            method(api, "getIndex", "(Ljava/lang/String;)I"),
            method(api, "getType", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;"),
            method(api, "getType", "(Ljava/lang/String;)Ljava/lang/String;"),
            method(api, "getValue", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;"),
            method(api, "getValue", "(Ljava/lang/String;)Ljava/lang/String;"),
        });
    }
    if (name == "org/xml/sax/ContentHandler") {
        return make_class(std::string(name), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(api, "setDocumentLocator", "(Lorg/xml/sax/Locator;)V"),
            method(api, "startDocument", "()V"),
            method(api, "endDocument", "()V"),
            method(api, "startPrefixMapping", "(Ljava/lang/String;Ljava/lang/String;)V"),
            method(api, "endPrefixMapping", "(Ljava/lang/String;)V"),
            method(api, "startElement", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Lorg/xml/sax/Attributes;)V"),
            method(api, "endElement", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V"),
            method(api, "characters", "([CII)V"),
            method(api, "ignorableWhitespace", "([CII)V"),
            method(api, "processingInstruction", "(Ljava/lang/String;Ljava/lang/String;)V"),
            method(api, "skippedEntity", "(Ljava/lang/String;)V"),
        });
    }
    if (name == "org/xml/sax/DocumentHandler") {
        return make_class(std::string(name), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(api, "setDocumentLocator", "(Lorg/xml/sax/Locator;)V"),
            method(api, "startDocument", "()V"),
            method(api, "endDocument", "()V"),
            method(api, "startElement", "(Ljava/lang/String;Lorg/xml/sax/AttributeList;)V"),
            method(api, "endElement", "(Ljava/lang/String;)V"),
            method(api, "characters", "([CII)V"),
            method(api, "ignorableWhitespace", "([CII)V"),
            method(api, "processingInstruction", "(Ljava/lang/String;Ljava/lang/String;)V"),
        });
    }
    if (name == "org/xml/sax/DTDHandler") {
        return make_class(std::string(name), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(api, "notationDecl", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V"),
            method(api, "unparsedEntityDecl", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V"),
        });
    }
    if (name == "org/xml/sax/EntityResolver") {
        return make_class(std::string(name), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(api, "resolveEntity", "(Ljava/lang/String;Ljava/lang/String;)Lorg/xml/sax/InputSource;"),
        });
    }
    if (name == "org/xml/sax/ErrorHandler") {
        return make_class(std::string(name), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(api, "warning", "(Lorg/xml/sax/SAXParseException;)V"),
            method(api, "error", "(Lorg/xml/sax/SAXParseException;)V"),
            method(api, "fatalError", "(Lorg/xml/sax/SAXParseException;)V"),
        });
    }
    if (name == "org/xml/sax/Locator") {
        return make_class(std::string(name), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(api, "getPublicId", "()Ljava/lang/String;"),
            method(api, "getSystemId", "()Ljava/lang/String;"),
            method(api, "getLineNumber", "()I"),
            method(api, "getColumnNumber", "()I"),
        });
    }
    if (name == "org/xml/sax/Parser") {
        return make_class(std::string(name), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(api, "setLocale", "(Ljava/util/Locale;)V"),
            method(api, "setEntityResolver", "(Lorg/xml/sax/EntityResolver;)V"),
            method(api, "setDTDHandler", "(Lorg/xml/sax/DTDHandler;)V"),
            method(api, "setDocumentHandler", "(Lorg/xml/sax/DocumentHandler;)V"),
            method(api, "setErrorHandler", "(Lorg/xml/sax/ErrorHandler;)V"),
            method(api, "parse", "(Lorg/xml/sax/InputSource;)V"),
            method(api, "parse", "(Ljava/lang/String;)V"),
        });
    }
    if (name == "org/xml/sax/XMLReader") {
        return make_class(std::string(name), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(api, "getFeature", "(Ljava/lang/String;)Z"),
            method(api, "setFeature", "(Ljava/lang/String;Z)V"),
            method(api, "getProperty", "(Ljava/lang/String;)Ljava/lang/Object;"),
            method(api, "setProperty", "(Ljava/lang/String;Ljava/lang/Object;)V"),
            method(api, "setEntityResolver", "(Lorg/xml/sax/EntityResolver;)V"),
            method(api, "getEntityResolver", "()Lorg/xml/sax/EntityResolver;"),
            method(api, "setDTDHandler", "(Lorg/xml/sax/DTDHandler;)V"),
            method(api, "getDTDHandler", "()Lorg/xml/sax/DTDHandler;"),
            method(api, "setContentHandler", "(Lorg/xml/sax/ContentHandler;)V"),
            method(api, "getContentHandler", "()Lorg/xml/sax/ContentHandler;"),
            method(api, "setErrorHandler", "(Lorg/xml/sax/ErrorHandler;)V"),
            method(api, "getErrorHandler", "()Lorg/xml/sax/ErrorHandler;"),
            method(api, "parse", "(Lorg/xml/sax/InputSource;)V"),
            method(api, "parse", "(Ljava/lang/String;)V"),
        });
    }

    if (name == "org/xml/sax/InputSource") {
        return make_class(std::string(name), "java/lang/Object", kOrdinary, {
            field(kPrivate, "publicId", "Ljava/lang/String;"),
            field(kPrivate, "systemId", "Ljava/lang/String;"),
            field(kPrivate, "byteStream", "Ljava/io/InputStream;"),
            field(kPrivate, "characterStream", "Ljava/io/Reader;"),
            field(kPrivate, "encoding", "Ljava/lang/String;"),
        }, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "<init>", "(Ljava/lang/String;)V"),
            method(kPublic, "<init>", "(Ljava/io/InputStream;)V"),
            method(kPublic, "<init>", "(Ljava/io/Reader;)V"),
            method(kPublic, "setPublicId", "(Ljava/lang/String;)V"),
            method(kPublic, "getPublicId", "()Ljava/lang/String;"),
            method(kPublic, "setSystemId", "(Ljava/lang/String;)V"),
            method(kPublic, "getSystemId", "()Ljava/lang/String;"),
            method(kPublic, "setByteStream", "(Ljava/io/InputStream;)V"),
            method(kPublic, "getByteStream", "()Ljava/io/InputStream;"),
            method(kPublic, "setEncoding", "(Ljava/lang/String;)V"),
            method(kPublic, "getEncoding", "()Ljava/lang/String;"),
            method(kPublic, "setCharacterStream", "(Ljava/io/Reader;)V"),
            method(kPublic, "getCharacterStream", "()Ljava/io/Reader;"),
        });
    }
    if (name == "org/xml/sax/SAXException") {
        return make_class(std::string(name), "java/lang/Exception", kOrdinary, {
            field(kPrivate, "exception", "Ljava/lang/Exception;"),
        }, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "<init>", "(Ljava/lang/String;)V"),
            method(kPublic, "<init>", "(Ljava/lang/Exception;)V"),
            method(kPublic, "<init>", "(Ljava/lang/String;Ljava/lang/Exception;)V"),
            method(kPublic, "getMessage", "()Ljava/lang/String;"),
            method(kPublic, "getException", "()Ljava/lang/Exception;"),
            method(kPublic, "toString", "()Ljava/lang/String;"),
        });
    }
    if (name == "org/xml/sax/SAXNotRecognizedException" ||
        name == "org/xml/sax/SAXNotSupportedException") {
        return make_class(std::string(name), "org/xml/sax/SAXException",
                          kOrdinary, {}, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "<init>", "(Ljava/lang/String;)V"),
        });
    }
    if (name == "org/xml/sax/SAXParseException") {
        return make_class(std::string(name), "org/xml/sax/SAXException",
                          kOrdinary, {
            field(kPrivate, "publicId", "Ljava/lang/String;"),
            field(kPrivate, "systemId", "Ljava/lang/String;"),
            field(kPrivate, "lineNumber", "I"),
            field(kPrivate, "columnNumber", "I"),
        }, {
            method(kPublic, "<init>", "(Ljava/lang/String;Lorg/xml/sax/Locator;)V"),
            method(kPublic, "<init>", "(Ljava/lang/String;Lorg/xml/sax/Locator;Ljava/lang/Exception;)V"),
            method(kPublic, "<init>", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;II)V"),
            method(kPublic, "<init>", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;IILjava/lang/Exception;)V"),
            method(kPublic, "getPublicId", "()Ljava/lang/String;"),
            method(kPublic, "getSystemId", "()Ljava/lang/String;"),
            method(kPublic, "getLineNumber", "()I"),
            method(kPublic, "getColumnNumber", "()I"),
        });
    }
    if (name == "org/xml/sax/helpers/DefaultHandler") {
        return make_class(std::string(name), "java/lang/Object", kOrdinary, {}, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "resolveEntity", "(Ljava/lang/String;Ljava/lang/String;)Lorg/xml/sax/InputSource;"),
            method(kPublic, "notationDecl", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V"),
            method(kPublic, "unparsedEntityDecl", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V"),
            method(kPublic, "setDocumentLocator", "(Lorg/xml/sax/Locator;)V"),
            method(kPublic, "startDocument", "()V"),
            method(kPublic, "endDocument", "()V"),
            method(kPublic, "startPrefixMapping", "(Ljava/lang/String;Ljava/lang/String;)V"),
            method(kPublic, "endPrefixMapping", "(Ljava/lang/String;)V"),
            method(kPublic, "startElement", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Lorg/xml/sax/Attributes;)V"),
            method(kPublic, "endElement", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V"),
            method(kPublic, "characters", "([CII)V"),
            method(kPublic, "ignorableWhitespace", "([CII)V"),
            method(kPublic, "processingInstruction", "(Ljava/lang/String;Ljava/lang/String;)V"),
            method(kPublic, "skippedEntity", "(Ljava/lang/String;)V"),
            method(kPublic, "warning", "(Lorg/xml/sax/SAXParseException;)V"),
            method(kPublic, "error", "(Lorg/xml/sax/SAXParseException;)V"),
            method(kPublic, "fatalError", "(Lorg/xml/sax/SAXParseException;)V"),
        }, {"org/xml/sax/EntityResolver", "org/xml/sax/DTDHandler",
            "org/xml/sax/ContentHandler", "org/xml/sax/ErrorHandler"});
    }
    if (name == "org/xml/sax/helpers/AttributesImpl") {
        return make_class(std::string(name), "java/lang/Object", kOrdinary, {
            field(kPrivate, "uris", "[Ljava/lang/String;"),
            field(kPrivate, "localNames", "[Ljava/lang/String;"),
            field(kPrivate, "qNames", "[Ljava/lang/String;"),
            field(kPrivate, "types", "[Ljava/lang/String;"),
            field(kPrivate, "values", "[Ljava/lang/String;"),
            field(kPrivate, "length", "I"),
        }, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "<init>", "(Lorg/xml/sax/Attributes;)V"),
            method(kPublic, "getLength", "()I"),
            method(kPublic, "getURI", "(I)Ljava/lang/String;"),
            method(kPublic, "getLocalName", "(I)Ljava/lang/String;"),
            method(kPublic, "getQName", "(I)Ljava/lang/String;"),
            method(kPublic, "getType", "(I)Ljava/lang/String;"),
            method(kPublic, "getValue", "(I)Ljava/lang/String;"),
            method(kPublic, "getIndex", "(Ljava/lang/String;Ljava/lang/String;)I"),
            method(kPublic, "getIndex", "(Ljava/lang/String;)I"),
            method(kPublic, "getType", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;"),
            method(kPublic, "getType", "(Ljava/lang/String;)Ljava/lang/String;"),
            method(kPublic, "getValue", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;"),
            method(kPublic, "getValue", "(Ljava/lang/String;)Ljava/lang/String;"),
            method(kPublic, "clear", "()V"),
            method(kPublic, "setAttributes", "(Lorg/xml/sax/Attributes;)V"),
            method(kPublic, "addAttribute", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V"),
            method(kPublic, "setAttribute", "(ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V"),
            method(kPublic, "removeAttribute", "(I)V"),
            method(kPublic, "setURI", "(ILjava/lang/String;)V"),
            method(kPublic, "setLocalName", "(ILjava/lang/String;)V"),
            method(kPublic, "setQName", "(ILjava/lang/String;)V"),
            method(kPublic, "setType", "(ILjava/lang/String;)V"),
            method(kPublic, "setValue", "(ILjava/lang/String;)V"),
        }, {"org/xml/sax/Attributes"});
    }
    if (name == "org/xml/sax/helpers/XMLReaderFactory") {
        return make_class(std::string(name), "java/lang/Object",
                          kOrdinary | kFinal, {}, {
            method(kPrivate, "<init>", "()V"),
            method(kPublic | kStatic, "createXMLReader", "()Lorg/xml/sax/XMLReader;"),
            method(kPublic | kStatic, "createXMLReader", "(Ljava/lang/String;)Lorg/xml/sax/XMLReader;"),
        });
    }

    return nullptr;
}

} // namespace

void register_xml_classes(BuiltinClassRegistry& registry) {
    registry.add_factory(build_xml_class);
}

} // namespace phoneme::vm
