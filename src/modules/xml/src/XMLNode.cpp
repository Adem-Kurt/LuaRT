#include "XMLNode.h"
#include "XMLHandler.h"
#include <sstream>
#include <msxml6.h>
#include <comdef.h>

void XmlNode::AddChild(const Ptr& child) {
    if (!child) return;
    child->parent = shared_from_this();
    children.push_back(child);
}

void XmlNode::SetParent(const Ptr& p) {
    parent = p;
}

void XmlNode::SetAttribute(const std::wstring& key, const wchar_t* value) {
    if (!value) {
        attributes.erase(key);
    } else {
        attributes[key] = value;
    }
}

const wchar_t* XmlNode::GetAttribute(const std::wstring& key) const {
    auto it = attributes.find(key);
    return (it != attributes.end()) ? it->second.c_str() : nullptr;
}

std::wstring XmlNode::EscapeText(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (wchar_t c : s) {
        switch (c) {
            case L'&': out += L"&amp;";  break;
            case L'<': out += L"&lt;";   break;
            case L'>': out += L"&gt;";   break;
            default:   out.push_back(c); break;
        }
    }
    return out;
}

std::wstring XmlNode::EscapeAttr(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (wchar_t c : s) {
        switch (c) {
            case L'&': out += L"&amp;";  break;
            case L'<': out += L"&lt;";   break;
            case L'>': out += L"&gt;";   break;
            case L'"': out += L"&quot;"; break;
            case L'\'':out += L"&apos;"; break;
            default:   out.push_back(c); break;
        }
    }
    return out;
}

std::wstring XmlNode::AsString() const {
    std::wostringstream ss;
    ss << L"<" << name;

    for (const auto& kv : attributes) {
        ss << L" " << kv.first << L"=\"" << EscapeAttr(kv.second) << L"\"";
    }

    const bool hasChildren = !children.empty();
    const bool hasText     = !text.empty();

    if (!hasChildren && !hasText) {
        ss << L"/>";
        return ss.str();
    }

    ss << L">";

    if (hasText) {
        ss << EscapeText(text);
    }
    for (const auto& ch : children) {
        ss << ch->AsString();
    }

    ss << L"</" << name << L">";
    return ss.str();
}

std::shared_ptr<XmlNode> XmlNode::Clone() const {
  auto copy = std::make_shared<XmlNode>(name);
  copy->text = text;
  copy->attributes = attributes;
  for (const auto& child : children)
    copy->AddChild(child->Clone());
  return copy;
}

std::vector<std::shared_ptr<XmlNode>> XmlNode::QueryXPath(const std::wstring& xpath) {
    std::vector<std::shared_ptr<XmlNode>> results;
    std::wstring xml = AsString();

    try {
        IXMLDOMDocumentPtr doc;
        HRESULT hr = doc.CreateInstance(__uuidof(DOMDocument60));
        if (FAILED(hr))
            return results;
        doc->put_async(VARIANT_FALSE);  
        VARIANT_BOOL ok = VARIANT_FALSE;
        hr = doc->loadXML(_bstr_t(xml.c_str()), &ok);
        if (FAILED(hr) || ok == VARIANT_FALSE)
            return results;
        IXMLDOMNodeListPtr nodes;
        hr = doc->selectNodes(_bstr_t(xpath.c_str()), &nodes);
        if (FAILED(hr) || !nodes)
            return results;

        long len = 0;
        hr = nodes->get_length(&len);
        if (FAILED(hr) || len == 0)
            return results;

        for (long i = 0; i < len; ++i) {
            IXMLDOMNodePtr node;
            hr = nodes->get_item(i, &node);
            if (SUCCEEDED(hr) && node) {
                auto xmlnode = FromDomElement(node);
                if (xmlnode)
                    results.push_back(xmlnode);
            }
        }

    } catch (...) {}

    return results;
}