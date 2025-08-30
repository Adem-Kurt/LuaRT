#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>

class XmlNode : public std::enable_shared_from_this<XmlNode> {
public:
    using Ptr      = std::shared_ptr<XmlNode>;
    using WPtr     = std::weak_ptr<XmlNode>;
    using Children = std::vector<Ptr>;
    using Attrs    = std::map<std::wstring, std::wstring>;
    using Iterator = Children::iterator;

    std::wstring name;
    std::wstring text;
    WPtr         parent;
    Children     children;
    Attrs        attributes;

    XmlNode() = default;
    explicit XmlNode(const std::wstring& n) : name(n) {}

    void AddChild(const Ptr& child);
    void SetParent(const Ptr& p);
    std::shared_ptr<XmlNode> Clone() const;
    void SetAttribute(const std::wstring& key, const wchar_t* value);
    const wchar_t* GetAttribute(const std::wstring& key) const;
    std::vector<std::shared_ptr<XmlNode>> XmlNode::QueryXPath(const std::wstring& xpath);

    size_t Count() const noexcept { return children.size(); }
    std::wstring AsString() const;

    Iterator begin() { return children.begin(); }
    Iterator end()   { return children.end();   }

private:
    static std::wstring EscapeText(const std::wstring& s);
    static std::wstring EscapeAttr(const std::wstring& s);
};