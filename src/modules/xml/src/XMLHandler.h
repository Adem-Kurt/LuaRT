#pragma once
#include <msxml6.h>
#include <comdef.h>
#include <string>
#include <memory>

class XmlNode;

class XmlHandler {
public:
    static std::shared_ptr<XmlNode> ReadXml(const std::wstring& filename);
    static std::shared_ptr<XmlNode> ReadString(const std::wstring& xml);
    static bool WriteXml(const std::wstring& filename, XmlNode* root);

    static std::wstring GetErrorMessage();

private:
    static void SetError(const std::wstring& msg);
    static void ClearError();
};

std::shared_ptr<XmlNode> FromDomElement(IXMLDOMNode* domNode);