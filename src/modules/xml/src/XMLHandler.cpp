#include "XMLHandler.h"
#include "XMLNode.h"

#include <windows.h>
#include <msxml6.h>
#include <comdef.h>
#include <string>
#include <vector>

#pragma comment(lib, "Msxml6.lib")

std::shared_ptr<XmlNode> FromDomElement(IXMLDOMNode* domNode) {
    if (!domNode) return nullptr;

    DOMNodeType nt;
    if (FAILED(domNode->get_nodeType(&nt))) return nullptr;
    if (nt != NODE_ELEMENT) return nullptr;

    BSTR bname = nullptr;
    if (FAILED(domNode->get_nodeName(&bname))) return nullptr;
    std::shared_ptr<XmlNode> node = std::make_shared<XmlNode>(std::wstring(bname ? bname : L""));
    if (bname) SysFreeString(bname);

    // Attributes
    IXMLDOMNamedNodeMap* attrs = nullptr;
    if (SUCCEEDED(domNode->get_attributes(&attrs)) && attrs) {
        long len = 0;
        attrs->get_length(&len);
        for (long i = 0; i < len; ++i) {
            IXMLDOMNode* attr = nullptr;
            if (SUCCEEDED(attrs->get_item(i, &attr)) && attr) {
                BSTR aname = nullptr;
                VARIANT val; VariantInit(&val);

                if (SUCCEEDED(attr->get_nodeName(&aname)) &&
                    SUCCEEDED(attr->get_nodeValue(&val))) {
                    if (val.vt == VT_BSTR && aname) {
                        node->attributes[aname] = val.bstrVal ? val.bstrVal : L"";
                    }
                }

                if (aname) SysFreeString(aname);
                VariantClear(&val);
                attr->Release();
            }
        }
        attrs->Release();
    }

    // Children and text
    IXMLDOMNodeList* list = nullptr;
    if (SUCCEEDED(domNode->get_childNodes(&list)) && list) {
        long len = 0;
        list->get_length(&len);
        for (long i = 0; i < len; ++i) {
            IXMLDOMNode* child = nullptr;
            if (SUCCEEDED(list->get_item(i, &child)) && child) {
                DOMNodeType ct;
                if (SUCCEEDED(child->get_nodeType(&ct))) {
                    if (ct == NODE_ELEMENT) {
                        auto cnode = FromDomElement(child);
                        if (cnode) node->AddChild(cnode);
                    } else if (ct == NODE_TEXT || ct == NODE_CDATA_SECTION) {
                        BSTR t = nullptr;
                        if (SUCCEEDED(child->get_text(&t))) {
                            if (t) {
                                node->text.append(t, SysStringLen(t));
                                SysFreeString(t);
                            }
                        }
                    }
                }
                child->Release();
            }
        }
        list->Release();
    }

    return node;
}

namespace {

static std::wstring lastError;

void SetLastErrorMsg(HRESULT hr, const wchar_t* context = nullptr) {
    _com_error err(hr);
    std::wstring msg = context ? std::wstring(context) + L": " : L"";
    msg += std::wstring(err.ErrorMessage());
    lastError = msg;
}

void SetLastErrorMsg(const std::wstring& msg) {
    lastError = msg;
}

void ClearLastError() {
    lastError.clear();
}

HRESULT BuildDomFromXmlNode(IXMLDOMDocument* doc, XmlNode* src, IXMLDOMElement** outElem) {
    if (!doc || !src || !outElem) return E_POINTER;
    *outElem = nullptr;

    IXMLDOMElement* elem = nullptr;
    HRESULT hr = doc->createElement(_bstr_t(src->name.c_str()), &elem);
    if (FAILED(hr) || !elem) return hr;

    // Attributes
    for (const auto& kv : src->attributes) {
        VARIANT v; VariantInit(&v);
        v.vt = VT_BSTR;
        v.bstrVal = SysAllocString(kv.second.c_str());
        hr = elem->setAttribute(_bstr_t(kv.first.c_str()), v);
        VariantClear(&v);
        if (FAILED(hr)) { elem->Release(); return hr; }
    }

    // Text
    if (!src->text.empty()) {
        IXMLDOMText* txt = nullptr;
        hr = doc->createTextNode(_bstr_t(src->text.c_str()), &txt);
        if (FAILED(hr) || !txt) { elem->Release(); return FAILED(hr) ? hr : E_FAIL; }

        IXMLDOMNode* appended = nullptr;
        hr = elem->appendChild(txt, &appended);
        if (appended) appended->Release();
        txt->Release();
        if (FAILED(hr)) { elem->Release(); return hr; }
    }

    // Children
    for (const auto& ch : src->children) {
        IXMLDOMElement* ce = nullptr;
        hr = BuildDomFromXmlNode(doc, ch.get(), &ce);
        if (FAILED(hr) || !ce) { elem->Release(); return FAILED(hr) ? hr : E_FAIL; }
        IXMLDOMNode* appended = nullptr;
        hr = elem->appendChild(ce, &appended);
        if (appended) appended->Release();
        ce->Release();
        if (FAILED(hr)) { elem->Release(); return hr; }
    }

    *outElem = elem;
    return S_OK;
}

} // namespace

std::shared_ptr<XmlNode> XmlHandler::ReadXml(const std::wstring& filename) {
    ClearLastError();

    IXMLDOMDocument* doc = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_DOMDocument60, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&doc));
    if (FAILED(hr) || !doc) {
        SetLastErrorMsg(hr, L"MSXML DOMDocument creation failed");
        return nullptr;
    }

    VARIANT_BOOL ok = VARIANT_FALSE;
    doc->put_async(VARIANT_FALSE);
    hr = doc->load(_variant_t(filename.c_str()), &ok);
    if (FAILED(hr) || ok == VARIANT_FALSE) {
        IXMLDOMParseError* perr = nullptr;
        if (SUCCEEDED(doc->get_parseError(&perr)) && perr) {
            BSTR reason = nullptr;
            if (SUCCEEDED(perr->get_reason(&reason)) && reason) {
                SetLastErrorMsg(reason);
                SysFreeString(reason);
            } else {
                SetLastErrorMsg(L"XML parse failed");
            }
            perr->Release();
        } else {
            SetLastErrorMsg(hr, L"XML load failed");
        }
        doc->Release();
        return nullptr;
    }

    IXMLDOMElement* root = nullptr;
    hr = doc->get_documentElement(&root);
    if (FAILED(hr) || !root) {
        SetLastErrorMsg(L"No document element");
        doc->Release();
        return nullptr;
    }

    auto node = FromDomElement(root);
    root->Release();
    doc->Release();

    if (!node) SetLastErrorMsg(L"Failed to convert DOM to XmlNode");
    return node;
}

std::shared_ptr<XmlNode> XmlHandler::ReadString(const std::wstring& xml) {
    ClearLastError();

    IXMLDOMDocument* doc = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_DOMDocument60, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&doc));
    if (FAILED(hr) || !doc) {
        SetLastErrorMsg(hr, L"MSXML DOMDocument creation failed");
        return nullptr;
    }

    VARIANT_BOOL ok = VARIANT_FALSE;
    doc->put_async(VARIANT_FALSE);
    hr = doc->loadXML(_bstr_t(xml.c_str()), &ok);
    if (FAILED(hr) || ok == VARIANT_FALSE) {
        IXMLDOMParseError* perr = nullptr;
        if (SUCCEEDED(doc->get_parseError(&perr)) && perr) {
            BSTR reason = nullptr;
            if (SUCCEEDED(perr->get_reason(&reason)) && reason) {
                SetLastErrorMsg(reason);
                SysFreeString(reason);
            } else {
                SetLastErrorMsg(L"XML parse failed");
            }
            perr->Release();
        } else {
            SetLastErrorMsg(hr, L"XML loadXML failed");
        }
        doc->Release();
        return nullptr;
    }

    IXMLDOMElement* root = nullptr;
    hr = doc->get_documentElement(&root);
    if (FAILED(hr) || !root) {
        SetLastErrorMsg(L"No document element");
        doc->Release();
        return nullptr;
    }

    auto node = FromDomElement(root);
    root->Release();
    doc->Release();

    if (!node) SetLastErrorMsg(L"Failed to convert DOM to XmlNode");
    return node;
}

bool XmlHandler::WriteXml(const std::wstring& filename, XmlNode* root) {
    ClearLastError();
    if (!root) {
        SetLastErrorMsg(L"Root node is null");
        return false;
    }

    IXMLDOMDocument* doc = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_DOMDocument60, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&doc));
    if (FAILED(hr) || !doc) {
        SetLastErrorMsg(hr, L"MSXML DOMDocument creation failed");
        return false;
    }

    IXMLDOMElement* rootElem = nullptr;
    hr = BuildDomFromXmlNode(doc, root, &rootElem);
    if (FAILED(hr) || !rootElem) {
        SetLastErrorMsg(hr, L"Failed to build DOM from XmlNode");
        if (rootElem) rootElem->Release();
        doc->Release();
        return false;
    }

    // Attach to document
    IXMLDOMNode* appended = nullptr;
    hr = doc->appendChild(rootElem, &appended);
    if (appended) appended->Release();
    rootElem->Release();
    if (FAILED(hr)) {
        SetLastErrorMsg(hr, L"Failed to append root to document");
        doc->Release();
        return false;
    }

    // Save
    VARIANT v; VariantInit(&v);
    v.vt = VT_BSTR;
    v.bstrVal = SysAllocString(filename.c_str());
    hr = doc->save(v);
    VariantClear(&v);
    doc->Release();

    if (FAILED(hr)) {
        SetLastErrorMsg(hr, L"Failed to save XML");
        return false;
    }
    return true;
}

std::wstring XmlHandler::GetErrorMessage() {
    return lastError;
}

void XmlHandler::SetError(const std::wstring& msg) {
    SetLastErrorMsg(msg);
}

void XmlHandler::ClearError() {
    ClearLastError();
}