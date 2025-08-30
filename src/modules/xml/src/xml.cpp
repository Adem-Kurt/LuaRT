#include <luart.h>
#include "Node.h"
#include "XMLHandler.h"
#include "XMLNode.h"

//---------------------------------------------| xml.load() function
LUA_METHOD(xml, load) {
    std::wstring fname = luaL_checkFilename(L, 1);
    auto node = XmlHandler::ReadXml(fname);

    if (node)
        lua_pushNode(L, node);
    else
        lua_pushnil(L);
    return 1;
}

//---------------------------------------------| xml.save() function
LUA_METHOD(xml, save) {
    Node *n = lua_self(L, 1, Node);
    std::wstring fname = luaL_checkFilename(L, 2);

    lua_pushboolean(L, XmlHandler::WriteXml(fname, n->xmlnode.get()));
    return 1;
}

//---------------------------------------------| xml.encode() function
LUA_METHOD(xml, encode) {
    wchar_t *str = lua_towstring(L, 1);
    auto node = XmlHandler::ReadString(str);

    if (node)
        lua_pushNode(L, node);
    else
        lua_pushnil(L);
    return 1;
}

//---------------------------------------------| xml.decode() function
LUA_METHOD(xml, decode) {
    luaL_tolstring(L, 1, NULL);
    return 1;
}

//---------------------------------------------| xml.error property
LUA_PROPERTY_GET(xml, error) {
    std::wstring err = XmlHandler::GetErrorMessage();
    if (err.empty())
        lua_pushnil(L);
    else lua_pushwstring(L, err.c_str());
    return 1;
}

MODULE_PROPERTIES(xml)
    READONLY_PROPERTY(xml, error)
END

MODULE_FUNCTIONS(xml)
    METHOD(xml, load)
    METHOD(xml, save)
    METHOD(xml, encode)
    METHOD(xml, decode)
END

//----- "serial" module registration function
extern "C" {
    int __declspec(dllexport) luaopen_xml(lua_State *L)
    {
        lua_regmodule(L, xml);
        lua_regobjectmt(L, Node);
        return 1;
    }
}